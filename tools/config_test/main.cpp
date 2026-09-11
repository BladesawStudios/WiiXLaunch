// Checks wiixlaunch/mod_config.h against files a user might actually write.
//
// The parser exists so that a mod's limits can be changed without rebuilding it,
// which means the inputs are written BY HAND by people who have never seen the
// grammar. So the cases here are not "does it read 45" - they are the ways a
// hand-written file goes wrong: a trailing comment, a missing space, a key that
// is a prefix of another key, a value that is a word, a file with no trailing
// newline, CRLF from Notepad.
//
// The module-side imports are DEFINED here rather than stubbed away, which is
// what lets the test drive Load(): ModReadFile hands back whatever text the case
// wants, so the parser is exercised through its real entry point instead of
// through a back door that only the test uses.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// --- the imports mod_config.h binds ----------------------------------------
static const char* g_File = nullptr;
static bool        g_FileMissing = false;

extern "C" void wiixl_import__wiixl_core__Log(const char* text) {
    // Quiet by default; the parser logs on every malformed value and the point
    // of the test is the value it returns, not the noise on the way.
    (void)text;
}

extern "C" int32_t wiixl_import__wiixl_core__ModReadFile(const char* path,
                                                         void* buffer,
                                                         uint32_t maxSize) {
    (void)path;
    if (g_FileMissing || !g_File) return -1;
    const uint32_t n = static_cast<uint32_t>(std::strlen(g_File));
    if (n >= maxSize) return static_cast<int32_t>(n);   // over the limit; refused
    std::memcpy(buffer, g_File, n);
    return static_cast<int32_t>(n);
}

extern "C" uint32_t wiixl_import__wiixl_core__ModFileExists(const char* path) {
    (void)path;
    return (g_FileMissing || !g_File) ? 0u : 1u;
}

#include <wiixlaunch/mod_config.h>

static int g_Checks = 0;
static int g_Fail = 0;

static void Eq(const char* what, long got, long want) {
    ++g_Checks;
    if (got != want) {
        ++g_Fail;
        std::printf("  FAIL %-52s got %ld want %ld\n", what, got, want);
    } else {
        std::printf("  ok   %-52s %ld\n", what, got);
    }
}

static WiiXLaunch::Config Load(const char* text) {
    g_File = text;
    g_FileMissing = (text == nullptr);
    static WiiXLaunch::Config cfg;   // 2 KB; one instance, reloaded per case
    cfg.Load("config.txt");
    return cfg;
}

int main() {
    std::printf("config_test - mod_config.h against hand-written files\n");

    // --- the ordinary case --------------------------------------------------
    {
        auto c = Load("rooms = 60\nparts = 46\n");
        Eq("rooms = 60", c.GetInt("rooms", 45), 60);
        Eq("parts = 46", c.GetInt("parts", 21), 46);
        Eq("a key that is not there falls back", c.GetInt("nope", 7), 7);
        Eq("file reports loaded", c.Loaded() ? 1 : 0, 1);
    }

    // --- spacing, because people type what they like ------------------------
    {
        auto c = Load("rooms=60\n   parts   =   46   \n");
        Eq("no spaces around =", c.GetInt("rooms", 0), 60);
        Eq("spaces everywhere", c.GetInt("parts", 0), 46);
    }

    // --- comments -----------------------------------------------------------
    {
        auto c = Load("# rooms = 99\n; parts = 99\n// parts = 98\nrooms = 60\n");
        Eq("hash-commented line is not read", c.GetInt("rooms", 0), 60);
        Eq("semicolon and slash comments are not read", c.GetInt("parts", 21), 21);
    }

    // --- a key that is a PREFIX of another ----------------------------------
    //
    // "rooms" must not match "rooms_extra", and looking only at the first five
    // characters is exactly how a naive parser gets this wrong.
    {
        auto c = Load("rooms_extra = 99\nrooms = 60\n");
        Eq("a longer key is not matched by its prefix", c.GetInt("rooms", 0), 60);
        Eq("the longer key still reads", c.GetInt("rooms_extra", 0), 99);
    }
    {
        auto c = Load("rooms_extra = 99\n");
        Eq("prefix key absent means fallback, not 99", c.GetInt("rooms", 45), 45);
    }

    // --- malformed values fall back, and are not read as zero ---------------
    {
        auto c = Load("rooms = fourty\n");
        Eq("a word is not a number", c.GetInt("rooms", 45), 45);
    }
    {
        auto c = Load("rooms =\n");
        Eq("an empty value is not zero", c.GetInt("rooms", 45), 45);
    }
    {
        auto c = Load("rooms = 60abc\n");
        Eq("trailing junk rejects the whole value", c.GetInt("rooms", 45), 45);
    }

    // --- number forms -------------------------------------------------------
    {
        auto c = Load("a = 0x2E\nb = -5\nc = +7\n");
        Eq("hex", c.GetInt("a", 0), 0x2E);
        Eq("negative", c.GetInt("b", 0), -5);
        Eq("explicit plus", c.GetInt("c", 0), 7);
    }

    // --- CRLF, because the file will be edited in Notepad -------------------
    {
        auto c = Load("rooms = 60\r\nparts = 46\r\n");
        Eq("CRLF line endings", c.GetInt("rooms", 0), 60);
        Eq("CRLF second line", c.GetInt("parts", 0), 46);
    }

    // --- no trailing newline on the last line -------------------------------
    {
        auto c = Load("rooms = 60");
        Eq("last line without a newline", c.GetInt("rooms", 0), 60);
    }

    // --- booleans -----------------------------------------------------------
    {
        auto c = Load("a = true\nb = no\nc = ON\nd = 1\ne = 0\nf = maybe\n");
        Eq("true",            c.GetBool("a", false) ? 1 : 0, 1);
        Eq("no",              c.GetBool("b", true)  ? 1 : 0, 0);
        Eq("ON is not on (case-sensitive)", c.GetBool("c", false) ? 1 : 0, 0);
        Eq("1",               c.GetBool("d", false) ? 1 : 0, 1);
        Eq("0",               c.GetBool("e", true)  ? 1 : 0, 0);
        Eq("a word falls back", c.GetBool("f", true) ? 1 : 0, 1);
        Eq("missing bool falls back", c.GetBool("zz", true) ? 1 : 0, 1);
    }

    // --- clamping -----------------------------------------------------------
    {
        auto c = Load("rooms = 900\n");
        Eq("above the maximum clamps down", c.GetIntClamped("rooms", 45, 21, 128), 128);
    }
    {
        auto c = Load("rooms = 2\n");
        Eq("below the minimum clamps up", c.GetIntClamped("rooms", 45, 21, 128), 21);
    }
    {
        auto c = Load("rooms = 60\n");
        Eq("in range is untouched", c.GetIntClamped("rooms", 45, 21, 128), 60);
    }

    // --- no file at all -----------------------------------------------------
    {
        auto c = Load(nullptr);
        Eq("absent file is not loaded", c.Loaded() ? 1 : 0, 0);
        Eq("absent file gives every fallback", c.GetInt("rooms", 45), 45);
    }

    // --- a file over the limit is refused ENTIRELY --------------------------
    //
    // Half a config is a config with silently missing keys, which is worse than
    // no config at all because the defaults then look like settings.
    {
        static char big[WiiXLaunch::Config::kMaxBytes + 64];
        std::memset(big, ' ', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        std::memcpy(big, "rooms = 60\n", 11);
        auto c = Load(big);
        Eq("oversize file is not loaded", c.Loaded() ? 1 : 0, 0);
        Eq("oversize file reads nothing", c.GetInt("rooms", 45), 45);
    }

    std::printf("[config_test] %d checks, %d failures\n", g_Checks, g_Fail);
    if (g_Checks < 30) {
        std::printf("[config_test] DISARMED: only %d checks ran\n", g_Checks);
        return 1;
    }
    return g_Fail != 0;
}
