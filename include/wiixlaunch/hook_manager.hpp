#pragma once

// WiiXLaunch::Hooks - one registry of every hook, keyed by target address.
//
// WHY THIS EXISTS. Hooks used to be installed directly by the backend, with no
// record of who had hooked what. Two mods hooking one function still worked,
// but only by accident, and the accident is worth spelling out because it is
// what this file replaces.
//
// The old InstallHook copied the four instructions at `target` into a
// trampoline, appended a jump back to target+16, handed that back as Original,
// and wrote a jump at `target` to the callback. When a SECOND hook installed on
// the same address it did the same thing - and the four instructions it copied
// were no longer the function's prologue, they were the first hook's jump. So
// the second hook's "original" was "jump to the first hook's callback", and the
// chain worked. It worked because a long jump happens to be exactly four
// instructions and happens to be position-independent. Nothing checked that,
// nothing recorded it, and nothing could report it.
//
// CORRECT BY CONSTRUCTION MEANS: the bytes at `target` are read EXACTLY ONCE,
// when the site is created, before any hook exists. Every Original after that
// is EMITTED from a callback address the manager knows, never copied out of
// memory whose contents depend on install history. There is no arrangement of
// installs that can make the manager mistake a jump for a prologue, because it
// never looks again.
//
// ---------------------------------------------------------------------------
// CALL ORDER: FIRST INSTALLED RUNS FIRST.
//
//   install A, then B, then C   =>   A -> B -> C -> the game
//
// Load order is priority order, which is the thing a user can actually control
// by choosing which mods to enable. The old accidental behaviour was the
// reverse - each new hook wrapped the previous - and inverting it has a
// pleasant consequence: `target` is written ONCE, on the first install, and
// never touched again. Appending a hook rewrites the CONTENTS of the previous
// tail's trampoline slot instead.
//
// That matters for a subtle reason. A mod captures its Original pointer at
// install time and may keep it forever. If appending changed where Original
// pointed, every earlier mod would be holding a stale pointer. So each link
// owns a fixed four-instruction SLOT, and Original is the slot's address; only
// the jump inside it is rewritten when a successor appears. The address a mod
// holds stays valid for the life of the process.
//
//   target        -> A.callback          (written once, on first install)
//   A.slot        -> B.callback          (rewritten when B installed)
//   B.slot        -> C.callback          (rewritten when C installed)
//   C.slot        -> prologueTramp       (C is the tail)
//   prologueTramp -> saved prologue, then jump to target+16
//
// ---------------------------------------------------------------------------
// CONFLICT REPORTING IS THE DELIVERABLE, not a side effect. When two mods hook
// one address the log names both, by mod id, in call order. That line is the
// whole reason the registry is central: it turns "my game crashes with these
// two mods" into a one-line diagnosis. Nothing is ever refused for sharing -
// the host does not arbitrate between mods it knows nothing about. A mod that
// means to replace a function simply never calls Original, which truncates the
// chain below it, and the summary still shows who else was there.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Hooks {

constexpr uint32_t kMaxSites = 32;
constexpr uint32_t kMaxLinks = 64;
constexpr uint32_t kOwnerLen = 17;

// A long jump on this platform is exactly four instructions, and the prologue
// it displaces is therefore exactly four. Both are this constant.
constexpr uint32_t kJumpWords = 4;

enum class Install : uint32_t {
    Ok = 0,
    BadTarget,               // null or unaligned
    NoSites,                 // kMaxSites reached
    NoLinks,                 // kMaxLinks reached
    NoTrampoline,            // the pool is full
    PrologueNotRelocatable,  // a PC-relative branch in the displaced prologue
};

inline const char* InstallName(Install r) {
    switch (r) {
        case Install::Ok:                     return "OK";
        case Install::BadTarget:              return "BAD-TARGET";
        case Install::NoSites:                return "NO-SITES";
        case Install::NoLinks:                return "NO-LINKS";
        case Install::NoTrampoline:           return "NO-TRAMPOLINE";
        case Install::PrologueNotRelocatable: return "PROLOGUE-NOT-RELOCATABLE";
    }
    return "?";
}

// --- PowerPC encoding, as arithmetic ---------------------------------------
//
// Deliberately pure functions over uint32_t. They run identically on the host,
// which is what lets tools/hook_test decode what the manager emitted without a
// console and without executing a single PowerPC instruction.

// lis r12,hi ; ori r12,r12,lo ; mtctr r12 ; bctr - the same encoding the
// backend has always used, kept here so emit and decode cannot drift apart.
inline void EmitLongJump(uint32_t* dst, uintptr_t dest) {
    const uint32_t d = static_cast<uint32_t>(dest);
    dst[0] = 0x3D800000u | (d >> 16);
    dst[1] = 0x618C0000u | (d & 0xFFFFu);
    dst[2] = 0x7D8903A6u;
    dst[3] = 0x4E800420u;
}

// The inverse. Returns 0 when these four words are not a long jump of the shape
// above - which is itself the assertion a test wants: "the manager wrote a jump
// here, to exactly this address."
inline uintptr_t DecodeLongJump(const uint32_t* src) {
    if ((src[0] & 0xFFFF0000u) != 0x3D800000u) return 0;
    if ((src[1] & 0xFFFF0000u) != 0x618C0000u) return 0;
    if (src[2] != 0x7D8903A6u) return 0;
    if (src[3] != 0x4E800420u) return 0;
    return static_cast<uintptr_t>(((src[0] & 0xFFFFu) << 16) | (src[1] & 0xFFFFu));
}

// Is this instruction's meaning tied to where it sits?
//
// The prologue is MOVED to a trampoline, so any instruction whose destination
// is computed from its own address means something different once relocated.
// On PowerPC that is the I-form branch (opcode 18: b/bl) and the B-form
// conditional branch (opcode 16: bc/bcl) when their AA bit is clear. With AA
// set the target is absolute and the instruction survives being moved;
// bclr/bcctr (opcode 19) are register-indirect and also survive.
//
// The old code copied four instructions blindly. For the accidental second hook
// that was safe, because what it copied was a long jump - lis/ori/mtctr/bctr,
// none of them PC-relative. For a REAL function prologue it was never
// guaranteed, and the failure is silent: the branch still executes, it just
// goes somewhere else. This is the check that was missing.
inline bool IsPcRelativeBranch(uint32_t insn) {
    const uint32_t op = insn >> 26;
    if (op == 16u || op == 18u) return (insn & 0x2u) == 0u;   // AA == 0
    return false;
}

struct Link {
    char      owner[kOwnerLen];
    uintptr_t callback;
    uint32_t* slot;     // four instructions; a mod's Original points HERE
    Link*     next;     // installed after this one, so: called after this one
};

struct Site {
    uintptr_t target;
    uint32_t  saved[kJumpWords];   // the real prologue, captured exactly once
    uint32_t* prologueTramp;       // saved prologue, then a jump to target+16
    Link*     head;                // first installed, runs first
    Link*     tail;
    uint32_t  depth;
    bool      inUse;
};

namespace impl {

inline Site g_Sites[kMaxSites];
inline uint32_t g_SiteCount = 0;

// THE PROLOGUE DECODER'S OWN LIVENESS. "We refused nothing" and "the decoder
// never ran" look identical in a log, and that ambiguity is the whole failure
// class docs/modules.md rule four is about. These count what was actually
// examined, so the number can visibly drop to zero.
inline uint32_t g_PrologueWordsDecoded = 0;
inline uint32_t g_PrologueRelativeFound = 0;
inline uint32_t g_PrologueSitesChecked = 0;
inline Link g_Links[kMaxLinks];
inline uint32_t g_LinkCount = 0;

// Executable scratch for trampolines.
//
// On Cemu this comes from the backend's trampoline pool, which stays a host
// static for the reasons in docs/loader.md - it is per-payload state, not a
// sharing problem, and once main.cpp is a .wxlm there is exactly one payload.
// On the host test it is ordinary memory: nothing is executed there, only
// decoded.
#if WIIXL_CEMU
inline uint32_t* AllocWords(uint32_t words) {
    return reinterpret_cast<uint32_t*>(Backend::AllocateTrampoline(words * 4));
}
inline void Flush(void* p, uint32_t bytes) {
    Backend::FlushCache(reinterpret_cast<uintptr_t>(p), bytes);
}
#else
inline uint32_t g_Pool[1024];
inline uint32_t g_PoolUsed = 0;
inline uint32_t* AllocWords(uint32_t words) {
    if (g_PoolUsed + words > 1024u) return nullptr;
    uint32_t* p = &g_Pool[g_PoolUsed];
    g_PoolUsed += words;
    return p;
}
inline void Flush(void*, uint32_t) {}
#endif

inline void CopyOwner(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kOwnerLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline Site* FindSite(uintptr_t target) {
    for (uint32_t i = 0; i < g_SiteCount; ++i) {
        if (g_Sites[i].inUse && g_Sites[i].target == target) return &g_Sites[i];
    }
    return nullptr;
}

} // namespace impl

// The mod id hooks are currently attributed to.
//
// The loader sets this around a module's entry and clears it afterwards, the
// same shape as Arena::SetCurrent and for the same reason: the install path has
// no place to carry an identity, and a hook that ends up attributed to the
// wrong mod makes the conflict report worse than useless. Null means "not
// inside a module", and the caller's own WIIXL_HOOK_OWNER is used.
// DELEGATED, not owned. "Which module is the host running" is one question
// that hooks, patches and the mod-scoped filesystem all ask, so it lives in
// wiixlaunch/mod_context.hpp rather than here. It used to live here, which was
// accurate while hooks were the only asker and became a small lie the moment
// anything else needed it - a file read has no owner, it has a caller.
//
// These two keep their names because callers and tests use them, and because
// "owner" is still the right word for a hook.
inline void SetCurrentOwner(const char* id) { ModContext::SetCurrent(id); }
inline const char* CurrentOwner() { return ModContext::Current(); }

// Forgets every site and link. For a host test that runs many scenarios;
// nothing in a real host calls it, because a hook is never uninstalled.
inline void ResetForTest() {
    impl::g_SiteCount = 0;
    impl::g_LinkCount = 0;
    for (uint32_t i = 0; i < kMaxSites; ++i) impl::g_Sites[i].inUse = false;
#if !WIIXL_CEMU
    impl::g_PoolUsed = 0;
#endif
}

inline uint32_t SiteCount() { return impl::g_SiteCount; }
inline uint32_t LinkCount() { return impl::g_LinkCount; }
inline const Site* SiteAt(uint32_t i) {
    return i < impl::g_SiteCount ? &impl::g_Sites[i] : nullptr;
}
inline const Site* FindSite(uintptr_t target) { return impl::FindSite(target); }

// Names every owner of a site, in call order, into `out`. This is the string
// the conflict line is built from, exposed so a test can assert the ORDER
// rather than trusting the log's formatting.
inline uint32_t OwnersOf(const Site* s, char* out, uint32_t cap) {
    uint32_t n = 0;
    if (!s || cap == 0) { if (cap) out[0] = '\0'; return 0; }
    out[0] = '\0';
    for (const Link* l = s->head; l; l = l->next) {
        if (n && n + 4 < cap) {
            out[n++] = ' '; out[n++] = '-'; out[n++] = '>'; out[n++] = ' ';
        }
        for (uint32_t i = 0; l->owner[i] && n + 1 < cap; ++i) out[n++] = l->owner[i];
    }
    out[n] = '\0';
    return n;
}

// Installs `callback` at `target` on behalf of `owner`, and hands back the
// address to call to continue the chain.
//
// `owner` is a mod id, or a host component's name. It exists for one reason:
// so that when this address is hooked twice the log can say who.
inline Install InstallHook(uintptr_t target, uintptr_t callback,
                           const char* owner, uintptr_t* originalOut) {
    if (originalOut) *originalOut = 0;
    if (target == 0 || (target & 3u) != 0 || callback == 0) {
        WIIXL_LOG("Hook: %s refused at %p - %s", owner ? owner : "?",
                  reinterpret_cast<void*>(target), InstallName(Install::BadTarget));
        return Install::BadTarget;
    }

    Site* site = impl::FindSite(target);

    if (!site) {
        if (impl::g_SiteCount >= kMaxSites) {
            WIIXL_LOG("Hook: %s refused at %p - %s (%u sites)", owner,
                      reinterpret_cast<void*>(target), InstallName(Install::NoSites),
                      kMaxSites);
            return Install::NoSites;
        }

        // THE ONE AND ONLY READ OF THE TARGET'S BYTES. Everything downstream is
        // emitted from addresses the manager knows, so no later install can
        // mistake a jump for a prologue.
        const volatile uint32_t* src = reinterpret_cast<const volatile uint32_t*>(target);
        uint32_t saved[kJumpWords];
        for (uint32_t i = 0; i < kJumpWords; ++i) saved[i] = src[i];

        uint32_t relative = 0;
        for (uint32_t i = 0; i < kJumpWords; ++i) {
            if (IsPcRelativeBranch(saved[i])) ++relative;
        }
        impl::g_PrologueWordsDecoded += kJumpWords;
        impl::g_PrologueRelativeFound += relative;
        impl::g_PrologueSitesChecked++;

        // Printed on EVERY site, clean or not. A silent decoder and an absent
        // decoder read the same; a count does not.
        WIIXL_LOG("Hook: prologue check at %p: %u instructions decoded, %u relative",
                  reinterpret_cast<void*>(target), kJumpWords, relative);

        for (uint32_t i = 0; i < kJumpWords; ++i) {
            if (IsPcRelativeBranch(saved[i])) {
                WIIXL_LOG("Hook: %s refused at %p - %s: instruction %u (0x%08X) is "
                          "PC-relative", owner, reinterpret_cast<void*>(target),
                          InstallName(Install::PrologueNotRelocatable), i, saved[i]);
                WIIXL_LOG("Hook:   moving it to a trampoline would silently send it "
                          "elsewhere; this target needs branch fixup first");
                return Install::PrologueNotRelocatable;
            }
        }

        uint32_t* tramp = impl::AllocWords(kJumpWords * 2);
        if (!tramp) {
            WIIXL_LOG("Hook: %s refused at %p - %s", owner,
                      reinterpret_cast<void*>(target), InstallName(Install::NoTrampoline));
            return Install::NoTrampoline;
        }

        site = &impl::g_Sites[impl::g_SiteCount++];
        site->target = target;
        site->head = nullptr;
        site->tail = nullptr;
        site->depth = 0;
        site->inUse = true;
        for (uint32_t i = 0; i < kJumpWords; ++i) {
            site->saved[i] = saved[i];
            tramp[i] = saved[i];
        }
        EmitLongJump(&tramp[kJumpWords], target + kJumpWords * 4);
        site->prologueTramp = tramp;
        impl::Flush(tramp, kJumpWords * 2 * 4);
    }

    if (impl::g_LinkCount >= kMaxLinks) {
        WIIXL_LOG("Hook: %s refused at %p - %s (%u links)", owner,
                  reinterpret_cast<void*>(target), InstallName(Install::NoLinks), kMaxLinks);
        return Install::NoLinks;
    }

    uint32_t* slot = impl::AllocWords(kJumpWords);
    if (!slot) {
        WIIXL_LOG("Hook: %s refused at %p - %s", owner,
                  reinterpret_cast<void*>(target), InstallName(Install::NoTrampoline));
        return Install::NoTrampoline;
    }

    Link* link = &impl::g_Links[impl::g_LinkCount++];
    impl::CopyOwner(link->owner, owner ? owner : "?");
    link->callback = callback;
    link->slot = slot;
    link->next = nullptr;

    // The new link is the TAIL, so it continues into the real function.
    EmitLongJump(slot, reinterpret_cast<uintptr_t>(site->prologueTramp));
    impl::Flush(slot, kJumpWords * 4);

    if (!site->head) {
        // First hook here: this is the only time `target` is ever written.
        site->head = link;
        site->tail = link;
        uint32_t* t = reinterpret_cast<uint32_t*>(target);
        EmitLongJump(t, callback);
        impl::Flush(t, kJumpWords * 4);
    } else {
        // Append. The previous tail stops going to the real function and goes
        // to us instead. Its SLOT ADDRESS does not move, so whatever Original
        // pointer that mod captured is still the right one to call.
        EmitLongJump(site->tail->slot, callback);
        impl::Flush(site->tail->slot, kJumpWords * 4);
        site->tail->next = link;
        site->tail = link;
    }
    site->depth++;

    if (originalOut) *originalOut = reinterpret_cast<uintptr_t>(slot);

    if (site->depth == 1) {
        WIIXL_LOG("Hook: %s hooked %p (depth 1)", link->owner,
                  reinterpret_cast<void*>(target));
    } else {
        // THE DIAGNOSIS LINE. Two mods on one address is legal and reported by
        // name, because this is what turns "it crashes with these two enabled"
        // into something actionable.
        char owners[160];
        OwnersOf(site, owners, sizeof(owners));
        WIIXL_LOG("Hook: SHARED TARGET %p is now %u deep - call order: %s -> game. "
                  "This is legal; if the game misbehaves with these mods together, "
                  "these are the ones sharing this function.",
                  reinterpret_cast<void*>(target), site->depth, owners);
    }
    return Install::Ok;
}

// Records a hook the manager did not install itself.
//
// Switch and Wii U hand hooking to exlaunch and WUPS, which build their own
// trampolines. Reimplementing them here would be a much larger claim than this
// stage is making, and would have to be right on hardware nobody is testing
// today. So on those platforms the manager does not chain - it REGISTERS, so
// that the conflict report, which is the deliverable, works identically
// everywhere. The chain order there is whatever the platform does; what is the
// same on all three is that a shared address is named, with both owners.
inline void Note(uintptr_t target, uintptr_t callback, const char* owner) {
    Site* site = impl::FindSite(target);
    if (!site) {
        if (impl::g_SiteCount >= kMaxSites) return;
        site = &impl::g_Sites[impl::g_SiteCount++];
        site->target = target;
        site->prologueTramp = nullptr;
        site->head = nullptr;
        site->tail = nullptr;
        site->depth = 0;
        site->inUse = true;
        for (uint32_t i = 0; i < kJumpWords; ++i) site->saved[i] = 0;
    }
    if (impl::g_LinkCount >= kMaxLinks) return;

    Link* link = &impl::g_Links[impl::g_LinkCount++];
    impl::CopyOwner(link->owner, owner ? owner : "?");
    link->callback = callback;
    link->slot = nullptr;
    link->next = nullptr;
    if (site->tail) { site->tail->next = link; site->tail = link; }
    else { site->head = link; site->tail = link; }
    site->depth++;

    if (site->depth > 1) {
        char owners[160];
        OwnersOf(site, owners, sizeof(owners));
        WIIXL_LOG("Hook: SHARED TARGET %p is now %u deep - owners: %s. This is legal; "
                  "if the game misbehaves with these mods together, these are the ones "
                  "sharing this function.",
                  reinterpret_cast<void*>(target), site->depth, owners);
    }
}

// Every site, at the load point - and every SHARED site called out separately,
// because that short list is the one worth reading in a bug report.
inline void LogState() {
    uint32_t shared = 0;
    for (uint32_t i = 0; i < impl::g_SiteCount; ++i) {
        if (impl::g_Sites[i].depth > 1) ++shared;
    }
    WIIXL_LOG("Hook: %u target(s) hooked by %u hook(s); %u target(s) shared by more "
              "than one owner", impl::g_SiteCount, impl::g_LinkCount, shared);

    // The decoder ran this many times. An append at an existing address does
    // NOT re-decode - the prologue was captured once, before any hook existed,
    // which is the property that makes the chain correct by construction - so
    // this counts SITES, not installs, and that is why the two numbers differ.
    WIIXL_LOG("Hook: prologue decoder ran on %u site(s): %u instructions decoded, "
              "%u PC-relative refused", impl::g_PrologueSitesChecked,
              impl::g_PrologueWordsDecoded, impl::g_PrologueRelativeFound);
    WIIXL_LOG("Hook:   appends do not re-decode - a prologue is captured once per "
              "address, before any hook exists");

    char owners[160];
    for (uint32_t i = 0; i < impl::g_SiteCount; ++i) {
        const Site& s = impl::g_Sites[i];
        OwnersOf(&s, owners, sizeof(owners));
        WIIXL_LOG("Hook:   %p depth %u: %s -> game",
                  reinterpret_cast<void*>(s.target), s.depth, owners);
    }
    if (shared == 0) {
        WIIXL_LOG("Hook:   no target is hooked by more than one owner");
    }
}

} // namespace WiiXLaunch::Hooks
