#pragma once

#include <cctype>
#include <functional>
#include <string>
#include <string_view>

namespace hexengine::core {

inline std::string foldCaseAscii(std::string_view value) {
    std::string folded;
    folded.reserve(value.size());
    for (const auto character : value) {
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return folded;
}

struct CaseInsensitiveStringHash {
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string>{}(foldCaseAscii(value));
    }
};

struct CaseInsensitiveStringEqual {
    [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
        return foldCaseAscii(left) == foldCaseAscii(right);
    }
};

}  // namespace hexengine::core
