// dddaDuo core - Fase A
// Resolves the game's player base at runtime (AOB signature) and logs live player state.
// Deliberately does NOT write to game memory yet: Fase A only proves we can find and read it.

#include "../common/ddpuck.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

static HMODULE g_hCore = nullptr;
static FILE*   g_log = nullptr;
static CRITICAL_SECTION g_logCs;
static volatile LONG g_started = 0;

// ---------------------------------------------------------------- logging
static void LogOpen() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(exePath, MAX_PATH, L"ddpuck.log");
    _wfopen_s(&g_log, exePath, L"a, ccs=UTF-8");
}

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logCs);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logCs);
}

// ---------------------------------------------------------------- AOB scan
static bool MaskMatch(const BYTE* p, const BYTE* sig, const char* mask) {
    for (; *mask; ++mask, ++p, ++sig) {
        if (*mask == 'x' && *p != *sig) return false;
    }
    return true;
}

// Scans every executable section of the main module for the signature.
// Returns address of the first matching instruction, or nullptr.
static BYTE* FindPattern(const BYTE* sig, const char* mask) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    if (!base) return nullptr;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const size_t sigLen = strlen(mask);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE* start = base + sec->VirtualAddress;
        size_t size = sec->Misc.VirtualSize;
        if (size < sigLen) continue;

        for (size_t off = 0; off <= size - sigLen; ++off) {
            if (MaskMatch(start + off, sig, mask)) {
                Log("signature hit at %p (section %.8s)", start + off, sec->Name);
                return start + off;
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- state read
struct PlayerView {
    BYTE*  base = nullptr;
    float  hp = -1.0f;
    bool   ok = false;
};

static bool SafeReadFloat(const void* addr, float* out) {
    __try {
        *out = *(const float*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static PlayerView ReadPlayer(BYTE** pBase) {
    PlayerView v;
    if (!pBase) return v;
    __try {
        BYTE* base = *pBase;                       // *pBase = real player base
        if (!base) return v;
        v.base = base;
        float hp = 0.0f;
        if (SafeReadFloat(base + DDPUCK_OFF_PLAYER + DDPUCK_OFF_HP, &hp)) {
            v.hp = hp;
            v.ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v.ok = false;
    }
    return v;
}

// ---------------------------------------------------------------- worker
static DWORD WINAPI Worker(LPVOID) {
    Sleep(1500); // let the game finish bringing up its heap / scene

    LogOpen();
    Log("=== dddaDuo core Fase A ===");
    Log("core module: %p", g_hCore);

    BYTE* hit = FindPattern(DDPUCK_SIG_PBASE, DDPUCK_SIG_PBASE_MASK);
    if (!hit) {
        Log("ERROR: pBase signature not found. Unsupported executable or wrong game version.");
        return 0;
    }

    BYTE** pBase = (BYTE**)(hit + 2); // operand of `mov edx, [imm32]`
    BYTE*  raw   = nullptr;
    __try { raw = *pBase; } __except (EXCEPTION_EXECUTE_HANDLER) { raw = nullptr; }

    if (!raw || (uintptr_t)raw < 0x10000) {
        Log("ERROR: pBase pointer not resolvable yet (%p). Will retry.", raw);
    } else {
        Log("pBase variable at %p -> player base %p", pBase, raw);
    }

    float lastHp = -9999.0f;
    for (;;) {
        BYTE* cur = nullptr;
        __try { cur = *pBase; } __except (EXCEPTION_EXECUTE_HANDLER) { cur = nullptr; }

        if (cur && (uintptr_t)cur >= 0x10000) {
            PlayerView v = ReadPlayer(pBase);
            if (v.ok && v.hp != lastHp) {
                Log("player base %p | HP = %.1f", v.base, v.hp);
                lastHp = v.hp;
            } else if (!v.ok) {
                Log("waiting: player struct not populated yet (base %p)", cur);
            }
        }
        Sleep(2000);
    }
    return 0;
}

// ---------------------------------------------------------------- exports
extern "C" __declspec(dllexport) void WINAPI DdpuckCoreInit(HMODULE hSelf) {
    if (InterlockedExchange(&g_started, 1) != 0) return; // single init even with two loaders
    g_hCore = hSelf;
    InitializeCriticalSection(&g_logCs);
    HANDLE h = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    // NOTE: no MessageBox here on purpose. A blocking popup inside a loader
    // call stalls the game's startup; diagnostics go to ddpuck.log instead.
}

BOOL APIENTRY DllMain(HMODULE hSelf, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
    }
    return TRUE;
}
