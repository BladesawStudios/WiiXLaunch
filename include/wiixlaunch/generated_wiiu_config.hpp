#pragma once

#include <cstdint>

#define WUPS_PLUGIN_NAME_STR "WiiXLaunch_BotW"
#define WUPS_PLUGIN_AUTHOR_STR "Modder"
#define WUPS_PLUGIN_VERSION_STR "1.0.0"
#define WUPS_PLUGIN_DESCRIPTION_STR "Cross-platform Breath of the Wild Mod powered by WiiXLaunch"

namespace WiiXLaunch::WiiUConfig {
    constexpr uint64_t TargetTitleIds[] = { 0x00050000101C9400ULL, 0x00050000101C9500ULL, 0x00050000101C9300ULL };
    constexpr uint32_t TargetTitleIdsCount = sizeof(TargetTitleIds) / sizeof(TargetTitleIds[0]);
}
