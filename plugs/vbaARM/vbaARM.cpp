// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "vbaARM.h"
#include "arm_aica.h"
#include "arm7.h"
#include "arm_mem.h"

arm_init_params arm_params;
emu_info arm_eminf;

/*HINSTANCE hinst;
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	hinst=(HINSTANCE)hModule;
    return TRUE;
}
 */
void EXPORT_CALL armhandle_About(u32 id,void* w,void* p)
{
//	MessageBoxA((HWND)w,"Made by the VBA Team\r\nPorted by drk||Raziel","About the VBA ARM Core...",MB_ICONINFORMATION);
}

void armLoadSettings()
{
	//load default settings before init
	//settings.BufferSize=cfgGetInt("BufferSize",1024);
}

void armSaveSettings()
{
	
}

s32 FASTCALL armOnLoad(emu_info* em)
{
	memcpy(&arm_eminf,em,sizeof(arm_eminf));

	armLoadSettings();
	arm_eminf.AddMenuItem(em->RootMenu,-1,"About",armhandle_About,0);
	return rv_ok;
}

void FASTCALL armOnUnload()
{
}

//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL armInit(arm_init_params* initp)
{
	memcpy(&arm_params,initp,sizeof(arm_params));

	arm_init_mem();
	arm_Init();

	return rv_ok;
}

//called when plugin is unloaded by emu , olny if dcInit is called (eg , not called to enumerate plugins)
void FASTCALL armTerm()
{
	arm_term_mem();
	//arm7_Term ?
}

//It's suposed to reset anything 
void FASTCALL armReset(bool Manual)
{
	printf("armReset\n");
	arm_Reset();
}

void FASTCALL SetResetState(u32 state)
{
//	printf("SetResetState %d\n",state);
	arm_SetEnabled(state==0);
}
//Give to the emu pointers for the PowerVR interface
EXPORT void EXPORT_CALL armGetInterface(plugin_interface* info)
{
	info->InterfaceVersion=PLUGIN_I_F_VERSION;

#define c info->common
#define a info->arm

	strcpy(c.Name,"VBA ARM Sound Cpu Core [" __DATE__ "]");

	c.InterfaceVersion=ARM_PLUGIN_I_F_VERSION;
	c.Type=Plugin_ARM;

	c.Load=armOnLoad;
	c.Unload=armOnUnload;

	a.Init=armInit;
	a.Reset=armReset;
	a.Term=armTerm;

	a.Update=armUpdateARM;
	a.ArmInterruptChange=ArmInterruptChange;
	a.ExeptionHanlder=0;
	a.SetResetState=SetResetState;
}

int armcfgGetInt(char* key,int def)
{
	char t[512];
	strncpy(t,key,511);
	t[511] = '\0';
	return arm_eminf.ConfigLoadInt("nullAica",t,def);
}
void armcfgSetInt(char* key,int def)
{
	char t[512];
	strncpy(t,key,511);
	t[511] = '\0';
	arm_eminf.ConfigSaveInt("nullAica",t,def);
}