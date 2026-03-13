#include "cepipeline/memory/allocation_manager.hpp"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace cepipeline::memory {
namespace {

[[nodiscard]] std::string buildError(std::string_view message, std::string_view name) {
    std::ostringstream stream;
    stream << message << ": " << name;
    return stream.str();
}

}  // namespace

AllocationManager::AllocationManager(ProcessMemory& process, SymbolTable& symbols)
    : process_(process),
      symbols_(symbols) {
}

AllocationRecord AllocationManager::allocate(const AllocationRequest& request) {
    if (request.name.empty()) {
        throw std::invalid_argument("Allocation name must not be empty");
    }
    if (request.size == 0) {
        throw std::invalid_argument("Allocation size must be greater than zero");
    }

    std::unique_lock lock(mutex_);
    const auto existing = records_.find(request.name);
    if (existing != records_.end()) {
        if (request.scope == AllocationScope::Global) {
            if (existing->second.size < request.size) {
                throw std::runtime_error(buildError("Existing global allocation is smaller than requested", request.name));
            }
            return existing->second;
        }

        throw std::runtime_error(buildError("Allocation already exists", request.name));
    }

    const auto block = process_.allocate(request.size, request.protection, request.nearAddress);
    AllocationRecord record{
        .name = request.name,
        .address = block.address,
        .size = block.size,
        .protection = block.protection,
        .scope = request.scope,
    };

    records_.emplace(record.name, record);
    symbols_.registerSymbol(SymbolRecord{
        .name = record.name,
        .address = record.address,
        .size = record.size,
        .kind = SymbolKind::Allocation,
        .persistent = record.scope == AllocationScope::Global,
    });

    return record;
}

bool AllocationManager::deallocate(std::string_view name) {
    std::unique_lock lock(mutex_);
    const auto iterator = records_.find(std::string(name));
    if (iterator == records_.end()) {
        return false;
    }

    process_.free(iterator->second.address);
    (void)symbols_.unregisterSymbol(iterator->second.name);
    records_.erase(iterator);
    return true;
}

std::optional<AllocationRecord> AllocationManager::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    const auto iterator = records_.find(std::string(name));
    if (iterator == records_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<AllocationRecord> AllocationManager::list() const {
    std::shared_lock lock(mutex_);

    std::vector<AllocationRecord> records;
    records.reserve(records_.size());
    for (const auto& [_, record] : records_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const AllocationRecord& left, const AllocationRecord& right) {
        return foldCaseAscii(left.name) < foldCaseAscii(right.name);
    });

    return records;
}

}  // namespace cepipeline::memory
