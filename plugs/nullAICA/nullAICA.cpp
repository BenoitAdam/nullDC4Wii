#include "nullAICA.h"
#include "sgc_if.h"
#include "aica.h"
#include "aica_mem.h"
#include "wii_audiostream.h"

aica_setts aica_settings;
aica_init_params aica_params;

// Called from dc/aica/aica_if.cpp to initialize the AICA subsystem.
// aica_init_params provides: RaiseInterrupt, aica_ram, SB_ISTEXT, CancelInterrupt
s32 FASTCALL InitAica(aica_init_params* initp)
{
    memcpy(&aica_params, initp, sizeof(aica_params));

    LoadSettingsAica();
    init_mem();
    AICA_Init();
    wii_InitAudio();

    return rv_ok;
}

void FASTCALL TermAica()
{
    wii_TermAudio();
    AICA_Term();
    term_mem();
}

void FASTCALL ResetAica(bool Manual)
{
    // nothing needed for now
}

// aica_ReadMem_reg / aica_WriteMem_reg / UpdateAICA are called directly
// from dc/aica/aica_if.cpp — they are declared in aica_mem.h and aica.h.

void LoadSettingsAica()
{
    aica_settings.BufferSize    = 2048;
    aica_settings.LimitFPS      = 0;
    aica_settings.HW_mixing     = 0;
    aica_settings.SoundRenderer = 1;
    aica_settings.GlobalFocus   = 1;
    aica_settings.BufferCount   = 1;
    aica_settings.CDDAMute      = 0;
    aica_settings.GlobalMute    = 0;
    aica_settings.DSPEnabled    = 0;
    aica_settings.Volume        = 90;
}

void SaveSettingsAica()
{
    // No config system available on Wii at this level — settings are hardcoded above.
}
