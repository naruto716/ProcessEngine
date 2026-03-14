#include "hexengine/backends/win32/win32_process_backend.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "hexengine/core/case_insensitive.hpp"

namespace hexengine::backends::win32 {
namespace {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept
        : handle_(handle) {
    }

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    ~UniqueHandle() {
        reset();
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

[[noreturn]] void throwLastError(const char* context) {
    const auto error = static_cast<int>(::GetLastError());
    throw std::system_error(error, std::system_category(), context);
}

[[nodiscard]] std::string wideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    const auto required = ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] core::ProtectionFlags toProtectionFlags(DWORD protection) noexcept {
    core::ProtectionFlags flags = core::ProtectionFlags::None;

    if ((protection & PAGE_GUARD) != 0) {
        flags |= core::ProtectionFlags::Guard;
    }
    if ((protection & PAGE_NOACCESS) != 0) {
        flags |= core::ProtectionFlags::NoAccess;
    }

    const auto baseProtection = protection & 0xFF;
    switch (baseProtection) {
    case PAGE_READONLY:
        flags |= core::ProtectionFlags::Read;
        break;
    case PAGE_READWRITE:
        flags |= core::ProtectionFlags::Read | core::ProtectionFlags::Write;
        break;
    case PAGE_WRITECOPY:
        flags |= core::ProtectionFlags::Read | core::ProtectionFlags::Write | core::ProtectionFlags::CopyOnWrite;
        break;
    case PAGE_EXECUTE:
        flags |= core::ProtectionFlags::Execute;
        break;
    case PAGE_EXECUTE_READ:
        flags |= core::ProtectionFlags::Read | core::ProtectionFlags::Execute;
        break;
    case PAGE_EXECUTE_READWRITE:
        flags |= core::ProtectionFlags::Read | core::ProtectionFlags::Write | core::ProtectionFlags::Execute;
        break;
    case PAGE_EXECUTE_WRITECOPY:
        flags |= core::ProtectionFlags::Read | core::ProtectionFlags::Write |
            core::ProtectionFlags::Execute | core::ProtectionFlags::CopyOnWrite;
        break;
    default:
        break;
    }

    return flags;
}

[[nodiscard]] DWORD toWin32Protection(core::ProtectionFlags protection) noexcept {
    const auto read = core::hasFlag(protection, core::ProtectionFlags::Read);
    const auto write = core::hasFlag(protection, core::ProtectionFlags::Write);
    const auto execute = core::hasFlag(protection, core::ProtectionFlags::Execute);
    const auto guard = core::hasFlag(protection, core::ProtectionFlags::Guard);
    const auto noAccess = core::hasFlag(protection, core::ProtectionFlags::NoAccess);
    const auto copyOnWrite = core::hasFlag(protection, core::ProtectionFlags::CopyOnWrite);

    DWORD baseProtection = PAGE_NOACCESS;
    if (noAccess) {
        baseProtection = PAGE_NOACCESS;
    } else if (execute && write) {
        baseProtection = copyOnWrite ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READWRITE;
    } else if (execute && read) {
        baseProtection = PAGE_EXECUTE_READ;
    } else if (execute) {
        baseProtection = PAGE_EXECUTE;
    } else if (write) {
        baseProtection = copyOnWrite ? PAGE_WRITECOPY : PAGE_READWRITE;
    } else if (read) {
        baseProtection = PAGE_READONLY;
    }

    if (guard) {
        baseProtection |= PAGE_GUARD;
    }

    return baseProtection;
}

[[nodiscard]] core::MemoryState toMemoryState(DWORD state) noexcept {
    switch (state) {
    case MEM_FREE:
        return core::MemoryState::Free;
    case MEM_RESERVE:
        return core::MemoryState::Reserved;
    case MEM_COMMIT:
        return core::MemoryState::Committed;
    default:
        return core::MemoryState::Unknown;
    }
}

[[nodiscard]] core::MemoryType toMemoryType(DWORD type) noexcept {
    switch (type) {
    case MEM_PRIVATE:
        return core::MemoryType::Private;
    case MEM_MAPPED:
        return core::MemoryType::Mapped;
    case MEM_IMAGE:
        return core::MemoryType::Image;
    default:
        return core::MemoryType::Unknown;
    }
}

[[nodiscard]] core::Address alignDown(core::Address value, core::Address alignment) noexcept {
    return value - (value % alignment);
}

[[nodiscard]] core::Address alignUp(core::Address value, core::Address alignment) noexcept {
    if (value % alignment == 0) {
        return value;
    }
    return value + (alignment - (value % alignment));
}

[[nodiscard]] core::Address addSaturated(core::Address value, core::Address delta) noexcept {
    if (delta > std::numeric_limits<core::Address>::max() - value) {
        return std::numeric_limits<core::Address>::max();
    }
    return value + delta;
}

[[nodiscard]] core::Address subtractSaturated(core::Address value, core::Address delta) noexcept {
    if (value < delta) {
        return 0;
    }
    return value - delta;
}

[[nodiscard]] std::string buildReadWriteError(const char* verb, std::size_t size, core::Address address) {
    std::ostringstream stream;
    stream << verb << " failed for " << size << " bytes at 0x" << std::hex << address;
    return stream.str();
}

[[nodiscard]] std::optional<core::Address> nearestAllocBaseInFreeRegion(
    const MEMORY_BASIC_INFORMATION& information,
    core::Address nearAddress,
    std::size_t size,
    core::Address granularity) noexcept {
    if (information.State != MEM_FREE) {
        return std::nullopt;
    }

    const auto regionBase = reinterpret_cast<core::Address>(information.BaseAddress);
    const auto regionEnd = addSaturated(regionBase, information.RegionSize);
    if (regionEnd <= regionBase || size > regionEnd - regionBase) {
        return std::nullopt;
    }

    const auto firstCandidate = alignUp(regionBase, granularity);
    if (firstCandidate < regionBase || firstCandidate > regionEnd || size > regionEnd - firstCandidate) {
        return std::nullopt;
    }

    const auto lastByteStart = regionEnd - size;
    const auto lastCandidate = alignDown(lastByteStart, granularity);
    if (lastCandidate < firstCandidate) {
        return std::nullopt;
    }

    const auto clampedTarget = std::clamp(nearAddress, firstCandidate, lastCandidate);
    const auto lowerCandidate = alignDown(clampedTarget, granularity);
    const auto upperCandidate = alignUp(clampedTarget, granularity);

    std::optional<core::Address> bestCandidate;
    auto bestDistance = std::numeric_limits<core::Address>::max();

    const auto consider = [&](core::Address candidate) {
        if (candidate < firstCandidate || candidate > lastCandidate) {
            return;
        }

        const auto distance = candidate > nearAddress ? candidate - nearAddress : nearAddress - candidate;
        if (!bestCandidate || distance < bestDistance) {
            bestCandidate = candidate;
            bestDistance = distance;
        }
    };

    consider(lowerCandidate);
    consider(upperCandidate);
    return bestCandidate;
}

[[nodiscard]] core::Address distanceToRegion(const MEMORY_BASIC_INFORMATION& information, core::Address nearAddress) noexcept {
    const auto regionBase = reinterpret_cast<core::Address>(information.BaseAddress);
    const auto regionEnd = addSaturated(regionBase, information.RegionSize);

    if (nearAddress < regionBase) {
        return regionBase - nearAddress;
    }
    if (nearAddress >= regionEnd) {
        return nearAddress - regionEnd;
    }
    return 0;
}

[[nodiscard]] std::optional<core::AllocationBlock> tryAllocateInRegion(
    HANDLE processHandle,
    const MEMORY_BASIC_INFORMATION& information,
    core::Address nearAddress,
    std::size_t size,
    core::ProtectionFlags protection,
    core::Address granularity) {
    const auto candidate = nearestAllocBaseInFreeRegion(information, nearAddress, size, granularity);
    if (!candidate) {
        return std::nullopt;
    }

    auto* address = ::VirtualAllocEx(
        processHandle,
        reinterpret_cast<LPVOID>(*candidate),
        size,
        MEM_COMMIT | MEM_RESERVE,
        toWin32Protection(protection));
    if (address == nullptr) {
        return std::nullopt;
    }

    return core::AllocationBlock{
        .address = reinterpret_cast<core::Address>(address),
        .size = size,
        .protection = protection,
    };
}

}  // namespace

std::unique_ptr<Win32ProcessBackend> Win32ProcessBackend::open(core::ProcessId pid, AccessMask access) {
    UniqueHandle handle(::OpenProcess(access, FALSE, pid));
    if (handle.get() == nullptr) {
        throwLastError("OpenProcess failed");
    }

    return std::unique_ptr<Win32ProcessBackend>(new Win32ProcessBackend(handle.release(), pid));
}

std::unique_ptr<Win32ProcessBackend> Win32ProcessBackend::attachCurrent(AccessMask access) {
    return open(::GetCurrentProcessId(), access);
}

Win32ProcessBackend::AccessMask Win32ProcessBackend::defaultAccess() noexcept {
    return PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ |
        PROCESS_VM_WRITE |
        PROCESS_VM_OPERATION |
        PROCESS_CREATE_THREAD;
}

Win32ProcessBackend::Win32ProcessBackend(HANDLE handle, core::ProcessId pid) noexcept
    : handle_(handle),
      pid_(pid) {
}

Win32ProcessBackend::Win32ProcessBackend(Win32ProcessBackend&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      pid_(std::exchange(other.pid_, 0)) {
}

Win32ProcessBackend& Win32ProcessBackend::operator=(Win32ProcessBackend&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
        pid_ = std::exchange(other.pid_, 0);
    }

    return *this;
}

Win32ProcessBackend::~Win32ProcessBackend() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

core::ProcessId Win32ProcessBackend::pid() const noexcept {
    return pid_;
}

std::vector<core::ModuleInfo> Win32ProcessBackend::modules() const {
    UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        throwLastError("CreateToolhelp32Snapshot failed");
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!::Module32FirstW(snapshot.get(), &entry)) {
        throwLastError("Module32FirstW failed");
    }

    std::vector<core::ModuleInfo> results;
    do {
        results.push_back(core::ModuleInfo{
            .name = wideToUtf8(entry.szModule),
            .path = wideToUtf8(entry.szExePath),
            .base = reinterpret_cast<core::Address>(entry.modBaseAddr),
            .size = static_cast<std::size_t>(entry.modBaseSize),
        });
    } while (::Module32NextW(snapshot.get(), &entry));

    return results;
}

std::optional<core::ModuleInfo> Win32ProcessBackend::findModule(std::string_view name) const {
    const auto target = core::foldCaseAscii(name);
    for (const auto& module : modules()) {
        if (core::foldCaseAscii(module.name) == target) {
            return module;
        }
    }

    return std::nullopt;
}

core::ModuleInfo Win32ProcessBackend::mainModule() const {
    const auto processModules = modules();
    if (processModules.empty()) {
        throw std::runtime_error("Process has no visible modules");
    }

    return processModules.front();
}

std::optional<core::MemoryRegion> Win32ProcessBackend::query(core::Address address) const {
    MEMORY_BASIC_INFORMATION information{};
    const auto queried = ::VirtualQueryEx(
        handle_,
        reinterpret_cast<LPCVOID>(address),
        &information,
        sizeof(information));
    if (queried == 0) {
        return std::nullopt;
    }

    return core::MemoryRegion{
        .base = reinterpret_cast<core::Address>(information.BaseAddress),
        .size = information.RegionSize,
        .protection = toProtectionFlags(information.Protect),
        .state = toMemoryState(information.State),
        .type = toMemoryType(information.Type),
    };
}

std::vector<core::MemoryRegion> Win32ProcessBackend::regions(std::optional<core::AddressRange> range) const {
    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);

    core::Address start = reinterpret_cast<core::Address>(systemInfo.lpMinimumApplicationAddress);
    core::Address end = reinterpret_cast<core::Address>(systemInfo.lpMaximumApplicationAddress);
    if (range) {
        start = std::max(start, range->start);
        end = std::min(end, range->end);
    }

    std::vector<core::MemoryRegion> results;
    auto cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        const auto queried = ::VirtualQueryEx(
            handle_,
            reinterpret_cast<LPCVOID>(cursor),
            &information,
            sizeof(information));
        if (queried == 0) {
            break;
        }

        const auto base = reinterpret_cast<core::Address>(information.BaseAddress);
        const auto size = information.RegionSize;
        if (base >= end) {
            break;
        }

        results.push_back(core::MemoryRegion{
            .base = base,
            .size = size,
            .protection = toProtectionFlags(information.Protect),
            .state = toMemoryState(information.State),
            .type = toMemoryType(information.Type),
        });

        cursor = addSaturated(base, size);
        if (cursor == 0) {
            break;
        }
    }

    return results;
}

std::vector<std::byte> Win32ProcessBackend::read(core::Address address, std::size_t size) const {
    std::vector<std::byte> buffer(size);
    if (size == 0) {
        return buffer;
    }

    const auto bytesRead = tryRead(address, buffer);
    if (bytesRead != size) {
        throw std::runtime_error(buildReadWriteError("ReadProcessMemory", size, address));
    }

    return buffer;
}

std::size_t Win32ProcessBackend::tryRead(core::Address address, std::span<std::byte> buffer) const noexcept {
    SIZE_T bytesRead = 0;
    if (::ReadProcessMemory(
            handle_,
            reinterpret_cast<LPCVOID>(address),
            buffer.data(),
            buffer.size(),
            &bytesRead) == FALSE) {
        return static_cast<std::size_t>(bytesRead);
    }

    return static_cast<std::size_t>(bytesRead);
}

void Win32ProcessBackend::write(core::Address address, std::span<const std::byte> bytes) const {
    if (bytes.empty()) {
        return;
    }

    const auto bytesWritten = tryWrite(address, bytes);
    if (bytesWritten != bytes.size()) {
        throw std::runtime_error(buildReadWriteError("WriteProcessMemory", bytes.size(), address));
    }
}

std::size_t Win32ProcessBackend::tryWrite(core::Address address, std::span<const std::byte> bytes) const noexcept {
    SIZE_T bytesWritten = 0;
    if (::WriteProcessMemory(
            handle_,
            reinterpret_cast<LPVOID>(address),
            bytes.data(),
            bytes.size(),
            &bytesWritten) == FALSE) {
        return static_cast<std::size_t>(bytesWritten);
    }

    return static_cast<std::size_t>(bytesWritten);
}

core::ProtectionChange Win32ProcessBackend::protect(
    core::Address address,
    std::size_t size,
    core::ProtectionFlags newProtection) {
    DWORD previousProtection = 0;
    if (::VirtualProtectEx(
            handle_,
            reinterpret_cast<LPVOID>(address),
            size,
            toWin32Protection(newProtection),
            &previousProtection) == FALSE) {
        throwLastError("VirtualProtectEx failed");
    }

    return core::ProtectionChange{
        .previous = toProtectionFlags(previousProtection),
        .current = newProtection,
    };
}

core::AllocationBlock Win32ProcessBackend::allocate(
    std::size_t size,
    core::ProtectionFlags protection,
    std::optional<core::Address> nearAddress) {
    if (size == 0) {
        throw std::invalid_argument("Allocation size must be greater than zero");
    }

    if (nearAddress.has_value()) {
        return allocateNear(*nearAddress, size, protection);
    }

    auto* address = ::VirtualAllocEx(
        handle_,
        nullptr,
        size,
        MEM_COMMIT | MEM_RESERVE,
        toWin32Protection(protection));
    if (address == nullptr) {
        throwLastError("VirtualAllocEx failed");
    }

    return core::AllocationBlock{
        .address = reinterpret_cast<core::Address>(address),
        .size = size,
        .protection = protection,
    };
}

core::AllocationBlock Win32ProcessBackend::allocateNear(
    core::Address nearAddress,
    std::size_t size,
    core::ProtectionFlags protection) const {
    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);

    const auto granularity = static_cast<core::Address>(systemInfo.dwAllocationGranularity);
    const auto maxDistance = static_cast<core::Address>(0x8000'0000ull);
    const auto minimum = reinterpret_cast<core::Address>(systemInfo.lpMinimumApplicationAddress);
    const auto maximum = reinterpret_cast<core::Address>(systemInfo.lpMaximumApplicationAddress);

    const auto searchStart = std::max(
        minimum,
        alignDown(subtractSaturated(nearAddress, maxDistance), granularity));
    const auto searchEnd = std::min(maximum, alignUp(addSaturated(nearAddress, maxDistance), granularity));

    if (searchEnd <= searchStart) {
        throw std::runtime_error("Unable to build a valid near-allocation search window");
    }

    std::optional<core::Address> forwardCursor = std::clamp(nearAddress, searchStart, searchEnd - 1);
    std::optional<core::Address> backwardCursor;
    if (nearAddress > searchStart) {
        backwardCursor = nearAddress - 1;
    }

    while (forwardCursor || backwardCursor) {
        std::optional<MEMORY_BASIC_INFORMATION> forwardInfo;
        std::optional<MEMORY_BASIC_INFORMATION> backwardInfo;

        if (forwardCursor && *forwardCursor < searchEnd) {
            MEMORY_BASIC_INFORMATION information{};
            if (::VirtualQueryEx(
                    handle_,
                    reinterpret_cast<LPCVOID>(*forwardCursor),
                    &information,
                    sizeof(information)) != 0) {
                forwardInfo = information;
            } else {
                forwardCursor.reset();
            }
        } else {
            forwardCursor.reset();
        }

        if (backwardCursor && *backwardCursor >= searchStart) {
            MEMORY_BASIC_INFORMATION information{};
            if (::VirtualQueryEx(
                    handle_,
                    reinterpret_cast<LPCVOID>(*backwardCursor),
                    &information,
                    sizeof(information)) != 0) {
                backwardInfo = information;
            } else {
                backwardCursor.reset();
            }
        } else {
            backwardCursor.reset();
        }

        if (forwardInfo && backwardInfo &&
            forwardInfo->BaseAddress == backwardInfo->BaseAddress) {
            backwardInfo.reset();
        }

        const auto tryForwardFirst = [&]() -> bool {
            if (!forwardInfo) {
                return false;
            }
            if (!backwardInfo) {
                return true;
            }
            return distanceToRegion(*forwardInfo, nearAddress) <= distanceToRegion(*backwardInfo, nearAddress);
        };

        const auto tryOne = [&](const MEMORY_BASIC_INFORMATION& information) -> std::optional<core::AllocationBlock> {
            return tryAllocateInRegion(handle_, information, nearAddress, size, protection, granularity);
        };

        if (tryForwardFirst()) {
            if (forwardInfo) {
                if (const auto block = tryOne(*forwardInfo)) {
                    return *block;
                }
            }
            if (backwardInfo) {
                if (const auto block = tryOne(*backwardInfo)) {
                    return *block;
                }
            }
        } else {
            if (backwardInfo) {
                if (const auto block = tryOne(*backwardInfo)) {
                    return *block;
                }
            }
            if (forwardInfo) {
                if (const auto block = tryOne(*forwardInfo)) {
                    return *block;
                }
            }
        }

        if (forwardInfo) {
            const auto next = addSaturated(
                reinterpret_cast<core::Address>(forwardInfo->BaseAddress),
                forwardInfo->RegionSize);
            if (next == 0 || next >= searchEnd) {
                forwardCursor.reset();
            } else {
                forwardCursor = next;
            }
        } else {
            forwardCursor.reset();
        }

        if (backwardInfo) {
            const auto base = reinterpret_cast<core::Address>(backwardInfo->BaseAddress);
            if (base <= searchStart) {
                backwardCursor.reset();
            } else {
                backwardCursor = base - 1;
            }
        } else {
            backwardCursor.reset();
        }
    }

    throw std::runtime_error("Unable to reserve memory near target address");
}

void Win32ProcessBackend::free(core::Address address) {
    if (::VirtualFreeEx(handle_, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE) == FALSE) {
        throwLastError("VirtualFreeEx failed");
    }
}

HANDLE Win32ProcessBackend::nativeHandle() const noexcept {
    return handle_;
}

}  // namespace hexengine::backends::win32
