#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asmjit/x86.h>

#include "hexengine/backend/process_backend.hpp"
#include "hexengine/core/memory_types.hpp"

namespace hexengine::engine {

class RemoteAssembler {
public:
    /// Create an assembler targeting a remote address with no capacity guard.
    /// Useful for direct patch sites where the caller intentionally controls how much code is emitted.
    RemoteAssembler(backend::IProcessBackend& process,
                    core::Address baseAddress);

    /// Create an assembler targeting a code cave at \p baseAddress in the remote process.
    /// The architecture (32-bit or 64-bit) is inferred from the backend's pointer size.
    RemoteAssembler(backend::IProcessBackend& process,
                    core::Address baseAddress,
                    std::size_t caveSizeBytes);

    /// Access the raw AsmJit x86/x64 assembler for emitting instructions.
    [[nodiscard]] asmjit::x86::Assembler& assembler() noexcept;

    /// Number of bytes emitted so far.
    [[nodiscard]] std::size_t offset() const noexcept;

    /// Maximum number of bytes the code cave can hold, or `max()` if no guard is active.
    [[nodiscard]] std::size_t capacity() const noexcept;

    /// Remote address corresponding to the current emit position.
    [[nodiscard]] core::Address currentAddress() const noexcept;

    /// Remote base address of the code cave.
    [[nodiscard]] core::Address baseAddress() const noexcept;

    /// Returns the resolved absolute address of the named label, if it exists and is bound.
    [[nodiscard]] std::optional<core::Address> labelAddress(std::string_view name) const;

    /// Returns all named labels currently known to the assembler.
    [[nodiscard]] std::vector<std::string> namedLabels() const;

    /// Returns any named labels that still have unresolved fixups.
    [[nodiscard]] std::vector<std::string> unresolvedLabelNames() const;

    /// Flush assembled bytes to the remote process.
    /// Writes only the bytes emitted since the last flush.
    /// Returns the total number of bytes written by this call.
    std::size_t flush();

private:
    backend::IProcessBackend& process_;
    core::Address baseAddress_;
    std::optional<std::size_t> caveSizeBytes_;
    std::size_t flushedOffset_ = 0;

    asmjit::CodeHolder code_;
    asmjit::x86::Assembler assembler_;
};

}  // namespace hexengine::engine
