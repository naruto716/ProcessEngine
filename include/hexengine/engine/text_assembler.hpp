#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include <asmjit/core.h>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/core/memory_types.hpp"
#include "hexengine/engine/allocation_repository.hpp"
#include "hexengine/engine/remote_assembler.hpp"

namespace asmtk {
class AsmParser;
}

namespace hexengine::engine {

class ScriptContext;

class TextAssembler {
public:
    TextAssembler(ScriptContext& script, core::Address baseAddress, std::size_t caveSizeBytes);
    TextAssembler(ScriptContext& script, const AllocationRecord& target);
    ~TextAssembler();

    void append(std::string_view text);

    [[nodiscard]] core::Address baseAddress() const noexcept;
    [[nodiscard]] core::Address currentAddress() const noexcept;
    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

    std::size_t flush();

private:
    static asmjit::Error ASMJIT_CDECL resolveUnknownSymbolThunk(
        asmtk::AsmParser* parser,
        asmjit::Operand* out,
        const char* name,
        size_t size);

    void collectDeclaredLabels(std::string_view text);
    asmjit::Error resolveUnknownSymbol(asmjit::Operand& out, std::string_view name);
    [[nodiscard]] std::string buildParseErrorMessage(asmjit::Error error) const;

    ScriptContext& script_;
    RemoteAssembler remote_;
    std::unique_ptr<asmtk::AsmParser> parser_;
    std::string callbackError_;
    std::unordered_set<std::string, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> currentDeclaredLabels_;
};

}  // namespace hexengine::engine
