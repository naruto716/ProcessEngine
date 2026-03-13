#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cepipeline::tests {

inline constexpr std::array<std::byte, 16> kModulePatternBytes{
    std::byte{0x41},
    std::byte{0x42},
    std::byte{0x13},
    std::byte{0x37},
    std::byte{0xC0},
    std::byte{0xDE},
    std::byte{0x7A},
    std::byte{0xE1},
    std::byte{0x5B},
    std::byte{0xAD},
    std::byte{0xF0},
    std::byte{0x0D},
    std::byte{0x55},
    std::byte{0xAA},
    std::byte{0x11},
    std::byte{0x99},
};

inline constexpr std::string_view kModulePatternText = "41 42 13 37 C0 DE 7A E1 5B AD F0 0D 55 AA 11 99";
inline constexpr std::string_view kModulePatternWildcardText = "41 42 13 37 C0 D? 7A E1 5B AD F0 0D 55 AA 11 99";

inline constexpr std::array<std::byte, 16> kWritableInitialBytes{
    std::byte{0x10},
    std::byte{0x20},
    std::byte{0x30},
    std::byte{0x40},
    std::byte{0x50},
    std::byte{0x60},
    std::byte{0x70},
    std::byte{0x80},
    std::byte{0x90},
    std::byte{0xA0},
    std::byte{0xB0},
    std::byte{0xC0},
    std::byte{0xD0},
    std::byte{0xE0},
    std::byte{0xF0},
    std::byte{0x01},
};

inline constexpr std::array<std::byte, 16> kWritableUpdatedBytes{
    std::byte{0x01},
    std::byte{0x23},
    std::byte{0x45},
    std::byte{0x67},
    std::byte{0x89},
    std::byte{0xAB},
    std::byte{0xCD},
    std::byte{0xEF},
    std::byte{0xFE},
    std::byte{0xDC},
    std::byte{0xBA},
    std::byte{0x98},
    std::byte{0x76},
    std::byte{0x54},
    std::byte{0x32},
    std::byte{0x10},
};

inline constexpr std::size_t kPagePatternOffset = 0x120;

inline constexpr std::array<std::byte, 12> kPagePatternBytes{
    std::byte{0xDE},
    std::byte{0xAD},
    std::byte{0xBE},
    std::byte{0xEF},
    std::byte{0x13},
    std::byte{0x37},
    std::byte{0xC0},
    std::byte{0xDE},
    std::byte{0x55},
    std::byte{0x66},
    std::byte{0x77},
    std::byte{0x88},
};

inline constexpr std::string_view kPagePatternText = "DE AD BE EF 13 37 C0 DE 55 66 77 88";
inline constexpr std::string_view kPagePatternWildcardText = "DE AD B? EF 13 37 C0 DE 55 66 7? 88";

}  // namespace cepipeline::tests
