#include "hexengine/engine/script_context.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "hexengine/engine/engine_session.hpp"
#include "name_validation.hpp"

namespace hexengine::engine {
namespace {

[[nodiscard]] std::string buildError(std::string_view message, std::string_view name) {
    std::ostringstream stream;
    stream << message << ": " << name;
    return stream.str();
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

ScriptContext::ScriptContext(EngineSession& session, std::string contextId)
    : session_(session),
      contextId_(std::move(contextId)),
      addresses_(session.process(), [this](std::string_view name) {
          return tryResolveName(name);
      }) {
}

ScriptContext::~ScriptContext() {
    const auto locals = localAllocations_.list();
    if (locals.empty()) {
        return;
    }

    std::clog << "hexengine: ScriptContext '" << contextId_
              << "' destroyed with " << locals.size()
              << " local allocation(s) still alive:";
    for (const auto& local : locals) {
        std::clog << ' ' << local.name;
    }
    std::clog << '\n';
}

std::string_view ScriptContext::id() const noexcept {
    return contextId_;
}

EngineSession& ScriptContext::session() noexcept {
    return session_;
}

const EngineSession& ScriptContext::session() const noexcept {
    return session_;
}

AllocationRecord ScriptContext::alloc(const AllocationRequest& request) {
    detail::validateUserDefinedName(request.name, "Local allocation");
    if (request.size == 0) {
        throw std::invalid_argument("Local allocation size must be greater than zero");
    }

    if (localAllocations_.find(request.name)) {
        throw std::runtime_error(buildError("Local allocation already exists", request.name));
    }
    if (labels_.find(std::string(request.name)) != labels_.end()) {
        throw std::runtime_error(buildError("Local allocation collides with an existing label", request.name));
    }

    const auto block = session_.process().allocate(request.size, request.protection, request.nearAddress);
    AllocationRecord record{
        .name = request.name,
        .address = block.address,
        .size = block.size,
        .protection = block.protection,
        .scope = AllocationScope::Local,
        .linkedLabels = {request.name},
    };

    localAllocations_.upsert(record);
    labels_.emplace(record.name, LabelRecord{
        .name = record.name,
        .address = record.address,
    });
    return record;
}

AllocationRecord ScriptContext::globalAlloc(const AllocationRequest& request) {
    return session_.globalAlloc(request);
}

bool ScriptContext::dealloc(std::string_view name) {
    if (const auto local = localAllocations_.find(name)) {
        for (const auto& labelName : local->linkedLabels) {
            (void)labels_.erase(labelName);
        }
        for (const auto& symbolName : local->linkedSymbols) {
            (void)session_.unregisterSymbol(symbolName);
        }

        session_.process().free(local->address);
        (void)localAllocations_.erase(name);
        return true;
    }

    return session_.deallocate(name);
}

std::optional<AllocationRecord> ScriptContext::findLocalAllocation(std::string_view name) const {
    return localAllocations_.find(name);
}

std::vector<AllocationRecord> ScriptContext::listLocalAllocations() const {
    return localAllocations_.list();
}

void ScriptContext::declareLabel(std::string_view name) {
    detail::validateUserDefinedName(name, "Label");
    if (labels_.find(std::string(name)) != labels_.end()) {
        throw std::runtime_error("Label already exists: " + std::string(name));
    }
    if (localAllocations_.find(name)) {
        throw std::runtime_error("Label already exists: " + std::string(name));
    }

    const auto key = std::string(name);
    labels_.emplace(key, LabelRecord{
        .name = key,
        .address = std::nullopt,
    });
}

void ScriptContext::bindLabel(std::string_view name, core::Address address) {
    detail::validateUserDefinedName(name, "Label");

    const auto key = std::string(name);
    if (const auto iterator = labels_.find(key); iterator != labels_.end()) {
        if (iterator->second.address.has_value()) {
            throw std::runtime_error("Label is already bound: " + key);
        }

        iterator->second.address = address;
        return;
    }

    labels_.emplace(key, LabelRecord{
        .name = key,
        .address = address,
    });
}

bool ScriptContext::hasLabel(std::string_view name) const {
    return labels_.find(std::string(name)) != labels_.end();
}

std::optional<core::Address> ScriptContext::findLabel(std::string_view name) const {
    const auto iterator = labels_.find(std::string(name));
    if (iterator == labels_.end()) {
        return std::nullopt;
    }

    return iterator->second.address;
}

std::vector<LabelRecord> ScriptContext::listLabels() const {
    std::vector<LabelRecord> labels;
    labels.reserve(labels_.size());
    for (const auto& [_, label] : labels_) {
        labels.push_back(label);
    }

    std::sort(labels.begin(), labels.end(), [](const LabelRecord& left, const LabelRecord& right) {
        return core::foldCaseAscii(left.name) < core::foldCaseAscii(right.name);
    });

    return labels;
}

SymbolRecord ScriptContext::registerSymbol(std::string_view name) {
    if (const auto localAllocation = localAllocations_.find(name)) {
        const auto symbol = session_.registerSymbol(
            name,
            localAllocation->address,
            SymbolKind::Allocation,
            true);
        linkLocalAllocationSymbol(localAllocation->address, symbol.name);
        return symbol;
    }

    if (const auto labelAddress = findLabel(name); labelAddress.has_value()) {
        return session_.registerSymbol(
            name,
            *labelAddress,
            SymbolKind::Label,
            true);
    }

    if (const auto explicitSymbol = session_.symbols().find(name)) {
        return *explicitSymbol;
    }

    return session_.registerSymbol(name, resolveAddress(name));
}

SymbolRecord ScriptContext::registerSymbol(
    std::string_view alias,
    core::Address address,
    SymbolKind kind,
    bool persistent) {
    const auto symbol = session_.registerSymbol(alias, address, kind, persistent);
    linkLocalAllocationSymbol(address, symbol.name);
    return symbol;
}

bool ScriptContext::unregisterSymbol(std::string_view name) {
    const auto removed = session_.unregisterSymbol(name);
    if (removed) {
        unlinkLocalAllocationSymbol(name);
    }
    return removed;
}

core::Address ScriptContext::resolveAddress(std::string_view expression) const {
    return addresses_.resolve(expression);
}

std::optional<core::Address> ScriptContext::tryResolveLocalName(std::string_view name) const {
    if (const auto iterator = labels_.find(std::string(name)); iterator != labels_.end()) {
        if (!iterator->second.address.has_value()) {
            throw std::invalid_argument("Label is not bound: " + std::string(name));
        }
        return iterator->second.address;
    }

    return std::nullopt;
}

std::optional<core::Address> ScriptContext::tryResolveName(std::string_view name) const {
    if (const auto local = tryResolveLocalName(name)) {
        return *local;
    }

    return session_.tryResolveSessionName(name);
}

void ScriptContext::linkLocalAllocationSymbol(core::Address address, std::string_view symbolName) {
    for (auto record : localAllocations_.list()) {
        if (record.address != address) {
            continue;
        }

        appendUnique(record.linkedSymbols, symbolName);
        localAllocations_.upsert(std::move(record));
        return;
    }
}

void ScriptContext::unlinkLocalAllocationSymbol(std::string_view symbolName) {
    for (auto record : localAllocations_.list()) {
        const auto before = record.linkedSymbols.size();
        eraseFolded(record.linkedSymbols, symbolName);
        if (record.linkedSymbols.size() == before) {
            continue;
        }

        localAllocations_.upsert(std::move(record));
        return;
    }
}

}  // namespace hexengine::engine
