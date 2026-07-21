#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <sicksaxis.h>

static int run = 1;
static void *xfb = NULL;
static GXRModeObj *rmode = NULL;
void init_video();
void print_ss_data(const struct ss_device *dev);
void move_leds(struct ss_device *dev);
void removal_callback(void *usrdata);


int main(int argc, char **argv)
{
	srand(time(NULL));
	IOS_ReloadIOS(58);
	usleep(100 * 1000);
	USB_Initialize();
	init_video();
	WPAD_Init();

	ss_init();
	struct ss_device dev, dev2;
	ss_initialize(&dev);


    while(run) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);
        printf("\x1b[2;1H  \n");
        printf("Press 1 or 2 to open the controllers\n");

        if (!ss_is_connected(&dev)) {
            if (pressed & WPAD_BUTTON_1) {
                if (ss_open(&dev)>0) {
                    ss_start_reading(&dev);
                    ss_set_removal_cb(&dev, removal_callback, (void*)1);
                }
            }
        } else {
            print_ss_data(&dev);
            move_leds(&dev);
            if (pressed & WPAD_BUTTON_PLUS) {
                ss_set_led(&dev, rand()%8);
                ss_set_rumble(&dev, rand()%0xFF, rand()%0xFF, rand()%0xFF, rand()%0xFF);
            }
        }

        if (!ss_is_connected(&dev2)) {
            if (pressed & WPAD_BUTTON_2) {
                if (ss_open(&dev2)>0) {
                    ss_start_reading(&dev2);
                    ss_set_removal_cb(&dev2, removal_callback, (void*)2);
                }
            }
        } else {
            print_ss_data(&dev2);
            move_leds(&dev2);
            if (pressed & WPAD_BUTTON_MINUS) {
                ss_set_led(&dev2, rand()%8);
                ss_set_rumble(&dev2, rand()%0xFF, rand()%0xFF, rand()%0xFF, rand()%0xFF);
            }
        }

        if (pressed & WPAD_BUTTON_HOME) run = 0;
        VIDEO_WaitVSync();
        VIDEO_ClearFrameBuffer (rmode, xfb, COLOR_BLACK);
    }
	ss_close(&dev);
	ss_close(&dev2);
	USB_Deinitialize();
	exit(0);
	return 0;
}

void removal_callback(void *usrdata)
{
    printf("Device %d disconnected\n", (int)usrdata);
}

void init_video()
{
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	printf("\x1b[2;0H");
}

void print_ss_data(const struct ss_device *dev)
{
    printf("\n\nPS: %i   START: %i   SELECT: %i   /\\: %i   []: %i   O: %i   X: %i   L3: %i   R3: %i\n", \
            dev->pad.buttons.PS, dev->pad.buttons.start, dev->pad.buttons.select, dev->pad.buttons.triangle, \
            dev->pad.buttons.square, dev->pad.buttons.circle, dev->pad.buttons.cross, dev->pad.buttons.L3, dev->pad.buttons.R3);

    printf("L1: %i   L2: %i   R1: %i   R2: %i   UP: %i   DOWN: %i   RIGHT: %i   LEFT: %i\n", \
            dev->pad.buttons.L1, dev->pad.buttons.L2, dev->pad.buttons.R1, dev->pad.buttons.R2, \
            dev->pad.buttons.up, dev->pad.buttons.down, dev->pad.buttons.right, dev->pad.buttons.left);

    printf("LX: %i   LY: %i   RX: %i   RY: %i\n", \
            dev->pad.left_analog.x, dev->pad.left_analog.y, dev->pad.right_analog.x, dev->pad.right_analog.y);

    printf("aX: %i   aY: %i   aZ: %i   Zgyro: %i\n", \
            dev->pad.motion.acc_x, dev->pad.motion.acc_y, dev->pad.motion.acc_z, dev->pad.motion.z_gyro);

    printf("L1 predev: %i   L2 predev: %i   R1 predev: %i   R2 predev: %i\n", \
            dev->pad.shoulder_sens.L1, dev->pad.shoulder_sens.L2, dev->pad.shoulder_sens.R1, dev->pad.shoulder_sens.R2);

    printf("/\\ predev: %i   [] predev: %i   O predev: %i   X predev: %i\n",
            dev->pad.button_sens.triangle, dev->pad.button_sens.square, dev->pad.button_sens.circle, dev->pad.button_sens.cross);

    printf("UP: %i   DOWN: %i   RIGHT: %i   LEFT: %i\n", \
            dev->pad.dpad_sens.up, dev->pad.dpad_sens.down, dev->pad.dpad_sens.right, dev->pad.dpad_sens.left);
}


void move_leds(struct ss_device *dev)
{
    static int cnt = 0, dir = 1, i = 0;
    cnt++;
    if (cnt > 15) {
        if (dir == 1) {
            i++;
            cnt = 0;
            if (i > 4) {
                i = 4;
                dir = 0;
            }
            ss_set_led(dev, i);
        } else {
            i--;
            cnt = 0;
            if(i < 1) {
                i = 1;
                dir = 1;
            }
            ss_set_led(dev, i);
        }
    }
}
