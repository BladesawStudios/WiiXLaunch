#pragma once

// One mod, several game versions. A mod resolving raw offsets is written
// against one game build; pointed at another, addresses still resolve and
// mean something else with nothing crashing at the mistake.
//
// Declare the builds once, then one line per address with a column per
// build:
//
//     WIIXL_DECLARE_BUILDS("1.2.1", "1.2.0", "1.1.2");
//
//     //               name                       1.2.1       1.2.0       1.1.2
//     WIIXL_BUILD_ADDR(getUsedHouseUnitCountAll, 0x15a7e14, 0x15a7c10, 0x15a7808);
//     WIIXL_BUILD_ADDR(createHouseUnits,         0x159f640, 0x159f44c, 0x159f044);
//     WIIXL_BUILD_ADDR(uiRoomCapImmediate,       0x1b299ec, 0x1b28a40,         0);
//
// A mod doesn't choose its build name; the host fingerprints the running
// game and publishes what it enrolled, and these rows are matched against
// that. A short row is a compile error (checked against the declared build
// count). `0` means "not in this build," distinct from "unknown build," so a
// mod can skip one feature rather than refuse wholesale. Resolution is one
// string compare at startup, then array indexing.
//
// A declared patch can't use this: its record is data the loader applies
// before any module code runs, so there's no moment to consult the build.
// Hook targets, wiixl.call resolutions, and runtime patches can, since those
// run while the module is executing.

#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
#include <wiixlaunch/mod_runtime.h>
#endif

#include <wiixlaunch/mod_log.h>
#include <wiixlaunch/imports/wiixl_call.h>
#include <wiixlaunch/imports/wiixl_version.h>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch {

// Through volatile pointers, never called directly: a direct call emits a
// branch relocation that reaches at most 128 MB and can't name an arbitrary
// host address. Use the generated WXL_USE_* binding rather than declaring
// the import yourself.
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

// Which column applies to the build we're on, or -1. Resolved on first use,
// not in a constructor: a module's .init_array runs before the host has
// necessarily finished, and this asks the host a question.
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

    // The offset for this build, or 0.
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

    // The resolved address, or 0. A resolved 0 would hand back the image
    // base, a real and catastrophic address to hook, so zero never reaches
    // wiixl.call.
    uintptr_t Resolve() const {
        const uintptr_t off = Offset();
        if (off == 0) return 0;
        return VerImports::ResolveTarget(off, off);
    }

    // So an existing flat address header converts without moving call
    // sites: gf<Fn>(A::someAddress) and Install(A::someAddress, 0) keep
    // compiling.
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

// One address, one column per declared build, in the same order. 0 means
// the address does not exist on that build.
#define WIIXL_BUILD_ADDR(name, ...)                                            \
    inline constexpr uintptr_t name##_wiixl_offs[] = { __VA_ARGS__ };          \
    static_assert(sizeof(name##_wiixl_offs) / sizeof(uintptr_t) ==             \
                      wiixl_build_count,                                       \
                  #name ": needs exactly one address per declared build, in "  \
                        "the order WIIXL_DECLARE_BUILDS lists them");          \
    inline const ::WiiXLaunch::BuildAddr name{#name, name##_wiixl_offs}
