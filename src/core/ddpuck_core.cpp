// dddaDuo core - Fase A
// Resolves the game's player base at runtime (AOB signature) and logs live player state.
// Deliberately does NOT write to game memory yet: Fase A only proves we can find and read it.

#include "../common/ddpuck.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <process.h>
#include <share.h>
#include <tlhelp32.h>

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

// ---------------------------------------------------------------- probe
// Guard-page tracer: marks the 4 KB page holding a target field as PAGE_GUARD.
// The first access faults; the VEH logs who touched it and re-arms the guard.
// Writes only are logged, and our own module is filtered out.
// Enabled by `[probe] enabled=1` in ddpuck.ini next to the executable.

// Reads a boolean flag from ddpuck.ini (game directory).
static bool ReadIniFlag(const wchar_t* key) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* s = wcsrchr(path, L'\\');
    if (s) *(s + 1) = L'\0';
    wcscat_s(path, MAX_PATH, L"ddpuck.ini");

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(L"probe", key, L"0", buf, 64, path);
    return buf[0] == L'1';
}

static int ReadIniInt(const wchar_t* key, int def) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* s = wcsrchr(path, L'\\');
    if (s) *(s + 1) = L'\0';
    wcscat_s(path, MAX_PATH, L"ddpuck.ini");

    wchar_t defs[32] = {0};
    _itow_s(def, defs, 32, 10);
    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(L"probe", key, defs, buf, 64, path);
    return _wtoi(buf);
}

// 0 = arisen_hp, 1 = pawn_hp
static int ReadIniTarget() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* s = wcsrchr(path, L'\\');
    if (s) *(s + 1) = L'\0';
    wcscat_s(path, MAX_PATH, L"ddpuck.ini");

    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(L"probe", L"target", L"pawn_hp", buf, 64, path);
    return (buf[0] == L'a') ? 0 : 1;
}

#ifndef STATUS_GUARD_PAGE_VIOLATION
#define STATUS_GUARD_PAGE_VIOLATION ((DWORD)0x80000001L)
#endif
#ifndef STATUS_SINGLE_STEP
#define STATUS_SINGLE_STEP ((DWORD)0x80000004L)
#endif

static BYTE*  g_moduleBase = nullptr;
static DWORD  g_moduleSize = 0;
static HMODULE g_hCoreMod = nullptr;
static volatile LONG g_probeOn = 0;
static DWORD  g_probePage = 0;
static DWORD  g_probeOrigProtect = 0;
static DWORD  g_probeSeen[256];
static int    g_probeSeenN = 0;
static volatile LONG g_probeWrites = 0;
static volatile LONG g_probeReads = 0;
static CRITICAL_SECTION g_probeCs;

static bool InOurModule(DWORD eip) {
    DWORD c = (DWORD)(uintptr_t)g_hCoreMod;
    return g_hCoreMod && eip >= c && eip < c + 0x100000;
}

// Records one hit per code site, logging the first time each one appears.
static void LogSiteOnce(DWORD rva, const char* kind) {
    bool known = false;
    EnterCriticalSection(&g_probeCs);
    for (int i = 0; i < g_probeSeenN; ++i) if (g_probeSeen[i] == rva) { known = true; break; }
    if (!known && g_probeSeenN < 256) g_probeSeen[g_probeSeenN++] = rva;
    InterlockedIncrement(&g_probeWrites);
    LeaveCriticalSection(&g_probeCs);
    if (!known) Log("probe: %s rva=0x%X eip=%p", kind, rva, (void*)(uintptr_t)((DWORD)(uintptr_t)g_moduleBase + rva));
}

static LONG CALLBACK VehHandler(PEXCEPTION_POINTERS p) {
    const DWORD code = p->ExceptionRecord->ExceptionCode;

    // --- hardware breakpoint (Dr0): fires only on the exact watched address ---
    if (code == STATUS_SINGLE_STEP) {
        CONTEXT* c = p->ContextRecord;
        if ((c->Dr6 & 0x1) && g_probeOn) {
            DWORD eip = (DWORD)c->Eip;
            if (!InOurModule(eip)) LogSiteOnce(eip - (DWORD)(uintptr_t)g_moduleBase, "HW WRITE");
            c->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // --- page guard (legacy mode) ---
    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        DWORD addr = (DWORD)(p->ExceptionRecord->ExceptionInformation[1]);
        if (g_probePage && (addr & ~0xFFFu) == g_probePage) {
            ULONG_PTR access = p->ExceptionRecord->ExceptionInformation[0];
            DWORD eip = (DWORD)p->ContextRecord->Eip;
            if (g_probeOn && access == 1 && !InOurModule(eip)) {
                LogSiteOnce(eip - (DWORD)(uintptr_t)g_moduleBase, "PAGE WRITE");
            }
            // ALWAYS settle the protection. If the guard stays set while g_probeOn
            // is 0, the next access is an unhandled 0x80000001 and the game dies.
            DWORD old = 0;
            VirtualProtect((LPVOID)(uintptr_t)g_probePage, 0x1000,
                           g_probeOn ? (PAGE_READWRITE | PAGE_GUARD) : g_probeOrigProtect, &old);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// Installs (or clears) a 4-byte write hardware breakpoint on DR0 in every thread
// of this process. Hardware breakpoints fire only on the exact address, so unlike
// a PAGE_GUARD they do not turn every access into an exception.
static int SetHwBpAllThreads(bool enable, DWORD addr) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te;
    ZeroMemory(&te, sizeof(te));
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    int count = 0;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                  FALSE, te.th32ThreadID);
            if (!h) continue;

            SuspendThread(h);
            CONTEXT ctx;
            ZeroMemory(&ctx, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                if (enable) {
                    ctx.Dr0 = addr;
                    // L0=1, RW0=01 (write), LEN0=11 (4 bytes)
                    ctx.Dr7 |= 0x1u | (0x1u << 16) | (0x3u << 18);
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr7 &= ~(0x1u | (0x3u << 16) | (0x3u << 18));
                }
                SetThreadContext(h, &ctx);
                ++count;
            }
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return count;
}

static unsigned __stdcall ProbeStopper(void* param) {
    Sleep((DWORD)(uintptr_t)param * 1000);
    InterlockedExchange(&g_probeOn, 0);

    if (g_probePage) {
        DWORD old = 0;
        VirtualProtect((LPVOID)(uintptr_t)g_probePage, 0x1000, g_probeOrigProtect, &old);
    }
    SetHwBpAllThreads(false, 0);

    Log("probe: finished (%d distinct write sites, %ld hits)",
        g_probeSeenN, g_probeWrites);
    return 0;
}

// mode 0 = hardware breakpoint (default, exact address), 1 = page guard (legacy)
static bool ProbeArm(BYTE* target, DWORD seconds, int mode) {
    if (!target) return false;
    InitializeCriticalSection(&g_probeCs);
    g_probePage = 0;

    if (mode == 1) {
        DWORD page = (DWORD)(uintptr_t)target & ~0xFFFu;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)(uintptr_t)page, &mbi, sizeof(mbi))) { Log("probe: VirtualQuery failed"); return false; }
        if (mbi.State != MEM_COMMIT) { Log("probe: page not committed"); return false; }
        g_probePage = page;
        if (!VirtualProtect((LPVOID)(uintptr_t)page, 0x1000, PAGE_READWRITE | PAGE_GUARD, &g_probeOrigProtect)) {
            Log("probe: VirtualProtect failed (%lu)", GetLastError());
            g_probePage = 0;
            return false;
        }
        Log("probe: armed (page guard) on page 0x%X (target %p) for %lu s", page, target, seconds);
    } else {
        int n = SetHwBpAllThreads(true, (DWORD)(uintptr_t)target);
        if (n <= 0) { Log("probe: could not set DR0 on any thread"); return false; }
        Log("probe: armed (hw bp DR0) on %p in %d thread(s) for %lu s", target, n, seconds);
    }

    InterlockedExchange(&g_probeOn, 1);
    HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, ProbeStopper, (void*)(uintptr_t)seconds, 0, nullptr);
    if (h) CloseHandle(h);
    return true;
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

    g_moduleBase = (BYTE*)GetModuleHandleW(nullptr);
    {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_moduleBase;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_moduleBase + dos->e_lfanew);
        g_moduleSize = nt->OptionalHeader.SizeOfImage;
    }

    const bool probeEnabled = ReadIniFlag(L"enabled");
    const int  probeTarget  = ReadIniTarget();
    const int  probeSeconds = ReadIniInt(L"seconds", 30);
    if (probeEnabled) {
        AddVectoredExceptionHandler(1, VehHandler);
        Log("probe: enabled via ddpuck.ini (target=%s, %d s)",
            probeTarget == 0 ? "arisen_hp" : "pawn_hp", probeSeconds);
    }
    bool probeArmed = false;

    if (!raw || (uintptr_t)raw < 0x10000) {
        Log("pBase global at %p not populated yet (%p). Will poll.", pBase, raw);
    } else {
        Log("pBase global at %p -> player base %p", pBase, raw);
    }

    CharView  last[DDPUCK_PARTY_SLOTS];
    bool  warnedNotReady = false;
    bool  firstPass = true;
    for (;;) {
        if (g_probeOn) { Sleep(500); continue; } // never touch the guarded page ourselves

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

            // Once the target's HP is real (not the 1.0 placeholder), start hunting
            // for the code that writes it.
            const int tgt = probeTarget;
            if (probeEnabled && !probeArmed && now[tgt].ok && now[tgt].hpMax > 1.0f) {
                BYTE* field = cur + DDPUCK_OFF_PLAYER
                            + (size_t)tgt * DDPUCK_STRIDE_CHARACTER + DDPUCK_OFF_HP;
                Log("probe: %s HP field at %p", tgt == 0 ? "arisen" : "pawn", field);
                if (ProbeArm(field, probeSeconds)) probeArmed = true;
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
    g_hCoreMod = hSelf;
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
