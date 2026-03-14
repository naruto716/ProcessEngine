#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/patch_repository.hpp"

namespace hexengine::engine {

class PatchService {
public:
    PatchService(backend::IProcessBackend& process, PatchRepository& records);

    [[nodiscard]] PatchRecord apply(const PatchRequest& request);
    [[nodiscard]] PatchRecord applyBytes(
        std::string_view name,
        core::Address address,
        std::span<const std::byte> replacement,
        std::span<const std::byte> expected = {});
    [[nodiscard]] PatchRecord applyNop(
        std::string_view name,
        core::Address address,
        std::size_t size,
        std::span<const std::byte> expected = {});
    [[nodiscard]] bool restore(std::string_view name);
    [[nodiscard]] std::optional<PatchRecord> find(std::string_view name) const;
    [[nodiscard]] std::vector<PatchRecord> list() const;

private:
    backend::IProcessBackend& process_;
    PatchRepository& records_;
};

}  // namespace hexengine::engine
