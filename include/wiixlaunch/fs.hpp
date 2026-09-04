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

    int32_t readBytes = readFile(impl::g_FSClient, impl::g_FSCmdBlock, outBuffer, 1, toRead, handle, 0, 0xFFFFFFFF);
    closeFile(impl::g_FSClient, impl::g_FSCmdBlock, handle, 0xFFFFFFFF);

    if (readBytes >= 0) {
        if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
        WIIXL_LOG("WiiXLaunch: ReadFile '%s' OK (%d bytes, stat=%d size=%u)",
                  openedPath, readBytes, statStatus, (unsigned)statBuf.size);
        return true;
    }

    WIIXL_LOG("WiiXLaunch: FSReadFile failed for '%s' (status=%d)", openedPath, readBytes);
    return false;
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

    int32_t readBytes = FSReadFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                   reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                   reinterpret_cast<uint8_t*>(outBuffer), 1, toRead, handle, 0, FS_ERROR_FLAG_ALL);
    FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                handle, FS_ERROR_FLAG_ALL);

    if (readBytes >= 0) {
        if (outReadSize) *outReadSize = static_cast<size_t>(readBytes);
        return true;
    }
    return false;
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

        const char* pathsToTry[4] = { path, nullptr, nullptr, nullptr };
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
        if (getStat && getStat(impl::g_FSClient, impl::g_FSCmdBlock, m_Handle, &statBuf, 0xFFFFFFFF) == 0) {
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
                          m_Handle, &stat, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            m_Size = stat.size;
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
        int32_t got = readAt(impl::g_FSClient, impl::g_FSCmdBlock, buffer, 1, size, offset, m_Handle, 0, 0xFFFFFFFF);
        return got > 0 ? static_cast<uint32_t>(got) : 0;
#elif WIIXL_WIIU
        int32_t got = FSReadFileWithPos(reinterpret_cast<FSClient*>(impl::g_FSClient),
                                        reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock),
                                        reinterpret_cast<uint8_t*>(buffer), 1, size, offset, m_Handle, 0, FS_ERROR_FLAG_ALL);
        return got > 0 ? static_cast<uint32_t>(got) : 0;
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
        if (closeFile) closeFile(impl::g_FSClient, impl::g_FSCmdBlock, m_Handle, 0xFFFFFFFF);
#elif WIIXL_WIIU
        FSCloseFile(reinterpret_cast<FSClient*>(impl::g_FSClient),
                    reinterpret_cast<FSCmdBlock*>(impl::g_FSCmdBlock), m_Handle, FS_ERROR_FLAG_ALL);
#endif
        m_Open = false;
        m_Handle = 0;
        m_Size = 0;
    }

private:
    uint32_t m_Handle = 0;
    uint32_t m_Size = 0;
    bool m_Open = false;
};

} // namespace WiiXLaunch::FS
