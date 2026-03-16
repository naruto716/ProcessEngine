#include "hexengine/engine/allocation_service.hpp"

#include <sstream>
#include <stdexcept>

#include "name_validation.hpp"

namespace hexengine::engine {
namespace {

[[nodiscard]] std::string buildError(std::string_view message, std::string_view name) {
    std::ostringstream stream;
    stream << message << ": " << name;
    return stream.str();
}

}  // namespace

AllocationService::AllocationService(
    backend::IProcessBackend& process,
    AllocationRepository& records)
    : process_(process),
      records_(records) {
}

AllocationRecord AllocationService::allocate(const AllocationRequest& request) {
    detail::validateUserDefinedName(request.name, "Global allocation");
    if (request.size == 0) {
        throw std::invalid_argument("Allocation size must be greater than zero");
    }

    if (const auto existing = records_.find(request.name)) {
        if (existing->scope == AllocationScope::Global) {
            if (existing->size < request.size) {
                throw std::runtime_error(buildError("Existing global allocation is smaller than requested", request.name));
            }
            return *existing;
        }

        throw std::runtime_error(buildError("Allocation already exists", request.name));
    }

    const auto block = process_.allocate(request.size, request.protection, request.nearAddress);
    AllocationRecord record{
        .name = request.name,
        .address = block.address,
        .size = block.size,
        .protection = block.protection,
        .scope = AllocationScope::Global,
    };

    records_.upsert(record);
    return record;
}

bool AllocationService::deallocate(std::string_view name) {
    const auto record = records_.find(name);
    if (!record) {
        return false;
    }

    process_.free(record->address);
    (void)records_.erase(record->name);
    return true;
}

std::optional<AllocationRecord> AllocationService::find(std::string_view name) const {
    return records_.find(name);
}

std::vector<AllocationRecord> AllocationService::list() const {
    return records_.list();
}

}  // namespace hexengine::engine
