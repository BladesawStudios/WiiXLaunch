#pragma once

// WiiXLaunch::LoadPoint - STAGE 1 PROBE. Is the title's filesystem usable yet?
//
// The module loader reads .wxlm blobs off the filesystem, so a mod's callback
// address does not exist until its bytes are in memory. That makes one question
// load-bearing for everything downstream: how early can the host call
// FSAddClient and actually get a file back?
//
// THREE PATHS, THREE SEPARATE ANSWERS. A single probe returning NOT_FOUND is
// ambiguous in a way that matters: it could mean the filesystem works and the
// file is absent, or that /vol/content is not mounted yet. Those have opposite
// consequences, so the probe asks three questions that can be answered
// independently at each site:
//
//   1. STOCK  - a file that ships in the title. Positive control: proves
//               /vol/content is mounted AND readable. Verified by content, not
//               just by opening.
//   2. PACK   - a file injected through the graphic pack's content/ overlay.
//               Proves Cemu's overlay is live at this point in boot, which the
//               whole mod-distribution design rests on.
//   3. DIR    - the mods directory itself, via FSOpenDir. Distinguishes "the
//               directory is not there" from "the directory is there and
//               empty", which FSOpenFile cannot do.
//
// OPENING IS NOT READING. FSOpenFile succeeding does not prove FSReadFile works
// at this timing, and the read is where a synchronous FS call on the boot
// thread would actually block. The stock probe reads bytes and checks them
// against a known magic.
//
// WRITE-AHEAD LOGGING. Every call is announced BEFORE it is made. Calling into
// coreinit's FS before the OS has set it up may take the process down rather
// than return an error, and a crash with no output is indistinguishable from
// the silent no-op this probe exists to rule out. Announcing first makes the
// last line in the log name the call that died.
//
// The probe uses its OWN client and command block, not the ones in
// wiixlaunch/fs.hpp: a failed early FSAddClient must not leave the host's real
// client half-registered for the loader to inherit.
//
// NOTHING HERE IS GAME-SPECIFIC. Paths and the load-point address are supplied
// by the caller - see the nomination note at the bottom.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_fs.hpp>
#elif WIIXL_WIIU
#include <coreinit/filesystem.h>
#endif

// Declares this build's load point: the game address whose instruction is
// redirected to WiiXLaunch_LoadPointStub.
//
// This is BUILD-TIME nomination. scripts/deploy.py reads the resulting global
// out of the ELF and emits `.origin = <addr> / b wiixlaunch_loadpoint_stub`
// into the host pack, exactly like the entry hook. Nothing patches game code at
// runtime, so there is no question of writing into a function Cemu may already
// have recompiled, and the rule that only the host pack writes into game memory
// holds.
//
// A build that never uses this macro has no load point. deploy.py emits no
// .origin and says so, which is the correct behaviour for a host with no game
// module rather than a host that boots and silently does nothing.
//
// The stub MUST be named WiiXLaunch_LoadPointStub - deploy.py looks that symbol
// up by name to place the branch target label.
// `inline` matches how every other deploy.py-patched global is declared
// (see g_CemuRelocTableOffset in wiixl_cemu_backend.hpp). A plain
// definition lands in flat .data while those land in a COMDAT .data, and
// GCC rejects the mix as a section type conflict. `used` is required for
// the usual reason: nothing in C++ reads this - deploy.py does.
#define WIIXL_DECLARE_LOAD_POINT(addr) \
    extern "C" { __attribute__((section(".data"), used)) \
        inline uint32_t g_WiiXLaunchLoadPointAddr = (addr); }

namespace WiiXLaunch::LoadPoint {

// What one path probe learned. Distinguishing these is the entire point.
enum class Verdict : uint32_t {
    ShimsMissing = 0,  // build/deploy problem, not a timing one
    FsAbsent,          // FSAddClient refused - FS is not up here at all
    NotFound,          // FS answered; this path does not exist
    OpenedNotRead,     // opened, but the read failed or returned nothing
    Verified,          // read back, and the content is what it should be
    Mismatch,          // read back, but the content is wrong
};

inline const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::ShimsMissing:   return "SHIMS-MISSING";
        case Verdict::FsAbsent:       return "FS-ABSENT";
        case Verdict::NotFound:       return "NOT-FOUND";
        case Verdict::OpenedNotRead:  return "OPENED-NOT-READ";
        case Verdict::Verified:       return "VERIFIED";
        case Verdict::Mismatch:       return "MISMATCH";
    }
    return "?";
}

// A file that ships in the title, used as the positive control. Pack/Bootup.pack
// is a plain (unYaz0'd) SARC archive, so its first four bytes are "SARC" - a
// magic worth checking rather than trusting a byte count. The BotW module's GUI
// asset loader already streams this same file successfully, which is why it is
// the one picked: it is known to exist and known to be readable.
//
// These are defaults, not knowledge base is entitled to - a caller on another
// title passes its own.
constexpr const char* kStockPath      = "/vol/content/Pack/Bootup.pack";
constexpr const char* kStockMagic     = "SARC";
// The pack ships this at content/WiiXLaunch/mods/, because a case-insensitive
// host filesystem will not let that coexist with a lower-case sibling (see
// deploy.py). Wii U's own filesystem IS case-sensitive, and whether Cemu's
// content overlay preserves that is not something to assume - so both spellings
// are probed and the log says which answered.
constexpr const char* kPackFilePath   = "/vol/content/WiiXLaunch/mods/probe.bin";
constexpr const char* kPackFilePathLC = "/vol/content/wiixlaunch/mods/probe.bin";
constexpr const char* kModsDirPath    = "/vol/content/WiiXLaunch/mods";

#if WIIXL_CEMU

namespace impl {

// Separate from WiiXLaunch::FS::impl on purpose - see the header comment.
alignas(32) inline uint8_t g_ProbeClient[0x1700];
alignas(32) inline uint8_t g_ProbeCmdBlock[0xA80];

// coreinit's FSReadFile family requires a 64-byte aligned buffer.
alignas(64) inline uint8_t g_ProbeBuf[128];

// coreinit's FSStat is 0x64 bytes and FSGetStatFile writes all of them. This
// was 96 bytes once: it wrote four bytes onto the adjacent stack slot, zeroed a
// read length, and every read then "succeeded" having transferred nothing.
struct FsStatBuf { uint32_t flags, mode, owner, group, size, rest[20]; };
static_assert(sizeof(FsStatBuf) == 0x64, "must match coreinit FSStat exactly");

// FSDirectoryEntry is an FSStat followed by a 256-byte name.
struct FsDirEntry { FsStatBuf stat; char name[256]; };
static_assert(sizeof(FsDirEntry) == 0x164, "must match coreinit FSDirectoryEntry");

alignas(64) inline FsDirEntry g_DirEntry;

using FnFSAddClient    = int32_t (*)(void*, uint32_t);
using FnFSDelClient    = int32_t (*)(void*, uint32_t);
using FnFSInitCmdBlock = void    (*)(void*);
using FnFSOpenFile     = int32_t (*)(void*, void*, const char*, const char*, uint32_t*, uint32_t);
using FnFSGetStatFile  = int32_t (*)(void*, void*, uint32_t, void*, uint32_t);
using FnFSReadFile     = int32_t (*)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using FnFSCloseFile    = int32_t (*)(void*, void*, uint32_t, uint32_t);
using FnFSOpenDir      = int32_t (*)(void*, void*, const char*, uint32_t*, uint32_t);
using FnFSReadDir      = int32_t (*)(void*, void*, uint32_t, void*, uint32_t);
using FnFSCloseDir     = int32_t (*)(void*, void*, uint32_t, uint32_t);
using FnFSReadFileWithPos = int32_t (*)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

inline bool g_ClientUp = false;

// Opens, reads, and optionally verifies a magic.
//
// `fingerprint` additionally logs enough to identify WHICH file was read, not
// merely that something was. That matters for the positive control: BotW
// graphic packs commonly overlay /vol/content, and Pack/Bootup.pack is one of
// the more frequently replaced files. A replacement is still a valid SARC, so
// the magic check alone cannot separate a stock file from an injected one - and
// if it cannot, STOCK and PACK collapse back into the single signal that
// splitting them was meant to avoid.
//
// The fingerprint is the stat size plus the SARC/SFAT header fields a repack
// almost always changes (file size, data offset, node count), plus a positioned
// read deeper into the file. Compare across boots to tell which file you got.
inline Verdict ProbeFile(const char* where, const char* label,
                         const char* path, const char* expectMagic,
                         bool fingerprint = false) {
    auto openFile  = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto getStat   = Backend::ResolveCemuFs<FnFSGetStatFile>(Backend::CemuFsImport::FSGetStatFile);
    auto readFile  = Backend::ResolveCemuFs<FnFSReadFile>(Backend::CemuFsImport::FSReadFile);
    auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);
    auto readAt    = Backend::ResolveCemuFs<FnFSReadFileWithPos>(Backend::CemuFsImport::FSReadFileWithPos);
    if (!openFile || !readFile || !closeFile) return Verdict::ShimsMissing;

    uint32_t handle = 0;
    WIIXL_LOG("[LP:%s][%s] calling FSOpenFile('%s')...", where, label, path);
    const int32_t openStatus = openFile(g_ProbeClient, g_ProbeCmdBlock, path, "r", &handle, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s][%s] FSOpenFile -> %d (handle=%u)", where, label, openStatus, handle);
    if (openStatus != 0) {
        WIIXL_LOG("[LP:%s][%s] verdict=%s (open status %d; -6 = NOT_FOUND)",
                  where, label, VerdictName(Verdict::NotFound), openStatus);
        return Verdict::NotFound;
    }

    FsStatBuf stat{};
    const int32_t statStatus = getStat
        ? getStat(g_ProbeClient, g_ProbeCmdBlock, handle, &stat, 0xFFFFFFFF) : -1;
    WIIXL_LOG("[LP:%s][%s] FSGetStatFile -> %d  SIZE=%u (0x%X)", where, label, statStatus,
              static_cast<unsigned>(stat.size), static_cast<unsigned>(stat.size));

    // Opening is not reading. This is the call that would actually block if a
    // synchronous FS read were unsafe on this thread at this time.
    const uint32_t toRead = 64;
    for (uint32_t i = 0; i < sizeof(g_ProbeBuf); ++i) g_ProbeBuf[i] = 0;
    WIIXL_LOG("[LP:%s][%s] calling FSReadFile(%u)...", where, label, toRead);
    const int32_t readBytes = readFile(g_ProbeClient, g_ProbeCmdBlock, g_ProbeBuf,
                                       1, toRead, handle, 0, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s][%s] FSReadFile -> %d", where, label, readBytes);

    if (readBytes <= 0) {
        closeFile(g_ProbeClient, g_ProbeCmdBlock, handle, 0xFFFFFFFF);
        WIIXL_LOG("[LP:%s][%s] verdict=%s - opened but read returned %d. FS accepts opens "
                  "but not reads at this point.",
                  where, label, VerdictName(Verdict::OpenedNotRead), readBytes);
        return Verdict::OpenedNotRead;
    }

    WIIXL_LOG("[LP:%s][%s] first bytes %02X %02X %02X %02X", where, label,
              g_ProbeBuf[0], g_ProbeBuf[1], g_ProbeBuf[2], g_ProbeBuf[3]);

    if (fingerprint) {
        // Two 16-byte lines rather than one 32-byte line: WIIXL_LOG caps each
        // call at kMaxLogTextLen (200 chars).
        WIIXL_LOG("[LP:%s][%s] FP 00-0F: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X %02X %02X", where, label,
                  g_ProbeBuf[0], g_ProbeBuf[1], g_ProbeBuf[2], g_ProbeBuf[3],
                  g_ProbeBuf[4], g_ProbeBuf[5], g_ProbeBuf[6], g_ProbeBuf[7],
                  g_ProbeBuf[8], g_ProbeBuf[9], g_ProbeBuf[10], g_ProbeBuf[11],
                  g_ProbeBuf[12], g_ProbeBuf[13], g_ProbeBuf[14], g_ProbeBuf[15]);
        WIIXL_LOG("[LP:%s][%s] FP 10-1F: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X %02X %02X", where, label,
                  g_ProbeBuf[16], g_ProbeBuf[17], g_ProbeBuf[18], g_ProbeBuf[19],
                  g_ProbeBuf[20], g_ProbeBuf[21], g_ProbeBuf[22], g_ProbeBuf[23],
                  g_ProbeBuf[24], g_ProbeBuf[25], g_ProbeBuf[26], g_ProbeBuf[27],
                  g_ProbeBuf[28], g_ProbeBuf[29], g_ProbeBuf[30], g_ProbeBuf[31]);

        // SARC is big-endian. These are the fields a repack changes.
        const uint8_t* q = g_ProbeBuf;
        auto be32 = [](const uint8_t* r) -> uint32_t {
            return (static_cast<uint32_t>(r[0]) << 24) | (static_cast<uint32_t>(r[1]) << 16)
                 | (static_cast<uint32_t>(r[2]) << 8)  |  static_cast<uint32_t>(r[3]);
        };
        auto be16 = [](const uint8_t* r) -> uint32_t {
            return (static_cast<uint32_t>(r[0]) << 8) | static_cast<uint32_t>(r[1]);
        };
        const bool isSarc = q[0] == 0x53 && q[1] == 0x41 && q[2] == 0x52 && q[3] == 0x43;
        if (isSarc) {
            WIIXL_LOG("[LP:%s][%s] FP SARC fileSize=%u dataOffset=0x%X version=0x%X",
                      where, label, be32(q + 0x08), be32(q + 0x0C), be16(q + 0x10));
            const bool isSfat = q[0x14] == 0x53 && q[0x15] == 0x46
                             && q[0x16] == 0x41 && q[0x17] == 0x54;
            if (isSfat) {
                WIIXL_LOG("[LP:%s][%s] FP SFAT nodeCount=%u hashMultiplier=0x%X",
                          where, label, be16(q + 0x1A), be32(q + 0x1C));
            }
        }

        // A positioned read deeper in, which also exercises FSReadFileWithPos -
        // the call FS::File::ReadAt and the loader both depend on.
        if (readAt) {
            alignas(64) static uint8_t deep[64];
            for (uint32_t i = 0; i < sizeof(deep); ++i) deep[i] = 0;
            const uint32_t deepOff = 0x1000;
            WIIXL_LOG("[LP:%s][%s] calling FSReadFileWithPos(off=0x%X, 64)...",
                      where, label, deepOff);
            const int32_t got = readAt(g_ProbeClient, g_ProbeCmdBlock, deep, 1, 64,
                                       deepOff, handle, 0, 0xFFFFFFFF);
            WIIXL_LOG("[LP:%s][%s] FSReadFileWithPos -> %d", where, label, got);
            if (got > 0) {
                WIIXL_LOG("[LP:%s][%s] FP @0x%X: %02X %02X %02X %02X %02X %02X %02X %02X",
                          where, label, deepOff,
                          deep[0], deep[1], deep[2], deep[3],
                          deep[4], deep[5], deep[6], deep[7]);
            }
        } else {
            WIIXL_LOG("[LP:%s][%s] FSReadFileWithPos shim unresolved - no deep sample",
                      where, label);
        }
    }

    const int32_t closeStatus = closeFile(g_ProbeClient, g_ProbeCmdBlock, handle, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s][%s] FSCloseFile -> %d", where, label, closeStatus);

    if (expectMagic) {
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
            if (g_ProbeBuf[i] != static_cast<uint8_t>(expectMagic[i])) { ok = false; break; }
        }
        if (!ok) {
            WIIXL_LOG("[LP:%s][%s] verdict=%s - expected magic '%s'",
                      where, label, VerdictName(Verdict::Mismatch), expectMagic);
            return Verdict::Mismatch;
        }
    }

    WIIXL_LOG("[LP:%s][%s] verdict=%s (%d bytes read). Magic alone does not prove this is "
              "the stock file - compare the FP lines above.",
              where, label, VerdictName(Verdict::Verified), readBytes);
    return Verdict::Verified;
}

inline Verdict ProbeDir(const char* where, const char* label, const char* path) {
    auto openDir  = Backend::ResolveCemuFs<FnFSOpenDir>(Backend::CemuFsImport::FSOpenDir);
    auto readDir  = Backend::ResolveCemuFs<FnFSReadDir>(Backend::CemuFsImport::FSReadDir);
    auto closeDir = Backend::ResolveCemuFs<FnFSCloseDir>(Backend::CemuFsImport::FSCloseDir);
    if (!openDir || !readDir || !closeDir) {
        WIIXL_LOG("[LP:%s][%s] verdict=%s - directory shims not resolved",
                  where, label, VerdictName(Verdict::ShimsMissing));
        return Verdict::ShimsMissing;
    }

    uint32_t dh = 0;
    WIIXL_LOG("[LP:%s][%s] calling FSOpenDir('%s')...", where, label, path);
    const int32_t openStatus = openDir(g_ProbeClient, g_ProbeCmdBlock, path, &dh, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s][%s] FSOpenDir -> %d (handle=%u)", where, label, openStatus, dh);
    if (openStatus != 0) {
        WIIXL_LOG("[LP:%s][%s] verdict=%s - the directory itself is not there",
                  where, label, VerdictName(Verdict::NotFound));
        return Verdict::NotFound;
    }

    // Listing it separates "exists and empty" from "exists with entries", which
    // is what the loader will actually need.
    int entries = 0;
    for (int i = 0; i < 16; ++i) {
        const int32_t rd = readDir(g_ProbeClient, g_ProbeCmdBlock, dh, &g_DirEntry, 0xFFFFFFFF);
        if (rd != 0) {
            WIIXL_LOG("[LP:%s][%s] FSReadDir -> %d (end of listing)", where, label, rd);
            break;
        }
        g_DirEntry.name[sizeof(g_DirEntry.name) - 1] = '\0';
        WIIXL_LOG("[LP:%s][%s] entry[%d] flags=0x%X size=%u name='%s'", where, label, i,
                  static_cast<unsigned>(g_DirEntry.stat.flags),
                  static_cast<unsigned>(g_DirEntry.stat.size), g_DirEntry.name);
        ++entries;
    }

    const int32_t closeStatus = closeDir(g_ProbeClient, g_ProbeCmdBlock, dh, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s][%s] FSCloseDir -> %d", where, label, closeStatus);
    WIIXL_LOG("[LP:%s][%s] verdict=%s (directory exists, %d entries listed)",
              where, label, VerdictName(Verdict::Verified), entries);
    return Verdict::Verified;
}

} // namespace impl

// Runs all three path probes at one site. `where` names the site and appears on
// every line: the value of this probe is comparing sites within a single boot.
inline void Probe(const char* where,
                  const char* stockPath = kStockPath,
                  const char* stockMagic = kStockMagic,
                  const char* packPath = kPackFilePath,
                  const char* modsDir = kModsDirPath,
                  const char* packPathLC = kPackFilePathLC) {
    WIIXL_LOG("[LP:%s] ===== probe start =====", where);

    if (!WiiXLaunch::Backend::CemuFsAvailable()) {
        WIIXL_LOG("[LP:%s] verdict=%s base=%p offset=%u - deploy.py has not patched the "
                  "shim table. Build problem, NOT a timing one.",
                  where, VerdictName(Verdict::ShimsMissing),
                  reinterpret_cast<void*>(WiiXLaunch::Backend::g_CodeCaveBase),
                  static_cast<unsigned>(::g_CemuFsShimTableOffset));
        return;
    }

    using namespace impl;
    auto addClient    = Backend::ResolveCemuFs<FnFSAddClient>(Backend::CemuFsImport::FSAddClient);
    auto delClient    = Backend::ResolveCemuFs<FnFSDelClient>(Backend::CemuFsImport::FSDelClient);
    auto initCmdBlock = Backend::ResolveCemuFs<FnFSInitCmdBlock>(Backend::CemuFsImport::FSInitCmdBlock);
    if (!addClient || !initCmdBlock) {
        WIIXL_LOG("[LP:%s] verdict=%s - table present but a slot resolved null",
                  where, VerdictName(Verdict::ShimsMissing));
        return;
    }

    // Announce before calling. If FS is not up, this is the call most likely to
    // take the process down, and this line being last is itself the answer.
    WIIXL_LOG("[LP:%s] calling FSAddClient...", where);
    const int32_t addStatus = addClient(g_ProbeClient, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s] FSAddClient -> %d", where, addStatus);
    if (addStatus != 0) {
        WIIXL_LOG("[LP:%s] verdict=%s (FSAddClient refused) - FS is not up at this site "
                  "at all; the other three questions cannot be asked here.",
                  where, VerdictName(Verdict::FsAbsent));
        return;
    }
    g_ClientUp = true;

    WIIXL_LOG("[LP:%s] calling FSInitCmdBlock...", where);
    initCmdBlock(g_ProbeCmdBlock);

    // 1. Positive control. If this is not VERIFIED, nothing else here means
    //    anything - /vol/content is not mounted or not readable yet.
    const Verdict stock = ProbeFile(where, "STOCK", stockPath, stockMagic, /*fingerprint=*/true);

    // 2. The graphic-pack content/ overlay. Only meaningful if STOCK passed:
    //    NOT-FOUND here with STOCK verified means the overlay is not live yet;
    //    NOT-FOUND with STOCK also NOT-FOUND means nothing is mounted.
    const Verdict pack = ProbeFile(where, "PACK", packPath, nullptr);

    // Same file, lower-case spelling. If PACK answers and this does not, Cemu's
    // overlay lookup is case-sensitive and stage 8's layout must match the
    // shipped capitalisation exactly. If both answer it is case-insensitive
    // here - a property of this host, not of a real Wii U.
    const Verdict packLC = ProbeFile(where, "PACK-LC", packPathLC, nullptr);

    // 3. The directory the loader will enumerate.
    const Verdict dir = ProbeDir(where, "DIR", modsDir);

    if (delClient) {
        const int32_t delStatus = delClient(g_ProbeClient, 0xFFFFFFFF);
        WIIXL_LOG("[LP:%s] FSDelClient -> %d", where, delStatus);
        g_ClientUp = false;
    }

    WIIXL_LOG("[LP:%s] ===== SUMMARY  stock=%s  pack=%s  pack-lc=%s  dir=%s =====",
              where, VerdictName(stock), VerdictName(pack),
              VerdictName(packLC), VerdictName(dir));
}

#elif WIIXL_WIIU

inline void Probe(const char* where,
                  const char* stockPath = "/vol/content/Pack/Bootup.pack",
                  const char* stockMagic = "SARC",
                  const char* packPath = "fs:/vol/external01/wiixlaunch/mods/probe.bin",
                  const char* modsDir = "fs:/vol/external01/wiixlaunch/mods") {
    WIIXL_LOG("[LP:%s] ===== probe start =====", where);

    alignas(32) static FSClient client;
    alignas(32) static FSCmdBlock cmdBlock;
    alignas(64) static uint8_t buf[128];

    WIIXL_LOG("[LP:%s] calling FSAddClient...", where);
    const FSStatus addStatus = FSAddClient(&client, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s] FSAddClient -> %d", where, static_cast<int>(addStatus));
    if (addStatus != FS_STATUS_OK) {
        WIIXL_LOG("[LP:%s] verdict=%s", where, VerdictName(Verdict::FsAbsent));
        return;
    }
    FSInitCmdBlock(&cmdBlock);

    auto probeFile = [&](const char* label, const char* path, const char* magic) -> Verdict {
        FSFileHandle handle = 0;
        WIIXL_LOG("[LP:%s][%s] calling FSOpenFile('%s')...", where, label, path);
        const FSStatus openStatus = FSOpenFile(&client, &cmdBlock, path, "r", &handle, FS_ERROR_FLAG_ALL);
        WIIXL_LOG("[LP:%s][%s] FSOpenFile -> %d", where, label, static_cast<int>(openStatus));
        if (openStatus != FS_STATUS_OK) {
            WIIXL_LOG("[LP:%s][%s] verdict=%s", where, label, VerdictName(Verdict::NotFound));
            return Verdict::NotFound;
        }
        for (uint32_t i = 0; i < sizeof(buf); ++i) buf[i] = 0;
        WIIXL_LOG("[LP:%s][%s] calling FSReadFile(64)...", where, label);
        const int32_t readBytes = FSReadFile(&client, &cmdBlock, buf, 1, 64, handle, 0, FS_ERROR_FLAG_ALL);
        WIIXL_LOG("[LP:%s][%s] FSReadFile -> %d", where, label, readBytes);
        FSCloseFile(&client, &cmdBlock, handle, FS_ERROR_FLAG_ALL);
        if (readBytes <= 0) {
            WIIXL_LOG("[LP:%s][%s] verdict=%s", where, label, VerdictName(Verdict::OpenedNotRead));
            return Verdict::OpenedNotRead;
        }
        if (magic) {
            for (int i = 0; i < 4; ++i) {
                if (buf[i] != static_cast<uint8_t>(magic[i])) {
                    WIIXL_LOG("[LP:%s][%s] verdict=%s - expected '%s'", where, label,
                              VerdictName(Verdict::Mismatch), magic);
                    return Verdict::Mismatch;
                }
            }
        }
        WIIXL_LOG("[LP:%s][%s] verdict=%s (%d bytes)", where, label,
                  VerdictName(Verdict::Verified), readBytes);
        return Verdict::Verified;
    };

    const Verdict stock = probeFile("STOCK", stockPath, stockMagic);
    const Verdict pack  = probeFile("PACK", packPath, nullptr);

    Verdict dir = Verdict::NotFound;
    FSDirectoryHandle dh = 0;
    WIIXL_LOG("[LP:%s][DIR] calling FSOpenDir('%s')...", where, modsDir);
    const FSStatus dirStatus = FSOpenDir(&client, &cmdBlock, modsDir, &dh, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s][DIR] FSOpenDir -> %d", where, static_cast<int>(dirStatus));
    if (dirStatus == FS_STATUS_OK) {
        FSDirectoryEntry entry{};
        int entries = 0;
        while (entries < 16 &&
               FSReadDir(&client, &cmdBlock, dh, &entry, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            WIIXL_LOG("[LP:%s][DIR] entry[%d] name='%s'", where, entries, entry.name);
            ++entries;
        }
        FSCloseDir(&client, &cmdBlock, dh, FS_ERROR_FLAG_ALL);
        WIIXL_LOG("[LP:%s][DIR] verdict=%s (%d entries)", where,
                  VerdictName(Verdict::Verified), entries);
        dir = Verdict::Verified;
    } else {
        WIIXL_LOG("[LP:%s][DIR] verdict=%s", where, VerdictName(Verdict::NotFound));
    }

    FSDelClient(&client, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s] ===== SUMMARY  stock=%s  pack=%s  dir=%s =====",
              where, VerdictName(stock), VerdictName(pack), VerdictName(dir));
}

#else

// Switch has no coreinit FS. The loader will read from mount_path through the
// Switch backend instead. This probe has nothing to say about that and says so,
// rather than reporting a pass that would mean nothing.
inline void Probe(const char* where,
                  const char* = nullptr, const char* = nullptr,
                  const char* = nullptr, const char* = nullptr) {
    WIIXL_LOG("[LP:%s] probe skipped: no coreinit FS on this target", where);
}

#endif

// ---------------------------------------------------------------------------
// NOMINATION is build-time, and deliberately not a base-owned address.
//
// The load point is a PER-GAME, PER-PLATFORM fact - a different title needs a
// different early post-FS function, and base has no way to know it. So base
// owns Probe(), the WIIXL_DECLARE_LOAD_POINT macro and (from stage 4) the
// loader; it never owns the address.
//
// A project declares the address with WIIXL_DECLARE_LOAD_POINT and provides a
// stub named WiiXLaunch_LoadPointStub. deploy.py reads both out of the ELF and
// emits the `.origin` into the host pack. A build that declares neither gets no
// load point and a log line saying so.
//
// WHAT THE CEMU MEASUREMENT DID AND DID NOT SETTLE. This probe reported
// FS-USABLE at the Cemu entry hook itself - FSAddClient, FSOpenFile, FSReadFile,
// FSReadFileWithPos and FSOpenDir all succeed there, against stock content and
// pack-injected content alike, before the game has called FSInit. That is
// because Cemu HLEs coreinit and the filesystem is live from process start, so
// the game's FSInit/FSAddClient pair concerns the game's client rather than the
// subsystem.
//
// That is a property of the EMULATOR, not of the game or the platform. Aroma
// runs against real IOSU; Switch has its own romfs mount timing. Neither has
// been probed. So nomination stays the RULE, not an exception for awkward
// titles, and BotW keeps nominating its load point on Cemu even though the
// entry hook would do - it is the only mechanism validated for the case where
// FS is genuinely not ready early, which is exactly what the other two
// platforms may turn out to be.
//
// See docs/loader.md for what is and is not initialised at each phase.
// ---------------------------------------------------------------------------

} // namespace WiiXLaunch::LoadPoint
