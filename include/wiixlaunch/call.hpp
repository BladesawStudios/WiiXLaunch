#pragma once

#include "platform.hpp"
#include "offsets.hpp"

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

// Resolves a Ghidra offset to a callable/readable/writable address. Switch
// NSOs relocate per launch, so a Switch offset needs the runtime base added
// back; Wii U/Cemu RPXs load at a fixed address, so that offset is already
// final.

namespace WiiXLaunch {

inline uptr ResolveTarget(uptr offset) {
#if WIIXL_SWITCH
    return exl::util::modules::GetTargetStart() + offset;
#else
    return offset;
#endif
}

// Resolves a WIIXL_OFFSET-style (switchOffset, wiiuOffset) pair to a callable
// function pointer:
//   using GetUniqueNameFn = const char* (*)(void* actor);
//   auto getUniqueName = WiiXLaunch::GetTargetFunction<GetUniqueNameFn>(0x11c9bfc, 0x0);
template<typename FnPtr>
inline FnPtr GetTargetFunction(uptr switchOffset, uptr wiiuOffset) {
    return reinterpret_cast<FnPtr>(ResolveTarget(WIIXL_OFFSET(switchOffset, wiiuOffset)));
}

}
