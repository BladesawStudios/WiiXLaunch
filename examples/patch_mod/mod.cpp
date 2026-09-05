// c_patch.wxlm - declared patches, one of each outcome.
//
// Three patches in one module, so a single boot shows the applied path and both
// refusal paths, each named and each with the host asserting the result rather
// than a human reading five log lines and deciding they look right.
//
//   1. APPLIED         an inert change to a real game instruction
//   2. ORIGIN-MISMATCH built against a build this is not
//   3. HOOKED-WINDOW   aimed inside bytes the host's own hook displaced
//
// The third is the case that is reachable today rather than hypothetically: the
// host installs its GX2 hook during WiiXLaunch_Init, before any module loads, so
// this collision exists on every boot.
//
// ---------------------------------------------------------------------------
// WHY PATCH 1 IS SAFE. The address was chosen deliberately and the reasoning is
// recorded here so nobody later assumes it was picked carelessly.
//
// TWO INDEPENDENT PROPERTIES, either of which would do on its own.
//
// FIRST, the change is inert. 0x02000030 holds 7C 9E 23 78, which is
// `or r30,r4,r4` - the PowerPC idiom for `mr r30,r4`, "copy r4 into r30". It is
// replaced with 60 9E 00 00, which is `ori r30,r4,0`, "r30 = r4 | 0". Both
// compute r30 = r4, and neither has an Rc bit, so neither touches the condition
// register. Same operation, two encodings; the instruction can be executed
// before, during or after the patch with identical results.
//
// The encodings were verified rather than calculated - the first calculation
// was wrong. The `or` field layout was checked against a second instance in the
// same binary (7C 7F 1B 78 = `or r31,r3,r3`), and `ori` against the fact that
// 60 00 00 00, the canonical nop, IS `ori r0,r0,0` with every field zero. The
// bytes came from Ghidra on the actual v208 program, whose GX2::Init prologue
// matches what the host reports hooking - a program-identity check worth doing
// whenever an address comes out of a disassembler.
//
// SECOND, and this is what makes it safe even if the first is wrong, THE PATCH
// IS PUT BACK. Patches::RestoreAll rewrites the original bytes and reads them
// back before any module entry runs, so the game executes modified for the few
// microseconds between the applier writing and the host restoring. "Provably
// inert forever" is a hard claim to support; "does not outlive the load
// sequence" is not, and the assertion is exactly as strong either way - the
// host still read the target back and saw the write.
//
// WHAT WAS CONSIDERED AND REJECTED. The gap at 0x02000008..0x0200001F between
// .syscall and .text looked like dead alignment padding, but it is not in the
// program image at all - Ghidra cannot read it - so its contents and even
// whether it is mapped could not be verified. An unreferenced word in .rodata
// was the other candidate; Ghidra does resolve lis/displacement pairs into real
// xrefs, so "no xrefs" there is decent evidence, but it would still miss data
// reached by a computed index, which makes it evidence rather than proof.
//
// A NOTE ON CEMU. Patches are applied at the load point, by which time Cemu's
// recompiler may already have translated this block; a write to already-compiled
// code does not necessarily change behaviour until the block is re-translated.
// It does not matter here, because the change is inert either way and the host
// verifies by reading MEMORY back rather than by observing behaviour. It does
// matter for a real patch mod, and docs/loader.md says so.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <wiixlaunch/patch_decl.hpp>

extern "C" {
    extern void wiixl_import__wiixl_core__Log(const char* text);
}

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;

// --- 1. applied ------------------------------------------------------------
// `or r30,r4,r4` becomes `ori r30,r4,0`. Same result, different bytes, so the
// host can read the target back and prove something was written.
WIIXL_DECLARE_PATCH(inert_reencode, 0x02000030,
    WIIXL_PATCH_BYTES(0x7C, 0x9E, 0x23, 0x78),
    WIIXL_PATCH_BYTES(0x60, 0x9E, 0x00, 0x00));

// --- 2. refused: ORIGIN-MISMATCH -------------------------------------------
// A REAL address with a REALISTIC wrong origin. 0x02000034 holds 7C 7F 1B 79,
// which is `or. r31,r3,r3`; this claims 7C 7F 1B 78, which is `or r31,r3,r3` -
// the same instruction without the Rc bit, a ONE-BIT difference.
//
// That is what a recompiled game version actually looks like, and it is a far
// better test than an obviously bogus value: it exercises the byte-by-byte
// read-back rather than anything that could be short-circuited by a sanity
// check on the shape of the origin. The mismatch is still guaranteed, because
// the real byte is 0x79 and the declared one is 0x78.
WIIXL_DECLARE_PATCH(wrong_build, 0x02000034,
    WIIXL_PATCH_BYTES(0x7C, 0x7F, 0x1B, 0x78),
    WIIXL_PATCH_BYTES(0x60, 0x00, 0x00, 0x00));

// --- 3. refused: HOOKED-WINDOW ---------------------------------------------
// 0x03A75D48 is GX2::Init on BotW v208, which the host hooks before any module
// is loaded. This aims four bytes into that function - inside the 16 bytes the
// hook manager displaced - so it would write into the long jump rather than the
// game, and the instructions it thinks it is editing live in a trampoline now.
//
// The origin is DELIBERATELY WRONG as well. HOOKED-WINDOW is checked before
// ORIGIN-MISMATCH, so this should be refused for the window; giving it a bogus
// origin too means that if the window check ever regressed, this patch is still
// refused rather than quietly corrupting a hook chain.
WIIXL_DECLARE_PATCH(into_hook_window, 0x03A75D4C,
    WIIXL_PATCH_BYTES(0xBA, 0xAD, 0xF0, 0x0D),
    WIIXL_PATCH_BYTES(0x60, 0x00, 0x00, 0x00));

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;
    // The patches were applied - or refused - before this ran. That ordering is
    // the point of declaring them as data: the host had the whole set before any
    // module code executed. See wiixlaunch/patches.hpp.
    log("c_patch: entry running; my patches were resolved before this line");
}
