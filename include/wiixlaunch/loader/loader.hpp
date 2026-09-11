#pragma once

// The .wxlm module loader.
//
// Reads a module off the filesystem, validates it, places it in memory,
// relocates it, resolves its imports against the surface registry, runs its
// .init_array, and calls its entry point at the right phase.
//
// SEVERAL MODULES. LoadAll enumerates a directory and loads every .wxlm in it,
// in lexical filename order, each into its own bounded arena grant and each
// attributed by mod id when it installs a hook. That order is a specification,
// not an enumeration artefact - see LoadAll and docs/loader.md.
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
#include <wiixlaunch/loader/arena.hpp>
#include <wiixlaunch/hook_manager.hpp>
#include <wiixlaunch/patches.hpp>
#include <wiixlaunch/mod_fs.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Loader {

using Wxlm::Reject;
using Wxlm::RejectName;

// A .wxlm filename and the path it is reached through. Both bounded; nothing
// here allocates to hold a name.
constexpr uint32_t kMaxNameLen = 64;
constexpr uint32_t kMaxPathLen = 192;

// How the loader publishes code it has written.
//
// THERE IS NO ALLOCATION HOOK ANY MORE, and removing it was not tidying. Stage
// 5 moved the image onto the module own sub-arena (Arena::AllocIn at the
// placement site), which left AllocFn set but never called - and
// tools/loader_fuzz was installing a red-zoned allocator through it. Its
// canaries were then bytes nothing could reach, so CheckRedZones() passed
// without testing anything: a dead hook turned a real test into a vacuous one,
// silently, and the case count did not change. The fuzzer now poisons the
// arena reservation itself and checks the loader wrote only inside the grant,
// which is both a live check and the actual stage-5 invariant.
//
// The flush hook stays because it is still called, and because it is what lets
// the fuzzer run this exact code natively: parsing attacker-shaped data is
// ordinary logic and testing it should not need a console or a boot.
using FlushFn = void (*)(uintptr_t addr, uint32_t size);

namespace impl {

#if WIIXL_CEMU
inline void DefaultFlush(uintptr_t addr, uint32_t size) {
    Backend::FlushCache(addr, size);
}
#else
inline void DefaultFlush(uintptr_t, uint32_t) {}
#endif

inline FlushFn g_Flush = &DefaultFlush;

} // namespace impl

// Replaces the cache-flush. Must be set before Load on a host that needs it.
inline void SetFlushHook(FlushFn flush) {
    impl::g_Flush = flush ? flush : &impl::DefaultFlush;
}

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
    Arena::SubArena* arena;
};

namespace impl {

// One slot per loaded module. Bounded by the arena's own module limit, since a
// module that cannot be granted memory cannot be loaded anyway.
inline LoadedModule g_Modules[Arena::kMaxModules]{};
inline uint32_t g_ModuleCount = 0;

// A rejected module must not leave a half-filled entry behind for RunPhase to
// walk into, so g_ModuleCount is only incremented once a module is completely
// recorded. Until then the slot is scratch and nothing reads it.

// Streaming buffer for the integrity pass. Static rather than heap because the
// integrity check runs BEFORE anything is allocated - that is the point of it.
alignas(64) inline uint8_t g_Scratch[1024];

// The header, read whole and aligned so it can be overlaid. Safe to overlay
// only after the endian check, which is why that check comes first.
//
// alignas(64), not 8: coreinit's FSReadFile family requires a 64-byte aligned
// destination. At alignas(8) this happened to work - a static usually lands
// more aligned than it asks for - which is the worst kind of working.
alignas(64) inline uint8_t g_HeaderBytes[sizeof(Wxlm::Header)];

// Filenames and paths. Bounded, because everything here is.
inline void CopyName(char* dst, const char* src) {
    uint32_t i = 0;
    for (; i + 1 < kMaxNameLen && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Byte-wise ascending. Deliberately NOT case-insensitive and not locale-aware:
// the order has to be predictable from the bytes of a filename on any host, and
// "predictable" beats "friendly" when it is the user's only lever over which
// mod runs first. Uppercase sorts before lowercase; docs/loader.md says so.
inline bool NameLess(const char* a, const char* b) {
    for (uint32_t i = 0; i < kMaxNameLen; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca != cb) return ca < cb;
        if (ca == 0) return false;          // equal
    }
    return false;
}

inline void JoinPath(char* dst, const char* dir, const char* name) {
    uint32_t n = 0;
    for (; n + 1 < kMaxPathLen && dir && dir[n]; ++n) dst[n] = dir[n];
    if (n && dst[n - 1] != '/' && n + 1 < kMaxPathLen) dst[n++] = '/';
    for (uint32_t i = 0; name && name[i] && n + 1 < kMaxPathLen; ++i) dst[n++] = name[i];
    dst[n] = '\0';
}

inline bool EndsWithWxlm(const char* name) {
    uint32_t n = 0;
    while (name[n] && n < kMaxNameLen) ++n;
    if (n < 5) return false;
    return name[n - 5] == '.' && name[n - 4] == 'w' && name[n - 3] == 'x'
        && name[n - 2] == 'l' && name[n - 1] == 'm';
}

inline void CopyId(char* dst, const char* src) {
    for (int i = 0; i < 16; ++i) dst[i] = src[i];
    dst[16] = '\0';
    for (int i = 0; i < 17; ++i) {
        if (dst[i] == '\0') break;
        if (dst[i] < 32 || dst[i] > 126) { dst[i] = '?'; }
    }
}

// Does [offset, offset+size) fit inside a file of `fileSize` bytes?
//
// Everything is widened to 64-bit first, deliberately. A section's size is
// count * sizeof(entry), and a corrupt or hostile count can wrap a 32-bit
// multiply to something small that then passes a naive bounds check - which is
// precisely the check standing between a bad file and a relocation into
// arbitrary memory. Doing the arithmetic where it cannot wrap makes the check
// mean what it says.
//
// The AArch64 build is what forced this into the open: size_t is 64-bit there,
// so the narrowing was a compile error, where on 32-bit PowerPC it would have
// silently truncated.
inline bool InFile(uint64_t offset, uint64_t size, uint64_t fileSize) {
    if (offset > fileSize) return false;
    return offset + size <= fileSize;
}

// Reads through the aligned scratch, for destinations that are neither
// 64-byte aligned nor a multiple of 64 - which is every struct and string this
// loader reads.
//
// WHY THIS EXISTS. coreinit's FSReadFile family requires a 64-BYTE ALIGNED
// buffer and, unless it is reading the tail of the file, a size that is a
// multiple of 64. wiixlaunch/fs.hpp says so at FS::File::ReadAt. Reading an
// 8-byte RequiredSurface into a stack local satisfies neither, and Cemu answers
// with "FS handleAsyncResult(): unexpected error ffffffff" - a failure at the
// FS layer, reported by the loader as READ-FAILED, with nothing about
// alignment anywhere in it.
//
// The fuzzer could not have found this: its reader is a byte array with no
// alignment requirement at all. It took a boot. tools/loader_fuzz's
// MemoryReader now enforces the same constraints, so the next one is caught on
// the host.
//
// Returns the number of bytes delivered, like ReadAt, so a short read at the
// end of a file stays distinguishable from a failure.
template <typename Reader>
inline uint32_t ReadVia(Reader& file, uint32_t offset, void* dst, uint32_t size) {
    if (size == 0 || size > sizeof(g_Scratch)) return 0;
    const uint32_t fileSize = file.Size();
    if (offset >= fileSize) return 0;

    uint32_t want = (size + 63u) & ~63u;
    const uint32_t avail = fileSize - offset;
    if (want > avail) want = avail;          // tail read: short is allowed

    const uint32_t got = file.ReadAt(offset, g_Scratch, want);
    const uint32_t n = got < size ? got : size;
    for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t*>(dst)[i] = g_Scratch[i];
    return n;
}

// Reads into a destination that IS 64-byte aligned - the module image. Whole
// 64-byte chunks go straight in; only the ragged tail is bounced, so a large
// payload is still one read.
template <typename Reader>
inline bool ReadAligned(Reader& file, uint32_t offset, uint8_t* dst, uint32_t size) {
    const uint32_t whole = size & ~63u;
    if (whole != 0 && file.ReadAt(offset, dst, whole) != whole) return false;
    const uint32_t rest = size - whole;
    if (rest != 0 && ReadVia(file, offset + whole, dst + whole, rest) != rest) return false;
    return true;
}

} // namespace impl

// Forgets every loaded module. For a host test that runs many loads in one
// process; nothing in a real host calls it, because a module is never unloaded.
//
// tools/loader_fuzz needs this and did not have it: g_ModuleCount survived
// across cases, so after Arena::kMaxModules successful loads every later valid
// module was rejected NO-MEMORY. The suite still ran 1171 cases and still
// reported PASS - only the accepted/rejected SPLIT moved, from 293 accepted to
// 8. A total that cannot move is not a liveness check; see the accepted-count
// floor in the fuzzer.
inline void ResetForTest() {
    impl::g_ModuleCount = 0;
    for (uint32_t i = 0; i < Arena::kMaxModules; ++i) {
        impl::g_Modules[i] = LoadedModule{};
    }
}

inline uint32_t ModuleCount() { return impl::g_ModuleCount; }
inline const LoadedModule* Module(uint32_t i) {
    return i < impl::g_ModuleCount ? &impl::g_Modules[i] : nullptr;
}

// Streams the whole file to check fileSize and the content CRC, before a single
// byte is allocated. This is what makes "skip it, log it, keep going" reliable:
// a partially-written file - the realistic corruption when a user drags a mod
// into a folder mid-copy - has the right length and a stale tail, which only a
// checksum can see.
template <typename Reader>
inline Reject VerifyIntegrity(Reader& file, const Wxlm::Header& h, const char* id) {
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
    // Zero is not a version this or any host ever wrote. Only ">" was checked,
    // so a single bit flip turning v1 into v0 was accepted - found by the
    // fuzzer's header sweep.
    if (h.formatVersion == 0) {
        WIIXL_LOG("[loader:%s] %s: format version 0, which no writer produces",
                  id, RejectName(Reject::FormatTooNew));
        return Reject::FormatTooNew;
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
    // Every reserved field, not only the counts. The OFFSETS were unchecked, so
    // a flip in declaredHookOffset or declaredPatchOffset was accepted while
    // the matching count flip was refused - the fuzzer's per-field attribution
    // is what made that visible. A reserved field is reserved whether or not
    // this host would have read it.
    //
    // declaredPatch* is IMPLEMENTED as of stage 7 and is no longer reserved.
    // declaredHook* still is - a mod installs hooks through wiixl.core at
    // runtime, and declaring them as data is a separate decision nobody has
    // made yet.
    bool reservedSet = (h.reserved0 != 0);
    for (int i = 0; i < 4; ++i) reservedSet = reservedSet || (h.reserved1[i] != 0);
    reservedSet = reservedSet || h.declaredHookOffset != 0;
    if (reservedSet || h.declaredHookCount != 0) {
        WIIXL_LOG("[loader:%s] %s: a reserved field is set, so this file wants "
                  "something this host does not implement yet. Refusing rather than "
                  "loading it partially.", id, RejectName(Reject::ReservedNotZero));
        return Reject::ReservedNotZero;
    }

    // The '_' id space belongs to the host - WiiXLaunch/mods/_host/ holds the
    // host's own resources, and a module claiming an id there could read or
    // shadow them. Reserving the whole PREFIX rather than one name means a
    // future reserved id needs no new check here.
    if (ModFS::IsReservedId(id)) {
        WIIXL_LOG("[loader:%s] %s: ids beginning with '%c' are reserved for the host",
                  id, RejectName(Reject::ReservedModId), ModFS::kReservedPrefix);
        return Reject::ReservedModId;
    }

    // Structure. Every section must lie inside the file.
    struct { uint64_t off, size; const char* what; } spans[] = {
        { h.declaredPatchOffset,
          static_cast<uint64_t>(h.declaredPatchCount) * sizeof(Wxlm::PatchEntry),
          "patches" },
        { h.payloadOffset,  static_cast<uint64_t>(h.payloadSize),   "payload"  },
        { h.relocOffset,    static_cast<uint64_t>(h.relocCount) * 8ull, "relocs" },
        { h.importOffset,   static_cast<uint64_t>(h.importCount)
                              * sizeof(Wxlm::ImportEntry),          "imports"  },
        { h.exportOffset,   static_cast<uint64_t>(h.exportCount)
                              * sizeof(Wxlm::ExportEntry),          "exports"  },
        { h.requiredOffset, static_cast<uint64_t>(h.requiredCount)
                              * sizeof(Wxlm::RequiredSurface),      "required" },
        { h.stringOffset,   static_cast<uint64_t>(h.stringSize),    "strings"  },
    };
    for (const auto& sp : spans) {
        if (sp.size == 0) continue;

        if (!impl::InFile(sp.off, sp.size, h.fileSize)) {
            WIIXL_LOG("[loader:%s] %s: %s section is [%u, +%u) but the file is %u bytes",
                      id, RejectName(Reject::BadSectionBounds), sp.what,
                      static_cast<uint32_t>(sp.off), static_cast<uint32_t>(sp.size),
                      h.fileSize);
            return Reject::BadSectionBounds;
        }

        // A section may not start inside the header. Nothing well-formed does,
        // and allowing it means a table can be made to read header bytes as
        // entries - offsets and counts of the loader's own choosing.
        if (sp.off < sizeof(Wxlm::Header)) {
            WIIXL_LOG("[loader:%s] %s: %s section starts at %u, inside the %u-byte "
                      "header", id, RejectName(Reject::BadSectionBounds), sp.what,
                      static_cast<uint32_t>(sp.off),
                      static_cast<uint32_t>(sizeof(Wxlm::Header)));
            return Reject::BadSectionBounds;
        }
    }

    // No two sections may overlap. Each is bounded and each starts after the
    // header, but that still permits a string blob sitting on top of the reloc
    // table, where one field's meaning is read out of another's bytes. Nothing
    // a writer produces overlaps, so refusing it costs nothing and removes a
    // whole class of confusion between tables.
    //
    // Found by tools/loader_fuzz: exportCount=1 with an unset exportOffset gave
    // a table lying on the header, in bounds and accepted.
    for (uint32_t i = 0; i < sizeof(spans) / sizeof(spans[0]); ++i) {
        if (spans[i].size == 0) continue;
        for (uint32_t j = i + 1; j < sizeof(spans) / sizeof(spans[0]); ++j) {
            if (spans[j].size == 0) continue;
            const uint64_t aStart = spans[i].off, aEnd = aStart + spans[i].size;
            const uint64_t bStart = spans[j].off, bEnd = bStart + spans[j].size;
            if (aStart < bEnd && bStart < aEnd) {
                WIIXL_LOG("[loader:%s] %s: %s [%u, +%u) overlaps %s [%u, +%u)",
                          id, RejectName(Reject::BadSectionBounds),
                          spans[i].what, static_cast<uint32_t>(aStart),
                          static_cast<uint32_t>(spans[i].size),
                          spans[j].what, static_cast<uint32_t>(bStart),
                          static_cast<uint32_t>(spans[j].size));
                return Reject::BadSectionBounds;
            }
        }
    }
    if (h.entryOffset >= h.payloadSize) {
        WIIXL_LOG("[loader:%s] %s: entry is at +0x%X, payload is %u bytes",
                  id, RejectName(Reject::BadEntry), h.entryOffset, h.payloadSize);
        return Reject::BadEntry;
    }
    if (h.initArrayCount != 0 &&
        !impl::InFile(h.initArrayOffset,
                      static_cast<uint64_t>(h.initArrayCount) * 4ull, h.payloadSize)) {
        WIIXL_LOG("[loader:%s] %s: .init_array is [%u, %u) but the payload is %u bytes",
                  id, RejectName(Reject::BadSectionBounds), h.initArrayOffset,
                  h.initArrayOffset + h.initArrayCount * 4u, h.payloadSize);
        return Reject::BadSectionBounds;
    }
    return Reject::None;
}

// Loads one module from anything with Size() and ReadAt().
//
// Templated on the reader so the same code serves a file on a console and a
// byte array in a test. FS::File satisfies it; so does tools/loader_fuzz's
// memory reader. The alternative - reading the whole file into a buffer first -
// would have been simpler to test and would have doubled peak memory in a code
// cave under 4 MB.
//
// Does NOT close the reader; the caller owns it.
template <typename Reader>
inline Reject LoadFrom(Reader& file) {
    if (file.Size() < Wxlm::kMinFileSize) {
        WIIXL_LOG("[loader] %s: %u bytes, a header alone is %u",
                  RejectName(Reject::TooSmall), file.Size(), Wxlm::kMinFileSize);
        return Reject::TooSmall;
    }

    if (impl::ReadVia(file, 0, impl::g_HeaderBytes, sizeof(Wxlm::Header)) != sizeof(Wxlm::Header)) {
        WIIXL_LOG("[loader] %s: could not read the header", RejectName(Reject::ReadFailed));
        return Reject::ReadFailed;
    }

    // Safe to overlay: the endian check inside ValidateHeader runs before any
    // multi-byte field is trusted, and magic is byte-order agnostic.
    const Wxlm::Header& h = *reinterpret_cast<const Wxlm::Header*>(impl::g_HeaderBytes);

    char id[17];
    impl::CopyId(id, h.modId);

    // Claimed size against delivered size FIRST. Structure is checked against
    // h.fileSize, so a shrunken fileSize would otherwise trip a section bound
    // and report BAD-SECTION-BOUNDS for what is really a truncated file. The
    // fuzzer caught that: the rejection was right and the diagnosis was not.
    if (file.Size() != h.fileSize) {
        WIIXL_LOG("[loader:%s] %s: header says %u bytes, the file is %u - truncated, "
                  "still being written, or not the file the header describes",
                  id, RejectName(Reject::SizeMismatch), h.fileSize, file.Size());
        return Reject::SizeMismatch;
    }

    Reject r = ValidateHeader(h, id);
    if (r != Reject::None) return r;

    WIIXL_LOG("[loader:%s] v%u.%u.%u  payload %u B, bss %u B, %u relocs, %u imports, "
              "phase %u", id, h.verMajor, h.verMinor, h.verPatch, h.payloadSize,
              h.bssSize, h.relocCount, h.importCount, h.phase);

    r = VerifyIntegrity(file, h, id);
    if (r != Reject::None) return r;
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
        if (impl::ReadVia(file, off, &req, sizeof(req)) != sizeof(req)) {
            WIIXL_LOG("[loader:%s] %s: reading required surface %u",
                      id, RejectName(Reject::ReadFailed), i);
            return Reject::ReadFailed;
        }
        char name[64] = {};
        const uint32_t nameAt = h.stringOffset + req.nameOffset;
        if (req.nameOffset >= h.stringSize ||
            impl::ReadVia(file, nameAt, name, sizeof(name) - 1) == 0) {
            WIIXL_LOG("[loader:%s] %s: required surface %u has a bad name offset",
                      id, RejectName(Reject::BadSectionBounds), i);
            return Reject::BadSectionBounds;
        }
        name[sizeof(name) - 1] = '\0';

        if (!Surface::Require(name, req.versionMajor, req.versionMinor)) {
            WIIXL_LOG("[loader:%s] %s: requires %s v%u.%u - see the surface list logged "
                      "at the load point for what this host offers",
                      id, RejectName(Reject::MissingSurface), name,
                      req.versionMajor, req.versionMinor);
            return Reject::MissingSurface;
        }
        WIIXL_LOG("[loader:%s] requires %s v%u.%u - present", id, name,
                  req.versionMajor, req.versionMinor);
    }

    //
    // payloadSize + bssSize in 32 bits can WRAP, and the consequence is not a
    // failed allocation - it is a successful small one followed by a zeroing
    // loop that runs bssSize times. bssSize = 0xFFFFFFFF with a 64-byte payload
    // gives an imageSize of 63: the allocation succeeds, and then the loop
    // writes four gigabytes starting inside it.
    //
    // Single-bit flips cannot produce that value, so the header sweep never hit
    // it; it turned up while working out why the sweep's NO-MEMORY results
    // disagreed with the oracle. The arithmetic is done in 64 bits and the
    // result is bounded before anything is allocated.
    const uint64_t imageSize64 =
        static_cast<uint64_t>(h.payloadSize) + static_cast<uint64_t>(h.bssSize);
    if (imageSize64 > 0xFFFFFFFFull) {
        WIIXL_LOG("[loader:%s] %s: payload %u + bss %u does not fit a 32-bit size",
                  id, RejectName(Reject::BadSectionBounds), h.payloadSize, h.bssSize);
        return Reject::BadSectionBounds;
    }
    const uint32_t imageSize = static_cast<uint32_t>(imageSize64);

    // The module's own bounded piece, acquired AFTER the size arithmetic above
    // has been validated and BEFORE anything is placed. Both halves matter: a
    // module whose sizes do not add up must be refused for that reason rather
    // than for running out of memory, and a module that cannot be given what it
    // needs must be refused without having been partially written anywhere.
    //
    // Acquiring first was the original order and the fuzzer rejected it - every
    // malformed-size case came back NO-MEMORY instead of naming the real fault.
    //
    // The module's own bounded piece, acquired BEFORE anything is placed, so a
    // module that cannot be given what it needs is refused without having been
    // partially written anywhere.
    //
    // The image itself is charged to that piece too. A module's footprint is
    // its code plus whatever it allocates, and leaving the image outside the
    // bound would mean a large module quietly costing more than its grant says.
    // 64 bytes on PowerPC - cache-line, and what every FS destination wants.
    //
    // 4096 on AArch64, and it is a CORRECTNESS requirement rather than a
    // performance one. An aarch64 module addresses its own data with adrp+add,
    // which the linker has already resolved and which stays correct after the
    // image moves ONLY if it moves by a whole number of pages: adrp computes
    // (PC & ~0xFFF) + imm, so a sub-page shift changes the page difference the
    // linker baked in. There is no relocation on those instructions to fix it
    // up afterwards, so nothing would report the damage - the module would just
    // read from one page away.
    //
    // Declared up here because the GRANT has to account for it; see below.
    constexpr uint32_t kImageAlign =
        (Wxlm::kHostMachine == Wxlm::Machine::AArch64) ? 4096u : 64u;

    Arena::SubArena* sub = nullptr;
    {
        const uint32_t need = h.payloadSize + h.bssSize;
        uint32_t request = h.heapRequest;
        if (request != 0) {
            // A stated requirement covers the module's own allocations; the
            // image has to fit as well, so the host reserves both.
            const uint64_t total = static_cast<uint64_t>(request) + need;
            if (total > 0xFFFFFFFFull) {
                WIIXL_LOG("[loader:%s] %s: heapRequest %u plus a %u-byte image does not "
                          "fit a 32-bit size", id, RejectName(Reject::BadSectionBounds),
                          request, need);
                return Reject::BadSectionBounds;
            }
            request = static_cast<uint32_t>(total);
        }

        // THE FLOOR IS THE IMAGE PLUS ITS ALIGNMENT PADDING, not the image.
        //
        // Sub-arenas are carved from the top of the arena and land wherever
        // the running total leaves them; the image inside one is then aligned
        // to kImageAlign. So a grant of exactly `need` fits only if the
        // sub-arena happens to start aligned, and AIPuppet on Switch is what
        // happens when it does not: granted 88064 at 0xb393800, aligned up to
        // 0xb394000, and the last 2048 bytes no longer fit. Refused for lack
        // of memory with 874 KB free, which is a true sentence and a useless
        // one. Reserving the padding as well makes the grant sufficient
        // wherever it lands.
        const uint64_t floor64 =
            static_cast<uint64_t>(need) + (kImageAlign - 1u);
        if (floor64 > 0xFFFFFFFFull) {
            WIIXL_LOG("[loader:%s] %s: a %u-byte image plus alignment does not fit "
                      "a 32-bit size", id, RejectName(Reject::BadSectionBounds), need);
            return Reject::BadSectionBounds;
        }
        const Arena::Grant g = Arena::Acquire(
            id, request, static_cast<uint32_t>(floor64), &sub);
        if (g != Arena::Grant::Ok) {
            WIIXL_LOG("[loader:%s] %s: arena said %s", id,
                      RejectName(Reject::NoMemory), Arena::GrantName(g));
            return Reject::NoMemory;
        }
        if (Arena::GrantedTo(sub) < need) {
            WIIXL_LOG("[loader:%s] %s: granted %u bytes but the image alone is %u "
                      "(payload %u + bss %u)", id, RejectName(Reject::NoMemory),
                      Arena::GrantedTo(sub), need, h.payloadSize, h.bssSize);
            WIIXL_LOG("[loader:%s] %s", id, Arena::kSharedArenaNote);
            return Reject::NoMemory;
        }
    }

    // Allocations are charged to this module from here until its entry returns.
    Arena::SetCurrent(sub);
    uint8_t* image = static_cast<uint8_t*>(Arena::AllocIn(*sub, imageSize, kImageAlign));
    if (!image) {
        WIIXL_LOG("[loader:%s] %s: wanted %u B for the image (payload %u + bss %u) "
                  "inside a %u-byte grant with %u used",
                  id, RejectName(Reject::NoMemory), imageSize, h.payloadSize, h.bssSize,
                  Arena::GrantedTo(sub), Arena::UsedIn(sub));
        WIIXL_LOG("[loader:%s] %s", id, Arena::kSharedArenaNote);
        Arena::SetCurrent(nullptr);
        return Reject::NoMemory;
    }

    // POISON BEFORE PLACING, so that "the loader zeroed my .bss" is a claim the
    // module can actually test.
    //
    // THE FOURTH RULE (docs/modules.md). The sample module checks its .bss is
    // zero and reports success - but freshly carved arena memory is very often
    // already zero, so that check passed whether or not the zeroing step below
    // ran. Delete the memset and the module would still have said "bss was
    // zeroed". A check that cannot tell "correct" from "never happened" is not
    // a check.
    //
    // Filling the image region with a non-zero pattern first fixes that in the
    // only way that matters: the payload copy and the bss zero each have to
    // overwrite it, and if either step were removed the module would see
    // kImagePoison and say so. Bounded by imageSize, so this is proportional to
    // the module, not to its grant.
    // 0xCD, deliberately NOT the 0xA5 tools/loader_fuzz fills the arena with.
    // The fuzzer distinguishes untouched arena (0xA5) from memory the loader
    // wrote; if both poisons were the same byte the two would be
    // indistinguishable and its liveness check would be reasoning about the
    // wrong thing.
    // EVERY WRITE from here on goes through the alias, and every VALUE stored
    // is still computed from `image`. On Cemu and Wii U the two are the same
    // pointer; on Switch the image lives in the host's .text, which cannot be
    // written to, and Arena hands back a writable view of the same pages. Get
    // this backwards and a module would be relocated to point into a mapping
    // that is not executable.
    uint8_t* wimage = static_cast<uint8_t*>(Arena::Writable(image));

    constexpr uint8_t kImagePoison = 0xCD;
    for (uint32_t i = 0; i < imageSize; ++i) wimage[i] = kImagePoison;

    if (!impl::ReadAligned(file, h.payloadOffset, wimage, h.payloadSize)) {
        WIIXL_LOG("[loader:%s] %s: short read of the %u-byte payload",
                  id, RejectName(Reject::ReadFailed), h.payloadSize);
        Arena::SetCurrent(nullptr);
        return Reject::ReadFailed;
    }
    for (uint32_t i = 0; i < h.bssSize; ++i) wimage[h.payloadSize + i] = 0;
    WIIXL_LOG("[loader:%s] image at %p, %u B (payload %u read, bss %u zeroed over "
              "0x%02X poison)",
              id, image, imageSize, h.payloadSize, h.bssSize, kImagePoison);

    // --- relocate ------------------------------------------------------------
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    const uintptr_t wbase = reinterpret_cast<uintptr_t>(wimage);
    uint32_t importCount = 0;

    for (uint32_t i = 0; i < h.relocCount; ++i) {
        uint32_t pair[2];
        if (impl::ReadVia(file, h.relocOffset + i * 8u, pair, 8) != 8) {
            WIIXL_LOG("[loader:%s] %s: reading relocation %u",
                      id, RejectName(Reject::ReadFailed), i);
            Arena::SetCurrent(nullptr);
            return Reject::ReadFailed;
        }
        const uint32_t kind = pair[0] >> 24;
        const uint32_t offset = pair[0] & 0x00FFFFFFu;
        uint32_t value = pair[1];

        // Bound by the width the kind actually writes, not by a flat 4. An
        // Addr64 site is eight bytes, and checking four would let the last four
        // land past the payload - inside the module's own bss, which is legal
        // memory and would therefore corrupt silently rather than be refused.
        const uint32_t width = Wxlm::RelocWidth(static_cast<Wxlm::RelocKind>(kind));
        if (width == 0) {
            WIIXL_LOG("[loader:%s] %s: relocation %u has unknown kind %u",
                      id, RejectName(Reject::BadRelocation), i, kind);
            Arena::SetCurrent(nullptr);
            return Reject::BadRelocation;
        }
        if (offset + width > h.payloadSize) {
            WIIXL_LOG("[loader:%s] %s: relocation %u targets +0x%X (%u B), payload is %u B",
                      id, RejectName(Reject::BadRelocation), i, offset, width, h.payloadSize);
            Arena::SetCurrent(nullptr);
            return Reject::BadRelocation;
        }

        if (kind == static_cast<uint32_t>(Wxlm::RelocKind::Import)) {
            // The one genuinely new kind: `value` indexes the import table and
            // resolves through the registry instead of adding base.
            if (value >= h.importCount) {
                WIIXL_LOG("[loader:%s] %s: relocation %u names import %u of %u",
                          id, RejectName(Reject::BadRelocation), i, value, h.importCount);
                Arena::SetCurrent(nullptr);
                return Reject::BadRelocation;
            }
            Wxlm::ImportEntry imp{};
            if (impl::ReadVia(file, h.importOffset + value * sizeof(imp), &imp, sizeof(imp)) != sizeof(imp)) {
                WIIXL_LOG("[loader:%s] %s: reading import %u",
                          id, RejectName(Reject::ReadFailed), value);
                Arena::SetCurrent(nullptr);
                return Reject::ReadFailed;
            }
            char surfaceName[64] = {};
            char symbolName[64] = {};
            impl::ReadVia(file, h.stringOffset + imp.surfaceNameOffset, surfaceName, sizeof(surfaceName) - 1);
            impl::ReadVia(file, h.stringOffset + imp.symbolNameOffset, symbolName, sizeof(symbolName) - 1);
            surfaceName[sizeof(surfaceName) - 1] = '\0';
            symbolName[sizeof(symbolName) - 1] = '\0';

            const void* fn = Surface::Resolve(surfaceName, imp.symbolHash);
            if (!fn) {
                WIIXL_LOG("[loader:%s] %s: %s does not export %s (hash 0x%08X). The "
                          "surface is present, the symbol is not - a version mismatch "
                          "rather than a missing module.",
                          id, RejectName(Reject::UnresolvedImport), surfaceName,
                          symbolName, imp.symbolHash);
                Arena::SetCurrent(nullptr);
                return Reject::UnresolvedImport;
            }
            // Pointer-width, not uint32_t: an aarch64 module's import slot is
            // eight bytes and truncating one to 32 bits would write a plausible
            // half-address that faults somewhere unrelated at first call.
            *reinterpret_cast<uintptr_t*>(wbase + offset) =
                reinterpret_cast<uintptr_t>(fn);
            ++importCount;
            continue;
        }

        // Addr64 adds the FULL base before truncation can happen; the addend
        // itself is a 32-bit offset inside the module, which is all the record
        // has room for and all a module image can span.
        uint8_t* site = wimage + offset;
        if (kind == static_cast<uint32_t>(Wxlm::RelocKind::Addr64)) {
            *reinterpret_cast<uint64_t*>(site) =
                static_cast<uint64_t>(base) + static_cast<uint64_t>(value);
            continue;
        }

        // Kinds 0-3 are exactly WiiXLaunch_Cemu_Relocate's cases, applied to the
        // module's own base instead of the host's.
        value += static_cast<uint32_t>(base);
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
                Arena::SetCurrent(nullptr);
                return Reject::BadRelocation;
        }
    }
    WIIXL_LOG("[loader:%s] relocated %u entries (%u resolved through the registry)",
              id, h.relocCount, importCount);

    // --- flush ---------------------------------------------------------------
    // We just wrote code we are about to execute. Without this it runs from a
    // stale instruction cache, which fails in a way that looks nothing like a
    // loader bug.
    impl::g_Flush(base, imageSize);

    // --- record --------------------------------------------------------------
    //
    // The slot is claimed HERE, not by the caller. It was briefly the caller's
    // job, which meant LoadFrom dereferenced a pointer only Load() ever set -
    // and tools/loader_fuzz calls LoadFrom directly, so it crashed on the first
    // case. A function that only works when a particular caller set a global
    // first is not a function, it is half of one.
    if (impl::g_ModuleCount >= Arena::kMaxModules) {
        WIIXL_LOG("[loader:%s] %s: already holding %u modules, which is the limit",
                  id, RejectName(Reject::NoMemory), Arena::kMaxModules);
        Arena::SetCurrent(nullptr);
        return Reject::NoMemory;
    }
    LoadedModule& m = impl::g_Modules[impl::g_ModuleCount];
    m = LoadedModule{};
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
    m.arena = sub;

    // COMMITTED. Everything above could still have failed; from here the module
    // is visible to RunPhase.
    impl::g_ModuleCount++;

    // --- declared patches ----------------------------------------------------
    //
    // APPLIED HERE, AT LOAD, BEFORE ANY MODULE ENTRY RUNS. LoadAll loads every
    // module before RunPhase calls a single entry, so by the time any mod code
    // executes the host has seen and applied every patch every mod declared.
    // That is what makes patch conflicts detectable at all rather than
    // discovered later - see the load-sequence comment in wiixlaunch/patches.hpp
    // for why reordering this breaks two separate things.
    //
    // A refused patch does not fail the module. The patch is named and skipped,
    // the module still loads, and the rest of its patches are still tried: one
    // bad address must not cost a user the mod, and must not silently cost them
    // its other patches either.
    if (h.declaredPatchCount != 0) {
        WIIXL_LOG("[loader:%s] %u declared patch(es), applied before any module entry",
                  id, h.declaredPatchCount);
        uint32_t applied = 0, refused = 0;
        for (uint32_t i = 0; i < h.declaredPatchCount; ++i) {
            Wxlm::PatchEntry pe{};
            const uint32_t at = h.declaredPatchOffset +
                                i * static_cast<uint32_t>(sizeof(Wxlm::PatchEntry));
            if (impl::ReadVia(file, at, &pe, sizeof(pe)) != sizeof(pe)) {
                WIIXL_LOG("[loader:%s] %s: reading declared patch %u",
                          id, RejectName(Reject::ReadFailed), i);
                Arena::SetCurrent(nullptr);
                return Reject::ReadFailed;
            }
#if WIIXL_HOST
            // A host test must not write to an address a .wxlm names - it is a
            // number from a file, and here it is not a game address at all.
            // Through uintptr_t: targetAddr is a 32-bit field and this build is
            // 64-bit, which MSVC rightly warns about. Widening explicitly says
            // the narrowing is understood rather than accidental - and it is a
            // standing question for Switch, where a game address does not fit
            // in 32 bits at all. See docs/loader.md on declared patches.
            WIIXL_LOG("[loader:%s] declared patch %u at %p not applied (host test)",
                      id, i,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(pe.targetAddr)));
            (void)applied; (void)refused;
#else
            if (Patches::Apply(pe, m.id) == Patches::Result::Ok) ++applied;
            else ++refused;
#endif
        }
#if !WIIXL_HOST
        WIIXL_LOG("[loader:%s] patches: %u applied, %u refused", id, applied, refused);
#endif
    }

    // --- init_array ----------------------------------------------------------
    // Nothing else will ever run these: the flat build has no .init_array output
    // section and the bootstrap never walks one, so a module's static
    // constructors exist only if the loader calls them.
    if (h.initArrayCount != 0) {
#if WIIXL_HOST
        // NEVER execute module code in a host-test build. tools/loader_fuzz
        // feeds this deliberately malformed input, and the whole point is to
        // check that bad structure is REJECTED - jumping to a pointer that came
        // out of a fuzzed file would be reckless, and would test nothing that
        // the bounds checks above have not already decided. A host build also
        // has 64-bit pointers, so a 32-bit entry could not be called correctly
        // even for a valid module.
        WIIXL_LOG("[loader:%s] %u .init_array entries, not called (host test build)",
                  id, h.initArrayCount);
#else
        WIIXL_LOG("[loader:%s] running %u .init_array entries", id, h.initArrayCount);
        auto* fns = reinterpret_cast<uint32_t*>(base + h.initArrayOffset);
        for (uint32_t i = 0; i < h.initArrayCount; ++i) {
            if (fns[i] == 0) continue;
            reinterpret_cast<void (*)()>(fns[i])();
        }
        WIIXL_LOG("[loader:%s] .init_array done", id);
#endif
    }

    WIIXL_LOG("[loader:%s] LOADED, entry at %p, waiting for phase %u. Arena: %u of %u "
              "bytes used, %u left for this module.",
              id, reinterpret_cast<void*>(base + h.entryOffset), h.phase,
              Arena::UsedIn(sub), Arena::GrantedTo(sub), Arena::RemainingIn(sub));
    return Reject::None;
}

// Calls the entry point of any loaded module whose phase matches.
//
// Separate from Load because PostGx2 fires from the graphics module's
// OnInitialized callback, and base must not know that GX2 exists - the project
// or the game module calls this when its phase arrives.
inline void RunPhase(Wxlm::Phase phase) {
    // IN LOAD ORDER, and that is a specification, not an implementation
    // detail. Entry order determines hook install order, and hook install
    // order determines call order (docs/hooks.md). So this walks the array
    // forwards, and the array is filled in the order LoadAll established.
    for (uint32_t i = 0; i < impl::g_ModuleCount; ++i) {
        LoadedModule& m = impl::g_Modules[i];
        if (!m.valid || m.entryCalled) continue;
        if (m.phase != static_cast<uint8_t>(phase)) continue;

#if WIIXL_HOST
        // Same reason as .init_array above: a host test never executes module
        // code.
        WIIXL_LOG("[loader:%s] phase %u reached, entry not called (host test build)",
                  m.id, m.phase);
        m.entryCalled = true;
#else
        auto entry = reinterpret_cast<void (*)()>(
            reinterpret_cast<uintptr_t>(m.image) + m.entryOffset);
        WIIXL_LOG("[loader:%s] phase %u reached, calling entry at %p (module %u of %u "
                  "in load order)", m.id, m.phase, reinterpret_cast<void*>(entry),
                  i + 1, impl::g_ModuleCount);
        m.entryCalled = true;

        // Anything this module does is charged and attributed to it: memory to
        // its arena, hooks to its mod id. Both are cleared afterwards so the
        // next module cannot inherit either.
        Arena::SetCurrent(m.arena);
        ModContext::SetCurrent(m.id);
        entry();
        ModContext::SetCurrent(nullptr);
        Arena::SetCurrent(nullptr);

        WIIXL_LOG("[loader:%s] entry returned, %u of %u arena bytes used",
                  m.id, Arena::UsedIn(m.arena), Arena::GrantedTo(m.arena));
#endif
    }
}

#if WIIXL_CEMU

namespace impl {

// coreinit's FSDirectoryEntry: a 100-byte stat followed by the name. Pinned,
// because FSReadDir writes through this and a wrong size is a buffer overrun
// into whatever follows.
struct DirEntry { uint8_t stat[0x64]; char name[256]; };
static_assert(sizeof(DirEntry) == 0x164, "must match coreinit FSDirectoryEntry");

// 64-byte aligned for the same reason every other FS destination is - see
// ReadVia above. FSReadDir is no more forgiving than FSReadFile.
alignas(64) inline DirEntry g_DirEntry;

using FnFSOpenDir  = int32_t (*)(void*, void*, const char*, uint32_t*, uint32_t);
using FnFSReadDir  = int32_t (*)(void*, void*, uint32_t, void*, uint32_t);
using FnFSCloseDir = int32_t (*)(void*, void*, uint32_t, uint32_t);

// Names of the .wxlm files in `dir`, UNSORTED - FSReadDir's order is not
// specified by coreinit and is not relied on anywhere. LoadAll sorts.
//
// Entries are filtered by extension rather than by stat flags: a directory
// named "foo.wxlm" is a mistake either way, and matching the name is the same
// rule scripts/deploy.py writes by.
inline uint32_t ListWxlm(const char* dir, char names[][kMaxNameLen], uint32_t cap) {
    if (!FS::impl::EnsureFSClient()) {
        WIIXL_LOG("[loader] cannot enumerate %s - no FS client", dir);
        return 0;
    }

    auto openDir  = Backend::ResolveCemuFs<FnFSOpenDir>(Backend::CemuFsImport::FSOpenDir);
    auto readDir  = Backend::ResolveCemuFs<FnFSReadDir>(Backend::CemuFsImport::FSReadDir);
    auto closeDir = Backend::ResolveCemuFs<FnFSCloseDir>(Backend::CemuFsImport::FSCloseDir);
    if (!openDir || !readDir || !closeDir) {
        WIIXL_LOG("[loader] cannot enumerate %s - directory shims not resolved. That is "
                  "a build problem, not a missing directory.", dir);
        return 0;
    }

    void* client = FS::impl::g_FSClient;
    void* block = FS::impl::g_FSCmdBlock;

    // THROUGH THE SAME CANDIDATE LIST FILES USE. Opening the raw string was
    // the first boot's failure: the loader reported "WiiXLaunch/mods does not
    // exist" three lines after the load-point probe had listed its contents,
    // because the probe used the absolute path and FS::File::Open resolves
    // candidates while a bare FSOpenDir does not.
    char storage[3][256];
    const char* candidates[4];
    FS::impl::Candidates(dir, storage, candidates);

    uint32_t handle = 0;
    const char* opened = nullptr;
    for (uint32_t c = 0; c < 4u && !opened; ++c) {
        if (!candidates[c] || !candidates[c][0]) continue;
        if (openDir(client, block, candidates[c], &handle, 0xFFFFFFFF) == 0) {
            opened = candidates[c];
        }
    }
    if (!opened) {
        WIIXL_LOG("[loader] %s does not exist or could not be opened, through any of "
                  "the %u candidate paths", dir, 4u);
        return 0;
    }
    WIIXL_LOG("[loader] enumerating %s", opened);

    uint32_t n = 0, seen = 0, skipped = 0;
    while (seen < 64) {
        if (readDir(client, block, handle, &g_DirEntry, 0xFFFFFFFF) != 0) break;
        ++seen;
        g_DirEntry.name[sizeof(g_DirEntry.name) - 1] = '\0';
        if (!EndsWithWxlm(g_DirEntry.name)) { ++skipped; continue; }
        if (n >= cap) {
            WIIXL_LOG("[loader] %s holds more than the %u modules this host can load; "
                      "%s and anything after it are ignored", dir, cap, g_DirEntry.name);
            break;
        }
        CopyName(names[n++], g_DirEntry.name);
    }

    closeDir(client, block, handle, 0xFFFFFFFF);
    WIIXL_LOG("[loader] %s: %u entries seen, %u are .wxlm, %u skipped",
              dir, seen, n, skipped);
    return n;
}

} // namespace impl

#elif WIIXL_WIIU

namespace impl {

// The Cemu version of this resolves coreinit through the import shims because
// a graphic-pack payload has no imports. On Wii U the plugin links coreinit for
// real, so these are the actual functions and the struct is WUT's rather than
// one pinned by static_assert here.
//
// Everything else is deliberately the same as Cemu's, including the parts that
// look like they could be simplified: the candidate path list (a directory that
// resolves differently from the files inside it is a bug with no symptom until
// something enumerates), the 64-byte alignment, the 64-entry sweep bound, and
// the filter on the name rather than on stat flags.
alignas(64) inline FSDirectoryEntry g_DirEntry;

// Names of the .wxlm files in `dir`, UNSORTED - FSReadDir's order is not
// specified by coreinit and is not relied on anywhere. LoadAll sorts.
inline uint32_t ListWxlm(const char* dir, char names[][kMaxNameLen], uint32_t cap) {
    if (!FS::impl::EnsureFSClient()) {
        WIIXL_LOG("[loader] cannot enumerate %s - no FS client", dir);
        return 0;
    }

    auto* client = reinterpret_cast<FSClient*>(FS::impl::g_FSClient);
    auto* block = reinterpret_cast<FSCmdBlock*>(FS::impl::g_FSCmdBlock);

    char storage[3][256];
    const char* candidates[4];
    FS::impl::Candidates(dir, storage, candidates);

    FSDirectoryHandle handle = 0;
    const char* opened = nullptr;
    for (uint32_t c = 0; c < 4u && !opened; ++c) {
        if (!candidates[c] || !candidates[c][0]) continue;
        if (FSOpenDir(client, block, candidates[c], &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            opened = candidates[c];
        }
    }
    if (!opened) {
        WIIXL_LOG("[loader] %s does not exist or could not be opened, through any of "
                  "the %u candidate paths", dir, 4u);
        return 0;
    }
    WIIXL_LOG("[loader] enumerating %s", opened);

    uint32_t n = 0, seen = 0, skipped = 0;
    while (seen < 64) {
        if (FSReadDir(client, block, handle, &g_DirEntry, FS_ERROR_FLAG_ALL) != FS_STATUS_OK) break;
        ++seen;
        g_DirEntry.name[sizeof(g_DirEntry.name) - 1] = '\0';
        if (!EndsWithWxlm(g_DirEntry.name)) { ++skipped; continue; }
        if (n >= cap) {
            WIIXL_LOG("[loader] %s holds more than the %u modules this host can load; "
                      "%s and anything after it are ignored", dir, cap, g_DirEntry.name);
            break;
        }
        CopyName(names[n++], g_DirEntry.name);
    }

    FSCloseDir(client, block, handle, FS_ERROR_FLAG_ALL);
    WIIXL_LOG("[loader] %s: %u entries seen, %u are .wxlm, %u skipped",
              dir, seen, n, skipped);
    return n;
}

} // namespace impl

#elif WIIXL_SWITCH

namespace impl {
// nn::fs hands back a whole DirectoryEntry per call and each one is 0x310
// bytes, so there is exactly one, reused. Static rather than stack for the same
// reason as everywhere else here: this runs before anything is allocated.
inline nn::fs::DirectoryEntry g_DirEntry;

// Names of the .wxlm files in `dir`, UNSORTED - nn::fs does not specify an
// order and nothing here relies on one. LoadAll sorts.
//
// Same shape as the Cemu and Wii U versions on purpose, including the candidate
// path list: a directory that resolves differently from the files inside it is
// a bug with no symptom until something enumerates, and that has happened once
// already on Cemu.
inline uint32_t ListWxlm(const char* dir, char names[][kMaxNameLen], uint32_t cap) {
    if (!FS::impl::EnsureFSClient()) {
        WIIXL_LOG("[loader] cannot enumerate %s - the SD card is not mounted", dir);
        return 0;
    }

    char storage[3][256];
    const char* candidates[4];
    FS::impl::Candidates(dir, storage, candidates);

    nn::fs::DirectoryHandle handle{};
    const char* opened = nullptr;
    for (uint32_t c = 0; c < 4u && !opened; ++c) {
        if (!candidates[c] || !candidates[c][0]) continue;
        // A path with no mount name does not fail here, it ABORTS - see
        // FS::impl::HasMountName. This is the line the first Switch boot died
        // on, trying the bare "WiiXLaunch/mods".
        if (!FS::impl::HasMountName(candidates[c])) continue;
        if (nn::fs::OpenDirectory(&handle, candidates[c],
                                  nn::fs::OpenDirectoryMode_File) == 0) {
            opened = candidates[c];
        }
    }
    if (!opened) {
        WIIXL_LOG("[loader] %s does not exist or could not be opened, through any of "
                  "the %u candidate paths", dir, 4u);
        return 0;
    }
    WIIXL_LOG("[loader] enumerating %s", opened);

    uint32_t n = 0, seen = 0, skipped = 0;
    while (seen < 64) {
        long got = 0;
        if (nn::fs::ReadDirectory(&got, &g_DirEntry, handle, 1) != 0) break;
        if (got <= 0) break;              // end of directory, not a failure
        ++seen;
        g_DirEntry.m_Name[sizeof(g_DirEntry.m_Name) - 1] = '\0';
        if (!EndsWithWxlm(g_DirEntry.m_Name)) { ++skipped; continue; }
        if (n >= cap) {
            WIIXL_LOG("[loader] %s holds more than the %u modules this host can load; "
                      "%s and anything after it are ignored", dir, cap, g_DirEntry.m_Name);
            break;
        }
        CopyName(names[n++], g_DirEntry.m_Name);
    }

    nn::fs::CloseDirectory(handle);
    WIIXL_LOG("[loader] %s: %u entries seen, %u are .wxlm, %u skipped",
              dir, seen, n, skipped);
    return n;
}
} // namespace impl

#else

// The host test build. Returning 0 is indistinguishable from an empty
// directory, which is the failure shape this codebase keeps hitting: a
// capability that answers "nothing there" when it means "I cannot look".
namespace impl {
inline uint32_t ListWxlm(const char* dir, char[][kMaxNameLen], uint32_t) {
    WIIXL_LOG("[loader] directory enumeration is not implemented on this platform (%s) - "
              "this is not the same as finding no modules", dir);
    return 0;
}
} // namespace impl

#endif

#if WIIXL_CEMU || WIIXL_WIIU || WIIXL_SWITCH

// Loads one module from the filesystem. The path is tried as given and through
// WiiXLaunch::FS's usual candidates, so "WiiXLaunch/mods/foo.wxlm" resolves the
// same way every other asset does.
inline Reject Load(const char* path) {
    WIIXL_LOG("[loader] opening %s", path);

    FS::File file;
    if (!file.Open(path)) {
        WIIXL_LOG("[loader] %s: could not open %s", RejectName(Reject::ReadFailed), path);
        return Reject::ReadFailed;
    }

    const Reject r = LoadFrom(file);
    file.Close();
    return r;
}

// --- loading every module in a directory -----------------------------------
//
// LOAD ORDER IS LEXICAL BY FILENAME, ascending, byte-wise on the raw name.
//
// This is a specification, not an accident, and it has to be one: load order
// determines hook install order, which determines the order mods see a call
// (docs/hooks.md). It is the user's only lever over which mod acts first, so it
// has to be something they can rely on and predict from the filenames they can
// see, rather than whatever order the filesystem happens to return.
//
// FSReadDir's order is NOT specified by coreinit and is not stable across
// filesystems or hosts, so it is never used directly - the names are collected
// and sorted here. Byte-wise means uppercase sorts before lowercase, which is
// worth knowing when naming a mod to run first.
inline uint32_t LoadAll(const char* dir) {
    WIIXL_LOG("[loader] enumerating %s", dir);

    char names[Arena::kMaxModules][kMaxNameLen];
    uint32_t count = impl::ListWxlm(dir, names, Arena::kMaxModules);
    if (count == 0) {
        WIIXL_LOG("[loader] no .wxlm files in %s - nothing to load. This is the "
                  "default state of a fresh host and the game boots normally.", dir);
        return 0;
    }

    // Insertion sort: at most kMaxModules entries, and being obviously correct
    // matters more here than being fast.
    for (uint32_t i = 1; i < count; ++i) {
        char key[kMaxNameLen];
        impl::CopyName(key, names[i]);
        uint32_t j = i;
        while (j > 0 && impl::NameLess(key, names[j - 1])) {
            impl::CopyName(names[j], names[j - 1]);
            --j;
        }
        impl::CopyName(names[j], key);
    }

    WIIXL_LOG("[loader] %u module(s) found; load order is lexical by filename, which "
              "is also hook priority:", count);
    for (uint32_t i = 0; i < count; ++i) {
        WIIXL_LOG("[loader]   %u. %s", i + 1, names[i]);
    }

    uint32_t loaded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        char path[kMaxPathLen];
        impl::JoinPath(path, dir, names[i]);
        const Reject r = Load(path);
        if (r == Reject::None) {
            ++loaded;
        } else {
            // A module that was FOUND and REFUSED must not report as an absent
            // one. Every other module still loads - one bad file must not cost
            // the user their boot, and must not silently cost them the rest of
            // their mods either.
            WIIXL_LOG("[loader] %s not loaded: %s. The remaining %u module(s) are "
                      "still being loaded.", names[i], RejectName(r), count - i - 1);
        }
    }

    WIIXL_LOG("[loader] %u of %u module(s) loaded", loaded, count);
    return loaded;
}

#else

// Switch reaches its modules through its own backend; not yet probed. Says so
// rather than silently doing nothing.
inline Reject Load(const char* path) {
    WIIXL_LOG("[loader] not implemented on this target yet (%s)", path);
    return Reject::ReadFailed;
}

inline uint32_t LoadAll(const char* dir) {
    WIIXL_LOG("[loader] not implemented on this target yet (%s)", dir);
    return 0;
}

#endif

} // namespace WiiXLaunch::Loader
