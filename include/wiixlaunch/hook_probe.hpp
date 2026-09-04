#pragma once

// WiiXLaunch::HookProbe - the host's own assertion that the chain ran in the
// right order.
//
// WHY THIS EXISTS AND WHAT IT REPLACES. The two demonstration mods each log a
// line before and after calling Original, and a human can read those five lines
// and see the nesting. That is not a test. It is exactly the load-point probe's
// old failure one level up: self-reported strings, judged by eye, where a
// missing line reads as a shorter log rather than as a failure. Nothing was
// asserting the sequence, so nothing could fail.
//
// So the host records the sequence itself and checks it, and prints a verdict
// that says PASS or FAIL rather than leaving the reader to reconstruct it.
//
// ---------------------------------------------------------------------------
// ATTRIBUTION COMES FROM WHAT THE HOST OBSERVES, NOT FROM WHAT A MOD CLAIMS.
//
// A mod marks the sequence with a numeric tag. If the host simply believed the
// tag, the ordering check would be worth nothing: a mod could pass any value
// and the "verified" order would be whatever the mods felt like reporting.
//
// So a tag is CLAIMED during the module's entry, and the host binds it to
// whichever module the loader is currently running - Hooks::CurrentOwner(),
// which the host set, not something the module passed. A tag already claimed by
// someone else is refused. By the time marks arrive, the host has its own
// record of which mod each tag belongs to, established under its own
// observation, and the ordering assertion is against that record.
//
// This is the same principle as wiixl.core's InstallHook taking no owner
// parameter, for the same reason: a self-declared identity makes every report
// built on it unfalsifiable, and an unfalsifiable report is not evidence.
// ---------------------------------------------------------------------------

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/hook_manager.hpp>

#include <cstdint>

namespace WiiXLaunch::HookProbe {

constexpr uint32_t kMaxMarks = 32;
constexpr uint32_t kMaxTags  = 8;

// The host's own mark, written by the probe body at the end of the chain. Not
// claimable by a mod - ClaimTag refuses it - so "the host body ran" cannot be
// forged by a module that never called Original.
constexpr uint32_t kHostTag = 0x484F5354u;   // 'HOST'

struct TagOwner {
    uint32_t tag;
    char     owner[Hooks::kOwnerLen];
    bool     inUse;
};

namespace impl {
inline TagOwner g_Tags[kMaxTags];
inline uint32_t g_TagCount = 0;
inline uint32_t g_Marks[kMaxMarks];
inline uint32_t g_MarkCount = 0;
inline uint32_t g_Overflow = 0;

inline const char* OwnerOfTag(uint32_t tag) {
    if (tag == kHostTag) return "<host>";
    for (uint32_t i = 0; i < g_TagCount; ++i) {
        if (g_Tags[i].inUse && g_Tags[i].tag == tag) return g_Tags[i].owner;
    }
    return nullptr;
}
} // namespace impl

inline void Reset() {
    impl::g_TagCount = 0;
    impl::g_MarkCount = 0;
    impl::g_Overflow = 0;
}

// Binds `tag` to the module the loader is currently running. Returns 0 if the
// caller is not inside a module entry, if the tag is the host's, or if someone
// else already claimed it.
inline uint32_t ClaimTag(uint32_t tag) {
    const char* owner = Hooks::CurrentOwner();
    if (!owner) {
        WIIXL_LOG("HookProbe: tag 0x%X claimed outside a module entry - refused. The "
                  "host only attributes what it is currently running.", tag);
        return 0;
    }
    if (tag == kHostTag) {
        WIIXL_LOG("HookProbe: %s tried to claim the host tag - refused", owner);
        return 0;
    }
    for (uint32_t i = 0; i < impl::g_TagCount; ++i) {
        if (impl::g_Tags[i].inUse && impl::g_Tags[i].tag == tag) {
            WIIXL_LOG("HookProbe: %s wanted tag 0x%X but %s already has it - refused",
                      owner, tag, impl::g_Tags[i].owner);
            return 0;
        }
    }
    if (impl::g_TagCount >= kMaxTags) return 0;

    TagOwner& t = impl::g_Tags[impl::g_TagCount++];
    t.tag = tag;
    t.inUse = true;
    uint32_t i = 0;
    for (; i + 1 < Hooks::kOwnerLen && owner[i]; ++i) t.owner[i] = owner[i];
    t.owner[i] = '\0';

    WIIXL_LOG("HookProbe: tag 0x%X bound to %s (attributed by the host, not claimed "
              "by the module)", tag, t.owner);
    return 1;
}

// Appends a mark. Cheap and total - a mark that does not fit is counted rather
// than dropped silently, because a truncated sequence that still looked
// well-formed would be the worst possible answer.
inline void Mark(uint32_t tag) {
    if (impl::g_MarkCount >= kMaxMarks) { impl::g_Overflow++; return; }
    impl::g_Marks[impl::g_MarkCount++] = tag;
}

inline void MarkHost() { Mark(kHostTag); }

inline uint32_t MarkCount() { return impl::g_MarkCount; }

// Checks the recorded sequence against what the hook registry says the chain
// should be, and logs a verdict.
//
// Three properties, and they are checked separately so a failure says which one
// broke:
//
//   1. LENGTH. Every hook marks once before and once after, and the host marks
//      once in the middle: 2*depth + 1.
//   2. NESTING. The sequence must be a palindrome around the single host mark.
//      That is what "each mod called Original and then regained control" means,
//      and it holds whatever tags the mods chose - a mod that returned without
//      calling Original breaks it, and so does one that marked twice.
//   3. ORDER. The tags before the host mark, mapped through the host's OWN
//      tag->owner record, must equal the call order the hook registry built.
//      This is the assertion that first-installed-first actually happened.
//
// Returns true only if all three hold.
inline bool Verify(uintptr_t target) {
    const Hooks::Site* site = Hooks::FindSite(target);
    if (!site) {
        WIIXL_LOG("HookProbe: FAIL - nothing is hooked at %p, so the chain never "
                  "existed", reinterpret_cast<void*>(target));
        return false;
    }

    const uint32_t depth = site->depth;
    bool ok = true;

    WIIXL_LOG("HookProbe: verifying %u mark(s) against a chain %u deep at %p",
              impl::g_MarkCount, depth, reinterpret_cast<void*>(target));

    if (impl::g_Overflow) {
        WIIXL_LOG("HookProbe: FAIL - %u mark(s) did not fit in the %u-entry buffer",
                  impl::g_Overflow, kMaxMarks);
        ok = false;
    }

    // 1. length
    const uint32_t expected = depth * 2u + 1u;
    if (impl::g_MarkCount != expected) {
        WIIXL_LOG("HookProbe: FAIL - expected %u marks (2 per hook plus the host's) "
                  "but recorded %u. A mod that returned without calling Original, or "
                  "never ran at all, looks like this.", expected, impl::g_MarkCount);
        ok = false;
    }

    // 2. nesting - one host mark, in the middle, with a palindrome around it
    uint32_t hostMarks = 0, hostAt = 0;
    for (uint32_t i = 0; i < impl::g_MarkCount; ++i) {
        if (impl::g_Marks[i] == kHostTag) { ++hostMarks; hostAt = i; }
    }
    if (hostMarks != 1) {
        WIIXL_LOG("HookProbe: FAIL - the host body marked %u times, expected exactly "
                  "1. 0 means the chain never reached the end.", hostMarks);
        ok = false;
    } else if (impl::g_MarkCount == expected && hostAt != depth) {
        WIIXL_LOG("HookProbe: FAIL - the host mark is at position %u, expected %u",
                  hostAt, depth);
        ok = false;
    }

    if (ok && impl::g_MarkCount > 0) {
        for (uint32_t i = 0; i < impl::g_MarkCount / 2u; ++i) {
            const uint32_t a = impl::g_Marks[i];
            const uint32_t b = impl::g_Marks[impl::g_MarkCount - 1u - i];
            if (a != b) {
                const char* na = impl::OwnerOfTag(a);
                const char* nb = impl::OwnerOfTag(b);
                WIIXL_LOG("HookProbe: FAIL - not properly nested: mark %u is %s but "
                          "its mirror %u is %s. Each hook must regain control in the "
                          "reverse of the order it gave it up.",
                          i, na ? na : "<unclaimed>",
                          impl::g_MarkCount - 1u - i, nb ? nb : "<unclaimed>");
                ok = false;
                break;
            }
        }
    }

    // 3. order, against the registry rather than against expectation
    if (ok) {
        const Hooks::Link* link = site->head;
        for (uint32_t i = 0; i < depth && link; ++i, link = link->next) {
            const char* observed = impl::OwnerOfTag(impl::g_Marks[i]);
            if (!observed) {
                WIIXL_LOG("HookProbe: FAIL - mark %u carries tag 0x%X, which no module "
                          "claimed. A mod marked with a tag the host never bound to it.",
                          i, impl::g_Marks[i]);
                ok = false;
                break;
            }
            bool same = true;
            for (uint32_t c = 0; c < Hooks::kOwnerLen; ++c) {
                if (observed[c] != link->owner[c]) { same = false; break; }
                if (observed[c] == '\0') break;
            }
            if (!same) {
                WIIXL_LOG("HookProbe: FAIL - position %u ran %s, but the hook registry "
                          "has %s there. The chain did not run in install order.",
                          i, observed, link->owner);
                ok = false;
                break;
            }
        }
    }

    // The sequence itself, named, whatever the verdict - a failing run is
    // exactly when this is worth reading.
    char line[192];
    uint32_t n = 0;
    for (uint32_t i = 0; i < impl::g_MarkCount && n + 20 < sizeof(line); ++i) {
        const char* who = impl::OwnerOfTag(impl::g_Marks[i]);
        if (!who) who = "?";
        if (i) { line[n++] = ' '; line[n++] = '>'; line[n++] = ' '; }
        for (uint32_t c = 0; who[c] && n + 1 < sizeof(line); ++c) line[n++] = who[c];
    }
    line[n] = '\0';
    WIIXL_LOG("HookProbe: observed order: %s", line);

    char owners[160];
    Hooks::OwnersOf(site, owners, sizeof(owners));
    WIIXL_LOG("HookProbe: registry says the call order is: %s", owners);

    WIIXL_LOG("HookProbe: %s - %u hook(s), %u mark(s), nesting and order checked "
              "against the hook registry, tags attributed by the host",
              ok ? "PASS" : "FAIL", depth, impl::g_MarkCount);
    return ok;
}

} // namespace WiiXLaunch::HookProbe
