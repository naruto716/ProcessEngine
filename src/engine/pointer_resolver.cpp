#include "hexengine/engine/pointer_resolver.hpp"

#include "hexengine/engine/address_resolver.hpp"

#include <limits>
#include <stdexcept>

namespace hexengine::engine {
namespace {

[[nodiscard]] core::Address applyOffset(core::Address value, std::ptrdiff_t offset) {
    if (offset >= 0) {
        const auto delta = static_cast<core::Address>(offset);
        if (delta > std::numeric_limits<core::Address>::max() - value) {
            throw std::overflow_error("Pointer-chain resolution overflowed address range");
        }
        return value + delta;
    }

    const auto delta = static_cast<core::Address>(-offset);
    if (value < delta) {
        throw std::overflow_error("Pointer-chain resolution underflowed address range");
    }
    return value - delta;
}

}  // namespace

PointerResolver::PointerResolver(const backend::IProcessBackend& process, const AddressResolver& addresses)
    : process_(process),
      addresses_(addresses) {
}

core::Address PointerResolver::resolve(core::Address base, std::span<const std::ptrdiff_t> offsets) const {
    auto current = base;
    for (const auto offset : offsets) {
        current = applyOffset(readPointer(current), offset);
    }
    return current;
}

core::Address PointerResolver::readPointer(core::Address address) const {
    switch (process_.pointerSize()) {
    case 4:
        return static_cast<core::Address>(process_.readValue<std::uint32_t>(address));
    case 8:
        return static_cast<core::Address>(process_.readValue<std::uint64_t>(address));
    default:
        throw std::runtime_error("Unsupported target pointer size");
    }
}

}  // namespace hexengine::engine
