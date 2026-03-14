#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/core/memory_types.hpp"

namespace hexengine::engine {

enum class PatchKind : std::uint8_t {
    Bytes,
    Nop,
};

struct PatchRequest {
    std::string name;
    core::Address address = 0;
    std::vector<std::byte> replacement;
    std::optional<std::vector<std::byte>> expected = std::nullopt;
    PatchKind kind = PatchKind::Bytes;
};

struct PatchRecord {
    std::string name;
    core::Address address = 0;
    std::vector<std::byte> originalBytes;
    std::vector<std::byte> replacementBytes;
    PatchKind kind = PatchKind::Bytes;
};

class PatchRepository {
public:
    [[nodiscard]] std::optional<PatchRecord> find(std::string_view name) const;
    void upsert(PatchRecord record);
    [[nodiscard]] bool erase(std::string_view name);
    [[nodiscard]] std::vector<PatchRecord> list() const;
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, PatchRecord, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> records_;
};

}  // namespace hexengine::engine
