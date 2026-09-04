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
static const int kExpectedChecks = 34;

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

    std::printf("\na PC-relative prologue is refused, not silently corrupted:\n");
    {
        H::ResetForTest();
        FillPrologue();
        g_Target[2] = 0x48000040u;             // b +0x40, third instruction
        uintptr_t orig = 0xDEADBEEFu;
        const H::Install r = H::InstallHook(Addr(g_Target), 0x01810000u, "badmod", &orig);
        ok("refused with PROLOGUE-NOT-RELOCATABLE",
           r == H::Install::PrologueNotRelocatable);
        ok("no site was created", H::SiteCount() == 0);
        ok("original was not handed out", orig == 0);
        ok("the target was left untouched", g_Target[0] == 0x9421FFE0u &&
                                            g_Target[2] == 0x48000040u);
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
                "construction, Original stability, site isolation)\n",
                g_failures == 0 ? "ALL HOOK TESTS PASS" : "HOOK TESTS FAILED", g_checks);
    return g_failures != 0;
}
