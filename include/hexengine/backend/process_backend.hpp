#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "hexengine/core/memory_types.hpp"

namespace hexengine::backend {

class IProcessBackend {
public:
    virtual ~IProcessBackend() = default;

    [[nodiscard]] virtual core::ProcessId pid() const noexcept = 0;
    [[nodiscard]] virtual std::size_t pointerSize() const noexcept = 0;

    [[nodiscard]] virtual std::vector<core::ModuleInfo> modules() const = 0;
    [[nodiscard]] virtual std::optional<core::ModuleInfo> findModule(std::string_view name) const = 0;
    [[nodiscard]] virtual core::ModuleInfo mainModule() const = 0;

    [[nodiscard]] virtual std::optional<core::MemoryRegion> query(core::Address address) const = 0;
    [[nodiscard]] virtual std::vector<core::MemoryRegion> regions(
        std::optional<core::AddressRange> range = std::nullopt) const = 0;

    [[nodiscard]] virtual std::vector<std::byte> read(core::Address address, std::size_t size) const = 0;
    [[nodiscard]] virtual std::size_t tryRead(core::Address address, std::span<std::byte> buffer) const noexcept = 0;

    template <typename T>
    [[nodiscard]] T readValue(core::Address address) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        T value{};
        auto buffer = std::as_writable_bytes(std::span<T>(&value, 1));
        const auto bytesRead = tryRead(address, buffer);
        if (bytesRead != sizeof(T)) {
            throw std::runtime_error("Failed to read the requested value from process memory");
        }
        return value;
    }

    virtual void write(core::Address address, std::span<const std::byte> bytes) const = 0;
    [[nodiscard]] virtual std::size_t tryWrite(
        core::Address address,
        std::span<const std::byte> bytes) const noexcept = 0;

    template <typename T>
    void writeValue(core::Address address, const T& value) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        write(address, std::as_bytes(std::span<const T>(&value, 1)));
    }

    [[nodiscard]] virtual core::ProtectionChange protect(
        core::Address address,
        std::size_t size,
        core::ProtectionFlags newProtection) = 0;
    [[nodiscard]] virtual core::AllocationBlock allocate(
        std::size_t size,
        core::ProtectionFlags protection = core::kReadWriteExecute,
        std::optional<core::Address> nearAddress = std::nullopt) = 0;
    virtual void free(core::Address address) = 0;
    virtual void executeCode(core::Address entryAddress) = 0;
};

}  // namespace hexengine::backend
