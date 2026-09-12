#pragma once

// WHICH BUILD OF THE GAME AM I RUNNING ON?
//
// Every offset in this framework is written against one build of one game, and
// until now nothing asked. A host built for BotW 1.5.0 starts on 1.6.0 and
// begins installing hooks at addresses that mean something else - not a crash
// at the point of the mistake, but arbitrary behaviour some time later.
//
// WHY A FINGERPRINT AND NOT A VERSION STRING. There is no version string to be
// had that works everywhere. Switch has nn::oe::GetDisplayVersion, which the
// vendored exlaunch has no binding for and which reports what the title claims
// rather than what the code is; Wii U has a title version that a re-release can
// share; Cemu has no notion of one at all. What all three DO have is the game's
// own code, and a build's bytes are the least ambiguous name it has.
//
// So the host CRCs a slice of the running game and reports the number. A build
// nobody has seen before still gets an identity - it just has no NAME yet, and
// naming it is a line in the target file rather than a code change:
//
//     "identity": {
//         "switch": { "offset": "0x1000",     "length": 4096 },
//         "wiiu":   { "address": "0x02000000", "length": 4096 },
//         "known": [
//             { "name": "1.5.0", "platform": "switch", "fingerprint": "0x1A2B3C4D" }
//         ]
//     }
//
// THE FIRST BOOT ON A NEW BUILD IS THE ENROLMENT STEP. It logs the fingerprint
// it computed; you paste that into `known` with a name. There is deliberately no
// way to guess: an unrecognised build is reported as unrecognised, because
// "probably 1.5.0" is exactly the assumption that produces a corrupted save
// three hours later.
//
// A target with no `identity` block gets no fingerprint and says so. That is
// the honest state for a host nobody has enrolled builds for yet, and it is
// different from "recognised", which is why it reads differently in the log.

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
// On Switch the target declares an OFFSET, because an NSO is relocated to a
// different base every launch and no constant in a json file could name an
// address there. On Wii U and Cemu it declares an address, because that is what
// the platform gives you and what every other offset in those targets already
// means. The same split as everything else that spans these two worlds.
inline void Slice(uintptr_t* addr, uint32_t* length) {
#if WIIXL_SWITCH
    *length = Host::IdentitySwitchLength;
    if (*length == 0) { *addr = 0; return; }
    *addr = exl::util::modules::GetTargetStart() + Host::IdentitySwitchOffset;
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
        WIIXL_LOG("Game: this target declares no identity slice, so the build "
                  "cannot be fingerprinted - every offset here is trusted, not "
                  "checked");
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
