#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "hexengine/engine/assembly_script.hpp"
#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"
#include "support/fake_process_backend.hpp"
#include "support/jump_decode.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::AssemblyScript;
using hexengine::engine::EngineSession;
using hexengine::engine::SymbolKind;
using hexengine::tests::support::FakeProcessBackend;

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Callback>
void expectThrows(Callback&& callback, std::string_view messagePart) {
    try {
        callback();
    } catch (const std::exception& exception) {
        if (std::string_view(exception.what()).find(messagePart) != std::string_view::npos) {
            return;
        }

        fail("Exception message did not contain the expected text");
    }

    fail("Expected the operation to throw");
}

void runTests() {
    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x0110'0000ull);

        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.multi");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
alloc(newmem1, 0x100)
alloc(newmem2, 0x100)

newmem1:
  ret

loopbegin:
  jmp loopbegin

newmem2:
  ret

game.exe+0x100:
  jmp newmem2
)");

        expect(result.segments.size() == 3, "Known alloc names and resolvable expressions should split the script into segments");
        expect(result.segments[0].targetExpression == "newmem1", "The first segment should target newmem1");
        expect(result.segments[1].targetExpression == "newmem2", "The second segment should target newmem2");
        expect(result.segments[2].targetExpression == "game.exe+0x100", "The third segment should target the raw patch site");

        const auto newmem1 = script.findLocalAllocation("newmem1");
        const auto newmem2 = script.findLocalAllocation("newmem2");
        expect(newmem1.has_value(), "alloc(newmem1, ...) should create a local allocation");
        expect(newmem2.has_value(), "alloc(newmem2, ...) should create a local allocation");
        expect(result.segments[0].address == newmem1->address, "The first segment should assemble at newmem1");
        expect(result.segments[1].address == newmem2->address, "The second segment should assemble at newmem2");
        expect(result.segments[2].address == 0x0110'0100ull, "The raw patch segment should resolve against the module base");
        expect(result.segments[0].capacity == newmem1->size, "Alloc-backed segments should retain their capacity limit");
        expect(!result.segments[2].capacity.has_value(), "Raw patch targets should not invent a fake capacity limit");

        const auto caveBytes = fake->read(newmem1->address, result.segments[0].emittedBytes);
        expect(caveBytes[0] == std::byte{0xC3}, "The first segment should begin with RET");
        expect(result.segments[0].emittedBytes >= 3, "RET plus the internal loop should emit multiple bytes");
        expect(!script.hasLabel("loopbegin"), "Implicit asm labels should stay private to their segment");
        expectThrows(
            [&] { (void)script.resolveAddress("loopbegin"); },
            "unknown symbol or module");

        const auto newmem2Bytes = fake->read(newmem2->address, result.segments[1].emittedBytes);
        expect(newmem2Bytes.size() == 1, "The second segment should emit exactly one RET byte");
        expect(newmem2Bytes[0] == std::byte{0xC3}, "The second segment should write RET");

        const auto patchBytes = fake->read(result.segments[2].address, result.segments[2].emittedBytes);
        expect(result.segments[2].emittedBytes >= 2, "The patch site should contain a jump to newmem2");
        expect(
            patchBytes[0] == std::byte{0xE9} || patchBytes[0] == std::byte{0xEB},
            "jmp newmem2 should encode as a jump opcode");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.internal");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
alloc(newmem1, 0x100)

newmem1:
  ret

newmem2:
  ret
)");

        expect(result.segments.size() == 1, "A brand-new label inside a segment should remain an internal asm label");
        expect(!script.hasLabel("newmem2"), "Internal asm labels should not leak into script scope");
        expect(result.segments[0].emittedBytes == 2, "Both RET instructions should land in the same segment");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.directives");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
alloc(newmem)
newmem: ret
registerSymbol(newmem)
unregisterSymbol(newmem)

globalAlloc(sharedmem, 0x40)
sharedmem:
  ret
registerSymbol(sharedmem)
dealloc(sharedmem)
)");

        expect(result.segments.size() == 2, "Directive-driven script should still emit both segments");
        expect(result.segments[0].capacity.has_value() && *result.segments[0].capacity == 0x1000, "alloc(name) should use the default size");
        expect(!session.resolveSymbol("newmem").has_value(), "unregisterSymbol(newmem) should remove the published symbol");
        expect(!session.resolveSymbol("sharedmem").has_value(), "dealloc(sharedmem) should remove linked global symbols");
        expect(!session.findGlobalAllocation("sharedmem").has_value(), "dealloc(sharedmem) should free the global allocation");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.publish");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
alloc(newmem, 0x40)
newmem:
  ret
registerSymbol(newmem)
)");

        expect(result.segments.size() == 1, "Single-target script should still emit one segment");
        const auto published = session.resolveSymbol("newmem");
        expect(published.has_value(), "registerSymbol(newmem) should publish the alloc-backed name");
        expect(published->kind == SymbolKind::Allocation, "registerSymbol(newmem) should preserve allocation kind");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);

        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.hook");
        AssemblyScript program(script);

        const auto hookAddress = session.resolveAddress("game.exe+0x200");
        script.declareLabel("returnhere");
        script.bindLabel("returnhere", hookAddress + 5);

        const auto result = program.execute(R"(
alloc(newmem, 0x100, game.exe+0x200)
registerSymbol(newmem)

newmem:
  nop
  jmp returnhere

game.exe+0x200:
  jmp newmem
)");

        expect(result.segments.size() == 2, "Manual hook script should emit a cave segment and a patch segment");

        const auto newmem = script.findLocalAllocation("newmem");
        expect(newmem.has_value(), "Hook script should create the cave allocation");
        const auto distance = hookAddress >= newmem->address ? hookAddress - newmem->address : newmem->address - hookAddress;
        expect(distance <= 0x8000'0000ull, "Hook cave should be allocated within rel32 range");

        const auto published = session.resolveSymbol("newmem");
        expect(published.has_value(), "registerSymbol(newmem) should publish the cave name");
        expect(published->address == newmem->address, "Published hook cave symbol should point at the cave");

        const auto caveBytes = fake->read(newmem->address, result.segments[0].emittedBytes);
        expect(caveBytes[0] == std::byte{0x90}, "Hook cave should begin with the manual preserved instruction");
        const auto caveJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            newmem->address + 1,
            std::span<const std::byte>(caveBytes).subspan(1));
        expect(caveJumpTarget.has_value(), "jmp returnhere should decode as a relative control transfer");
        expect(*caveJumpTarget == hookAddress + 5, "Cave jump should return to the post-hook address");

        const auto hookBytes = fake->read(hookAddress, result.segments[1].emittedBytes);
        const auto hookJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            hookAddress,
            std::span<const std::byte>(hookBytes));
        expect(hookJumpTarget.has_value(), "Hook site should contain a decodable jump");
        expect(*hookJumpTarget == newmem->address, "Hook site jump should land at the cave allocation");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.hook.unbound.return");
        AssemblyScript program(script);

        script.declareLabel("returnhere");
        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(newmem, 0x100, game.exe+0x200)
newmem:
  jmp returnhere
)");
            },
            "script label is not bound");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.noaddr");
        AssemblyScript program(script);

        expectThrows(
            [&] { (void)program.execute("ret\n"); },
            "assembly instruction has no current address");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.label.before.target");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
loop:
  ret
)");
            },
            "internal asm label 'loop' cannot appear before any current address");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.bad.target");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
missing+0x10:
  ret
)");
            },
            "segment target 'missing+0x10' did not resolve");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.bad.syntax");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(cave, 0x40)
cave:
  mov eax, [
)");
            },
            "line 4");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.unresolved.label");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(cave, 0x40)
cave:
  jmp missing
)");
            },
            "flush failed for target 'cave'");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.implicit.label.publish");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(cave, 0x40)
cave:
internal:
  ret
registerSymbol(internal)
)");
            },
            "unknown symbol or module");
    }
}

}  // namespace

int main() {
    try {
        runTests();
        std::cout << "ce_assembly_script_test: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_assembly_script_test failed: " << exception.what() << '\n';
        return 1;
    }
}
