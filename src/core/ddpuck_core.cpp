// dddaDuo core - Fase A
// Resolves the game's player base at runtime (AOB signature) and logs live player state.
// Deliberately does NOT write to game memory yet: Fase A only proves we can find and read it.

#include "../common/ddpuck.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <process.h>
#include <share.h>

static HMODULE g_hCore = nullptr;
static FILE*   g_log = nullptr;
static CRITICAL_SECTION g_logCs;
static volatile LONG g_started = 0;

// ---------------------------------------------------------------- logging
static bool TryOpenLog(const wchar_t* path) {
    // _wfsopen with _SH_DENYWR: other processes may READ the log while the game runs
    // (plain fopen locks it exclusively, which makes live diagnostics impossible).
    g_log = _wfsopen(path, L"a", _SH_DENYWR);
    return g_log != nullptr;
}

static void LogOpen() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(exePath, MAX_PATH, L"ddpuck.log");

    // 1) Next to the executable (normal case for a game outside Program Files).
    if (TryOpenLog(exePath)) return;

    // 2) %LOCALAPPDATA%\dddaDuo - writable even when the game sits in Program Files.
    wchar_t dir[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH) > 0) {
        wcscat_s(dir, MAX_PATH, L"\\dddaDuo");
        CreateDirectoryW(dir, nullptr);
        wcscat_s(dir, MAX_PATH, L"\\ddpuck.log");
        if (TryOpenLog(dir)) return;
    }

    // 3) %TEMP%\dddaDuo - last resort, always writable for the running user.
    wchar_t tmp[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, tmp) > 0) {
        wcscat_s(tmp, MAX_PATH, L"dddaDuo");
        CreateDirectoryW(tmp, nullptr);
        wcscat_s(tmp, MAX_PATH, L"\\ddpuck.log");
        TryOpenLog(tmp);
    }
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
struct CharView {
    float hp = -1.0f;
    float hpMax = -1.0f;
    float stamina = -1.0f;
    float staminaMax = -1.0f;
    bool  ok = false;
};

static bool SafeReadFloat(const void* addr, float* out) {
    __try {
        *out = *(const float*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Reads one party slot. Slot 0 is the Arisen, slot 1 the main pawn.
static CharView ReadChar(BYTE* base, int slot) {
    CharView v;
    if (!base) return v;
    BYTE* p = base + DDPUCK_OFF_PLAYER + (size_t)slot * DDPUCK_STRIDE_CHARACTER;
    __try {
        float hp = 0.0f, hpMax = 0.0f, st = 0.0f, stMax = 0.0f;
        bool a = SafeReadFloat(p + DDPUCK_OFF_HP, &hp);
        bool b = SafeReadFloat(p + DDPUCK_OFF_HP_MAX, &hpMax);
        bool c = SafeReadFloat(p + DDPUCK_OFF_STAMINA, &st);
        bool d = SafeReadFloat(p + DDPUCK_OFF_STAMINA_MAX, &stMax);
        if (a && b) {
            v.hp = hp; v.hpMax = hpMax;
            v.stamina = c ? st : -1.0f;
            v.staminaMax = d ? stMax : -1.0f;
            v.ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v.ok = false;
    }
    return v;
}

// True when any slot that matters (HP) changed since the last snapshot.
static bool PartyChanged(const CharView* now, const CharView* before) {
    for (int i = 0; i < DDPUCK_PARTY_SLOTS; ++i) {
        if (now[i].hp != before[i].hp || now[i].hpMax != before[i].hpMax) return true;
    }
    return false;
}

// ---------------------------------------------------------------- worker
static void LogParty(const CharView* party) {
    static const char* kName[DDPUCK_PARTY_SLOTS] = { "arisen", "pawn1", "pawn2", "pawn3", "slot4", "slot5" };
    for (int i = 0; i < DDPUCK_PARTY_SLOTS; ++i) {
        if (!party[i].ok) continue;
        if (party[i].hp <= 0.0f && party[i].hpMax <= 0.0f) continue; // empty slot
        Log("%-7s slot %d | HP %.1f/%.1f | ST %.1f/%.1f",
            kName[i], i, party[i].hp, party[i].hpMax, party[i].stamina, party[i].staminaMax);
    }
}

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

    // The instruction is `mov edx, dword ptr [imm32]`. The imm32 IS the absolute
    // address of the pBase global (not an address relative to the instruction), so
    // it must be read out of the instruction bytes first: hit+2 points AT the
    // operand, it is not the operand's value.
    DWORD varAddr = 0;
    __try { varAddr = *(const DWORD*)(hit + 2); }
    __except (EXCEPTION_EXECUTE_HANDLER) { varAddr = 0; }
    if (varAddr < 0x10000) {
        Log("ERROR: pBase operand unreadable (hit %p).", hit);
        return 0;
    }

    BYTE** pBase = (BYTE**)varAddr;
    BYTE*  raw   = nullptr;
    __try { raw = *pBase; } __except (EXCEPTION_EXECUTE_HANDLER) { raw = nullptr; }

    if (!raw || (uintptr_t)raw < 0x10000) {
        Log("pBase global at %p not populated yet (%p). Will poll.", pBase, raw);
    } else {
        Log("pBase global at %p -> player base %p", pBase, raw);
    }

    CharView  last[DDPUCK_PARTY_SLOTS];
    bool  warnedNotReady = false;
    bool  firstPass = true;
    for (;;) {
        BYTE* cur = nullptr;
        __try { cur = *pBase; } __except (EXCEPTION_EXECUTE_HANDLER) { cur = nullptr; }

        if (cur && (uintptr_t)cur >= 0x10000) {
            CharView now[DDPUCK_PARTY_SLOTS];
            for (int i = 0; i < DDPUCK_PARTY_SLOTS; ++i) now[i] = ReadChar(cur, i);

            if (firstPass || PartyChanged(now, last)) {
                Log("--- party snapshot (base %p) ---", cur);
                LogParty(now);
                for (int i = 0; i < DDPUCK_PARTY_SLOTS; ++i) last[i] = now[i];
                firstPass = false;
                warnedNotReady = false;
            } else if (!now[0].ok && !warnedNotReady) {
                Log("waiting: party block not populated yet (base %p)", cur);
                warnedNotReady = true;
            }
        }
        Sleep(2000);
    }
    return 0;
}

// ---------------------------------------------------------------- exports
static unsigned __stdcall WorkerThunk(void*) { return Worker(nullptr); }

extern "C" __declspec(dllexport) void WINAPI DdpuckCoreInit(HMODULE hSelf) {
    if (InterlockedExchange(&g_started, 1) != 0) return; // single init even with two loaders
    g_hCore = hSelf;
    InitializeCriticalSection(&g_logCs);
    // _beginthreadex (not CreateThread): the worker uses the CRT (fprintf), and a
    // raw CreateThread leaves the CRT's per-thread data uninitialised, which makes
    // the UCRT fail fast (0xC0000409) on the first formatted write.
    HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, WorkerThunk, nullptr, 0, nullptr);
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
