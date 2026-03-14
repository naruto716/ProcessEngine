#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hexengine::core {

using Address = std::uintptr_t;
using ProcessId = std::uint32_t;

enum class MemoryState : std::uint8_t {
    Unknown,
    Free,
    Reserved,
    Committed,
};

enum class MemoryType : std::uint8_t {
    Unknown,
    Private,
    Mapped,
    Image,
};

enum class ProtectionFlags : std::uint32_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Execute = 1u << 2,
    Guard = 1u << 3,
    NoAccess = 1u << 4,
    CopyOnWrite = 1u << 5,
};

[[nodiscard]] constexpr ProtectionFlags operator|(ProtectionFlags left, ProtectionFlags right) noexcept {
    return static_cast<ProtectionFlags>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr ProtectionFlags operator&(ProtectionFlags left, ProtectionFlags right) noexcept {
    return static_cast<ProtectionFlags>(
        static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr ProtectionFlags operator~(ProtectionFlags flags) noexcept {
    return static_cast<ProtectionFlags>(~static_cast<std::uint32_t>(flags));
}

inline ProtectionFlags& operator|=(ProtectionFlags& left, ProtectionFlags right) noexcept {
    left = left | right;
    return left;
}

inline ProtectionFlags& operator&=(ProtectionFlags& left, ProtectionFlags right) noexcept {
    left = left & right;
    return left;
}

[[nodiscard]] constexpr bool hasFlag(ProtectionFlags flags, ProtectionFlags bit) noexcept {
    return (flags & bit) != ProtectionFlags::None;
}

struct AddressRange {
    Address start = 0;
    Address end = 0;
};

struct ModuleInfo {
    std::string name;
    std::string path;
    Address base = 0;
    std::size_t size = 0;
};

struct MemoryRegion {
    Address base = 0;
    std::size_t size = 0;
    ProtectionFlags protection = ProtectionFlags::None;
    MemoryState state = MemoryState::Unknown;
    MemoryType type = MemoryType::Unknown;

    [[nodiscard]] bool isCommitted() const noexcept {
        return state == MemoryState::Committed;
    }

    [[nodiscard]] bool isReadable() const noexcept {
        return hasFlag(protection, ProtectionFlags::Read) && !hasFlag(protection, ProtectionFlags::NoAccess);
    }

    [[nodiscard]] bool isWritable() const noexcept {
        return hasFlag(protection, ProtectionFlags::Write) && !hasFlag(protection, ProtectionFlags::NoAccess);
    }

    [[nodiscard]] bool isExecutable() const noexcept {
        return hasFlag(protection, ProtectionFlags::Execute) && !hasFlag(protection, ProtectionFlags::NoAccess);
    }

    [[nodiscard]] bool isScanCandidate() const noexcept {
        return isCommitted() && isReadable() && !hasFlag(protection, ProtectionFlags::Guard);
    }
};

struct ProtectionChange {
    ProtectionFlags previous = ProtectionFlags::None;
    ProtectionFlags current = ProtectionFlags::None;
};

struct AllocationBlock {
    Address address = 0;
    std::size_t size = 0;
    ProtectionFlags protection = ProtectionFlags::None;
};

inline constexpr ProtectionFlags kReadWriteExecute =
    ProtectionFlags::Read | ProtectionFlags::Write | ProtectionFlags::Execute;

}  // namespace hexengine::core
