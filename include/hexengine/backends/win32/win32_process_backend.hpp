#pragma once

#include <Windows.h>

#include <memory>
#include <optional>

#include "hexengine/backend/process_backend.hpp"

namespace hexengine::backends::win32 {

class Win32ProcessBackend final : public backend::IProcessBackend {
public:
    using AccessMask = DWORD;

    [[nodiscard]] static std::unique_ptr<Win32ProcessBackend> open(
        core::ProcessId pid,
        AccessMask access = defaultAccess());
    [[nodiscard]] static AccessMask defaultAccess() noexcept;

    Win32ProcessBackend(Win32ProcessBackend&& other) noexcept;
    Win32ProcessBackend& operator=(Win32ProcessBackend&& other) noexcept;

    Win32ProcessBackend(const Win32ProcessBackend&) = delete;
    Win32ProcessBackend& operator=(const Win32ProcessBackend&) = delete;

    ~Win32ProcessBackend() override;

    [[nodiscard]] core::ProcessId pid() const noexcept override;
    [[nodiscard]] std::size_t pointerSize() const noexcept override;

    [[nodiscard]] std::vector<core::ModuleInfo> modules() const override;
    [[nodiscard]] std::optional<core::ModuleInfo> findModule(std::string_view name) const override;
    [[nodiscard]] core::ModuleInfo mainModule() const override;

    [[nodiscard]] std::optional<core::MemoryRegion> query(core::Address address) const override;
    [[nodiscard]] std::vector<core::MemoryRegion> regions(
        std::optional<core::AddressRange> range = std::nullopt) const override;

    [[nodiscard]] std::vector<std::byte> read(core::Address address, std::size_t size) const override;
    [[nodiscard]] std::size_t tryRead(core::Address address, std::span<std::byte> buffer) const noexcept override;

    void write(core::Address address, std::span<const std::byte> bytes) const override;
    [[nodiscard]] std::size_t tryWrite(core::Address address, std::span<const std::byte> bytes) const noexcept override;

    [[nodiscard]] core::ProtectionChange protect(
        core::Address address,
        std::size_t size,
        core::ProtectionFlags newProtection) override;
    [[nodiscard]] core::AllocationBlock allocate(
        std::size_t size,
        core::ProtectionFlags protection = core::kReadWriteExecute,
        std::optional<core::Address> nearAddress = std::nullopt) override;
    void free(core::Address address) override;

    [[nodiscard]] HANDLE nativeHandle() const noexcept;

private:
    Win32ProcessBackend(HANDLE handle, core::ProcessId pid) noexcept;

    [[nodiscard]] core::AllocationBlock allocateNear(
        core::Address nearAddress,
        std::size_t size,
        core::ProtectionFlags protection) const;

    HANDLE handle_ = nullptr;
    core::ProcessId pid_ = 0;
    std::size_t pointerSize_ = sizeof(void*);
};

}  // namespace hexengine::backends::win32
