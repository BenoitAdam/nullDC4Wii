#include "wii/dcnet.h"

#include <network.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * DCNet transport for the future virtual BBA device.
 *
 * All socket activity stays on the worker thread. The SH-4/emulation thread
 * only puts completed Ethernet frames into s_txQueue and removes completed
 * frames from s_rxQueue. The fixed queues and mutex keep networking from
 * allocating memory or waiting for socket I/O during emulation.
 */

namespace
{
static const char kRelayHost[] = "dcnet.flyca.st";
static const u16 kRelayPort = 7655;
static const u16 kMaxEthernetFrame = 1514;
static const u32 kQueueSize = 12;
static const u32 kMaxAccessPoints = 8;
static const u32 kStreamBufferSize = 8192;

struct Frame
{
    u16 length;
    u8 data[kMaxEthernetFrame];
};

struct FrameQueue
{
    Frame frames[kQueueSize];
    u32 head;
    u32 tail;
    u32 count;
};

static mutex_t s_mutex = LWP_MUTEX_NULL;
static bool s_mutexReady = false;
static lwp_t s_thread = LWP_THREAD_NULL;
static bool s_threadCreated = false;
static bool s_stopRequested = false;
static s32 s_activeSocket = -1;
static int s_status = DCNET_STATUS_STOPPED;
static char s_lastError[160] = "Not connected";
static FrameQueue s_txQueue = {};
static FrameQueue s_rxQueue = {};

static void Lock()
{
    if (s_mutexReady)
        LWP_MutexLock(s_mutex);
}

static void Unlock()
{
    if (s_mutexReady)
        LWP_MutexUnlock(s_mutex);
}

static bool StopRequested()
{
    Lock();
    const bool stop = s_stopRequested;
    Unlock();
    return stop;
}

static void ClearQueue(FrameQueue* queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

static bool QueuePush(FrameQueue* queue, const u8* frame, u16 length)
{
    if (queue->count >= kQueueSize)
        return false;

    Frame* slot = &queue->frames[queue->tail];
    slot->length = length;
    memcpy(slot->data, frame, length);
    queue->tail = (queue->tail + 1) % kQueueSize;
    queue->count++;
    return true;
}

static bool QueuePop(FrameQueue* queue, Frame* out)
{
    if (queue->count == 0)
        return false;

    *out = queue->frames[queue->head];
    queue->head = (queue->head + 1) % kQueueSize;
    queue->count--;
    return true;
}

static void SetStatus(int status, const char* message)
{
    Lock();
    if (!(s_stopRequested && status != DCNET_STATUS_STOPPED))
    {
        s_status = status;
        if (message != nullptr)
        {
            strncpy(s_lastError, message, sizeof(s_lastError) - 1);
            s_lastError[sizeof(s_lastError) - 1] = '\0';
        }
    }
    Unlock();
}

static bool IsStoppingOrPublishSocket(s32 socket)
{
    Lock();
    if (s_stopRequested)
    {
        Unlock();
        net_close(socket);
        return false;
    }
    s_activeSocket = socket;
    Unlock();
    return true;
}

/* The worker owns the socket unless DCNet_Stop has already closed it. */
static void CloseWorkerSocket(s32 socket)
{
    bool closeIt = false;
    Lock();
    if (s_activeSocket == socket)
    {
        s_activeSocket = -1;
        closeIt = true;
    }
    Unlock();

    if (closeIt)
        net_close(socket);
}

static bool SendAll(s32 socket, const u8* data, u32 length)
{
    u32 sent = 0;
    while (sent < length && !StopRequested())
    {
        const s32 result = net_send(socket, data + sent, (s32)(length - sent), 0);
        if (result <= 0)
            return false;
        sent += (u32)result;
    }
    return sent == length;
}

static bool ResolveRelay(sockaddr_in* relay)
{
    hostent* host = net_gethostbyname(kRelayHost);
    if (host == nullptr || host->h_addr_list == nullptr ||
        host->h_addr_list[0] == nullptr)
        return false;

    memset(relay, 0, sizeof(*relay));
    relay->sin_family = AF_INET;
    relay->sin_port = htons(kRelayPort);
    memcpy(&relay->sin_addr.s_addr, host->h_addr_list[0],
           sizeof(relay->sin_addr.s_addr));
    return relay->sin_addr.s_addr != 0;
}

/*
 * DCNet's discovery datagrams have a four-byte magic, a message kind, then
 * optional payload. Encode the magic explicitly: the Wii CPU is big-endian,
 * while the relay wire representation is fixed byte-order.
 */
static void PutMagic(u8* packet)
{
    packet[0] = 0x01;
    packet[1] = 0xC0;
    packet[2] = 0x15;
    packet[3] = 0xDC;
}

static bool HasMagic(const u8* packet, u32 length)
{
    return length >= 4 && packet[0] == 0x01 && packet[1] == 0xC0 &&
           packet[2] == 0x15 && packet[3] == 0xDC;
}

/*
 * Resolve relay access points before opening TCP. A relay response contains
 * zero or more [IPv4(4 bytes), UTF-8 name length(1 byte), name] records.
 * If discovery is unavailable or empty, the main relay endpoint is tried.
 */
static u32 DiscoverAccessPoints(const sockaddr_in* relay,
                                sockaddr_in* accessPoints,
                                u32 capacity)
{
    const s32 udp = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp < 0)
        return 0;

    u8 discover[5];
    PutMagic(discover);
    discover[4] = 3; /* DISCOVER */

    const s32 sent = net_sendto(udp, discover, sizeof(discover), 0,
                                (struct sockaddr*)relay, sizeof(*relay));
    if (sent != (s32)sizeof(discover))
    {
        net_close(udp);
        return 0;
    }

    u8 response[512];
    sockaddr_in from;
    socklen_t fromLength = sizeof(from);
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(udp, &readSet);
    timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    const s32 selected = net_select(udp + 1, &readSet, nullptr, nullptr, &timeout);
    if (selected <= 0 || !FD_ISSET(udp, &readSet))
    {
        net_close(udp);
        return 0;
    }

    const s32 received = net_recvfrom(udp, response, sizeof(response), 0,
                                      (struct sockaddr*)&from, &fromLength);
    net_close(udp);

    if (received < 5 || !HasMagic(response, (u32)received) ||
        response[4] != 3 || from.sin_addr.s_addr != relay->sin_addr.s_addr)
        return 0;

    u32 count = 0;
    u32 offset = 5;
    while (offset + 5 <= (u32)received && count < capacity)
    {
        const u8* addressBytes = &response[offset];
        const u8 nameLength = response[offset + 4];
        offset += 5;
        if (offset + nameLength > (u32)received)
            break;

        sockaddr_in candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.sin_family = AF_INET;
        candidate.sin_port = htons(kRelayPort);
        memcpy(&candidate.sin_addr.s_addr, addressBytes, 4);
        if (candidate.sin_addr.s_addr != 0)
            accessPoints[count++] = candidate;

        offset += nameLength;
    }
    return count;
}

static s32 ConnectOne(const sockaddr_in* endpoint)
{
    if (StopRequested())
        return -1;

    const s32 socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket < 0)
        return -1;

    if (!IsStoppingOrPublishSocket(socket))
        return -1;

    if (net_connect(socket, (struct sockaddr*)endpoint, sizeof(*endpoint)) < 0)
    {
        CloseWorkerSocket(socket);
        return -1;
    }

    if (StopRequested())
    {
        CloseWorkerSocket(socket);
        return -1;
    }

    /* DCNet Ethernet tunnel handshake: ASCII "DCNET", protocol version 1. */
    static const u8 prologue[6] = { 'D', 'C', 'N', 'E', 'T', 1 };
    if (!SendAll(socket, prologue, sizeof(prologue)))
    {
        CloseWorkerSocket(socket);
        return -1;
    }

    return socket;
}

static s32 ConnectRelay(const sockaddr_in* relay)
{
    sockaddr_in accessPoints[kMaxAccessPoints];
    const u32 count = DiscoverAccessPoints(relay, accessPoints, kMaxAccessPoints);

    for (u32 i = 0; i < count && !StopRequested(); i++)
    {
        const s32 socket = ConnectOne(&accessPoints[i]);
        if (socket >= 0)
            return socket;
    }

    if (StopRequested())
        return -1;

    return ConnectOne(relay);
}

static bool EnqueueReceivedFrame(const u8* frame, u16 length)
{
    if (length < 14 || length > kMaxEthernetFrame)
        return false;

    Lock();
    const bool ok = QueuePush(&s_rxQueue, frame, length);
    Unlock();
    return ok;
}

static bool DequeueTransmitFrame(Frame* frame)
{
    Lock();
    const bool ok = QueuePop(&s_txQueue, frame);
    Unlock();
    return ok;
}

static bool ReceiveAvailable(s32 socket, u8* stream, u32* used)
{
    u8 incoming[2048];
    const s32 received = net_recv(socket, incoming, sizeof(incoming), 0);
    if (received <= 0)
        return false;

    if (*used + (u32)received > kStreamBufferSize)
        return false;

    memcpy(stream + *used, incoming, (u32)received);
    *used += (u32)received;

    while (*used >= 2)
    {
        const u16 length = (u16)stream[0] | ((u16)stream[1] << 8);
        if (length < 14 || length > kMaxEthernetFrame)
            return false;

        const u32 packetBytes = (u32)length + 2;
        if (*used < packetBytes)
            break;

        EnqueueReceivedFrame(stream + 2, length);
        *used -= packetBytes;
        if (*used != 0)
            memmove(stream, stream + packetBytes, *used);
    }

    return true;
}

static bool SendQueuedFrame(s32 socket)
{
    Frame frame;
    if (!DequeueTransmitFrame(&frame))
        return true;

    u8 wire[kMaxEthernetFrame + 2];
    wire[0] = (u8)(frame.length & 0xFF);
    wire[1] = (u8)(frame.length >> 8);
    memcpy(wire + 2, frame.data, frame.length);
    return SendAll(socket, wire, (u32)frame.length + 2);
}

static void* DCNetWorker(void*)
{
    bool networkInitialized = false;
    s32 socket = -1;

    if (StopRequested())
        goto cleanup;

    if (net_init() < 0)
    {
        SetStatus(DCNET_STATUS_FAILED, "Wii network initialization failed");
        goto cleanup;
    }
    networkInitialized = true;

    {
        char ip[16] = { 0 };
        char netmask[16] = { 0 };
        char gateway[16] = { 0 };
        if (if_config(ip, netmask, gateway, true, 20) < 0)
        {
            SetStatus(DCNET_STATUS_FAILED, "Wii DHCP/network setup failed");
            goto cleanup;
        }
        printf("[DCNet] Wii network: %s, gateway %s\n", ip, gateway);
    }

    if (StopRequested())
        goto cleanup;

    {
        sockaddr_in relay;
        if (!ResolveRelay(&relay))
        {
            SetStatus(DCNET_STATUS_FAILED, "DNS lookup for dcnet.flyca.st failed");
            goto cleanup;
        }

        printf("[DCNet] Resolving relay at %s\n", kRelayHost);
        socket = ConnectRelay(&relay);
        if (socket < 0)
        {
            if (!StopRequested())
                SetStatus(DCNET_STATUS_FAILED, "Could not connect to a DCNet relay");
            goto cleanup;
        }
    }

    SetStatus(DCNET_STATUS_CONNECTED, "Connected to DCNet");
    printf("[DCNet] BBA relay connected\n");

    {
        u8 stream[kStreamBufferSize];
        u32 used = 0;

        while (!StopRequested())
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);

            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 20000;

            const s32 selected = net_select(socket + 1, &readSet,
                                            nullptr, nullptr, &timeout);
            if (StopRequested())
                break;

            if (selected < 0)
            {
                SetStatus(DCNET_STATUS_FAILED, "DCNet socket select failed");
                break;
            }

            if (selected > 0 && FD_ISSET(socket, &readSet))
            {
                if (!ReceiveAvailable(socket, stream, &used))
                {
                    SetStatus(DCNET_STATUS_FAILED, "DCNet disconnected or sent an invalid frame");
                    break;
                }
            }

            if (!SendQueuedFrame(socket))
            {
                if (!StopRequested())
                    SetStatus(DCNET_STATUS_FAILED, "Sending Ethernet data to DCNet failed");
                break;
            }
        }
    }

cleanup:
    if (socket >= 0)
        CloseWorkerSocket(socket);

    if (networkInitialized)
        net_deinit();

    if (StopRequested())
        SetStatus(DCNET_STATUS_STOPPED, "Not connected");
    else if (s_status != DCNET_STATUS_FAILED)
        SetStatus(DCNET_STATUS_FAILED, "DCNet worker stopped unexpectedly");

    return nullptr;
}
} // namespace

extern "C" bool DCNet_Start(void)
{
    if (!s_mutexReady)
    {
        if (LWP_MutexInit(&s_mutex, false) < 0)
            return false;
        s_mutexReady = true;
    }

    Lock();
    if (s_threadCreated)
    {
        Unlock();
        return true;
    }

    s_stopRequested = false;
    s_activeSocket = -1;
    s_status = DCNET_STATUS_STARTING;
    strncpy(s_lastError, "Starting DCNet", sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = '\0';
    ClearQueue(&s_txQueue);
    ClearQueue(&s_rxQueue);
    s_threadCreated = true;
    Unlock();

    if (LWP_CreateThread(&s_thread, DCNetWorker, nullptr, nullptr, 32 * 1024, 80) < 0)
    {
        Lock();
        s_threadCreated = false;
        s_status = DCNET_STATUS_FAILED;
        strncpy(s_lastError, "Could not create DCNet thread", sizeof(s_lastError) - 1);
        s_lastError[sizeof(s_lastError) - 1] = '\0';
        Unlock();
        return false;
    }

    return true;
}

extern "C" void DCNet_Stop(void)
{
    if (!s_mutexReady)
        return;

    Lock();
    const bool join = s_threadCreated;
    s_stopRequested = true;
    const s32 socket = s_activeSocket;
    s_activeSocket = -1;
    Unlock();

    /* Closing the active descriptor interrupts select/receive/connect. */
    if (socket >= 0)
        net_close(socket);

    if (join)
        LWP_JoinThread(s_thread, nullptr);

    Lock();
    s_threadCreated = false;
    s_thread = LWP_THREAD_NULL;
    s_stopRequested = false;
    s_status = DCNET_STATUS_STOPPED;
    strncpy(s_lastError, "Not connected", sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = '\0';
    ClearQueue(&s_txQueue);
    ClearQueue(&s_rxQueue);
    Unlock();
}

extern "C" int DCNet_GetStatus(void)
{
    if (!s_mutexReady)
        return DCNET_STATUS_STOPPED;
    Lock();
    const int status = s_status;
    Unlock();
    return status;
}

extern "C" void DCNet_CopyLastError(char* dst, u32 size)
{
    if (dst == nullptr || size == 0)
        return;
    if (!s_mutexReady)
    {
        strncpy(dst, "Not connected", size - 1);
        dst[size - 1] = '\0';
        return;
    }

    Lock();
    strncpy(dst, s_lastError, size - 1);
    dst[size - 1] = '\0';
    Unlock();
}

extern "C" bool DCNet_SendFrame(const u8* frame, u16 length)
{
    if (frame == nullptr || length < 14 || length > kMaxEthernetFrame)
        return false;
    if (!s_mutexReady)
        return false;

    Lock();
    const bool ok = s_status == DCNET_STATUS_CONNECTED &&
                    !s_stopRequested &&
                    QueuePush(&s_txQueue, frame, length);
    Unlock();
    return ok;
}

extern "C" int DCNet_ReceiveFrame(u8* dst, u16 capacity)
{
    if (dst == nullptr || !s_mutexReady)
        return 0;

    Lock();
    if (s_rxQueue.count == 0)
    {
        Unlock();
        return 0;
    }

    const Frame& frame = s_rxQueue.frames[s_rxQueue.head];
    if (capacity < frame.length)
    {
        Unlock();
        return -1;
    }

    const u16 length = frame.length;
    memcpy(dst, frame.data, length);
    s_rxQueue.head = (s_rxQueue.head + 1) % kQueueSize;
    s_rxQueue.count--;
    Unlock();
    return (int)length;
}
