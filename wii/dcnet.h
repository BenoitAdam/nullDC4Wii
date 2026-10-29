#pragma once

#include <gccore.h>

/*
 * Small Wii-side DCNet BBA transport.
 *
 * This file only handles the DCNet TCP wire protocol and bounded packet
 * queues. The emulated GAPS/RTL8139 hardware must call DCNet_SendFrame() for
 * transmitted Ethernet frames and drain DCNet_ReceiveFrame() from the emulation
 * thread. Network and DNS calls run on a dedicated libogc thread.
 *
 * The wire protocol is implemented independently for this project:
 *   - TCP destination port 7655
 *   - "DCNET", version 1 prologue
 *   - repeated little-endian 16-bit frame length + Ethernet frame
 */

#ifdef __cplusplus
extern "C" {
#endif

enum DCNetStatus
{
    DCNET_STATUS_FAILED = -1,
    DCNET_STATUS_STOPPED = 0,
    DCNET_STATUS_STARTING = 1,
    DCNET_STATUS_CONNECTED = 2
};

/* Starts the transport worker. Returns false if thread creation fails. */
bool DCNet_Start(void);

/* Requests worker shutdown and joins it. Safe to call when not started. */
void DCNet_Stop(void);

/* Returns the current DCNet_STATUS_* value. */
int DCNet_GetStatus(void);

/* Copies the last error message into dst, always NUL-terminating if size > 0. */
void DCNet_CopyLastError(char* dst, u32 size);

/*
 * Queue an Ethernet frame for transmission.
 * Returns false when disconnected, invalid, or the bounded queue is full.
 */
bool DCNet_SendFrame(const u8* frame, u16 length);

/*
 * Pull one received Ethernet frame on the emulation thread.
 * Returns frame length, 0 if no frame is waiting, or -1 if dst is too small.
 */
int DCNet_ReceiveFrame(u8* dst, u16 capacity);

#ifdef __cplusplus
}
#endif
