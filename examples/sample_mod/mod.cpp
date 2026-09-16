// Minimal .wxlm module demonstrating relocations, imports, static ctors, and .bss.
//
// Imports declare wiixl_import__<surface>__<Symbol> and take its address into .data.
// scripts/wxlm.py converts these into kind-4 relocations resolved at load time.
// Pointers and probes are volatile to prevent compiler dead-code elimination.

#include <cstdint>

extern "C" {
    // wiixl.core v1.0
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__AbiVersion(void);
    extern void*    wiixl_import__wiixl_core__Alloc(uint32_t size, uint32_t align);
    // wiixl.core v1.1
    extern uint32_t wiixl_import__wiixl_core__HeapGranted(void);
    extern uint32_t wiixl_import__wiixl_core__HeapUsed(void);
    extern uint32_t wiixl_import__wiixl_core__HeapRemaining(void);
}

using LogFn   = void (*)(const char*);
using AbiFn   = uint32_t (*)(void);
using AllocFn = void* (*)(uint32_t, uint32_t);
using U32Fn   = uint32_t (*)(void);

// Import function pointers patched by loader.
static LogFn   volatile g_Log   = &wiixl_import__wiixl_core__Log;
static AbiFn   volatile g_Abi   = &wiixl_import__wiixl_core__AbiVersion;
static AllocFn volatile g_Alloc = &wiixl_import__wiixl_core__Alloc;
static U32Fn   volatile g_Granted   = &wiixl_import__wiixl_core__HeapGranted;
static U32Fn   volatile g_Used      = &wiixl_import__wiixl_core__HeapUsed;
static U32Fn   volatile g_Remaining = &wiixl_import__wiixl_core__HeapRemaining;

// Relocation against module rodata.
static const char kGreeting[] = "sample.wxlm: loaded, relocated and running";
static const char* volatile g_Greeting = kGreeting;

// .bss zero check probe.
static volatile uint32_t g_ZeroCheck[16];
static volatile uint32_t g_Counter;

// Static constructor probe executed by loader.
static volatile uint32_t g_CtorRan;
namespace {
struct CtorProbe {
    CtorProbe() { g_CtorRan = 0xC70FA11u; }
};
CtorProbe g_CtorProbe;
} // namespace

// Minimal integer/string formatter (no libc available in flat build).
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

    // Verify sub-arena allocation and accounting consistency.
    AllocFn alloc = g_Alloc;
    U32Fn granted = g_Granted, used = g_Used, remaining = g_Remaining;
    if (alloc && granted && used && remaining) {
        char line[192];
        char* end = line + sizeof(line);

        const uint32_t grantedBytes = granted();
        const uint32_t beforeUsed = used();
        const uint32_t beforeLeft = remaining();

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
