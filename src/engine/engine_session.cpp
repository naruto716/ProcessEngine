#include "hexengine/engine/engine_session.hpp"

#include <stdexcept>

#include "write_with_temporary_protection.hpp"

namespace hexengine::engine {
namespace {

backend::IProcessBackend& requireProcess(const std::unique_ptr<backend::IProcessBackend>& process) {
    if (!process) {
        throw std::invalid_argument("EngineSession requires a process backend");
    }

    return *process;
}

}  // namespace

EngineSession::EngineSession(std::unique_ptr<backend::IProcessBackend> process)
    : process_(std::move(process)),
      scanner_(requireProcess(process_)),
      addresses_(requireProcess(process_), symbols_),
      pointers_(requireProcess(process_), addresses_),
      allocations_(requireProcess(process_), symbols_, allocationRecords_),
      patches_(requireProcess(process_), patchRecords_) {
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

AddressResolver& EngineSession::addresses() noexcept {
    return addresses_;
}

const AddressResolver& EngineSession::addresses() const noexcept {
    return addresses_;
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

PatchService& EngineSession::patches() noexcept {
    return patches_;
}

const PatchService& EngineSession::patches() const noexcept {
    return patches_;
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

PatchRecord EngineSession::applyPatch(const PatchRequest& request) {
    return patches_.apply(request);
}

PatchRecord EngineSession::applyPatch(
    std::string_view name,
    core::Address address,
    std::span<const std::byte> replacement,
    std::span<const std::byte> expected) {
    return patches_.applyBytes(name, address, replacement, expected);
}

PatchRecord EngineSession::applyNopPatch(
    std::string_view name,
    core::Address address,
    std::size_t size,
    std::span<const std::byte> expected) {
    return patches_.applyNop(name, address, size, expected);
}

bool EngineSession::restorePatch(std::string_view name) {
    return patches_.restore(name);
}

std::vector<core::Address> EngineSession::aobScan(std::string_view pattern) const {
    return scanner_.scan(core::BytePattern::parse(pattern));
}

std::vector<core::Address> EngineSession::aobScanModule(std::string_view moduleName, std::string_view pattern) const {
    return scanner_.scanModule(moduleName, core::BytePattern::parse(pattern));
}

std::vector<core::Address> EngineSession::aobScanRegion(
    core::Address startAddress,
    core::Address stopAddress,
    std::string_view pattern) const {
    if (stopAddress <= startAddress) {
        throw std::invalid_argument("AOB scan region stop address must be greater than the start address");
    }

    return scanner_.scan(
        core::BytePattern::parse(pattern),
        core::AddressRange{
            .start = startAddress,
            .end = stopAddress,
        });
}

bool EngineSession::assertBytes(core::Address address, std::string_view pattern) const {
    const auto parsed = core::BytePattern::parse(pattern);
    const auto bytes = process_->read(address, parsed.size());
    return parsed.matches(bytes);
}

void EngineSession::readMem(core::Address sourceAddress, core::Address destinationAddress, std::size_t size) {
    if (size == 0) {
        throw std::invalid_argument("readMem size must be greater than zero");
    }

    const auto bytes = process_->read(sourceAddress, size);
    detail::writeWithTemporaryProtection(*process_, destinationAddress, bytes);
}

void EngineSession::executeCode(core::Address entryAddress) {
    process_->executeCode(entryAddress);
}

core::ProtectionChange EngineSession::fullAccess(core::Address address, std::size_t size) {
    return process_->protect(address, size, core::kReadWriteExecute);
}

core::Address EngineSession::resolveAddress(std::string_view expression) const {
    return addresses_.resolve(expression);
}

core::Address EngineSession::resolvePointer(core::Address base, std::span<const std::ptrdiff_t> offsets) const {
    return pointers_.resolve(base, offsets);
}

}  // namespace hexengine::engine
