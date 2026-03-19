#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "hexengine/host/host_api.h"

namespace {

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

std::string currentLastError() {
    const auto* error = hexengine_host_get_last_error();
    return error != nullptr ? std::string(error) : std::string();
}

std::string runGlobal(HexEngineRuntimeHandle runtime, const char* source) {
    char* json = nullptr;
    const auto ok = hexengine_host_run_global(runtime, source, "host_api_test.global", &json);
    if (ok == 0) {
        fail("hexengine_host_run_global failed: " + currentLastError());
    }

    const std::string result = json != nullptr ? std::string(json) : std::string();
    hexengine_host_free_string(json);
    return result;
}

std::string runScript(HexEngineRuntimeHandle runtime, const char* scriptId, const char* source) {
    char* json = nullptr;
    const auto ok = hexengine_host_run_script(runtime, scriptId, source, "host_api_test.script", &json);
    if (ok == 0) {
        fail("hexengine_host_run_script failed: " + currentLastError());
    }

    const std::string result = json != nullptr ? std::string(json) : std::string();
    hexengine_host_free_string(json);
    return result;
}

void runTests() {
    const auto pid = hexengine_host_get_current_process_id();
    expect(pid != 0, "Current process id should be non-zero");

    auto runtime = hexengine_host_create_runtime_for_pid(pid);
    expect(runtime != nullptr, "Runtime should open the current process");

    try {
        const auto global = runGlobal(runtime, "return getOpenedProcessID(), targetIs64Bit()");
        expect(
            global.find("\"values\":[") != std::string::npos,
            "Global execution should return a JSON values array");
        expect(
            global.find(std::to_string(pid)) != std::string::npos,
            "Global execution should report the attached process id");

        const auto first = runScript(runtime, "host.api.counter", "counter = (counter or 0) + 1; return counter");
        const auto second = runScript(runtime, "host.api.counter", "return counter");
        expect(first == "{\"values\":[1]}", "First script run should initialize the script-scoped counter");
        expect(second == "{\"values\":[1]}", "Second script run should reuse the same script environment");

        expect(hexengine_host_destroy_script(runtime, "host.api.counter") == 1, "Destroying an existing script should succeed");
        const auto afterDestroy = runScript(runtime, "host.api.counter", "return counter");
        expect(afterDestroy == "{\"values\":[null]}", "Destroyed scripts should lose their script globals");
    } catch (...) {
        hexengine_host_destroy_runtime(runtime);
        throw;
    }

    hexengine_host_destroy_runtime(runtime);
}

}  // namespace

int main() {
    try {
        runTests();
        std::cout << "ce_host_api_test: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_host_api_test failed: " << exception.what() << '\n';
        return 1;
    }
}
