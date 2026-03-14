#include "cepipeline/memory/pattern.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace cepipeline::memory {
namespace {

[[nodiscard]] int hexValue(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + (value - 'a');
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + (value - 'A');
    }
    return -1;
}

[[nodiscard]] PatternToken parseToken(std::string_view token) {
    if (token == "?" || token == "??") {
        return {};
    }

    if (token.size() != 2) {
        throw std::invalid_argument("Invalid AOB token length");
    }

    PatternToken parsed{};
    if (token[0] == '?') {
        parsed.mask = 0x0F;
    } else {
        const auto nibble = hexValue(token[0]);
        if (nibble < 0) {
            throw std::invalid_argument("Invalid high nibble in AOB token");
        }
        parsed.value = static_cast<std::uint8_t>(nibble << 4);
        parsed.mask |= 0xF0;
    }

    if (token[1] != '?') {
        const auto nibble = hexValue(token[1]);
        if (nibble < 0) {
            throw std::invalid_argument("Invalid low nibble in AOB token");
        }
        parsed.value |= static_cast<std::uint8_t>(nibble);
        parsed.mask |= 0x0F;
    }

    return parsed;
}

}  // namespace

bool PatternToken::matches(std::byte byte) const noexcept {
    const auto candidate = static_cast<std::uint8_t>(byte);
    return (candidate & mask) == value;
}

BytePattern BytePattern::parse(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::string token;
    std::vector<PatternToken> tokens;

    while (stream >> token) {
        tokens.push_back(parseToken(token));
    }

    if (tokens.empty()) {
        throw std::invalid_argument("Byte pattern must contain at least one token");
    }

    return BytePattern(std::move(tokens));
}

BytePattern::BytePattern(std::vector<PatternToken> tokens)
    : tokens_(std::move(tokens)) {
    rebuildAnchor();
}

bool BytePattern::empty() const noexcept {
    return tokens_.empty();
}

std::size_t BytePattern::size() const noexcept {
    return tokens_.size();
}

bool BytePattern::matches(std::span<const std::byte> bytes) const noexcept {
    if (bytes.size() < tokens_.size()) {
        return false;
    }

    return matchesAt(bytes, 0);
}

bool BytePattern::matchesAt(std::span<const std::byte> bytes, std::size_t offset) const noexcept {
    if (offset > bytes.size() || bytes.size() - offset < tokens_.size()) {
        return false;
    }

    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        if (!tokens_[i].matches(bytes[offset + i])) {
            return false;
        }
    }

    return true;
}

std::vector<std::size_t> BytePattern::findAll(std::span<const std::byte> bytes) const {
    std::vector<std::size_t> offsets;
    if (tokens_.empty() || bytes.size() < tokens_.size()) {
        return offsets;
    }

    const auto lastStart = bytes.size() - tokens_.size();
    if (anchorBytes_.empty()) {
        for (std::size_t offset = 0; offset <= lastStart; ++offset) {
            if (matchesAt(bytes, offset)) {
                offsets.push_back(offset);
            }
        }

        return offsets;
    }

    const auto byteHash = [](std::byte value) noexcept {
        return static_cast<std::size_t>(std::to_integer<unsigned char>(value));
    };
    const auto searcher = std::boyer_moore_horspool_searcher(
        anchorBytes_.begin(),
        anchorBytes_.end(),
        byteHash,
        std::equal_to<>{});

    auto current = bytes.begin();
    while (current != bytes.end()) {
        const auto found = std::search(current, bytes.end(), searcher);
        if (found == bytes.end()) {
            break;
        }

        const auto anchorPosition = static_cast<std::size_t>(std::distance(bytes.begin(), found));
        if (anchorPosition >= anchorOffset_) {
            const auto candidateOffset = anchorPosition - anchorOffset_;
            if (candidateOffset <= lastStart && matchesAt(bytes, candidateOffset)) {
                offsets.push_back(candidateOffset);
            }
        }

        current = found + 1;
    }

    return offsets;
}

void BytePattern::rebuildAnchor() {
    anchorBytes_.clear();
    anchorOffset_ = 0;

    std::size_t bestOffset = 0;
    std::size_t bestLength = 0;
    std::size_t runOffset = 0;
    std::size_t runLength = 0;

    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i].mask == 0xFF) {
            if (runLength == 0) {
                runOffset = i;
            }
            ++runLength;
            if (runLength > bestLength) {
                bestOffset = runOffset;
                bestLength = runLength;
            }
        } else {
            runLength = 0;
        }
    }

    if (bestLength == 0) {
        return;
    }

    anchorOffset_ = bestOffset;
    anchorBytes_.reserve(bestLength);
    for (std::size_t i = 0; i < bestLength; ++i) {
        anchorBytes_.push_back(std::byte{tokens_[bestOffset + i].value});
    }
}

std::string BytePattern::canonical() const {
    std::ostringstream stream;
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        if (i != 0) {
            stream << ' ';
        }

        const auto& token = tokens_[i];
        if (token.mask == 0x00) {
            stream << "??";
            continue;
        }

        const auto high = (token.mask & 0xF0) == 0xF0
            ? "0123456789ABCDEF"[token.value >> 4]
            : '?';
        const auto low = (token.mask & 0x0F) == 0x0F
            ? "0123456789ABCDEF"[token.value & 0x0F]
            : '?';
        stream << high << low;
    }

    return stream.str();
}

const std::vector<PatternToken>& BytePattern::tokens() const noexcept {
    return tokens_;
}

}  // namespace cepipeline::memory
