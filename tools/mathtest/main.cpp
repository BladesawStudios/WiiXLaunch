// Checks mod_math.h against libm on the host.
//
// A module cannot call libm, so these are approximations - but "approximate"
// has to mean a number, not a hope. This sweeps the ranges a mod actually uses
// and asserts the bound the header promises. It runs natively because that is
// where a correct answer to compare against exists.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define main_math_shim 1
#include "../../include/wiixlaunch/mod_math.h"

static int g_Fail = 0;
static int g_Checks = 0;

// The worst error seen, per name. The bound in mod_math.h has to be a measured
// number rather than a guess, and the only way to write one down honestly is to
// have the sweep report what it actually reached.
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
    // COMPARE AT THE SAME INPUT. The first version of this swept a double and
    // passed (float)x to us and x to libm, so every failure it reported was the
    // float cast, not the approximation: at 100 radians an angle rounds by up
    // to 3.8e-6, and sin moves by that much whoever computes it. The reference
    // therefore takes the float too, widened back - which is exact.

    // sqrt: every magnitude a game coordinate or a squared distance reaches.
    for (double xd = 1e-6; xd < 1e9; xd *= 1.7) {
        const float x = (float)xd;
        Check("sqrt", WiiXLaunch::ModMath::Sqrt(x), std::sqrt((double)x), 1e-7, true);
    }
    Check("sqrt(0)", WiiXLaunch::ModMath::Sqrt(0.0f), 0.0, 0.0, false);
    Check("sqrt(-1) is 0", WiiXLaunch::ModMath::Sqrt(-1.0f), 0.0, 0.0, false);

    // sin/cos across +/- 100 radians: a camera yaw accumulates without being
    // wrapped, so the reduction matters as much as the series.
    for (double ad = -100.0; ad <= 100.0; ad += 0.00037) {
        const float a = (float)ad;
        Check("sin", WiiXLaunch::ModMath::Sin(a), std::sin((double)a), 3e-7, false);
        Check("cos", WiiXLaunch::ModMath::Cos(a), std::cos((double)a), 3e-7, false);
    }

    // Printed whether or not anything failed: these are the numbers the header
    // quotes, and a bound is only honest if the thing it bounds is visible next
    // to it. Tighten them if a change here improves on them.
    for (int i = 0; i < 8 && g_Worst[i].what; ++i)
        std::printf("[mathtest] worst %-14s %.3g\n", g_Worst[i].what, g_Worst[i].err);
    std::printf("[mathtest] %d checks, %d failures\n", g_Checks, g_Fail);
    if (g_Checks < 500000) {
        std::printf("[mathtest] DISARMED: only %d checks ran\n", g_Checks);
        return 1;
    }
    return g_Fail != 0;
}
