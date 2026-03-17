#include "hexengine/engine/text_assembler.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <asmjit/x86.h>
#include <asmtk/asmtk.h>

#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"

namespace hexengine::engine {
namespace {

[[nodiscard]] std::string joinNames(const std::vector<std::string>& names) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << names[index];
    }
    return stream.str();
}

}  // namespace

TextAssembler::TextAssembler(ScriptContext& script, core::Address baseAddress, std::size_t caveSizeBytes)
    : script_(script),
      remote_(script.session().process(), baseAddress, caveSizeBytes),
      parser_(std::make_unique<asmtk::AsmParser>(&remote_.assembler())) {
    parser_->set_unknown_symbol_handler(resolveUnknownSymbolThunk, this);
}

TextAssembler::TextAssembler(ScriptContext& script, core::Address baseAddress)
    : script_(script),
      remote_(script.session().process(), baseAddress),
      parser_(std::make_unique<asmtk::AsmParser>(&remote_.assembler())) {
    parser_->set_unknown_symbol_handler(resolveUnknownSymbolThunk, this);
}

TextAssembler::TextAssembler(ScriptContext& script, const AllocationRecord& target)
    : TextAssembler(script, target.address, target.size) {
}

TextAssembler::~TextAssembler() = default;

void TextAssembler::append(std::string_view text) {
    callbackError_.clear();
    collectDeclaredLabels(text);

    const auto error = parser_->parse(text.data(), text.size());
    if (error == asmjit::Error::kOk) {
        return;
    }

    if (!callbackError_.empty()) {
        throw std::runtime_error(callbackError_);
    }

    throw std::runtime_error(buildParseErrorMessage(error));
}

core::Address TextAssembler::baseAddress() const noexcept {
    return remote_.baseAddress();
}

core::Address TextAssembler::currentAddress() const noexcept {
    return remote_.currentAddress();
}

std::size_t TextAssembler::offset() const noexcept {
    return remote_.offset();
}

std::size_t TextAssembler::capacity() const noexcept {
    return remote_.capacity();
}

std::size_t TextAssembler::flush() {
    if (const auto unresolved = remote_.unresolvedLabelNames(); !unresolved.empty()) {
        throw std::runtime_error("TextAssembler: unresolved label(s): " + joinNames(unresolved));
    }

    return remote_.flush();
}

asmjit::Error ASMJIT_CDECL TextAssembler::resolveUnknownSymbolThunk(
    asmtk::AsmParser* parser,
    asmjit::Operand* out,
    const char* name,
    size_t size) {
    auto* assembler = static_cast<TextAssembler*>(parser->unknown_symbol_handler_data());
    return assembler->resolveUnknownSymbol(*out, std::string_view(name, size));
}

asmjit::Error TextAssembler::resolveUnknownSymbol(asmjit::Operand& out, std::string_view name) {
    if (currentDeclaredLabels_.find(std::string(name)) != currentDeclaredLabels_.end()) {
        return asmjit::Error::kOk;
    }

    if (script_.hasLabel(name)) {
        if (const auto address = script_.findLabel(name)) {
            out = asmjit::imm(*address);
            return asmjit::Error::kOk;
        }

        callbackError_ = "TextAssembler: script label is not bound: " + std::string(name);
        return asmjit::Error::kInvalidLabel;
    }

    if (const auto symbol = script_.session().resolveSymbol(name)) {
        out = asmjit::imm(symbol->address);
        return asmjit::Error::kOk;
    }

    return asmjit::Error::kOk;
}

void TextAssembler::collectDeclaredLabels(std::string_view text) {
    currentDeclaredLabels_.clear();

    std::size_t offset = 0;
    while (offset < text.size()) {
        auto lineEnd = text.find('\n', offset);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }

        auto line = text.substr(offset, lineEnd - offset);
        if (const auto semicolon = line.find(';'); semicolon != std::string_view::npos) {
            line = line.substr(0, semicolon);
        }

        if (const auto slashSlash = line.find("//"); slashSlash != std::string_view::npos) {
            line = line.substr(0, slashSlash);
        }

        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);

            auto tokenEnd = line.find_first_of(" \t\r:");
            if (tokenEnd == std::string_view::npos) {
                tokenEnd = line.size();
            }

            auto token = line.substr(0, tokenEnd);
            auto colonPos = line.find(':', tokenEnd);
            if (!token.empty() && colonPos != std::string_view::npos) {
                const auto beforeColonOnlyWhitespace =
                    line.substr(tokenEnd, colonPos - tokenEnd).find_first_not_of(" \t\r") == std::string_view::npos;
                if (beforeColonOnlyWhitespace) {
                    currentDeclaredLabels_.insert(std::string(token));
                }
            }
        }

        offset = lineEnd + (lineEnd < text.size() ? 1 : 0);
    }
}

std::string TextAssembler::buildParseErrorMessage(asmjit::Error error) const {
    std::ostringstream stream;
    stream << "TextAssembler: parse failed at offset " << parser_->current_command_offset()
           << ": " << asmjit::DebugUtils::error_as_string(error);
    return stream.str();
}

}  // namespace hexengine::engine
