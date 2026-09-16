#pragma once

// WiiXLaunch::ModFS - a mod's own directory, and only its own directory.
//
// wiixl.core gives a mod two distinct reads, chosen at the call site:
// ModReadFile (this mod's directory, cannot escape it) and GameReadFile
// (game content, through the host's usual path candidates). There is no
// single call that tries the mod directory and falls back - "whichever
// resolves first wins" is order-dependent and silent. Nor is there full
// containment on the game-content side: reading game content is a large
// part of what modding is, so containment lives only on the scoped call.
//
// A scoped path resolves under WiiXLaunch/mods/<mod id>/ and may not leave
// it. Refused: an absolute path, any ".." component, a backslash or
// control character, or a path too long to resolve - as a value
// (PathResult), not a log string, so a test can assert which fired.
// Checked on the path the mod supplied, before any concatenation.
//
// Game modules (compiled into the payload, e.g. wiixlaunch-botw) are not
// mods: they use the game-content path like any other host code. The
// scoped call is for .wxlm modules, the only things with an id and a
// directory.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdint>

namespace WiiXLaunch::ModFS {

// Where every module's directory lives. The .wxlm files themselves sit in this
// directory too; a module's resources sit in a subdirectory named for its id.
constexpr const char* kModsRoot = "WiiXLaunch/mods";

// Where it actually lives this boot, which on Switch may be a per-title
// subdirectory (a Switch SD card has no per-title place for a host's own
// files, unlike Cemu's per-pack directory or Wii U's game content, so
// without this two games' modules would land in one shared directory). The
// loader prefers WiiXLaunch/mods/<titleid>/ and this follows it.
inline const char* g_Root = kModsRoot;

inline void SetRoot(const char* root) { if (root && *root) g_Root = root; }
inline const char* Root() { return g_Root; }

// The host's own resources live under a reserved id rather than beside the
// mods directory. The whole '_' prefix is reserved, not just this name.
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

// Is [p, p+3) a ".." component (bounded by separators or ends)? Checked as
// a component rather than a substring, since "a..b" and "..foo" don't
// escape anything and refusing them would make ordinary filenames
// unreadable.
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

// Checks a mod-supplied relative path without joining it. Separate from
// Resolve so a test can assert the reason on a path it never intends to
// open.
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

// Joins a checked path under the current module's directory. `out` must
// hold kMaxScopedPath bytes. Identity comes from ModContext, never the
// caller, so a module can't ask for another module's directory by naming
// it.
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
