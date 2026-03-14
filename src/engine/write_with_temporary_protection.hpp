#pragma once

#include <optional>
#include <span>
#include <stdexcept>

#include "hexengine/backend/process_backend.hpp"

namespace hexengine::engine::detail {

[[nodiscard]] inline core::ProtectionFlags makeWritableProtection(const core::MemoryRegion& region) {
    auto protection = region.protection;
    protection |= core::ProtectionFlags::Read | core::ProtectionFlags::Write;
    protection &= ~(core::ProtectionFlags::NoAccess | core::ProtectionFlags::Guard);
    return protection;
}

inline void writeWithTemporaryProtection(
    backend::IProcessBackend& process,
    core::Address address,
    std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return;
    }

    const auto region = process.query(address);
    if (!region.has_value()) {
        throw std::runtime_error("Unable to query target region");
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

}  // namespace hexengine::engine::detail
