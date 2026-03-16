#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/allocation_repository.hpp"

namespace hexengine::engine {

class AllocationService {
public:
    AllocationService(backend::IProcessBackend& process, AllocationRepository& records);

    [[nodiscard]] AllocationRecord allocate(const AllocationRequest& request);
    [[nodiscard]] bool deallocate(std::string_view name);
    [[nodiscard]] std::optional<AllocationRecord> find(std::string_view name) const;
    [[nodiscard]] std::vector<AllocationRecord> list() const;

private:
    backend::IProcessBackend& process_;
    AllocationRepository& records_;
};

}  // namespace hexengine::engine
