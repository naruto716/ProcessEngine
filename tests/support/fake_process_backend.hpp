#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hexengine/backend/process_backend.hpp"

namespace hexengine::tests::support {

class FakeProcessBackend final : public backend::IProcessBackend {
public:
    explicit FakeProcessBackend(std::size_t pointerSize)
        : pointerSize_(pointerSize) {
    }

    void addModule(std::string name, core::Address base, std::size_t size = 0x100000) {
        modules_.push_back(core::ModuleInfo{
            .name = std::move(name),
            .path = {},
            .base = base,
            .size = size,
        });
    }

    template <typename T>
    void storeValue(core::Address address, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        const auto bytes = std::as_bytes(std::span<const T>(&value, 1));
        storeBytes(address, bytes);
    }

    void storeBytes(core::Address address, std::span<const std::byte> bytes) {
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            memory_[address + index] = bytes[index];
        }
    }

    [[nodiscard]] core::ProcessId pid() const noexcept override {
        return 1;
    }

    [[nodiscard]] std::size_t pointerSize() const noexcept override {
        return pointerSize_;
    }

    [[nodiscard]] std::vector<core::ModuleInfo> modules() const override {
        return modules_;
    }

    [[nodiscard]] std::optional<core::ModuleInfo> findModule(std::string_view name) const override {
        for (const auto& module : modules_) {
            if (module.name == name) {
                return module;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] core::ModuleInfo mainModule() const override {
        if (modules_.empty()) {
            throw std::runtime_error("No modules registered");
        }
        return modules_.front();
    }

    [[nodiscard]] std::optional<core::MemoryRegion> query(core::Address) const override {
        return std::nullopt;
    }

    [[nodiscard]] std::vector<core::MemoryRegion> regions(std::optional<core::AddressRange>) const override {
        return {};
    }

    [[nodiscard]] std::vector<std::byte> read(core::Address address, std::size_t size) const override {
        std::vector<std::byte> bytes(size);
        const auto bytesRead = tryRead(address, bytes);
        if (bytesRead != size) {
            throw std::runtime_error("Read failed");
        }
        return bytes;
    }

    [[nodiscard]] std::size_t tryRead(core::Address address, std::span<std::byte> buffer) const noexcept override {
        std::size_t count = 0;
        for (; count < buffer.size(); ++count) {
            const auto iterator = memory_.find(address + count);
            if (iterator == memory_.end()) {
                break;
            }
            buffer[count] = iterator->second;
        }
        return count;
    }

    void write(core::Address address, std::span<const std::byte> bytes) const override {
        const_cast<FakeProcessBackend*>(this)->storeBytes(address, bytes);
    }

    [[nodiscard]] std::size_t tryWrite(core::Address address, std::span<const std::byte> bytes) const noexcept override {
        const_cast<FakeProcessBackend*>(this)->storeBytes(address, bytes);
        return bytes.size();
    }

    [[nodiscard]] core::ProtectionChange protect(
        core::Address,
        std::size_t,
        core::ProtectionFlags newProtection) override {
        return core::ProtectionChange{
            .previous = newProtection,
            .current = newProtection,
        };
    }

    [[nodiscard]] core::AllocationBlock allocate(
        std::size_t,
        core::ProtectionFlags,
        std::optional<core::Address>) override {
        throw std::runtime_error("allocate not implemented in FakeProcessBackend");
    }

    void free(core::Address) override {
        throw std::runtime_error("free not implemented in FakeProcessBackend");
    }

private:
    std::size_t pointerSize_ = 0;
    std::vector<core::ModuleInfo> modules_;
    std::unordered_map<core::Address, std::byte> memory_;
};

}  // namespace hexengine::tests::support
