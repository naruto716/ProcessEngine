#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"
#include "hexengine/lua/lua_runtime.hpp"
#include "support/fake_process_backend.hpp"
#include "support/jump_decode.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::EngineSession;
using hexengine::lua::LuaResult;
using hexengine::lua::LuaRuntime;
using hexengine::lua::LuaValue;
using hexengine::tests::support::FakeProcessBackend;

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

bool isNil(const LuaValue& value) {
    return std::holds_alternative<std::monostate>(value);
}

std::int64_t asInteger(const LuaValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer;
    }

    fail("Expected an integer Lua value");
}

bool asBool(const LuaValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }

    fail("Expected a boolean Lua value");
}

std::string asString(const LuaValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }

    fail("Expected a string Lua value");
}

const LuaValue& onlyValue(const LuaResult& result) {
    expect(result.values.size() == 1, "Expected exactly one Lua return value");
    return result.values.front();
}

void runTests() {
    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);
        fake->storeBytes(
            0x1400'0200ull,
            std::array{
                std::byte{0x41},
                std::byte{0x42},
                std::byte{0x13},
                std::byte{0x37},
                std::byte{0xC0},
                std::byte{0xDE},
                std::byte{0x7A},
                std::byte{0xE1},
            });

        EngineSession session(std::move(backend));
        LuaRuntime lua(session);

        expect(asInteger(onlyValue(lua.runScript("alpha", "counter = (counter or 0) + 1; return counter"))) == 1, "Script globals should persist inside one script");
        expect(asInteger(onlyValue(lua.runScript("alpha", "return counter"))) == 1, "Script globals should still exist on the next run");
        expect(isNil(onlyValue(lua.runScript("beta", "return counter"))), "Different scripts should not share globals");

        const auto memory = session.createScriptContext("mem").alloc({
            .name = "buffer",
            .size = 0x40,
            .protection = hexengine::core::kReadWriteExecute,
        });

        const auto typedIo = lua.runScript("mem", R"(
writeWord("buffer", 0x1234)
writeQword("buffer+0x8", 0x1122334455667788)
return readWord("buffer"), readQword("buffer+0x8")
)");
        expect(typedIo.values.size() == 2, "Typed IO script should return two values");
        expect(asInteger(typedIo.values[0]) == 0x1234, "writeWord/readWord should round-trip a 16-bit value");
        expect(asInteger(typedIo.values[1]) == 0x1122334455667788ll, "writeQword/readQword should round-trip a 64-bit value");
        expect(lua.destroyScript("mem"), "destroyScript should tear down the typed-IO script context");

        const auto scanResult = lua.runGlobal(R"(
local hits = AOBScan("41 42 13 37 C0 DE 7A E1")
return hits.Count, hits[0]
)");
        expect(scanResult.values.size() == 2, "AOBScan should return the list count and first string result");
        expect(asInteger(scanResult.values[0]) == 1, "AOBScan should find the stored pattern exactly once");
        expect(asString(scanResult.values[1]) == "14000200", "AOBScan results should be exposed as CE-style hex strings");

        const auto hookResult = lua.runScript("hook", R"(
local ok, err = autoAssemble([[
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)
label(returnhere)

newmem:
  nop
  jmp returnhere

injection:
  jmp newmem
  nop
  nop
returnhere:
]])
if not ok then
  error(err)
end
return getAddress("newmem"), getAddress("returnhere"), getAddress("injection")
)");
        expect(hookResult.values.size() == 3, "Lua autoAssemble hook should return the cave, returnhere, and injection addresses");
        const auto newmem = static_cast<Address>(asInteger(hookResult.values[0]));
        const auto returnhere = static_cast<Address>(asInteger(hookResult.values[1]));
        const auto injection = static_cast<Address>(asInteger(hookResult.values[2]));
        expect(injection == 0x1400'0200ull, "AOB-driven injection label should resolve to the matched hook site");
        expect(returnhere > injection, "Explicit returnhere label should bind after the patch chunk");

        const auto caveBytes = fake->read(newmem, 6);
        expect(caveBytes[0] == std::byte{0x90}, "Hook cave should preserve the manual NOP payload");
        const auto caveJump = hexengine::tests::support::decodeRelativeControlTarget(
            newmem + 1,
            std::span<const std::byte>(caveBytes).subspan(1));
        expect(caveJump.has_value() && *caveJump == returnhere, "Hook cave should jump to the explicit returnhere label");

        const auto hookBytes = fake->read(injection, 5);
        const auto hookJump = hexengine::tests::support::decodeRelativeControlTarget(injection, hookBytes);
        expect(hookJump.has_value() && *hookJump == newmem, "Hook site should jump into the cave");

        const auto repeatingSetup = lua.runScript("timers", R"(
count = 0
timer = createTimer(false)
timer.Interval = 20
timer.OnTimer = function(sender)
  count = count + 1
  if count >= 2 then
    sender.Enabled = false
  end
end
timer.Enabled = true
return count
)");
        expect(asInteger(onlyValue(repeatingSetup)) == 0, "Timer setup should begin with count at zero");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        expect(asInteger(onlyValue(lua.runScript("timers", "return count"))) >= 2, "Repeating timer should fire in the owning script env");

        const auto oneshotSetup = lua.runScript("oneshot", R"(
fired = false
createTimer(15, function()
  fired = true
end)
return fired
)");
        expect(!asBool(onlyValue(oneshotSetup)), "One-shot helper should not fire immediately");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        expect(asBool(onlyValue(lua.runScript("oneshot", "return fired"))), "One-shot timer helper should run its callback");

        const auto cleanupSetup = lua.runScript("cleanup", R"(
count = 0
t = createTimer(false)
t.Interval = 10
t.OnTimer = function()
  count = count + 1
end
t.Enabled = true
return true
)");
        expect(asBool(onlyValue(cleanupSetup)), "Cleanup timer should be created successfully");
        expect(lua.destroyScript("cleanup"), "destroyScript should remove a live script environment");
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        expect(isNil(onlyValue(lua.runScript("cleanup", "return count"))), "Destroyed scripts should lose their globals and timers");
    }
}

}  // namespace

int main() {
    try {
        runTests();
        std::cout << "ce_lua_runtime_test: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_lua_runtime_test failed: " << exception.what() << '\n';
        return 1;
    }
}
