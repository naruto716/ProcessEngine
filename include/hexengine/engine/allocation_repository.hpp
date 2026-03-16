#pragma once

#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/core/memory_types.hpp"

namespace hexengine::engine {

enum class AllocationScope {
    Local,
    Global,
};

struct AllocationRequest {
    std::string name;
    std::size_t size = 0;
    core::ProtectionFlags protection = core::kReadWriteExecute;
    std::optional<core::Address> nearAddress = std::nullopt;
};

struct AllocationRecord {
    std::string name;
    core::Address address = 0;
    std::size_t size = 0;
    core::ProtectionFlags protection = core::ProtectionFlags::None;
    AllocationScope scope = AllocationScope::Local;
};

class AllocationRepository {
public:
    [[nodiscard]] std::optional<AllocationRecord> find(std::string_view name) const;
    void upsert(AllocationRecord record);
    [[nodiscard]] bool erase(std::string_view name);
    [[nodiscard]] std::vector<AllocationRecord> list() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, AllocationRecord, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> records_;
};

}  // namespace hexengine::engine
