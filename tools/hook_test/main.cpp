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
#include <wiixlaunch/patches.hpp>
#include <wiixlaunch/tick.hpp>

namespace H = WiiXLaunch::Hooks;
namespace P = WiiXLaunch::Patches;
namespace T = WiiXLaunch::Tick;
namespace MC = WiiXLaunch::ModContext;

static int g_checks = 0;
static int g_failures = 0;

// A floor, so a suite that shrinks cannot report success over what is left of
// itself. See the fourth rule in docs/framework/modules.md.
static const int kExpectedChecks = 90;

// Section bookkeeping: how many checks each block contributed.
static int g_SectionBase = 0;
static const char* g_CurrentSection = "";
static int g_SectionFloorFailures = 0;

static void BeginSection(const char* name) {
    g_SectionBase = g_checks;
    g_CurrentSection = name;
}

static void EndSection(int atLeast) {
    const int ran = g_checks - g_SectionBase;
    if (ran < atLeast) {
        ++g_SectionFloorFailures;
        std::printf("  FAIL  section '%s' ran %d checks, expected at least %d - a "
                    "total can stay level while a section empties\n",
                    g_CurrentSection, ran, atLeast);
    }
}

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

// What the tick callbacks below recorded, and in what order.
static char g_TickOrder[64];
static uint32_t g_TickOrderLen = 0;
static char g_SeenInFlight[3][WiiXLaunch::Tick::kOwnerLen];
static uint32_t g_SeenCount = 0;

static void RecordTick(char mark) {
    if (g_TickOrderLen + 1 < sizeof(g_TickOrder)) g_TickOrder[g_TickOrderLen++] = mark;
    g_TickOrder[g_TickOrderLen] = 0;
    // Who does the dispatcher SAY is running, from inside the call? This is the
    // field a hang leaves behind, so it has to be correct while the tick runs -
    // checking it afterwards would prove nothing about a freeze.
    if (g_SeenCount < 3) {
        const char* who = WiiXLaunch::Tick::InFlight().owner;
        uint32_t i = 0;
        for (; i + 1 < WiiXLaunch::Tick::kOwnerLen && who[i]; ++i) {
            g_SeenInFlight[g_SeenCount][i] = who[i];
        }
        g_SeenInFlight[g_SeenCount][i] = 0;
        ++g_SeenCount;
    }
}

static void TickA() { RecordTick('A'); }
static void TickB() { RecordTick('B'); }

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
// the third rule in docs/framework/modules.md it can only confirm what the code already
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
    // Unbuffered, because this binary can crash. With block-buffered stdout
    // a segfault discards everything printed so far, so the run looks like a
    // program that produced no output at all - which says nothing about where
    // it got to. Documented in docs/framework/modules.md; it applies to every test
    // binary here, not only the one where it was first noticed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    BeginSection("encoding");
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

    EndSection(6);
    BeginSection("pc-relative detection");
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

    EndSection(10);
    BeginSection("refusal");
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

    EndSection(6);
    BeginSection("three-deep chain");
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

    EndSection(14);
    BeginSection("Original stability");
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

    EndSection(4);
    BeginSection("decoder fuzz");
    FuzzDecoder();

    EndSection(4);
    BeginSection("patches vs hooks");
    std::printf("\npatches against hook windows:\n");
    {
        // A patch landing inside the 16 bytes a hook displaced writes into the
        // long jump, not the game - the instructions it is aiming at live in a
        // trampoline now. The host's own GX2 hook installs before any module
        // loads, so this is reachable on a real boot, not hypothetically.
        // ONE buffer, and every address in this section is an offset into it.
        //
        // The first version used two separate static arrays and computed a
        // patch's target as the difference between them. That is undefined
        // behaviour across distinct objects, and in practice it crashed: the
        // difference is not guaranteed positive, and a negative one truncated
        // to uint32_t resolves to an address nowhere near either array.
        //
        // A 32-bit game address cannot name a 64-bit host buffer at all, which
        // is what Patches::SetAddressBase exists for - the test declares where
        // game address 0 lives and then speaks in the offsets a real patch
        // record would hold.
        alignas(64) static uint8_t world[256];
        // Deliberately NOT offset 0: targetAddr 0 is refused as BAD-TARGET
        // before any window check runs, and a game address of zero is never a
        // real patch target. Putting the function there made 15 of 16 window
        // bytes refuse for the right reason and one for a different one.
        const uint32_t kHookedAt = 64;     // a function, for InstallHook
        const uint32_t kPlainAt = 128;     // ordinary bytes, hooked by nobody

        uint32_t* const hooked = reinterpret_cast<uint32_t*>(world + kHookedAt);
        uint8_t* const plain = world + kPlainAt;

        auto ResetWorld = [&]() {
            H::ResetForTest();
            P::ResetForTest();
            P::SetArena(0, 0);
            P::SetAddressBase(Addr(world));
            FillPrologue();
            for (int i = 0; i < 32; ++i) hooked[i] = 0x60000000u;
            hooked[0] = 0x9421FFE0u;
            hooked[1] = 0x7C0802A6u;
            hooked[2] = 0x90010024u;
            hooked[3] = 0x38600001u;
            for (int i = 0; i < 64; ++i) plain[i] = static_cast<uint8_t>(0x10 + i);
        };

        // Builds a patch that expects whatever is currently at `addr`, so the
        // origin check passes and the OTHER checks are what decide.
        // `at` is an OFFSET into world, which is what targetAddr holds.
        auto PatchAt = [&](uint32_t off, uint32_t size) {
            const uintptr_t addr = off;
            WiiXLaunch::Wxlm::PatchEntry pe{};
            pe.targetAddr = static_cast<uint32_t>(addr);
            pe.size = size;
            const uint8_t* at = world + off;
            for (uint32_t i = 0; i < size && i < WiiXLaunch::Wxlm::kMaxPatchBytes; ++i) {
                pe.origin[i] = at[i];
                pe.data[i] = static_cast<uint8_t>(~at[i]);
            }
            return pe;
        };

        // --- the positive control, first ------------------------------------
        //
        // Without this the whole section passes if Apply refuses everything.
        ResetWorld();
        {
            auto pe = PatchAt(kPlainAt + 8, 4);
            const uint8_t want[4] = { pe.data[0], pe.data[1], pe.data[2], pe.data[3] };
            ok("a patch to an unhooked address is applied",
               P::Apply(pe, "modP") == P::Result::Ok);
            ok("and the bytes actually changed",
               plain[8] == want[0] && plain[9] == want[1] &&
               plain[10] == want[2] && plain[11] == want[3]);
            ok("neighbouring bytes were left alone",
               plain[7] == 0x17 && plain[12] == 0x1C);
            ok("it is recorded as applied", P::AppliedCount() == 1);
        }

        // --- into a hooked window -------------------------------------------
        ResetWorld();
        {
            uintptr_t orig = 0;
            H::InstallHook(Addr(hooked), 0x01810000u, "modH", &orig);

            // Every byte of the displaced window, and the two bytes either side
            // of it. A window check written with <= or off by one shows up here
            // rather than on someone's console.
            int refusedInside = 0, allowedOutside = 0;
            for (uint32_t off = 0; off < 16u; ++off) {
                ResetWorld();
                H::InstallHook(Addr(hooked), 0x01810000u, "modH", &orig);
                auto pe = PatchAt(kHookedAt + off, 1);
                const char* who = nullptr;
                if (P::Check(pe, &who) == P::Result::HookedWindow) ++refusedInside;
            }
            ok("all 16 bytes of the displaced window are refused", refusedInside == 16);

            ResetWorld();
            H::InstallHook(Addr(hooked), 0x01810000u, "modH", &orig);
            {
                auto pe = PatchAt(kHookedAt + 16, 4);
                ok("the byte after the window is not refused",
                   P::Check(pe, nullptr) == P::Result::Ok);
                ++allowedOutside;
            }
            {
                // A four-byte patch ending one byte INTO the window overlaps.
                auto pe = PatchAt(kHookedAt + 13, 4);
                ok("a patch straddling the end of the window is refused",
                   P::Check(pe, nullptr) == P::Result::HookedWindow);
            }

            // Refused BY NAME, and naming the hook it collided with - that is
            // the whole diagnosis, and a bare false would carry neither.
            const char* who = nullptr;
            auto pe = PatchAt(kHookedAt + 4, 4);
            const P::Result r = P::Check(pe, &who);
            ok("refused as HOOKED-WINDOW, not something generic",
               std::strcmp(P::ResultName(r), "HOOKED-WINDOW") == 0);
            ok("and names the hook owner it collided with",
               who && std::strcmp(who, "modH") == 0);

            // Nothing was written by any of that.
            ok("a refused patch leaves the target untouched",
               H::DecodeLongJump(hooked) == Low(0x01810000u));
        }

        // --- origin verification --------------------------------------------
        ResetWorld();
        {
            auto pe = PatchAt(kPlainAt + 8, 4);
            pe.origin[2] ^= 0xFF;          // built against a different build
            ok("a patch whose origin does not match is refused",
               P::Apply(pe, "modP") == P::Result::OriginMismatch);
            ok("and the target is unchanged", plain[8] == 0x18 && plain[10] == 0x1A);
            ok("nothing was recorded as applied", P::AppliedCount() == 0);
        }

        // A hooked window ALSO fails an origin check - the jump is there, not
        // the prologue - so the order of the two decides which diagnosis a user
        // gets. HookedWindow must win: it sends them to another mod, where
        // ORIGIN-MISMATCH would send them to their game version.
        ResetWorld();
        {
            uintptr_t orig = 0;
            H::InstallHook(Addr(hooked), 0x01810000u, "modH", &orig);
            WiiXLaunch::Wxlm::PatchEntry pe{};
            pe.targetAddr = kHookedAt;
            pe.size = 4;
            for (int i = 0; i < 4; ++i) { pe.origin[i] = 0xAB; pe.data[i] = 0xCD; }
            ok("a hooked window is reported as HOOKED-WINDOW, not ORIGIN-MISMATCH",
               P::Check(pe, nullptr) == P::Result::HookedWindow);
        }

        // --- patch vs patch --------------------------------------------------
        ResetWorld();
        {
            ok("first patch applies", P::Apply(PatchAt(kPlainAt + 8, 4), "modA")
                                      == P::Result::Ok);
            const char* who = nullptr;
            auto pe = PatchAt(kPlainAt + 10, 4);
            ok("an overlapping second patch is refused",
               P::Check(pe, &who) == P::Result::PatchOverlap);
            ok("and names the module that got there first",
               who && std::strcmp(who, "modA") == 0);
            ok("a non-overlapping second patch is allowed",
               P::Check(PatchAt(kPlainAt + 12, 4), nullptr) == P::Result::Ok);
        }

        // --- malformed records ------------------------------------------------
        ResetWorld();
        {
            auto pe = PatchAt(kPlainAt, 4);
            pe.size = 0;
            ok("size 0 is refused", P::Check(pe, nullptr) == P::Result::BadSize);
            pe.size = WiiXLaunch::Wxlm::kMaxPatchBytes + 1;
            ok("size past the maximum is refused",
               P::Check(pe, nullptr) == P::Result::BadSize);
            pe = PatchAt(kPlainAt, 4);
            pe.targetAddr = 0;
            ok("a null target is refused", P::Check(pe, nullptr) == P::Result::BadTarget);
        }

        // --- the arena is off limits ------------------------------------------
        ResetWorld();
        {
            P::SetArena(Addr(world) + kPlainAt, 64);
            ok("a patch into the arena is refused",
               P::Check(PatchAt(kPlainAt + 8, 4), nullptr) == P::Result::IntoArena);
            P::SetArena(0, 0);
            ok("and allowed once that range is not the arena",
               P::Check(PatchAt(kPlainAt + 8, 4), nullptr) == P::Result::Ok);
        }
        P::ResetForTest();
        H::ResetForTest();
    }

    EndSection(20);
    BeginSection("per-frame ticks");
    std::printf("\nper-frame ticks:\n");
    {
        auto reg = [&](const char* what, T::Register got, T::Register want) {
            ++g_checks;
            if (got != want) {
                ++g_failures;
                std::printf("  FAIL  %s gave %s, expected %s\n", what,
                            T::RegisterName(got), T::RegisterName(want));
            } else {
                std::printf("  ok    %s -> %s\n", what, T::RegisterName(got));
            }
        };

        T::ResetForTest();

        // --- refusals, each by its own name --------------------------------
        MC::SetCurrent(nullptr);
        reg("registering outside a module", T::Add(&TickA), T::Register::NoModule);

        MC::SetCurrent("modA");
        reg("a null callback", T::Add(nullptr), T::Register::NullCallback);

        // --- and the positive control ---------------------------------------
        reg("modA registers", T::Add(&TickA), T::Register::Ok);
        reg("modA registering twice", T::Add(&TickA), T::Register::AlreadyRegistered);

        MC::SetCurrent("modB");
        reg("modB registers", T::Add(&TickB), T::Register::Ok);
        MC::SetCurrent(nullptr);

        ok("two ticks are registered", T::Count() == 2);

        // --- dispatch --------------------------------------------------------
        g_TickOrderLen = 0; g_TickOrder[0] = 0; g_SeenCount = 0;
        const uint32_t seqBefore = T::InFlight().sequence;

        T::RunAll();
        ok("one dispatch calls every tick once",
           std::strcmp(g_TickOrder, "AB") == 0);
        if (std::strcmp(g_TickOrder, "AB") != 0) {
            std::printf("        order was '%s'\n", g_TickOrder);
        }

        T::RunAll();
        ok("a second dispatch calls them again in the same order",
           std::strcmp(g_TickOrder, "ABAB") == 0);
        ok("registration order is call order - modA registered first",
           g_TickOrder[0] == 'A');

        // --- the field a hang leaves behind ----------------------------------
        //
        // This is the whole reason the marker exists: "my game freezes with
        // these mods installed" has to name a module. Checked from INSIDE the
        // callbacks, because that is the only moment it matters.
        ok("in-flight named modA while modA's tick ran",
           g_SeenCount >= 1 && std::strcmp(g_SeenInFlight[0], "modA") == 0);
        ok("in-flight named modB while modB's tick ran",
           g_SeenCount >= 2 && std::strcmp(g_SeenInFlight[1], "modB") == 0);
        if (g_SeenCount >= 2 &&
            (std::strcmp(g_SeenInFlight[0], "modA") != 0 ||
             std::strcmp(g_SeenInFlight[1], "modB") != 0)) {
            std::printf("        saw '%s' then '%s'\n",
                        g_SeenInFlight[0], g_SeenInFlight[1]);
        }

        ok("in-flight is cleared between dispatches",
           T::InFlight().owner[0] == '\0');
        ok("depth returns to zero", T::InFlight().depth == 0);
        ok("the sequence advanced once per tick call",
           T::InFlight().sequence == seqBefore + 4u);
        ok("the record carries its magic so a dump can find it",
           T::InFlight().magic == T::kInFlightMagic);
        ok("dispatches were counted", T::Dispatches() == 2);

        // --- a registered tick with nothing driving it ------------------------
        //
        // The state a host with no game module is in. It must be visible, not
        // inferred from a mod that quietly does nothing.
        ok("no source is nominated by default", T::SourceName() == nullptr);
        T::NominateSource("test harness");
        ok("a nominated source is recorded",
           T::SourceName() != nullptr &&
           std::strcmp(T::SourceName(), "test harness") == 0);

        // --- slots ------------------------------------------------------------
        T::ResetForTest();
        char ids[T::kMaxTicks + 2][8];
        uint32_t accepted = 0;
        for (uint32_t i = 0; i < T::kMaxTicks + 2u; ++i) {
            ids[i][0] = 'm'; ids[i][1] = (char)('0' + (i / 10));
            ids[i][2] = (char)('0' + (i % 10)); ids[i][3] = 0;
            MC::SetCurrent(ids[i]);
            if (T::Add(&TickA) == T::Register::Ok) ++accepted;
        }
        MC::SetCurrent(nullptr);
        ok("registration stops at the slot limit", accepted == T::kMaxTicks);

        T::ResetForTest();
        ok("reset clears the registry", T::Count() == 0);
    }

    EndSection(18);
    BeginSection("site isolation");
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

    EndSection(4);
    g_failures += g_SectionFloorFailures;

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);

    if (g_checks < kExpectedChecks) {
        std::printf("HOOK TESTS DISARMED: only %d of the expected %d checks ran.\n",
                    g_checks, kExpectedChecks);
        return 1;
    }
    std::printf("%s (%d checks: encoding, PC-relative refusal, three-deep chain "
                "construction, Original stability, site isolation, 526k-word "
                "decoder fuzz, patch-vs-hook, ticks)\n",
                g_failures == 0 ? "ALL HOOK TESTS PASS" : "HOOK TESTS FAILED", g_checks);
    return g_failures != 0;
}
