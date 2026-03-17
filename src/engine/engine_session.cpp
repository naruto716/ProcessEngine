#include "hexengine/engine/engine_session.hpp"

#include <algorithm>
#include <stdexcept>

#include "hexengine/engine/script_context.hpp"
#include "name_validation.hpp"
#include "write_with_temporary_protection.hpp"

namespace hexengine::engine {
namespace {

backend::IProcessBackend& requireProcess(const std::unique_ptr<backend::IProcessBackend>& process) {
    if (!process) {
        throw std::invalid_argument("EngineSession requires a process backend");
    }

    return *process;
}

template <typename Container>
void appendUnique(Container& values, std::string_view value) {
    const auto folded = core::foldCaseAscii(value);
    const auto exists = std::any_of(values.begin(), values.end(), [&](const std::string& current) {
        return core::foldCaseAscii(current) == folded;
    });
    if (!exists) {
        values.emplace_back(value);
    }
}

template <typename Container>
void eraseFolded(Container& values, std::string_view value) {
    const auto folded = core::foldCaseAscii(value);
    values.erase(
        std::remove_if(values.begin(), values.end(), [&](const std::string& current) {
            return core::foldCaseAscii(current) == folded;
        }),
        values.end());
}

}  // namespace

EngineSession::EngineSession(std::unique_ptr<backend::IProcessBackend> process)
    : process_(std::move(process)),
      scanner_(requireProcess(process_)),
      addresses_(requireProcess(process_), [this](std::string_view name) {
          return tryResolveSessionName(name);
      }),
      pointers_(requireProcess(process_), addresses_),
      allocations_(requireProcess(process_), allocationRecords_),
      patches_(requireProcess(process_), patchRecords_) {
}

EngineSession::~EngineSession() = default;

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
    SymbolKind kind,
    bool persistent) {
    detail::validateUserDefinedName(name, "Registered symbol");
    if (const auto allocation = allocationRecords_.find(name); allocation && allocation->scope == AllocationScope::Global) {
        throw std::runtime_error("Registered symbol collides with an existing global allocation: " + std::string(name));
    }

    SymbolRecord symbol{
        .name = std::string(name),
        .address = address,
        .kind = kind,
        .persistent = persistent,
    };

    symbols_.registerSymbol(symbol);
    linkGlobalAllocationSymbol(address, symbol.name);
    return symbol;
}

bool EngineSession::unregisterSymbol(std::string_view name) {
    const auto removed = symbols_.unregisterSymbol(name);
    if (removed) {
        unlinkGlobalAllocationSymbol(name);
    }
    return removed;
}

std::optional<SymbolRecord> EngineSession::resolveSymbol(std::string_view name) const {
    if (const auto symbol = symbols_.find(name)) {
        return symbol;
    }

    if (const auto module = process_->findModule(name)) {
        return SymbolRecord{
            .name = module->name,
            .address = module->base,
            .kind = SymbolKind::Module,
            .persistent = true,
        };
    }

    return std::nullopt;
}

AllocationRecord EngineSession::globalAlloc(const AllocationRequest& request) {
    if (const auto existing = allocationRecords_.find(request.name); !existing && symbols_.find(request.name)) {
        throw std::runtime_error("Global allocation collides with a registered symbol: " + request.name);
    }

    const auto record = allocations_.allocate(request);
    auto stored = allocationRecords_.find(record.name).value_or(record);
    appendUnique(stored.linkedSymbols, record.name);
    allocationRecords_.upsert(stored);
    symbols_.registerSymbol(SymbolRecord{
        .name = record.name,
        .address = record.address,
        .kind = SymbolKind::Allocation,
        .persistent = true,
    });
    return record;
}

bool EngineSession::deallocate(std::string_view name) {
    const auto record = allocationRecords_.find(name);
    if (!record) {
        return false;
    }

    for (const auto& symbolName : record->linkedSymbols) {
        (void)symbols_.unregisterSymbol(symbolName);
    }

    return allocations_.deallocate(name);
}

ScriptContext& EngineSession::createScriptContext(std::string_view contextId) {
    detail::validateUserDefinedName(contextId, "Script context");

    const auto key = std::string(contextId);
    if (const auto iterator = scriptContexts_.find(key); iterator != scriptContexts_.end()) {
        return *iterator->second;
    }

    auto context = std::make_unique<ScriptContext>(*this, key);
    auto* result = context.get();
    scriptContexts_.emplace(key, std::move(context));
    return *result;
}

ScriptContext* EngineSession::findScriptContext(std::string_view contextId) noexcept {
    const auto iterator = scriptContexts_.find(std::string(contextId));
    return iterator == scriptContexts_.end() ? nullptr : iterator->second.get();
}

const ScriptContext* EngineSession::findScriptContext(std::string_view contextId) const noexcept {
    const auto iterator = scriptContexts_.find(std::string(contextId));
    return iterator == scriptContexts_.end() ? nullptr : iterator->second.get();
}

bool EngineSession::destroyScriptContext(std::string_view contextId) {
    return scriptContexts_.erase(std::string(contextId)) > 0;
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

std::optional<core::Address> EngineSession::tryResolveSessionName(std::string_view name) const {
    if (const auto symbol = symbols_.find(name)) {
        return symbol->address;
    }

    return std::nullopt;
}

void EngineSession::linkGlobalAllocationSymbol(core::Address address, std::string_view symbolName) {
    for (auto record : allocationRecords_.list()) {
        if (record.scope != AllocationScope::Global || record.address != address) {
            continue;
        }

        appendUnique(record.linkedSymbols, symbolName);
        allocationRecords_.upsert(std::move(record));
        return;
    }
}

void EngineSession::unlinkGlobalAllocationSymbol(std::string_view symbolName) {
    for (auto record : allocationRecords_.list()) {
        const auto before = record.linkedSymbols.size();
        eraseFolded(record.linkedSymbols, symbolName);
        if (record.linkedSymbols.size() == before) {
            continue;
        }

        allocationRecords_.upsert(std::move(record));
        return;
    }
}

}  // namespace hexengine::engine
