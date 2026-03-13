#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cepipeline::memory {

struct PatternToken {
    std::uint8_t value = 0;
    std::uint8_t mask = 0x00;

    [[nodiscard]] bool matches(std::byte byte) const noexcept;
};

class BytePattern {
public:
    [[nodiscard]] static BytePattern parse(std::string_view text);

    BytePattern() = default;
    explicit BytePattern(std::vector<PatternToken> tokens);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool matches(std::span<const std::byte> bytes) const noexcept;
    [[nodiscard]] std::vector<std::size_t> findAll(std::span<const std::byte> bytes) const;
    [[nodiscard]] std::string canonical() const;
    [[nodiscard]] const std::vector<PatternToken>& tokens() const noexcept;

private:
    std::vector<PatternToken> tokens_;
};

}  // namespace cepipeline::memory
