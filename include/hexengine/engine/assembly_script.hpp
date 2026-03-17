#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hexengine/core/memory_types.hpp"

namespace hexengine::engine {

class ScriptContext;

struct AssemblyScriptSegment {
    std::string targetExpression;
    core::Address address = 0;
    std::optional<std::size_t> capacity = std::nullopt;
    std::size_t emittedBytes = 0;
    std::size_t writtenBytes = 0;
    std::size_t firstLine = 0;
};

struct AssemblyScriptResult {
    std::vector<AssemblyScriptSegment> segments;
};

class AssemblyScript {
public:
    explicit AssemblyScript(ScriptContext& script);

    [[nodiscard]] ScriptContext& script() noexcept;
    [[nodiscard]] const ScriptContext& script() const noexcept;

    [[nodiscard]] AssemblyScriptResult execute(std::string_view source);

private:
    ScriptContext& script_;
};

}  // namespace hexengine::engine
