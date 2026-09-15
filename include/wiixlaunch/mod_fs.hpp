#pragma once

// WiiXLaunch::ModFS - a mod's own directory, and only its own directory.
//
// ---------------------------------------------------------------------------
// TWO CALLS, NOT ONE WITH A FALLBACK.
//
// wiixl.core gives a mod two distinct reads and the choice is made at the call
// site:
//
//   ModReadFile   this mod's own directory, and it CANNOT escape it
//   GameReadFile  game content, through the host's usual path candidates
//
// It is deliberately not one call that tries the mod directory and falls back.
// "Whichever resolves first wins" is order-dependent and silent - the same
// ambiguity as an FS status of -6 meaning two different things, as two path
// resolvers disagreeing, and as three refusal reasons collapsed into one bool.
// Every one of those cost a debugging round. A mod's intent is legible in the
// log because it is legible in the call it made.
//
// It is also deliberately not full containment. Reading game content is a large
// part of what modding IS; a BotW mod that cannot open a game pack is crippled.
// The containment is on the SCOPED call, where it is a guarantee worth having,
// and the other call says plainly that it is not scoped.
//
// ---------------------------------------------------------------------------
// WHAT SCOPED MEANS, precisely.
//
// A scoped path is resolved under WiiXLaunch/mods/<mod id>/ and may not leave
// it. Refused, by value:
//
//   - an absolute path (leading '/')
//   - any ".." component, anywhere in the path
//   - a backslash, which is a separator on the host build and not on the target
//   - control characters
//   - an empty path, or one too long to resolve
//
// The refusals are a VALUE and not a log string, so a test can assert which one
// happened - see the log-string rule in docs/framework/modules.md. And they are checked
// on the path the mod supplied, BEFORE any concatenation, so there is no window
// in which a joined string has to be re-parsed to find out whether it escaped.
//
// GAME MODULES ARE NOT MODS. wiixlaunch-botw is compiled into the payload, so
// it is host code and uses the game-content path exactly as the host does. The
// scoped call is for .wxlm modules, which are the only things with an id and a
// directory. docs/framework/modules.md states the distinction.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdint>

namespace WiiXLaunch::ModFS {

// Where every module's directory lives. The .wxlm files themselves sit in this
// directory too; a module's resources sit in a subdirectory named for its id.
constexpr const char* kModsRoot = "WiiXLaunch/mods";

// ...and where it ACTUALLY lives this boot, which on Switch may be a per-title
// subdirectory of that.
//
// sd:/WiiXLaunch/mods is one folder shared by every game on the card, because a
// Switch SD card has no per-title place for a host's own files - unlike Cemu,
// where the mods directory sits inside a graphic pack that names its titleIds,
// and Wii U, where it sits in the game's own content. So two games' modules
// land in the same directory and each game's host tries to load both.
//
// Most crossovers are already refused: a mod needing a game surface is refused
// by name, and a patch is refused because the bytes it expects are not there.
// A mod that only needs the base surfaces and HOOKS RAW OFFSETS is refused by
// nothing, and that is the common shape for a game with no module yet.
//
// The loader therefore prefers WiiXLaunch/mods/<titleid>/ and this follows it,
// so a module's own files are found beside the module that was actually loaded
// rather than in whichever directory the constant happened to name.
inline const char* g_Root = kModsRoot;

inline void SetRoot(const char* root) { if (root && *root) g_Root = root; }
inline const char* Root() { return g_Root; }

// The host's own resources live under a reserved id rather than beside the
// mods directory, so the scheme has no exception. An exception is how someone
// later concludes the scheme is optional.
//
// The whole '_' PREFIX is reserved, not just this one name - reserving a space
// rather than a single string means a future reserved id needs no new check and
// no new refusal path.
constexpr const char* kHostId = "_host";
constexpr char kReservedPrefix = '_';

constexpr uint32_t kMaxScopedPath = 192;

inline bool IsReservedId(const char* id) {
    return id && id[0] == kReservedPrefix;
}

enum class PathResult : uint32_t {
    Ok = 0,
    NoModule,       // called outside a module entry - there is no "own" directory
    Empty,          // no path given
    Absolute,       // leading '/' - that is not this mod's directory
    ParentEscape,   // a ".." component, anywhere
    BadChar,        // a backslash or a control character
    TooLong,        // will not fit kMaxScopedPath once joined
};

inline const char* PathResultName(PathResult r) {
    switch (r) {
        case PathResult::Ok:           return "OK";
        case PathResult::NoModule:     return "NO-MODULE";
        case PathResult::Empty:        return "EMPTY";
        case PathResult::Absolute:     return "ABSOLUTE";
        case PathResult::ParentEscape: return "PARENT-ESCAPE";
        case PathResult::BadChar:      return "BAD-CHAR";
        case PathResult::TooLong:      return "TOO-LONG";
    }
    return "?";
}

namespace impl {

// Is [p, p+3) a ".." component - that is, ".." bounded by separators or ends?
//
// Checked as a COMPONENT rather than as a substring, because "a..b" and
// "..foo" contain the characters without escaping anything, and refusing those
// would make perfectly ordinary filenames unreadable for no gain. What must be
// refused is a ".." that the filesystem would act on.
inline bool IsParentComponentAt(const char* path, uint32_t i) {
    if (path[i] != '.' || path[i + 1] != '.') return false;
    const bool startsComponent = (i == 0) || (path[i - 1] == '/');
    const char after = path[i + 2];
    const bool endsComponent = (after == '\0') || (after == '/');
    return startsComponent && endsComponent;
}

inline uint32_t Len(const char* s) {
    uint32_t n = 0;
    while (s && s[n] && n < kMaxScopedPath * 2) ++n;
    return n;
}

} // namespace impl

// Checks a mod-supplied relative path without joining it.
//
// Deliberately separate from Resolve so a test can assert the reason on a path
// it never intends to open, and so the rules are stated once in a function that
// takes nothing but the path.
inline PathResult CheckScoped(const char* path) {
    if (!path || path[0] == '\0') return PathResult::Empty;
    if (path[0] == '/') return PathResult::Absolute;

    const uint32_t n = impl::Len(path);
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c == '\\' || c < 32u) return PathResult::BadChar;
        if (impl::IsParentComponentAt(path, i)) return PathResult::ParentEscape;
    }

    // "<root>/<id>/<path>" plus separators and a terminator.
    const uint32_t rootLen = impl::Len(Root());
    const char* id = ModContext::Current();
    const uint32_t idLen = impl::Len(id ? id : "");
    if (rootLen + 1u + idLen + 1u + n + 1u > kMaxScopedPath) return PathResult::TooLong;

    return PathResult::Ok;
}

// Joins a checked path under the CURRENT module's directory.
//
// `out` must hold kMaxScopedPath bytes. The identity comes from ModContext -
// what the host is running - never from the caller, so a module cannot ask for
// another module's directory by naming it.
inline PathResult Resolve(const char* path, char* out) {
    if (out) out[0] = '\0';

    const char* id = ModContext::Current();
    if (!id || id[0] == '\0') return PathResult::NoModule;

    const PathResult r = CheckScoped(path);
    if (r != PathResult::Ok) return r;

    uint32_t n = 0;
    for (const char* p = Root(); *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    if (n + 1 < kMaxScopedPath) out[n++] = '/';
    for (const char* p = id; *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    if (n + 1 < kMaxScopedPath) out[n++] = '/';
    for (const char* p = path; *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    out[n] = '\0';
    return PathResult::Ok;
}

// The host's own resource directory, for host code rather than for a mod.
inline void HostPath(const char* path, char* out) {
    uint32_t n = 0;
    for (const char* p = Root(); *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    if (n + 1 < kMaxScopedPath) out[n++] = '/';
    for (const char* p = kHostId; *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    if (n + 1 < kMaxScopedPath) out[n++] = '/';
    for (const char* p = path; p && *p && n + 1 < kMaxScopedPath; ++p) out[n++] = *p;
    out[n] = '\0';
}

} // namespace WiiXLaunch::ModFS
