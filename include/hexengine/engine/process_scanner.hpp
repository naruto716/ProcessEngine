#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/core/pattern.hpp"

namespace hexengine::engine {

class ProcessScanner {
public:
    explicit ProcessScanner(const backend::IProcessBackend& process);

    [[nodiscard]] std::vector<core::Address> scan(
        const core::BytePattern& pattern,
        std::optional<core::AddressRange> range = std::nullopt) const;
    [[nodiscard]] std::vector<core::Address> scanModule(
        std::string_view moduleName,
        const core::BytePattern& pattern) const;

private:
    const backend::IProcessBackend& process_;
};

}  // namespace hexengine::engine
