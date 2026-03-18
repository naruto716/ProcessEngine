#include "hexengine/engine/remote_assembler.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "write_with_temporary_protection.hpp"

namespace hexengine::engine {

RemoteAssembler::RemoteAssembler(backend::IProcessBackend& process,
                                 core::Address baseAddress)
    : process_(process),
      baseAddress_(baseAddress) {
    const auto arch = (process.pointerSize() == 4)
        ? asmjit::Arch::kX86
        : asmjit::Arch::kX64;

    asmjit::Environment env(arch);
    auto err = code_.init(env, baseAddress);
    if (err != asmjit::kErrorOk) {
        throw std::runtime_error(
            "RemoteAssembler: CodeHolder::init failed: " +
            std::string(asmjit::DebugUtils::error_as_string(err)));
    }

    err = code_.attach(&assembler_);
    if (err != asmjit::kErrorOk) {
        throw std::runtime_error(
            "RemoteAssembler: failed to attach assembler: " +
            std::string(asmjit::DebugUtils::error_as_string(err)));
    }
}

RemoteAssembler::RemoteAssembler(backend::IProcessBackend& process,
                                 core::Address baseAddress,
                                 std::size_t caveSizeBytes)
    : RemoteAssembler(process, baseAddress) {
    if (caveSizeBytes == 0) {
        throw std::invalid_argument("RemoteAssembler: cave size must be greater than zero");
    }
    caveSizeBytes_ = caveSizeBytes;
}

asmjit::x86::Assembler& RemoteAssembler::assembler() noexcept {
    return assembler_;
}

std::size_t RemoteAssembler::offset() const noexcept {
    return assembler_.offset();
}

std::size_t RemoteAssembler::capacity() const noexcept {
    return caveSizeBytes_.value_or(std::numeric_limits<std::size_t>::max());
}

core::Address RemoteAssembler::currentAddress() const noexcept {
    return baseAddress_ + assembler_.offset();
}

core::Address RemoteAssembler::baseAddress() const noexcept {
    return baseAddress_;
}

std::optional<core::Address> RemoteAssembler::labelAddress(std::string_view name) const {
    const auto entries = code_.label_entries();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!entry.has_name() || entry.name_size() != name.size()) {
            continue;
        }

        if (std::string_view(entry.name(), entry.name_size()) != name) {
            continue;
        }

        if (!entry.is_bound()) {
            return std::nullopt;
        }

        return baseAddress_ + code_.label_offset_from_base(static_cast<uint32_t>(index));
    }
    return std::nullopt;
}

std::vector<std::string> RemoteAssembler::namedLabels() const {
    std::vector<std::string> labels;
    for (const auto& entry : code_.label_entries()) {
        if (!entry.has_name()) {
            continue;
        }

        labels.emplace_back(entry.name(), entry.name_size());
    }

    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
}

std::vector<std::string> RemoteAssembler::unresolvedLabelNames() const {
    std::vector<std::string> labels;
    for (const auto& entry : code_.label_entries()) {
        if (!entry.has_name() || entry.is_bound() || !entry.has_fixups()) {
            continue;
        }

        labels.emplace_back(entry.name(), entry.name_size());
    }

    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    return labels;
}

std::size_t RemoteAssembler::flush() {
    const auto currentOffset = assembler_.offset();

    if (caveSizeBytes_.has_value() && currentOffset > *caveSizeBytes_) {
        throw std::runtime_error(
            "RemoteAssembler: emitted " + std::to_string(currentOffset) +
            " bytes but the code cave is only " + std::to_string(*caveSizeBytes_) +
            " bytes");
    }

    if (currentOffset <= flushedOffset_) {
        return 0;
    }

    const auto* section = code_.text_section();
    if (!section) {
        throw std::runtime_error("RemoteAssembler: no text section in CodeHolder");
    }

    const auto& buffer = section->buffer();
    const auto bytesToWrite = currentOffset - flushedOffset_;
    const auto remoteAddress = baseAddress_ + flushedOffset_;

    const auto* begin = reinterpret_cast<const std::byte*>(buffer.data() + flushedOffset_);
    detail::writeWithTemporaryProtection(process_, remoteAddress, std::span<const std::byte>(begin, bytesToWrite));

    flushedOffset_ = currentOffset;
    return bytesToWrite;
}

}  // namespace hexengine::engine
