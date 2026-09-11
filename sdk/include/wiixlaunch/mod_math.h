#pragma once

// sqrtf, sinf and cosf for a freestanding module.
//
// <cmath> is not available: libstdc++ refuses it outright under -ffreestanding
// ("This header is not available in freestanding mode"), and even if it were,
// the calls it makes land in libm, which a .wxlm does not link.
//
// So a mod doing any 3D work - a direction to a target, a camera basis, a
// distance check - has nothing to call. AIPuppet needed exactly these three,
// thirteen times, and there is no reason the next mod will not.
//
// WHY NOT __builtin_sqrtf. GCC lowers it to a hardware instruction only where
// one exists. AArch64 has fsqrt; the Wii U's Espresso is a PowerPC 750 and has
// frsqrte (an ESTIMATE, 5 bits) but no fsqrt, so the builtin becomes a call to
// libm and the module is back where it started - except now it links cleanly
// and branches to 0, which is the failure mod_runtime.h exists to prevent.
//
// These are therefore written out, and they are approximations. Accuracy is
// checked against libm on the host by tools/mathtest, which sweeps sqrt from
// 1e-6 to 1e9 and sin/cos across +/- 100 radians - a million points - and
// reports the worst it saw. MEASURED, not hoped:
//
//     sqrt    7.05e-08 relative      bound asserted 1e-7
//     sin     2.14e-07 absolute      bound asserted 3e-7
//     cos     2.19e-07 absolute      bound asserted 3e-7
//
// which is far tighter than anything a camera or a movement vector needs and
// nowhere near an IEEE-correct result. Do not use these to compute a save
// file, and if you change anything here, re-run tools/mathtest/build.bat -
// two real errors in this header were found by nothing else.

#include <cstdint>

namespace WiiXLaunch::ModMath {

namespace impl {

// A union, not memcpy. <cstring> is one of the headers freestanding does give
// you, but the host test that checks these against libm builds under MSVC,
// which has no __builtin_memcpy, and reaching for the real memcpy would put a
// libc call in a module that does not link one. Union punning is what both GCC
// and MSVC document as supported, and both fold it to nothing.
union Punner {
    float    f;
    uint32_t b;
};

inline float BitsToFloat(uint32_t b) {
    Punner p;
    p.b = b;
    return p.f;
}

inline uint32_t FloatToBits(float f) {
    Punner p;
    p.f = f;
    return p.b;
}

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kHalfPi = 1.57079632679489661923f;

} // namespace impl

// Newton-Raphson from a bit-shift seed.
//
// The seed halves the exponent by arithmetic on the bit pattern, which lands
// within a factor of two, and each iteration doubles the correct digits. Three
// gets to float precision from there with room to spare.
inline float Sqrt(float x) {
    if (x <= 0.0f) return 0.0f;                       // and NaN, which compares false
    if (x != x) return x;

    uint32_t bits = impl::FloatToBits(x);
    // 0x1fbd1df5 is the classic magic constant for this seed: (bits >> 1) puts
    // the exponent right and the constant corrects the mantissa's offset.
    bits = 0x1fbd1df5u + (bits >> 1);
    float r = impl::BitsToFloat(bits);

    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    return r;
}

namespace impl {

// sin on [-pi/2, pi/2] by its Taylor series, which converges fast enough over
// that range that seven terms is under a float ulp near the ends.
inline float SinCore(float a) {
    const float a2 = a * a;
    // Horner, so the small terms are added first and do not vanish into the
    // large one.
    return a * (1.0f
         + a2 * (-1.0f / 6.0f
         + a2 * (1.0f / 120.0f
         + a2 * (-1.0f / 5040.0f
         + a2 * (1.0f / 362880.0f
         + a2 * (-1.0f / 39916800.0f))))));
}

// Reduce a + offset to [-pi, pi]. Done in double because the subtraction
// cancels: for an angle of 100 radians a float multiply-subtract has already
// lost the digits the result depends on.
//
// THE OFFSET IS A PARAMETER for cos. Cos was Sin(a + kHalfPi), and that
// addition happens in float: at 100 radians a float ulp is 7.6e-6, so the sum
// rounds by up to half of that and cos comes out 1.7e-6 wrong - measured, by
// tools/mathtest, which is the only reason this is not still shipping. Adding
// it here instead puts it inside the double arithmetic, where 100 + pi/2 is
// exact to 1e-14, and the only rounding left is the one float result.
inline float Wrap(float a, double offset) {
    const double x = static_cast<double>(a) + offset;
    const double twoPi = 6.283185307179586476925286766559;
    double k = x / twoPi;
    k = (k >= 0.0) ? static_cast<double>(static_cast<long long>(k + 0.5))
                   : static_cast<double>(static_cast<long long>(k - 0.5));
    return static_cast<float>(x - k * twoPi);
}

// Shared by both: everything after the reduction is identical, so cos cannot
// drift away from sin at the fold points.
inline float SinReduced(float a, double offset) {
    float x = Wrap(a, offset);                         // [-pi, pi]
    // Fold the outer quarters onto the core's range: sin(pi - x) == sin(x).
    if (x > kHalfPi)       x =  kPi - x;
    else if (x < -kHalfPi) x = -kPi - x;
    return SinCore(x);
}

} // namespace impl

inline float Sin(float a) { return impl::SinReduced(a, 0.0); }

// cos(x) == sin(x + pi/2), with the shift folded into the reduction.
inline float Cos(float a) {
    return impl::SinReduced(a, 1.5707963267948966192313216916398);
}

} // namespace WiiXLaunch::ModMath

// NO SHIM INTO namespace std. Defining std::sqrt is undefined behaviour even
// where nothing else declares it, and on a hosted compiler - which is where
// these get checked against libm - it collides outright. A mod that used
// <cmath> changes its call sites to these instead; there are never many, and
// an explicit name says which implementation is running.
