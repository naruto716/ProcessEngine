#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/core/memory_types.hpp"
#include "hexengine/engine/address_resolver.hpp"
#include "hexengine/engine/allocation_repository.hpp"
#include "hexengine/engine/symbol_repository.hpp"

namespace hexengine::engine {

class EngineSession;

struct LabelRecord {
    std::string name;
    std::optional<core::Address> address;
};

class ScriptContext {
public:
    ScriptContext(EngineSession& session, std::string contextId);
    ~ScriptContext();

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] EngineSession& session() noexcept;
    [[nodiscard]] const EngineSession& session() const noexcept;

    [[nodiscard]] AllocationRecord alloc(const AllocationRequest& request);
    [[nodiscard]] AllocationRecord globalAlloc(const AllocationRequest& request);
    [[nodiscard]] bool dealloc(std::string_view name);
    [[nodiscard]] std::optional<AllocationRecord> findLocalAllocation(std::string_view name) const;
    [[nodiscard]] std::vector<AllocationRecord> listLocalAllocations() const;

    void declareLabel(std::string_view name);
    void bindLabel(std::string_view name, core::Address address);
    [[nodiscard]] bool hasLabel(std::string_view name) const;
    [[nodiscard]] std::optional<core::Address> findLabel(std::string_view name) const;
    [[nodiscard]] std::vector<LabelRecord> listLabels() const;

    [[nodiscard]] SymbolRecord registerSymbol(std::string_view name);
    [[nodiscard]] SymbolRecord registerSymbol(
        std::string_view alias,
        core::Address address,
        SymbolKind kind = SymbolKind::UserDefined,
        bool persistent = true);
    [[nodiscard]] bool unregisterSymbol(std::string_view name);

    [[nodiscard]] core::Address resolveAddress(std::string_view expression) const;

private:
    [[nodiscard]] std::optional<core::Address> tryResolveLocalName(std::string_view name) const;
    [[nodiscard]] std::optional<core::Address> tryResolveName(std::string_view name) const;
    void linkLocalAllocationSymbol(core::Address address, std::string_view symbolName);
    void unlinkLocalAllocationSymbol(std::string_view symbolName);

    EngineSession& session_;
    std::string contextId_;
    AllocationRepository localAllocations_;
    std::unordered_map<std::string, LabelRecord, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> labels_;
    AddressResolver addresses_;
};

}  // namespace hexengine::engine
