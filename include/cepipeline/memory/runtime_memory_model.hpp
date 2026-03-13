#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cepipeline/memory/allocation_manager.hpp"
#include "cepipeline/memory/process_memory.hpp"
#include "cepipeline/memory/symbol_table.hpp"

namespace cepipeline::memory {

class RuntimeMemoryModel {
public:
    [[nodiscard]] static RuntimeMemoryModel open(std::uint32_t pid);
    [[nodiscard]] static RuntimeMemoryModel attachCurrent();

    explicit RuntimeMemoryModel(ProcessMemory process);

    [[nodiscard]] ProcessMemory& process() noexcept;
    [[nodiscard]] const ProcessMemory& process() const noexcept;
    [[nodiscard]] SymbolTable& symbols() noexcept;
    [[nodiscard]] const SymbolTable& symbols() const noexcept;
    [[nodiscard]] AllocationManager& allocations() noexcept;
    [[nodiscard]] const AllocationManager& allocations() const noexcept;

    [[nodiscard]] SymbolRecord registerSymbol(
        std::string_view name,
        std::uintptr_t address,
        std::size_t size = 0,
        SymbolKind kind = SymbolKind::UserDefined,
        bool persistent = true);
    [[nodiscard]] bool unregisterSymbol(std::string_view name);
    [[nodiscard]] std::optional<SymbolRecord> resolveSymbol(std::string_view name) const;

    [[nodiscard]] AllocationRecord allocate(const AllocationRequest& request);
    [[nodiscard]] bool deallocate(std::string_view name);

    [[nodiscard]] std::vector<std::uintptr_t> aobScan(std::string_view pattern) const;
    [[nodiscard]] std::vector<std::uintptr_t> aobScanModule(std::string_view moduleName, std::string_view pattern) const;
    [[nodiscard]] bool assertBytes(std::uintptr_t address, std::string_view pattern) const;
    [[nodiscard]] ProtectionChange fullAccess(std::uintptr_t address, std::size_t size) const;

private:
    ProcessMemory process_;
    SymbolTable symbols_;
    AllocationManager allocations_;
};

}  // namespace cepipeline::memory
