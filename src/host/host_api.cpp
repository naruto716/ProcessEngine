#include "hexengine/host/host_api.h"

#include <Windows.h>
#include <combaseapi.h>

#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

#include "hexengine/engine/win32_engine_factory.hpp"
#include "hexengine/lua/lua_runtime.hpp"

namespace {

using hexengine::engine::EngineSession;
using hexengine::engine::Win32EngineFactory;
using hexengine::lua::LuaResult;
using hexengine::lua::LuaValue;
using hexengine::lua::LuaRuntime;

thread_local std::string g_lastError;

struct RuntimeState {
    std::unique_ptr<EngineSession> session;
    std::unique_ptr<LuaRuntime> lua;
};

void setLastError(std::string message) {
    g_lastError = std::move(message);
}

void clearLastError() {
    g_lastError.clear();
}

char* allocateCoTaskString(const std::string& value) {
    const auto size = value.size() + 1;
    auto* buffer = static_cast<char*>(::CoTaskMemAlloc(size));
    if (buffer == nullptr) {
        throw std::bad_alloc();
    }

    std::memcpy(buffer, value.c_str(), size);
    return buffer;
}

void appendJsonString(std::ostringstream& stream, std::string_view text) {
    stream << '"';
    for (const auto ch : text) {
        switch (ch) {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            stream << ch;
            break;
        }
    }
    stream << '"';
}

std::string toJson(const LuaResult& result) {
    std::ostringstream stream;
    stream << "{\"values\":[";
    for (std::size_t index = 0; index < result.values.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }

        const auto& value = result.values[index];
        if (std::holds_alternative<std::monostate>(value)) {
            stream << "null";
        } else if (const auto* boolean = std::get_if<bool>(&value)) {
            stream << (*boolean ? "true" : "false");
        } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            stream << *integer;
        } else if (const auto* number = std::get_if<double>(&value)) {
            stream << *number;
        } else if (const auto* text = std::get_if<std::string>(&value)) {
            appendJsonString(stream, *text);
        }
    }
    stream << "]}";
    return stream.str();
}

RuntimeState& requireRuntime(HexEngineRuntimeHandle runtime) {
    if (runtime == nullptr) {
        throw std::invalid_argument("runtime handle is null");
    }

    return *static_cast<RuntimeState*>(runtime);
}

int runWithJson(
    HexEngineRuntimeHandle runtime,
    char** outJson,
    const std::function<LuaResult(RuntimeState&)>& callback) {
    if (outJson == nullptr) {
        setLastError("out_json must not be null");
        return 0;
    }

    *outJson = nullptr;

    try {
        clearLastError();
        auto& state = requireRuntime(runtime);
        const auto result = callback(state);
        const auto json = toJson(result);
        *outJson = allocateCoTaskString(json);
        return 1;
    } catch (const std::exception& exception) {
        setLastError(exception.what());
        return 0;
    }
}

}  // namespace

extern "C" {

uint32_t hexengine_host_get_current_process_id(void) {
    return ::GetCurrentProcessId();
}

HexEngineRuntimeHandle hexengine_host_create_runtime_for_pid(uint32_t pid) {
    try {
        clearLastError();
        Win32EngineFactory factory;
        auto state = std::make_unique<RuntimeState>();
        state->session = factory.open(pid);
        state->lua = std::make_unique<LuaRuntime>(*state->session);
        return state.release();
    } catch (const std::exception& exception) {
        setLastError(exception.what());
        return nullptr;
    }
}

void hexengine_host_destroy_runtime(HexEngineRuntimeHandle runtime) {
    auto* state = static_cast<RuntimeState*>(runtime);
    delete state;
}

int hexengine_host_run_global(
    HexEngineRuntimeHandle runtime,
    const char* source,
    const char* chunkName,
    char** outJson) {
    return runWithJson(runtime, outJson, [&](RuntimeState& state) {
        return state.lua->runGlobal(source != nullptr ? source : "", chunkName != nullptr ? chunkName : "global");
    });
}

int hexengine_host_run_script(
    HexEngineRuntimeHandle runtime,
    const char* scriptId,
    const char* source,
    const char* chunkName,
    char** outJson) {
    if (scriptId == nullptr || *scriptId == '\0') {
        setLastError("script_id must not be empty");
        return 0;
    }

    return runWithJson(runtime, outJson, [&](RuntimeState& state) {
        return state.lua->runScript(
            scriptId,
            source != nullptr ? source : "",
            chunkName != nullptr ? chunkName : "script");
    });
}

int hexengine_host_destroy_script(
    HexEngineRuntimeHandle runtime,
    const char* scriptId) {
    try {
        clearLastError();
        if (scriptId == nullptr || *scriptId == '\0') {
            throw std::invalid_argument("script_id must not be empty");
        }

        auto& state = requireRuntime(runtime);
        return state.lua->destroyScript(scriptId) ? 1 : 0;
    } catch (const std::exception& exception) {
        setLastError(exception.what());
        return 0;
    }
}

const char* hexengine_host_get_last_error(void) {
    return g_lastError.empty() ? nullptr : g_lastError.c_str();
}

void hexengine_host_free_string(char* value) {
    if (value != nullptr) {
        ::CoTaskMemFree(value);
    }
}

}  // extern "C"
