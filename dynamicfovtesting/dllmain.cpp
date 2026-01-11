// dllmain.cpp (UG2)
#include "pch.h"
#include <windows.h>
#include <stdint.h>
#include <cmath>
#include <stdlib.h>
#include <string.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;

// ======================================================
// game state
// ======================================================
// *(DWORD*)0x8654A4
// 3 = FE
// 4,5 = loading
// 6 = gameplay

#define GameFlowState (*(int*)0x8654A4)

// ======================================================
// config
// ======================================================
int      cfg_toggleKey = 118;   // F7 default
uint16_t cfg_initial_fov = 15000;
uint16_t cfg_max_fov = 24000;
float    cfg_max_speed = 80.0f;
int      cfg_graph_type = 1;
bool cfg_permanentEnable = false;

// ======================================================
// runtime state
// ======================================================
bool userWantsEnabled = false;
bool effectActive = false;
bool lastPatchState = false;

// ======================================================
// memory addresses (ug2)
// ======================================================
volatile float* speed_ptr = (float*)0x007F09E8;
volatile uint16_t* fov_ptr = (uint16_t*)0x008761A4;

// ======================================================
// patch info
// original instruction @ 0x00453B26:
// mov [ecx+000000C4], ax
// ======================================================
uintptr_t patchAddr = 0x00453B26;

BYTE originalBytes[7] = { 0x66, 0x89, 0x81, 0xC4, 0x00, 0x00, 0x00 };
BYTE nopBytes[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

// ======================================================
// helpers
// ======================================================
void PatchBytes(bool enable)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)patchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect);

    memcpy((void*)patchAddr, enable ? nopBytes : originalBytes, 7);

    VirtualProtect((LPVOID)patchAddr, 7, oldProtect, &oldProtect);
}

void UpdatePatch(bool shouldEnable)
{
    if (shouldEnable != lastPatchState)
    {
        PatchBytes(shouldEnable);
        lastPatchState = shouldEnable;
    }
}

float ApplyGraph(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (cfg_graph_type)
    {
    case 0: // linear
        return t;

    case 1: // cubic in
        return t * t * t;

    case 2: // cubic out
        return 1.0f - powf(1.0f - t, 3.0f);

    default:
        return t;
    }
}

void LoadConfig()
{
    // =========================
    // resolve ini path (DLL dir)
    // =========================
    char iniPath[MAX_PATH] = { 0 };
    GetModuleFileNameA((HMODULE)&__ImageBase, iniPath, MAX_PATH);

    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';

    strcat_s(iniPath, "DFconfig.ini");

    // =========================
    // read config
    // =========================
    cfg_toggleKey = GetPrivateProfileIntA(
        "hotkeys",
        "toggle_fov",
        118,
        iniPath
    );

    cfg_initial_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "initial_fov",
        15000,
        iniPath
    );

    cfg_max_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "max_fov",
        24000,
        iniPath
    );

    cfg_graph_type = GetPrivateProfileIntA(
        "settings",
        "graph_type",
        1,
        iniPath
    );

    char buf[64] = { 0 };
    GetPrivateProfileStringA(
        "speed",
        "max_speed",
        "80.0",
        buf,
        sizeof(buf),
        iniPath
    );

    cfg_max_speed = (float)atof(buf);
    if (cfg_max_speed <= 0.0f)
        cfg_max_speed = 80.0f;

    cfg_permanentEnable = GetPrivateProfileIntA(
        "settings",
        "permanentEnable",
        0,
        iniPath
    ) != 0;

    // initial runtime state
    userWantsEnabled = cfg_permanentEnable;
}

// ======================================================
// main thread
// ======================================================
DWORD WINAPI MainThread(void*)
{
    LoadConfig();

    while (true)
    {
        bool inGameplay = (GameFlowState == 6);

        if (inGameplay && (GetAsyncKeyState(cfg_toggleKey) & 1))
        {
            userWantsEnabled = !userWantsEnabled;
        }

        effectActive = userWantsEnabled && inGameplay;
        UpdatePatch(effectActive);

        if (effectActive)
        {
            float s = *speed_ptr;
            if (s < 0.0f) s = 0.0f;
            if (s > cfg_max_speed) s = cfg_max_speed;

            float t = s / cfg_max_speed;
            float eased = ApplyGraph(t);

            float f =
                (float)cfg_initial_fov +
                (float)(cfg_max_fov - cfg_initial_fov) * eased;

            if (f > cfg_max_fov)     f = (float)cfg_max_fov;
            if (f < cfg_initial_fov) f = (float)cfg_initial_fov;

            *fov_ptr = (uint16_t)f;
        }

        SwitchToThread();
    }


}

// ======================================================
// dll entry
// ======================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}