// Wii Rendering

#pragma once
#include "drkPvr.h"
#include "Renderer_if.h"


bool InitRenderer();
void TermRenderer();
void ResetRenderer(bool Manual);

bool ThreadStart();
void ThreadEnd();
void VBlank();
void StartRender();
void EndRender();

void ListCont();
void ListInit();
void SoftReset();

void SetFpsText(char* text);
void SetFbScale(float x, float y);


#define rend_init         InitRenderer
#define rend_term         TermRenderer
#define rend_reset        ResetRenderer

#define rend_thread_start ThreadStart
#define rend_thread_end	  ThreadEnd
#define rend_vblank       VBlank
#define rend_start_render StartRender
#define rend_end_render   EndRender

#define rend_list_cont ListCont
#define rend_list_init ListInit
#define rend_list_srst SoftReset

#define rend_set_fps_text SetFpsText
#define rend_set_render_rect(rect,sht)
#define rend_set_fb_scale SetFbScale
