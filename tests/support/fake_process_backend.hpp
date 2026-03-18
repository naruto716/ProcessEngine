#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <functional>
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

    [[nodiscard]] bool isAllocated(core::Address address) const {
        return allocations_.find(address) != allocations_.end();
    }

    [[nodiscard]] std::size_t allocationCount() const noexcept {
        return allocations_.size();
    }

    [[nodiscard]] const std::vector<core::Address>& executedCodeAddresses() const noexcept {
        return executedCodeAddresses_;
    }

    void setExecuteCodeHook(std::function<void(core::Address)> hook) {
        executeCodeHook_ = std::move(hook);
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

    [[nodiscard]] std::optional<core::MemoryRegion> query(core::Address address) const override {
        for (const auto& [base, block] : allocations_) {
            if (address >= base && address < (base + block.size)) {
                return core::MemoryRegion{
                    .base = block.address,
                    .size = block.size,
                    .protection = block.protection,
                    .state = core::MemoryState::Committed,
                    .type = core::MemoryType::Private,
                };
            }
        }

        for (const auto& module : modules_) {
            if (address >= module.base && address < (module.base + module.size)) {
                return core::MemoryRegion{
                    .base = module.base,
                    .size = module.size,
                    .protection = core::ProtectionFlags::Read | core::ProtectionFlags::Execute,
                    .state = core::MemoryState::Committed,
                    .type = core::MemoryType::Image,
                };
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::vector<core::MemoryRegion> regions(std::optional<core::AddressRange> range) const override {
        std::vector<core::MemoryRegion> result;

        const auto overlaps = [&](core::Address base, std::size_t size) {
            if (!range.has_value()) {
                return true;
            }

            const auto end = base + size;
            return end > range->start && base < range->end;
        };

        for (const auto& module : modules_) {
            if (!overlaps(module.base, module.size)) {
                continue;
            }

            result.push_back(core::MemoryRegion{
                .base = module.base,
                .size = module.size,
                .protection = core::ProtectionFlags::Read | core::ProtectionFlags::Execute,
                .state = core::MemoryState::Committed,
                .type = core::MemoryType::Image,
            });
        }

        for (const auto& [_, block] : allocations_) {
            if (!overlaps(block.address, block.size)) {
                continue;
            }

            result.push_back(core::MemoryRegion{
                .base = block.address,
                .size = block.size,
                .protection = block.protection,
                .state = core::MemoryState::Committed,
                .type = core::MemoryType::Private,
            });
        }

        std::sort(result.begin(), result.end(), [](const core::MemoryRegion& left, const core::MemoryRegion& right) {
            return left.base < right.base;
        });

        return result;
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
                if (!isMapped(address + count)) {
                    break;
                }

                buffer[count] = std::byte{0x00};
                continue;
            }

            buffer[count] = iterator->second;
        }
        return count;
    }

private:
    [[nodiscard]] bool isMapped(core::Address address) const noexcept {
        for (const auto& [base, block] : allocations_) {
            if (address >= base && address < (base + block.size)) {
                return true;
            }
        }

        for (const auto& module : modules_) {
            if (address >= module.base && address < (module.base + module.size)) {
                return true;
            }
        }

        return false;
    }

public:
    void write(core::Address address, std::span<const std::byte> bytes) const override {
        const_cast<FakeProcessBackend*>(this)->storeBytes(address, bytes);
    }

    [[nodiscard]] std::size_t tryWrite(core::Address address, std::span<const std::byte> bytes) const noexcept override {
        const_cast<FakeProcessBackend*>(this)->storeBytes(address, bytes);
        return bytes.size();
    }

    [[nodiscard]] core::ProtectionChange protect(
        core::Address address,
        std::size_t,
        core::ProtectionFlags newProtection) override {
        for (auto& [_, block] : allocations_) {
            if (address >= block.address && address < (block.address + block.size)) {
                const auto previous = block.protection;
                block.protection = newProtection;
                return core::ProtectionChange{
                    .previous = previous,
                    .current = newProtection,
                };
            }
        }

        return core::ProtectionChange{
            .previous = newProtection,
            .current = newProtection,
        };
    }

    [[nodiscard]] core::AllocationBlock allocate(
        std::size_t size,
        core::ProtectionFlags protection,
        std::optional<core::Address>) override {
        if (size == 0) {
            throw std::invalid_argument("Allocation size must be greater than zero");
        }

        const auto address = nextAllocationAddress_;
        nextAllocationAddress_ += size + 0x100;

        auto block = core::AllocationBlock{
            .address = address,
            .size = size,
            .protection = protection,
        };
        allocations_.emplace(address, block);
        return block;
    }

    void free(core::Address address) override {
        if (allocations_.erase(address) == 0) {
            throw std::runtime_error("free called for an unknown fake allocation");
        }
    }

    void executeCode(core::Address address) override {
        executedCodeAddresses_.push_back(address);
        if (executeCodeHook_) {
            executeCodeHook_(address);
        }
    }

private:
    std::size_t pointerSize_ = 0;
    std::vector<core::ModuleInfo> modules_;
    std::unordered_map<core::Address, std::byte> memory_;
    std::unordered_map<core::Address, core::AllocationBlock> allocations_;
    core::Address nextAllocationAddress_ = 0x0100'0000;
    std::vector<core::Address> executedCodeAddresses_;
    std::function<void(core::Address)> executeCodeHook_;
};

}  // namespace hexengine::tests::support
