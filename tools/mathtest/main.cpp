// Host-side verification of mod_math.h approximations against libm reference values.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define main_math_shim 1
#include "../../include/wiixlaunch/mod_math.h"

static int g_Fail = 0;
static int g_Checks = 0;

// Tracks worst-case observed error across test sweeps.
struct Worst { const char* what; double err; };
static Worst g_Worst[8] = {};

static void Note(const char* what, double err) {
    for (int i = 0; i < 8; ++i) {
        if (g_Worst[i].what == nullptr) { g_Worst[i].what = what; g_Worst[i].err = err; return; }
        if (g_Worst[i].what == what) {
            if (err > g_Worst[i].err) g_Worst[i].err = err;
            return;
        }
    }
}

static void Check(const char* what, double got, double want, double tol, bool rel) {
    ++g_Checks;
    const double err = rel && want != 0.0 ? std::fabs((got - want) / want)
                                          : std::fabs(got - want);
    Note(what, err);
    if (err > tol) {
        ++g_Fail;
        std::printf("  FAIL %-28s got %.9g want %.9g err %.3g > %.3g\n",
                    what, got, want, err, tol);
    }
}

int main() {
    // Reference libm calls use widened float input to test approximation rather than precision cast.

    // Sqrt across range 1e-6 to 1e9.
    for (double xd = 1e-6; xd < 1e9; xd *= 1.7) {
        const float x = (float)xd;
        Check("sqrt", WiiXLaunch::ModMath::Sqrt(x), std::sqrt((double)x), 1e-7, true);
    }
    Check("sqrt(0)", WiiXLaunch::ModMath::Sqrt(0.0f), 0.0, 0.0, false);
    Check("sqrt(-1) is 0", WiiXLaunch::ModMath::Sqrt(-1.0f), 0.0, 0.0, false);

    // Sin/cos sweep across +/- 100 radians.
    for (double ad = -100.0; ad <= 100.0; ad += 0.00037) {
        const float a = (float)ad;
        Check("sin", WiiXLaunch::ModMath::Sin(a), std::sin((double)a), 3e-7, false);
        Check("cos", WiiXLaunch::ModMath::Cos(a), std::cos((double)a), 3e-7, false);
    }

    // Report observed worst-case errors.
    for (int i = 0; i < 8 && g_Worst[i].what; ++i)
        std::printf("[mathtest] worst %-14s %.3g\n", g_Worst[i].what, g_Worst[i].err);
    std::printf("[mathtest] %d checks, %d failures\n", g_Checks, g_Fail);
    if (g_Checks < 500000) {
        std::printf("[mathtest] DISARMED: only %d checks ran\n", g_Checks);
        return 1;
    }
    return g_Fail != 0;
}
