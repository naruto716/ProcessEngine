#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "cepipeline/memory/pattern.hpp"

namespace {

using cepipeline::memory::BytePattern;

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

void expectOffsets(
    std::string_view patternText,
    std::span<const std::byte> bytes,
    std::initializer_list<std::size_t> expectedOffsets) {
    const auto pattern = BytePattern::parse(patternText);
    const auto actual = pattern.findAll(bytes);
    const std::vector<std::size_t> expected(expectedOffsets);
    expect(actual == expected, "Pattern offsets did not match expectation");
}

void runPatternTests() {
    constexpr std::array<std::byte, 10> exactBuffer{
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0xCC},
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0xCC},
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0xDD},
        std::byte{0xEE},
    };
    expectOffsets("AA BB CC", exactBuffer, {0, 3});

    constexpr std::array<std::byte, 9> wildcardBuffer{
        std::byte{0x10},
        std::byte{0xAB},
        std::byte{0x30},
        std::byte{0x10},
        std::byte{0xCD},
        std::byte{0x30},
        std::byte{0x10},
        std::byte{0xEF},
        std::byte{0x31},
    };
    expectOffsets("10 ?? 30", wildcardBuffer, {0, 3});

    constexpr std::array<std::byte, 7> nibbleBuffer{
        std::byte{0x4A},
        std::byte{0x1F},
        std::byte{0x00},
        std::byte{0x4B},
        std::byte{0x2F},
        std::byte{0x10},
        std::byte{0x5A},
    };
    expectOffsets("4? ?F", nibbleBuffer, {0, 3});

    constexpr std::array<std::byte, 6> noExactAnchorBuffer{
        std::byte{0x4A},
        std::byte{0x1F},
        std::byte{0x20},
        std::byte{0x4B},
        std::byte{0x2F},
        std::byte{0x99},
    };
    expectOffsets("4? ?F ??", noExactAnchorBuffer, {0, 3});

    constexpr std::array<std::byte, 4> allWildcardBuffer{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };
    expectOffsets("?? ??", allWildcardBuffer, {0, 1, 2});

    constexpr std::array<std::byte, 3> overlappingBuffer{
        std::byte{0xAA},
        std::byte{0xAA},
        std::byte{0xAA},
    };
    expectOffsets("AA AA", overlappingBuffer, {0, 1});

    constexpr std::array<std::byte, 6> leadingWildcardBuffer{
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0x00},
        std::byte{0x11},
        std::byte{0xAA},
        std::byte{0xBB},
    };
    expectOffsets("?? AA BB", leadingWildcardBuffer, {3});
}

}  // namespace

int main() {
    try {
        runPatternTests();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_pattern_test failed: " << exception.what() << '\n';
        return 1;
    }
}
