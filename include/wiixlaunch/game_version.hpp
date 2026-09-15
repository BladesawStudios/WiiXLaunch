#pragma once

// Which build of the game are we running on? Every offset in this framework
// is written against one build; starting on a different one installs hooks
// at addresses that mean something else, not a crash but arbitrary later
// behavior.
//
// Not a build id: the NSO header holding it is never mapped, TOTK's main
// has no .note.gnu.build-id, and exlaunch exposes nothing. Not
// nn::oe::GetDisplayVersion: it reports what the title's metadata claims
// (a repack can lie), and aborts outright if the underlying call fails
// under an emulator that stubs `am`. Wii U has a title version a
// re-release can share; Cemu has no notion of one at all.
//
// What every platform has is the game's own bytes, so the host CRCs a
// slice of the running game and reports the number. A build nobody has
// seen still gets an identity, just no name yet - naming it is a line in
// the target file:
//
//     "identity": {
//         "switch": { "offset": "0x20000", "length": 4096 },
//         "known": [
//             { "name": "1.5.0", "platform": "switch", "fingerprint": "0x1A2B3C4D" }
//         ]
//     }
//
// The Switch entry has no address: `offset` is measured from wherever
// exlaunch says the module's read-only data begins (see Slice below). The
// first boot on a new build is the enrolment step - it logs the fingerprint
// to paste into `known`. There is no guessing: an unrecognised build is
// reported as unrecognised, and a target with no `identity` block reports
// that distinctly from "recognised."

#include "platform.hpp"
#include "debug_log.hpp"
#include "generated_host.hpp"
#include "loader/wxlm.hpp"      // Crc32 - the same one the loader checks modules with

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

namespace WiiXLaunch::GameVersion {

namespace impl {

inline uint32_t g_Fingerprint = 0;
inline const char* g_Name = nullptr;
inline bool g_Ran = false;
inline bool g_Configured = false;

// The slice this platform hashes, as an absolute address and a length, or
// (0, 0) if this target has not declared one.
//
// Read-only data, not code: .text is exactly what mods and graphic packs
// rewrite, so a fingerprint over it would identify the build plus whatever
// mods are installed rather than the build alone.
//
// Switch doesn't declare a location: exlaunch already knows where the main
// module's read-only data is. Wii U and Cemu have no equivalent (the host
// doesn't know its own image bounds there), so those declare an address.
inline void Slice(uintptr_t* addr, uint32_t* length) {
#if WIIXL_SWITCH
    *length = Host::IdentitySwitchLength;
    if (*length == 0) { *addr = 0; return; }

    const exl::util::Range& ro = exl::util::GetMainModuleInfo().m_Rodata;
    // IdentitySwitchOffset is an offset INTO read-only data, not into the
    // module, so a target can move the window off anything it finds unstable
    // without having to know where rodata starts.
    const uintptr_t start = ro.m_Start + Host::IdentitySwitchOffset;
    if (start < ro.m_Start || (start + *length) > ro.GetEnd()) {
        *addr = 0;
        *length = 0;
        return;
    }
    *addr = start;
#else
    *length = Host::IdentityWiiuLength;
    *addr = (*length == 0) ? 0 : Host::IdentityWiiuAddress;
#endif
}

inline const Host::KnownBuild* Table() {
#if WIIXL_SWITCH
    return Host::KnownSwitchBuilds;
#else
    return Host::KnownWiiuBuilds;
#endif
}

} // namespace impl

// The CRC of this build's slice. 0 means it was never computed - either Detect
// has not run, or this target declares no identity slice.
inline uint32_t Fingerprint() { return impl::g_Fingerprint; }

// The name this build was enrolled under, or nullptr if it is not one the
// target knows. NEVER a guess.
inline const char* Name() { return impl::g_Name; }

inline bool Recognised() { return impl::g_Name != nullptr; }

// Whether this target declares an identity slice at all. A host that cannot
// fingerprint is not the same as one running an unknown build, and a mod
// deciding what to do about its offsets needs to tell them apart.
inline bool Configured() { return impl::g_Configured; }

// Run once, early, before anything installs a hook. Logs what it found either
// way - a host silently running on a build nobody has checked is the state this
// whole file exists to make visible.
inline void Detect() {
    if (impl::g_Ran) return;
    impl::g_Ran = true;

    uintptr_t addr = 0;
    uint32_t length = 0;
    impl::Slice(&addr, &length);

    if (addr == 0 || length == 0) {
        impl::g_Configured = false;
        WIIXL_LOG("Game: no usable identity slice, so the build cannot be "
                  "fingerprinted - every offset here is trusted, not checked");
        return;
    }

    impl::g_Configured = true;
    impl::g_Fingerprint = Wxlm::Crc32(reinterpret_cast<const void*>(addr), length);

    for (const Host::KnownBuild* b = impl::Table(); b->name != nullptr; ++b) {
        if (b->fingerprint == impl::g_Fingerprint) {
            impl::g_Name = b->name;
            break;
        }
    }

    if (impl::g_Name) {
        WIIXL_LOG("Game: build 0x%08X, which this host knows as \"%s\" "
                  "(crc32 over %u B at %p)",
                  impl::g_Fingerprint, impl::g_Name, length,
                  reinterpret_cast<void*>(addr));
        return;
    }

    WIIXL_LOG("Game: build 0x%08X is NOT ONE THIS HOST KNOWS (crc32 over %u B "
              "at %p)", impl::g_Fingerprint, length,
              reinterpret_cast<void*>(addr));
    WIIXL_LOG("Game:   every offset here was written for some other build. Add "
              "it to the target's identity.known with a name if this one is "
              "supposed to work.");
}

} // namespace WiiXLaunch::GameVersion
