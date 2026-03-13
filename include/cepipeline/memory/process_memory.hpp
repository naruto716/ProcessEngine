#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cepipeline/memory/pattern.hpp"

namespace cepipeline::memory {

struct AddressRange {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
};

struct ModuleInfo {
    std::string name;
    std::string path;
    std::uintptr_t base = 0;
    std::size_t size = 0;
};

struct MemoryRegion {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    DWORD protection = 0;
    DWORD state = 0;
    DWORD type = 0;

    [[nodiscard]] bool isCommitted() const noexcept;
    [[nodiscard]] bool isReadable() const noexcept;
    [[nodiscard]] bool isWritable() const noexcept;
    [[nodiscard]] bool isExecutable() const noexcept;
    [[nodiscard]] bool isScanCandidate() const noexcept;
};

struct ProtectionChange {
    DWORD previous = 0;
    DWORD current = 0;
};

struct AllocationBlock {
    std::uintptr_t address = 0;
    std::size_t size = 0;
    DWORD protection = 0;
};

class ProcessMemory {
public:
    [[nodiscard]] static ProcessMemory open(std::uint32_t pid, DWORD access = defaultAccess());
    [[nodiscard]] static ProcessMemory attachCurrent(DWORD access = defaultAccess());
    [[nodiscard]] static DWORD defaultAccess() noexcept;

    ProcessMemory(ProcessMemory&& other) noexcept;
    ProcessMemory& operator=(ProcessMemory&& other) noexcept;

    ProcessMemory(const ProcessMemory&) = delete;
    ProcessMemory& operator=(const ProcessMemory&) = delete;

    ~ProcessMemory();

    [[nodiscard]] std::uint32_t pid() const noexcept;
    [[nodiscard]] HANDLE nativeHandle() const noexcept;

    [[nodiscard]] std::vector<ModuleInfo> modules() const;
    [[nodiscard]] std::optional<ModuleInfo> findModule(std::string_view name) const;
    [[nodiscard]] ModuleInfo mainModule() const;

    [[nodiscard]] std::optional<MemoryRegion> query(std::uintptr_t address) const;
    [[nodiscard]] std::vector<MemoryRegion> regions(std::optional<AddressRange> range = std::nullopt) const;

    [[nodiscard]] std::vector<std::byte> read(std::uintptr_t address, std::size_t size) const;
    [[nodiscard]] std::size_t tryRead(std::uintptr_t address, std::span<std::byte> buffer) const noexcept;

    template <typename T>
    [[nodiscard]] T readValue(std::uintptr_t address) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        T value{};
        auto buffer = std::as_writable_bytes(std::span<T>(&value, 1));
        const auto bytesRead = tryRead(address, buffer);
        if (bytesRead != sizeof(T)) {
            throw std::runtime_error("Failed to read the requested value from process memory");
        }
        return value;
    }

    void write(std::uintptr_t address, std::span<const std::byte> bytes) const;
    [[nodiscard]] std::size_t tryWrite(std::uintptr_t address, std::span<const std::byte> bytes) const noexcept;

    template <typename T>
    void writeValue(std::uintptr_t address, const T& value) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        write(address, std::as_bytes(std::span<const T>(&value, 1)));
    }

    [[nodiscard]] ProtectionChange protect(std::uintptr_t address, std::size_t size, DWORD newProtection) const;
    [[nodiscard]] AllocationBlock allocate(
        std::size_t size,
        DWORD protection = PAGE_EXECUTE_READWRITE,
        std::optional<std::uintptr_t> nearAddress = std::nullopt) const;
    void free(std::uintptr_t address) const;

    [[nodiscard]] std::vector<std::uintptr_t> scan(
        const BytePattern& pattern,
        std::optional<AddressRange> range = std::nullopt) const;
    [[nodiscard]] std::vector<std::uintptr_t> scanModule(std::string_view moduleName, const BytePattern& pattern) const;

private:
    ProcessMemory(HANDLE handle, std::uint32_t pid) noexcept;

    [[nodiscard]] AllocationBlock allocateNear(std::uintptr_t nearAddress, std::size_t size, DWORD protection) const;

    HANDLE handle_ = nullptr;
    std::uint32_t pid_ = 0;
};

}  // namespace cepipeline::memory
