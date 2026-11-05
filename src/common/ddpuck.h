// dddaDuo - shared definitions
// Local split-screen co-op for Dragon's Dogma: Dark Arisen (32-bit, MT Framework)
#pragma once

#include <windows.h>

#define DDPUCK_CORE_DLL L"ddpuck_core.dll"

// Exported by ddpuck_core.dll. Called once by whichever loader got in first.
typedef void (WINAPI* PFN_DDPUCK_INIT)(HMODULE hSelf);

// Known-good offsets (community-verified, DDDA Steam 32-bit).
// pBase is resolved at runtime by AOB signature, never hardcoded as an address.
// Known-good offsets (verified in-game, DDDA Steam build 2364871, x86).
// pBase is resolved at runtime by AOB signature, never hardcoded as an address.
// Chain: base = *(void**)pBaseGlobal; field = *(base + OFF_PLAYER + slot*STRIDE + fieldOff).
#define DDPUCK_OFF_PLAYER        0xA7000  // first character block = the Arisen
#define DDPUCK_STRIDE_CHARACTER  0x7F0    // stride between party slots (verified)
#define DDPUCK_PARTY_SLOTS       6

// Offsets inside a character block
#define DDPUCK_OFF_HP            0x96C    // float, current HP  (verified: 530.0)
#define DDPUCK_OFF_HP_MAX        0x970    // float, max HP
#define DDPUCK_OFF_STAMINA       0x974    // float, current stamina
#define DDPUCK_OFF_STAMINA_MAX   0x978    // float, max stamina

static const BYTE DDPUCK_SIG_PBASE[] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,  // mov edx, dword ptr [pBase]
    0x33, 0xDB,                          // xor ebx, ebx
    0x8B, 0xF8                           // mov edi, eax
};
// 0x00 entries above are wildcards (the imm32 address varies per build).
static const char DDPUCK_SIG_PBASE_MASK[] = "xx????xxxx";
