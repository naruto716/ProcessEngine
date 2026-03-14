#include "hexengine/engine/patch_service.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace hexengine::engine {
namespace {

[[nodiscard]] std::string buildError(std::string_view message, std::string_view name) {
    std::ostringstream stream;
    stream << message << ": " << name;
    return stream.str();
}

[[nodiscard]] std::vector<std::byte> toOwnedBytes(std::span<const std::byte> bytes) {
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

[[nodiscard]] core::ProtectionFlags makeWritableProtection(const core::MemoryRegion& region) {
    auto protection = region.protection;
    protection |= core::ProtectionFlags::Read | core::ProtectionFlags::Write;
    protection &= ~(core::ProtectionFlags::NoAccess | core::ProtectionFlags::Guard);
    return protection;
}

void writeWithTemporaryProtection(
    backend::IProcessBackend& process,
    core::Address address,
    std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return;
    }

    const auto region = process.query(address);
    if (!region.has_value()) {
        throw std::runtime_error("Unable to query patch target region");
    }

    std::optional<core::ProtectionFlags> restoreProtection;
    if (!region->isWritable()) {
        const auto change = process.protect(address, bytes.size(), makeWritableProtection(*region));
        restoreProtection = change.previous;
    }

    try {
        process.write(address, bytes);
    } catch (...) {
        if (restoreProtection.has_value()) {
            try {
                (void)process.protect(address, bytes.size(), *restoreProtection);
            } catch (...) {
            }
        }
        throw;
    }

    if (restoreProtection.has_value()) {
        (void)process.protect(address, bytes.size(), *restoreProtection);
    }
}

[[nodiscard]] std::vector<std::byte> buildNops(std::size_t size) {
    return std::vector<std::byte>(size, std::byte{0x90});
}

void validateRequest(const PatchRequest& request) {
    if (request.name.empty()) {
        throw std::invalid_argument("Patch name must not be empty");
    }
    if (request.replacement.empty()) {
        throw std::invalid_argument("Patch replacement must not be empty");
    }
    if (request.expected.has_value() && request.expected->size() != request.replacement.size()) {
        throw std::invalid_argument("Patch expected bytes must match replacement size");
    }
}

}  // namespace

PatchService::PatchService(backend::IProcessBackend& process, PatchRepository& records)
    : process_(process),
      records_(records) {
}

PatchRecord PatchService::apply(const PatchRequest& request) {
    validateRequest(request);

    if (records_.find(request.name)) {
        throw std::runtime_error(buildError("Patch already exists", request.name));
    }

    const auto originalBytes = process_.read(request.address, request.replacement.size());
    if (request.expected.has_value() && originalBytes != *request.expected) {
        throw std::runtime_error(buildError("Patch expected bytes did not match process memory", request.name));
    }

    writeWithTemporaryProtection(process_, request.address, request.replacement);

    PatchRecord record{
        .name = request.name,
        .address = request.address,
        .originalBytes = originalBytes,
        .replacementBytes = request.replacement,
        .kind = request.kind,
    };
    records_.upsert(record);
    return record;
}

PatchRecord PatchService::applyBytes(
    std::string_view name,
    core::Address address,
    std::span<const std::byte> replacement,
    std::span<const std::byte> expected) {
    return apply(PatchRequest{
        .name = std::string(name),
        .address = address,
        .replacement = toOwnedBytes(replacement),
        .expected = expected.empty() ? std::nullopt : std::optional(toOwnedBytes(expected)),
        .kind = PatchKind::Bytes,
    });
}

PatchRecord PatchService::applyNop(
    std::string_view name,
    core::Address address,
    std::size_t size,
    std::span<const std::byte> expected) {
    if (size == 0) {
        throw std::invalid_argument("NOP patch size must be greater than zero");
    }

    return apply(PatchRequest{
        .name = std::string(name),
        .address = address,
        .replacement = buildNops(size),
        .expected = expected.empty() ? std::nullopt : std::optional(toOwnedBytes(expected)),
        .kind = PatchKind::Nop,
    });
}

bool PatchService::restore(std::string_view name) {
    const auto record = records_.find(name);
    if (!record.has_value()) {
        return false;
    }

    writeWithTemporaryProtection(process_, record->address, record->originalBytes);
    (void)records_.erase(name);
    return true;
}

std::optional<PatchRecord> PatchService::find(std::string_view name) const {
    return records_.find(name);
}

std::vector<PatchRecord> PatchService::list() const {
    return records_.list();
}

}  // namespace hexengine::engine
