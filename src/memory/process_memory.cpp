#include "cepipeline/memory/process_memory.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace cepipeline::memory {
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

[[nodiscard]] std::string foldCase(std::string_view value) {
    std::string folded;
    folded.reserve(value.size());
    for (const auto character : value) {
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return folded;
}

[[nodiscard]] bool isReadableProtection(DWORD protection) noexcept {
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
        return false;
    }

    const auto baseProtection = protection & 0xFF;
    switch (baseProtection) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isWritableProtection(DWORD protection) noexcept {
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
        return false;
    }

    const auto baseProtection = protection & 0xFF;
    switch (baseProtection) {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isExecutableProtection(DWORD protection) noexcept {
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
        return false;
    }

    const auto baseProtection = protection & 0xFF;
    switch (baseProtection) {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::uintptr_t alignDown(std::uintptr_t value, std::uintptr_t alignment) noexcept {
    return value - (value % alignment);
}

[[nodiscard]] std::uintptr_t alignUp(std::uintptr_t value, std::uintptr_t alignment) noexcept {
    if (value % alignment == 0) {
        return value;
    }
    return value + (alignment - (value % alignment));
}

[[nodiscard]] std::uintptr_t addSaturated(std::uintptr_t value, std::uintptr_t delta) noexcept {
    if (delta > std::numeric_limits<std::uintptr_t>::max() - value) {
        return std::numeric_limits<std::uintptr_t>::max();
    }
    return value + delta;
}

[[nodiscard]] std::uintptr_t subtractSaturated(std::uintptr_t value, std::uintptr_t delta) noexcept {
    if (value < delta) {
        return 0;
    }
    return value - delta;
}

[[nodiscard]] std::string buildReadWriteError(const char* verb, std::size_t size, std::uintptr_t address) {
    std::ostringstream stream;
    stream << verb << " failed for " << size << " bytes at 0x" << std::hex << address;
    return stream.str();
}

[[nodiscard]] std::optional<std::uintptr_t> nearestAllocBaseInFreeRegion(
    const MEMORY_BASIC_INFORMATION& information,
    std::uintptr_t nearAddress,
    std::size_t size,
    std::uintptr_t granularity) noexcept {
    if (information.State != MEM_FREE) {
        return std::nullopt;
    }

    const auto regionBase = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
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

    std::optional<std::uintptr_t> bestCandidate;
    auto bestDistance = std::numeric_limits<std::uintptr_t>::max();

    const auto consider = [&](std::uintptr_t candidate) {
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

[[nodiscard]] std::uintptr_t distanceToRegion(const MEMORY_BASIC_INFORMATION& information, std::uintptr_t nearAddress) noexcept {
    const auto regionBase = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    const auto regionEnd = addSaturated(regionBase, information.RegionSize);

    if (nearAddress < regionBase) {
        return regionBase - nearAddress;
    }
    if (nearAddress >= regionEnd) {
        return nearAddress - regionEnd;
    }
    return 0;
}

[[nodiscard]] std::optional<AllocationBlock> tryAllocateInRegion(
    HANDLE processHandle,
    const MEMORY_BASIC_INFORMATION& information,
    std::uintptr_t nearAddress,
    std::size_t size,
    DWORD protection,
    std::uintptr_t granularity) {
    const auto candidate = nearestAllocBaseInFreeRegion(information, nearAddress, size, granularity);
    if (!candidate) {
        return std::nullopt;
    }

    auto* address = ::VirtualAllocEx(
        processHandle,
        reinterpret_cast<LPVOID>(*candidate),
        size,
        MEM_COMMIT | MEM_RESERVE,
        protection);
    if (address == nullptr) {
        return std::nullopt;
    }

    return AllocationBlock{
        .address = reinterpret_cast<std::uintptr_t>(address),
        .size = size,
        .protection = protection,
    };
}

}  // namespace

bool MemoryRegion::isCommitted() const noexcept {
    return state == MEM_COMMIT;
}

bool MemoryRegion::isReadable() const noexcept {
    return isReadableProtection(protection);
}

bool MemoryRegion::isWritable() const noexcept {
    return isWritableProtection(protection);
}

bool MemoryRegion::isExecutable() const noexcept {
    return isExecutableProtection(protection);
}

bool MemoryRegion::isScanCandidate() const noexcept {
    return isCommitted() && isReadable();
}

ProcessMemory ProcessMemory::open(std::uint32_t pid, DWORD access) {
    UniqueHandle handle(::OpenProcess(access, FALSE, pid));
    if (handle.get() == nullptr) {
        throwLastError("OpenProcess failed");
    }

    return ProcessMemory(handle.release(), pid);
}

ProcessMemory ProcessMemory::attachCurrent(DWORD access) {
    return open(::GetCurrentProcessId(), access);
}

DWORD ProcessMemory::defaultAccess() noexcept {
    return PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ |
        PROCESS_VM_WRITE |
        PROCESS_VM_OPERATION |
        PROCESS_CREATE_THREAD;
}

ProcessMemory::ProcessMemory(HANDLE handle, std::uint32_t pid) noexcept
    : handle_(handle),
      pid_(pid) {
}

ProcessMemory::ProcessMemory(ProcessMemory&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      pid_(std::exchange(other.pid_, 0)) {
}

ProcessMemory& ProcessMemory::operator=(ProcessMemory&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
        pid_ = std::exchange(other.pid_, 0);
    }

    return *this;
}

ProcessMemory::~ProcessMemory() {
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

std::uint32_t ProcessMemory::pid() const noexcept {
    return pid_;
}

HANDLE ProcessMemory::nativeHandle() const noexcept {
    return handle_;
}

std::vector<ModuleInfo> ProcessMemory::modules() const {
    UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        throwLastError("CreateToolhelp32Snapshot failed");
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!::Module32FirstW(snapshot.get(), &entry)) {
        throwLastError("Module32FirstW failed");
    }

    std::vector<ModuleInfo> results;
    do {
        results.push_back(ModuleInfo{
            .name = wideToUtf8(entry.szModule),
            .path = wideToUtf8(entry.szExePath),
            .base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr),
            .size = static_cast<std::size_t>(entry.modBaseSize),
        });
    } while (::Module32NextW(snapshot.get(), &entry));

    return results;
}

std::optional<ModuleInfo> ProcessMemory::findModule(std::string_view name) const {
    const auto target = foldCase(name);
    for (const auto& module : modules()) {
        if (foldCase(module.name) == target) {
            return module;
        }
    }

    return std::nullopt;
}

ModuleInfo ProcessMemory::mainModule() const {
    const auto processModules = modules();
    if (processModules.empty()) {
        throw std::runtime_error("Process has no visible modules");
    }

    return processModules.front();
}

std::optional<MemoryRegion> ProcessMemory::query(std::uintptr_t address) const {
    MEMORY_BASIC_INFORMATION information{};
    const auto queried = ::VirtualQueryEx(
        handle_,
        reinterpret_cast<LPCVOID>(address),
        &information,
        sizeof(information));
    if (queried == 0) {
        return std::nullopt;
    }

    return MemoryRegion{
        .base = reinterpret_cast<std::uintptr_t>(information.BaseAddress),
        .size = information.RegionSize,
        .protection = information.Protect,
        .state = information.State,
        .type = information.Type,
    };
}

std::vector<MemoryRegion> ProcessMemory::regions(std::optional<AddressRange> range) const {
    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);

    std::uintptr_t start = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    std::uintptr_t end = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    if (range) {
        start = std::max(start, range->start);
        end = std::min(end, range->end);
    }

    std::vector<MemoryRegion> results;
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

        const auto base = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        const auto size = information.RegionSize;
        if (base >= end) {
            break;
        }

        results.push_back(MemoryRegion{
            .base = base,
            .size = size,
            .protection = information.Protect,
            .state = information.State,
            .type = information.Type,
        });

        cursor = addSaturated(base, size);
        if (cursor == 0) {
            break;
        }
    }

    return results;
}

std::vector<std::byte> ProcessMemory::read(std::uintptr_t address, std::size_t size) const {
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

std::size_t ProcessMemory::tryRead(std::uintptr_t address, std::span<std::byte> buffer) const noexcept {
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

void ProcessMemory::write(std::uintptr_t address, std::span<const std::byte> bytes) const {
    if (bytes.empty()) {
        return;
    }

    const auto bytesWritten = tryWrite(address, bytes);
    if (bytesWritten != bytes.size()) {
        throw std::runtime_error(buildReadWriteError("WriteProcessMemory", bytes.size(), address));
    }
}

std::size_t ProcessMemory::tryWrite(std::uintptr_t address, std::span<const std::byte> bytes) const noexcept {
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

ProtectionChange ProcessMemory::protect(std::uintptr_t address, std::size_t size, DWORD newProtection) const {
    DWORD previousProtection = 0;
    if (::VirtualProtectEx(
            handle_,
            reinterpret_cast<LPVOID>(address),
            size,
            newProtection,
            &previousProtection) == FALSE) {
        throwLastError("VirtualProtectEx failed");
    }

    return ProtectionChange{
        .previous = previousProtection,
        .current = newProtection,
    };
}

AllocationBlock ProcessMemory::allocate(
    std::size_t size,
    DWORD protection,
    std::optional<std::uintptr_t> nearAddress) const {
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
        protection);
    if (address == nullptr) {
        throwLastError("VirtualAllocEx failed");
    }

    return AllocationBlock{
        .address = reinterpret_cast<std::uintptr_t>(address),
        .size = size,
        .protection = protection,
    };
}

AllocationBlock ProcessMemory::allocateNear(std::uintptr_t nearAddress, std::size_t size, DWORD protection) const {
    SYSTEM_INFO systemInfo{};
    ::GetNativeSystemInfo(&systemInfo);

    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto maxDistance = static_cast<std::uintptr_t>(0x8000'0000ull);
    const auto minimum = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const auto maximum = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    const auto searchStart = std::max(
        minimum,
        alignDown(subtractSaturated(nearAddress, maxDistance), granularity));
    const auto searchEnd = std::min(maximum, alignUp(addSaturated(nearAddress, maxDistance), granularity));

    if (searchEnd <= searchStart) {
        throw std::runtime_error("Unable to build a valid near-allocation search window");
    }

    std::optional<std::uintptr_t> forwardCursor = std::clamp(nearAddress, searchStart, searchEnd - 1);
    std::optional<std::uintptr_t> backwardCursor;
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

        const auto tryOne = [&](const MEMORY_BASIC_INFORMATION& information) -> std::optional<AllocationBlock> {
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
                reinterpret_cast<std::uintptr_t>(forwardInfo->BaseAddress),
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
            const auto base = reinterpret_cast<std::uintptr_t>(backwardInfo->BaseAddress);
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

void ProcessMemory::free(std::uintptr_t address) const {
    if (::VirtualFreeEx(handle_, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE) == FALSE) {
        throwLastError("VirtualFreeEx failed");
    }
}

std::vector<std::uintptr_t> ProcessMemory::scan(
    const BytePattern& pattern,
    std::optional<AddressRange> range) const {
    if (pattern.empty()) {
        return {};
    }

    constexpr std::size_t chunkSize = 64 * 1024;
    const auto overlap = pattern.size() > 0 ? pattern.size() - 1 : 0;

    std::vector<std::uintptr_t> matches;
    for (const auto& region : regions(range)) {
        if (!region.isScanCandidate()) {
            continue;
        }

        const auto regionStart = range ? std::max(region.base, range->start) : region.base;
        const auto regionLimit = addSaturated(region.base, region.size);
        const auto regionEnd = range ? std::min(regionLimit, range->end) : regionLimit;
        if (regionEnd <= regionStart || regionEnd - regionStart < pattern.size()) {
            continue;
        }

        std::vector<std::byte> carry;
        std::vector<std::byte> buffer;
        auto cursor = regionStart;
        while (cursor < regionEnd) {
            const auto remaining = static_cast<std::size_t>(regionEnd - cursor);
            const auto toRead = std::min(chunkSize, remaining);

            buffer.resize(carry.size() + toRead);
            std::copy(carry.begin(), carry.end(), buffer.begin());

            auto target = std::span<std::byte>(buffer).subspan(carry.size(), toRead);
            const auto bytesRead = tryRead(cursor, target);
            if (carry.size() + bytesRead < pattern.size()) {
                cursor += toRead;
                carry.clear();
                continue;
            }

            buffer.resize(carry.size() + bytesRead);
            const auto offsets = pattern.findAll(buffer);
            const auto carryBase = cursor - carry.size();
            for (const auto offset : offsets) {
                if (offset + pattern.size() <= carry.size()) {
                    continue;
                }
                matches.push_back(carryBase + offset);
            }

            if (buffer.size() > overlap) {
                carry.assign(buffer.end() - static_cast<std::ptrdiff_t>(overlap), buffer.end());
            } else {
                carry = buffer;
            }

            cursor += bytesRead;
        }
    }

    return matches;
}

std::vector<std::uintptr_t> ProcessMemory::scanModule(std::string_view moduleName, const BytePattern& pattern) const {
    const auto module = findModule(moduleName);
    if (!module) {
        throw std::runtime_error("Module was not found");
    }

    return scan(pattern, AddressRange{
        .start = module->base,
        .end = module->base + module->size,
    });
}

}  // namespace cepipeline::memory
