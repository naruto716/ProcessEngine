#include "hexengine/engine/patch_repository.hpp"

#include <algorithm>
#include <mutex>

namespace hexengine::engine {

std::optional<PatchRecord> PatchRepository::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    const auto iterator = records_.find(std::string(name));
    if (iterator == records_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

void PatchRepository::upsert(PatchRecord record) {
    std::unique_lock lock(mutex_);
    records_.insert_or_assign(record.name, std::move(record));
}

bool PatchRepository::erase(std::string_view name) {
    std::unique_lock lock(mutex_);
    return records_.erase(std::string(name)) > 0;
}

std::vector<PatchRecord> PatchRepository::list() const {
    std::shared_lock lock(mutex_);

    std::vector<PatchRecord> records;
    records.reserve(records_.size());
    for (const auto& [_, record] : records_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const PatchRecord& left, const PatchRecord& right) {
        return core::foldCaseAscii(left.name) < core::foldCaseAscii(right.name);
    });

    return records;
}

void PatchRepository::clear() {
    std::unique_lock lock(mutex_);
    records_.clear();
}

}  // namespace hexengine::engine
