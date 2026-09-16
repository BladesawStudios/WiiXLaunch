#pragma once

// A `key = value` file a module reads from its own directory
// (wiixl.core:ModReadFile, scoped by the host to the running module).
//
//     Config cfg;
//     cfg.Load("config.ini");                  // absent is fine
//     int rooms = cfg.GetInt("rooms", 45);     // clamped by the caller
//
// Format: one setting per line, `key = value`. Blank lines skipped; `#`,
// `;`, `//` start a comment to end of line. Whitespace around key/value is
// trimmed. Values are decimal or hex (`0x` prefix), may be negative.
// `true`/`false`/`yes`/`no`/`on`/`off`/`1`/`0` parse as booleans. Keys are
// case-sensitive.
//
// No sections: this is matched across the whole file, so `[Section]`
// headers (which the `.ini` name invites) don't scope a key, and a reused
// key resolves to the first hit. A section header logs a warning at load
// rather than being silently misread.
//
// The file is optional; a missing one is not an error. A malformed value is
// logged; a missing key is not.

#include <wiixlaunch/mod_log.h>
// GCC clears the buffer below with memset under -ffreestanding, so this
// header needs mod_runtime.h wherever the C library isn't already present.
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
#include <wiixlaunch/mod_runtime.h>
#endif
#include <wiixlaunch/imports/wiixl_core.h>

#include <cstdint>
#include <cstddef>

namespace WiiXLaunch {

class Config {
public:
    // A file longer than this is refused rather than truncated: half a
    // config is one with silently missing keys.
    static constexpr uint32_t kMaxBytes = 2048;

    bool Load(const char* path) {
        m_Size = 0;
        m_Loaded = false;
        if (!path) return false;

        ReadFn read = g_Read;
        if (!read) return false;

        const int32_t got = read(path, m_Buf, kMaxBytes);
        if (got < 0) {
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
        WarnOnSections(path);
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
    static inline ReadFn volatile g_Read = &wiixl_import__wiixl_core__ModReadFile;

    static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

    static bool WordIs(const char* at, const char* word) {
        uint32_t i = 0;
        for (; word[i]; ++i) {
            if (at[i] != word[i]) return false;
        }
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
            else return false;
            if (d >= base) return false;
            acc = acc * static_cast<int32_t>(base) + static_cast<int32_t>(d);
            ++digits;
        }
        if (digits == 0) return false;

        out = neg ? -acc : acc;
        return true;
    }

    void WarnOnSections(const char* path) const {
        const char* at = m_Buf;
        while (*at) {
            while (*at && IsSpace(*at) && *at != '\n') ++at;
            if (*at == '[') {
                const char* end = at;
                while (*end && *end != '\n' && *end != ']') ++end;
                WIIXL_LOG("config: %s has a section header %.*s] - this reader "
                          "has no sections, so keys are matched across the whole "
                          "file and the FIRST one wins", path,
                          static_cast<int>(end - at), at);
                return;
            }
            while (*at && *at != '\n') ++at;
            if (*at == '\n') ++at;
        }
    }

    const char* Find(const char* key) const {
        if (!m_Loaded || !key) return nullptr;

        const char* at = m_Buf;
        while (*at) {
            while (*at && IsSpace(*at)) ++at;

            const bool comment = (*at == '#') || (*at == ';') ||
                                 (at[0] == '/' && at[1] == '/');
            if (!comment && *at != '\n' && *at != '\0') {
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
