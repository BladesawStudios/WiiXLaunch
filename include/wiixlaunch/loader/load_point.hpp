#pragma once

// WiiXLaunch::LoadPoint - is the title's filesystem usable yet? A mod's
// callback address doesn't exist until its bytes are read off the
// filesystem, so this asks how early the host can call FSAddClient and
// actually get a file back.
//
// A single probe returning NOT_FOUND is ambiguous (filesystem works and
// the file is absent, or /vol/content isn't mounted yet), so three
// questions are asked separately:
//
//   1. STOCK  - a file shipped in the title. Positive control: proves
//               /vol/content is mounted and readable, verified by content.
//   2. PACK   - a file injected through the graphic pack's content/
//               overlay. Proves Cemu's overlay is live at this boot point.
//   3. DIR    - the mods directory itself, via FSOpenDir, distinguishing
//               "not there" from "there and empty."
//
// Opening is not reading: FSOpenFile succeeding doesn't prove FSReadFile
// works at this timing. Every call is announced before it's made, since
// calling into coreinit's FS before the OS has set it up may take the
// process down rather than return an error, and a crash with no output
// would otherwise be indistinguishable from a silent no-op.
//
// Uses its own client and command block, not wiixlaunch/fs.hpp's: a failed
// early FSAddClient must not leave the host's real client half-registered.
// Paths and the load-point address are supplied by the caller; nothing
// here is game-specific.

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
// redirected to WiiXLaunch_LoadPointStub. Build-time nomination:
// scripts/deploy.py reads this global out of the ELF and emits
// `.origin = <addr> / b wiixlaunch_loadpoint_stub` into the host pack, so
// nothing patches game code at runtime. A build that never uses this macro
// has no load point, and deploy.py says so rather than silently doing
// nothing. The stub must be named WiiXLaunch_LoadPointStub - deploy.py
// looks that symbol up by name.
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
    NotRun,            // the probe ended before this question was asked
};

inline const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::ShimsMissing:   return "SHIMS-MISSING";
        case Verdict::FsAbsent:       return "FS-ABSENT";
        case Verdict::NotFound:       return "NOT-FOUND";
        case Verdict::NotRun:         return "NOT-RUN";
        case Verdict::OpenedNotRead:  return "OPENED-NOT-READ";
        case Verdict::Verified:       return "VERIFIED";
        case Verdict::Mismatch:       return "MISMATCH";
    }
    return "?";
}

// A file that ships in the title, used as the positive control.
// Pack/Bootup.pack is a plain (unYaz0'd) SARC archive, first four bytes
// "SARC"; BotW's GUI asset loader already streams it successfully. These
// are defaults; a caller on another title passes its own.
constexpr const char* kStockPath      = "/vol/content/Pack/Bootup.pack";
constexpr const char* kStockMagic     = "SARC";
// Shipped at content/WiiXLaunch/mods/ (a case-insensitive host filesystem
// won't let that coexist with a lowercase sibling). Wii U's filesystem is
// case-sensitive and whether Cemu's overlay preserves that isn't assumed,
// so both spellings are probed.
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

// coreinit's FSStat is 0x64 bytes; FSGetStatFile writes all of them.
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

// Opens, reads, and optionally verifies a magic. `fingerprint` additionally
// logs enough to identify which file was read (stat size plus SARC/SFAT
// header fields a repack almost always changes), since a magic check alone
// can't tell a stock Bootup.pack from a graphic-pack replacement.
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

    // FOUR SELF-REPORTED VERDICTS WITH NOTHING ASSERTING ALL FOUR APPEARED is
    // the fourth rule (docs/framework/modules.md) one level up: a probe that ends early
    // used to print a reason and simply stop, and a log MISSING three lines
    // reads like a log that passed. So the questions start at NOT-RUN, every
    // early exit falls through to a single summary, and the summary counts.
    // A "2/4 probes ran" is a liveness assertion a human cannot skim past.
    Verdict stock  = Verdict::NotRun;
    Verdict pack   = Verdict::NotRun;
    Verdict packLC = Verdict::NotRun;
    Verdict dir    = Verdict::NotRun;

    // The body returns instead of falling off the end; the summary below always
    // runs. This is a lambda purely so `return` keeps meaning "stop asking".
    [&] {

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

    // Announced before calling: if FS isn't up, this call is most likely to
    // take the process down, and a missing follow-up line is the answer.
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
    stock = ProbeFile(where, "STOCK", stockPath, stockMagic, /*fingerprint=*/true);

    // 2. The graphic-pack content/ overlay. Only meaningful if STOCK passed:
    //    NOT-FOUND here with STOCK verified means the overlay is not live yet;
    //    NOT-FOUND with STOCK also NOT-FOUND means nothing is mounted.
    pack = ProbeFile(where, "PACK", packPath, nullptr);

    // Same file, lowercase spelling. Tests whether Cemu's overlay lookup is
    // case-sensitive here (a property of this host, not a real Wii U).
    packLC = ProbeFile(where, "PACK-LC", packPathLC, nullptr);

    // 3. The directory the loader will enumerate.
    dir = ProbeDir(where, "DIR", modsDir);

    if (delClient) {
        const int32_t delStatus = delClient(g_ProbeClient, 0xFFFFFFFF);
        WIIXL_LOG("[LP:%s] FSDelClient -> %d", where, delStatus);
        g_ClientUp = false;
    }

    }();

    const Verdict all[4] = { stock, pack, packLC, dir };
    uint32_t ran = 0, verified = 0;
    for (const Verdict v : all) {
        if (v != Verdict::NotRun) ++ran;
        if (v == Verdict::Verified) ++verified;
    }

    WIIXL_LOG("[LP:%s] ===== SUMMARY  stock=%s  pack=%s  pack-lc=%s  dir=%s  |  "
              "%u/4 probes ran, %u/4 VERIFIED =====",
              where, VerdictName(stock), VerdictName(pack),
              VerdictName(packLC), VerdictName(dir), ran, verified);
    if (ran != 4) {
        WIIXL_LOG("[LP:%s] ===== the probe ENDED EARLY - %u of 4 questions were never "
                  "asked, so this run says nothing about them =====", where, 4u - ran);
    }
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

// Nomination is build-time, deliberately not a base-owned address: the load
// point is a per-game, per-platform fact base can't know. A project
// declares the address with WIIXL_DECLARE_LOAD_POINT and a stub named
// WiiXLaunch_LoadPointStub; deploy.py reads both and emits the `.origin`.
// A build declaring neither gets no load point and a log line saying so.
//
// This probe reported FS-USABLE at Cemu's entry hook itself, before the
// game calls FSInit - a property of the emulator (it HLEs coreinit, so the
// filesystem is live from process start), not of the game or platform.
// Aroma runs against real IOSU and Switch has its own romfs mount timing;
// neither has been probed, so nomination stays the rule rather than an
// exception. See docs/framework/loader.md for what's initialized at each
// phase.

} // namespace WiiXLaunch::LoadPoint
