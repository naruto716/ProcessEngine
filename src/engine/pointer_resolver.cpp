#include "hexengine/engine/pointer_resolver.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace hexengine::engine {
namespace {

[[nodiscard]] core::Address addChecked(core::Address value, core::Address delta) {
    if (delta > std::numeric_limits<core::Address>::max() - value) {
        throw std::overflow_error("Pointer expression overflowed address range");
    }
    return value + delta;
}

[[nodiscard]] core::Address applyOffset(core::Address value, std::ptrdiff_t offset) {
    if (offset >= 0) {
        return addChecked(value, static_cast<core::Address>(offset));
    }

    const auto delta = static_cast<core::Address>(-offset);
    if (value < delta) {
        throw std::overflow_error("Pointer expression underflowed address range");
    }
    return value - delta;
}

[[nodiscard]] bool isHexDigit(char ch) noexcept {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] bool looksLikeHexLiteral(std::string_view token) noexcept {
    if (token.empty()) {
        return false;
    }

    if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        return std::all_of(token.begin() + 2, token.end(), isHexDigit);
    }

    return std::all_of(token.begin(), token.end(), isHexDigit);
}

[[nodiscard]] core::Address parseHexLiteral(std::string_view token) {
    if (token.empty()) {
        throw std::invalid_argument("Pointer expression contains an empty number");
    }

    std::uint64_t value = 0;
    auto digits = token;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits.remove_prefix(2);
    }

    if (digits.empty()) {
        throw std::invalid_argument("Pointer expression contains an invalid number");
    }

    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
        throw std::invalid_argument("Pointer expression contains an invalid hex literal");
    }

    return static_cast<core::Address>(value);
}

}  // namespace

class PointerExpressionParser {
public:
    PointerExpressionParser(std::string_view text, const PointerResolver& resolver)
        : text_(text),
          resolver_(resolver) {
    }

    [[nodiscard]] core::Address parse() {
        const auto value = parseExpression();
        skipSpaces();
        if (position_ != text_.size()) {
            throw std::invalid_argument("Pointer expression contains unexpected trailing characters");
        }
        return value;
    }

private:
    [[nodiscard]] core::Address parseExpression() {
        auto value = parsePrimary();
        for (;;) {
            skipSpaces();
            if (!consume('+')) {
                break;
            }
            value = addChecked(value, parsePrimary());
        }
        return value;
    }

    [[nodiscard]] core::Address parsePrimary() {
        skipSpaces();
        if (consume('[')) {
            const auto address = parseExpression();
            skipSpaces();
            if (!consume(']')) {
                throw std::invalid_argument("Pointer expression is missing a closing ']'");
            }
            return resolver_.readPointer(address);
        }

        return parseAtom();
    }

    [[nodiscard]] core::Address parseAtom() {
        skipSpaces();
        const auto start = position_;
        while (position_ < text_.size()) {
            const auto ch = text_[position_];
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '[' || ch == ']') {
                break;
            }
            ++position_;
        }

        if (start == position_) {
            throw std::invalid_argument("Pointer expression expected a symbol, module, or number");
        }

        return resolver_.resolveToken(text_.substr(start, position_ - start));
    }

    void skipSpaces() {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    std::string_view text_;
    const PointerResolver& resolver_;
    std::size_t position_ = 0;
};

PointerResolver::PointerResolver(const backend::IProcessBackend& process, const SymbolRepository& symbols)
    : process_(process),
      symbols_(symbols) {
}

core::Address PointerResolver::resolve(core::Address base, std::span<const std::ptrdiff_t> offsets) const {
    auto current = base;
    for (const auto offset : offsets) {
        current = applyOffset(readPointer(current), offset);
    }
    return current;
}

core::Address PointerResolver::resolve(std::string_view expression) const {
    return PointerExpressionParser(expression, *this).parse();
}

core::Address PointerResolver::readPointer(core::Address address) const {
    switch (process_.pointerSize()) {
    case 4:
        return static_cast<core::Address>(process_.readValue<std::uint32_t>(address));
    case 8:
        return static_cast<core::Address>(process_.readValue<std::uint64_t>(address));
    default:
        throw std::runtime_error("Unsupported target pointer size");
    }
}

core::Address PointerResolver::resolveToken(std::string_view token) const {
    if (looksLikeHexLiteral(token)) {
        return parseHexLiteral(token);
    }

    if (const auto symbol = symbols_.find(token)) {
        return symbol->address;
    }

    if (const auto module = process_.findModule(token)) {
        return module->base;
    }

    throw std::invalid_argument("Pointer expression references an unknown symbol or module: " + std::string(token));
}

}  // namespace hexengine::engine
