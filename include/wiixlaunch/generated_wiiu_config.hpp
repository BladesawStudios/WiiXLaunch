#pragma once

#include <cstdint>

#define WUPS_PLUGIN_NAME_STR "WiiXLaunch_TotK"
#define WUPS_PLUGIN_AUTHOR_STR "Mindstormman"
#define WUPS_PLUGIN_VERSION_STR "1.0.0"
#define WUPS_PLUGIN_DESCRIPTION_STR "WiiXLaunch host for Tears of the Kingdom 1.2.1"

namespace WiiXLaunch::WiiUConfig {
    constexpr uint64_t TargetTitleIds[] = { 0x00050000101C9400ULL, 0x00050000101C9500ULL };
    constexpr uint32_t TargetTitleIdsCount = sizeof(TargetTitleIds) / sizeof(TargetTitleIds[0]);
}
