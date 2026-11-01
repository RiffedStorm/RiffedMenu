// RiffedMenu - ImGui trainer for GTA San Andreas (v1.0 US / 2014 Steam), loaded as an .asi plugin.
#include <windows.h>
#include <MinHook.h>
#include "game.h"
#include "hooks.h"
#include "log.h"

HMODULE g_module = nullptr;

static DWORD WINAPI InitThread(LPVOID)
{
    Game::DetectVersion();
    Log("Game features %s", Game::Supported() ? "ENABLED" : "DISABLED (unsupported exe)");

    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed - aborting"); return 1; }

    bool input  = Hooks::InstallInputHooks();
    bool render = Hooks::InstallRenderHooks();
    Log("Init done: render=%d input=%d. Press Insert in game.", render, input);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
        LogInit(inst);
        InstallCrashHandlers();
        if (HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
