#pragma once

// WiiXLaunch::Patches - raw byte patches, declared as data and applied by
// the host before any module code runs. A patch overwrites bytes and can't
// be chained the way a hook can, but both are things two mods can do to
// the same address, so both go through one registry that can name the
// parties when they collide.
//
// Load sequence: host hooks, then every module's declared patches in load
// order, then module entries (which may install more hooks). Patches
// before entries is what makes patch conflicts detectable up front rather
// than discovered later. Patches before later hooks is what makes hooking
// a patched function safe: the hook manager captures a target's prologue
// once, on first install, so it captures the already-patched bytes.
// Reversed, the manager would capture the original prologue and the patch
// would then overwrite the jump it had just written.
//
// The other direction is checked, not just ordered around: a patch landing
// inside the 16 bytes a hook has displaced would write into the trampoline
// jump, not the game. See docs/framework/loader.md and
// docs/framework/hooks.md for the full reasoning.

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

// An enum, not a bool: a malformed record, an origin mismatch, and a
// hooked window want different fixes, and a caller getting `false` can't
// tell them apart.
enum class Result : uint32_t {
    Ok = 0,
    BadSize,          // size is 0, or larger than kMaxPatchBytes
    BadTarget,        // null, or somewhere no patch may write
    IntoArena,        // aimed at the module arena, whose addresses move per boot
    OriginMismatch,   // the target does not hold what the patch expected
    HookedWindow,     // overlaps the 16 bytes a hook has already displaced
    PatchOverlap,     // overlaps bytes another module already patched
    NoSlots,          // kMaxPatches already recorded
    WriteFailed,      // the bytes were written and the target still disagrees
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
        case Result::WriteFailed:    return "WRITE-FAILED";
    }
    return "?";
}

// One applied patch, kept so a later one can be told who it collides with,
// and so the host can read the target back rather than trust the return
// value.
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

// The arena, whose addresses move every boot with the code cave's load
// order. A patch aimed into that range can't mean anything, so it's
// refused. Supplied rather than read, so a host test can describe a range
// without a console; zero size means "no arena known" and skips the check.
inline uintptr_t g_ArenaBase = 0;
inline uint32_t g_ArenaSize = 0;

// Where a 32-bit game address lands in this process. Zero (the real-target
// default) means the game's address space is the process's own; nonzero
// only on a host test, whose buffers sit far above what a uint32_t can
// name.
inline uintptr_t g_AddrBase = 0;

inline uintptr_t Resolve(uint32_t targetAddr) {
#if WIIXL_SWITCH
    // An offset on Switch, not an address: NSOs relocate per launch, so the
    // only meaning available is an offset from the main module's start
    // (same reason wiixl.call exists).
    if (g_AddrBase == 0) {
        return exl::util::modules::GetTargetStart() + static_cast<uintptr_t>(targetAddr);
    }
#endif
    return g_AddrBase + static_cast<uintptr_t>(targetAddr);
}

// Is [addr, addr+size) inside the game image? Only answerable where the
// host knows the image bounds (Switch today). Returns true elsewhere,
// deliberately: "can't check" must not read as "failed."
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

// Decides whether a patch may be written, without writing it. Separate
// from Apply so a test can assert the reason. Checked in diagnosis order:
// a hooked window always also fails the origin check (the jump is there,
// not the prologue), so HookedWindow is tested first, or a patch into a
// hook would misleadingly report ORIGIN-MISMATCH.
//
// Works on a resolved address rather than a PatchEntry, so a declared
// patch (32-bit targetAddr, resolved once) and a runtime patch through
// wiixl.patch (which already has the address) both go through the same
// checks and the same collision table.
inline Result CheckAt(uintptr_t addr, const uint8_t* origin, uint32_t size,
                      const char** collidesWith) {
    if (collidesWith) *collidesWith = nullptr;

    if (size == 0 || size > Wxlm::kMaxPatchBytes) return Result::BadSize;
    if (addr == 0 || !origin) return Result::BadTarget;

    // Before anything dereferences it: the origin comparison below reads
    // the target, and an unmapped address kills the process there.
    if (!impl::InGameImage(addr, size)) return Result::BadTarget;

    // Derived, not set, on a real host: it reads the arena it already
    // owns. Only a host test, which has no arena, supplies one explicitly.
    uintptr_t arenaBase = impl::g_ArenaBase;
    uint32_t arenaSize = impl::g_ArenaSize;
#if WIIXL_CEMU
    if (arenaSize == 0) {
        arenaBase = Arena::Base();
        arenaSize = Arena::Total();
    }
#endif
    if (arenaSize != 0 && impl::Overlaps(addr, size, arenaBase, arenaSize)) {
        return Result::IntoArena;
    }

    // Against every hook site's displaced window.
    for (uint32_t i = 0; i < Hooks::SiteCount(); ++i) {
        const Hooks::Site* s = Hooks::SiteAt(i);
        if (!s) continue;
        if (impl::Overlaps(addr, size, s->target, Hooks::kJumpWords * 4)) {
            if (collidesWith && s->head) *collidesWith = s->head->owner;
            return Result::HookedWindow;
        }
    }

    // Against every patch already applied, declared or runtime.
    for (uint32_t i = 0; i < impl::g_AppliedCount; ++i) {
        const Applied& a = impl::g_Applied[i];
        if (impl::Overlaps(addr, size, a.addr, a.size)) {
            if (collidesWith) *collidesWith = a.owner;
            return Result::PatchOverlap;
        }
    }

    // Last: the only check that reads the target. Everything above is
    // arithmetic.
    const volatile uint8_t* at = reinterpret_cast<const volatile uint8_t*>(addr);
    for (uint32_t i = 0; i < size; ++i) {
        if (at[i] != origin[i]) return Result::OriginMismatch;
    }

    if (impl::g_AppliedCount >= kMaxPatches) return Result::NoSlots;
    return Result::Ok;
}

// Same as CheckAt, for a declared patch record.
inline Result Check(const Wxlm::PatchEntry& p, const char** collidesWith) {
    if (collidesWith) *collidesWith = nullptr;
    if (p.size == 0 || p.size > Wxlm::kMaxPatchBytes) return Result::BadSize;
    if (p.targetAddr == 0) return Result::BadTarget;
    return CheckAt(impl::Resolve(p.targetAddr), p.origin, p.size, collidesWith);
}

// Applies one patch at an already-resolved address, or refuses it by name.
// Never fatal: a refused patch leaves the target untouched and the boot
// continues.
inline Result ApplyAt(uintptr_t addr, const uint8_t* data, const uint8_t* origin,
                      uint32_t size, const char* owner) {
    impl::g_ExaminedCount++;
    if (!owner) owner = "?";

    const char* collides = nullptr;
    const Result r = CheckAt(addr, origin, size, &collides);
    void* where = reinterpret_cast<void*>(addr);

    if (r != Result::Ok) {
        impl::g_RefusedCount++;
        switch (r) {
            case Result::HookedWindow:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - inside the hook %s wrote",
                          owner, ResultName(r), where, size,
                          collides ? collides : "a hook");
                WIIXL_LOG("Patch:   it would corrupt the branch into the chain, not the "
                          "game - those instructions live in a trampoline now");
                break;
            case Result::PatchOverlap:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - %s already patched those "
                          "bytes", owner, ResultName(r), where, size,
                          collides ? collides : "another module");
                WIIXL_LOG("Patch:   both mods write the same address - this is the pair "
                          "to disable one of");
                break;
            case Result::OriginMismatch: {
                const volatile uint8_t* at =
                    reinterpret_cast<const volatile uint8_t*>(addr);
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - expected %02X %02X %02X "
                          "%02X, found %02X %02X %02X %02X",
                          owner, ResultName(r), where, size,
                          origin[0], origin[1], origin[2], origin[3],
                          at[0], at[1], at[2], at[3]);
                WIIXL_LOG("Patch:   built against a different build of the game; "
                          "writing it would corrupt a function it has never seen");
                break;
            }
            case Result::IntoArena:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - inside the module arena",
                          owner, ResultName(r), where, size);
                WIIXL_LOG("Patch:   arena addresses differ on every boot, so an absolute "
                          "patch cannot mean anything there");
                break;
            default:
                WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B)", owner, ResultName(r),
                          where, size);
                break;
        }
        return r;
    }

#if WIIXL_SWITCH
    // Writing to .text through .text is not a write: Horizon maps game
    // code read-execute, so a plain store either aborts, or under an
    // emulator that doesn't enforce the permission, lands in memory while
    // the recompiler keeps running its translation of the original
    // instruction. Both outcomes are silent and the bytes read back
    // correctly, so exlaunch's writable alias of the same physical pages
    // is used instead (the same mechanism hooks already go through).
    {
        const exl::util::RwPages& pages = exl::patch::impl::GetRwPages();
        const uintptr_t ro = pages.GetRo();
        const uintptr_t span = static_cast<uintptr_t>(pages.GetSize());
        if (addr < ro || (addr - ro) > span || (addr - ro) + size > span) {
            // The alias covers the module up to the end of .rodata. Past
            // that, writing to the plain address is the silent non-write
            // again.
            impl::g_RefusedCount++;
            WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - outside the writable "
                      "alias of the game image", owner,
                      ResultName(Result::BadTarget), where, size);
            return Result::BadTarget;
        }
        uint8_t* rw = reinterpret_cast<uint8_t*>(pages.GetRw() + (addr - ro));
        for (uint32_t i = 0; i < size; ++i) rw[i] = data[i];

        // Flush the data side where it was written, invalidate the
        // instruction side where it runs - the two mappings' caches don't
        // see each other, and a whole-claim flush would be ~74 MB of cache
        // maintenance per four-byte patch on TOTK.
        armDCacheFlush(rw, size);
        armICacheInvalidate(reinterpret_cast<void*>(addr), size);
    }
#else
    volatile uint8_t* at = reinterpret_cast<volatile uint8_t*>(addr);
    for (uint32_t i = 0; i < size; ++i) at[i] = data[i];
#endif
#if WIIXL_CEMU
    Backend::FlushCache(addr, size);
#endif

    // Read back through the address the CPU executes, not whatever alias
    // the write used. Every earlier check tests whether the patch is
    // allowed; this is the only one that tests whether it happened.
    {
        const volatile uint8_t* check =
            reinterpret_cast<const volatile uint8_t*>(addr);
        for (uint32_t i = 0; i < size; ++i) {
            if (check[i] == data[i]) continue;
            impl::g_RefusedCount++;
            WIIXL_LOG("Patch: %s REFUSED %s at %p (%u B) - wrote %02X %02X %02X "
                      "%02X, target still reads %02X %02X %02X %02X",
                      owner, ResultName(Result::WriteFailed), where, size,
                      data[0], data[1], data[2], data[3],
                      check[0], check[1], check[2], check[3]);
            return Result::WriteFailed;
        }
    }

    Applied& a = impl::g_Applied[impl::g_AppliedCount++];
    a.addr = addr;
    a.size = size;
    for (uint32_t i = 0; i < Wxlm::kMaxPatchBytes; ++i) {
        a.origin[i] = (i < size) ? origin[i] : 0;
        a.data[i]   = (i < size) ? data[i]   : 0;
    }
    a.restored = false;
    impl::CopyOwner(a.owner, owner);

    WIIXL_LOG("Patch: %s applied %u B at %p (origin verified)", a.owner, size, where);
    return Result::Ok;
}

inline Result Apply(const Wxlm::PatchEntry& p, const char* owner) {
    if (p.size == 0 || p.size > Wxlm::kMaxPatchBytes) {
        impl::g_ExaminedCount++;
        impl::g_RefusedCount++;
        WIIXL_LOG("Patch: %s REFUSED %s - size %u", owner ? owner : "?",
                  ResultName(Result::BadSize), p.size);
        return Result::BadSize;
    }
    if (p.targetAddr == 0) {
        impl::g_ExaminedCount++;
        impl::g_RefusedCount++;
        WIIXL_LOG("Patch: %s REFUSED %s - null target", owner ? owner : "?",
                  ResultName(Result::BadTarget));
        return Result::BadTarget;
    }
    return ApplyAt(impl::Resolve(p.targetAddr), p.data, p.origin, p.size, owner);
}

// Reads every applied patch's target back, by code that didn't do the
// writing. Checks two things: the target holds `data`, and `data` is
// actually different from `origin` (otherwise a no-op write, or one that
// happened to already match, would pass).
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

// Puts every applied patch back, and checks the restore took. A shipping
// host with real patch mods must not call this - a patch is meant to
// persist. Useful only for a demonstration, or a host tearing down before
// a reload.
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

// Everything patched, and by whom.
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
