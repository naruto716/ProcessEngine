#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/engine/address_resolver.hpp"
#include "hexengine/engine/allocation_service.hpp"
#include "hexengine/engine/patch_service.hpp"
#include "hexengine/engine/pointer_resolver.hpp"
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
    [[nodiscard]] AddressResolver& addresses() noexcept;
    [[nodiscard]] const AddressResolver& addresses() const noexcept;
    [[nodiscard]] PointerResolver& pointers() noexcept;
    [[nodiscard]] const PointerResolver& pointers() const noexcept;
    [[nodiscard]] AllocationService& allocations() noexcept;
    [[nodiscard]] const AllocationService& allocations() const noexcept;
    [[nodiscard]] PatchService& patches() noexcept;
    [[nodiscard]] const PatchService& patches() const noexcept;

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
    [[nodiscard]] PatchRecord applyPatch(const PatchRequest& request);
    [[nodiscard]] PatchRecord applyPatch(
        std::string_view name,
        core::Address address,
        std::span<const std::byte> replacement,
        std::span<const std::byte> expected = {});
    [[nodiscard]] PatchRecord applyNopPatch(
        std::string_view name,
        core::Address address,
        std::size_t size,
        std::span<const std::byte> expected = {});
    [[nodiscard]] bool restorePatch(std::string_view name);

    [[nodiscard]] std::vector<core::Address> aobScan(std::string_view pattern) const;
    [[nodiscard]] std::vector<core::Address> aobScanModule(std::string_view moduleName, std::string_view pattern) const;
    [[nodiscard]] bool assertBytes(core::Address address, std::string_view pattern) const;
    [[nodiscard]] core::ProtectionChange fullAccess(core::Address address, std::size_t size);
    [[nodiscard]] core::Address resolveAddress(std::string_view expression) const;
    [[nodiscard]] core::Address resolvePointer(core::Address base, std::span<const std::ptrdiff_t> offsets) const;

    template <typename... Offsets>
    [[nodiscard]] core::Address resolvePointer(core::Address base, Offsets... offsets) const {
        return pointers_.resolve(base, offsets...);
    }

    template <typename T, typename... Offsets>
    [[nodiscard]] T readPointerValue(core::Address base, Offsets... offsets) const {
        return pointers_.read<T>(base, offsets...);
    }

    template <typename T>
    [[nodiscard]] T readPointerValue(std::string_view expression) const {
        return process_->readValue<T>(resolveAddress(expression));
    }

private:
    std::unique_ptr<backend::IProcessBackend> process_;
    ProcessScanner scanner_;
    SymbolRepository symbols_;
    AddressResolver addresses_;
    PointerResolver pointers_;
    AllocationRepository allocationRecords_;
    AllocationService allocations_;
    PatchRepository patchRecords_;
    PatchService patches_;
};

}  // namespace hexengine::engine
