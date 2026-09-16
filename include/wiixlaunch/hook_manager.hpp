#pragma once

// WiiXLaunch::Hooks - one registry of every hook, keyed by target address.
//
// The bytes at `target` are read exactly once, when a site is first
// created, before any hook exists there. Every later `Original` is emitted
// from an address the manager already knows, never copied out of memory
// whose contents depend on install history - so nothing can mistake a
// previously-installed jump for the function's real prologue.
//
// Call order is first-installed-first:
//
//   install A, then B, then C   =>   A -> B -> C -> the game
//
// `target` is written once, on the first install, and never touched again.
// Appending a hook rewrites the previous tail's trampoline slot instead. A
// mod captures its `Original` pointer at install time and may keep it
// forever, so each link owns a fixed slot and only the jump inside it is
// rewritten when a successor appears - the address a mod holds stays valid
// for the life of the process.
//
//   target        -> A.callback          (written once, on first install)
//   A.slot        -> B.callback          (rewritten when B installed)
//   B.slot        -> C.callback          (rewritten when C installed)
//   C.slot        -> prologueTramp       (C is the tail)
//   prologueTramp -> saved prologue, then jump to target+16
//
// Sharing is never refused: the host does not arbitrate between mods it
// knows nothing about, and a mod meaning to replace a function simply
// never calls `Original`, truncating the chain below it. When two mods
// hook one address the log names both, by mod id, in call order.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Hooks {

constexpr uint32_t kMaxSites = 256;
constexpr uint32_t kMaxLinks = 256;
constexpr uint32_t kOwnerLen = 17;

// A long jump on this platform is exactly four instructions, and the
// prologue it displaces is therefore exactly four. Both are this constant.
constexpr uint32_t kJumpWords = 4;

enum class Install : uint32_t {
    Ok = 0,
    BadTarget,               // null or unaligned
    NoSites,                 // kMaxSites reached
    NoLinks,                 // kMaxLinks reached
    NoTrampoline,            // the pool is full
    PrologueNotRelocatable,  // a PC-relative branch in the displaced prologue
    NoArchSupport,           // this manager cannot emit code for this CPU
};

inline const char* InstallName(Install r) {
    switch (r) {
        case Install::Ok:                     return "OK";
        case Install::BadTarget:              return "BAD-TARGET";
        case Install::NoSites:                return "NO-SITES";
        case Install::NoLinks:                return "NO-LINKS";
        case Install::NoTrampoline:           return "NO-TRAMPOLINE";
        case Install::PrologueNotRelocatable: return "PROLOGUE-NOT-RELOCATABLE";
        case Install::NoArchSupport:          return "NO-ARCH-SUPPORT";
    }
    return "?";
}

// This encoder and decoder emit PowerPC only. On Switch, Install refuses by
// name (Install::NoArchSupport) rather than write PowerPC into aarch64
// code. exlaunch (vendored) already implements hooking and a real prologue
// relocator for aarch64; delegating to it is the fix for this gap.
//
// Pure functions over uint32_t so tools/hook_test can decode what the
// manager emitted on the host, without a console.

// lis r12,hi ; ori r12,r12,lo ; mtctr r12 ; bctr
inline void EmitLongJump(uint32_t* dst, uintptr_t dest) {
    const uint32_t d = static_cast<uint32_t>(dest);
    dst[0] = 0x3D800000u | (d >> 16);
    dst[1] = 0x618C0000u | (d & 0xFFFFu);
    dst[2] = 0x7D8903A6u;
    dst[3] = 0x4E800420u;
}

// The inverse. Returns 0 when these four words are not a long jump of the
// shape above.
inline uintptr_t DecodeLongJump(const uint32_t* src) {
    if ((src[0] & 0xFFFF0000u) != 0x3D800000u) return 0;
    if ((src[1] & 0xFFFF0000u) != 0x618C0000u) return 0;
    if (src[2] != 0x7D8903A6u) return 0;
    if (src[3] != 0x4E800420u) return 0;
    return static_cast<uintptr_t>(((src[0] & 0xFFFFu) << 16) | (src[1] & 0xFFFFu));
}

// Is this instruction's meaning tied to where it sits? The prologue is
// moved to a trampoline, so a branch computed from its own address means
// something different once relocated. On PowerPC that's the I-form branch
// (opcode 18) and the B-form conditional branch (opcode 16) when AA is
// clear; with AA set, or for register-indirect bclr/bcctr (opcode 19), the
// instruction survives being moved.
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

// Counts of what the prologue decoder actually examined, so a silent
// decoder and an absent one don't read the same in the log.
inline uint32_t g_PrologueWordsDecoded = 0;
inline uint32_t g_PrologueRelativeFound = 0;
inline uint32_t g_PrologueSitesChecked = 0;
inline Link g_Links[kMaxLinks];
inline uint32_t g_LinkCount = 0;

// Executable scratch for trampolines: Cemu's backend trampoline pool, or on
// the host test, ordinary memory (nothing there is executed, only decoded).
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

// The mod id hooks are currently attributed to. Delegated to
// wiixlaunch/mod_context.hpp, which the loader sets around a module's entry
// and clears afterward - hooks, patches, and the mod-scoped filesystem all
// ask the same question. Null means "not inside a module"; the caller's own
// WIIXL_HOOK_OWNER is used then.
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

// Names every owner of a site, in call order, into `out`. Exposed so a test
// can assert order rather than trusting the log's formatting.
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
// address to call to continue the chain. `owner` names who to blame in the
// conflict report if this address is hooked twice.
inline Install InstallHook(uintptr_t target, uintptr_t callback,
                           const char* owner, uintptr_t* originalOut) {
    if (originalOut) *originalOut = 0;
    if (target == 0 || (target & 3u) != 0 || callback == 0) {
        WIIXL_LOG("Hook: %s refused at %p - %s", owner ? owner : "?",
                  reinterpret_cast<void*>(target), InstallName(Install::BadTarget));
        return Install::BadTarget;
    }

    // Guarded on the platform, not the host machine: the host-test build is
    // x86 and must keep exercising this function, since that test is the
    // only thing checking the encoding before it reaches a real console.
#if WIIXL_SWITCH
    WIIXL_LOG("Hook: %s refused at %p - %s: this hook manager emits PowerPC and "
              "this host is aarch64", owner ? owner : "?",
              reinterpret_cast<void*>(target), InstallName(Install::NoArchSupport));
    (void)callback;
    return Install::NoArchSupport;
#else
    Site* site = impl::FindSite(target);

    if (!site) {
        if (impl::g_SiteCount >= kMaxSites) {
            WIIXL_LOG("Hook: %s refused at %p - %s (%u sites)", owner,
                      reinterpret_cast<void*>(target), InstallName(Install::NoSites),
                      kMaxSites);
            return Install::NoSites;
        }

        // The one and only read of the target's bytes.
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

        WIIXL_LOG("Hook: prologue check at %p: %u instructions decoded, %u relative",
                  reinterpret_cast<void*>(target), kJumpWords, relative);

        for (uint32_t i = 0; i < kJumpWords; ++i) {
            if (IsPcRelativeBranch(saved[i])) {
                WIIXL_LOG("Hook: %s refused at %p - %s: instruction %u (0x%08X) is "
                          "PC-relative", owner, reinterpret_cast<void*>(target),
                          InstallName(Install::PrologueNotRelocatable), i, saved[i]);
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

    // The new link is the tail, so it continues into the real function.
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
        // Append: the previous tail stops going to the real function and
        // goes to us instead. Its slot address does not move, so any
        // Original a mod captured is still correct.
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
        char owners[160];
        OwnersOf(site, owners, sizeof(owners));
        WIIXL_LOG("Hook: SHARED TARGET %p is now %u deep - call order: %s -> game. "
                  "This is legal; if the game misbehaves with these mods together, "
                  "these are the ones sharing this function.",
                  reinterpret_cast<void*>(target), site->depth, owners);
    }
    return Install::Ok;
#endif  // !WIIXL_SWITCH
}

// Records a hook the manager did not install itself. Switch and Wii U hand
// hooking to exlaunch and WUPS, which build their own trampolines; the
// manager registers rather than chains, so the conflict report still names
// every owner of a shared address on every platform.
inline void Note(uintptr_t target, uintptr_t callback, const char* owner) {
    Site* site = impl::FindSite(target);
    if (!site) {
        if (impl::g_SiteCount >= kMaxSites) {
            WIIXL_LOG("Hook: %s noted at %p but all %u site slots are taken - "
                      "this target is MISSING from the conflict report",
                      owner ? owner : "?", reinterpret_cast<void*>(target), kMaxSites);
            return;
        }
        site = &impl::g_Sites[impl::g_SiteCount++];
        site->target = target;
        site->prologueTramp = nullptr;
        site->head = nullptr;
        site->tail = nullptr;
        site->depth = 0;
        site->inUse = true;
        for (uint32_t i = 0; i < kJumpWords; ++i) site->saved[i] = 0;
    }
    if (impl::g_LinkCount >= kMaxLinks) {
        WIIXL_LOG("Hook: %s noted at %p but all %u link slots are taken - this "
                  "owner is MISSING from the conflict report",
                  owner ? owner : "?", reinterpret_cast<void*>(target), kMaxLinks);
        return;
    }

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

// Every site, at the load point, and every shared site called out
// separately since that short list is the one worth reading in a bug
// report.
inline void LogState() {
    uint32_t shared = 0;
    for (uint32_t i = 0; i < impl::g_SiteCount; ++i) {
        if (impl::g_Sites[i].depth > 1) ++shared;
    }
    WIIXL_LOG("Hook: %u target(s) hooked by %u hook(s); %u target(s) shared by more "
              "than one owner", impl::g_SiteCount, impl::g_LinkCount, shared);

    // Sites, not installs: an append at an existing address does not
    // re-decode, since the prologue was captured once, before any hook
    // existed.
    WIIXL_LOG("Hook: prologue decoder ran on %u site(s): %u instructions decoded, "
              "%u PC-relative refused", impl::g_PrologueSitesChecked,
              impl::g_PrologueWordsDecoded, impl::g_PrologueRelativeFound);

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
