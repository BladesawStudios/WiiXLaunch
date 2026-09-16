#pragma once

// WiiXLaunch::ModContext - which module the host is currently running.
//
// One source of truth for a question the hook manager, the patch applier,
// and the mod-scoped filesystem all ask. Set by the host from the module it
// chose to run, never from anything a module passes (see the ABI
// discipline block in wiixlaunch/loader/surface.hpp). Null means "not
// inside a module."

#include <wiixlaunch/platform.hpp>

#include <cstdint>

namespace WiiXLaunch::ModContext {

// Matches the loader's LoadedModule::id: 16 characters plus a terminator.
constexpr uint32_t kMaxIdLen = 17;

namespace impl {
inline const char* g_Current = nullptr;
}

// Set by the loader around a module's entry, and cleared afterwards so the next
// module cannot inherit it.
inline void SetCurrent(const char* id) { impl::g_Current = id; }

// The module being run, or null when the host itself is running.
inline const char* Current() { return impl::g_Current; }

inline bool InsideModule() { return impl::g_Current != nullptr; }

} // namespace WiiXLaunch::ModContext
