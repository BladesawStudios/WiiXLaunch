// Host-side tests for the central hook manager.
//
// WHAT THIS CAN AND CANNOT PROVE, said plainly, because the difference matters.
//
// It CANNOT execute PowerPC. It does not prove the chain runs; the boot log
// does that. What it proves is that the manager CONSTRUCTED the chain
// correctly - that every jump it emitted decodes to the address it should, that
// the real prologue was captured exactly once and byte-for-byte, and that call
// order is what was specified. That is the "correct by construction, not by
// luck" half, and it is checkable natively by decoding instructions.
//
// The old chaining could not have been tested this way at all, because it had
// no model to check: whether it worked depended on what bytes happened to be at
// the target when the second hook installed.
//
// Every assertion decodes real emitted instructions. Nothing here reads the
// manager's bookkeeping and calls that agreement - the bookkeeping is the thing
// under test.

#include <cstdio>
#include <cstring>
#include <cstdint>

#include <wiixlaunch/hook_manager.hpp>

namespace H = WiiXLaunch::Hooks;

static int g_checks = 0;
static int g_failures = 0;

// A floor, so a suite that shrinks cannot report success over what is left of
// itself. See the fourth rule in docs/modules.md.
static const int kExpectedChecks = 52;

static void ok(const char* what, bool cond) {
    ++g_checks;
    if (cond) {
        std::printf("  ok    %s\n", what);
    } else {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

static void eq_addr(const char* what, uintptr_t got, uintptr_t want) {
    ++g_checks;
    if (got == want) {
        std::printf("  ok    %-58s 0x%08X\n", what, (unsigned)got);
    } else {
        ++g_failures;
        std::printf("  FAIL  %-58s got 0x%08X, want 0x%08X\n",
                    what, (unsigned)got, (unsigned)want);
    }
}

// A stand-in for a game function. 64-bit hosts hand out addresses far above
// what a 32-bit long jump can encode, so the manager's emitted jumps are
// checked against the low 32 bits - which is exactly what the instruction pair
// can carry, and what the real payload uses.
alignas(64) static uint32_t g_Target[16];

static uintptr_t Addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }
static uint32_t Low(uintptr_t a) { return static_cast<uint32_t>(a); }

// An ordinary, relocatable prologue: stwu/mflr/stw/li - nothing PC-relative.
static void FillPrologue() {
    g_Target[0] = 0x9421FFE0u;   // stwu r1,-32(r1)
    g_Target[1] = 0x7C0802A6u;   // mflr r0
    g_Target[2] = 0x90010024u;   // stw  r0,36(r1)
    g_Target[3] = 0x38600001u;   // li   r3,1
    for (int i = 4; i < 16; ++i) g_Target[i] = 0x60000000u;  // nop
}

// ---------------------------------------------------------------------------
// An INDEPENDENT oracle for "is this instruction position-independent?"
//
// The obvious test would be to assert IsPcRelativeBranch(insn) equals
// (opcode is 16 or 18) && AA == 0. That is the implementation restated, and by
// the third rule in docs/modules.md it can only confirm what the code already
// believes.
//
// So the property is re-derived from what the ISA says a branch DOES. PowerPC
// computes a branch target as:
//
//     AA == 0   NIA = CIA + EXTS(displacement || 0b00)      (relative)
//     AA == 1   NIA =       EXTS(displacement || 0b00)      (absolute)
//
// An instruction is position-dependent exactly when moving it changes where it
// goes. So: decode the target at two different addresses and compare. Nothing
// here mentions opcode 16 or 18 as a CLASSIFICATION - they appear only as the
// encodings whose target is computed, which is ISA fact, not a restatement of
// the function under test.
// ---------------------------------------------------------------------------

static const uint64_t kNotABranch = 0xFFFFFFFFFFFFFFFFull;

static uint64_t BranchTargetAt(uint32_t insn, uint32_t pc) {
    const uint32_t op = insn >> 26;
    const uint32_t aa = (insn >> 1) & 1u;

    if (op == 18u) {                       // I-form: b, ba, bl, bla
        int32_t li = static_cast<int32_t>((insn & 0x03FFFFFCu) << 6) >> 6;  // sign-extend 26
        return static_cast<uint64_t>(static_cast<uint32_t>(aa ? li : (int32_t)pc + li));
    }
    if (op == 16u) {                       // B-form: bc, bca, bcl, bcla
        int32_t bd = static_cast<int32_t>((insn & 0x0000FFFCu) << 16) >> 16;  // sign-extend 16
        return static_cast<uint64_t>(static_cast<uint32_t>(aa ? bd : (int32_t)pc + bd));
    }
    return kNotABranch;                    // everything else goes nowhere by address
}

// Moving it changes where it goes => its meaning depends on where it sits.
static bool OracleIsPositionDependent(uint32_t insn) {
    const uint64_t a = BranchTargetAt(insn, 0x02000000u);
    const uint64_t b = BranchTargetAt(insn, 0x01800000u);
    if (a == kNotABranch && b == kNotABranch) return false;
    return a != b;
}

static void FuzzDecoder() {
    std::printf("\ndecoder fuzz, judged by an independent oracle:\n");

    long long checked = 0, relative = 0, absolute = 0, nonBranch = 0;
    int disagreements = 0;
    uint32_t firstBad = 0;

    // Payload bits the function must NOT be looking at, varied so that a
    // classifier keying off the wrong field shows up.
    const uint32_t payloads[8] = {
        0x00000000u, 0x03FFFFFCu, 0x00007FFCu, 0x0000FFFCu,
        0x02AAAAA8u, 0x01555554u, 0x0000AAA8u, 0x00005554u,
    };

    for (uint32_t op = 0; op < 64u; ++op) {
        for (uint32_t pay = 0; pay < 8u; ++pay) {
            for (uint32_t lowBits = 0; lowBits < 4u; ++lowBits) {   // AA and LK
                const uint32_t insn = (op << 26) | (payloads[pay] & 0x03FFFFFCu) | lowBits;
                const bool got = H::IsPcRelativeBranch(insn);
                const bool want = OracleIsPositionDependent(insn);
                ++checked;
                if (want) ++relative; else if (BranchTargetAt(insn, 0) == kNotABranch) ++nonBranch;
                else ++absolute;
                if (got != want && disagreements++ == 0) firstBad = insn;
            }
        }
    }

    // Dense sweep of the two branch forms across their whole displacement
    // range, both AA values. This is where a sign-extension or mask error
    // would live.
    for (uint32_t d = 0; d < 0x04000000u; d += 0x400u) {
        for (uint32_t lowBits = 0; lowBits < 4u; ++lowBits) {
            const uint32_t bi = (18u << 26) | (d & 0x03FFFFFCu) | lowBits;
            const bool gi = H::IsPcRelativeBranch(bi);
            const bool wi = OracleIsPositionDependent(bi);
            ++checked;
            if (wi) ++relative; else ++absolute;
            if (gi != wi && disagreements++ == 0) firstBad = bi;

            const uint32_t bb = (16u << 26) | (d & 0x0000FFFCu) | lowBits;
            const bool gb = H::IsPcRelativeBranch(bb);
            const bool wb = OracleIsPositionDependent(bb);
            ++checked;
            if (wb) ++relative; else ++absolute;
            if (gb != wb && disagreements++ == 0) firstBad = bb;
        }
    }

    std::printf("  %lld words checked: %lld position-dependent, %lld absolute-form, "
                "%lld non-branch\n", checked, relative, absolute, nonBranch);

    ok("the decoder agrees with the oracle on every word", disagreements == 0);
    if (disagreements) {
        std::printf("        %d disagreements, first at 0x%08X (decoder says %s)\n",
                    disagreements, firstBad,
                    H::IsPcRelativeBranch(firstBad) ? "relative" : "not relative");
    }

    // The sweep has to have SEEN both answers, or agreement is vacuous - a
    // decoder returning a constant would agree with an oracle that also never
    // varied. Fourth rule, applied to this test.
    ok("the sweep saw position-dependent words", relative > 0);
    ok("the sweep saw absolute-form branches", absolute > 0);
    ok("the sweep saw non-branch words", nonBranch > 0);
    ok("the sweep was not trivially small", checked > 100000);

    // The failure that actually matters: a relative form classified as safe
    // becomes a silent wrong jump. Assert that direction on its own.
    int missed = 0;
    for (uint32_t op = 0; op < 64u; ++op) {
        for (uint32_t pay = 0; pay < 8u; ++pay) {
            const uint32_t insn = (op << 26) | (payloads[pay] & 0x03FFFFFCu);
            if (OracleIsPositionDependent(insn) && !H::IsPcRelativeBranch(insn)) ++missed;
        }
    }
    ok("no position-dependent word is ever called safe to relocate", missed == 0);
}

int main() {
    std::printf("encoding round-trip:\n");
    {
        uint32_t buf[4];
        const uintptr_t probes[4] = { 0x01800000u, 0x0309FFFCu, 0xFFFF8000u, 0x00000004u };
        for (uintptr_t a : probes) {
            H::EmitLongJump(buf, a);
            eq_addr("EmitLongJump/DecodeLongJump round-trip", H::DecodeLongJump(buf), a);
        }
        // Low half with the high bit set must not sign-extend through ori.
        H::EmitLongJump(buf, 0x0180FFFFu);
        eq_addr("low half 0xFFFF survives ori", H::DecodeLongJump(buf), 0x0180FFFFu);

        const uint32_t notAJump[4] = {0x9421FFE0u, 0x7C0802A6u, 0x90010024u, 0x38600001u};
        ok("a prologue does not decode as a long jump", H::DecodeLongJump(notAJump) == 0);
    }

    std::printf("\nPC-relative detection (the silent-corruption check):\n");
    ok("b   +0x40 is PC-relative",        H::IsPcRelativeBranch(0x48000040u));
    ok("bl  +0x40 is PC-relative",        H::IsPcRelativeBranch(0x48000041u));
    ok("bc  is PC-relative",              H::IsPcRelativeBranch(0x40820010u));
    ok("bcl 20,31,$+4 is PC-relative",    H::IsPcRelativeBranch(0x429F0005u));
    ok("ba  (absolute) is not",           !H::IsPcRelativeBranch(0x48000042u));
    ok("bla (absolute) is not",           !H::IsPcRelativeBranch(0x48000043u));
    ok("blr (register) is not",           !H::IsPcRelativeBranch(0x4E800020u));
    ok("bctr (register) is not",          !H::IsPcRelativeBranch(0x4E800420u));
    ok("stwu is not a branch",            !H::IsPcRelativeBranch(0x9421FFE0u));
    ok("mflr is not a branch",            !H::IsPcRelativeBranch(0x7C0802A6u));

    // THE REFUSAL PATH NEEDS ITS OWN COVERAGE, and this is the only place it
    // gets any.
    //
    // The one real hook target in the tree - WiiXLaunch_HookProbe in
    // src/cemu/bootstrap.cpp - is hand-written assembly chosen precisely so its
    // prologue can never be PC-relative. That makes it a fine accept-path
    // demonstration and a useless canary for refusal: nothing in the tree can
    // reach the refusal branch. So it is exercised here, synthetically, in
    // every displaced position and across every relative form.
    std::printf("\na PC-relative prologue is refused, not silently corrupted:\n");
    {
        // b, bl, bc, bcl - the forms that survive as traps because they still
        // execute after being moved, just to the wrong place.
        const uint32_t relatives[4] = {
            0x48000040u,   // b   +0x40
            0x48000041u,   // bl  +0x40
            0x40820010u,   // bc  +0x10
            0x429F0005u,   // bcl 20,31,$+4  - the classic PC-getter
        };
        const char* names[4] = { "b", "bl", "bc", "bcl" };

        int refused = 0, sitesCreated = 0, originalsLeaked = 0, targetsTouched = 0;

        for (uint32_t pos = 0; pos < 4u; ++pos) {
            for (uint32_t k = 0; k < 4u; ++k) {
                H::ResetForTest();
                FillPrologue();
                uint32_t before[4];
                g_Target[pos] = relatives[k];
                std::memcpy(before, g_Target, sizeof(before));

                uintptr_t orig = 0xDEADBEEFu;
                const H::Install r =
                    H::InstallHook(Addr(g_Target), 0x01810000u, "badmod", &orig);

                if (r == H::Install::PrologueNotRelocatable) ++refused;
                if (H::SiteCount() != 0) ++sitesCreated;
                if (orig != 0) ++originalsLeaked;
                if (std::memcmp(before, g_Target, sizeof(before)) != 0) ++targetsTouched;
            }
        }

        ok("every relative form in every displaced position is refused", refused == 16);
        ok("a refused hook creates no site", sitesCreated == 0);
        ok("a refused hook hands out no Original", originalsLeaked == 0);
        ok("a refused hook does not touch the target", targetsTouched == 0);
        if (refused != 16) {
            std::printf("        only %d of 16 refused\n", refused);
        }

        // The refusal has to be NAMED, because a caller that gets a generic
        // failure cannot tell "this prologue cannot be moved" from "the arena
        // is full", and those want different fixes.
        H::ResetForTest();
        FillPrologue();
        g_Target[2] = 0x48000040u;
        const H::Install r =
            H::InstallHook(Addr(g_Target), 0x01810000u, "badmod", nullptr);
        ok("refused by name, not generically",
           std::strcmp(H::InstallName(r), "PROLOGUE-NOT-RELOCATABLE") == 0);
        for (uint32_t k = 0; k < 4u; ++k) (void)names[k];

        // And the accept path still accepts - a decoder that refused everything
        // would pass every assertion above.
        H::ResetForTest();
        FillPrologue();
        uintptr_t good = 0;
        ok("an ordinary prologue is still accepted",
           H::InstallHook(Addr(g_Target), 0x01810000u, "goodmod", &good) == H::Install::Ok
           && good != 0);
    }

    std::printf("\nTHREE DEEP - first installed runs first:\n");
    {
        H::ResetForTest();
        FillPrologue();
        uint32_t prologueCopy[4];
        std::memcpy(prologueCopy, g_Target, sizeof(prologueCopy));

        const uintptr_t cbA = 0x01810000u, cbB = 0x01820000u, cbC = 0x01830000u;
        uintptr_t origA = 0, origB = 0, origC = 0;

        ok("A installs", H::InstallHook(Addr(g_Target), cbA, "modA", &origA) == H::Install::Ok);
        ok("B installs", H::InstallHook(Addr(g_Target), cbB, "modB", &origB) == H::Install::Ok);
        ok("C installs", H::InstallHook(Addr(g_Target), cbC, "modC", &origC) == H::Install::Ok);

        ok("one site for three hooks", H::SiteCount() == 1);
        ok("three links", H::LinkCount() == 3);

        const H::Site* s = H::FindSite(Addr(g_Target));
        ok("site found", s != nullptr);
        ok("depth is 3", s && s->depth == 3);

        // --- the chain, decoded from what was actually emitted --------------
        eq_addr("target jumps to A (first installed runs first)",
                H::DecodeLongJump(g_Target), Low(cbA));
        eq_addr("A's original jumps to B",
                H::DecodeLongJump(reinterpret_cast<uint32_t*>(origA)), Low(cbB));
        eq_addr("B's original jumps to C",
                H::DecodeLongJump(reinterpret_cast<uint32_t*>(origB)), Low(cbC));
        eq_addr("C's original jumps to the prologue trampoline",
                H::DecodeLongJump(reinterpret_cast<uint32_t*>(origC)),
                Low(Addr(s->prologueTramp)));

        // --- the prologue survived, exactly ---------------------------------
        ok("prologue trampoline holds the ORIGINAL four instructions, byte for byte",
           s && std::memcmp(s->prologueTramp, prologueCopy, sizeof(prologueCopy)) == 0);
        eq_addr("prologue trampoline then jumps past the patch",
                H::DecodeLongJump(s->prologueTramp + 4), Low(Addr(g_Target) + 16));
        ok("the saved copy in the site matches too",
           s && std::memcmp(s->saved, prologueCopy, sizeof(prologueCopy)) == 0);

        // --- call order, as a string ----------------------------------------
        char owners[160];
        H::OwnersOf(s, owners, sizeof(owners));
        ok("call order is modA -> modB -> modC",
           std::strcmp(owners, "modA -> modB -> modC") == 0);
        if (std::strcmp(owners, "modA -> modB -> modC") != 0) {
            std::printf("        owners was \"%s\"\n", owners);
        }

        // --- the property the old mechanism could not have -------------------
        //
        // The manager must never have re-read the target after the first
        // install. If it had, B would have captured A's jump as a "prologue" -
        // so the saved prologue would decode as a long jump to A. It must not.
        ok("the saved prologue is NOT a long jump (the target was read only once)",
           s && H::DecodeLongJump(s->saved) == 0);

        // Originals must be distinct, stable addresses.
        ok("each hook got its own original slot",
           origA && origB && origC && origA != origB && origB != origC && origA != origC);
    }

    std::printf("\nappending does not move an earlier Original:\n");
    {
        H::ResetForTest();
        FillPrologue();
        uintptr_t origA = 0, origB = 0;
        H::InstallHook(Addr(g_Target), 0x01810000u, "modA", &origA);
        const uintptr_t origA_before = origA;
        eq_addr("A's original points at the prologue trampoline while A is alone",
                H::DecodeLongJump(reinterpret_cast<uint32_t*>(origA)),
                Low(Addr(H::FindSite(Addr(g_Target))->prologueTramp)));

        H::InstallHook(Addr(g_Target), 0x01820000u, "modB", &origB);
        ok("A's Original POINTER is unchanged after B appends", origA_before == origA);
        eq_addr("but its CONTENTS now jump to B",
                H::DecodeLongJump(reinterpret_cast<uint32_t*>(origA_before)), 0x01820000u);
        eq_addr("and the target still jumps to A - written once, never again",
                H::DecodeLongJump(g_Target), 0x01810000u);
    }

    FuzzDecoder();

    std::printf("\ntwo separate targets do not interfere:\n");
    {
        H::ResetForTest();
        FillPrologue();
        alignas(64) static uint32_t other[16];
        for (int i = 0; i < 16; ++i) other[i] = 0x60000000u;

        uintptr_t o1 = 0, o2 = 0;
        H::InstallHook(Addr(g_Target), 0x01810000u, "modA", &o1);
        H::InstallHook(Addr(other), 0x01820000u, "modB", &o2);
        ok("two sites", H::SiteCount() == 2);
        eq_addr("first target jumps to modA", H::DecodeLongJump(g_Target), 0x01810000u);
        eq_addr("second target jumps to modB", H::DecodeLongJump(other), 0x01820000u);
        ok("neither is depth 2", H::FindSite(Addr(g_Target))->depth == 1 &&
                                 H::FindSite(Addr(other))->depth == 1);
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);

    if (g_checks < kExpectedChecks) {
        std::printf("HOOK TESTS DISARMED: only %d of the expected %d checks ran.\n",
                    g_checks, kExpectedChecks);
        return 1;
    }
    std::printf("%s (%d checks: encoding, PC-relative refusal, three-deep chain "
                "construction, Original stability, site isolation, 526k-word "
                "decoder fuzz)\n",
                g_failures == 0 ? "ALL HOOK TESTS PASS" : "HOOK TESTS FAILED", g_checks);
    return g_failures != 0;
}
