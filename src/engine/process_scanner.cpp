#include "hexengine/engine/process_scanner.hpp"

#include <algorithm>
#include <limits>

namespace hexengine::engine {
namespace {

[[nodiscard]] core::Address addSaturated(core::Address value, core::Address delta) noexcept {
    if (delta > std::numeric_limits<core::Address>::max() - value) {
        return std::numeric_limits<core::Address>::max();
    }
    return value + delta;
}

}  // namespace

ProcessScanner::ProcessScanner(const backend::IProcessBackend& process)
    : process_(process) {
}

std::vector<core::Address> ProcessScanner::scan(
    const core::BytePattern& pattern,
    std::optional<core::AddressRange> range) const {
    if (pattern.empty()) {
        return {};
    }

    constexpr std::size_t chunkSize = 64 * 1024;
    const auto overlap = pattern.size() > 0 ? pattern.size() - 1 : 0;

    std::vector<core::Address> matches;
    for (const auto& region : process_.regions(range)) {
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
            const auto bytesRead = process_.tryRead(cursor, target);
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

std::vector<core::Address> ProcessScanner::scanModule(
    std::string_view moduleName,
    const core::BytePattern& pattern) const {
    const auto module = process_.findModule(moduleName);
    if (!module) {
        throw std::runtime_error("Module was not found");
    }

    return scan(pattern, core::AddressRange{
        .start = module->base,
        .end = module->base + module->size,
    });
}

}  // namespace hexengine::engine
