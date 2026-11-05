// dddaDuo - shared definitions
// Local split-screen co-op for Dragon's Dogma: Dark Arisen (32-bit, MT Framework)
#pragma once

#include <windows.h>

#define DDPUCK_CORE_DLL L"ddpuck_core.dll"

// Exported by ddpuck_core.dll. Called once by whichever loader got in first.
typedef void (WINAPI* PFN_DDPUCK_INIT)(HMODULE hSelf);

// Known-good offsets (community-verified, DDDA Steam 32-bit).
// pBase is resolved at runtime by AOB signature, never hardcoded as an address.
#define DDPUCK_OFF_PLAYER     0xA7000
#define DDPUCK_OFF_HP         0x96C   // float, current HP (verified in-game: 530.0)
#define DDPUCK_OFF_HP_MAX     0x970   // float, max HP (equal to HP at full health)
#define DDPUCK_OFF_MAIN_PAWN  0x7F0   // UNVERIFIED: reads 0x00000000 on build 2364871

static const BYTE DDPUCK_SIG_PBASE[] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,  // mov edx, dword ptr [pBase]
    0x33, 0xDB,                          // xor ebx, ebx
    0x8B, 0xF8                           // mov edi, eax
};
// 0x00 entries above are wildcards (the imm32 address varies per build).
static const char DDPUCK_SIG_PBASE_MASK[] = "xx????xxxx";
