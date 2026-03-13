#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cepipeline/memory/case_insensitive.hpp"

namespace cepipeline::memory {

enum class SymbolKind {
    UserDefined,
    Allocation,
    Module
};

struct SymbolRecord {
    std::string name;
    std::uintptr_t address = 0;
    std::size_t size = 0;
    SymbolKind kind = SymbolKind::UserDefined;
    bool persistent = true;
};

class SymbolTable {
public:
    void registerSymbol(SymbolRecord symbol);
    [[nodiscard]] bool unregisterSymbol(std::string_view name);
    [[nodiscard]] std::optional<SymbolRecord> find(std::string_view name) const;
    [[nodiscard]] std::vector<SymbolRecord> list() const;
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SymbolRecord, CaseInsensitiveStringHash, CaseInsensitiveStringEqual> symbols_;
};

}  // namespace cepipeline::memory
