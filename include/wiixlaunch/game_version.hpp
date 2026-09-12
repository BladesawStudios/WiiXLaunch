#pragma once

// WHICH BUILD OF THE GAME AM I RUNNING ON?
//
// Every offset in this framework is written against one build of one game, and
// until now nothing asked. A host built for BotW 1.5.0 starts on 1.6.0 and
// begins installing hooks at addresses that mean something else - not a crash
// at the point of the mistake, but arbitrary behaviour some time later.
//
// WHY A FINGERPRINT, AND NOT ONE OF THE THINGS THAT LOOK EASIER.
//
// A BUILD ID would be ideal - exact, produced by the toolchain, and immune to
// patching because it lives outside the image a mod rewrites. It is not
// reachable: the NSO header holding it is never mapped, TOTK's main carries no
// .note.gnu.build-id section, and exlaunch exposes nothing. Checked, not
// assumed.
//
// nn::oe::GetDisplayVersion IS exported - confirmed in nnSdk's .dynstr as
// _ZN2nn2oe17GetDisplayVersionEPNS0_14DisplayVersionE, writing 16 bytes. Two
// reasons it is not the identity. It reports what the title's metadata CLAIMS,
// so a repack or a rebuilt update can say "1.2.1" over different code; and it
// does not return a Result, it aborts through diag::detail when the underlying
// IApplicationFunctions call fails - which on an emulator that stubs am is a
// dead process at boot. It belongs as a LABEL, fetched lazily and opt-in, never
// as the thing offsets are selected by.
//
// Wii U has a title version a re-release can share. Cemu has no notion of one.
// What every platform does have is the game's own bytes, and those are the
// least ambiguous name a build has.
//
// So the host CRCs a slice of the running game and reports the number. A build
// nobody has seen before still gets an identity - it just has no NAME yet, and
// naming it is a line in the target file rather than a code change:
//
//     "identity": {
//         "switch": { "offset": "0x20000", "length": 4096 },
//         "known": [
//             { "name": "1.5.0", "platform": "switch", "fingerprint": "0x1A2B3C4D" }
//         ]
//     }
//
// The Switch entry has no address in it: `offset` is measured from wherever
// exlaunch says the module's read-only data begins. See Slice below.
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
// READ-ONLY DATA, NOT CODE. The first version of this hashed .text, which is
// exactly wrong: .text is the region mods exist to rewrite. A fingerprint taken
// over it does not identify the BUILD, it identifies the build plus whatever
// mods are installed - so enrolling one would bake in a mod set, and adding a
// mod would look like the game changing version. It was worse than theoretical
// here: the declared slice began at 0x02000030, which is the exact address
// examples/patch_mod writes to. Only the ordering of init and module load kept
// that from mattering.
//
// Cemu makes the point again from the other side - graphic packs patch code as
// a matter of course, and the recompiler treats .text as its own working
// surface. Read-only data differs per build and nothing patches it.
//
// SWITCH DOES NOT DECLARE A LOCATION, because it does not have to. exlaunch
// already knows where the main module's read-only data is, so the host asks
// rather than making a target carry a number somebody has to look up per game
// and get right. Wii U and Cemu have no equivalent - the host does not know its
// own image bounds there, which is why Patches::InGameImage returns true rather
// than checking - so those still declare an address.
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
