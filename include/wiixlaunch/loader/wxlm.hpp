#pragma once

// The .wxlm module format.
//
// A mod ships as a relocatable blob with no source available, and this is the
// contract between the writer (scripts/wxlm.py) and the loader
// (wiixlaunch/loader/loader.hpp). Both implement this file; neither may drift
// from it, so every field has a fixed width and the struct carries
// static_asserts on its size and offsets.
//
// LAYOUT. One header, then sections located by offset from the start of file:
//
//   header    this struct, fixed size
//   payload   the mod's code and data, linked at 0
//   relocs    (kind << 24 | offset, value) pairs - the same encoding
//             scripts/deploy.py already emits for the host itself
//   imports   (surface, symbol) requirements and the sites that need them
//   exports   symbol hash -> offset, for inter-mod dependencies
//   required  surfaces the module needs as a whole
//   strings   a blob every name in the tables indexes into
//
// WHY A STRING BLOB rather than inline names: an entry stays fixed-width so the
// loader can walk a table without parsing, and surface names repeat across
// entries. Offsets into one blob keep entries uniform and the file small.
//
// ENDIANNESS AND MACHINE are explicit and checked before anything else is
// interpreted. The same mod id and version will exist for Wii U (big-endian
// PowerPC) and Switch (little-endian AArch64), and loading the wrong one would
// relocate garbage into executable memory.
//
// INTEGRITY. The realistic corruption is not a truncated file but a
// PARTIALLY-WRITTEN one: a user drags a mod into the folder and the copy is
// interrupted, leaving a file of the right length whose tail is stale or zero.
// Length checks cannot see that. The whole point of "skip it, log it, keep
// going" is that the loader can tell corrupt from valid BEFORE it relocates, so
// the header carries fileSize and a CRC32 over everything after the header.
//
// RESERVED FIELDS MUST BE ZERO, AND THE LOADER REJECTS NON-ZERO. Reserved-and-
// checked is a compatibility mechanism; reserved-and-ignored is a future bug -
// a newer writer sets a field, an older loader ignores it, and the mod
// half-works instead of failing cleanly. Failing cleanly is the contract.

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch::Wxlm {

// "WXLM" as a big-endian word.
constexpr uint32_t kMagic = 0x57584C4Du;

// Bumped when the LAYOUT below changes. Distinct from the ABI version, which is
// about what the host exports: a file can be well-formed and still require a
// surface this host does not have.
constexpr uint16_t kFormatVersion = 1;

enum class Machine : uint16_t {
    None    = 0,
    Ppc32   = 1,   // Wii U and Cemu, big-endian
    AArch64 = 2,   // Switch, little-endian
};

enum class Endian : uint8_t {
    Little = 0,
    Big    = 1,
};

// When the mod's entry point runs. See docs/loader.md for what is and is not
// initialised at each - a mod asking for Load must not touch game state.
enum class Phase : uint8_t {
    Load     = 0,  // right after ingestion, at the load point
    PostGx2  = 1,  // GX2/NVN is up; textures and draw callbacks are legal
    AppStart = 2,  // Wii U only: WUPS ON_APPLICATION_START
    Count
};

// Relocation kinds. 0-3 are exactly what deploy.py already emits for the host,
// deliberately: the same emitter produces both, and the loader's relocate loop
// is the same logic as WiiXLaunch_Cemu_Relocate.
enum class RelocKind : uint8_t {
    Addr32   = 0,  // a whole pointer
    Addr16Ha = 1,  // `lis` half, with the sign-extension carry
    Addr16Hi = 2,  // `lis` half, plain
    Addr16Lo = 3,  // `ori`/`addi` half
    // The only genuinely new machinery the format needs: the value indexes the
    // import table rather than being a link-time address, and the loader
    // resolves it through the surface registry instead of adding base.
    Import   = 4,
    Count
};

// One import: which surface, which symbol, and the minimum version required.
// Carries names as well as the hash so a failure reads as
// "requires botw.gx2 v1, not present" rather than a bare number.
struct ImportEntry {
    uint32_t surfaceNameOffset;  // into the string blob
    uint32_t symbolNameOffset;   // into the string blob, for diagnostics
    uint32_t symbolHash;         // what the registry is looked up by
    uint16_t versionMajor;
    uint16_t versionMinor;
};
static_assert(sizeof(ImportEntry) == 16, "ImportEntry layout is part of the format");

// One export, for a mod another mod depends on.
//
// VERSIONED, even though nothing uses inter-mod dependencies yet. Adding two
// fields now costs nothing; adding them after mods ship costs a formatVersion
// bump plus a compatibility story for every file already in the wild. The cost
// is entirely asymmetric, so exports get the same major-exact / minor-at-least
// treatment surfaces have.
struct ExportEntry {
    uint32_t symbolHash;
    uint32_t offset;             // into the payload
    uint16_t versionMajor;
    uint16_t versionMinor;
};
static_assert(sizeof(ExportEntry) == 12, "ExportEntry layout is part of the format");

// A surface the module requires as a whole, independent of any one symbol.
// Checked before a single relocation is applied, so a module that cannot
// possibly work is rejected before it is written into memory.
struct RequiredSurface {
    uint32_t nameOffset;         // into the string blob
    uint16_t versionMajor;
    uint16_t versionMinor;
};
static_assert(sizeof(RequiredSurface) == 8, "RequiredSurface layout is part of the format");

// The header. Every offset is from the start of the file; every size is bytes.
struct Header {
    uint32_t magic;              // kMagic
    uint16_t formatVersion;      // kFormatVersion
    uint16_t machine;            // Machine
    uint8_t  endian;             // Endian
    uint8_t  phase;              // Phase
    uint16_t abiVersion;         // must match wiixl.core's kAbiVersion

    char     modId[16];          // NUL-padded; the name in every log line
    uint16_t verMajor;
    uint16_t verMinor;
    uint16_t verPatch;
    uint16_t reserved0;          // must be zero

    // Integrity. fileSize catches truncation immediately - the loader compares
    // it to what FS actually delivered. contentCrc32 catches the partial write
    // a length check cannot see, and is computed over every byte from the end
    // of this header to the end of the file, which is payload, relocs,
    // imports, exports, required and strings.
    uint32_t fileSize;
    uint32_t contentCrc32;

    uint32_t payloadOffset;
    uint32_t payloadSize;        // code and data, linked at 0

    uint32_t relocOffset;
    uint32_t relocCount;         // pairs of (kind<<24|offset, value)

    uint32_t importOffset;
    uint32_t importCount;

    uint32_t exportOffset;
    uint32_t exportCount;

    uint32_t requiredOffset;
    uint32_t requiredCount;

    uint32_t stringOffset;
    uint32_t stringSize;

    uint32_t entryOffset;        // into the payload; the mod's entry point

    // .init_array, run before the entry point. The flat build runs no static
    // constructors of its own - scripts/cemu.ld has no such output section and
    // the bootstrap never walks one - so if a mod has any, the loader is the
    // only thing that will ever call them.
    uint32_t initArrayOffset;    // into the payload
    uint32_t initArrayCount;     // function pointers

    uint32_t bssSize;            // zeroed by the loader after the payload

    // How much heap the module would LIKE. A REQUEST, NOT A GRANT.
    //
    // The host decides what it actually gets and may refuse or trim it. On Cemu
    // the arena is the tail of a code cave whose size depends on how many other
    // graphic packs the user has enabled - 3959 KB with seven of them, 3963 KB
    // with one, measured - so no module can be promised a number. A module must
    // handle being given less than it asked for, and the host tells it how much
    // it actually has. 0 means "no particular need, host default".
    uint32_t heapRequest;

    // Reserved for stages 6 and 7, zero until then. Declared now rather than
    // appended later so the header size never moves: a mod's hooks and raw
    // patches become header data the host applies, instead of graphic-pack
    // .origin lines only the host pack may emit.
    uint32_t declaredHookOffset;
    uint32_t declaredHookCount;
    uint32_t declaredPatchOffset;
    uint32_t declaredPatchCount;

    uint32_t reserved1[4];       // must be zero
};

// These are the format, not an implementation detail. scripts/wxlm.py packs to
// exactly these offsets, and a drift between the two is the one failure neither
// side can detect at runtime - the loader would read a plausible garbage offset
// and relocate into it.
static_assert(sizeof(Header) == 144, "Header layout is part of the format");
static_assert(alignof(Header) == 4, "Header must not gain stricter alignment");
static_assert(offsetof(Header, magic) == 0, "magic must be first");
static_assert(offsetof(Header, modId) == 12, "modId offset is part of the format");
static_assert(offsetof(Header, fileSize) == 36, "integrity fields are part of the format");
static_assert(offsetof(Header, contentCrc32) == 40, "integrity fields are part of the format");
static_assert(offsetof(Header, payloadOffset) == 44, "payload fields are part of the format");
static_assert(offsetof(Header, entryOffset) == 92, "entryOffset is part of the format");
static_assert(offsetof(Header, heapRequest) == 108, "heapRequest is part of the format");
static_assert(offsetof(Header, reserved1) == 128, "the reserved tail is part of the format");

constexpr uint32_t kMinFileSize = sizeof(Header);

// CRC32, IEEE reflected (poly 0xEDB88320) - byte-for-byte what Python's
// zlib.crc32 produces, so the writer gets it right for free and only this side
// has to be matched.
//
// Nibble table rather than a 256-entry one: 64 bytes of rodata instead of 1 KB,
// in a code cave that is under 4 MB and shared with every other graphic pack.
namespace impl {
constexpr uint32_t kCrcNibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};
} // namespace impl

inline uint32_t Crc32(const void* data, uint32_t length) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= p[i];
        crc = (crc >> 4) ^ impl::kCrcNibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ impl::kCrcNibble[crc & 0x0Fu];
    }
    return crc ^ 0xFFFFFFFFu;
}

// Why a module was rejected. Every one is logged with the module's id and the
// specific thing that failed: a module must never fail silently, and must never
// be left partially applied.
enum class Reject : uint32_t {
    None = 0,
    ReadFailed,          // FS could not deliver the bytes
    TooSmall,            // shorter than a header
    BadMagic,
    FormatTooNew,        // formatVersion > kFormatVersion
    WrongMachine,        // built for another CPU
    WrongEndian,
    AbiMismatch,         // built against a different wiixl.core ABI
    SizeMismatch,        // fileSize disagrees with what was read - truncated
    BadChecksum,         // content CRC failed - partially written or corrupt
    ReservedNotZero,     // a reserved field is set; this file wants a newer host
    BadPhase,            // phase is not one this host knows
    BadSectionBounds,    // an offset or size runs past the file
    MissingSurface,      // a required surface is not registered
    UnresolvedImport,    // the surface is there, the symbol is not
    BadRelocation,       // a reloc site is outside the payload
    NoMemory,            // the host arena could not fit it
    BadEntry,            // entryOffset is outside the payload
};

inline const char* RejectName(Reject r) {
    switch (r) {
        case Reject::None:             return "OK";
        case Reject::ReadFailed:       return "READ-FAILED";
        case Reject::TooSmall:         return "TOO-SMALL";
        case Reject::BadMagic:         return "BAD-MAGIC";
        case Reject::FormatTooNew:     return "FORMAT-TOO-NEW";
        case Reject::WrongMachine:     return "WRONG-MACHINE";
        case Reject::WrongEndian:      return "WRONG-ENDIAN";
        case Reject::AbiMismatch:      return "ABI-MISMATCH";
        case Reject::SizeMismatch:     return "SIZE-MISMATCH";
        case Reject::BadChecksum:      return "BAD-CHECKSUM";
        case Reject::ReservedNotZero:  return "RESERVED-NOT-ZERO";
        case Reject::BadPhase:         return "BAD-PHASE";
        case Reject::BadSectionBounds: return "BAD-SECTION-BOUNDS";
        case Reject::MissingSurface:   return "MISSING-SURFACE";
        case Reject::UnresolvedImport: return "UNRESOLVED-IMPORT";
        case Reject::BadRelocation:    return "BAD-RELOCATION";
        case Reject::NoMemory:         return "NO-MEMORY";
        case Reject::BadEntry:         return "BAD-ENTRY";
    }
    return "?";
}

// What this host is, for the checks above.
#if WIIXL_SWITCH
constexpr Machine kHostMachine = Machine::AArch64;
constexpr Endian  kHostEndian  = Endian::Little;
#else
constexpr Machine kHostMachine = Machine::Ppc32;
constexpr Endian  kHostEndian  = Endian::Big;
#endif

} // namespace WiiXLaunch::Wxlm
