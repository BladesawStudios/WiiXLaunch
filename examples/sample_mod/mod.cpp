// A minimal .wxlm module - the thing the loader loads.
//
// It exists to prove the whole path end to end: relocations against the
// module's own data, imports resolved through the surface registry, a static
// constructor the loader has to run, and .bss the loader has to zero. Each is a
// separate mechanism that fails differently, so each is exercised and reported.
//
// HOW IMPORTS WORK. A module does not link against the host - there is nothing
// to link against, since the host is already in memory at an address the module
// cannot know. Instead it declares an UNDEFINED symbol named
//
//     wiixl_import__<surface with dots as underscores>__<Symbol>
//
// and takes its ADDRESS into a variable in .data. That produces one
// R_PPC_ADDR32 relocation against an undefined symbol, which scripts/wxlm.py
// recognises by name and rewrites into a kind-4 relocation carrying an
// import-table index. At load time the loader resolves it through the surface
// registry and writes the real function pointer into that slot.
//
// EVERYTHING THE LOADER WRITES MUST BE volatile, and this is not a style
// preference - the first version of this file was wrong in two ways that only
// showed up on building it:
//
//   * Without volatile on the import pointers, the compiler saw
//     `g_Log = &wiixl_import__...` and folded the indirect call into a DIRECT
//     one, emitting R_PPC_REL24 against the undefined symbol. A branch
//     relocation cannot reach an arbitrary host address, and wxlm.py rightly
//     refuses to build it. volatile says what is actually true: the loader
//     writes these at runtime.
//
//   * Without volatile on the .bss probes, the compiler proved a static array
//     that is never written must be all zero, folded the check to a constant,
//     and dropped the array - so the test for "did the loader zero .bss?"
//     compiled down to "yes". The array vanished from .bss entirely.
//
// A test the optimizer can answer is not testing the loader.

#include <cstdint>

extern "C" {
    // wiixl.core v1. The name encodes the surface, so nothing has to be passed
    // on a command line where the two could disagree.
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__AbiVersion(void);
    extern void*    wiixl_import__wiixl_core__Alloc(uint32_t size, uint32_t align);
    // v1.1. Resolving these at all is the minor-at-least rule working: this
    // module declares wiixl.core 1.0 and gets 1.1, and the three appended
    // symbols are there.
    extern uint32_t wiixl_import__wiixl_core__HeapGranted(void);
    extern uint32_t wiixl_import__wiixl_core__HeapUsed(void);
    extern uint32_t wiixl_import__wiixl_core__HeapRemaining(void);
}

using LogFn   = void (*)(const char*);
using AbiFn   = uint32_t (*)(void);
using AllocFn = void* (*)(uint32_t, uint32_t);
using U32Fn   = uint32_t (*)(void);

// In .data, one ADDR32 relocation each against an undefined symbol. volatile so
// the call goes through the pointer the loader patched, not through the address
// the compiler thinks it knows.
static LogFn   volatile g_Log   = &wiixl_import__wiixl_core__Log;
static AbiFn   volatile g_Abi   = &wiixl_import__wiixl_core__AbiVersion;
static AllocFn volatile g_Alloc = &wiixl_import__wiixl_core__Alloc;
static U32Fn   volatile g_Granted   = &wiixl_import__wiixl_core__HeapGranted;
static U32Fn   volatile g_Used      = &wiixl_import__wiixl_core__HeapUsed;
static U32Fn   volatile g_Remaining = &wiixl_import__wiixl_core__HeapRemaining;

// A pointer to the module's own rodata: an ordinary relocation against the
// module itself, which the loader fixes by adding the module's base. If base
// were wrong this prints garbage rather than crashing, which makes it a useful
// canary for the relocation path.
static const char kGreeting[] = "sample.wxlm: loaded, relocated and running";
static const char* volatile g_Greeting = kGreeting;

// .bss - the loader has to zero this. volatile so the compiler cannot conclude
// that an unwritten static must be zero and delete the check.
static volatile uint32_t g_ZeroCheck[16];
static volatile uint32_t g_Counter;

// A static constructor. Nothing but the loader will ever run it: the flat build
// has no crt0, scripts/cemu.ld has no .init_array output section, and the
// host's bootstrap never walks one. The write is through a volatile so the
// constructor cannot be constant-folded into a .data initialiser, which is what
// happened the first time and left .init_array empty.
static volatile uint32_t g_CtorRan;
namespace {
struct CtorProbe {
    CtorProbe() { g_CtorRan = 0xC70FA11u; }
};
CtorProbe g_CtorProbe;
} // namespace

// wiixl.core Log takes a finished string on purpose - a vararg mismatch across
// the module boundary is not diagnosable - so a module that wants numbers in its
// log formats them itself. No libc here: the flat build has none.
namespace {

char* AppendText(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

char* AppendU32(char* out, char* end, uint32_t v) {
    char tmp[11];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = static_cast<char>('0' + (v % 10u)); v /= 10u; }
    while (n > 0 && out < end - 1) *out++ = tmp[--n];
    return out;
}

} // namespace

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    log(const_cast<const char*>(g_Greeting));

    uint32_t dirty = 0;
    for (uint32_t i = 0; i < 16; ++i) dirty |= g_ZeroCheck[i];
    log(dirty == 0 ? "sample.wxlm: bss was zeroed"
                   : "sample.wxlm: BSS WAS NOT ZEROED");

    log(g_CtorRan == 0xC70FA11u ? "sample.wxlm: static constructor ran"
                                : "sample.wxlm: STATIC CONSTRUCTOR DID NOT RUN");

    AbiFn abi = g_Abi;
    if (abi) {
        log(abi() == 1u ? "sample.wxlm: wiixl.core AbiVersion() == 1"
                        : "sample.wxlm: AbiVersion() RETURNED SOMETHING ELSE");
    }

    // Allocation through the surface, into this module bounded sub-arena.
    //
    // A pointer coming back only proves the call returned something. What is
    // worth proving is that the accounting is REAL: that the host charged this
    // module for exactly what it took, out of a grant this module can see. So
    // the numbers are read on both sides of the allocation and the module works
    // out the delta ITSELF, rather than printing whatever the host reports and
    // calling that agreement.
    //
    // The expected delta is 256 plus up to 63 bytes of alignment padding: the
    // sub-arena base is wherever the downward carve landed and is not
    // guaranteed 64-aligned, so the first 64-aligned allocation inside it can
    // skip a few bytes. Anything outside that window is a real accounting bug
    // and says so by name.
    AllocFn alloc = g_Alloc;
    U32Fn granted = g_Granted, used = g_Used, remaining = g_Remaining;
    if (alloc && granted && used && remaining) {
        char line[192];
        char* end = line + sizeof(line);

        const uint32_t grantedBytes = granted();
        const uint32_t beforeUsed = used();
        const uint32_t beforeLeft = remaining();

        // Asked before allocating - the whole point of HeapGranted being
        // callable during load. A best-effort module sizes its buffers off this
        // instead of finding out by getting null.
        char* o = AppendText(line, end, "sample.wxlm: granted ");
        o = AppendU32(o, end, grantedBytes);
        o = AppendText(o, end, " B, used ");
        o = AppendU32(o, end, beforeUsed);
        o = AppendText(o, end, " B, remaining ");
        o = AppendU32(o, end, beforeLeft);
        o = AppendText(o, end, " B before Alloc(256)");
        *o = 0;
        log(line);

        void* p = alloc(256, 64);
        const uint32_t afterUsed = used();
        const uint32_t afterLeft = remaining();

        o = AppendText(line, end, "sample.wxlm: Alloc(256) ");
        o = AppendText(o, end, p ? "OK" : "returned NULL");
        o = AppendText(o, end, ", used ");
        o = AppendU32(o, end, afterUsed);
        o = AppendText(o, end, " B, remaining ");
        o = AppendU32(o, end, afterLeft);
        o = AppendText(o, end, " B");
        *o = 0;
        log(line);

        if (p) {
            const uint32_t spent = beforeLeft - afterLeft;
            const bool consistent = (afterUsed - beforeUsed) == spent &&
                                    (afterLeft + afterUsed) == grantedBytes &&
                                    spent >= 256u && spent <= 256u + 63u;
            o = AppendText(line, end, consistent
                    ? "sample.wxlm: arena accounting checks out - remaining fell by "
                    : "sample.wxlm: ARENA ACCOUNTING IS WRONG - remaining fell by ");
            o = AppendU32(o, end, spent);
            o = AppendText(o, end, " B for a 256 B request (expected 256..319), used rose by ");
            o = AppendU32(o, end, afterUsed - beforeUsed);
            o = AppendText(o, end, " B, and used+remaining is ");
            o = AppendU32(o, end, afterUsed + afterLeft);
            o = AppendText(o, end, " of ");
            o = AppendU32(o, end, grantedBytes);
            *o = 0;
            log(line);
        }
    }

    g_Counter = g_Counter + 1;
    log("sample.wxlm: entry complete");
}
