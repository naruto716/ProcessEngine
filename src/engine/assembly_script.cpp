#include "hexengine/engine/assembly_script.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hexengine/core/case_insensitive.hpp"
#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"
#include "hexengine/engine/text_assembler.hpp"
#include "name_validation.hpp"

namespace hexengine::engine {
namespace {

constexpr std::size_t kDefaultAllocSize = 0x1000;

enum class DirectiveKind {
    Alloc,
    GlobalAlloc,
    Dealloc,
    AobScan,
    AobScanModule,
    AobScanRegion,
    FullAccess,
    CreateThread,
    Label,
    RegisterSymbol,
    UnregisterSymbol,
};

struct DirectiveCall {
    DirectiveKind kind;
    std::vector<std::string> args;
};

struct LabelHeader {
    std::string expression;
    std::string remainder;
};

struct SegmentTarget {
    std::string expression;
    core::Address address = 0;
    std::optional<std::size_t> capacity = std::nullopt;
};

struct SegmentLine {
    std::size_t lineNumber = 0;
    std::string text;
};

struct ActiveSegment {
    AssemblyScriptSegment metadata;
    std::vector<SegmentLine> lines;
    std::vector<std::string> exportedLabels;
};

struct PendingSegment {
    AssemblyScriptSegment metadata;
    std::vector<SegmentLine> lines;
    std::vector<std::string> exportedLabels;
    std::string lastError;
};

[[nodiscard]] std::string trimCopy(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }

    const auto last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1));
}

[[nodiscard]] std::string stripInlineComments(std::string_view line) {
    auto end = line.size();

    if (const auto semicolon = line.find(';'); semicolon != std::string_view::npos) {
        end = std::min(end, semicolon);
    }

    if (const auto slashSlash = line.find("//"); slashSlash != std::string_view::npos) {
        end = std::min(end, slashSlash);
    }

    return trimCopy(line.substr(0, end));
}

[[nodiscard]] std::string lowercaseCopy(std::string_view text) {
    return core::foldCaseAscii(text);
}

[[nodiscard]] bool looksLikeSimpleName(std::string_view text) {
    try {
        detail::validateUserDefinedName(text, "Assembly label");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

template <typename Container>
void appendUnique(Container& values, std::string_view value) {
    const auto folded = core::foldCaseAscii(value);
    const auto exists = std::any_of(values.begin(), values.end(), [&](const auto& current) {
        return core::foldCaseAscii(current) == folded;
    });
    if (!exists) {
        values.emplace_back(value);
    }
}

[[nodiscard]] std::vector<std::string> splitArguments(std::string_view text) {
    std::vector<std::string> args;
    std::size_t start = 0;
    std::size_t bracketDepth = 0;
    std::size_t parenDepth = 0;

    for (std::size_t index = 0; index < text.size(); ++index) {
        switch (text[index]) {
        case '[':
            ++bracketDepth;
            break;
        case ']':
            if (bracketDepth > 0) {
                --bracketDepth;
            }
            break;
        case '(':
            ++parenDepth;
            break;
        case ')':
            if (parenDepth > 0) {
                --parenDepth;
            }
            break;
        case ',':
            if (bracketDepth == 0 && parenDepth == 0) {
                args.push_back(trimCopy(text.substr(start, index - start)));
                start = index + 1;
            }
            break;
        default:
            break;
        }
    }

    args.push_back(trimCopy(text.substr(start)));
    if (args.size() == 1 && args.front().empty()) {
        args.clear();
    }

    return args;
}

[[nodiscard]] std::optional<DirectiveCall> tryParseDirective(std::string_view line) {
    const auto open = line.find('(');
    const auto close = line.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return std::nullopt;
    }

    if (!trimCopy(line.substr(close + 1)).empty()) {
        return std::nullopt;
    }

    const auto name = lowercaseCopy(trimCopy(line.substr(0, open)));
    if (name.empty()) {
        return std::nullopt;
    }

    auto directive = DirectiveCall{
        .args = splitArguments(line.substr(open + 1, close - open - 1)),
    };

    if (name == "alloc") {
        directive.kind = DirectiveKind::Alloc;
    } else if (name == "globalalloc") {
        directive.kind = DirectiveKind::GlobalAlloc;
    } else if (name == "dealloc") {
        directive.kind = DirectiveKind::Dealloc;
    } else if (name == "aobscan") {
        directive.kind = DirectiveKind::AobScan;
    } else if (name == "aobscanmodule") {
        directive.kind = DirectiveKind::AobScanModule;
    } else if (name == "aobscanregion") {
        directive.kind = DirectiveKind::AobScanRegion;
    } else if (name == "fullaccess") {
        directive.kind = DirectiveKind::FullAccess;
    } else if (name == "createthread") {
        directive.kind = DirectiveKind::CreateThread;
    } else if (name == "label") {
        directive.kind = DirectiveKind::Label;
    } else if (name == "registersymbol") {
        directive.kind = DirectiveKind::RegisterSymbol;
    } else if (name == "unregistersymbol") {
        directive.kind = DirectiveKind::UnregisterSymbol;
    } else {
        return std::nullopt;
    }

    return directive;
}

[[nodiscard]] std::optional<LabelHeader> tryParseLabelHeader(std::string_view line) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    const auto expression = trimCopy(line.substr(0, colon));
    if (expression.empty()) {
        return std::nullopt;
    }

    if (expression.find_first_of(",[]'\"") != std::string::npos) {
        return std::nullopt;
    }

    return LabelHeader{
        .expression = expression,
        .remainder = trimCopy(line.substr(colon + 1)),
    };
}

[[noreturn]] void throwLineError(std::size_t lineNumber, std::string_view message) {
    std::ostringstream stream;
    stream << "AssemblyScript: line " << lineNumber << ": " << message;
    throw std::runtime_error(stream.str());
}

[[nodiscard]] std::size_t resolveSizeArgument(const ScriptContext& script, std::string_view value) {
    const auto resolved = script.resolveAddress(value);
    return static_cast<std::size_t>(resolved);
}

void bindOrValidateScriptLabel(
    ScriptContext& script,
    std::string_view name,
    core::Address address,
    std::size_t lineNumber,
    std::string_view sourceName) {
    detail::validateUserDefinedName(name, "AssemblyScript label");

    if (script.hasLabel(name)) {
        if (const auto existing = script.findLabel(name)) {
            if (*existing != address) {
                throwLineError(
                    lineNumber,
                    std::string(sourceName) + " result for '" + std::string(name) +
                        "' conflicts with an existing bound label");
            }
            return;
        }

        script.bindLabel(name, address);
        return;
    }

    script.bindLabel(name, address);
}

[[nodiscard]] SegmentTarget resolveSegmentTarget(const ScriptContext& script, std::size_t lineNumber, std::string_view expression) {
    if (const auto local = script.findLocalAllocation(expression)) {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = local->address,
            .capacity = local->size,
        };
    }

    if (const auto global = script.session().findGlobalAllocation(expression)) {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = global->address,
            .capacity = global->size,
        };
    }

    try {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = script.resolveAddress(expression),
            .capacity = std::nullopt,
        };
    } catch (const std::exception& exception) {
        if (looksLikeSimpleName(expression)) {
            throwLineError(
                lineNumber,
                "assembly label '" + std::string(expression) + "' cannot define a segment target because it is not bound");
        }

        throwLineError(
            lineNumber,
            "segment target '" + std::string(expression) + "' did not resolve: " + exception.what());
    }
}

[[nodiscard]] std::optional<SegmentTarget> tryResolveHeaderTarget(
    const ScriptContext& script,
    std::string_view expression) {
    if (const auto local = script.findLocalAllocation(expression)) {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = local->address,
            .capacity = local->size,
        };
    }

    if (const auto global = script.session().findGlobalAllocation(expression)) {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = global->address,
            .capacity = global->size,
        };
    }

    try {
        return SegmentTarget{
            .expression = std::string(expression),
            .address = script.resolveAddress(expression),
            .capacity = std::nullopt,
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void executeDirective(
    ScriptContext& script,
    const DirectiveCall& directive,
    std::size_t lineNumber) {
    switch (directive.kind) {
    case DirectiveKind::Alloc: {
        if (directive.args.empty() || directive.args.size() > 3) {
            throwLineError(lineNumber, "alloc expects 1 to 3 arguments");
        }

        AllocationRequest request{
            .name = directive.args[0],
            .size = directive.args.size() >= 2 ? resolveSizeArgument(script, directive.args[1]) : kDefaultAllocSize,
        };
        if (directive.args.size() == 3) {
            request.nearAddress = script.resolveAddress(directive.args[2]);
        }
        (void)script.alloc(request);
        return;
    }

    case DirectiveKind::GlobalAlloc: {
        if (directive.args.empty() || directive.args.size() > 3) {
            throwLineError(lineNumber, "globalAlloc expects 1 to 3 arguments");
        }

        AllocationRequest request{
            .name = directive.args[0],
            .size = directive.args.size() >= 2 ? resolveSizeArgument(script, directive.args[1]) : kDefaultAllocSize,
        };
        if (directive.args.size() == 3) {
            request.nearAddress = script.resolveAddress(directive.args[2]);
        }
        (void)script.globalAlloc(request);
        return;
    }

    case DirectiveKind::Dealloc:
        if (directive.args.size() != 1) {
            throwLineError(lineNumber, "dealloc expects exactly 1 argument");
        }
        if (!script.dealloc(directive.args[0])) {
            throwLineError(lineNumber, "dealloc could not find '" + directive.args[0] + '\'');
        }
        return;

    case DirectiveKind::AobScan: {
        if (directive.args.size() != 2) {
            throwLineError(lineNumber, "aobScan expects exactly 2 arguments");
        }

        try {
            const auto hits = script.session().aobScan(directive.args[1]);
            if (hits.empty()) {
                throwLineError(lineNumber, "aobScan found no match for '" + directive.args[0] + '\'');
            }

            bindOrValidateScriptLabel(script, directive.args[0], hits.front(), lineNumber, "aobScan");
        } catch (const std::exception& exception) {
            if (std::string_view(exception.what()).find("AssemblyScript: line ") == 0) {
                throw;
            }
            throwLineError(lineNumber, exception.what());
        }
        return;
    }

    case DirectiveKind::AobScanModule: {
        if (directive.args.size() != 3) {
            throwLineError(lineNumber, "aobScanModule expects exactly 3 arguments");
        }

        try {
            const auto hits = script.session().aobScanModule(directive.args[1], directive.args[2]);
            if (hits.empty()) {
                throwLineError(lineNumber, "aobScanModule found no match for '" + directive.args[0] + '\'');
            }

            bindOrValidateScriptLabel(script, directive.args[0], hits.front(), lineNumber, "aobScanModule");
        } catch (const std::exception& exception) {
            if (std::string_view(exception.what()).find("AssemblyScript: line ") == 0) {
                throw;
            }
            throwLineError(lineNumber, exception.what());
        }
        return;
    }

    case DirectiveKind::AobScanRegion: {
        if (directive.args.size() != 4) {
            throwLineError(lineNumber, "aobScanRegion expects exactly 4 arguments");
        }

        try {
            const auto startAddress = script.resolveAddress(directive.args[1]);
            const auto stopAddress = script.resolveAddress(directive.args[2]);
            const auto hits = script.session().aobScanRegion(startAddress, stopAddress, directive.args[3]);
            if (hits.empty()) {
                throwLineError(lineNumber, "aobScanRegion found no match for '" + directive.args[0] + '\'');
            }

            bindOrValidateScriptLabel(script, directive.args[0], hits.front(), lineNumber, "aobScanRegion");
        } catch (const std::exception& exception) {
            if (std::string_view(exception.what()).find("AssemblyScript: line ") == 0) {
                throw;
            }
            throwLineError(lineNumber, exception.what());
        }
        return;
    }

    case DirectiveKind::FullAccess: {
        if (directive.args.size() != 2) {
            throwLineError(lineNumber, "fullAccess expects exactly 2 arguments");
        }

        try {
            const auto address = script.resolveAddress(directive.args[0]);
            const auto size = resolveSizeArgument(script, directive.args[1]);
            (void)script.session().fullAccess(address, size);
        } catch (const std::exception& exception) {
            throwLineError(lineNumber, exception.what());
        }
        return;
    }

    case DirectiveKind::CreateThread: {
        if (directive.args.size() != 1) {
            throwLineError(lineNumber, "createThread expects exactly 1 argument");
        }

        try {
            const auto entryAddress = script.resolveAddress(directive.args[0]);
            script.session().executeCode(entryAddress);
        } catch (const std::exception& exception) {
            throwLineError(lineNumber, exception.what());
        }
        return;
    }

    case DirectiveKind::Label:
        if (directive.args.size() != 1) {
            throwLineError(lineNumber, "label expects exactly 1 argument");
        }

        if (script.findLocalAllocation(directive.args[0]) || script.session().findGlobalAllocation(directive.args[0])) {
            return;
        }

        script.resetLabel(directive.args[0]);
        return;

    case DirectiveKind::RegisterSymbol:
        if (directive.args.size() != 1) {
            throwLineError(lineNumber, "registerSymbol expects exactly 1 argument");
        }
        (void)script.registerSymbol(directive.args[0]);
        return;

    case DirectiveKind::UnregisterSymbol:
        if (directive.args.size() != 1) {
            throwLineError(lineNumber, "unregisterSymbol expects exactly 1 argument");
        }
        (void)script.unregisterSymbol(directive.args[0]);
        return;
    }
}

[[nodiscard]] ActiveSegment startSegment(const SegmentTarget& target, std::size_t lineNumber) {
    ActiveSegment active;
    active.metadata = AssemblyScriptSegment{
        .targetExpression = target.expression,
        .address = target.address,
        .capacity = target.capacity,
        .firstLine = lineNumber,
    };

    return active;
}

[[nodiscard]] bool isDeferredChunkError(std::string_view message) {
    return message.find("TextAssembler: script label is not bound: ") != std::string_view::npos;
}

void appendAssemblyLine(ActiveSegment& active, std::string_view line, std::size_t lineNumber) {
    std::string text(line);
    text.push_back('\n');
    active.lines.push_back(SegmentLine{
        .lineNumber = lineNumber,
        .text = std::move(text),
    });
}

template <typename BoundSet>
[[nodiscard]] bool tryAssembleSegment(
    ScriptContext& script,
    const ActiveSegment& segment,
    AssemblyScriptResult& result,
    BoundSet& boundExportedLabels,
    std::string* deferredError) {
    try {
        auto assembler = segment.metadata.capacity.has_value()
            ? std::make_unique<TextAssembler>(script, segment.metadata.address, *segment.metadata.capacity)
            : std::make_unique<TextAssembler>(script, segment.metadata.address);

        for (const auto& line : segment.lines) {
            try {
                assembler->append(line.text);
            } catch (const std::exception& exception) {
                std::ostringstream stream;
                stream << "AssemblyScript: line " << line.lineNumber
                       << " in target '" << segment.metadata.targetExpression
                       << "': " << exception.what();
                const auto message = stream.str();
                if (deferredError != nullptr && isDeferredChunkError(message)) {
                    *deferredError = message;
                    return false;
                }
                throw std::runtime_error(message);
            }
        }

        auto completed = segment.metadata;
        try {
            completed.emittedBytes = assembler->offset();
            completed.writtenBytes = assembler->flush();
        } catch (const std::exception& exception) {
            std::ostringstream stream;
            stream << "AssemblyScript: flush failed for target '" << completed.targetExpression
                   << "' (started at line " << completed.firstLine << "): "
                   << exception.what();
            throw std::runtime_error(stream.str());
        }

        for (const auto& labelName : segment.exportedLabels) {
            const auto folded = core::foldCaseAscii(labelName);
            if (!boundExportedLabels.insert(folded).second) {
                throw std::runtime_error(
                    "AssemblyScript: explicit script label defined more than once: " + labelName);
            }

            const auto address = assembler->labelAddress(labelName);
            if (!address.has_value()) {
                throw std::runtime_error(
                    "AssemblyScript: explicit script label was declared but not bound in target '" +
                    completed.targetExpression + "': " + labelName);
            }

            script.setLabelAddress(labelName, *address);
        }

        result.segments.push_back(std::move(completed));
        return true;
    } catch (const std::exception& exception) {
        if (deferredError != nullptr && isDeferredChunkError(exception.what())) {
            *deferredError = exception.what();
            return false;
        }
        throw;
    }
}

template <typename BoundSet>
void flushActiveSegment(
    ScriptContext& script,
    std::optional<ActiveSegment>& active,
    std::vector<PendingSegment>& pending,
    AssemblyScriptResult& result,
    BoundSet& boundExportedLabels) {
    if (!active.has_value()) {
        return;
    }

    std::string deferredError;
    if (!tryAssembleSegment(script, *active, result, boundExportedLabels, &deferredError)) {
        pending.push_back(PendingSegment{
            .metadata = std::move(active->metadata),
            .lines = std::move(active->lines),
            .exportedLabels = std::move(active->exportedLabels),
            .lastError = std::move(deferredError),
        });
    }

    active.reset();
}

template <typename BoundSet>
void drainPendingSegments(
    ScriptContext& script,
    std::vector<PendingSegment>& pending,
    AssemblyScriptResult& result,
    BoundSet& boundExportedLabels) {
    bool progressed = true;
    while (progressed) {
        progressed = false;

        for (auto iterator = pending.begin(); iterator != pending.end();) {
            ActiveSegment attempt{
                .metadata = iterator->metadata,
                .lines = iterator->lines,
                .exportedLabels = iterator->exportedLabels,
            };

            std::string deferredError;
            if (tryAssembleSegment(script, attempt, result, boundExportedLabels, &deferredError)) {
                iterator = pending.erase(iterator);
                progressed = true;
                continue;
            }

            iterator->lastError = std::move(deferredError);
            ++iterator;
        }
    }
}

}  // namespace

AssemblyScript::AssemblyScript(ScriptContext& script)
    : script_(script) {
}

ScriptContext& AssemblyScript::script() noexcept {
    return script_;
}

const ScriptContext& AssemblyScript::script() const noexcept {
    return script_;
}

AssemblyScriptResult AssemblyScript::execute(std::string_view source) {
    AssemblyScriptResult result;
    std::optional<ActiveSegment> active;
    std::vector<PendingSegment> pending;
    std::unordered_set<std::string, core::CaseInsensitiveStringHash, core::CaseInsensitiveStringEqual> boundExportedLabels;

    std::size_t lineNumber = 0;
    std::size_t offset = 0;
    while (offset <= source.size()) {
        ++lineNumber;

        const auto lineEnd = source.find('\n', offset);
        const auto rawLine = lineEnd == std::string_view::npos
            ? source.substr(offset)
            : source.substr(offset, lineEnd - offset);
        const auto line = stripInlineComments(rawLine);

        if (!line.empty()) {
            if (const auto directive = tryParseDirective(line)) {
                try {
                    flushActiveSegment(script_, active, pending, result, boundExportedLabels);
                    drainPendingSegments(script_, pending, result, boundExportedLabels);
                    executeDirective(script_, *directive, lineNumber);
                    drainPendingSegments(script_, pending, result, boundExportedLabels);
                } catch (const std::exception& exception) {
                    throw std::runtime_error(exception.what());
                }
            } else if (const auto header = tryParseLabelHeader(line)) {
                if (const auto target = tryResolveHeaderTarget(script_, header->expression)) {
                    try {
                        flushActiveSegment(script_, active, pending, result, boundExportedLabels);
                        drainPendingSegments(script_, pending, result, boundExportedLabels);
                        active = startSegment(*target, lineNumber);
                        if (!header->remainder.empty()) {
                            appendAssemblyLine(*active, header->remainder, lineNumber);
                        }
                    } catch (const std::exception& exception) {
                        if (std::string_view(exception.what()).find("AssemblyScript:") == 0) {
                            throw;
                        }
                        std::ostringstream stream;
                        stream << "AssemblyScript: line " << lineNumber << ": " << exception.what();
                        throw std::runtime_error(stream.str());
                    }
                } else {
                    if (!looksLikeSimpleName(header->expression)) {
                        (void)resolveSegmentTarget(script_, lineNumber, header->expression);
                    }

                    if (!active.has_value()) {
                        throwLineError(
                            lineNumber,
                            "internal asm label '" + header->expression + "' cannot appear before any current address");
                    }

                    if (script_.hasLabel(header->expression)) {
                        appendUnique(active->exportedLabels, header->expression);
                    }
                    appendAssemblyLine(*active, line, lineNumber);
                }
            } else {
                if (!active.has_value()) {
                    throwLineError(lineNumber, "assembly instruction has no current address");
                }

                appendAssemblyLine(*active, line, lineNumber);
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        offset = lineEnd + 1;
    }

    try {
        flushActiveSegment(script_, active, pending, result, boundExportedLabels);
        drainPendingSegments(script_, pending, result, boundExportedLabels);
    } catch (const std::exception& exception) {
        if (std::string_view(exception.what()).find("AssemblyScript:") == 0) {
            throw;
        }

        throw std::runtime_error(std::string("AssemblyScript: ") + exception.what());
    }

    if (!pending.empty()) {
        throw std::runtime_error(pending.front().lastError);
    }

    std::sort(result.segments.begin(), result.segments.end(), [](const AssemblyScriptSegment& left, const AssemblyScriptSegment& right) {
        return left.firstLine < right.firstLine;
    });

    return result;
}

}  // namespace hexengine::engine
