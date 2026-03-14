#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/allocation_service.hpp"
#include "hexengine/engine/process_scanner.hpp"
#include "hexengine/engine/symbol_repository.hpp"

namespace hexengine::engine {

class EngineSession {
public:
    explicit EngineSession(std::unique_ptr<backend::IProcessBackend> process);

    [[nodiscard]] backend::IProcessBackend& process() noexcept;
    [[nodiscard]] const backend::IProcessBackend& process() const noexcept;
    [[nodiscard]] ProcessScanner& scanner() noexcept;
    [[nodiscard]] const ProcessScanner& scanner() const noexcept;
    [[nodiscard]] SymbolRepository& symbols() noexcept;
    [[nodiscard]] const SymbolRepository& symbols() const noexcept;
    [[nodiscard]] AllocationService& allocations() noexcept;
    [[nodiscard]] const AllocationService& allocations() const noexcept;

    [[nodiscard]] SymbolRecord registerSymbol(
        std::string_view name,
        core::Address address,
        std::size_t size = 0,
        SymbolKind kind = SymbolKind::UserDefined,
        bool persistent = true);
    [[nodiscard]] bool unregisterSymbol(std::string_view name);
    [[nodiscard]] std::optional<SymbolRecord> resolveSymbol(std::string_view name) const;

    [[nodiscard]] AllocationRecord allocate(const AllocationRequest& request);
    [[nodiscard]] bool deallocate(std::string_view name);

    [[nodiscard]] std::vector<core::Address> aobScan(std::string_view pattern) const;
    [[nodiscard]] std::vector<core::Address> aobScanModule(std::string_view moduleName, std::string_view pattern) const;
    [[nodiscard]] bool assertBytes(core::Address address, std::string_view pattern) const;
    [[nodiscard]] core::ProtectionChange fullAccess(core::Address address, std::size_t size);

private:
    std::unique_ptr<backend::IProcessBackend> process_;
    ProcessScanner scanner_;
    SymbolRepository symbols_;
    AllocationRepository allocationRecords_;
    AllocationService allocations_;
};

}  // namespace hexengine::engine
