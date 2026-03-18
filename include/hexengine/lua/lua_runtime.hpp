#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hexengine::engine {
class EngineSession;
}

namespace hexengine::lua {

using LuaValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

struct LuaResult {
    std::vector<LuaValue> values;
};

class LuaRuntime {
public:
    explicit LuaRuntime(engine::EngineSession& session);
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    [[nodiscard]] LuaResult runGlobal(std::string_view source, std::string_view chunkName = "global");
    [[nodiscard]] LuaResult runScript(
        std::string_view scriptId,
        std::string_view source,
        std::string_view chunkName = "script");

    [[nodiscard]] bool destroyScript(std::string_view scriptId);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hexengine::lua
