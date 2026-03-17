#pragma once

// Pull in all C++ stdlib headers BEFORE defining min/max macros,
// otherwise the macros corrupt std::numeric_limits<T>::min()/max()
// member function declarations in <limits>.
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "../../types.h"

// Wii is big-endian PowerPC
#define WII 1

// Big-endian byte-swap macros for AICA register access
#define ReadMemArrRet(arr,addr,sz)               \
    {if (sz==1)                                  \
        return arr[(addr)^3];                    \
    else if (sz==2)                              \
        return (*(u16*)&arr[(addr)^2]);          \
    else if (sz==4)                              \
        return (*(u32*)&arr[(addr)]);}

#define WriteMemArrRet(arr,addr,data,sz)             \
    {if(sz==1)                                       \
        {arr[(addr)^3]=(u8)data;return;}             \
    else if (sz==2)                                  \
        {*(u16*)&arr[(addr)^2]=((u16)data);return;}  \
    else if (sz==4)                                  \
    {*(u32*)&arr[(addr)]=(data);return;}}

#define WriteMemArr(arr,addr,data,sz)            \
    {if(sz==1)                                   \
        {arr[(addr)^3]=(u8)data;}                \
    else if (sz==2)                              \
        {*(u16*)&arr[(addr)^2]=((u16)data);}     \
    else if (sz==4)                              \
    {*(u32*)&arr[(addr)]=(data);}}

#define UINT16 u16
#define INT32  s32
#define DWORD  u32
#define UINT32 u32

// Undefine any min/max the platform may have defined as macros, then
// redefine as inline templates. This avoids corrupting zero-argument
// member calls like std::numeric_limits<T>::min() in <limits>.
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif
template<typename T> inline T wii_max(T a, T b) { return a > b ? a : b; }
template<typename T> inline T wii_min(T a, T b) { return a < b ? a : b; }
#define max(a,b) wii_max((a),(b))
#define min(a,b) wii_min((a),(b))

struct aica_setts
{
    u32 SoundRenderer;
    u32 HW_mixing;
    u32 BufferSize;
    u32 LimitFPS;
    u32 GlobalFocus;
    u32 BufferCount;
    u32 CDDAMute;
    u32 GlobalMute;
    u32 DSPEnabled;
    u32 Volume;
};

extern aica_setts       aica_settings;
extern aica_init_params aica_params;

// Called from dc/aica/aica_if.cpp
s32  FASTCALL InitAica(aica_init_params* initp);
void FASTCALL TermAica();
void FASTCALL ResetAica(bool Manual);

void LoadSettingsAica();
void SaveSettingsAica();
