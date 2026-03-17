#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "hexengine/core/memory_types.hpp"

namespace hexengine::tests::support {

[[nodiscard]] inline std::optional<hexengine::core::Address> decodeRelativeControlTarget(
    hexengine::core::Address instructionAddress,
    std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return std::nullopt;
    }

    const auto opcode = std::to_integer<std::uint8_t>(bytes[0]);
    if ((opcode == 0xE8 || opcode == 0xE9) && bytes.size() >= 5) {
        const auto b0 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1]));
        const auto b1 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8;
        const auto b2 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 16;
        const auto b3 = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[4])) << 24;
        const auto displacement = static_cast<std::int32_t>(b0 | b1 | b2 | b3);
        return static_cast<hexengine::core::Address>(
            static_cast<std::intptr_t>(instructionAddress + 5) + displacement);
    }

    if (opcode == 0xEB && bytes.size() >= 2) {
        const auto displacement = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(bytes[1]));
        return static_cast<hexengine::core::Address>(
            static_cast<std::intptr_t>(instructionAddress + 2) + displacement);
    }

    return std::nullopt;
}

}  // namespace hexengine::tests::support
