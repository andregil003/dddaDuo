// dddaDuo loader - DirectInput8 proxy (32-bit)
// Alternative entry point for setups where xinput1_3.dll is already taken.
// Only DirectInput8Create is exported: that is the one symbol DDDA.exe imports.
// Never install this loader and the XInput loader at the same time.

#include "../common/ddpuck.h"
#include <cstdio>
#include <cwchar>

static HMODULE g_real = nullptr;
static CRITICAL_SECTION g_cs;
static PFN_DDPUCK_INIT g_coreInit = nullptr;

static void EnsureReal() {
    if (g_real) return;
    EnterCriticalSection(&g_cs);
    if (!g_real) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH); // SysWOW64 in a 32-bit process
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\dinput8.dll", sys);
        g_real = LoadLibraryW(path);
    }
    LeaveCriticalSection(&g_cs);
}

static FARPROC Real(const char* name) {
    EnsureReal();
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

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
            FARPROC pInit = GetProcAddress(h, "DdpuckCoreInit");
            if (!pInit) pInit = GetProcAddress(h, "_DdpuckCoreInit@4"); // x86 stdcall decoration
            g_coreInit = (PFN_DDPUCK_INIT)pInit;
            if (g_coreInit) g_coreInit(h);
        }
    }
    LeaveCriticalSection(&g_cs);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    typedef HRESULT (WINAPI* Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    EnsureCore(); // hook the core before the game starts polling devices
    Fn fn = (Fn)Real("DirectInput8Create");
    if (!fn) return E_FAIL;
    return fn(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

BOOL APIENTRY DllMain(HMODULE hSelf, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
        InitializeCriticalSection(&g_cs);
    } else if (reason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&g_cs);
    }
    return TRUE;
}
