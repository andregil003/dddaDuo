// dddaDuo - test host (32-bit console)
// Loads ddpuck_core.dll exactly like the game loader does, calls DdpuckCoreInit,
// then stays alive long enough for the core's worker thread to log and exit.
// Fast way to reproduce crashes without launching the game.

#include <windows.h>
#include <cstdio>

typedef void (WINAPI* PFN_INIT)(HMODULE);

int wmain(int argc, wchar_t** argv) {
    wchar_t dll[MAX_PATH];
    if (argc > 1) {
        wcscpy_s(dll, MAX_PATH, argv[1]);
    } else {
        GetModuleFileNameW(nullptr, dll, MAX_PATH);
        wchar_t* s = wcsrchr(dll, L'\\');
        if (s) *(s + 1) = L'\0';
        wcscat_s(dll, MAX_PATH, L"ddpuck_core.dll");
    }
    wprintf(L"[host] loading %s\n", dll);
    HMODULE h = LoadLibraryW(dll);
    if (!h) { wprintf(L"[host] LoadLibrary failed: %lu\n", GetLastError()); return 2; }
    wprintf(L"[host] module at %p\n", (void*)h);

    FARPROC p = GetProcAddress(h, "DdpuckCoreInit");
    if (!p) p = GetProcAddress(h, "_DdpuckCoreInit@4");
    wprintf(L"[host] DdpuckCoreInit at %p\n", (void*)p);
    if (!p) { wprintf(L"[host] export missing\n"); return 3; }

    ((PFN_INIT)p)(h);
    wprintf(L"[host] init called, sleeping 12s...\n");
    for (int i = 0; i < 12; ++i) { Sleep(1000); wprintf(L"."); fflush(stdout); }
    wprintf(L"\n[host] survived\n");
    return 0;
}
