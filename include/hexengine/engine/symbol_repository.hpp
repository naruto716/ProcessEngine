#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/core/memory_types.hpp"

namespace hexengine::engine {

enum class SymbolKind {
    UserDefined,
    Label,
    Allocation,
    Module,
};

struct SymbolRecord {
    std::string name;
    core::Address address = 0;
    SymbolKind kind = SymbolKind::UserDefined;
    bool persistent = true;
};

class SymbolRepository {
public:
    void registerSymbol(SymbolRecord symbol);
    [[nodiscard]] bool unregisterSymbol(std::string_view name);
    [[nodiscard]] std::optional<SymbolRecord> find(std::string_view name) const;
    [[nodiscard]] std::vector<SymbolRecord> list() const;
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SymbolRecord, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> symbols_;
};

}  // namespace hexengine::engine
