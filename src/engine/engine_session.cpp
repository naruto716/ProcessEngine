#include "hexengine/engine/engine_session.hpp"

#include <stdexcept>

namespace hexengine::engine {

EngineSession::EngineSession(std::unique_ptr<backend::IProcessBackend> process)
    : process_(std::move(process)),
      scanner_(*process_),
      pointers_(*process_, symbols_),
      allocations_(*process_, symbols_, allocationRecords_) {
    if (!process_) {
        throw std::invalid_argument("EngineSession requires a process backend");
    }
}

backend::IProcessBackend& EngineSession::process() noexcept {
    return *process_;
}

const backend::IProcessBackend& EngineSession::process() const noexcept {
    return *process_;
}

ProcessScanner& EngineSession::scanner() noexcept {
    return scanner_;
}

const ProcessScanner& EngineSession::scanner() const noexcept {
    return scanner_;
}

SymbolRepository& EngineSession::symbols() noexcept {
    return symbols_;
}

const SymbolRepository& EngineSession::symbols() const noexcept {
    return symbols_;
}

PointerResolver& EngineSession::pointers() noexcept {
    return pointers_;
}

const PointerResolver& EngineSession::pointers() const noexcept {
    return pointers_;
}

AllocationService& EngineSession::allocations() noexcept {
    return allocations_;
}

const AllocationService& EngineSession::allocations() const noexcept {
    return allocations_;
}

SymbolRecord EngineSession::registerSymbol(
    std::string_view name,
    core::Address address,
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

bool EngineSession::unregisterSymbol(std::string_view name) {
    return symbols_.unregisterSymbol(name);
}

std::optional<SymbolRecord> EngineSession::resolveSymbol(std::string_view name) const {
    if (const auto symbol = symbols_.find(name)) {
        return symbol;
    }

    if (const auto module = process_->findModule(name)) {
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

AllocationRecord EngineSession::allocate(const AllocationRequest& request) {
    return allocations_.allocate(request);
}

bool EngineSession::deallocate(std::string_view name) {
    return allocations_.deallocate(name);
}

std::vector<core::Address> EngineSession::aobScan(std::string_view pattern) const {
    return scanner_.scan(core::BytePattern::parse(pattern));
}

std::vector<core::Address> EngineSession::aobScanModule(std::string_view moduleName, std::string_view pattern) const {
    return scanner_.scanModule(moduleName, core::BytePattern::parse(pattern));
}

bool EngineSession::assertBytes(core::Address address, std::string_view pattern) const {
    const auto parsed = core::BytePattern::parse(pattern);
    const auto bytes = process_->read(address, parsed.size());
    return parsed.matches(bytes);
}

core::ProtectionChange EngineSession::fullAccess(core::Address address, std::size_t size) {
    return process_->protect(address, size, core::kReadWriteExecute);
}

core::Address EngineSession::resolvePointer(core::Address base, std::span<const std::ptrdiff_t> offsets) const {
    return pointers_.resolve(base, offsets);
}

core::Address EngineSession::resolvePointer(std::string_view expression) const {
    return pointers_.resolve(expression);
}

}  // namespace hexengine::engine
