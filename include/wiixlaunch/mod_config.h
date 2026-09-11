#pragma once

// A `key = value` file a module reads from its own directory.
//
// A compiled module bakes its constants in, which is right for an address table
// and wrong for a number the user is meant to choose. The alternative shipped by
// the mod this was written for was TWO COMPLETE PAYLOAD BINARIES under an
// options/ folder, differing in one #define - because a compile-time setting can
// only be changed by compiling.
//
// wiixl.core:ModReadFile already reads from mods/<id>/, scoped by the HOST to
// whichever module is running, so a mod cannot read another mod's files by
// naming them. This is the parsing half, in the SDK rather than in each mod,
// because every mod that wants a setting would otherwise write a slightly
// different integer parser and get the edge cases slightly differently wrong.
//
// THE FILE IS OPTIONAL AND A MISSING ONE IS NOT AN ERROR. What must never be
// silent is a file that exists and says something this does not understand: a
// user who writes `rooms = fourty` and gets the default has been told nothing,
// and will report that the setting does not work. So a malformed value is
// LOGGED and a missing key is not - those are different events and they read
// differently.
//
//     Config cfg;
//     cfg.Load("config.txt");                  // absent is fine
//     int rooms = cfg.GetInt("rooms", 45);     // clamped by the caller
//
// FORMAT. One setting per line, `key = value`. Blank lines are skipped; `#`,
// `;` and `//` start a comment, to end of line. Whitespace around the key and
// value is trimmed. Values are decimal, or hex with a 0x prefix, and may be
// negative. `true`/`false`/`yes`/`no`/`on`/`off` parse as booleans, as do 1 and
// 0. Keys are matched case-sensitively, because a key that works in two
// spellings is two keys to document.

#include <wiixlaunch/mod_log.h>
// The buffer below is a zero-initialised 2 KB array, which GCC clears with a
// call to memset whatever -ffreestanding says - so the header that creates
// that need is the one that has to satisfy it. Without this a mod including
// only mod_config.h links cleanly and branches to address 0 the first time it
// builds a Config.
//
// __STDC_HOSTED__ and not a compiler check: the question is whether this
// build has a C library, and a freestanding one answers 0. tools/config_test
// drives this parser on a PC, where memset already exists and mod_runtime.h
// would collide with it - and where its __attribute__((used)) does not parse.
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
#include <wiixlaunch/mod_runtime.h>
#endif
#include <wiixlaunch/imports/wiixl_core.h>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch {

class Config {
public:
    // Big enough for a settings file and small enough to sit in a module's bss
    // without arguing with the arena. A file longer than this is REFUSED rather
    // than truncated: half a config is a config with silently missing keys.
    static constexpr uint32_t kMaxBytes = 2048;

    // Returns false when the file is absent, unreadable, or too large - all
    // three logged distinctly, because "you have no config" and "your config was
    // ignored" are not the same news.
    bool Load(const char* path) {
        m_Size = 0;
        m_Loaded = false;
        if (!path) return false;

        ReadFn read = g_Read;
        if (!read) return false;

        const int32_t got = read(path, m_Buf, kMaxBytes);
        if (got < 0) {
            // The host distinguishes "not there" from "could not read it"; this
            // only sees a negative, so it says what it knows and no more.
            WIIXL_LOG("config: %s not read (%d) - using built-in defaults", path, got);
            return false;
        }
        if (static_cast<uint32_t>(got) >= kMaxBytes) {
            WIIXL_LOG("config: %s is %d bytes, over the %u-byte limit - IGNORED "
                      "entirely rather than read in part", path, got, kMaxBytes);
            return false;
        }

        m_Size = static_cast<uint32_t>(got);
        m_Buf[m_Size] = '\0';
        m_Loaded = true;
        WIIXL_LOG("config: %s loaded (%u bytes)", path, m_Size);
        return true;
    }

    bool Loaded() const { return m_Loaded; }

    int32_t GetInt(const char* key, int32_t fallback) const {
        const char* v = Find(key);
        if (!v) return fallback;

        int32_t out = 0;
        if (!ParseInt(v, out)) {
            WIIXL_LOG("config: %s is not a number - using %d", key, fallback);
            return fallback;
        }
        return out;
    }

    // Clamped, and LOUD about it. A user who asks for 900 rooms should be told
    // they got 128, not left to discover it.
    int32_t GetIntClamped(const char* key, int32_t fallback,
                          int32_t lo, int32_t hi) const {
        const int32_t v = GetInt(key, fallback);
        if (v < lo) {
            WIIXL_LOG("config: %s = %d is below the minimum %d - using %d", key, v, lo, lo);
            return lo;
        }
        if (v > hi) {
            WIIXL_LOG("config: %s = %d is above the maximum %d - using %d", key, v, hi, hi);
            return hi;
        }
        return v;
    }

    bool GetBool(const char* key, bool fallback) const {
        const char* v = Find(key);
        if (!v) return fallback;

        if (WordIs(v, "true") || WordIs(v, "yes") || WordIs(v, "on") || WordIs(v, "1"))
            return true;
        if (WordIs(v, "false") || WordIs(v, "no") || WordIs(v, "off") || WordIs(v, "0"))
            return false;

        WIIXL_LOG("config: %s is not a yes/no value - using %d", key, fallback ? 1 : 0);
        return fallback;
    }

private:
    using ReadFn = int32_t (*)(const char*, void*, uint32_t);
    // volatile for the reason in docs/modules.md: the loader writes this pointer
    // at relocation time, and without volatile the compiler folds it into a
    // direct branch that cannot reach a host address.
    static inline ReadFn volatile g_Read = &wiixl_import__wiixl_core__ModReadFile;

    static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

    static bool WordIs(const char* at, const char* word) {
        uint32_t i = 0;
        for (; word[i]; ++i) {
            if (at[i] != word[i]) return false;
        }
        // The candidate must END here, or "on" would match "onwards".
        const char c = at[i];
        return c == '\0' || c == '\n' || IsSpace(c);
    }

    static bool ParseInt(const char* at, int32_t& out) {
        bool neg = false;
        if (*at == '-') { neg = true; ++at; }
        else if (*at == '+') { ++at; }

        uint32_t base = 10;
        if (at[0] == '0' && (at[1] == 'x' || at[1] == 'X')) { base = 16; at += 2; }

        int32_t acc = 0;
        uint32_t digits = 0;
        for (; *at && *at != '\n' && !IsSpace(*at); ++at) {
            uint32_t d;
            if (*at >= '0' && *at <= '9')        d = static_cast<uint32_t>(*at - '0');
            else if (base == 16 && *at >= 'a' && *at <= 'f') d = static_cast<uint32_t>(*at - 'a' + 10);
            else if (base == 16 && *at >= 'A' && *at <= 'F') d = static_cast<uint32_t>(*at - 'A' + 10);
            else return false;                   // a stray character is a typo, not a zero
            if (d >= base) return false;
            acc = acc * static_cast<int32_t>(base) + static_cast<int32_t>(d);
            ++digits;
        }
        if (digits == 0) return false;           // "rooms =" with nothing after it

        out = neg ? -acc : acc;
        return true;
    }

    // Returns a pointer at the first character of the value, or null.
    const char* Find(const char* key) const {
        if (!m_Loaded || !key) return nullptr;

        const char* at = m_Buf;
        while (*at) {
            // Start of a line: skip indentation.
            while (*at && IsSpace(*at)) ++at;

            const bool comment = (*at == '#') || (*at == ';') ||
                                 (at[0] == '/' && at[1] == '/');
            if (!comment && *at != '\n' && *at != '\0') {
                // Compare the key up to '=' or whitespace.
                uint32_t i = 0;
                while (key[i] && at[i] == key[i]) ++i;
                if (key[i] == '\0') {
                    const char* after = at + i;
                    while (*after && IsSpace(*after)) ++after;
                    if (*after == '=') {
                        ++after;
                        while (*after && IsSpace(*after)) ++after;
                        return after;
                    }
                }
            }

            // To the end of this line, whatever it turned out to be.
            while (*at && *at != '\n') ++at;
            if (*at == '\n') ++at;
        }
        return nullptr;
    }

    char     m_Buf[kMaxBytes + 1] = {};
    uint32_t m_Size = 0;
    bool     m_Loaded = false;
};

} // namespace WiiXLaunch
