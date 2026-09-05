#pragma once

// WiiXLaunch::ModContext - which module the host is currently running.
//
// ONE source of truth for a question three subsystems ask. The loader sets it
// around a module's entry; the hook manager attributes installs with it, the
// patch applier names owners with it, and the mod-scoped filesystem resolves
// paths under it. Null means "not inside a module" - the host itself.
//
// It lives in its own header because it is not any of those subsystems' idea.
// It used to be Hooks::CurrentOwner, which was accurate when hooks were the
// only thing that asked, and became a small lie the moment anything else did:
// a file read has no owner, it has a caller. Naming it after the question
// rather than the first asker is what stops the next subsystem from either
// reaching into the hook manager for an identity or inventing a second one that
// can drift out of step.
//
// THE ATTRIBUTION RULE APPLIES HERE ABOVE ALL. This is set by the HOST, from
// the module it chose to run - never from anything a module passes. See the ABI
// discipline block in wiixlaunch/loader/surface.hpp: an identity a module can
// assert makes every report and every containment check built on it worthless.

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
