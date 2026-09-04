#pragma once

// The .wxlm module loader.
//
// Reads a module off the filesystem, validates it, places it in memory,
// relocates it, resolves its imports against the surface registry, runs its
// .init_array, and calls its entry point at the right phase.
//
// ONE MODULE, deliberately. Multi-mod is a later stage and brings its own
// questions - shared heap accounting, hook chaining, load order. This gets one
// module end to end first.
//
// ORDER OF OPERATIONS, and it is the order for a reason:
//
//   1. integrity   size, magic, version, machine, endian, ABI, reserved, CRC
//   2. structure   every section offset and size inside the file
//   3. surfaces    everything the module requires is registered
//   4. memory      allocate, copy the payload, zero the bss
//   5. relocate    apply the table, resolving imports through the registry
//   6. flush       the payload is code we just wrote and are about to execute
//   7. init_array  static constructors, which nothing else will ever run
//   8. entry       at the phase the module asked for
//
// Nothing is allocated until the file has been proved intact, and nothing is
// executed until it has been proved resolvable. A module that fails at any step
// is skipped with a named reason and leaves nothing behind - one corrupt file
// must not cost the user their boot.
//
// EVERY STEP LOGS. A failure names itself: which module, which step, which
// symbol or surface. That is the whole difference between a bug report that can
// be acted on and "mods don't work".

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/fs.hpp>
#include <wiixlaunch/loader/wxlm.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/loader/core_surface.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Loader {

using Wxlm::Reject;
using Wxlm::RejectName;

// A module that made it all the way through. `image` is payload+bss, and the
// module's own code and data live inside it - so this must outlive the game,
// which it does: nothing is ever freed.
struct LoadedModule {
    char     id[17];
    uint8_t* image;
    uint32_t imageSize;
    uint32_t payloadSize;
    uint32_t entryOffset;
    uint32_t initArrayOffset;
    uint32_t initArrayCount;
    uint8_t  phase;
    bool     entryCalled;
    bool     valid;
};

namespace impl {

// One slot. Multi-mod is a later stage.
inline LoadedModule g_Module{};

// Streaming buffer for the integrity pass. Static rather than heap because the
// integrity check runs BEFORE anything is allocated - that is the point of it.
alignas(64) inline uint8_t g_Scratch[1024];

// The header, read whole and aligned so it can be overlaid. Safe to overlay
// only after the endian check, which is why that check comes first.
alignas(8) inline uint8_t g_HeaderBytes[sizeof(Wxlm::Header)];

inline void CopyId(char* dst, const char* src) {
    for (int i = 0; i < 16; ++i) dst[i] = src[i];
    dst[16] = '\0';
    for (int i = 0; i < 17; ++i) {
        if (dst[i] == '\0') break;
        if (dst[i] < 32 || dst[i] > 126) { dst[i] = '?'; }
    }
}

// Does [offset, offset+size) fit inside a file of `fileSize` bytes?
inline bool InFile(uint32_t offset, uint32_t size, uint32_t fileSize) {
    if (offset > fileSize) return false;
    if (size > fileSize) return false;
    return offset + size <= fileSize;   // no overflow: both are <= fileSize
}

} // namespace impl

inline const LoadedModule& Module() { return impl::g_Module; }

// Streams the whole file to check fileSize and the content CRC, before a single
// byte is allocated. This is what makes "skip it, log it, keep going" reliable:
// a partially-written file - the realistic corruption when a user drags a mod
// into a folder mid-copy - has the right length and a stale tail, which only a
// checksum can see.
inline Reject VerifyIntegrity(FS::File& file, const Wxlm::Header& h, const char* id) {
    const uint32_t actual = file.Size();
    if (actual != h.fileSize) {
        WIIXL_LOG("[loader:%s] %s: header says %u bytes, the file is %u - truncated "
                  "or still being written", id, RejectName(Reject::SizeMismatch),
                  h.fileSize, actual);
        return Reject::SizeMismatch;
    }

    uint32_t crc = 0xFFFFFFFFu;
    uint32_t pos = sizeof(Wxlm::Header);
    while (pos < h.fileSize) {
        uint32_t want = h.fileSize - pos;
        if (want > sizeof(impl::g_Scratch)) want = sizeof(impl::g_Scratch);
        const uint32_t got = file.ReadAt(pos, impl::g_Scratch, want);
        if (got == 0) {
            WIIXL_LOG("[loader:%s] %s: read stopped at offset %u of %u",
                      id, RejectName(Reject::ReadFailed), pos, h.fileSize);
            return Reject::ReadFailed;
        }
        for (uint32_t i = 0; i < got; ++i) {
            crc ^= impl::g_Scratch[i];
            crc = (crc >> 4) ^ Wxlm::impl::kCrcNibble[crc & 0x0Fu];
            crc = (crc >> 4) ^ Wxlm::impl::kCrcNibble[crc & 0x0Fu];
        }
        pos += got;
    }
    crc ^= 0xFFFFFFFFu;

    if (crc != h.contentCrc32) {
        WIIXL_LOG("[loader:%s] %s: content crc32 is 0x%08X, header says 0x%08X - the "
                  "file is corrupt or was copied while being written",
                  id, RejectName(Reject::BadChecksum), crc, h.contentCrc32);
        return Reject::BadChecksum;
    }
    return Reject::None;
}

// Header sanity, in the order that makes each check meaningful: nothing is
// interpreted before the thing that says how to interpret it has been checked.
inline Reject ValidateHeader(const Wxlm::Header& h, const char* id) {
    if (h.magic != Wxlm::kMagic) {
        WIIXL_LOG("[loader:%s] %s: magic is 0x%08X, expected 0x%08X",
                  id, RejectName(Reject::BadMagic), h.magic, Wxlm::kMagic);
        return Reject::BadMagic;
    }
    if (h.endian != static_cast<uint8_t>(Wxlm::kHostEndian)) {
        WIIXL_LOG("[loader:%s] %s: built %s-endian, this host is %s-endian",
                  id, RejectName(Reject::WrongEndian),
                  h.endian ? "big" : "little",
                  Wxlm::kHostEndian == Wxlm::Endian::Big ? "big" : "little");
        return Reject::WrongEndian;
    }
    if (h.machine != static_cast<uint16_t>(Wxlm::kHostMachine)) {
        WIIXL_LOG("[loader:%s] %s: built for machine %u, this host is %u - a module "
                  "for another console, not a corrupt file",
                  id, RejectName(Reject::WrongMachine), h.machine,
                  static_cast<uint16_t>(Wxlm::kHostMachine));
        return Reject::WrongMachine;
    }
    if (h.formatVersion > Wxlm::kFormatVersion) {
        WIIXL_LOG("[loader:%s] %s: format v%u, this host understands up to v%u",
                  id, RejectName(Reject::FormatTooNew), h.formatVersion,
                  Wxlm::kFormatVersion);
        return Reject::FormatTooNew;
    }
    if (h.abiVersion != Core::kAbiVersion) {
        WIIXL_LOG("[loader:%s] %s: built against ABI v%u, this host is v%u",
                  id, RejectName(Reject::AbiMismatch), h.abiVersion, Core::kAbiVersion);
        return Reject::AbiMismatch;
    }
    if (h.phase >= static_cast<uint8_t>(Wxlm::Phase::Count)) {
        WIIXL_LOG("[loader:%s] %s: phase %u is not one this host knows",
                  id, RejectName(Reject::BadPhase), h.phase);
        return Reject::BadPhase;
    }

    // Reserved fields are CHECKED, not ignored. A newer writer setting one and
    // an older loader ignoring it is how a mod half-works instead of failing.
    bool reservedSet = (h.reserved0 != 0);
    for (int i = 0; i < 4; ++i) reservedSet = reservedSet || (h.reserved1[i] != 0);
    if (reservedSet || h.declaredHookCount != 0 || h.declaredPatchCount != 0) {
        WIIXL_LOG("[loader:%s] %s: a reserved field is set, so this file wants "
                  "something this host does not implement yet. Refusing rather than "
                  "loading it partially.", id, RejectName(Reject::ReservedNotZero));
        return Reject::ReservedNotZero;
    }

    // Structure. Every section must lie inside the file.
    struct { uint32_t off, size; const char* what; } spans[] = {
        { h.payloadOffset,  h.payloadSize,                        "payload"  },
        { h.relocOffset,    h.relocCount * 8u,                    "relocs"   },
        { h.importOffset,   h.importCount * sizeof(Wxlm::ImportEntry),   "imports"  },
        { h.exportOffset,   h.exportCount * sizeof(Wxlm::ExportEntry),   "exports"  },
        { h.requiredOffset, h.requiredCount * sizeof(Wxlm::RequiredSurface), "required" },
        { h.stringOffset,   h.stringSize,                         "strings"  },
    };
    for (const auto& s : spans) {
        if (s.size != 0 && !impl::InFile(s.off, s.size, h.fileSize)) {
            WIIXL_LOG("[loader:%s] %s: %s section is [%u, %u) but the file is %u bytes",
                      id, RejectName(Reject::BadSectionBounds), s.what,
                      s.off, s.off + s.size, h.fileSize);
            return Reject::BadSectionBounds;
        }
    }
    if (h.entryOffset >= h.payloadSize) {
        WIIXL_LOG("[loader:%s] %s: entry is at +0x%X, payload is %u bytes",
                  id, RejectName(Reject::BadEntry), h.entryOffset, h.payloadSize);
        return Reject::BadEntry;
    }
    if (h.initArrayCount != 0 &&
        !impl::InFile(h.initArrayOffset, h.initArrayCount * 4u, h.payloadSize)) {
        WIIXL_LOG("[loader:%s] %s: .init_array is [%u, %u) but the payload is %u bytes",
                  id, RejectName(Reject::BadSectionBounds), h.initArrayOffset,
                  h.initArrayOffset + h.initArrayCount * 4u, h.payloadSize);
        return Reject::BadSectionBounds;
    }
    return Reject::None;
}

#if WIIXL_CEMU

// Loads one module. Returns Reject::None on success.
//
// The path is tried as given and through WiiXLaunch::FS's usual candidates, so
// "WiiXLaunch/mods/foo.wxlm" resolves the same way every other asset does.
inline Reject Load(const char* path) {
    WIIXL_LOG("[loader] opening %s", path);

    FS::File file;
    if (!file.Open(path)) {
        WIIXL_LOG("[loader] %s: could not open %s", RejectName(Reject::ReadFailed), path);
        return Reject::ReadFailed;
    }

    if (file.Size() < Wxlm::kMinFileSize) {
        WIIXL_LOG("[loader] %s: %u bytes, a header alone is %u",
                  RejectName(Reject::TooSmall), file.Size(), Wxlm::kMinFileSize);
        file.Close();
        return Reject::TooSmall;
    }

    if (file.ReadAt(0, impl::g_HeaderBytes, sizeof(Wxlm::Header)) != sizeof(Wxlm::Header)) {
        WIIXL_LOG("[loader] %s: could not read the header", RejectName(Reject::ReadFailed));
        file.Close();
        return Reject::ReadFailed;
    }

    // Safe to overlay: the endian check inside ValidateHeader runs before any
    // multi-byte field is trusted, and magic is byte-order agnostic.
    const Wxlm::Header& h = *reinterpret_cast<const Wxlm::Header*>(impl::g_HeaderBytes);

    char id[17];
    impl::CopyId(id, h.modId);

    Reject r = ValidateHeader(h, id);
    if (r != Reject::None) { file.Close(); return r; }

    WIIXL_LOG("[loader:%s] v%u.%u.%u  payload %u B, bss %u B, %u relocs, %u imports, "
              "phase %u", id, h.verMajor, h.verMinor, h.verPatch, h.payloadSize,
              h.bssSize, h.relocCount, h.importCount, h.phase);

    r = VerifyIntegrity(file, h, id);
    if (r != Reject::None) { file.Close(); return r; }
    WIIXL_LOG("[loader:%s] integrity OK (crc32 0x%08X over %u bytes)",
              id, h.contentCrc32, h.fileSize - static_cast<uint32_t>(sizeof(Wxlm::Header)));

    // --- surfaces, before anything is allocated ------------------------------
    //
    // A module that cannot possibly work is rejected before it is written into
    // memory, and the message names the surface rather than a symbol, because
    // "requires botw.gx2 v1, not present" is the actionable form.
    for (uint32_t i = 0; i < h.requiredCount; ++i) {
        Wxlm::RequiredSurface req{};
        const uint32_t off = h.requiredOffset + i * sizeof(req);
        if (file.ReadAt(off, &req, sizeof(req)) != sizeof(req)) {
            WIIXL_LOG("[loader:%s] %s: reading required surface %u",
                      id, RejectName(Reject::ReadFailed), i);
            file.Close();
            return Reject::ReadFailed;
        }
        char name[64] = {};
        const uint32_t nameAt = h.stringOffset + req.nameOffset;
        if (req.nameOffset >= h.stringSize ||
            file.ReadAt(nameAt, name, sizeof(name) - 1) == 0) {
            WIIXL_LOG("[loader:%s] %s: required surface %u has a bad name offset",
                      id, RejectName(Reject::BadSectionBounds), i);
            file.Close();
            return Reject::BadSectionBounds;
        }
        name[sizeof(name) - 1] = '\0';

        if (!Surface::Require(name, req.versionMajor, req.versionMinor)) {
            WIIXL_LOG("[loader:%s] %s: requires %s v%u.%u - see the surface list logged "
                      "at the load point for what this host offers",
                      id, RejectName(Reject::MissingSurface), name,
                      req.versionMajor, req.versionMinor);
            file.Close();
            return Reject::MissingSurface;
        }
        WIIXL_LOG("[loader:%s] requires %s v%u.%u - present", id, name,
                  req.versionMajor, req.versionMinor);
    }

    // --- memory --------------------------------------------------------------
    const uint32_t imageSize = h.payloadSize + h.bssSize;
    uint8_t* image = static_cast<uint8_t*>(Backend::AllocCemuHeap(imageSize, 64));
    if (!image) {
        WIIXL_LOG("[loader:%s] %s: wanted %u B (payload %u + bss %u); the host heap has "
                  "%u of %u bytes used. heapRequest was %u, but a request is not a grant "
                  "and there is nothing left to grant.",
                  id, RejectName(Reject::NoMemory), imageSize, h.payloadSize, h.bssSize,
                  static_cast<uint32_t>(Backend::CemuHeapUsed()),
                  static_cast<uint32_t>(Backend::CemuHeapLimit()), h.heapRequest);
        file.Close();
        return Reject::NoMemory;
    }

    if (file.ReadAt(h.payloadOffset, image, h.payloadSize) != h.payloadSize) {
        WIIXL_LOG("[loader:%s] %s: short read of the %u-byte payload",
                  id, RejectName(Reject::ReadFailed), h.payloadSize);
        file.Close();
        return Reject::ReadFailed;
    }
    for (uint32_t i = 0; i < h.bssSize; ++i) image[h.payloadSize + i] = 0;
    WIIXL_LOG("[loader:%s] image at %p, %u B (payload %u, bss %u zeroed)",
              id, image, imageSize, h.payloadSize, h.bssSize);

    // --- relocate ------------------------------------------------------------
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    uint32_t importCount = 0;

    for (uint32_t i = 0; i < h.relocCount; ++i) {
        uint32_t pair[2];
        if (file.ReadAt(h.relocOffset + i * 8u, pair, 8) != 8) {
            WIIXL_LOG("[loader:%s] %s: reading relocation %u",
                      id, RejectName(Reject::ReadFailed), i);
            file.Close();
            return Reject::ReadFailed;
        }
        const uint32_t kind = pair[0] >> 24;
        const uint32_t offset = pair[0] & 0x00FFFFFFu;
        uint32_t value = pair[1];

        if (offset + 4 > h.payloadSize) {
            WIIXL_LOG("[loader:%s] %s: relocation %u targets +0x%X, payload is %u B",
                      id, RejectName(Reject::BadRelocation), i, offset, h.payloadSize);
            file.Close();
            return Reject::BadRelocation;
        }

        if (kind == static_cast<uint32_t>(Wxlm::RelocKind::Import)) {
            // The one genuinely new kind: `value` indexes the import table and
            // resolves through the registry instead of adding base.
            if (value >= h.importCount) {
                WIIXL_LOG("[loader:%s] %s: relocation %u names import %u of %u",
                          id, RejectName(Reject::BadRelocation), i, value, h.importCount);
                file.Close();
                return Reject::BadRelocation;
            }
            Wxlm::ImportEntry imp{};
            if (file.ReadAt(h.importOffset + value * sizeof(imp), &imp, sizeof(imp)) != sizeof(imp)) {
                WIIXL_LOG("[loader:%s] %s: reading import %u",
                          id, RejectName(Reject::ReadFailed), value);
                file.Close();
                return Reject::ReadFailed;
            }
            char surfaceName[64] = {};
            char symbolName[64] = {};
            file.ReadAt(h.stringOffset + imp.surfaceNameOffset, surfaceName, sizeof(surfaceName) - 1);
            file.ReadAt(h.stringOffset + imp.symbolNameOffset, symbolName, sizeof(symbolName) - 1);
            surfaceName[sizeof(surfaceName) - 1] = '\0';
            symbolName[sizeof(symbolName) - 1] = '\0';

            const void* fn = Surface::Resolve(surfaceName, imp.symbolHash);
            if (!fn) {
                WIIXL_LOG("[loader:%s] %s: %s does not export %s (hash 0x%08X). The "
                          "surface is present, the symbol is not - a version mismatch "
                          "rather than a missing module.",
                          id, RejectName(Reject::UnresolvedImport), surfaceName,
                          symbolName, imp.symbolHash);
                file.Close();
                return Reject::UnresolvedImport;
            }
            *reinterpret_cast<uint32_t*>(base + offset) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn));
            ++importCount;
            continue;
        }

        // Kinds 0-3 are exactly WiiXLaunch_Cemu_Relocate's cases, applied to the
        // module's own base instead of the host's.
        value += static_cast<uint32_t>(base);
        uint8_t* site = image + offset;
        switch (kind) {
            case 0: *reinterpret_cast<uint32_t*>(site) = value; break;
            case 1: *reinterpret_cast<uint16_t*>(site) =
                        static_cast<uint16_t>(((value + 0x8000) >> 16) & 0xFFFF); break;
            case 2: *reinterpret_cast<uint16_t*>(site) =
                        static_cast<uint16_t>((value >> 16) & 0xFFFF); break;
            case 3: *reinterpret_cast<uint16_t*>(site) =
                        static_cast<uint16_t>(value & 0xFFFF); break;
            default:
                WIIXL_LOG("[loader:%s] %s: relocation %u has unknown kind %u",
                          id, RejectName(Reject::BadRelocation), i, kind);
                file.Close();
                return Reject::BadRelocation;
        }
    }
    file.Close();
    WIIXL_LOG("[loader:%s] relocated %u entries (%u resolved through the registry)",
              id, h.relocCount, importCount);

    // --- flush ---------------------------------------------------------------
    // We just wrote code we are about to execute. Without this it runs from a
    // stale instruction cache, which fails in a way that looks nothing like a
    // loader bug.
    Backend::FlushCache(base, imageSize);

    // --- record --------------------------------------------------------------
    LoadedModule& m = impl::g_Module;
    impl::CopyId(m.id, h.modId);
    m.image = image;
    m.imageSize = imageSize;
    m.payloadSize = h.payloadSize;
    m.entryOffset = h.entryOffset;
    m.initArrayOffset = h.initArrayOffset;
    m.initArrayCount = h.initArrayCount;
    m.phase = h.phase;
    m.entryCalled = false;
    m.valid = true;

    // --- init_array ----------------------------------------------------------
    // Nothing else will ever run these: the flat build has no .init_array output
    // section and the bootstrap never walks one, so a module's static
    // constructors exist only if the loader calls them.
    if (h.initArrayCount != 0) {
        WIIXL_LOG("[loader:%s] running %u .init_array entries", id, h.initArrayCount);
        auto* fns = reinterpret_cast<uint32_t*>(base + h.initArrayOffset);
        for (uint32_t i = 0; i < h.initArrayCount; ++i) {
            if (fns[i] == 0) continue;
            reinterpret_cast<void (*)()>(fns[i])();
        }
        WIIXL_LOG("[loader:%s] .init_array done", id);
    }

    WIIXL_LOG("[loader:%s] LOADED, entry at %p, waiting for phase %u",
              id, reinterpret_cast<void*>(base + h.entryOffset), h.phase);
    return Reject::None;
}

// Calls the entry point of any loaded module whose phase matches.
//
// Separate from Load because PostGx2 fires from the graphics module's
// OnInitialized callback, and base must not know that GX2 exists - the project
// or the game module calls this when its phase arrives.
inline void RunPhase(Wxlm::Phase phase) {
    LoadedModule& m = impl::g_Module;
    if (!m.valid || m.entryCalled) return;
    if (m.phase != static_cast<uint8_t>(phase)) return;

    auto entry = reinterpret_cast<void (*)()>(
        reinterpret_cast<uintptr_t>(m.image) + m.entryOffset);
    WIIXL_LOG("[loader:%s] phase %u reached, calling entry at %p",
              m.id, m.phase, reinterpret_cast<void*>(entry));
    m.entryCalled = true;
    entry();
    WIIXL_LOG("[loader:%s] entry returned", m.id);
}

#else

// Switch and Wii U reach their modules through their own backends; the loader
// body is Cemu-only until those are probed. Says so rather than silently
// doing nothing.
inline Reject Load(const char* path) {
    WIIXL_LOG("[loader] not implemented on this target yet (%s)", path);
    return Reject::ReadFailed;
}
inline void RunPhase(Wxlm::Phase) {}

#endif

} // namespace WiiXLaunch::Loader
