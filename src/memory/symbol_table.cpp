#include "cepipeline/memory/symbol_table.hpp"

#include <algorithm>
#include <mutex>

namespace cepipeline::memory {

void SymbolTable::registerSymbol(SymbolRecord symbol) {
    std::unique_lock lock(mutex_);
    symbols_.insert_or_assign(symbol.name, std::move(symbol));
}

bool SymbolTable::unregisterSymbol(std::string_view name) {
    std::unique_lock lock(mutex_);
    return symbols_.erase(std::string(name)) > 0;
}

std::optional<SymbolRecord> SymbolTable::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    const auto iterator = symbols_.find(std::string(name));
    if (iterator == symbols_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<SymbolRecord> SymbolTable::list() const {
    std::shared_lock lock(mutex_);

    std::vector<SymbolRecord> records;
    records.reserve(symbols_.size());
    for (const auto& [_, record] : symbols_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const SymbolRecord& left, const SymbolRecord& right) {
        return foldCaseAscii(left.name) < foldCaseAscii(right.name);
    });

    return records;
}

void SymbolTable::clear() {
    std::unique_lock lock(mutex_);
    symbols_.clear();
}

}  // namespace cepipeline::memory
