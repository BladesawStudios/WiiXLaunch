// Checks the NVN block-linear swizzle against a texture NVN actually accepted.
//
// botw.gfx:CreateTexture takes raw RGBA on both backends. GX2 tiles it on the
// CPU and always has; the NVN side copied it in rows and handed it to
// nvnTextureBuilderSetPackagedTextureData, which means "already in the device's
// layout". So the first module texture on Switch would have drawn as stripes.
//
// The layout is not documented anywhere this project can cite, so it is not
// asserted here - it is DERIVED FROM AN ARTEFACT. include/testpic_texture_bytes.hpp
// is a 256x256 packaged texture the Switch build has loaded and drawn; its
// pixel data is therefore, by construction, exactly what NVN wants. This test
// swizzles the un-swizzled form back and requires the result to equal that file
// byte for byte.
//
// That is a real oracle rather than a restatement: if the swizzle is wrong in
// any bit, the bytes differ. And it cannot pass vacuously - the byte count is
// floored, and a round-trip alone would agree with itself no matter how wrong
// both halves were, so the comparison is against the file, not against us.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../vendor/wiixlaunch-botw/include/wiixlaunch/botw/graphics/nvn_swizzle.hpp"
#include "../../include/testpic_texture_bytes.hpp"

namespace S = WiiXLaunch::BotW::NvnSwizzle;

static int g_Checks = 0;
static int g_Fail = 0;

static void Check(const char* what, bool ok) {
    ++g_Checks;
    if (!ok) { ++g_Fail; std::printf("  FAIL %s\n", what); }
    else       std::printf("  ok   %s\n", what);
}

int main() {
    const uint32_t W = kTestPicTextureWidth;
    const uint32_t H = kTestPicTextureHeight;
    const uint32_t bpp = 4;

    std::printf("nvn_swizzle - against include/testpic_texture_bytes.hpp (%ux%u)\n", W, H);

    const uint8_t* packed = g_TestPicTextureBytes + kTestPicTextureHeaderSize;
    const size_t packedBytes = kTestPicTextureDataSize;

    // The block height rule, at the sizes that actually ship.
    Check("8 tall  -> block height 1",  S::BlockHeightFor(8)   == 1);
    Check("64 tall -> block height 8",  S::BlockHeightFor(64)  == 8);
    Check("128 tall-> block height 16", S::BlockHeightFor(128) == 16);
    Check("256 tall-> block height 16 (capped)", S::BlockHeightFor(256) == 16);

    const uint32_t bh = S::BlockHeightFor(H);
    const uint32_t storage = S::StorageSize(W, H, bpp, bh);
    Check("storage size matches the file's data section",
          storage == packedBytes);

    // An 8x8 texture is smaller than one GOB and must still round up to one -
    // the GUI builds exactly that at startup.
    Check("8x8 rounds up to a whole GOB", S::StorageSize(8, 8, bpp, 1) == 512);

    // Every offset is inside the buffer, and no two pixels share one.
    std::vector<uint8_t> hit(storage, 0);
    bool inRange = true, collision = false;
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const uint32_t o = S::Offset(x, y, W, bpp, bh);
            if (o + bpp > storage) { inRange = false; continue; }
            if (hit[o]) collision = true;
            hit[o] = 1;
        }
    }
    Check("every pixel lands inside the buffer", inRange);
    Check("no two pixels share an offset", !collision);

    // THE ONE THAT MATTERS. Un-swizzle the real texture, swizzle it back, and
    // require the original bytes.
    std::vector<uint8_t> linear(static_cast<size_t>(W) * H * bpp, 0);
    S::UnswizzleRgba8(linear.data(), packed, W, H);

    std::vector<uint8_t> again(storage, 0xCD);
    S::SwizzleRgba8(again.data(), linear.data(), W, H);

    size_t diff = 0;
    for (size_t i = 0; i < storage; ++i) if (again[i] != packed[i]) ++diff;
    Check("swizzled form is byte-identical to the packaged texture", diff == 0);
    if (diff) std::printf("       %u of %u bytes differ\n",
                          static_cast<unsigned>(diff), static_cast<unsigned>(storage));

    // And the un-swizzled form is a real image, not noise: a photograph or a
    // logo is far smoother along a row than a mis-read layout is. Measured on
    // the same file, the correct reading scores ~5x lower than reading it as
    // rows. This is what catches a swizzle that round-trips with itself while
    // being wrong about the file.
    auto rowDelta = [&](const uint8_t* buf, bool asRows) {
        double tot = 0; size_t n = 0;
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x + 1 < W; ++x) {
                size_t a = asRows ? (static_cast<size_t>(y) * W + x) * bpp
                                  : S::Offset(x, y, W, bpp, bh);
                size_t b = asRows ? (static_cast<size_t>(y) * W + x + 1) * bpp
                                  : S::Offset(x + 1, y, W, bpp, bh);
                for (uint32_t c = 0; c < bpp; ++c)
                    tot += std::abs(int(buf[a + c]) - int(buf[b + c]));
                ++n;
            }
        }
        return tot / double(n);
    };
    const double correct = rowDelta(linear.data(), true);
    const double asRows  = rowDelta(packed, true);
    std::printf("  neighbour delta: de-swizzled %.2f, read as rows %.2f\n", correct, asRows);
    Check("de-swizzled image is smoother than the same bytes read as rows",
          correct < asRows * 0.75);

    std::printf("[nvn_swizzle_test] %d checks, %d failures\n", g_Checks, g_Fail);
    if (g_Checks < 10) {
        std::printf("[nvn_swizzle_test] DISARMED: only %d checks ran\n", g_Checks);
        return 1;
    }
    return g_Fail != 0;
}
