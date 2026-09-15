#pragma once

// ONE MOD, SEVERAL GAME VERSIONS.
//
// A mod that resolves raw offsets - which is every mod for a game with no
// WiiXLaunch module - is written against ONE build. Point it at another and the
// addresses still resolve, still install, and mean something else. Nothing
// crashes at the mistake; something else does later, somewhere unrelated.
//
// Declare the builds once, then one line per address with a column per build:
//
//     WIIXL_DECLARE_BUILDS("1.2.1", "1.2.0", "1.1.2");
//
//     //               name                       1.2.1       1.2.0       1.1.2
//     WIIXL_BUILD_ADDR(getUsedHouseUnitCountAll, 0x15a7e14, 0x15a7c10, 0x15a7808);
//     WIIXL_BUILD_ADDR(createHouseUnits,         0x159f640, 0x159f44c, 0x159f044);
//     WIIXL_BUILD_ADDR(uiRoomCapImmediate,       0x1b299ec, 0x1b28a40,         0);
//
// That is the shape the data arrives in - a row per symbol, a column per
// version - and it drops onto an existing flat address header without moving
// any call site, because BuildAddr converts to uintptr_t.
//
// THE NAMES ARE THE HOST'S. A mod does not get to decide which build it is on:
// the host fingerprints the running game and publishes the name it enrolled,
// and these rows are matched against that. A mod cannot be talked into matching
// by a string it invented.
//
// THREE THINGS THIS BUYS over writing the addresses out flat:
//
//   * A short row is a COMPILE ERROR. The row is deduced as uintptr_t[N] and
//     checked against the declared build count, so pasting a line with a column
//     missing fails the build instead of silently zero-filling the last one.
//
//   * 0 MEANS "NOT IN THIS BUILD", which is not the same as "unknown build". A
//     mod can check one address and skip one feature rather than refusing
//     wholesale.
//
//   * ONE string compare, at startup. The column index is resolved once; every
//     lookup after that is an array index. An unrecognised build logs ONE line -
//     with fifty addresses, complaining per address would bury the reason in the
//     noise it caused.
//
// WHAT A DECLARED PATCH CANNOT DO. WIIXL_DECLARE_PATCH needs a compile-time
// constant, because the record is DATA the loader applies before any of the
// module's code runs - there is no moment at which it could consult the build.
// A versioned address cannot go in one. Hook targets, wiixl.call resolutions and
// runtime patches are all fine, because those happen while the module is
// running.

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
#include <wiixlaunch/mod_runtime.h>
#endif

#include <wiixlaunch/mod_log.h>
#include <wiixlaunch/imports/wiixl_call.h>
#include <wiixlaunch/imports/wiixl_version.h>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch {

// THROUGH VOLATILE POINTERS, NEVER CALLED DIRECTLY.
//
// The first version of this header called the imports directly and the packer
// refused it outright:
//
//   [wxlm] wiixl_import__wiixl_call__ResolveTarget is imported by
//          R_AARCH64_CALL26, which the loader cannot fix up.
//
// A direct call emits a branch relocation that reaches at most 128 MB and
// cannot name an arbitrary host address; the loader patches POINTERS. The
// generated headers ship a WXL_USE_* macro that binds one correctly, so use it
// rather than writing the declaration out. See docs/framework/modules.md.
namespace VerImports {
WXL_USE_wiixl_call(ResolveTarget);
WXL_USE_wiixl_version(Fingerprint);
WXL_USE_wiixl_version(Name);
WXL_USE_wiixl_version(Configured);
}  // namespace VerImports

namespace BuildTable {

inline const char* const* g_Names = nullptr;
inline uint32_t g_Count = 0;
inline int g_Column = -1;
inline bool g_Resolved = false;

inline bool Same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}

// Which column of every row applies to the build we are on, or -1.
//
// Resolved on first use rather than in a constructor: a module's .init_array
// runs before the host has necessarily finished, and this asks the host a
// question. Once is enough either way.
inline int Column() {
    if (g_Resolved) return g_Column;
    g_Resolved = true;

    if (g_Count == 0) {
        WIIXL_LOG("version: this mod declared no builds, so none of its "
                  "addresses can be selected - see WIIXL_DECLARE_BUILDS");
        return g_Column;
    }

    if (!VerImports::Configured()) {
        WIIXL_LOG("version: the host cannot fingerprint this game, so none of "
                  "this mod's %u build(s) can be matched - no address applies",
                  g_Count);
        return g_Column;
    }

    const char* name = VerImports::Name();
    if (!name) {
        WIIXL_LOG("version: the host does not recognise this build (0x%08X), so "
                  "it has no name to match - no address applies",
                  VerImports::Fingerprint());
        return g_Column;
    }

    for (uint32_t i = 0; i < g_Count; ++i) {
        if (g_Names[i] && Same(g_Names[i], name)) {
            g_Column = static_cast<int>(i);
            WIIXL_LOG("version: running on \"%s\", column %u of %u", name, i,
                      g_Count);
            return g_Column;
        }
    }

    // ONE line, not one per address. The list is named so the fix is obvious.
    WIIXL_LOG("version: this mod has no addresses for build \"%s\" - it knows "
              "%u build(s), first is \"%s\"", name, g_Count,
              g_Names[0] ? g_Names[0] : "?");
    return g_Column;
}

struct Init {
    Init(const char* const* names, uint32_t count) {
        g_Names = names;
        g_Count = count;
    }
};

} // namespace BuildTable

// One address across every declared build.
class BuildAddr {
public:
    template <size_t N>
    constexpr BuildAddr(const char* what, const uintptr_t (&offs)[N])
        : m_What(what), m_Offs(offs), m_Count(static_cast<uint32_t>(N)) {}

    // The offset for this build, or 0 - either because the build is unknown or
    // because this row has no address for it.
    uintptr_t Offset() const {
        const int col = BuildTable::Column();
        if (col < 0 || static_cast<uint32_t>(col) >= m_Count) return 0;
        const uintptr_t off = m_Offs[col];
        if (off == 0 && !m_Warned) {
            m_Warned = true;
            WIIXL_LOG("version: %s has no address on this build - whatever uses "
                      "it will be skipped", m_What);
        }
        return off;
    }

    // The resolved address, or 0. Resolving 0 would hand back the image base,
    // which is a real address and catastrophic to hook, so the zero never
    // reaches wiixl.call.
    uintptr_t Resolve() const {
        const uintptr_t off = Offset();
        if (off == 0) return 0;
        return VerImports::ResolveTarget(off, off);
    }

    // SO AN EXISTING ADDRESS HEADER CONVERTS WITHOUT MOVING ITS CALL SITES.
    // gf<Fn>(A::someAddress) and Install(A::someAddress, 0) keep compiling; the
    // number they get is now the one for the build underneath.
    operator uintptr_t() const { return Offset(); }

private:
    const char* m_What;
    const uintptr_t* m_Offs;
    uint32_t m_Count;
    mutable bool m_Warned = false;
};

} // namespace WiiXLaunch

// The builds this mod carries addresses for, in column order. Declare once,
// before any WIIXL_BUILD_ADDR.
#define WIIXL_DECLARE_BUILDS(...)                                              \
    inline constexpr const char* wiixl_build_names[] = { __VA_ARGS__ };        \
    inline constexpr uint32_t wiixl_build_count =                              \
        sizeof(wiixl_build_names) / sizeof(wiixl_build_names[0]);              \
    inline const ::WiiXLaunch::BuildTable::Init wiixl_build_init_{             \
        wiixl_build_names, wiixl_build_count}

// One address, one column per declared build, in the same order. 0 means the
// address does not exist on that build.
#define WIIXL_BUILD_ADDR(name, ...)                                            \
    inline constexpr uintptr_t name##_wiixl_offs[] = { __VA_ARGS__ };          \
    static_assert(sizeof(name##_wiixl_offs) / sizeof(uintptr_t) ==             \
                      wiixl_build_count,                                       \
                  #name ": needs exactly one address per declared build, in "  \
                        "the order WIIXL_DECLARE_BUILDS lists them");          \
    inline const ::WiiXLaunch::BuildAddr name{#name, name##_wiixl_offs}
