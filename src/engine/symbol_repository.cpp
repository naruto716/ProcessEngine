#include "hexengine/engine/symbol_repository.hpp"

#include <algorithm>
#include <mutex>

namespace hexengine::engine {

void SymbolRepository::registerSymbol(SymbolRecord symbol) {
    std::unique_lock lock(mutex_);
    symbols_.insert_or_assign(symbol.name, std::move(symbol));
}

bool SymbolRepository::unregisterSymbol(std::string_view name) {
    std::unique_lock lock(mutex_);
    return symbols_.erase(std::string(name)) > 0;
}

std::optional<SymbolRecord> SymbolRepository::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    const auto iterator = symbols_.find(std::string(name));
    if (iterator == symbols_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<SymbolRecord> SymbolRepository::list() const {
    std::shared_lock lock(mutex_);

    std::vector<SymbolRecord> records;
    records.reserve(symbols_.size());
    for (const auto& [_, record] : symbols_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const SymbolRecord& left, const SymbolRecord& right) {
        return core::foldCaseAscii(left.name) < core::foldCaseAscii(right.name);
    });

    return records;
}

void SymbolRepository::clear() {
    std::unique_lock lock(mutex_);
    symbols_.clear();
}

}  // namespace hexengine::engine
