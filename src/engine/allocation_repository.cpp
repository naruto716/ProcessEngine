#include "hexengine/engine/allocation_repository.hpp"

#include <algorithm>

namespace hexengine::engine {

std::optional<AllocationRecord> AllocationRepository::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    const auto iterator = records_.find(std::string(name));
    if (iterator == records_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

void AllocationRepository::upsert(AllocationRecord record) {
    std::unique_lock lock(mutex_);
    records_.insert_or_assign(record.name, std::move(record));
}

bool AllocationRepository::erase(std::string_view name) {
    std::unique_lock lock(mutex_);
    return records_.erase(std::string(name)) > 0;
}

std::vector<AllocationRecord> AllocationRepository::list() const {
    std::shared_lock lock(mutex_);

    std::vector<AllocationRecord> records;
    records.reserve(records_.size());
    for (const auto& [_, record] : records_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const AllocationRecord& left, const AllocationRecord& right) {
        return core::foldCaseAscii(left.name) < core::foldCaseAscii(right.name);
    });

    return records;
}

}  // namespace hexengine::engine
