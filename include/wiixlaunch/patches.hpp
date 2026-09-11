#pragma once

// WiiXLaunch::Patches - raw byte patches, declared as data and applied by the
// host before any module code runs.
//
// A patch is not a hook. A hook redirects a function and can be chained; a
// patch overwrites bytes and cannot. Both are legitimate, and both are things
// two mods can do to the same address, so both need one registry that can name
// the parties when they collide.
//
// ---------------------------------------------------------------------------
// THE LOAD SEQUENCE, AND WHY IT IS THIS ORDER.
//
//   1. host hooks          installed by WiiXLaunch_Init, before any module
//   2. DECLARED PATCHES    every module's, at load, in load order
//   3. module entries      which may install more hooks
//
// This is a specification, not an implementation detail, and reordering it
// breaks two different things.
//
// PATCHES BEFORE ENTRIES is what makes patch conflicts detectable at all. The
// host sees every patch every module declares before any mod code runs, so
// patch-vs-patch and patch-vs-hook overlaps are known up front rather than
// discovered when someone's game misbehaves. Applying patches on request from
// inside a module's entry would give that up entirely - the host would learn
// about the second patch only after the first had already been written.
//
// PATCHES BEFORE LATER HOOKS is what makes patching a to-be-hooked function
// safe. The hook manager captures a target's prologue exactly once, when the
// first hook on that address is installed. Because declared patches run before
// any module entry, a module hooking an address another module patched captures
// the PATCHED bytes - which is correct, and is the only reason that direction
// needs no check. Reverse the order and the manager would capture the original
// prologue, the patch would then overwrite the jump the manager had just
// written, and the trampoline would carry bytes that no longer match anything.
//
// THE OTHER DIRECTION IS NOT SAFE AND IS CHECKED. A patch landing inside the
// 16 bytes a hook has already displaced writes into the long jump, not into the
// game: the bytes it is aiming at now live in a trampoline somewhere else, and
// what it actually corrupts is the branch to the first hook in the chain. This
// is reachable today, not hypothetically - the host's own GX2 hook is installed
// during WiiXLaunch_Init, before any module is loaded, so the very first patch
// any mod declares is already able to land in a hooked window.
// ---------------------------------------------------------------------------

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/hook_manager.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/loader/wxlm.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Patches {

constexpr uint32_t kMaxPatches = 32;
constexpr uint32_t kOwnerLen = 17;

// WHY THIS IS AN ENUM AND NOT A BOOL. Six refusals that want six different
// fixes: a malformed record is a build problem, an origin mismatch is a
// game-version problem, a hooked window is a mod-interaction problem. A caller
// that gets `false` cannot tell them apart, and neither can a test - see the
// log-string rule in docs/modules.md.
enum class Result : uint32_t {
    Ok = 0,
    BadSize,          // size is 0, or larger than kMaxPatchBytes
    BadTarget,        // null, or somewhere no patch may write
    IntoArena,        // aimed at the module arena, whose addresses move per boot
    OriginMismatch,   // the target does not hold what the patch expected
    HookedWindow,     // overlaps the 16 bytes a hook has already displaced
    PatchOverlap,     // overlaps bytes another module already patched
    NoSlots,          // kMaxPatches already recorded
};

inline const char* ResultName(Result r) {
    switch (r) {
        case Result::Ok:             return "OK";
        case Result::BadSize:        return "BAD-SIZE";
        case Result::BadTarget:      return "BAD-TARGET";
        case Result::IntoArena:      return "INTO-ARENA";
        case Result::OriginMismatch: return "ORIGIN-MISMATCH";
        case Result::HookedWindow:   return "HOOKED-WINDOW";
        case Result::PatchOverlap:   return "PATCH-OVERLAP";
        case Result::NoSlots:        return "NO-SLOTS";
    }
    return "?";
}

// One applied patch, kept so a later one can be told who it collides with -
// and so the host can go back and READ the target rather than believing the
// applier's return value.
struct Applied {
    uintptr_t addr;
    uint32_t  size;
    uint8_t   origin[Wxlm::kMaxPatchBytes];
    uint8_t   data[Wxlm::kMaxPatchBytes];
    char      owner[kOwnerLen];
    bool      restored;      // put back, so the target holds `origin` again
};

namespace impl {

inline Applied g_Applied[kMaxPatches];
inline uint32_t g_AppliedCount = 0;
inline uint32_t g_RefusedCount = 0;
inline uint32_t g_ExaminedCount = 0;

// The arena, whose addresses are different on every boot because the code cave
// moves with the graphic-pack load order. A patch written by absolute address
// into that range cannot mean anything - it is either a mod trying to rewrite
// another module's image, or a build mistake. Both are refused.
//
// Supplied rather than read, so a host test can describe a range without a
// console. Zero size means "no arena known", and the check is skipped.
inline uintptr_t g_ArenaBase = 0;
inline uint32_t g_ArenaSize = 0;

// Where a 32-bit game address lands in this process.
//
// On a console or in Cemu this is zero and the answer is the address itself -
// the game's address space IS the process's. It exists because a host test runs
// on a 64-bit machine, where a buffer to patch is far above anything a uint32_t
// can name, and a patch record cannot hold a 64-bit address without changing
// the format for every real target that will never need one.
//
// This is a test seam, not a hook: it is consulted on EVERY patch, so it cannot
// quietly stop being called the way an unused allocation hook did.
inline uintptr_t g_AddrBase = 0;

inline uintptr_t Resolve(uint32_t targetAddr) {
#if WIIXL_SWITCH
    // AN OFFSET ON SWITCH, not an address. NSOs are relocated to a random base
    // every launch, so there is no 32-bit number a patch record could hold that
    // names a Switch address - the same reason wiixl.call exists. The only
    // meaning available is the one hooks already use: an offset from the main
    // module's start.
    //
    // g_AddrBase is still added, and is still zero outside a host test.
    if (g_AddrBase == 0) {
        return exl::util::modules::GetTargetStart() + static_cast<uintptr_t>(targetAddr);
    }
#endif
    return g_AddrBase + static_cast<uintptr_t>(targetAddr);
}

// Is [addr, addr+size) inside the game image?
//
// Only answerable where the host knows the image bounds, which today is Switch.
// Returning true elsewhere is deliberate: "I cannot check" must not read as
// "it failed", and on Wii U and Cemu the address IS the process address and the
// origin comparison below is a meaningful test on its own.
inline bool InGameImage(uintptr_t addr, uint32_t size) {
#if WIIXL_SWITCH
    const exl::util::Range& r = exl::util::GetMainModuleInfo().m_Total;
    return addr >= r.m_Start && (addr + size) <= r.GetEnd() && (addr + size) >= addr;
#else
    (void)addr; (void)size;
    return true;
#endif
}

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Do [a, a+an) and [b, b+bn) share a byte?
inline bool Overlaps(uintptr_t a, uint32_t an, uintptr_t b, uint32_t bn) {
    return a < b + bn && b < a + an;
}

} // namespace impl

// Only a host test calls this. Zero - the default - means a game address is a
// process address, which is what every real target is.
inline void SetAddressBase(uintptr_t base) { impl::g_AddrBase = base; }

inline void SetArena(uintptr_t base, uint32_t size) {
    impl::g_ArenaBase = base;
    impl::g_ArenaSize = size;
}

inline void ResetForTest() {
    impl::g_AddrBase = 0;
    impl::g_AppliedCount = 0;
    impl::g_RefusedCount = 0;
    impl::g_ExaminedCount = 0;
}

inline uint32_t AppliedCount()  { return impl::g_AppliedCount; }
inline uint32_t RefusedCount()  { return impl::g_RefusedCount; }
inline uint32_t ExaminedCount() { return impl::g_ExaminedCount; }

// Decides whether a patch may be written, without writing it.
//
// Separate from Apply so a test can assert the REASON on a target it has no
// intention of letting anything write to. The order of the checks is the order
// of the diagnoses: a malformed record is not a game-version problem, and a
// hooked window is not an origin mismatch even though a hooked window will
// always ALSO fail an origin check - the jump is there, not the prologue. That
// is exactly why HookedWindow is tested first: reporting ORIGIN-MISMATCH for a
// patch into a hook would send someone looking at their game version when the
// answer is another mod.
inline Result Check(const Wxlm::PatchEntry& p, const char** collidesWith) {
    if (collidesWith) *collidesWith = nullptr;

    if (p.size == 0 || p.size > Wxlm::kMaxPatchBytes) return Result::BadSize;
    if (p.targetAddr == 0) return Result::BadTarget;

    const uintptr_t addr = impl::Resolve(p.targetAddr);

    // BEFORE ANYTHING DEREFERENCES IT. The origin comparison at the bottom of
    // this function READS the target, and an address that is not mapped kills
    // the process there - which turns "this patch is refused" into "the game
    // does not boot", and refusing without killing the boot is the entire
    // contract this module documents.
    //
    // It happened the first time a declared patch reached a Switch: c_patch
    // carries the Wii U address 0x3a75d48, the sample built for aarch64 all
    // the same, and the origin read faulted at 0x03a75000 before any check
    // could have an opinion. The arithmetic checks below were all correct and
    // none of them ever ran.
    if (!impl::InGameImage(addr, p.size)) return Result::BadTarget;

    // DERIVED, not set. An explicit SetArena would be a check that goes dead
    // the day someone forgets to call it, and a dead check is indistinguishable
    // from a passing one - the whole subject of the fourth rule. The real host
    // reads the arena it already owns; only a host test, which has no arena,
    // supplies one.
    uintptr_t arenaBase = impl::g_ArenaBase;
    uint32_t arenaSize = impl::g_ArenaSize;
#if WIIXL_CEMU
    if (arenaSize == 0) {
        arenaBase = Arena::Base();
        arenaSize = Arena::Total();
    }
#endif
    if (arenaSize != 0 && impl::Overlaps(addr, p.size, arenaBase, arenaSize)) {
        return Result::IntoArena;
    }

    // Against every hook site's displaced window.
    for (uint32_t i = 0; i < Hooks::SiteCount(); ++i) {
        const Hooks::Site* s = Hooks::SiteAt(i);
        if (!s) continue;
        if (impl::Overlaps(addr, p.size, s->target, Hooks::kJumpWords * 4)) {
            if (collidesWith && s->head) *collidesWith = s->head->owner;
            return Result::HookedWindow;
        }
    }

    // Against every patch already applied.
    for (uint32_t i = 0; i < impl::g_AppliedCount; ++i) {
        const Applied& a = impl::g_Applied[i];
        if (impl::Overlaps(addr, p.size, a.addr, a.size)) {
            if (collidesWith) *collidesWith = a.owner;
            return Result::PatchOverlap;
        }
    }

    // Last, because it is the one that reads the target. Everything above is
    // arithmetic and can be answered without touching the game's memory.
    const volatile uint8_t* at = reinterpret_cast<const volatile uint8_t*>(addr);
    for (uint32_t i = 0; i < p.size; ++i) {
        if (at[i] != p.origin[i]) return Result::OriginMismatch;
    }

    if (impl::g_AppliedCount >= kMaxPatches) return Result::NoSlots;
    return Result::Ok;
}

// Applies one patch on behalf of `owner`, or refuses it by name.
//
// Never fatal. A refused patch leaves the target untouched and the boot
// continues - one mod's bad patch must not cost the user their game, and must
// not silently cost them the other mods either.
inline Result Apply(const Wxlm::PatchEntry& p, const char* owner) {
    impl::g_ExaminedCount++;

    const char* collides = nullptr;
    const Result r = Check(p, &collides);

    if (r != Result::Ok) {
        impl::g_RefusedCount++;
        switch (r) {
            case Result::HookedWindow:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - inside the hook %s wrote",
                          owner, ResultName(r),
                          reinterpret_cast<void*>(impl::Resolve(p.targetAddr)),
                          p.size, collides ? collides : "a hook");
                WIIXL_LOG("Patch:   it would corrupt the branch into the chain, not the "
                          "game - those instructions live in a trampoline now");
                break;
            case Result::PatchOverlap:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - %s already patched those "
                          "bytes", owner, ResultName(r),
                          reinterpret_cast<void*>(impl::Resolve(p.targetAddr)),
                          p.size, collides ? collides : "another module");
                WIIXL_LOG("Patch:   both mods write the same address - this is the pair "
                          "to disable one of");
                break;
            case Result::OriginMismatch: {
                const volatile uint8_t* at =
                    reinterpret_cast<const volatile uint8_t*>(impl::Resolve(p.targetAddr));
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - expected %02X %02X %02X "
                          "%02X, found %02X %02X %02X %02X",
                          owner, ResultName(r),
                          reinterpret_cast<void*>(impl::Resolve(p.targetAddr)),
                          p.size, p.origin[0], p.origin[1], p.origin[2], p.origin[3],
                          at[0], at[1], at[2], at[3]);
                WIIXL_LOG("Patch:   built against a different build of the game; "
                          "writing it would corrupt a function it has never seen");
                break;
            }
            case Result::IntoArena:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - inside the module arena",
                          owner, ResultName(r),
                          reinterpret_cast<void*>(impl::Resolve(p.targetAddr)), p.size);
                WIIXL_LOG("Patch:   arena addresses differ on every boot, so an absolute "
                          "patch cannot mean anything there");
                break;
            default:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B)", owner, ResultName(r),
                          reinterpret_cast<void*>(impl::Resolve(p.targetAddr)), p.size);
                break;
        }
        return r;
    }

    const uintptr_t where = impl::Resolve(p.targetAddr);
    volatile uint8_t* at = reinterpret_cast<volatile uint8_t*>(where);
    for (uint32_t i = 0; i < p.size; ++i) at[i] = p.data[i];
#if WIIXL_CEMU
    Backend::FlushCache(where, p.size);
#endif

    Applied& a = impl::g_Applied[impl::g_AppliedCount++];
    a.addr = where;
    a.size = p.size;
    for (uint32_t i = 0; i < Wxlm::kMaxPatchBytes; ++i) {
        a.origin[i] = p.origin[i];
        a.data[i] = p.data[i];
    }
    a.restored = false;
    impl::CopyOwner(a.owner, owner ? owner : "?");

    WIIXL_LOG("Patch: %s applied %u B at %p (origin verified)",
              a.owner, p.size, reinterpret_cast<void*>(impl::Resolve(p.targetAddr)));
    return Result::Ok;
}

// Goes back and READS every applied patch's target.
//
// WHY THIS IS NOT REDUNDANT. Apply returning Ok says the applier believed it
// wrote. This says the bytes are there now, read back from the target by code
// that did not do the writing. A refusal is self-evidencing - nothing changed,
// and the origin still matches - but a success is not: without a readback the
// applied path would be verified only by the thing that performed it.
//
// Two properties, and the second is the one that would be missed:
//
//   1. the target now holds `data`
//   2. `data` is actually DIFFERENT from `origin`
//
// Without (2) a patch that wrote the bytes already there would pass, and so
// would an applier that wrote nothing at all to a target whose origin and data
// happened to be equal. A patch that changes nothing is a patch that proves
// nothing.
inline bool VerifyApplied() {
    if (impl::g_AppliedCount == 0) {
        WIIXL_LOG("Patch: nothing was applied, so there is nothing to verify");
        return true;
    }

    uint32_t ok = 0, wrong = 0, inert = 0;
    for (uint32_t i = 0; i < impl::g_AppliedCount; ++i) {
        const Applied& a = impl::g_Applied[i];
        const volatile uint8_t* at = reinterpret_cast<const volatile uint8_t*>(a.addr);

        bool holds = true, changed = false;
        for (uint32_t b = 0; b < a.size; ++b) {
            if (at[b] != a.data[b]) holds = false;
            if (a.data[b] != a.origin[b]) changed = true;
        }

        if (!holds) {
            ++wrong;
            WIIXL_LOG("Patch: VERIFY FAILED at %p (%s) - wrote %02X %02X %02X %02X but "
                      "the target now reads %02X %02X %02X %02X. Something wrote over "
                      "it, or the write never landed.",
                      reinterpret_cast<void*>(a.addr), a.owner,
                      a.data[0], a.data[1], a.data[2], a.data[3],
                      at[0], at[1], at[2], at[3]);
        } else if (!changed) {
            ++inert;
            WIIXL_LOG("Patch: VERIFY INCONCLUSIVE at %p (%s) - the bytes are correct, "
                      "but they are the same as the origin, so this proves nothing "
                      "about whether anything was written.",
                      reinterpret_cast<void*>(a.addr), a.owner);
        } else {
            ++ok;
            WIIXL_LOG("Patch: verified %p (%s) - %02X %02X %02X %02X became "
                      "%02X %02X %02X %02X, read back from the target",
                      reinterpret_cast<void*>(a.addr), a.owner,
                      a.origin[0], a.origin[1], a.origin[2], a.origin[3],
                      at[0], at[1], at[2], at[3]);
        }
    }

    const bool pass = (wrong == 0 && inert == 0);
    WIIXL_LOG("Patch: %s - %u of %u applied patch(es) verified by reading the target "
              "back%s", pass ? "VERIFY PASS" : "VERIFY FAIL", ok, impl::g_AppliedCount,
              inert ? " (some changed nothing, which proves nothing)" : "");
    return pass;
}

// Puts every applied patch back, and checks the restore took.
//
// WHY A DEMONSTRATION WANTS THIS. Proving the applier writes to game memory
// needs a target the host can read back. Proving it is HARMLESS needs either a
// target whose modification is provably inert forever, or a much cheaper
// property: that the modification does not outlive the load sequence.
//
// The second is easier to support and does not depend on being right about an
// address. `examples/patch_mod` uses both - its target is an instruction whose
// replacement is a different encoding of the same operation, AND it is put back
// here - so the demonstration is safe even if the inertness analysis is wrong.
//
// A SHIPPING HOST WITH REAL PATCH MODS MUST NOT CALL THIS. A patch is meant to
// persist; undoing one is only useful for a demonstration, or for a host tearing
// down before a reload. The log says so on every call rather than leaving it to
// whoever reads this header.
inline bool RestoreAll() {
    if (impl::g_AppliedCount == 0) return true;

    uint32_t done = 0, failed = 0;
    for (uint32_t i = 0; i < impl::g_AppliedCount; ++i) {
        Applied& a = impl::g_Applied[i];
        if (a.restored) continue;

        volatile uint8_t* at = reinterpret_cast<volatile uint8_t*>(a.addr);
        for (uint32_t b = 0; b < a.size; ++b) at[b] = a.origin[b];
#if WIIXL_CEMU
        Backend::FlushCache(a.addr, a.size);
#endif

        // Read it back. A restore that silently did not take would leave the
        // game running modified for the rest of the session, which is the exact
        // thing this is here to prevent - so it is checked, not assumed.
        bool holds = true;
        for (uint32_t b = 0; b < a.size; ++b) {
            if (at[b] != a.origin[b]) holds = false;
        }

        if (holds) {
            a.restored = true;
            ++done;
            WIIXL_LOG("Patch: restored %p (%s) - target holds %02X %02X %02X %02X again",
                      reinterpret_cast<void*>(a.addr), a.owner,
                      at[0], at[1], at[2], at[3]);
        } else {
            ++failed;
            WIIXL_LOG("Patch: RESTORE FAILED at %p (%s) - target still reads "
                      "%02X %02X %02X %02X", reinterpret_cast<void*>(a.addr), a.owner,
                      at[0], at[1], at[2], at[3]);
        }
    }

    WIIXL_LOG("Patch: %s - %u restored, %u failed. A host shipping real patch mods "
              "must not call RestoreAll; a patch is meant to persist.",
              failed == 0 ? "RESTORE PASS" : "RESTORE FAIL", done, failed);
    return failed == 0;
}

// Everything patched, and by whom. Printed at the load point beside the hook
// summary, because "which mods touched this address" is one question with two
// mechanisms behind it.
inline void LogState() {
    WIIXL_LOG("Patch: %u examined, %u applied, %u refused",
              impl::g_ExaminedCount, impl::g_AppliedCount, impl::g_RefusedCount);
    for (uint32_t i = 0; i < impl::g_AppliedCount; ++i) {
        const Applied& a = impl::g_Applied[i];
        WIIXL_LOG("Patch:   %p %u B by %s%s",
                  reinterpret_cast<void*>(a.addr), a.size, a.owner,
                  a.restored ? " (restored - no longer in force)" : "");
    }
    if (impl::g_AppliedCount == 0) {
        WIIXL_LOG("Patch:   no module declared a patch");
    }
}

} // namespace WiiXLaunch::Patches
