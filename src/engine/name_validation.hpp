#pragma once

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hexengine::engine::detail {

[[nodiscard]] inline bool isHexDigit(char ch) noexcept {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] inline bool looksLikeHexLiteralName(std::string_view token) noexcept {
    if (token.empty()) {
        return false;
    }

    if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        return std::all_of(token.begin() + 2, token.end(), isHexDigit);
    }

    return std::all_of(token.begin(), token.end(), isHexDigit);
}

inline void validateUserDefinedName(std::string_view name, std::string_view what) {
    if (name.empty()) {
        throw std::invalid_argument(std::string(what) + " name must not be empty");
    }

    if (looksLikeHexLiteralName(name)) {
        throw std::invalid_argument(std::string(what) + " name must not be a pure hex literal");
    }

    for (const auto ch : name) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '[' || ch == ']') {
            throw std::invalid_argument(std::string(what) + " name contains characters that are not valid in address expressions");
        }
    }
}

}  // namespace hexengine::engine::detail
