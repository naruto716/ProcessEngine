#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cepipeline/memory/case_insensitive.hpp"
#include "cepipeline/memory/process_memory.hpp"
#include "cepipeline/memory/symbol_table.hpp"

namespace cepipeline::memory {

enum class AllocationScope {
    Local,
    Global
};

struct AllocationRequest {
    std::string name;
    std::size_t size = 0;
    DWORD protection = PAGE_EXECUTE_READWRITE;
    AllocationScope scope = AllocationScope::Local;
    std::optional<std::uintptr_t> nearAddress = std::nullopt;
};

struct AllocationRecord {
    std::string name;
    std::uintptr_t address = 0;
    std::size_t size = 0;
    DWORD protection = 0;
    AllocationScope scope = AllocationScope::Local;
};

class AllocationManager {
public:
    AllocationManager(ProcessMemory& process, SymbolTable& symbols);

    [[nodiscard]] AllocationRecord allocate(const AllocationRequest& request);
    [[nodiscard]] bool deallocate(std::string_view name);
    [[nodiscard]] std::optional<AllocationRecord> find(std::string_view name) const;
    [[nodiscard]] std::vector<AllocationRecord> list() const;

private:
    ProcessMemory& process_;
    SymbolTable& symbols_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, AllocationRecord, CaseInsensitiveStringHash, CaseInsensitiveStringEqual> records_;
};

}  // namespace cepipeline::memory
