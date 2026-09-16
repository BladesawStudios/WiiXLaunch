#pragma once

// WiiXLaunch::Surface - the export registry mods resolve their imports
// against. A surface is a (name, version, symbol table) triple registered
// at host init:
//
//     wiixl.core   v1.0   the base framework's own services
//     botw.gx2     v1.0   registered by vendor/wiixlaunch-botw
//
// A mod declares which surfaces and versions it needs; the loader resolves
// each import as a (surface name, symbol hash) lookup, so a mod requiring
// botw.gx2 on a host with no BotW module fails cleanly by name. Base never
// knows which game surfaces exist; it owns this mechanism and wiixl.core.
//
// ABI discipline, read before adding a symbol to any surface: a surface's
// signature IS an ABI once mods are compiled binaries, and there's no link
// step to catch a mismatch.
//
//   * Never pass or return a struct by value. A layout change silently
//     breaks every compiled mod with no build error.
//   * Pass opaque handles plus accessor calls instead.
//   * Primitives, pointers-to-opaque, const char*, and out-parameters are
//     fine.
//   * Symbol tables are append-only within a major version. Adding bumps
//     the minor; changing or removing bumps the major.
//   * No varargs: formatting conventions differ between host and mod
//     builds and a mismatch is unfixable at the boundary.
//
// Attribution comes from what the host observes, never from what a mod
// claims: no surface entry takes "who is calling" as a parameter, since a
// mod could pass anything and every report built on it would be
// unfalsifiable. wiixl.core's InstallHook takes (target, callback), not an
// owner - the loader's own notion of the running module supplies that. The
// general shape: where a mod supplies a token (a tag, a handle, an id),
// bind it to the caller at a moment the host knows who that is, and check
// later uses against the binding.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

// Declares that a game module is compiled into this build, as opposed to
// merely present on disk. scripts/deploy.py scans vendor/wiixlaunch-*/ and
// needs a way to tell "this module is vendored but unused" from "this
// module is used but its shim-table symbol got dropped" - this marker
// symbol is the difference. A module calls it once from its umbrella
// header; the name must match the vendor directory suffix
// (vendor/wiixlaunch-botw declares WIIXL_DECLARE_MODULE(botw)).
#define WIIXL_DECLARE_MODULE(name) \
    extern "C" { __attribute__((section(".data"), used)) \
        inline uint32_t g_WiiXLaunchModule_##name = 1; }

namespace WiiXLaunch::Surface {

// FNV-1a, 32-bit. constexpr so an import site hashes its symbol name at
// build time and the runtime lookup is an integer compare. Register()
// rejects a table with a duplicate hash rather than letting two symbols
// silently share a slot.
constexpr uint32_t Hash(const char* s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) {
        h ^= static_cast<uint32_t>(static_cast<unsigned char>(*s));
        h *= 0x01000193u;
    }
    return h;
}

struct Symbol {
    uint32_t hash;
    const void* fn;
};

// Builds one Symbol entry. The name is hashed at compile time.
#define WIIXL_SURFACE_SYMBOL(name, fn) \
    ::WiiXLaunch::Surface::Symbol{ ::WiiXLaunch::Surface::Hash(name), \
                                   reinterpret_cast<const void*>(fn) }

struct Registration {
    const char* name;        // "wiixl.core", "botw.gx2"
    uint16_t versionMajor;   // breaking change - a mod requiring N needs exactly N
    uint16_t versionMinor;   // additive - a mod requiring N needs host >= N
    const Symbol* symbols;   // static table, must outlive the host (it always does)
    uint32_t symbolCount;
};

namespace impl {

// Fixed capacity, no allocation; registration happens once at host init,
// before any arena exists. scripts/surface_coverage.py counts declared
// surfaces at build time and fails before this cap can be silently
// exceeded.
constexpr uint32_t kMaxSurfaces = 48;

inline Registration g_Surfaces[kMaxSurfaces];
inline uint32_t g_SurfaceCount = 0;

// Registrations the host built and then refused. Count() and the surface
// list both report only what IS registered, so a mod rejected for a
// missing surface can look identical to one asking for something the host
// was never built with - this makes the truncation itself visible.
inline uint32_t g_SurfaceRefused = 0;

inline bool NameEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
}

} // namespace impl

inline const Registration* Find(const char* name) {
    for (uint32_t i = 0; i < impl::g_SurfaceCount; ++i) {
        if (impl::NameEquals(impl::g_Surfaces[i].name, name)) return &impl::g_Surfaces[i];
    }
    return nullptr;
}

// Registers a surface. Called by the host at init, before any mod is
// loaded. Rejects a duplicate surface name, a duplicate symbol hash within
// one table, or a full registry, rather than half-registering.
inline bool Register(const Registration& reg) {
    if (!reg.name || !reg.symbols) {
        ++impl::g_SurfaceRefused;
        WIIXL_LOG("Surface: rejected a registration with no name or no table");
        return false;
    }
    if (Find(reg.name)) {
        ++impl::g_SurfaceRefused;
        WIIXL_LOG("Surface: '%s' is already registered - refusing the second one", reg.name);
        return false;
    }
    if (impl::g_SurfaceCount >= impl::kMaxSurfaces) {
        ++impl::g_SurfaceRefused;
        WIIXL_LOG("Surface: registry full (%u), cannot register '%s'",
                  impl::kMaxSurfaces, reg.name);
        WIIXL_LOG("Surface:   every mod requiring it will now be rejected as if this "
                  "host never had it. Raise kMaxSurfaces in loader/surface.hpp.");
        return false;
    }
    for (uint32_t i = 0; i < reg.symbolCount; ++i) {
        for (uint32_t j = i + 1; j < reg.symbolCount; ++j) {
            if (reg.symbols[i].hash == reg.symbols[j].hash) {
                WIIXL_LOG("Surface: '%s' has a duplicate symbol hash 0x%08X at %u and %u "
                          "- a collision or the same name twice; refusing to register",
                          reg.name, reg.symbols[i].hash, i, j);
                ++impl::g_SurfaceRefused;
                return false;
            }
        }
    }

    impl::g_Surfaces[impl::g_SurfaceCount++] = reg;
    WIIXL_LOG("Surface: registered %s v%u.%u (%u symbols)",
              reg.name, reg.versionMajor, reg.versionMinor, reg.symbolCount);
    return true;
}

// Resolves one import. Returns null if the surface is absent or has no such
// symbol - the caller reports which, since it knows what it was looking for.
inline const void* Resolve(const char* surfaceName, uint32_t symbolHash) {
    const Registration* s = Find(surfaceName);
    if (!s) return nullptr;
    for (uint32_t i = 0; i < s->symbolCount; ++i) {
        if (s->symbols[i].hash == symbolHash) return s->symbols[i].fn;
    }
    return nullptr;
}

// Typed convenience for host-side and module-side callers.
template <typename FnPtr>
inline FnPtr ResolveAs(const char* surfaceName, const char* symbolName) {
    return reinterpret_cast<FnPtr>(Resolve(surfaceName, Hash(symbolName)));
}

// Does the host satisfy a requirement? Major must match exactly (breaking
// changes); minor must be at least what was asked for, since surfaces are
// append-only within a major - a single exact-match version would force
// every appended symbol to break every existing compiled mod.
//
// Three refusals as a value, not only a log line, so a test can assert
// which one fired: a surface nobody registered, a major the host can't
// satisfy in either direction, and a minor the host merely predates.
enum class Compat : uint32_t {
    Ok = 0,
    NotPresent,      // no surface of that name is registered
    MajorMismatch,   // a different major - incompatible in either direction
    MinorTooOld,     // right major, but the host predates a symbol it needs
};

inline const char* CompatName(Compat c) {
    switch (c) {
        case Compat::Ok:            return "OK";
        case Compat::NotPresent:    return "NOT-PRESENT";
        case Compat::MajorMismatch: return "MAJOR-MISMATCH";
        case Compat::MinorTooOld:   return "MINOR-TOO-OLD";
    }
    return "?";
}

// The decision, with no logging, so a test can assert the REASON.
inline Compat Check(const char* name, uint16_t major, uint16_t minor) {
    const Registration* s = Find(name);
    if (!s) return Compat::NotPresent;

    // Not `<`: a different major is incompatible whichever side is higher.
    if (s->versionMajor != major) return Compat::MajorMismatch;

    // Minor-at-least: the host may be newer, never older.
    if (s->versionMinor < minor) return Compat::MinorTooOld;

    return Compat::Ok;
}

inline bool Require(const char* name, uint16_t major, uint16_t minor) {
    const Registration* s = Find(name);
    const Compat c = Check(name, major, minor);
    switch (c) {
        case Compat::Ok:
            return true;
        case Compat::NotPresent:
            WIIXL_LOG("Surface: %s - requires %s v%u.%u, and no surface of that name is "
                      "registered on this host", CompatName(c), name, major, minor);
            return false;
        case Compat::MajorMismatch:
            WIIXL_LOG("Surface: %s - requires %s v%u.%u, host has v%u.%u. A different "
                      "major means a symbol changed or was removed, so this is "
                      "incompatible in either direction, not merely old.",
                      CompatName(c), name, major, minor,
                      s->versionMajor, s->versionMinor);
            return false;
        case Compat::MinorTooOld:
            WIIXL_LOG("Surface: %s - requires %s v%u.%u, host has v%u.%u. The host "
                      "predates a symbol this module was built against; a newer host "
                      "would load it.", CompatName(c), name, major, minor,
                      s->versionMajor, s->versionMinor);
            return false;
    }
    return false;
}

inline uint32_t Count() { return impl::g_SurfaceCount; }

// Logged at the load point so a rejected mod can be diagnosed from the log
// alone.
inline void LogRegistered() {
    WIIXL_LOG("Surface: %u surface(s) registered on this host:", impl::g_SurfaceCount);
    for (uint32_t i = 0; i < impl::g_SurfaceCount; ++i) {
        const Registration& r = impl::g_Surfaces[i];
        WIIXL_LOG("Surface:   %s v%u.%u (%u symbols)",
                  r.name, r.versionMajor, r.versionMinor, r.symbolCount);
    }
    if (impl::g_SurfaceRefused) {
        WIIXL_LOG("Surface: INCOMPLETE - %u built by this host, REFUSED at "
                  "registration (capacity %u).",
                  impl::g_SurfaceRefused, impl::kMaxSurfaces);
        WIIXL_LOG("Surface:   the list above is what survived, not what was written. "
                  "A mod rejected for a missing surface may be asking for a refused "
                  "one.");
    }
    if (impl::g_SurfaceCount <= 1) {
        WIIXL_LOG("Surface: no GAME surfaces registered - this host has no game module "
                  "installed, so any mod requiring one will be rejected by name.");
    }
}

} // namespace WiiXLaunch::Surface
