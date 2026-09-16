#pragma once

// sqrtf, sinf, cosf for a freestanding module. <cmath> is unavailable under
// -ffreestanding and libm isn't linked, so any 3D work needs these.
//
// Not __builtin_sqrtf: GCC lowers it to hardware only where an instruction
// exists. The Wii U's Espresso (PowerPC 750) has no fsqrt, only frsqrte (a
// 5-bit estimate), so the builtin would fall back to a libm call the module
// can't link.
//
// These are approximations, measured against libm by tools/mathtest across
// sqrt 1e-6..1e9 and sin/cos +/-100 radians:
//
//     sqrt    7.05e-08 relative      cos     2.19e-07 absolute
//     sin     2.14e-07 absolute
//
// Far tighter than a camera or movement vector needs, not IEEE-correct.
// Don't use these for save data. Re-run tools/mathtest/build.bat after any
// change here.

#include <cstdint>

namespace WiiXLaunch::ModMath {

namespace impl {

// Union punning, not memcpy: the host-side test builds under MSVC, which has
// no __builtin_memcpy, and a real memcpy would need libc.
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

// Newton-Raphson from a bit-shift seed (the classic fast-inverse-sqrt-style
// constant), refined three times to float precision.
inline float Sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x != x) return x;    // NaN

    uint32_t bits = impl::FloatToBits(x);
    bits = 0x1fbd1df5u + (bits >> 1);
    float r = impl::BitsToFloat(bits);

    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    return r;
}

namespace impl {

// Taylor series on [-pi/2, pi/2], Horner form so small terms don't vanish
// into the large one.
inline float SinCore(float a) {
    const float a2 = a * a;
    return a * (1.0f
         + a2 * (-1.0f / 6.0f
         + a2 * (1.0f / 120.0f
         + a2 * (-1.0f / 5040.0f
         + a2 * (1.0f / 362880.0f
         + a2 * (-1.0f / 39916800.0f))))));
}

// Reduces a + offset to [-pi, pi] in double: at large angles a float
// multiply-subtract has already lost the digits the result depends on.
// `offset` (cos's pi/2 shift) is added here rather than in float, since a
// float ulp at 100 radians is 7.6e-6 and cos measured 1.7e-6 wrong when
// added outside the double arithmetic.
inline float Wrap(float a, double offset) {
    const double x = static_cast<double>(a) + offset;
    const double twoPi = 6.283185307179586476925286766559;
    double k = x / twoPi;
    k = (k >= 0.0) ? static_cast<double>(static_cast<long long>(k + 0.5))
                   : static_cast<double>(static_cast<long long>(k - 0.5));
    return static_cast<float>(x - k * twoPi);
}

// Shared by sin and cos so they can't drift apart at the fold points.
inline float SinReduced(float a, double offset) {
    float x = Wrap(a, offset);
    if (x > kHalfPi)       x =  kPi - x;
    else if (x < -kHalfPi) x = -kPi - x;
    return SinCore(x);
}

} // namespace impl

inline float Sin(float a) { return impl::SinReduced(a, 0.0); }

// cos(x) == sin(x + pi/2).
inline float Cos(float a) {
    return impl::SinReduced(a, 1.5707963267948966192313216916398);
}

} // namespace WiiXLaunch::ModMath

// No shim into namespace std: a mod using <cmath> switches its call sites
// to these explicitly instead.
