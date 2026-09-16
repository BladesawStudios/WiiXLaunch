// c_patch.wxlm - declared patches demonstration covering three outcomes:
// 1. APPLIED: inert instruction re-encoding (restored after verification)
// 2. ORIGIN-MISMATCH: declared origin does not match target memory
// 3. HOOKED-WINDOW: target address falls inside a displaced hook window

#include <cstdint>
#include <wiixlaunch/patch_decl.hpp>

extern "C" {
    extern void wiixl_import__wiixl_core__Log(const char* text);
}

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;

// 1. Applied: re-encode 'or r30,r4,r4' to 'ori r30,r4,0'.
WIIXL_DECLARE_PATCH(inert_reencode, 0x02000030,
    WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),
    WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));

// 2. Refused: origin mismatch (simulates wrong game version).
WIIXL_DECLARE_PATCH(wrong_build, 0x02000034,
    WIIXL_PATCH_BYTES(0x7C, 0x7F, 0x1B, 0x78),
    WIIXL_PATCH_BYTES(0x60, 0x00, 0x00, 0x00));

// 3. Refused: target falls inside GX2::Init hook window.
WIIXL_DECLARE_PATCH(into_hook_window, 0x03A75D4C,
    WIIXL_PATCH_BYTES(0xBA, 0xAD, 0xF0, 0x0D),
    WIIXL_PATCH_BYTES(0x60, 0x00, 0x00, 0x00));

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;
    log("c_patch: entry running; my patches were resolved before this line");
}
