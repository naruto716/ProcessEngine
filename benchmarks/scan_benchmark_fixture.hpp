#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace hexengine::benchmarks {

inline constexpr std::size_t kOneMegabyte = 1024 * 1024;
inline constexpr std::array<std::size_t, 3> kBaseRegionSizes{
    96 * kOneMegabyte,
    64 * kOneMegabyte,
    32 * kOneMegabyte,
};

struct PatternSpec {
    std::string_view name;
    std::string_view patternText;
    std::size_t regionIndex;
    std::size_t offset;
    bool injectAnchorDecoys = false;
};

inline constexpr std::array<PatternSpec, 6> kPatternSpecs{{
    {
        .name = "short_exact",
        .patternText = "48 8B 05 11 22 33 44 E8 90 90 48 85 C0 74 05",
        .regionIndex = 0,
        .offset = 0x0010'0000,
        .injectAnchorDecoys = false,
    },
    {
        .name = "short_wildcards",
        .patternText = "48 8B ?? ?? ?? 89 5C 24 ?8 41 ?4 88 7C 24 ??",
        .regionIndex = 0,
        .offset = 0x0200'0000,
        .injectAnchorDecoys = true,
    },
    {
        .name = "long_exact",
        .patternText = "F3 0F 10 05 10 20 30 40 48 8D 8B A0 00 00 00 48 89 5C 24 20 41 B8 34 12 56 78 E8 10 32 54 76 84 C0 75 0A 4C 8B D3 48 8B CE 48 8D 15 11 22 33 44",
        .regionIndex = 1,
        .offset = 0x0100'0000,
        .injectAnchorDecoys = false,
    },
    {
        .name = "long_mixed",
        .patternText = "F3 0F 10 05 ?? ?? ?? ?? 48 8D 8B A0 00 00 00 48 89 5C 24 20 41 B8 34 12 ?? ?? E8 ?? ?? ?? ?? 84 C0 75 0A 4C 8B D3 48 8B CE 48 8D 15 ?? ?? ?? ??",
        .regionIndex = 1,
        .offset = 0x0280'0000,
        .injectAnchorDecoys = true,
    },
    {
        .name = "long_nibble",
        .patternText = "48 8B ?5 ?? ?? ?? ?? 4C 8D ?D ?? ?? ?? ?? 41 B? ?4 48 89 ?C 24 ?0 0F 84 ?? ?? ?? ?? 33 D2 48 8B ?C 24 ?8",
        .regionIndex = 2,
        .offset = 0x0040'0000,
        .injectAnchorDecoys = true,
    },
    {
        .name = "no_anchor",
        .patternText = "4? ?5 A? ?? ?7 8? ?F 9? ?? B? ?2 C? ?4 D? ?? E? ?6 F? 1? ?? 2? ?8 3? ?A",
        .regionIndex = 2,
        .offset = 0x0100'0000,
        .injectAnchorDecoys = false,
    },
}};

inline constexpr std::size_t kDefaultScale = 1;
inline constexpr std::size_t kDefaultIterations = 4;
inline constexpr std::size_t kDefaultWarmupIterations = 1;

}  // namespace hexengine::benchmarks
