// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.audio v1.0, 3 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_audio(Available); }
//     S::Available(...);
//
// so a mod that uses two symbols imports two, not all 3.
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// 1 when sounds can play here right now: sndcore2 reached and AX initialised by the game.
extern uint32_t wiixl_import__wiixl_audio__Available(void);

// Loads a WXAU file from the calling mod's own directory (scoped like wiixl.core's ModReadFile) into host memory, once,
// and returns a sound handle, or 0 when it cannot be read or is not WXAU. Load at entry: the samples come from the game's
// own heap and are never freed.
extern uint32_t wiixl_import__wiixl_audio__LoadSound(const char* path);

// Plays a loaded sound once, at `volume` (1 = as recorded), on the TV and the GamePad. Returns 1 if it started.
extern uint32_t wiixl_import__wiixl_audio__PlaySound(uint32_t sound, float volume);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.audio@1.0 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_audio {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 0;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_audio(sym) \
    inline decltype(&wiixl_import__wiixl_audio__##sym) volatile sym = \
        &wiixl_import__wiixl_audio__##sym
