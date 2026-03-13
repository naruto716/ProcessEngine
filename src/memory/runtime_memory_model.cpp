#include "cepipeline/memory/runtime_memory_model.hpp"

#include <stdexcept>

namespace cepipeline::memory {

RuntimeMemoryModel RuntimeMemoryModel::open(std::uint32_t pid) {
    return RuntimeMemoryModel(ProcessMemory::open(pid));
}

RuntimeMemoryModel RuntimeMemoryModel::attachCurrent() {
    return RuntimeMemoryModel(ProcessMemory::attachCurrent());
}

RuntimeMemoryModel::RuntimeMemoryModel(ProcessMemory process)
    : process_(std::move(process)),
      allocations_(process_, symbols_) {
}

ProcessMemory& RuntimeMemoryModel::process() noexcept {
    return process_;
}

const ProcessMemory& RuntimeMemoryModel::process() const noexcept {
    return process_;
}

SymbolTable& RuntimeMemoryModel::symbols() noexcept {
    return symbols_;
}

const SymbolTable& RuntimeMemoryModel::symbols() const noexcept {
    return symbols_;
}

AllocationManager& RuntimeMemoryModel::allocations() noexcept {
    return allocations_;
}

const AllocationManager& RuntimeMemoryModel::allocations() const noexcept {
    return allocations_;
}

SymbolRecord RuntimeMemoryModel::registerSymbol(
    std::string_view name,
    std::uintptr_t address,
    std::size_t size,
    SymbolKind kind,
    bool persistent) {
    if (name.empty()) {
        throw std::invalid_argument("Symbol name must not be empty");
    }

    SymbolRecord symbol{
        .name = std::string(name),
        .address = address,
        .size = size,
        .kind = kind,
        .persistent = persistent,
    };

    symbols_.registerSymbol(symbol);
    return symbol;
}

bool RuntimeMemoryModel::unregisterSymbol(std::string_view name) {
    return symbols_.unregisterSymbol(name);
}

std::optional<SymbolRecord> RuntimeMemoryModel::resolveSymbol(std::string_view name) const {
    if (const auto symbol = symbols_.find(name)) {
        return symbol;
    }

    if (const auto module = process_.findModule(name)) {
        return SymbolRecord{
            .name = module->name,
            .address = module->base,
            .size = module->size,
            .kind = SymbolKind::Module,
            .persistent = true,
        };
    }

    return std::nullopt;
}

AllocationRecord RuntimeMemoryModel::allocate(const AllocationRequest& request) {
    return allocations_.allocate(request);
}

bool RuntimeMemoryModel::deallocate(std::string_view name) {
    return allocations_.deallocate(name);
}

std::vector<std::uintptr_t> RuntimeMemoryModel::aobScan(std::string_view pattern) const {
    return process_.scan(BytePattern::parse(pattern));
}

std::vector<std::uintptr_t> RuntimeMemoryModel::aobScanModule(std::string_view moduleName, std::string_view pattern) const {
    return process_.scanModule(moduleName, BytePattern::parse(pattern));
}

bool RuntimeMemoryModel::assertBytes(std::uintptr_t address, std::string_view pattern) const {
    const auto parsed = BytePattern::parse(pattern);
    const auto bytes = process_.read(address, parsed.size());
    return parsed.matches(bytes);
}

ProtectionChange RuntimeMemoryModel::fullAccess(std::uintptr_t address, std::size_t size) const {
    return process_.protect(address, size, PAGE_EXECUTE_READWRITE);
}

}  // namespace cepipeline::memory
