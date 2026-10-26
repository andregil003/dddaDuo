// dddaDuo loader - XInput 1.3 proxy (32-bit)
// DDDA.exe imports XINPUT1_3.dll statically, so dropping this file beside the exe
// gets our core loaded before the first frame. Everything is forwarded untouched.

#include "../common/ddpuck.h"
#include <cstdio>
#include <cwchar>

// ---- local mirrors of the XInput ABI (no xinput.h: avoids dllimport redefinition) ----
struct DDPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct DDSTATE { DWORD dwPacketNumber; DDPAD Gamepad; };
struct DDVIBRATION { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; };
struct DDCAPS {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    DDPAD Gamepad;
    DDVIBRATION Vibration;
};
struct DDBATTERY { BYTE BatteryType; BYTE BatteryLevel; };
struct DDKEYSTROKE { WORD VirtualKey; WORD Unicode; DWORD Flags; BYTE UserIndex; BYTE HidCode; };

static HMODULE g_real = nullptr;
static CRITICAL_SECTION g_cs;
static PFN_DDPUCK_INIT g_coreInit = nullptr;

static void EnsureReal() {
    if (g_real) return;
    EnterCriticalSection(&g_cs);
    if (!g_real) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);              // resolves to SysWOW64 in a 32-bit process
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\xinput1_3.dll", sys);
        g_real = LoadLibraryW(path);
    }
    LeaveCriticalSection(&g_cs);
}

static FARPROC Real(const char* name, int ordinal) {
    EnsureReal();
    if (!g_real) return nullptr;
    if (name) return GetProcAddress(g_real, name);
    return GetProcAddress(g_real, (LPCSTR)(uintptr_t)ordinal);
}

// Loads the shared core exactly once, whichever loader the game calls first.
static void EnsureCore() {
    if (g_coreInit) return;
    EnterCriticalSection(&g_cs);
    if (!g_coreInit) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        wcscat_s(exePath, MAX_PATH, DDPUCK_CORE_DLL);
        HMODULE h = LoadLibraryW(exePath);
        if (h) {
            g_coreInit = (PFN_DDPUCK_INIT)GetProcAddress(h, "DdpuckCoreInit");
            if (g_coreInit) g_coreInit(h);
        }
    }
    LeaveCriticalSection(&g_cs);
}

extern "C" {

DWORD WINAPI XInputGetState(DWORD idx, DDSTATE* st) {
    typedef DWORD (WINAPI* Fn)(DWORD, DDSTATE*);
    EnsureCore();
    Fn fn = (Fn)Real("XInputGetState", 2);
    return fn ? fn(idx, st) : 1167u; // ERROR_DEVICE_NOT_CONNECTED
}

DWORD WINAPI XInputSetState(DWORD idx, DDVIBRATION* vib) {
    typedef DWORD (WINAPI* Fn)(DWORD, DDVIBRATION*);
    Fn fn = (Fn)Real("XInputSetState", 3);
    return fn ? fn(idx, vib) : 1167u;
}

DWORD WINAPI XInputGetCapabilities(DWORD idx, DWORD flags, DDCAPS* caps) {
    typedef DWORD (WINAPI* Fn)(DWORD, DWORD, DDCAPS*);
    Fn fn = (Fn)Real("XInputGetCapabilities", 4);
    return fn ? fn(idx, flags, caps) : 1167u;
}

void WINAPI XInputEnable(BOOL enable) {
    typedef void (WINAPI* Fn)(BOOL);
    Fn fn = (Fn)Real("XInputEnable", 5);
    if (fn) fn(enable);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD idx, GUID* render, GUID* capture) {
    typedef DWORD (WINAPI* Fn)(DWORD, GUID*, GUID*);
    Fn fn = (Fn)Real("XInputGetDSoundAudioDeviceGuids", 6);
    return fn ? fn(idx, render, capture) : 1167u;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD idx, BYTE devType, DDBATTERY* batt) {
    typedef DWORD (WINAPI* Fn)(DWORD, BYTE, DDBATTERY*);
    Fn fn = (Fn)Real("XInputGetBatteryInformation", 7);
    return fn ? fn(idx, devType, batt) : 1167u;
}

DWORD WINAPI XInputGetKeystroke(DWORD idx, DWORD flags, DDKEYSTROKE* ks) {
    typedef DWORD (WINAPI* Fn)(DWORD, DWORD, DDKEYSTROKE*);
    Fn fn = (Fn)Real("XInputGetKeystroke", 8);
    return fn ? fn(idx, flags, ks) : 1167u;
}

// Undocumented ordinals 100-103 (guide button family). Games that poll the Guide
// button use ordinal 100, so it must be forwarded too.
DWORD WINAPI XInputGetStateEx(DWORD idx, DDSTATE* st) {
    typedef DWORD (WINAPI* Fn)(DWORD, DDSTATE*);
    Fn fn = (Fn)Real(nullptr, 100);
    if (fn) return fn(idx, st);
    return XInputGetState(idx, st);
}

DWORD WINAPI XInputWaitForGuideButton(DWORD idx, DWORD flags, void* pv) {
    typedef DWORD (WINAPI* Fn)(DWORD, DWORD, void*);
    Fn fn = (Fn)Real(nullptr, 101);
    return fn ? fn(idx, flags, pv) : 1167u;
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD idx) {
    typedef DWORD (WINAPI* Fn)(DWORD);
    Fn fn = (Fn)Real(nullptr, 102);
    return fn ? fn(idx) : 1167u;
}

DWORD WINAPI XInputPowerOffController(DWORD idx) {
    typedef DWORD (WINAPI* Fn)(DWORD);
    Fn fn = (Fn)Real(nullptr, 103);
    return fn ? fn(idx) : 1167u;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hSelf, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
        InitializeCriticalSection(&g_cs);
    } else if (reason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&g_cs);
    }
    return TRUE;
}
