#include "hexengine/engine/remote_assembler.hpp"

#include <stdexcept>
#include <string>

namespace hexengine::engine {

RemoteAssembler::RemoteAssembler(backend::IProcessBackend& process,
                                 core::Address baseAddress,
                                 std::size_t caveSizeBytes)
    : process_(process),
      baseAddress_(baseAddress),
      caveSizeBytes_(caveSizeBytes) {
    if (caveSizeBytes == 0) {
        throw std::invalid_argument("RemoteAssembler: cave size must be greater than zero");
    }

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

asmjit::x86::Assembler& RemoteAssembler::assembler() noexcept {
    return assembler_;
}

std::size_t RemoteAssembler::offset() const noexcept {
    return assembler_.offset();
}

std::size_t RemoteAssembler::capacity() const noexcept {
    return caveSizeBytes_;
}

core::Address RemoteAssembler::currentAddress() const noexcept {
    return baseAddress_ + assembler_.offset();
}

core::Address RemoteAssembler::baseAddress() const noexcept {
    return baseAddress_;
}

std::size_t RemoteAssembler::flush() {
    const auto currentOffset = assembler_.offset();

    if (currentOffset > caveSizeBytes_) {
        throw std::runtime_error(
            "RemoteAssembler: emitted " + std::to_string(currentOffset) +
            " bytes but the code cave is only " + std::to_string(caveSizeBytes_) +
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
    process_.write(remoteAddress, std::span<const std::byte>(begin, bytesToWrite));

    flushedOffset_ = currentOffset;
    return bytesToWrite;
}

}  // namespace hexengine::engine
