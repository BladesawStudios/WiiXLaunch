#pragma once

// The .wxlm module format.
//
// A mod ships as a relocatable blob with no source available, and this is the
// contract between the writer (scripts/wxlm.py) and the loader
// (wiixlaunch/loader/loader.hpp). Both implement this file; neither may drift
// from it, so every field below has a fixed width and the struct has
// static_asserts on its size and offsets.
//
// LAYOUT. One header, then five sections, each located by an offset from the
// start of the file:
//
//   header    this struct, fixed size
//   payload   the mod's code and data, linked at 0
//   relocs    (kind << 24 | offset, value) pairs - the same encoding
//             scripts/deploy.py already emits for the host itself
//   imports   (surface, symbol) requirements and the sites that need them
//   exports   symbol hash -> offset, for inter-mod dependencies
//   strings   a blob every name in the tables indexes into
//
// WHY A STRING BLOB rather than inline names: an import entry is fixed-width so
// the loader can walk the table without parsing, and surface names repeat
// across entries. Offsets into one blob keep entries uniform and the file small.
//
// ENDIANNESS AND MACHINE are explicit, and checked before anything is
// interpreted. A .wxlm built for Wii U is big-endian PowerPC; one built for
// Switch is little-endian AArch64. Loading the wrong one would relocate
// garbage into executable memory, so `machine` is verified first and a
// mismatch is rejected by name. This is not hypothetical - the same mod id and
// version will exist for both platforms.
//
// ALL RESERVED FIELDS MUST BE ZERO. A loader that ignored them could not tell a
// future format apart from a corrupt one; rejecting non-zero means the format
// can grow without old hosts silently misreading new files.

#include <wiixlaunch/platform.hpp>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch::Wxlm {

// "WXLM" as a big-endian word. The loader compares raw bytes, so this reads the
// same either way round and a wrong-endian file fails on `endian` instead of
// looking like a corrupt magic.
constexpr uint32_t kMagic = 0x57584C4Du;

// Bumped when the LAYOUT below changes. Distinct from the ABI version, which is
// about what the host exports; a file can be well-formed and still require a
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
    Load    = 0,   // immediately after the module is ingested, at the load point
    PostGx2 = 1,   // GX2/NVN pipeline is up; textures and draw callbacks are legal
    AppStart = 2,  // Wii U only: WUPS ON_APPLICATION_START
};

// Relocation kinds. 0-3 are exactly what deploy.py already emits for the host,
// deliberately: the same emitter produces both, and the loader's relocate loop
// is the same logic as WiiXLaunch_Cemu_Relocate.
enum class RelocKind : uint8_t {
    Addr32   = 0,  // a whole pointer
    Addr16Ha = 1,  // `lis` half, with the sign-extension carry
    Addr16Hi = 2,  // `lis` half, plain
    Addr16Lo = 3,  // `ori`/`addi` half
    // NEW, and the only genuinely new machinery the format needs: the value is
    // an index into the import table rather than a link-time address. The
    // loader resolves it through the surface registry instead of adding base.
    Import   = 4,
};

// One import: which surface, which symbol, and the minimum version required.
// The loader reports a failure as "requires <surface> v<major>, not present"
// using these fields, so they carry the name rather than only a hash.
struct ImportEntry {
    uint32_t surfaceNameOffset;  // into the string blob
    uint32_t symbolNameOffset;   // into the string blob, for diagnostics
    uint32_t symbolHash;         // what the registry is actually looked up by
    uint16_t versionMajor;
    uint16_t versionMinor;
};
static_assert(sizeof(ImportEntry) == 16, "ImportEntry layout is part of the format");

// One export, for a mod another mod depends on.
struct ExportEntry {
    uint32_t symbolHash;
    uint32_t offset;             // into the payload
};
static_assert(sizeof(ExportEntry) == 8, "ExportEntry layout is part of the format");

// A surface the module requires as a whole, independent of any particular
// symbol. Checked before a single relocation is applied, so a module that
// cannot possibly work is rejected before it is written into memory.
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

    char     modId[16];          // NUL-padded; the name used in every log line
    uint16_t verMajor;
    uint16_t verMinor;
    uint16_t verPatch;
    uint16_t reserved0;

    uint32_t payloadOffset;
    uint32_t payloadSize;        // bytes of code and data, linked at 0

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
    uint32_t heapBudget;         // bytes the mod may allocate; 0 = host default

    // Reserved for stages 6 and 7, and zero until then. Declared here rather
    // than added later so the header size does not move: a mod's hooks and raw
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
// side can detect at runtime - the loader would read a plausible-looking
// garbage offset and relocate into it.
static_assert(sizeof(Header) == 136, "Header layout is part of the format");
static_assert(alignof(Header) == 4, "Header must not gain stricter alignment");
static_assert(offsetof(Header, magic) == 0, "magic must be first");
static_assert(offsetof(Header, modId) == 12, "modId offset is part of the format");
static_assert(offsetof(Header, payloadOffset) == 36, "payload fields are part of the format");
static_assert(offsetof(Header, entryOffset) == 84, "entryOffset is part of the format");
static_assert(offsetof(Header, reserved1) == 120, "the reserved tail is part of the format");

// The smallest a well-formed file can be. Used to reject a truncated read
// before any field is trusted.
constexpr uint32_t kMinFileSize = sizeof(Header);

// Why a module was rejected. Every one of these is logged with the module's id
// and the specific thing that failed - a module must never fail silently, and
// must never be partially applied.
enum class Reject : uint32_t {
    None = 0,
    ReadFailed,          // FS could not deliver the bytes
    TooSmall,            // shorter than a header
    BadMagic,
    FormatTooNew,        // formatVersion > kFormatVersion
    WrongMachine,        // built for another CPU
    WrongEndian,
    AbiMismatch,         // built against a different wiixl.core ABI
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
        case Reject::BadSectionBounds: return "BAD-SECTION-BOUNDS";
        case Reject::MissingSurface:   return "MISSING-SURFACE";
        case Reject::UnresolvedImport:  return "UNRESOLVED-IMPORT";
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
