#pragma once

#include "common.hpp"

#define EXL_MODULE_NAME "WiiXLaunch_BotW"
#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

namespace WiiXLaunch::Setting {
    constexpr size_t HeapSize       = 0x50000;
    constexpr size_t JitSize        = 0x10000;
    constexpr size_t InlinePoolSize = 0x10000;
    constexpr size_t LogBufferSize  = 512;
}

namespace exl::setting {
    constexpr size_t HeapSize       = 0x50000;
    constexpr size_t JitSize        = 0x10000;
    constexpr size_t InlinePoolSize = 0x10000;
    constexpr size_t LogBufferSize  = 512;

    /* exlaunch's own sanity checks, on our numbers. */
    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
    static_assert(ALIGN_UP(InlinePoolSize, PAGE_SIZE) == InlinePoolSize, "");
}
