// Fuzzes the .wxlm loader against malformed input, natively.
//
// The loader is the one component that reads data it did not produce and then
// writes to memory it executes. It is about to become the thing every mod on
// every platform goes through. Every bug found here costs a script run; found
// later it costs a boot loop on someone else's machine.
//
// This runs the REAL loader - include/wiixlaunch/loader/loader.hpp, compiled
// for WIIXL_HOST_TEST - against a memory reader and a counting allocator. What
// is under test is the validation and relocation logic, which is ordinary
// parsing and needs no console.
//
// WHAT THIS DOES NOT TEST: the big-endian byte layout of a real module. The
// baseline here is built in host-native order and tagged Machine::HostTest, so
// the loader's field reads work the same way they do on a console reading its
// own byte order. scripts/test_wxlm.py covers the layout agreement between the
// writer and the format.
//
// THE IMPORTANT CLASS IS CRC-CORRECT CORRUPTION. A checksum only proves the
// bytes are the ones the writer produced; it says nothing about whether their
// structure is sane. A module built by a slightly-wrong future writer has a
// valid checksum over invalid structure, and that is exactly the file that must
// not relocate. So every mutation recomputes the CRC before the loader sees it,
// which stops the integrity check short-circuiting the structural ones.
//
// The assertion is not merely "it returned an error". It is:
//   - a specific Reject code, for the cases with one right answer
//   - nothing was allocated, or everything allocated was released
//   - the entry point was never called
//   - no write landed outside an allocation (the allocator red-zones every one)

#define WIIXL_HOST_TEST 1
#define _CRT_SECURE_NO_WARNINGS 1

#include <wiixlaunch/loader/loader.hpp>
#include <wiixlaunch/loader/core_surface.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/mod_fs.hpp>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>

namespace Wxlm = WiiXLaunch::Wxlm;
namespace Loader = WiiXLaunch::Loader;
using Wxlm::Reject;

// ---------------------------------------------------------------------------
// A reader over a byte vector, satisfying what the loader needs.
// ---------------------------------------------------------------------------
// It ENFORCES coreinit's constraints, and that is the point rather than a
// detail.
//
// A plain byte array will happily serve any pointer and any length, so the
// first version of this reader accepted reads the real filesystem refuses - and
// the loader shipped violating them at every structured read. Cemu answered
// "FS handleAsyncResult(): unexpected error ffffffff" on the first boot, which
// is a failure at the FS layer that says nothing about alignment.
//
// A stand-in more permissive than the thing it stands in for does not test that
// thing. This one refuses what coreinit refuses: a destination that is not
// 64-byte aligned, and a length that is not a multiple of 64 unless the read
// reaches the end of the file.
int g_AlignmentViolations = 0;

class MemoryReader {
public:
    explicit MemoryReader(const std::vector<uint8_t>& data) : m_Data(data) {}

    uint32_t Size() const { return static_cast<uint32_t>(m_Data.size()); }

    uint32_t ReadAt(uint32_t offset, void* out, uint32_t size) {
        if ((reinterpret_cast<uintptr_t>(out) & 63u) != 0) {
            ++g_AlignmentViolations;
            std::printf("  FAIL  ReadAt destination is not 64-byte aligned - "
                        "coreinit refuses this\n");
            return 0;
        }
        const bool reachesEnd =
            (static_cast<uint64_t>(offset) + size >= m_Data.size());
        if ((size & 63u) != 0 && !reachesEnd) {
            ++g_AlignmentViolations;
            std::printf("  FAIL  ReadAt size %u is not a multiple of 64 and is not a "
                        "tail read - coreinit refuses this\n", size);
            return 0;
        }

        if (offset >= m_Data.size()) return 0;
        uint32_t avail = static_cast<uint32_t>(m_Data.size()) - offset;
        if (size > avail) size = avail;
        std::memcpy(out, m_Data.data() + offset, size);
        return size;
    }

private:
    const std::vector<uint8_t>& m_Data;
};

// ---------------------------------------------------------------------------
// Containment: did the loader write ONLY inside the sub-arena it was granted?
//
// This replaces a red-zoned allocator installed through the loader old
// AllocFn hook. That hook stopped being called when stage 5 moved the image
// onto Arena::AllocIn, so the canaries became bytes nothing could reach and the
// check passed without testing anything - a dead hook turning a live test
// vacuous, with no change in the case count to notice it by.
//
// The property checked here is stronger and cannot go dead the same way,
// because it is not attached to a hook. The whole reservation is poisoned
// before each case; after a load, every byte OUTSIDE the granted sub-arena must
// still be poison. That is the actual stage-5 invariant - a module cannot reach
// the host end of the arena or another module grant - stated in terms of the
// memory rather than in terms of the plumbing.
// ---------------------------------------------------------------------------
// Counted in the harness below; declared here because arena_backing uses them.
extern int g_ContainmentChecks;
extern int g_LivenessChecks;

namespace alloc {

constexpr uint8_t kPoison = 0xA5;

void Flush(uintptr_t, uint32_t) {}

} // namespace alloc

// ---------------------------------------------------------------------------
// A valid baseline module, built by hand so the fuzzer owns every byte.
// ---------------------------------------------------------------------------
constexpr uint32_t kHdr = sizeof(Wxlm::Header);

struct Baseline {
    std::vector<uint8_t> bytes;
    uint32_t payloadOffset, payloadSize;
    uint32_t relocOffset, relocCount;
    uint32_t importOffset, importCount;
    uint32_t requiredOffset, requiredCount;
    uint32_t stringOffset, stringSize;
};

static uint32_t Fnv1a(const char* s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 0x01000193u; }
    return h;
}

static void Put32(std::vector<uint8_t>& v, uint32_t at, uint32_t value) {
    std::memcpy(v.data() + at, &value, 4);
}
static void Put16(std::vector<uint8_t>& v, uint32_t at, uint16_t value) {
    std::memcpy(v.data() + at, &value, 2);
}
static uint32_t Get32(const std::vector<uint8_t>& v, uint32_t at) {
    uint32_t out; std::memcpy(&out, v.data() + at, 4); return out;
}

static void Recrc(std::vector<uint8_t>& v) {
    if (v.size() < kHdr) return;
    Wxlm::Header h{};
    std::memcpy(&h, v.data(), kHdr);
    h.fileSize = static_cast<uint32_t>(v.size());
    h.contentCrc32 = Wxlm::Crc32(v.data() + kHdr, static_cast<uint32_t>(v.size()) - kHdr);
    std::memcpy(v.data(), &h, kHdr);
}

static Baseline MakeBaseline() {
    Baseline b{};

    // Payload: 64 bytes. Offset 0 is the entry; 0x20 holds a pointer the
    // relocator fixes; 0x30 is where an import lands.
    std::vector<uint8_t> payload(64, 0);

    const char* surface = "wiixl.core";
    const char* symbol  = "Log";
    std::vector<uint8_t> strings;
    strings.push_back(0);
    const uint32_t surfOff = static_cast<uint32_t>(strings.size());
    strings.insert(strings.end(), surface, surface + std::strlen(surface) + 1);
    const uint32_t symOff = static_cast<uint32_t>(strings.size());
    strings.insert(strings.end(), symbol, symbol + std::strlen(symbol) + 1);

    // Two relocations: one module-relative, one import.
    struct R { uint32_t hdr, val; };
    std::vector<R> relocs = {
        { (0u << 24) | 0x20u, 0x10u },   // ADDR32 -> payload + 0x10
        { (4u << 24) | 0x30u, 0u    },   // Import -> import[0]
    };

    Wxlm::ImportEntry imp{};
    imp.surfaceNameOffset = surfOff;
    imp.symbolNameOffset = symOff;
    imp.symbolHash = Fnv1a(symbol);
    imp.versionMajor = 1;
    imp.versionMinor = 0;

    Wxlm::RequiredSurface req{};
    req.nameOffset = surfOff;
    req.versionMajor = 1;
    req.versionMinor = 0;

    auto a4 = [](uint32_t n) { return (n + 3u) & ~3u; };

    uint32_t off = kHdr;
    b.payloadOffset = off;  b.payloadSize = static_cast<uint32_t>(payload.size());
    off = a4(off + b.payloadSize);
    b.relocOffset = off;    b.relocCount = static_cast<uint32_t>(relocs.size());
    off = a4(off + b.relocCount * 8u);
    b.importOffset = off;   b.importCount = 1;
    off = a4(off + b.importCount * sizeof(Wxlm::ImportEntry));
    b.requiredOffset = off; b.requiredCount = 1;
    off = a4(off + b.requiredCount * sizeof(Wxlm::RequiredSurface));
    b.stringOffset = off;   b.stringSize = static_cast<uint32_t>(strings.size());
    off = a4(off + b.stringSize);

    b.bytes.assign(off, 0);

    Wxlm::Header h{};
    h.magic = Wxlm::kMagic;
    h.formatVersion = Wxlm::kFormatVersion;
    h.machine = static_cast<uint16_t>(Wxlm::kHostMachine);
    h.endian = static_cast<uint8_t>(Wxlm::kHostEndian);
    h.phase = static_cast<uint8_t>(Wxlm::Phase::Load);
    h.abiVersion = WiiXLaunch::Core::kAbiVersion;
    std::strncpy(h.modId, "fuzzbase", sizeof(h.modId));
    h.verMajor = 1;
    h.payloadOffset = b.payloadOffset;   h.payloadSize = b.payloadSize;
    h.relocOffset = b.relocOffset;       h.relocCount = b.relocCount;
    h.importOffset = b.importOffset;     h.importCount = b.importCount;
    h.requiredOffset = b.requiredOffset; h.requiredCount = b.requiredCount;
    h.stringOffset = b.stringOffset;     h.stringSize = b.stringSize;
    h.entryOffset = 0;
    h.bssSize = 32;

    std::memcpy(b.bytes.data(), &h, kHdr);
    std::memcpy(b.bytes.data() + b.payloadOffset, payload.data(), payload.size());
    std::memcpy(b.bytes.data() + b.relocOffset, relocs.data(), relocs.size() * 8);
    std::memcpy(b.bytes.data() + b.importOffset, &imp, sizeof(imp));
    std::memcpy(b.bytes.data() + b.requiredOffset, &req, sizeof(req));
    std::memcpy(b.bytes.data() + b.stringOffset, strings.data(), strings.size());

    Recrc(b.bytes);
    return b;
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------
static int g_Cases = 0, g_Failures = 0;
static int g_Accepted = 0, g_Rejected = 0;
// Relocation results read back and compared, as opposed to loads that merely
// succeeded. Floored below for the usual reason: a check that stops running
// looks exactly like a check that passes.
static int g_ValueChecks = 0;

// How many cases must run. The dead-hook incident (docs/modules.md, fourth
// rule) was invisible partly because THE CASE COUNT DID NOT MOVE - there was no
// number that a disarmed suite would have changed. This is that number. Raise
// it when cases are added; never lower it to make a build go green.
static const int kExpectedCases = 1181;

// And a floor on how many of them are ACCEPTED.
//
// The case total alone is not enough, and that is not hypothetical: when the
// module table stopped being reset between cases, every case still ran and the
// suite still passed, but accepted collapsed from 293 to 8 because the loader
// was refusing valid modules for a reason that had nothing to do with them. A
// suite that only counts how many times it ran cannot see that.
static const int kExpectedAccepted = 327;
static const int kExpectedRejected = 853;

// Both halves of the containment property must actually be exercised, or the
// pair reduces to the single check that went vacuous last time.
int g_ContainmentChecks = 0;
int g_LivenessChecks = 0;

// Offsets of the header fields the mutators poke, derived from the struct so
// they cannot drift from it.
#define OFF(field) static_cast<uint32_t>(offsetof(Wxlm::Header, field))

// The arena reservation for a host run. Real memory, so a granted sub-arena is
// a range the loader can actually write into - and so the containment check
// above has something genuine to inspect.
namespace arena_backing {
constexpr uint32_t kSize = 2u << 20;
uint8_t* g_Block = nullptr;
uintptr_t g_Base = 0;

void Init() {
    if (!g_Block) g_Block = static_cast<uint8_t*>(std::calloc(kSize + 64, 1));
    g_Base = (reinterpret_cast<uintptr_t>(g_Block) + 63u) & ~static_cast<uintptr_t>(63u);
    // Poison, not zero. Zeroed backing would make "the loader never touched
    // this" indistinguishable from "the loader zeroed it", which is exactly
    // what a .bss overrun past the grant would look like.
    std::memset(reinterpret_cast<void*>(g_Base), alloc::kPoison, kSize);
    WiiXLaunch::Arena::SetReservation(g_Base, kSize);
}

// Everything below the granted sub-arena must be untouched. Exactly one module
// is loaded per case, so ModuleCarved() is that module grant and the region
// below it is the host end plus whatever is still free.
bool ContainedInGrant() {
    ++g_ContainmentChecks;
    const uint32_t carved = WiiXLaunch::Arena::ModuleCarved();
    if (carved > kSize) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(g_Base);
    for (uint32_t i = 0; i < kSize - carved; ++i) {
        if (p[i] != alloc::kPoison) return false;
    }
    return true;
}

// The other half of the pair, and the reason the first half cannot go quietly
// vacuous the way the red-zone check did.
//
// ContainedInGrant() alone passes trivially if the loader never writes anywhere
// - which is exactly the state a dead hook leaves behind. So a module that
// loaded successfully must ALSO have disturbed the poison INSIDE its grant.
// Together the two say: it wrote, and it wrote only there. Neither statement is
// worth much without the other.
bool WroteInGrant() {
    ++g_LivenessChecks;
    const uint32_t carved = WiiXLaunch::Arena::ModuleCarved();
    if (carved == 0 || carved > kSize) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(g_Base) + (kSize - carved);
    for (uint32_t i = 0; i < carved; ++i) {
        if (p[i] != alloc::kPoison) return true;
    }
    return false;
}
} // namespace arena_backing

static Reject RunLoader(std::vector<uint8_t>& bytes) {
    // Fresh grants, fresh poison and a fresh module table per case: state left
    // behind by one case makes the next fail for the wrong reason. The module
    // table was missed at first, and every case after the eighth accepted one
    // came back NO-MEMORY.
    Loader::ResetForTest();
    arena_backing::Init();
    MemoryReader reader(bytes);
    return Loader::LoadFrom(reader);
}

// `expected` may be Reject::None to mean "any rejection will do", for mutations
// whose right answer depends on which check happens to trip first.
static void Case(const char* what, std::vector<uint8_t> bytes, Reject expected,
                 bool recrc = true) {
    ++g_Cases;
    if (recrc) Recrc(bytes);

    const Reject got = RunLoader(bytes);
    bool ok = true;
    std::string why;

    if (got == Reject::None) {
        ok = false;
        why = "ACCEPTED a malformed module";
    } else if (expected != Reject::None && got != expected) {
        ok = false;
        why = std::string("expected ") + Wxlm::RejectName(expected)
            + ", got " + Wxlm::RejectName(got);
    }

    // A REJECTED module must not have left anything behind either. Several
    // rejections happen after the image has been placed and partly relocated,
    // and those writes still have to have stayed inside the grant.
    if (ok && !arena_backing::ContainedInGrant()) {
        ok = false;
        why = "wrote outside the granted sub-arena before being rejected";
    }

    if (ok) { ++g_Rejected; }
    else {
        ++g_Failures;
        std::printf("  FAIL  %-52s %s\n", what, why.c_str());
    }
}

static void ExpectAccepted(const char* what, std::vector<uint8_t> bytes) {
    ++g_Cases;
    const Reject got = RunLoader(bytes);
    if (got != Reject::None) {
        ++g_Failures;
        std::printf("  FAIL  %-52s rejected a VALID module: %s\n",
                    what, Wxlm::RejectName(got));
    } else if (!arena_backing::ContainedInGrant()) {
        ++g_Failures;
        std::printf("  FAIL  %-52s wrote outside the granted sub-arena\n", what);
    } else if (!arena_backing::WroteInGrant()) {
        ++g_Failures;
        std::printf("  FAIL  %-52s loaded without writing inside its grant - the "
                    "containment check above is not looking at live memory\n", what);
    } else {
        ++g_Accepted;
    }
}

// ---------------------------------------------------------------------------
// Path resolution.
//
// This exists because a bug shipped through the gap it now covers. The module
// loader enumerated its directory with a raw FSOpenDir on the path as written,
// while every FILE open went through FS::impl::Candidates - so on the first
// two-module boot the loader reported "WiiXLaunch/mods does not exist" three
// lines after the load-point probe had listed that directory's contents.
//
// Nothing tested path resolution, so nothing could catch a second resolver
// disagreeing with the first. There is one resolver now, and this checks it.
// ---------------------------------------------------------------------------
static void TestPathCandidates() {
    std::printf("=== path candidates ===\n");
    int checks = 0, bad = 0;

    auto expect = [&](const char* what, bool cond) {
        ++checks;
        if (!cond) { ++bad; std::printf("  FAIL  %s\n", what); }
    };

    {
        char storage[3][256];
        const char* out[4];
        WiiXLaunch::FS::impl::Candidates("WiiXLaunch/mods", storage, out);

        expect("relative: candidate 0 is the path as given",
               out[0] && std::strcmp(out[0], "WiiXLaunch/mods") == 0);
        expect("relative: /vol/content/ prefix is offered",
               out[1] && std::strcmp(out[1], "/vol/content/WiiXLaunch/mods") == 0);
        expect("relative: content/ prefix is offered",
               out[2] && std::strcmp(out[2], "content/WiiXLaunch/mods") == 0);
        expect("relative: /vol/content/WiiXLaunch/ prefix is offered",
               out[3] && std::strcmp(out[3], "/vol/content/WiiXLaunch/WiiXLaunch/mods") == 0);

        // The one that actually mattered on the boot: the absolute form the
        // load-point probe uses has to be reachable from the relative form the
        // loader is given.
        bool reachesProbePath = false;
        for (int i = 0; i < 4; ++i) {
            if (out[i] && std::strcmp(out[i], "/vol/content/WiiXLaunch/mods") == 0) {
                reachesProbePath = true;
            }
        }
        expect("relative 'WiiXLaunch/mods' reaches '/vol/content/WiiXLaunch/mods'",
               reachesProbePath);
    }

    {
        // HasMountName decides whether an nn::fs call is safe or FATAL. On
        // Switch a path with no mount name does not fail, it aborts the
        // process - FindFileSystem calls nn::diag Abort and the game dies with
        // ResultFsInvalidMountName. The first Switch boot ever attempted died
        // exactly here, on the bare "WiiXLaunch/mods" that has always been
        // candidate 0.
        //
        // Checked on this host because the function is pure string handling and
        // the alternative is discovering the answer by killing a console.
        using WiiXLaunch::FS::impl::HasMountName;
        expect("mount name: 'sd:/WiiXLaunch/mods' is safe",
               HasMountName("sd:/WiiXLaunch/mods"));
        expect("mount name: 'sd:' alone is safe",
               HasMountName("sd:"));
        expect("mount name: bare relative path is NOT safe",
               !HasMountName("WiiXLaunch/mods"));
        expect("mount name: leading slash is NOT safe",
               !HasMountName("/vol/content/WiiXLaunch/mods"));
        expect("mount name: a colon after a separator does not count",
               !HasMountName("WiiXLaunch/mods:x"));
        expect("mount name: a leading colon is NOT safe",
               !HasMountName(":sd/mods"));
        expect("mount name: empty is NOT safe", !HasMountName(""));
        expect("mount name: null is NOT safe", !HasMountName(nullptr));
    }

    {
        char storage[3][256];
        const char* out[4];
        WiiXLaunch::FS::impl::Candidates("/vol/content/WiiXLaunch/mods", storage, out);
        expect("absolute: offered unchanged",
               out[0] && std::strcmp(out[0], "/vol/content/WiiXLaunch/mods") == 0);
        expect("absolute: no prefixes invented",
               out[1] == nullptr && out[2] == nullptr && out[3] == nullptr);
    }

    {
        // A name long enough to overrun the 256-byte buffers must truncate
        // rather than write past them.
        char longName[400];
        for (int i = 0; i < 399; ++i) longName[i] = 'x';
        longName[399] = '\0';
        char storage[3][256];
        const char* out[4];
        WiiXLaunch::FS::impl::Candidates(longName, storage, out);
        expect("over-long path is truncated, not overrun",
               std::strlen(storage[0]) < 256 && std::strlen(storage[1]) < 256 &&
               std::strlen(storage[2]) < 256);
    }

    g_Failures += bad;
    std::printf("  %d path checks, %d failures\n", checks, bad);
    if (checks < 8) {
        std::printf("  FAIL  path candidate suite shrank to %d checks\n", checks);
        ++g_Failures;
    }
}

// ---------------------------------------------------------------------------
// Mod-scoped path resolution.
//
// A mod reads its own directory through a call that CANNOT leave it. What
// "cannot" means is checked here rather than trusted: every refusal is a value,
// so a test can assert which rule fired, and the positive controls are here
// too - a scoping check that refuses everything would satisfy every escape
// assertion and be useless.
// ---------------------------------------------------------------------------
static void TestModPaths() {
    namespace MF = WiiXLaunch::ModFS;
    std::printf("=== mod-scoped paths ===\n");

    int checks = 0, bad = 0;
    auto expect = [&](const char* what, bool cond) {
        ++checks;
        if (!cond) { ++bad; std::printf("  FAIL  %s\n", what); }
    };
    auto reason = [&](const char* path, MF::PathResult want) {
        ++checks;
        const MF::PathResult got = MF::CheckScoped(path);
        if (got != want) {
            ++bad;
            std::printf("  FAIL  '%s' gave %s, expected %s\n", path,
                        MF::PathResultName(got), MF::PathResultName(want));
        }
    };

    // Outside a module there is no "own" directory, and that is its own reason
    // rather than a path problem.
    WiiXLaunch::ModContext::SetCurrent(nullptr);
    char out[MF::kMaxScopedPath];
    expect("outside a module, Resolve says NO-MODULE",
           MF::Resolve("a.txt", out) == MF::PathResult::NoModule);

    WiiXLaunch::ModContext::SetCurrent("a_first");

    // --- refusals, each by its own name ------------------------------------
    reason("/vol/content/Pack/Bootup.pack", MF::PathResult::Absolute);
    reason("/etc/passwd",                   MF::PathResult::Absolute);
    reason("..",                            MF::PathResult::ParentEscape);
    reason("../b_second/greeting.txt",      MF::PathResult::ParentEscape);
    reason("data/../../escape.txt",         MF::PathResult::ParentEscape);
    reason("data/..",                       MF::PathResult::ParentEscape);
    reason("a\\b.txt",                      MF::PathResult::BadChar);
    reason("a\tb.txt",                      MF::PathResult::BadChar);
    reason("",                              MF::PathResult::Empty);

    // --- and the positive controls -----------------------------------------
    //
    // Without these the whole section passes if CheckScoped refuses everything.
    reason("greeting.txt",                  MF::PathResult::Ok);
    reason("data/greeting.txt",             MF::PathResult::Ok);
    reason("deep/nested/path/file.bin",     MF::PathResult::Ok);

    // A ".." that is not a path COMPONENT is an ordinary filename. Refusing
    // these would make perfectly legal names unreadable and buy nothing.
    reason("version..txt",                  MF::PathResult::Ok);
    reason("..hidden",                      MF::PathResult::Ok);
    reason("a..b/c",                        MF::PathResult::Ok);

    // --- what a resolved path actually is -----------------------------------
    expect("a legal path resolves",
           MF::Resolve("greeting.txt", out) == MF::PathResult::Ok);
    expect("and lands under this module's own directory",
           std::strcmp(out, "WiiXLaunch/mods/a_first/greeting.txt") == 0);
    if (std::strcmp(out, "WiiXLaunch/mods/a_first/greeting.txt") != 0) {
        std::printf("        resolved to '%s'\n", out);
    }

    // THE POINT OF THE WHOLE SCHEME: the same filename in two mods is two
    // different files, decided by the host's idea of who is running rather than
    // by anything either mod said.
    WiiXLaunch::ModContext::SetCurrent("b_second");
    char other[MF::kMaxScopedPath];
    MF::Resolve("greeting.txt", other);
    expect("the same name under another module resolves elsewhere",
           std::strcmp(other, "WiiXLaunch/mods/b_second/greeting.txt") == 0);
    expect("and the two do not collide", std::strcmp(out, other) != 0);

    // A refused path must leave nothing usable behind.
    WiiXLaunch::ModContext::SetCurrent("a_first");
    char scratch[MF::kMaxScopedPath];
    scratch[0] = 'x';
    MF::Resolve("../escape", scratch);
    expect("a refused Resolve leaves an empty result", scratch[0] == '\0');

    // --- the reserved namespace ---------------------------------------------
    expect("_host is reserved", MF::IsReservedId("_host"));
    expect("any leading underscore is reserved", MF::IsReservedId("_anything"));
    expect("an ordinary id is not", !MF::IsReservedId("a_first"));
    expect("an underscore elsewhere is not", !MF::IsReservedId("a_first_x"));

    WiiXLaunch::ModContext::SetCurrent(nullptr);

    g_Failures += bad;
    std::printf("  %d mod-path checks, %d failures\n", checks, bad);
    if (checks < 25) {
        std::printf("  FAIL  mod-path suite shrank to %d checks\n", checks);
        ++g_Failures;
    }
}

int main() {
    // Unbuffered, because this binary can crash. With block-buffered stdout a
    // segfault discards everything printed so far, so a crash looks like a
    // program that produced no output at all and says nothing about where it
    // got to. That cost a debugging round once.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Loader::SetFlushHook(&alloc::Flush);
    arena_backing::Init();
    WiiXLaunch::Core::Register();

    const Baseline base = MakeBaseline();

    TestPathCandidates();
    TestModPaths();

    std::printf("=== baseline ===\n");
    ExpectAccepted("valid module loads", base.bytes);

    // --- header field corruption -------------------------------------------
    std::printf("=== header fields ===\n");
    {
        auto v = base.bytes; Put32(v, OFF(magic), 0xDEADBEEF);
        Case("bad magic", v, Reject::BadMagic);
    }
    {
        auto v = base.bytes;
        v[OFF(endian)] = static_cast<uint8_t>(Wxlm::kHostEndian) ^ 1u;
        Case("wrong endian", v, Reject::WrongEndian);
    }
    {
        auto v = base.bytes;
        uint16_t m = 0x1234; std::memcpy(v.data() + OFF(machine), &m, 2);
        Case("wrong machine", v, Reject::WrongMachine);
    }
    {
        auto v = base.bytes;
        uint16_t fv = Wxlm::kFormatVersion + 1;
        std::memcpy(v.data() + OFF(formatVersion), &fv, 2);
        Case("format version from the future", v, Reject::FormatTooNew);
    }
    {
        auto v = base.bytes;
        uint16_t abi = WiiXLaunch::Core::kAbiVersion + 7;
        std::memcpy(v.data() + OFF(abiVersion), &abi, 2);
        Case("abi mismatch", v, Reject::AbiMismatch);
    }
    {
        auto v = base.bytes; v[OFF(phase)] = 99;
        Case("unknown phase", v, Reject::BadPhase);
    }
    {
        auto v = base.bytes;
        uint16_t r = 1; std::memcpy(v.data() + OFF(reserved0), &r, 2);
        Case("reserved0 set", v, Reject::ReservedNotZero);
    }
    for (int i = 0; i < 4; ++i) {
        auto v = base.bytes;
        Put32(v, OFF(reserved1) + i * 4, 0x1u);
        char name[64];
        std::snprintf(name, sizeof(name), "reserved1[%d] set", i);
        Case(name, v, Reject::ReservedNotZero);
    }
    {
        auto v = base.bytes; Put32(v, OFF(declaredHookCount), 1);
        Case("declaredHookCount set (stage 6 field)", v, Reject::ReservedNotZero);
    }
    {
        // declaredPatch* is IMPLEMENTED now, so a count set with no table
        // behind it is a STRUCTURE error rather than a reserved-field one. The
        // expectation changed with the format; the case did not go away,
        // because "a count pointing at nothing" still has to be refused.
        auto v = base.bytes; Put32(v, OFF(declaredPatchCount), 1);
        Case("declaredPatchCount set with no table", v, Reject::BadSectionBounds);
    }
    {
        // A count large enough that count * sizeof(PatchEntry) wraps 32 bits.
        // Same shape as the payloadSize + bssSize wrap: the multiply is the
        // dangerous part, and the loader does it in 64 bits for this reason.
        auto v = base.bytes;
        Put32(v, OFF(declaredPatchOffset), kHdr);
        Put32(v, OFF(declaredPatchCount), 0xFFFFFFFFu / sizeof(Wxlm::PatchEntry) + 2u);
        Case("declaredPatchCount * 40 wraps 32 bits", v, Reject::BadSectionBounds);
    }
    {
        // A patch table that overlaps the payload. Sections may not overlap,
        // and the newest section is no exception.
        auto v = base.bytes;
        Put32(v, OFF(declaredPatchOffset), base.payloadOffset);
        Put32(v, OFF(declaredPatchCount), 1);
        Case("patch table overlaps the payload", v, Reject::BadSectionBounds);
    }

    // --- integrity ----------------------------------------------------------
    std::printf("=== integrity ===\n");
    {
        auto v = base.bytes;
        v[v.size() - 1] ^= 0xFFu;
        Case("content bit flip with a stale CRC", v, Reject::BadChecksum, /*recrc=*/false);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(fileSize), static_cast<uint32_t>(v.size()) + 16);
        Case("fileSize larger than the file", v, Reject::SizeMismatch, false);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(fileSize), static_cast<uint32_t>(v.size()) - 4);
        Case("fileSize smaller than the file", v, Reject::SizeMismatch, false);
    }
    {
        std::vector<uint8_t> v(base.bytes.begin(), base.bytes.begin() + kHdr / 2);
        Case("truncated mid-header", v, Reject::TooSmall, false);
    }

    // Truncation at every section boundary, CRC left stale on purpose so the
    // size check is what fires - a shorter file cannot have the recorded size.
    for (uint32_t cut : { base.payloadOffset, base.relocOffset, base.importOffset,
                          base.requiredOffset, base.stringOffset }) {
        std::vector<uint8_t> v(base.bytes.begin(), base.bytes.begin() + cut);
        char name[64];
        std::snprintf(name, sizeof(name), "truncated at offset %u", cut);
        Case(name, v, Reject::None, false);
    }

    // --- structural, every one with a CORRECT checksum ----------------------
    std::printf("=== structure (CRC recomputed, so integrity cannot shortcut) ===\n");
    // Counts of 0 and 1 are LEGAL values, not malformed ones - a module with no
    // relocations or no required surfaces is well-formed. Asserting they must be
    // rejected was a bug in this harness, not in the loader, and the first run
    // reported it as such. Only values whose span overflows or leaves the file
    // have a required answer.
    const uint32_t kOverflowPokes[] = { 0xFFFFFFFFu, 0x7FFFFFFFu, 0xFFFFFFF0u };
    const struct { const char* name; uint32_t off; } kCountFields[] = {
        { "payloadSize",   OFF(payloadSize)   },
        { "relocCount",    OFF(relocCount)    },
        { "importCount",   OFF(importCount)   },
        { "exportCount",   OFF(exportCount)   },
        { "requiredCount", OFF(requiredCount) },
        { "stringSize",    OFF(stringSize)    },
    };
    for (const auto& f : kCountFields) {
        for (uint32_t poke : kOverflowPokes) {
            auto v = base.bytes;
            Put32(v, f.off, poke);
            char name[96];
            std::snprintf(name, sizeof(name), "%s = 0x%X", f.name, poke);
            Case(name, v, Reject::BadSectionBounds);
        }
    }

    // A table pointed at the header is in bounds and reads header bytes as
    // entries. It must still be refused.
    {
        auto v = base.bytes;
        Put32(v, OFF(exportCount), 1);
        Put32(v, OFF(exportOffset), 0);
        Case("export table lying on the header", v, Reject::BadSectionBounds);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(stringOffset), base.relocOffset);
        Case("string blob overlapping the reloc table", v, Reject::BadSectionBounds);
    }

    const struct { const char* name; uint32_t off; } kOffsetFields[] = {
        { "payloadOffset",  OFF(payloadOffset)  },
        { "relocOffset",    OFF(relocOffset)    },
        { "importOffset",   OFF(importOffset)   },
        { "requiredOffset", OFF(requiredOffset) },
        { "stringOffset",   OFF(stringOffset)   },
    };
    for (const auto& f : kOffsetFields) {
        for (uint32_t poke : { 0xFFFFFFFFu, 0xFFFFFFF0u,
                               static_cast<uint32_t>(base.bytes.size()),
                               static_cast<uint32_t>(base.bytes.size()) - 1 }) {
            auto v = base.bytes;
            Put32(v, f.off, poke);
            char name[96];
            std::snprintf(name, sizeof(name), "%s = 0x%X", f.name, poke);
            Case(name, v, Reject::None);
        }
    }

    // --- entry and init_array ----------------------------------------------
    std::printf("=== entry and init_array ===\n");
    for (uint32_t poke : { base.payloadSize, base.payloadSize + 1, 0xFFFFFFFFu }) {
        auto v = base.bytes;
        Put32(v, OFF(entryOffset), poke);
        char name[96];
        std::snprintf(name, sizeof(name), "entryOffset = 0x%X (payload is %u)",
                      poke, base.payloadSize);
        Case(name, v, Reject::BadEntry);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(initArrayOffset), base.payloadSize - 4);
        Put32(v, OFF(initArrayCount), 1000);
        Case("init_array range crosses the payload end", v, Reject::BadSectionBounds);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(initArrayOffset), 0xFFFFFFF0u);
        Put32(v, OFF(initArrayCount), 4);
        Case("init_array offset wraps", v, Reject::BadSectionBounds);
    }

    // --- relocations --------------------------------------------------------
    std::printf("=== relocations ===\n");
    {
        auto v = base.bytes;
        Put32(v, base.relocOffset, (0u << 24) | 0x00FFFFFCu);  // site past payload
        Case("relocation site past the payload", v, Reject::BadRelocation);
    }
    {
        auto v = base.bytes;
        Put32(v, base.relocOffset, (9u << 24) | 0x10u);        // unknown kind
        Case("unknown relocation kind", v, Reject::BadRelocation);
    }
    {
        auto v = base.bytes;
        Put32(v, base.relocOffset + 8, (4u << 24) | 0x30u);
        Put32(v, base.relocOffset + 12, 999u);                 // import index OOB
        Case("import relocation names a missing import", v, Reject::BadRelocation);
    }
    {
        auto v = base.bytes;
        // Point the import at a symbol the registry does not have.
        Put32(v, base.importOffset + 8, 0xDEADBEEFu);
        Case("import symbol not exported by the surface", v, Reject::UnresolvedImport);
    }
    {
        auto v = base.bytes;
        // A required surface nobody registered: repoint the name at "Log".
        Put32(v, base.requiredOffset, 12u);
        Case("required surface not registered", v, Reject::MissingSurface);
    }

    // --- Addr64, the AArch64 fixup ------------------------------------------
    //
    // No PowerPC module can produce this kind, so on the consoles that exist
    // today it is code nothing reaches. This build is 64-bit and little-endian,
    // which is exactly the shape a Switch module has, so the fixup can be
    // exercised here for real - and the suite's own history is that a check
    // nothing reaches is worse than a check nobody wrote.
    {
        auto v = base.bytes;
        Put32(v, base.relocOffset, ((uint32_t)Wxlm::RelocKind::Addr64 << 24) | 0x28u);
        Put32(v, base.relocOffset + 4, 0x10u);
        Recrc(v);
        ExpectAccepted("Addr64 relocation inside the payload", v);

        // "It loaded" would not tell a correct 64-bit write from one truncated
        // to 32 bits: on a host whose arena sits below 4 GiB the low half is
        // right and the module runs anyway, which is the version of this bug
        // that ships. Read the eight bytes back and compare against the base
        // this run actually used.
        ++g_ValueChecks;
        const Loader::LoadedModule& m = Loader::impl::g_Modules[0];
        uint64_t wrote = 0;
        if (m.valid && m.image) std::memcpy(&wrote, m.image + 0x28, 8);
        const uint64_t want =
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m.image)) + 0x10ull;
        if (!m.valid || !m.image || wrote != want) {
            ++g_Failures;
            std::printf("  FAIL  %-52s site holds 0x%llX, expected 0x%llX\n",
                        "Addr64 wrote a full 64-bit pointer",
                        (unsigned long long)wrote, (unsigned long long)want);
        }
    }
    {
        // 0x3C + 8 runs four bytes past a 64-byte payload, into the bss - which
        // is memory the module owns, so the write would land somewhere legal
        // and corrupt silently. Under the old flat "offset + 4" bound this case
        // was ACCEPTED; it is the regression test for that bound being wrong.
        auto v = base.bytes;
        Put32(v, base.relocOffset, ((uint32_t)Wxlm::RelocKind::Addr64 << 24) | 0x3Cu);
        Put32(v, base.relocOffset + 4, 0x10u);
        Case("Addr64 site whose last four bytes leave the payload", v,
             Reject::BadRelocation);
    }

    // --- values a single bit flip cannot reach -----------------------------
    //
    // The sweep below changes one bit at a time, so it can never produce
    // 0xFFFFFFFF from a small value. These are the crafted cases, and the first
    // of them is a real bug found this way: payloadSize + bssSize wrapped in 32
    // bits, so the allocation succeeded small and the zeroing loop then ran
    // bssSize times.
    std::printf("=== crafted overflow values ===\n");
    {
        auto v = base.bytes;
        Put32(v, OFF(bssSize), 0xFFFFFFFFu);
        Case("bssSize 0xFFFFFFFF wraps payload+bss", v, Reject::BadSectionBounds);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(bssSize), 0xFFFFFFFFu - base.payloadSize + 1u);
        Case("payload+bss lands exactly on 2^32", v, Reject::BadSectionBounds);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(payloadSize), 0xFFFFFF00u);
        Put32(v, OFF(bssSize), 0x200u);
        Case("payloadSize huge, bss pushes it over", v, Reject::None);
    }

    // --- surface version refusal --------------------------------------------
    //
    // Until now the only evidence the version check worked was that everything
    // resolved - which is the same shape as a refusal path nothing reaches.
    // wiixl.core has been bumped three times in three stages and every module
    // kept loading, so the ACCEPT direction is thoroughly demonstrated and the
    // REFUSE direction had never once been exercised.
    //
    // The host here registers wiixl.core v1.3, the same as the real one.
    //
    // RequiredSurface is { nameOffset:4, versionMajor:2, versionMinor:2 }, so
    // the version fields sit at +4 and +6.
    // DERIVED FROM THE HOST, not hardcoded. These cases were pinned to v1.3 and
    // broke the moment wiixl.core went to v1.4 - "requires v1.4" stopped being
    // too new and started being exactly met. A test that has to be edited every
    // time the thing it tests is bumped will eventually be edited wrongly, and
    // in the meantime it fails for a reason that has nothing to do with the
    // property under test.
    const uint16_t hostMajor = WiiXLaunch::Core::kVersionMajor;
    const uint16_t hostMinor = WiiXLaunch::Core::kVersionMinor;
    std::printf("=== surface version refusal (host has wiixl.core v%u.%u) ===\n",
                hostMajor, hostMinor);
    {
        const uint32_t majorAt = base.requiredOffset + 4;
        const uint32_t minorAt = base.requiredOffset + 6;

        {
            // A mod needing a symbol appended AFTER this host was built. The
            // minor-at-least rule refuses it: the host cannot supply what it
            // does not have.
            auto v = base.bytes;
            Put16(v, minorAt, static_cast<uint16_t>(hostMinor + 1));
            Case("requires one minor past the host - too new", v, Reject::MissingSurface);
        }
        {
            // A major bump means a symbol changed or was removed, so a mod
            // built against the old shape would call the wrong function. This
            // must be refused even though the host's major is LOWER, because
            // incompatible is not the same as older.
            auto v = base.bytes;
            Put16(v, majorAt, static_cast<uint16_t>(hostMajor + 1));
            Put16(v, minorAt, 0);
            Case("requires one major above the host - incompatible", v,
                 Reject::MissingSurface);
        }
        {
            // The reverse major direction, in case anyone ever writes the
            // comparison as a < rather than a !=.
            auto v = base.bytes;
            Put16(v, majorAt, 0);
            Put16(v, minorAt, 0);
            Case("requires v0.0, host has v1.x - incompatible major", v,
                 Reject::MissingSurface);
        }
        {
            // A surface nobody registered, which is the third way to fail and
            // shares a Reject code with the two above - so the LOG has to
            // distinguish them even though the code does not.
            auto v = base.bytes;
            // Point the name at the symbol string ("Log"), which is not a
            // registered surface name.
            Put32(v, base.requiredOffset, base.stringSize - 4);
            Case("requires a surface that is not registered", v,
                 Reject::MissingSurface);
        }

        // --- and the accept side, so this is not a check that refuses all ---
        {
            auto v = base.bytes;
            Put16(v, minorAt, hostMinor);
            Recrc(v);
            ExpectAccepted("requires the host's exact minor - met", v);
        }
        {
            auto v = base.bytes;
            Put16(v, minorAt, static_cast<uint16_t>(hostMinor - 1));
            Recrc(v);
            ExpectAccepted("requires one minor below the host - minor-at-least", v);
        }

        // --- and the REASON, not just the refusal ---------------------------
        //
        // All four rejections above share one Reject code, so the code alone
        // cannot tell "too new" from "wrong major" from "not registered". The
        // reason used to live only in a WIIXL_LOG string, which is compiled out
        // here - so the check could have been right for the wrong reason and
        // nothing could see it. Surface::Check returns it as a value now.
        namespace S = WiiXLaunch::Surface;
        struct { const char* what; const char* name; uint16_t maj, min; S::Compat want; }
        compat[] = {
            { "the host's exact version",  "wiixl.core", hostMajor, hostMinor,
              S::Compat::Ok },
            { "minor 0 - oldest possible", "wiixl.core", hostMajor, 0,
              S::Compat::Ok },
            { "one minor past the host",   "wiixl.core", hostMajor,
              (uint16_t)(hostMinor + 1), S::Compat::MinorTooOld },
            { "far past the host",         "wiixl.core", hostMajor,
              (uint16_t)(hostMinor + 90), S::Compat::MinorTooOld },
            { "one major above",           "wiixl.core", (uint16_t)(hostMajor + 1), 0,
              S::Compat::MajorMismatch },
            { "one major below",           "wiixl.core", (uint16_t)(hostMajor - 1), 9,
              S::Compat::MajorMismatch },
            { "unregistered surface",      "nope.nope",  1, 0,
              S::Compat::NotPresent },
        };
        int compatChecked = 0, compatBad = 0;
        for (const auto& c : compat) {
            ++compatChecked;
            const S::Compat got = S::Check(c.name, c.maj, c.min);
            if (got != c.want) {
                ++compatBad;
                std::printf("  FAIL  Check(%s) said %s, expected %s\n",
                            c.what, S::CompatName(got), S::CompatName(c.want));
            }
        }
        ++g_Cases;
        if (compatBad || compatChecked < 7) {
            g_Failures += (compatBad ? compatBad : 1);
        } else {
            ++g_Rejected;
        }
        std::printf("  %d compatibility reasons checked, %d wrong\n",
                    compatChecked, compatBad);

        ++g_Cases;
        if (std::strcmp(Wxlm::RejectName(Reject::MissingSurface), "MISSING-SURFACE") != 0) {
            ++g_Failures;
            std::printf("  FAIL  MissingSurface is named '%s', expected "
                        "'MISSING-SURFACE'\n", Wxlm::RejectName(Reject::MissingSurface));
        } else {
            ++g_Rejected;
        }
    }

    // --- hostile heapRequest -----------------------------------------------
    //
    // Every case above this point runs the BEST-EFFORT path, because the
    // baseline heapRequest is 0. These exercise the stated-requirement path,
    // and specifically its arithmetic, which is the newest code in the loader.
    //
    // The loader does not pass heapRequest to the arena as written: a module
    // footprint is its code PLUS what it allocates, so it asks for
    // heapRequest + payloadSize + bssSize. That addition is the same shape as
    // the payloadSize + bssSize wrap the fuzzer already found - two attacker
    // controlled 32-bit values summed into a size - and it is exactly where the
    // next one of those would be.
    //
    // The boundary cases go on both sides deliberately. A bound that rejects
    // everything near it passes a one-sided test and is still wrong.
    const uint32_t kNeed = base.payloadSize + 32u;          // payload + bss
    const uint32_t kArena = arena_backing::kSize;           // free, reset per case

    {
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), 0xFFFFFFFFu);
        Case("heapRequest 0xFFFFFFFF", v, Reject::BadSectionBounds);
    }
    {
        // Fine as a standalone uint32 - it is not absurd and nothing about the
        // field alone rejects it - but heapRequest + image lands exactly on
        // 2^32 and wraps to 0. Wrapping to 0 is the dangerous direction: 0 is
        // the BEST-EFFORT sentinel, so a wrap would silently switch contracts
        // and hand this module a default grant it never asked for.
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), 0xFFFFFFFFu - kNeed + 1u);
        Case("heapRequest + image lands exactly on 2^32", v, Reject::BadSectionBounds);
    }
    {
        // One below the wrap: the sum is representable, so this must NOT be a
        // size error. It is simply more memory than exists, which is a
        // different diagnosis and has to say so.
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), 0xFFFFFFFFu - kNeed);
        Case("heapRequest + image lands exactly on 0xFFFFFFFF", v, Reject::NoMemory);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), kArena - kNeed + 1u);
        Case("heapRequest one byte over the free arena", v, Reject::NoMemory);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), kArena - kNeed - 1u);
        Recrc(v);
        ExpectAccepted("heapRequest one byte under the free arena", v);
    }
    {
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), kArena - kNeed);
        Recrc(v);
        ExpectAccepted("heapRequest exactly fills the free arena", v);
    }
    {
        // A stated requirement small enough to be met, to prove the accepted
        // path is not simply "the check never fires".
        auto v = base.bytes;
        Put32(v, OFF(heapRequest), 4096u);
        Recrc(v);
        ExpectAccepted("heapRequest 4096, comfortably met", v);
    }

    // --- exhaustive single-byte flips over the header ----------------------
    //
    // Judged against an INDEPENDENT ORACLE rather than a list of field names.
    //
    // The first version of this sweep classified fields as load-bearing or
    // descriptive and complained whenever a flip in a load-bearing one was
    // accepted. That was wrong, and usefully so: most of what it flagged were
    // flips producing a DIFFERENT BUT STILL VALID module - phase 0 becoming
    // phase 1, an unused exportOffset changing, entryOffset moving to another
    // spot inside the payload. Accepting those is correct.
    //
    // So the property is not "which field changed" but "was the result actually
    // well-formed". Oracle() re-derives that from the mutated bytes using the
    // format rules, with no reference to the loader's code. A flip is a failure
    // only when the loader and the oracle disagree.
    //
    // fileSize and contentCrc32 are skipped: Recrc rewrites both, so a flip in
    // them never reaches the loader here. The dedicated integrity cases above
    // cover them, deliberately without recomputing.
    std::printf("=== every bit of the header, CRC recomputed ===\n");

    auto Oracle = [&](const std::vector<uint8_t>& v) -> bool {
        if (v.size() < kHdr) return false;
        Wxlm::Header h{};
        std::memcpy(&h, v.data(), kHdr);

        if (h.magic != Wxlm::kMagic) return false;
        if (h.endian != static_cast<uint8_t>(Wxlm::kHostEndian)) return false;
        if (h.machine != static_cast<uint16_t>(Wxlm::kHostMachine)) return false;
        if (h.formatVersion == 0 || h.formatVersion > Wxlm::kFormatVersion) return false;
        if (h.abiVersion != WiiXLaunch::Core::kAbiVersion) return false;
        if (h.phase >= static_cast<uint8_t>(Wxlm::Phase::Count)) return false;
        if (h.reserved0 != 0) return false;
        for (int i = 0; i < 4; ++i) if (h.reserved1[i] != 0) return false;
        if (h.declaredHookOffset || h.declaredHookCount) return false;
        // declaredPatch* is IMPLEMENTED as of stage 7, so it is a real section
        // with real bounds rather than a reserved field that must be zero. The
        // oracle follows the FORMAT, not the loader - when the format grows a
        // section, the independent model grows the same section, derived from
        // the same rule every other section obeys.
        if (h.declaredPatchCount != 0 &&
            (uint64_t)h.declaredPatchCount * sizeof(Wxlm::PatchEntry) > 0xFFFFFFFFull) {
            return false;
        }
        if (h.fileSize != v.size()) return false;

        struct Span { uint64_t off, size; };
        const Span spans[] = {
            { h.declaredPatchOffset,
              (uint64_t)h.declaredPatchCount * sizeof(Wxlm::PatchEntry) },
            { h.payloadOffset,  (uint64_t)h.payloadSize },
            { h.relocOffset,    (uint64_t)h.relocCount * 8ull },
            { h.importOffset,   (uint64_t)h.importCount * sizeof(Wxlm::ImportEntry) },
            { h.exportOffset,   (uint64_t)h.exportCount * sizeof(Wxlm::ExportEntry) },
            { h.requiredOffset, (uint64_t)h.requiredCount * sizeof(Wxlm::RequiredSurface) },
            { h.stringOffset,   (uint64_t)h.stringSize },
        };
        const int n = (int)(sizeof(spans) / sizeof(spans[0]));
        for (int i = 0; i < n; ++i) {
            if (spans[i].size == 0) continue;
            if (spans[i].off < kHdr) return false;
            if (spans[i].off + spans[i].size > h.fileSize) return false;
            for (int j = i + 1; j < n; ++j) {
                if (spans[j].size == 0) continue;
                if (spans[i].off < spans[j].off + spans[j].size &&
                    spans[j].off < spans[i].off + spans[i].size) return false;
            }
        }
        if (h.entryOffset >= h.payloadSize) return false;
        if ((uint64_t)h.payloadSize + (uint64_t)h.bssSize > 0xFFFFFFFFull) return false;

        // Names referenced by the tables must lie inside the string blob. The
        // loader checks this; the first oracle did not, and disagreed with it
        // on a flip that emptied stringSize while a required surface still
        // pointed into it.
        for (uint32_t i = 0; i < h.requiredCount; ++i) {
            Wxlm::RequiredSurface rq{};
            std::memcpy(&rq, v.data() + h.requiredOffset + i * sizeof(rq), sizeof(rq));
            if (rq.nameOffset >= h.stringSize) return false;
        }
        for (uint32_t i = 0; i < h.importCount; ++i) {
            Wxlm::ImportEntry ie{};
            std::memcpy(&ie, v.data() + h.importOffset + i * sizeof(ie), sizeof(ie));
            if (ie.surfaceNameOffset >= h.stringSize) return false;
            if (ie.symbolNameOffset >= h.stringSize) return false;
        }
        if (h.initArrayCount != 0 &&
            (uint64_t)h.initArrayOffset + (uint64_t)h.initArrayCount * 4ull > h.payloadSize)
            return false;

        // Relocation sites must land inside the payload, and an import
        // relocation must name an import that exists.
        for (uint32_t i = 0; i < h.relocCount; ++i) {
            uint32_t hdr32, val;
            std::memcpy(&hdr32, v.data() + h.relocOffset + i * 8, 4);
            std::memcpy(&val,   v.data() + h.relocOffset + i * 8 + 4, 4);
            const uint32_t kind = hdr32 >> 24, off = hdr32 & 0x00FFFFFFu;
            if (kind >= (uint32_t)Wxlm::RelocKind::Count) return false;
            // Spelled out here rather than calling Wxlm::RelocWidth, on purpose:
            // an oracle that asks the implementation what it thinks the answer
            // is can only ever agree with it. Two independent statements of the
            // same table is the whole mechanism.
            uint32_t width = 4;
            if (kind == (uint32_t)Wxlm::RelocKind::Addr16Ha ||
                kind == (uint32_t)Wxlm::RelocKind::Addr16Hi ||
                kind == (uint32_t)Wxlm::RelocKind::Addr16Lo) width = 2;
            else if (kind == (uint32_t)Wxlm::RelocKind::Addr64) width = 8;
            else if (kind == (uint32_t)Wxlm::RelocKind::Import) width = sizeof(void*);
            if (off + width > h.payloadSize) return false;
            if (kind == (uint32_t)Wxlm::RelocKind::Import && val >= h.importCount)
                return false;
        }
        return true;
    };

    int flipAgree = 0, flipAccepted = 0;
    for (uint32_t byte = 0; byte < kHdr; ++byte) {
        if (byte >= OFF(fileSize) && byte < OFF(fileSize) + 4) continue;
        if (byte >= OFF(contentCrc32) && byte < OFF(contentCrc32) + 4) continue;

        for (int bit = 0; bit < 8; ++bit) {
            auto v = base.bytes;
            v[byte] ^= static_cast<uint8_t>(1u << bit);
            Recrc(v);

            const bool oracleSaysValid = Oracle(v);

            Loader::ResetForTest();
            arena_backing::Init();
            MemoryReader reader(v);
            const Reject got = Loader::LoadFrom(reader);
            ++g_Cases;

            const bool loaderAccepted = (got == Reject::None);
            if (!arena_backing::ContainedInGrant()) {
                ++g_Failures;
                std::printf("  FAIL  flip %u:%d wrote outside the granted sub-arena\n",
                            byte, bit);
            } else if (got == Reject::NoMemory && oracleSaysValid) {
                // Well-formed but unallocatable. A module asking for 2 GB of
                // bss is structurally valid and still cannot be loaded, so this
                // is a resource answer rather than a disagreement about the
                // file. Counted as agreement.
                ++flipAgree;
                ++g_Rejected;
            } else if (loaderAccepted != oracleSaysValid) {
                ++g_Failures;
                std::printf("  FAIL  flip %u:%d - loader %s, oracle says %s\n",
                            byte, bit,
                            loaderAccepted ? "ACCEPTED" : Wxlm::RejectName(got),
                            oracleSaysValid ? "well-formed" : "malformed");
            } else if (loaderAccepted && !arena_backing::WroteInGrant()) {
                // The liveness half, on the 289 accepted flips too. It used to
                // run only on the handful of explicitly-accepted cases, so the
                // bulk of the suite checked containment alone - which passes
                // trivially when nothing writes at all.
                ++g_Failures;
                std::printf("  FAIL  flip %u:%d loaded without writing inside its grant\n",
                            byte, bit);
            } else {
                ++flipAgree;
                if (loaderAccepted) ++flipAccepted;
                else ++g_Rejected;
            }
        }
    }
    std::printf("  %d flips checked against the oracle, %d agreed (%d of them valid)\n",
                flipAgree + 0, flipAgree, flipAccepted);

    g_Failures += g_AlignmentViolations;
    if (g_AlignmentViolations != 0) {
        std::printf("\n  %d FS alignment violations - the loader asked for reads "
                    "coreinit would refuse\n", g_AlignmentViolations);
    }

    // The numbers FIRST, then the floors that judge them. A disarm message that
    // withholds the counts it is complaining about sends the reader back to run
    // the suite again to find out what they were.
    const int acceptedTotal = g_Accepted + flipAccepted;

    std::printf("\n%d containment checks, %d liveness checks\n",
                g_ContainmentChecks, g_LivenessChecks);
    std::printf("\n%d cases, %d rejected, %d accepted, %d value(s) read back, "
                "%d FAILURES\n",
                g_Cases, g_Rejected, acceptedTotal, g_ValueChecks, g_Failures);

    // FLOORS. A suite that shrinks silently reports success over whatever is
    // left of itself, and a floor on the TOTAL does not constrain the split -
    // see the fourth rule in docs/modules.md.
    if (g_Cases < kExpectedCases) {
        std::printf("LOADER FUZZ DISARMED: %d cases ran, expected at least %d.\n"
                    "Cases were removed, or a block stopped being reached.\n",
                    g_Cases, kExpectedCases);
        return 1;
    }
    if (acceptedTotal < kExpectedAccepted) {
        std::printf("LOADER FUZZ DISARMED: only %d of the expected %d cases were "
                    "ACCEPTED.\nValid modules are being rejected for a reason that is "
                    "not about the modules -\nleftover state between cases looks exactly "
                    "like this.\n", acceptedTotal, kExpectedAccepted);
        return 1;
    }
    if (g_Rejected < kExpectedRejected) {
        std::printf("LOADER FUZZ DISARMED: only %d of the expected %d cases were "
                    "REJECTED.\nMalformed modules are being accepted, or cases "
                    "stopped running.\n", g_Rejected, kExpectedRejected);
        return 1;
    }
    if (g_ValueChecks == 0) {
        std::printf("LOADER FUZZ DISARMED: no relocation VALUE was read back.\n"
                    "Accepting a module says the loader wrote something; only\n"
                    "reading the site says it wrote the right bytes.\n");
        return 1;
    }
    if (g_ContainmentChecks == 0 || g_LivenessChecks == 0) {
        std::printf("LOADER FUZZ DISARMED: containment ran %d times, liveness %d.\n"
                    "Both halves must run - containment alone passes trivially when\n"
                    "nothing writes at all, which is the state a dead hook leaves.\n",
                    g_ContainmentChecks, g_LivenessChecks);
        return 1;
    }

    std::printf("%s\n", g_Failures == 0 ? "LOADER FUZZ PASSED" : "LOADER FUZZ FAILED");
    return g_Failures != 0;
}
