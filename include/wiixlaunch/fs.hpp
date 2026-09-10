#pragma once

// WiiXLaunch::FS - reading and writing files from the title's filesystem.
//
// This is base-framework plumbing, not game knowledge. Every OS call below is
// a coreinit FS export present in every Wii U title, reached with no
// game-specific address: on Wii U by linking coreinit directly, on Cemu
// through the `import.coreinit.<Name>` tail-call shims in src/cemu/cemu_fs.asm
// (see wiixlaunch/cemu/cemu_fs.hpp for why that indirection exists at all).
//
// It lives in base because base depends on it: the module loader reads .wxlm
// blobs off the filesystem at the load point, and the single FS client below
// has to be owned by the host. Two mods each carrying their own copy would
// mean two FSAddClient calls against two separate 0x1700-byte client buffers.
//
// Switch has no coreinit FS; every entry point here returns a clean failure on
// that target rather than pretending to work.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_fs.hpp>
#elif WIIXL_WIIU
#include <coreinit/filesystem.h>
#elif WIIXL_SWITCH
#include <nn/fs.hpp>
#endif

namespace WiiXLaunch::FS {

namespace impl {

// Static client and command block buffers to avoid heap allocation.
//
// There is exactly one of each, and they belong to the host. Anything needing
// FS goes through this client rather than adding its own.
alignas(32) inline uint8_t g_FSClient[0x1700];
alignas(32) inline uint8_t g_FSCmdBlock[0xA80];
inline bool g_FSClientReady = false;

// coreinit's FSReadFile family requires a 64-BYTE-ALIGNED destination buffer.
// ReadAt has always said so in a comment; ReadFile did not, and did not check.
//
// The failure is quiet and it is not deterministic, which is the worst
// combination. An unaligned buffer does not fault - the read simply transfers
// nothing, or less than asked - so whether a mod's file read works depends on
// where the compiler happened to put its stack buffer that build. Two example
// mods doing the identical thing, `char buf[64]` on the stack, disagreed:
// b_second read its greeting and a_first got zero bytes back for an 18-byte
// file. Nothing in either mod was different; the stack offsets were.
//
// A mod cannot reasonably be expected to know this, and telling it to use
// alignas(64) only moves the trap - it still bites whoever forgets. So an
// unaligned destination is STAGED through this buffer instead: correct for any
// caller, at the cost of one copy for the callers that need it.
constexpr uint32_t kFSBufferAlign = 64;
alignas(kFSBufferAlign) inline uint8_t g_FSStaging[4096];

inline bool IsFSAligned(const void* p) {
    return (reinterpret_cast<uintptr_t>(p) & (kFSBufferAlign - 1)) == 0;
}

// How many times an unaligned read has been staged. Logged for the first few
// only: it is worth knowing that a caller is paying for a copy, and not worth
// one line a frame if something reads in a tick.
inline uint32_t g_StagedReads = 0;

// The path candidates a relative name is tried through, in order.
//
// ONE list, because a directory that resolves differently from the files inside
// it is a bug with no symptom until something enumerates. That is exactly what
// happened: the module loader opened "WiiXLaunch/mods" with a raw FSOpenDir
// while every file open went through this list, so the loader reported the
// directory missing three lines after the load-point probe had listed its
// contents. Anything that opens a path resolves it here.
//
// `storage` supplies the buffers; `out` is filled with up to 4 candidates, the
// first being the path exactly as given. Entries may be null - skip those.
// Where a Switch host keeps its files. "sd" rather than something longer
// because it appears in every path this builds.
constexpr const char* kSwitchMount = "sd";

// nn::fs ABORTS THE PROCESS for a path with no mount name. It does not return
// a Result: FindFileSystem calls nn::diag Abort, the game dies, and the log
// ends with ResultFsInvalidMountName (2002-6065) and a guest stack trace.
//
// That breaks the assumption the candidate list is built on. coreinit answers
// "no" to a path it cannot open, so trying several and keeping the first that
// works costs nothing. Here the FIRST WRONG TRY IS FATAL - and the first
// candidate has always been the path exactly as given, which for
// "WiiXLaunch/mods" has no mount name at all.
//
// Measured, on the first Switch boot ever attempted: the loader logged
// "enumerating WiiXLaunch/mods" and the process aborted inside OpenDirectory
// before ListWxlm could report anything.
//
// So every path handed to nn::fs is checked first. A candidate without a mount
// name is skipped rather than tried.
inline bool HasMountName(const char* p) {
    if (!p || !p[0] || p[0] == '/' || p[0] == ':') return false;
    for (uint32_t i = 0; p[i] && i < 32; ++i) {
        if (p[i] == ':') return i > 0;
        if (p[i] == '/') return false;      // a separator came first
    }
    return false;
}


inline void Candidates(const char* path, char storage[3][256], const char* out[4]) {
    auto concat2 = [](char* dst, size_t cap, const char* a, const char* b) {
        size_t la = 0; while (a[la] && la + 1 < cap) { dst[la] = a[la]; ++la; }
        size_t lb = 0; while (b[lb] && la + lb + 1 < cap) { dst[la + lb] = b[lb]; ++lb; }
        dst[la + lb] = '\0';
    };

    out[0] = path;
    out[1] = out[2] = out[3] = nullptr;
#if WIIXL_SWITCH
    // A Switch has no /vol/content: the game's own files are in romfs and a
    // host's files are on the SD card. Only the SD forms are offered, because
    // a candidate that cannot exist is a line of log noise on every miss.
    //
    // out[0] stays the path as given so an absolute "sd:/..." still works.
    if (path && path[0] != '\0') {
        concat2(storage[0], 256, "sd:/", path);
        concat2(storage[1], 256, "sd:/atmosphere/contents/WiiXLaunch/", path);
        out[1] = storage[0];
        out[2] = storage[1];
    }
#else
    if (path && path[0] != '/') {
        concat2(storage[0], 256, "/vol/content/", path);
        concat2(storage[1], 256, "content/", path);
        concat2(storage[2], 256, "/vol/content/WiiXLaunch/", path);
        out[1] = storage[0];
        out[2] = storage[1];
        out[3] = storage[2];
    }
#endif
}

inline bool EnsureFSClient() {
#if WIIXL_CEMU
    if (g_FSClientReady) return true;

    using FnFSAddClient = int32_t (*)(void* client, uint32_t errorMask);
    using FnFSInitCmdBlock = void (*)(void* cmdBlock);

    auto addClient = Backend::ResolveCemuFs<FnFSAddClient>(Backend::CemuFsImport::FSAddClient);
    auto initCmdBlock = Backend::ResolveCemuFs<FnFSInitCmdBlock>(Backend::CemuFsImport::FSInitCmdBlock);

    if (addClient && initCmdBlock) {
        int32_t status = addClient(g_FSClient, 0xFFFFFFFF);
        initCmdBlock(g_FSCmdBlock);
        g_FSClientReady = (status == 0);
        WIIXL_LOG("WiiXLaunch: FS client initialized (status=%d)", status);
        return g_FSClientReady;
    }
    return false;
#elif WIIXL_WIIU
    if (g_FSClientReady) return true;
    int32_t status = FSAddClient(reinterpret_cast<FSClient*>(g_FSClient), FS_ERROR_FLAG_ALL);
    FSInitCmdBlock(reinterpret_cast<FSCmdBlock*>(g_FSCmdBlock));
    g_FSClientReady = (status == 0);
    return g_FSClientReady;
#elif WIIXL_SWITCH
    // There is no client and no command block here; what has to happen once is
    // the mount. nn::fs paths are "<mount>:/...", so nothing resolves until
    // this has succeeded, and every candidate below is written against it.
    //
    // MountSdCardForDebug needs the process to have filesystem permission. A
    // subsdk under Atmosphere normally does; a build that does not gets a
    // non-zero Result here, and that is a permissions problem rather than a
    // missing file - so it is logged as itself rather than becoming "not
    // found" four candidate paths later.
    if (g_FSClientReady) return true;
    // nn::Result is a bare u32 in exlaunch's bindings - 0 is success. There is
    // no IsSuccess() to call, and treating the value as a class compiles
    // nowhere.
    // Result is a global typedef in these bindings, not nn::Result - it is
    // declared in nn_common.hpp outside any namespace. auto sidesteps the
    // question of which spelling this vendored copy happens to use.
    const auto r = nn::fs::MountSdCardForDebug(kSwitchMount);
    g_FSClientReady = (r == 0);
    if (!g_FSClientReady) {
        WIIXL_LOG("WiiXLaunch: could not mount the SD card as '%s:' (result 0x%X). "
                  "Nothing on the card is readable, which is not the same as the "
                  "files being absent.", kSwitchMount, static_cast<unsigned>(r));
    }
    return g_FSClientReady;
#else
    return true;
#endif
}

} // namespace impl

// Reads an entire file from disk (e.g. "WiiXLaunch/logo.bin" or "/vol/content/WiiXLaunch/logo.bin")
inline bool ReadFile(const char* path, void* outBuffer, size_t maxBufferSize, size_t* outReadSize = nullptr) {
    if (!path || !outBuffer || maxBufferSize == 0) return false;
    if (!impl::EnsureFSClient()) return false;

#if WIIXL_CEMU
    using FnFSOpenFile = int32_t (*)(void* client, void* block, const char* path, const char* mode, uint32_t* handle, uint32_t errorMask);
    using FnFSGetStatFile = int32_t (*)(void* client, void* block, uint32_t handle, void* stat, uint32_t errorMask);
    using FnFSReadFile = int32_t (*)(void* client, void* block, void* buffer, uint32_t size, uint32_t count, uint32_t handle, uint32_t unk1, uint32_t errorMask);
    using FnFSCloseFile = int32_t (*)(void* client, void* block, uint32_t handle, uint32_t errorMask);

    auto openFile = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto getStat = Backend::ResolveCemuFs<FnFSGetStatFile>(Backend::CemuFsImport::FSGetStatFile);
    auto readFile = Backend::ResolveCemuFs<FnFSReadFile>(Backend::CemuFsImport::FSReadFile);
    auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);

    if (!openFile || !readFile || !closeFile) return false;

    auto concat2 = [](char* dst, size_t cap, const char* a, const char* b) {
        size_t la = strlen(a);
        size_t lb = strlen(b);
        if (la + lb + 1 > cap) return;
        memcpy(dst, a, la);
        memcpy(dst + la, b, lb);
        dst[la + lb] = '\0';
    };

    // Try both absolute /vol/content paths and relative content paths
    const char* pathsToTry[4] = {
        path,
        nullptr,
        nullptr,
        nullptr
    };
    char altPath1[256];
    char altPath2[256];
    char altPath3[256];
    if (path[0] != '/') {
        concat2(altPath1, sizeof(altPath1), "/vol/content/", path);
        concat2(altPath2, sizeof(altPath2), "content/", path);
        concat2(altPath3, sizeof(altPath3), "/vol/content/WiiXLaunch/", path);
        pathsToTry[1] = altPath1;
        pathsToTry[2] = altPath2;
        pathsToTry[3] = altPath3;
    }

    uint32_t handle = 0;
    int32_t status = -1;
    const char* openedPath = nullptr;

    for (int i = 0; i < 4; ++i) {
        if (!pathsToTry[i]) continue;
        status = openFile(impl::g_FSClient, impl::g_FSCmdBlock, pathsToTry[i], "r", &handle, 218);
        if (status == 0) {
            openedPath = pathsToTry[i];
            break;
        }
    }

    if (status != 0) {
        WIIXL_LOG("WiiXLaunch: FSOpenFile failed for '%s' (status=%d)", path, status);
        return false;
    }

    // MUST be the full 0x64 bytes coreinit's FSStat occupies (wut asserts that
    // size). This was 96 bytes for a while, and FSGetStatFile duly wrote four
    // bytes past the end of it - straight onto the adjacent stack slot, which
    // the compiler had given to toRead. FSStat's tail is its `attributes` array
    // and reads back as zeroes, so toRead became 0, FSReadFile was asked for
    // zero bytes, and every read "succeeded" having transferred nothing.
    struct FsStatBuf {
        uint32_t flags;
        uint32_t mode;
        uint32_t owner;
        uint32_t group;
        uint32_t size;
        uint32_t rest[20];
    };
    static_assert(sizeof(FsStatBuf) == 0x64, "must match coreinit FSStat exactly");
    alignas(64) FsStatBuf statBuf{};

    size_t toRead = maxBufferSize;
    int32_t statStatus = getStat ? getStat(impl::g_FSClient, impl::g_FSCmdBlock, handle, &statBuf, 0xFFFFFFFF)
                                 : -1;
    if (statStatus == 0 && statBuf.size > 0 && statBuf.size <= maxBufferSize) {
        toRead = statBuf.size;
    }

    // Belt and braces after the above: asking FSReadFile for zero bytes reads
    // nothing and reports success, which is indistinguishable from a genuinely
    // empty file and took a while to spot the first time.
    if (toRead == 0) {
        WIIXL_LOG("WiiXLaunch: ReadFile '%s' aborted - computed a zero-byte read", openedPath);
        closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);
        return false;
    }

    int32_t readBytes;
    if (impl::IsFSAligned(outBuffer)) {
        readBytes = readFile(impl::g_FSClient, impl::g_FSCmdBlock, outBuffer, 1, toRead,
                             handle, 0, 0xFFFFFFFF);
    } else {
        // Staged, in chunks, through an aligned buffer. FSReadFile advances the
        // file position, so successive calls continue where the last stopped.
        if (impl::g_StagedReads < 3) {
            WIIXL_LOG("WiiXLaunch: ReadFile '%s' - caller's buffer is not 64-byte "
                      "aligned, staging the read through the host's buffer", openedPath);
        }
        impl::g_StagedReads++;

        size_t done = 0;
        readBytes = 0;
        while (done < toRead) {
            size_t chunk = toRead - done;
            if (chunk > sizeof(impl::g_FSStaging)) chunk = sizeof(impl::g_FSStaging);

            const int32_t got = readFile(impl::g_FSClient, impl::g_FSCmdBlock,
                                         impl::g_FSStaging, 1, static_cast<uint32_t>(chunk),
                                         handle, 0, 0xFFFFFFFF);
            if (got < 0) { readBytes = got; break; }
            for (int32_t b = 0; b < got; ++b) {
                static_cast<uint8_t*>(outBuffer)[done + b] = impl::g_FSStaging[b];
            }
            done += static_cast<size_t>(got);
            readBytes = static_cast<int32_t>(done);
            if (static_cast<size_t>(got) < chunk) break;   // short read, stop
        }
    }
    closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);

    if (readBytes < 0) {
        WIIXL_LOG("WiiXLaunch: FSReadFile failed for '%s' (status=%d)", openedPath, readBytes);
        return false;
    }

    // A SHORT READ IS NOT SUCCESS, and used to be reported as one. "OK (0
    // bytes, size=18)" was printed for a file the caller then found empty - the
    // log asserted a success the code had not achieved, which is worse than
    // silence because it points the search away from the real fault.
    if (static_cast<size_t>(readBytes) < toRead) {
        WIIXL_LOG("WiiXLaunch: ReadFile '%s' SHORT - got %d of %u bytes (stat=%d)",
                  openedPath, readBytes, (unsigned)toRead, statStatus);
        if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
        return false;
    }

    if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
    WIIXL_LOG("WiiXLaunch: ReadFile '%s' OK (%d bytes, stat=%d size=%u)",
              openedPath, readBytes, statStatus, (unsigned)statBuf.size);
    return true;
#elif WIIXL_WIIU
    FSFileHandle handle = 0;
    FSStatus status = FSOpenFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                 reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                 path, "r", &handle, FS_ERROR_FLAG_ALL);
    if (status != FS_STATUS_OK) return false;

    FSStat stat{};
    size_t toRead = maxBufferSize;
    if (FSGetStatFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                      reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                      handle, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        if (stat.size > 0 && stat.size <= maxBufferSize) {
            toRead = stat.size;
        }
    }

    // Identical alignment requirement and identical short-read rule as the Cemu
    // branch above - it is the same coreinit call underneath, so the fix is
    // swept here in the same commit rather than left for the next boot on
    // hardware to rediscover.
    int32_t readBytes;
    if (impl::IsFSAligned(outBuffer)) {
        readBytes = FSReadFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                               reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                               reinterpret_cast<uint8_t*>(outBuffer), 1, toRead, handle, 0,
                               FS_ERROR_FLAG_ALL);
    } else {
        if (impl::g_StagedReads < 3) {
            WIIXL_LOG("WiiXLaunch: ReadFile '%s' - caller's buffer is not 64-byte "
                      "aligned, staging the read through the host's buffer", path);
        }
        impl::g_StagedReads++;

        size_t done = 0;
        readBytes = 0;
        while (done < toRead) {
            size_t chunk = toRead - done;
            if (chunk > sizeof(impl::g_FSStaging)) chunk = sizeof(impl::g_FSStaging);

            const int32_t got = FSReadFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                           reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                           impl::g_FSStaging, 1, static_cast<uint32_t>(chunk),
                                           handle, 0, FS_ERROR_FLAG_ALL);
            if (got < 0) { readBytes = got; break; }
            for (int32_t b = 0; b < got; ++b) {
                static_cast<uint8_t*>(outBuffer)[done + b] = impl::g_FSStaging[b];
            }
            done += static_cast<size_t>(got);
            readBytes = static_cast<int32_t>(done);
            if (static_cast<size_t>(got) < chunk) break;
        }
    }
    FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                handle, FS_ERROR_FLAG_ALL);

    if (readBytes < 0) return false;
    if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
    if (static_cast<size_t>(readBytes) < toRead) {
        WIIXL_LOG("WiiXLaunch: ReadFile '%s' SHORT - got %d of %u bytes",
                  path, readBytes, (unsigned)toRead);
        return false;
    }
    return true;
#else
    return false;
#endif
}

// Writes a buffer to a file on disk
inline bool WriteFile(const char* path, const void* buffer, size_t size, size_t* outWrittenSize = nullptr) {
    if (!path || !buffer || size == 0) return false;
    if (!impl::EnsureFSClient()) return false;

#if WIIXL_CEMU
    using FnFSOpenFile = int32_t (*)(void* client, void* block, const char* path, const char* mode, uint32_t* handle, uint32_t errorMask);
    using FnFSWriteFile = int32_t (*)(void* client, void* block, const void* buffer, uint32_t size, uint32_t count, uint32_t handle, uint32_t unk1, uint32_t errorMask);
    using FnFSCloseFile = int32_t (*)(void* client, void* block, uint32_t handle, uint32_t errorMask);

    auto openFile = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
    auto writeFile = Backend::ResolveCemuFs<FnFSWriteFile>(Backend::CemuFsImport::FSWriteFile);
    auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);

    if (!openFile || !writeFile || !closeFile) return false;

    uint32_t handle = 0;
    int32_t status = openFile(impl::g_FSClient, impl::g_FSCmdBlock, path, "w", &handle, 0xFFFFFFFF);
    if (status != 0) {
        WIIXL_LOG("WiiXLaunch: FSOpenFile for write failed for '%s' (status=%d)", path, status);
        return false;
    }

    int32_t writtenBytes = writeFile(impl::g_FSClient, impl::g_FSCmdBlock, buffer, 1, size, handle, 0, 0xFFFFFFFF);
    closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);

    if (writtenBytes >= 0) {
        if (outWrittenSize) *outWrittenSize = static_cast<size_t>(writtenBytes);
        WIIXL_LOG("WiiXLaunch: WriteFile '%s' OK (%d bytes)", path, writtenBytes);
        return true;
    }

    WIIXL_LOG("WiiXLaunch: FSWriteFile failed for '%s' (status=%d)", path, writtenBytes);
    return false;
#elif WIIXL_WIIU
    FSFileHandle handle = 0;
    FSStatus status = FSOpenFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                 reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                 path, "w", &handle, FS_ERROR_FLAG_ALL);
    if (status != FS_STATUS_OK) return false;

    // wut declares FSWriteFile's buffer as a non-const uint8_t* even though the
    // call only reads from it, so the const has to come off somewhere. Doing it
    // here keeps WriteFile's own signature honest (`const void* buffer`); before
    // this, the mismatch made the whole header fail to compile on the Wii U
    // target, which is why callers had to avoid including it at all.
    int32_t writtenBytes = FSWriteFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                       reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buffer)),
                                       1, size, handle, 0, FS_ERROR_FLAG_ALL);
    FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                handle, FS_ERROR_FLAG_ALL);

    if (writtenBytes >= 0) {
        if (outWrittenSize) *outWrittenSize = static_cast<size_t>(writtenBytes);
        return true;
    }
    return false;
#else
    return false;
#endif
}

// An open file that supports positioned reads, for pulling pieces out of
// archives far too large to ReadFile whole (BotW's Pack/Bootup.pack is ~30 MB;
// the Cemu payload's built-in heap is the tail of its own code cave, under
// 4 MB in total - see wiixl_cemu_backend.hpp). Same path candidates as ReadFile.
// `buffer` passed to ReadAt must be 64-byte aligned - coreinit's FSReadFile
// family requires it - and the read size should be a multiple of 64 unless
// it is the tail of the file.
//
// Trivially destructible on purpose (no auto-Close): the Cemu payload runs
// without a C runtime, so nothing may need static destructors, and this is
// held as an inline global by the BotW module's GUI asset loader. Call Close().
class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool Open(const char* path) {
        Close();
        if (!path || !impl::EnsureFSClient()) return false;

        auto concat2 = [](char* dst, size_t cap, const char* a, const char* b) {
            size_t la = strlen(a);
            size_t lb = strlen(b);
            if (la + lb + 1 > cap) { dst[0] = '\0'; return; }
            memcpy(dst, a, la);
            memcpy(dst + la, b, lb);
            dst[la + lb] = '\0';
        };

        // Through the shared list, so a file and the directory holding it are
        // always resolved the same way.
        char storage[3][256];
        const char* pathsToTry[4];
        impl::Candidates(path, storage, pathsToTry);
        (void)concat2;

#if WIIXL_CEMU
        using FnFSOpenFile = int32_t (*)(void* client, void* block, const char* path, const char* mode, uint32_t* handle, uint32_t errorMask);
        using FnFSGetStatFile = int32_t (*)(void* client, void* block, uint32_t handle, void* stat, uint32_t errorMask);
        auto openFile = Backend::ResolveCemuFs<FnFSOpenFile>(Backend::CemuFsImport::FSOpenFile);
        auto getStat = Backend::ResolveCemuFs<FnFSGetStatFile>(Backend::CemuFsImport::FSGetStatFile);
        if (!openFile) return false;

        for (int i = 0; i < 4; ++i) {
            if (!pathsToTry[i] || !pathsToTry[i][0]) continue;
            uint32_t handle = 0;
            if (openFile(impl::g_FSClient, impl::g_FSCmdBlock, pathsToTry[i], "r", &handle, 218) == 0) {
                m_Handle = handle;
                m_Open = true;
                break;
            }
        }
        if (!m_Open) {
            WIIXL_LOG("WiiXLaunch: File::Open failed for '%s'", path);
            return false;
        }

        struct FsStatBuf { uint32_t flags, mode, owner, group, size, rest[20]; };
        static_assert(sizeof(FsStatBuf) == 0x64, "must match coreinit FSStat exactly");
        alignas(64) FsStatBuf statBuf{};
        if (getStat && getStat(impl::g_FSClient, impl::g_FSCmdBlock,
                       static_cast<uint32_t>(m_Handle), &statBuf, 0xFFFFFFFF) == 0) {
            m_Size = statBuf.size;
        }
        return true;
#elif WIIXL_WIIU
        for (int i = 0; i < 4; ++i) {
            if (!pathsToTry[i] || !pathsToTry[i][0]) continue;
            FSFileHandle handle = 0;
            if (FSOpenFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                           reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                           pathsToTry[i], "r", &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
                m_Handle = handle;
                m_Open = true;
                break;
            }
        }
        if (!m_Open) return false;
        FSStat stat{};
        if (FSGetStatFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                          reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                          static_cast<FSFileHandle>(m_Handle), &stat,
                          FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            m_Size = stat.size;
        }
        return true;
#elif WIIXL_SWITCH
        for (int i = 0; i < 4; ++i) {
            if (!pathsToTry[i] || !pathsToTry[i][0]) continue;
            // Not a "would probably fail" check - see HasMountName. Handing
            // this one to nn::fs would end the process, not the loop.
            if (!impl::HasMountName(pathsToTry[i])) continue;
            nn::fs::FileHandle handle{};
            if (nn::fs::OpenFile(&handle, pathsToTry[i],
                                 nn::fs::OpenMode_Read) == 0) {
                m_Handle = handle._internal;
                m_Open = true;
                break;
            }
        }
        if (!m_Open) {
            WIIXL_LOG("WiiXLaunch: File::Open failed for '%s'", path);
            return false;
        }
        {
            nn::fs::FileHandle handle{m_Handle};
            long size = 0;
            if (nn::fs::GetFileSize(&size, handle) == 0 && size >= 0) {
                // A .wxlm is bounded by a 32-bit fileSize field, so anything
                // that does not fit one is not a module this loader can read.
                m_Size = (size > 0xFFFFFFFFll) ? 0xFFFFFFFFu
                                               : static_cast<uint32_t>(size);
            }
        }
        return true;
#else
        (void)pathsToTry;
        return false;
#endif
    }

    bool IsOpen() const { return m_Open; }
    uint32_t Size() const { return m_Size; }

    // Reads up to `size` bytes at absolute `offset`. Returns the byte count
    // actually read (0 at/after EOF or on error).
    uint32_t ReadAt(uint32_t offset, void* buffer, uint32_t size) {
        if (!m_Open || !buffer || size == 0) return 0;
        if (m_Size && offset >= m_Size) return 0;
        if (m_Size && offset + size > m_Size) size = m_Size - offset;
#if WIIXL_CEMU
        using FnFSReadFileWithPos = int32_t (*)(void* client, void* block, void* buffer, uint32_t size, uint32_t count,
                                                uint32_t pos, uint32_t handle, uint32_t flags, uint32_t errorMask);
        auto readAt = Backend::ResolveCemuFs<FnFSReadFileWithPos>(Backend::CemuFsImport::FSReadFileWithPos);
        if (!readAt) return 0;
        int32_t got = readAt(impl::g_FSClient, impl::g_FSCmdBlock, buffer, 1, size,
                             offset, static_cast<uint32_t>(m_Handle), 0, 0xFFFFFFFF);
        return got > 0 ? static_cast<uint32_t>(got) : 0;
#elif WIIXL_WIIU
        int32_t got = FSReadFileWithPos(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                        reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                        reinterpret_cast<uint8_t*>(buffer), 1, size,
                                        offset, static_cast<FSFileHandle>(m_Handle), 0,
                                        FS_ERROR_FLAG_ALL);
        return got > 0 ? static_cast<uint32_t>(got) : 0;
#elif WIIXL_SWITCH
        // THE FOUR-ARGUMENT OVERLOAD WITH THE SIZE, not the one with a
        // bytesRead out-parameter.
        //
        // exlaunch declares both. The out-parameter one is
        //   ReadFile(ulong* bytesRead, FileHandle, long position, void* buffer)
        // which documents a `size` argument in its comment and does not have
        // one in its signature - and nnSdk does not export it either. Calling
        // it links, because a module resolves its imports at load; rtld then
        // reports "Unresolved symbol _ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPv"
        // and the call branches to address 0. That is what the first Switch
        // boot that got this far actually did.
        //
        // This overload reads EXACTLY `size` bytes or fails - there is no short
        // read to report, which is why it needs no out-parameter. Tail reads
        // work because ReadAt has already clamped `size` against m_Size above.
        nn::fs::FileHandle handle{m_Handle};
        if (nn::fs::ReadFile(handle, static_cast<long>(offset), buffer,
                             static_cast<unsigned long>(size)) != 0) {
            return 0;
        }
        return size;
#else
        (void)offset;
        return 0;
#endif
    }

    void Close() {
        if (!m_Open) return;
#if WIIXL_CEMU
        using FnFSCloseFile = int32_t (*)(void* client, void* block, uint32_t handle, uint32_t errorMask);
        auto closeFile = Backend::ResolveCemuFs<FnFSCloseFile>(Backend::CemuFsImport::FSCloseFile);
        if (closeFile) closeFile(impl::g_FSClient, impl::g_FSCmdBlock,
                                 static_cast<uint32_t>(m_Handle), 0xFFFFFFFF);
#elif WIIXL_WIIU
        FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                    reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                    static_cast<FSFileHandle>(m_Handle), FS_ERROR_FLAG_ALL);
#elif WIIXL_SWITCH
        nn::fs::CloseFile(nn::fs::FileHandle{m_Handle});
#endif
        m_Open = false;
        m_Handle = 0;
        m_Size = 0;
    }

private:
    // 64 bits because nn::fs::FileHandle is a u64 and coreinit's FSFileHandle
    // is a u32. Widening the storage costs four bytes per open file and means
    // the class does not need a per-platform member.
    uint64_t m_Handle = 0;
    uint32_t m_Size = 0;
    bool m_Open = false;
};

} // namespace WiiXLaunch::FS
