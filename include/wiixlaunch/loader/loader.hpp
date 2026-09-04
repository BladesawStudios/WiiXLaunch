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
#include <wiixlaunch/loader/arena.hpp>

#include <cstdint>
#include <cstddef>

#if WIIXL_CEMU
#include <wiixl_cemu_backend.hpp>
#endif

namespace WiiXLaunch::Loader {

using Wxlm::Reject;
using Wxlm::RejectName;

// Where the loader gets memory, and how it publishes code it has written.
//
// Indirected through hooks for two reasons. Stage 5 replaces the allocator with
// a per-module arena and should not have to edit this file to do it. And it is
// what lets tools/loader_fuzz run this exact code natively: parsing
// attacker-shaped data is ordinary logic, and testing it should not need a
// console or a boot.
using AllocFn = void* (*)(uint32_t size, uint32_t align);
using FlushFn = void  (*)(uintptr_t addr, uint32_t size);

namespace impl {

#if WIIXL_CEMU
inline void* DefaultAlloc(uint32_t size, uint32_t align) {
    return Backend::AllocCemuHeap(size, align);
}
inline void DefaultFlush(uintptr_t addr, uint32_t size) {
    Backend::FlushCache(addr, size);
}
#else
inline void* DefaultAlloc(uint32_t, uint32_t) { return nullptr; }
inline void DefaultFlush(uintptr_t, uint32_t) {}
#endif

inline AllocFn g_Alloc = &DefaultAlloc;
inline FlushFn g_Flush = &DefaultFlush;

} // namespace impl

// Replaces the allocator and the cache-flush. Both must be set before Load.
inline void SetMemoryHooks(AllocFn alloc, FlushFn flush) {
    impl::g_Alloc = alloc ? alloc : &impl::DefaultAlloc;
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

// One slot. Multi-mod is a later stage.
inline LoadedModule g_Module{};

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

inline const LoadedModule& Module() { return impl::g_Module; }

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
    bool reservedSet = (h.reserved0 != 0);
    for (int i = 0; i < 4; ++i) reservedSet = reservedSet || (h.reserved1[i] != 0);
    reservedSet = reservedSet || h.declaredHookOffset != 0 || h.declaredPatchOffset != 0;
    if (reservedSet || h.declaredHookCount != 0 || h.declaredPatchCount != 0) {
        WIIXL_LOG("[loader:%s] %s: a reserved field is set, so this file wants "
                  "something this host does not implement yet. Refusing rather than "
                  "loading it partially.", id, RejectName(Reject::ReservedNotZero));
        return Reject::ReservedNotZero;
    }

    // Structure. Every section must lie inside the file.
    struct { uint64_t off, size; const char* what; } spans[] = {
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

        const Arena::Grant g = Arena::Acquire(id, request, &sub);
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
    uint8_t* image = static_cast<uint8_t*>(Arena::AllocIn(*sub, imageSize, 64));
    if (!image) {
        WIIXL_LOG("[loader:%s] %s: wanted %u B for the image (payload %u + bss %u) "
                  "inside a %u-byte grant with %u used",
                  id, RejectName(Reject::NoMemory), imageSize, h.payloadSize, h.bssSize,
                  Arena::GrantedTo(sub), Arena::UsedIn(sub));
        WIIXL_LOG("[loader:%s] %s", id, Arena::kSharedArenaNote);
        Arena::SetCurrent(nullptr);
        return Reject::NoMemory;
    }

    if (!impl::ReadAligned(file, h.payloadOffset, image, h.payloadSize)) {
        WIIXL_LOG("[loader:%s] %s: short read of the %u-byte payload",
                  id, RejectName(Reject::ReadFailed), h.payloadSize);
        Arena::SetCurrent(nullptr);
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
        if (impl::ReadVia(file, h.relocOffset + i * 8u, pair, 8) != 8) {
            WIIXL_LOG("[loader:%s] %s: reading relocation %u",
                      id, RejectName(Reject::ReadFailed), i);
            Arena::SetCurrent(nullptr);
            return Reject::ReadFailed;
        }
        const uint32_t kind = pair[0] >> 24;
        const uint32_t offset = pair[0] & 0x00FFFFFFu;
        uint32_t value = pair[1];

        if (offset + 4 > h.payloadSize) {
            WIIXL_LOG("[loader:%s] %s: relocation %u targets +0x%X, payload is %u B",
                      id, RejectName(Reject::BadRelocation), i, offset, h.payloadSize);
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
    m.arena = sub;

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
    LoadedModule& m = impl::g_Module;
    if (!m.valid || m.entryCalled) return;
    if (m.phase != static_cast<uint8_t>(phase)) return;

#if WIIXL_HOST
    // Same reason as .init_array above: a host test never executes module code.
    WIIXL_LOG("[loader:%s] phase %u reached, entry not called (host test build)",
              m.id, m.phase);
    m.entryCalled = true;
    return;
#else
    auto entry = reinterpret_cast<void (*)()>(
        reinterpret_cast<uintptr_t>(m.image) + m.entryOffset);
    WIIXL_LOG("[loader:%s] phase %u reached, calling entry at %p",
              m.id, m.phase, reinterpret_cast<void*>(entry));
    m.entryCalled = true;
    Arena::SetCurrent(m.arena);
    entry();
    Arena::SetCurrent(nullptr);
    WIIXL_LOG("[loader:%s] entry returned, %u of %u arena bytes used",
              m.id, Arena::UsedIn(m.arena), Arena::GrantedTo(m.arena));
#endif
}

#if WIIXL_CEMU || WIIXL_WIIU

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

#else

// Switch reaches its modules through its own backend; not yet probed. Says so
// rather than silently doing nothing.
inline Reject Load(const char* path) {
    WIIXL_LOG("[loader] not implemented on this target yet (%s)", path);
    return Reject::ReadFailed;
}

#endif

} // namespace WiiXLaunch::Loader
