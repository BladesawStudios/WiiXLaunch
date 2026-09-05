#pragma once

// WiiXLaunch::Surface - the export registry mods resolve their imports against.
//
// The export table is a REGISTRY, not a fixed struct. A surface is a
// (name, version, symbol table) triple registered at host init:
//
//     wiixl.core   v1.0   the base framework's own services
//     botw.gx2     v1.0   registered by vendor/wiixlaunch-botw
//     botw.gui     v1.0   ...
//
// A mod declares which surfaces and versions it needs. The loader resolves each
// import as a (surface name, symbol hash) lookup, so a mod built against
// botw.gx2 and loaded into a host with no BotW module fails cleanly with
// "requires botw.gx2 v1, not present" instead of jumping into nothing.
//
// Base never knows which game surfaces exist. It owns this mechanism and
// wiixl.core; everything else is registered by whoever was installed.
//
// ---------------------------------------------------------------------------
// ABI DISCIPLINE - read before adding a symbol to any surface.
//
// Once mods are compiled binaries, a surface's signature IS an ABI. A mod is
// built against one header and run against a host built from another, with no
// link step to catch a mismatch. So:
//
//   * NEVER pass or return a struct by value across a surface. A layout change
//     silently breaks every compiled mod with no build error anywhere.
//     gui_types.hpp's Canvas is the obvious trap.
//   * Pass opaque handles plus accessor calls instead. A handle is an integer
//     the host understands and the mod does not.
//   * Primitives and pointers-to-opaque are fine. So are const char*, and
//     out-parameters written through a pointer.
//   * Symbol tables are APPEND-ONLY within a major version. Adding an entry
//     bumps the minor; changing or removing one bumps the major.
//   * No varargs. Formatting conventions differ between host and mod builds,
//     and a vararg mismatch is unfixable at the boundary. wiixl.core's Log
//     takes a finished string.
//
// ---------------------------------------------------------------------------
// ATTRIBUTION COMES FROM WHAT THE HOST OBSERVES, NEVER FROM WHAT A MOD CLAIMS.
//
// No surface entry takes "who is calling" as a parameter. If it did, a mod
// could pass any value it liked, and every report built on that value would be
// unfalsifiable - which is to say worthless, because the only thing a report is
// for is being checkable against reality.
//
// The host already knows who is calling: the loader sets the current module
// around that module's entry, so identity is something the host established by
// running the code, not something the code asserted about itself.
//
// Two entries follow this rule and neither is an accident:
//
//   * wiixl.core InstallHook takes (target, callback) and NOT an owner. The
//     hook is attributed to whichever module the loader is running. This is
//     what makes "SHARED TARGET ... call order: a -> b" evidence rather than
//     hearsay - and naming the mods in a conflict is the entire reason the hook
//     registry is central.
//   * wiixl.core HookProbeClaimTag binds a marker tag to the calling module at
//     CLAIM time, under host observation, and refuses a tag someone else
//     already holds. The ordering assertion in wiixlaunch/hook_probe.hpp is
//     then against the host's own record.
//
// The general shape: where a mod must supply a token (a tag, a handle, an id),
// BIND it to the caller at a moment when the host knows who that is, and check
// later uses against the binding. Never accept the identity itself.
// ---------------------------------------------------------------------------

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

// Declares that a game module is COMPILED INTO this build, as opposed to merely
// being present on disk.
//
// scripts/deploy.py splices every vendor/wiixlaunch-*/src/cemu/*.asm it finds
// into the code cave, because it scans directories. That leaves it unable to
// tell two very different situations apart when a module's shim-table offset
// symbol is missing:
//
//   the module is vendored but this project does not use it   - fine, skip it
//   the module IS used but its global got dropped             - a bug, stop
//
// Both used to print the same "skipping" line, and only one of them is
// acceptable. That is the same silent-success shape that let the memory shims
// ship unreachable and the load-point probe fail to link, so it is settled
// mechanically rather than left to whoever reads the log: this macro emits a
// marker symbol, deploy.py looks for it, and the answer is unambiguous.
//
// A module calls it once from its umbrella header. The name must match the
// vendor directory suffix - vendor/wiixlaunch-botw declares WIIXL_DECLARE_MODULE(botw).
#define WIIXL_DECLARE_MODULE(name) \
    extern "C" { __attribute__((section(".data"), used)) \
        inline uint32_t g_WiiXLaunchModule_##name = 1; }

namespace WiiXLaunch::Surface {

// FNV-1a, 32-bit. constexpr so an import site hashes its symbol name at build
// time and the runtime lookup is an integer compare.
//
// 32 bits is ample for the few hundred symbols a host will ever register, but
// "ample" is not "impossible" - Register() rejects a table containing a
// duplicate hash rather than letting two symbols silently share a slot.
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

// Fixed capacity, no allocation. Registration happens once at host init, long
// before any arena exists.
//
// This said 16, and said a host with more than 16 surfaces had a design problem
// rather than a capacity problem. Then the BotW module grew to 18 surfaces and
// the base to 6, and eight of them were refused in registration order - so the
// host advertised botw.player through botw.events and simply did not exist from
// botw.sound onward. Every refusal was logged correctly and the mod that needed
// one was rejected by name; none of that made the CAP visible, because the
// count that mattered lived in a different file from the number it exceeded.
//
// The figure now has headroom, and scripts/surface_coverage.py counts the
// declared surfaces at build time and fails before a boot can find this again.
constexpr uint32_t kMaxSurfaces = 48;

inline Registration g_Surfaces[kMaxSurfaces];
inline uint32_t g_SurfaceCount = 0;

// Registrations the host built and then refused, for any reason. A truncated
// registry has a loud symptom at the moment of refusal and an invisible one
// afterwards: Count() and the surface list both read as healthy, because they
// report what IS registered, and a mod rejected for a missing surface looks
// exactly like a mod asking for something this host was never built with.
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

// Registers a surface. Called by the host at init, before any mod is loaded.
//
// Rejects, loudly, rather than half-registering:
//   - a duplicate surface name (two modules claiming botw.gx2)
//   - a duplicate symbol hash within one table (a real collision, or the same
//     name listed twice)
//   - a full registry
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
// changes); minor must be at least what was asked for (surfaces are
// append-only within a major). Logs the reason it does not.
//
// WHY TWO NUMBERS rather than one exact-match version, settled and not to be
// relitigated: a single version compared exactly would have to be bumped every
// time anyone appends a symbol, and every bump rejects every existing compiled
// mod - including the ones that never referenced the new symbol and are
// entirely compatible with it. The predictable outcome is that people stop
// appending, and the surface ossifies or grows by mutation instead, which is
// the failure this whole scheme exists to prevent.
//
// Splitting the number lets additions be free and breakage be explicit:
// appending a symbol bumps the minor and every existing mod keeps resolving;
// changing or removing one bumps the major and mods built against the old
// shape are rejected by name rather than calling the wrong function. This is
// what stable plugin ABIs converge on, for exactly this reason.
// WHY a requirement was refused, as a value rather than only as a log line.
//
// The three refusals are genuinely different diagnoses - a surface nobody
// registered, a major the host cannot satisfy at all, and a minor the host is
// simply older than - and they collapsed into one bool, with the distinction
// living only in a WIIXL_LOG string. That string is compiled out on the host
// test, so the difference was untestable: the version check could have been
// giving the right answer for the wrong reason and nothing could tell.
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

    // NOT `<`. A different major means a symbol changed shape or went away, and
    // that is incompatible whichever side is higher - a mod built for v2 on a
    // v1 host would call functions that do not exist, and a mod built for v0 on
    // a v1 host would call functions that no longer mean what it thinks.
    if (s->versionMajor != major) return Compat::MajorMismatch;

    // Minor-at-least: the host may be newer, never older. Appending a symbol
    // bumps the minor, so a host with a lower minor is missing something the
    // mod was built against.
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
// alone: the host states its version and everything it offers, and the loader
// then names what a mod asked for that is not in this list.
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
