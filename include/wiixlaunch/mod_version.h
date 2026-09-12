#pragma once

// ONE MOD, SEVERAL GAME VERSIONS.
//
// A mod that resolves raw offsets - which is every mod for a game with no
// WiiXLaunch module yet - is written against one build. Point it at another and
// the addresses still resolve, still install, and mean something else. Nothing
// crashes at the mistake; something else does, later, somewhere unrelated.
//
// This is the table that makes that a refusal instead:
//
//     static constexpr WiiXLaunch::BuildOffset kRoomCap[] = {
//         { "1.2.1", 0x01B299EC },
//         { "1.2.0", 0x01B28A40 },
//     };
//     static const WiiXLaunch::VersionedOffset g_RoomCap("roomCap", kRoomCap);
//
//     uintptr_t addr = g_RoomCap.Resolve();   // 0, and a log line, on anything else
//
// The names are the ones the HOST enrolled, from its target file's
// `identity.known`. That is deliberate: the mod does not get to decide what
// build it is on, and cannot be talked into matching by a string it made up.
//
// WHAT AN UNRECOGNISED BUILD DOES. Nothing - Resolve returns 0 and says why. A
// host that does not recognise the build has no name to match, so no row can
// apply, and applying one anyway would be the guess this exists to prevent. The
// log distinguishes the three reasons, because they want three different fixes:
//
//     version: roomCap - this host cannot fingerprint the game ...
//     version: roomCap - the host does not recognise this build (0x1A2B3C4D) ...
//     version: roomCap has no row for build "1.2.0" (2 known) ...
//
// The first is a target that declares no identity slice; the second is a build
// nobody has enrolled; the third is an enrolled build this particular mod was
// never written for. Only the third is the mod's problem.
//
// HOOKS AND PATCHES STILL CHECK THEIR OWN WORK. This narrows the window rather
// than closing it: a declared patch still names its origin bytes, and the host
// still reads a patch back after writing it. A version table is the cheapest
// check and the coarsest, and it is not a reason to skip the others.

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
// rather than writing the declaration out. See docs/modules.md.
namespace VerImports {
WXL_USE_wiixl_call(ResolveTarget);
WXL_USE_wiixl_version(Fingerprint);
WXL_USE_wiixl_version(Name);
WXL_USE_wiixl_version(Configured);
}  // namespace VerImports

// One row: the build name the host enrolled, and the offset for it.
//
// `offset` is what wiixl.call means by an offset on THIS platform - a module
// offset on Switch, an absolute address on Wii U and Cemu. A table is for one
// build of one game, and a game is on one platform, so there is nothing to
// split here the way WIIXL_OFFSET has to.
struct BuildOffset {
    const char* build;
    uintptr_t   offset;
};

class VersionedOffset {
public:
    template <size_t N>
    VersionedOffset(const char* what, const BuildOffset (&table)[N])
        : m_What(what), m_Table(table), m_Count(N) {}

    // The raw offset for the running build, or 0.
    uintptr_t Offset() const {
        const char* name = Build();
        if (!name) return 0;
        for (size_t i = 0; i < m_Count; ++i) {
            if (m_Table[i].build && Same(m_Table[i].build, name))
                return m_Table[i].offset;
        }
        Complain(name);
        return 0;
    }

    // The resolved address, or 0. Resolving 0 would hand back the image base,
    // which is a real address and would be catastrophic to hook, so the zero is
    // checked before it ever reaches wiixl.call.
    uintptr_t Resolve() const {
        const uintptr_t off = Offset();
        if (off == 0) return 0;
        return VerImports::ResolveTarget(off, off);
    }

    // The build name, or nullptr - and it complains once about why not.
    const char* Build() const {
        if (!VerImports::Configured()) {
            if (!m_Warned) {
                m_Warned = true;
                WIIXL_LOG("version: %s - this host cannot fingerprint the game, "
                          "so no offset can be checked against a build",
                          m_What);
            }
            return nullptr;
        }
        const char* name = VerImports::Name();
        if (!name && !m_Warned) {
            m_Warned = true;
            WIIXL_LOG("version: %s - the host does not recognise this build "
                      "(0x%08X), so it has no name to match and nothing is "
                      "applied", m_What,
                      VerImports::Fingerprint());
        }
        return name;
    }

private:
    static bool Same(const char* a, const char* b) {
        while (*a && *a == *b) { ++a; ++b; }
        return *a == '\0' && *b == '\0';
    }

    // Once per offset, not once per call. A per-frame resolve would otherwise
    // fill the ring buffer with the same line and push out everything that
    // explained how it got there.
    void Complain(const char* name) const {
        if (m_Warned) return;
        m_Warned = true;
        WIIXL_LOG("version: %s has no row for build \"%s\" (%u known) - "
                  "refusing rather than using another build's address",
                  m_What, name, static_cast<uint32_t>(m_Count));
    }

    const char* m_What;
    const BuildOffset* m_Table;
    size_t m_Count;
    mutable bool m_Warned = false;
};

} // namespace WiiXLaunch
