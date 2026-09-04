#pragma once

// WiiXLaunch::LoadPoint - STAGE 1 PROBE. Is the title's filesystem usable yet?
//
// The module loader reads .wxlm blobs off the filesystem, so a mod's callback
// address does not exist until its bytes are in memory. That makes one question
// load-bearing for everything downstream: how early can the host call
// FSAddClient and actually get a file back? This header answers it by
// measurement rather than assumption, and logs every raw status code on the way.
//
// WRITE-AHEAD LOGGING IS THE POINT. Every call is announced BEFORE it is made,
// not just reported after. Calling into coreinit's FS before the OS has set it
// up may take the process down rather than return an error, and a crash with no
// output is indistinguishable from the silent no-op this probe exists to rule
// out. Announcing first turns a hard crash into a log naming the call that died.
//
// The probe deliberately uses its OWN client and command block, not the ones in
// wiixlaunch/fs.hpp. A failed early FSAddClient must not leave the host's real
// client half-registered for the loader to inherit later.
//
// NOTHING HERE IS GAME-SPECIFIC. The probe runs wherever it is called from and
// names that site in its output; choosing the site is the caller's business.
// That split is deliberate - see the note on nomination at the bottom.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_fs.hpp>
#elif WIIXL_WIIU
#include <coreinit/filesystem.h>
#endif

namespace WiiXLaunch::LoadPoint {

// Where the loader will read modules from, per platform. Only the Cemu path is
// exercised by this probe; the others are written down here so the parity story
// lives in one place instead of being rediscovered each stage.
//
//   Cemu    /vol/content/wiixlaunch/mods/   (graphic pack content overlay)
//   Wii U   sd:/wiixlaunch/mods/            (from ON_APPLICATION_START)
//   Switch  <mount_path>/wiixlaunch/mods/   (/mnt/sdcard, see wiixlaunch.json)
constexpr const char* kCemuProbePath  = "/vol/content/wiixlaunch/mods/probe.bin";
constexpr const char* kWiiUProbePath  = "fs:/vol/external01/wiixlaunch/mods/probe.bin";

// What a probe learned at one site. Distinguishing "FS answered, no such file"
// from "FS is not there" is the whole reason this is not a bool.
enum class Verdict : uint32_t {
    ShimsMissing = 0,  // deploy/build problem, not a timing one
    FsAbsent,          // FSAddClient refused - FS is not up here
    FsUpFileMissing,   // FS answered; the probe file simply is not there
    FsUsable,          // opened and read a file
};

inline const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::ShimsMissing:    return "SHIMS-MISSING";
        case Verdict::FsAbsent:        return "FS-ABSENT";
        case Verdict::FsUpFileMissing: return "FS-UP-FILE-MISSING";
        case Verdict::FsUsable:        return "FS-USABLE";
    }
    return "?";
}

#if WIIXL_CEMU

namespace impl {

// Separate from WiiXLaunch::FS::impl on purpose - see the header comment.
alignas(32) inline uint8_t g_ProbeClient[0x1700];
alignas(32) inline uint8_t g_ProbeCmdBlock[0xA80];

// coreinit's FSStat is 0x64 bytes and FSGetStatFile writes all of them. This
// was 96 bytes once: FSGetStatFile wrote four bytes onto the adjacent stack
// slot, zeroed a read length, and every read then "succeeded" having
// transferred nothing. Same trap as wiixlaunch/fs.hpp guards against.
struct FsStatBuf {
    uint32_t flags, mode, owner, group, size, rest[20];
};
static_assert(sizeof(FsStatBuf) == 0x64, "must match coreinit FSStat exactly");

using FnFSAddClient    = int32_t (*)(void*, uint32_t);
using FnFSDelClient    = int32_t (*)(void*, uint32_t);
using FnFSInitCmdBlock = void    (*)(void*);
using FnFSOpenFile     = int32_t (*)(void*, void*, const char*, const char*, uint32_t*, uint32_t);
using FnFSGetStatFile  = int32_t (*)(void*, void*, uint32_t, void*, uint32_t);
using FnFSReadFile     = int32_t (*)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using FnFSCloseFile    = int32_t (*)(void*, void*, uint32_t, uint32_t);

} // namespace impl

// Runs FSAddClient -> FSOpenFile -> FSGetStatFile -> FSReadFile -> FSCloseFile
// -> FSDelClient and logs each raw status code.
//
// `where` names the call site and appears on every line: the entire value of
// this probe is comparing one site against another within a single boot.
inline Verdict Probe(const char* where, const char* path = kCemuProbePath) {
    WIIXL_LOG("[LP:%s] probe start, path=%s", where, path);

    if (!WiiXLaunch::Backend::CemuFsAvailable()) {
        WIIXL_LOG("[LP:%s] verdict=%s base=%p offset=%u - deploy.py has not patched the "
                  "shim table. This is a build problem, NOT a timing one.",
                  where, VerdictName(Verdict::ShimsMissing),
                  reinterpret_cast<void*>(WiiXLaunch::Backend::g_CodeCaveBase),
                  static_cast<unsigned>(::g_CemuFsShimTableOffset));
        return Verdict::ShimsMissing;
    }

    using namespace impl;
    auto addClient    = Backend::ResolveCemuFs<FnFSAddClient>(Backend::CemuFsImport::FSAddClient);
    auto delClient    = Backend::ResolveCemuFs<FnFSDelClient>(Backend::CemuFsImport::FSDelClient);
    auto initCmdBlock = Backend::ResolveCemuFs<FnFSInitCmdBlock>(Backend::CemuFsImport::FSInitCmdBlock);
    auto openFile     = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto getStat      = Backend::ResolveCemuFs<FnFSGetStatFile>(Backend::CemuFsImport::FSGetStatFile);
    auto readFile     = Backend::ResolveCemuFs<FnFSReadFile>(Backend::CemuFsImport::FSReadFile);
    auto closeFile    = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);

    WIIXL_LOG("[LP:%s] shims add=%p init=%p open=%p stat=%p read=%p close=%p",
              where, (void*)addClient, (void*)initCmdBlock, (void*)openFile,
              (void*)getStat, (void*)readFile, (void*)closeFile);

    if (!addClient || !initCmdBlock || !openFile || !readFile || !closeFile) {
        WIIXL_LOG("[LP:%s] verdict=%s - table present but a slot resolved null",
                  where, VerdictName(Verdict::ShimsMissing));
        return Verdict::ShimsMissing;
    }

    // Announce before calling. If coreinit's FS is not up, this is the call
    // most likely to take the process down, and this line being last in the log
    // is itself the answer.
    WIIXL_LOG("[LP:%s] calling FSAddClient...", where);
    const int32_t addStatus = addClient(g_ProbeClient, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s] FSAddClient -> %d", where, addStatus);
    if (addStatus != 0) {
        WIIXL_LOG("[LP:%s] verdict=%s (FSAddClient refused)", where, VerdictName(Verdict::FsAbsent));
        return Verdict::FsAbsent;
    }

    WIIXL_LOG("[LP:%s] calling FSInitCmdBlock...", where);
    initCmdBlock(g_ProbeCmdBlock);

    uint32_t handle = 0;
    WIIXL_LOG("[LP:%s] calling FSOpenFile...", where);
    const int32_t openStatus = openFile(g_ProbeClient, g_ProbeCmdBlock, path, "r", &handle, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s] FSOpenFile -> %d (handle=%u)", where, openStatus, handle);

    if (openStatus != 0) {
        // -6 is FS_STATUS_NOT_FOUND, and it is a PASS for load-point purposes:
        // the filesystem answered, it just has no such file. Logging the raw
        // code rather than a bool is what makes that distinguishable.
        WIIXL_LOG("[LP:%s] verdict=%s - FS answered %d (-6 = NOT_FOUND). The load point "
                  "is viable here; the probe file just is not present.",
                  where, VerdictName(Verdict::FsUpFileMissing), openStatus);
        if (delClient) delClient(g_ProbeClient, 0xFFFFFFFF);
        return Verdict::FsUpFileMissing;
    }

    impl::FsStatBuf stat{};
    const int32_t statStatus = getStat
        ? getStat(g_ProbeClient, g_ProbeCmdBlock, handle, &stat, 0xFFFFFFFF) : -1;
    WIIXL_LOG("[LP:%s] FSGetStatFile -> %d (size=%u)", where, statStatus, (unsigned)stat.size);

    alignas(64) static uint8_t buf[64];
    const uint32_t toRead = (statStatus == 0 && stat.size && stat.size < sizeof(buf))
                          ? stat.size : static_cast<uint32_t>(sizeof(buf));
    const int32_t readBytes = readFile(g_ProbeClient, g_ProbeCmdBlock, buf, 1, toRead, handle, 0, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s] FSReadFile(%u) -> %d", where, toRead, readBytes);

    const int32_t closeStatus = closeFile(g_ProbeClient, g_ProbeCmdBlock, handle, 0xFFFFFFFF);
    WIIXL_LOG("[LP:%s] FSCloseFile -> %d", where, closeStatus);

    if (delClient) {
        const int32_t delStatus = delClient(g_ProbeClient, 0xFFFFFFFF);
        WIIXL_LOG("[LP:%s] FSDelClient -> %d", where, delStatus);
    }

    WIIXL_LOG("[LP:%s] verdict=%s (read %d bytes)", where, VerdictName(Verdict::FsUsable), readBytes);
    return Verdict::FsUsable;
}

#elif WIIXL_WIIU

inline Verdict Probe(const char* where, const char* path = kWiiUProbePath) {
    WIIXL_LOG("[LP:%s] probe start, path=%s", where, path);

    alignas(32) static FSClient client;
    alignas(32) static FSCmdBlock cmdBlock;

    WIIXL_LOG("[LP:%s] calling FSAddClient...", where);
    const FSStatus addStatus = FSAddClient(&client, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s] FSAddClient -> %d", where, static_cast<int>(addStatus));
    if (addStatus != FS_STATUS_OK) {
        WIIXL_LOG("[LP:%s] verdict=%s", where, VerdictName(Verdict::FsAbsent));
        return Verdict::FsAbsent;
    }
    FSInitCmdBlock(&cmdBlock);

    FSFileHandle handle = 0;
    const FSStatus openStatus = FSOpenFile(&client, &cmdBlock, path, "r", &handle, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s] FSOpenFile -> %d (handle=%u)", where,
              static_cast<int>(openStatus), static_cast<unsigned>(handle));
    if (openStatus != FS_STATUS_OK) {
        WIIXL_LOG("[LP:%s] verdict=%s - FS answered %d", where,
                  VerdictName(Verdict::FsUpFileMissing), static_cast<int>(openStatus));
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        return Verdict::FsUpFileMissing;
    }

    alignas(64) static uint8_t buf[64];
    const int32_t readBytes = FSReadFile(&client, &cmdBlock, buf, 1, sizeof(buf),
                                         handle, 0, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[LP:%s] FSReadFile -> %d", where, readBytes);
    FSCloseFile(&client, &cmdBlock, handle, FS_ERROR_FLAG_ALL);
    FSDelClient(&client, FS_ERROR_FLAG_ALL);

    WIIXL_LOG("[LP:%s] verdict=%s (read %d bytes)", where,
              VerdictName(Verdict::FsUsable), readBytes);
    return Verdict::FsUsable;
}

#else

// Switch has no coreinit FS. The loader will read from mount_path through the
// Switch backend instead. This probe has nothing to say about that and says so,
// rather than reporting a pass that would mean nothing.
inline Verdict Probe(const char* where, const char* path = "") {
    WIIXL_LOG("[LP:%s] probe skipped: no coreinit FS on this target%s", where, path);
    return Verdict::ShimsMissing;
}

#endif

// ---------------------------------------------------------------------------
// NOMINATION - deliberately absent, and this is the note explaining why.
//
// The load point is a PER-GAME fact. A different title's host needs a different
// early post-FS function, and base has no way to know it. So base owns Probe()
// and the loader, never the address.
//
// Stage 2 gives the surface registry an inverse direction: a game module
// NOMINATES a load point (address + name) to the host, base installs the hook
// there and runs the loader from inside it, and a host with NO game module logs
// "no load point nominated" instead of booting and silently doing nothing. Until
// that exists, the caller passes the address in - see src/main.cpp, where the
// BotW address is marked as the stand-in it is.
// ---------------------------------------------------------------------------

} // namespace WiiXLaunch::LoadPoint
