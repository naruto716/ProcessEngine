#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
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

        expect(script.dealloc("newmem1"), "First multi-target alloc should be deallocatable");
        expect(script.dealloc("newmem2"), "Second multi-target alloc should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.multi"), "Multi-target script context destroy should succeed");
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
        expect(script.dealloc("newmem1"), "Internal-label test alloc should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.internal"), "Internal-label script context destroy should succeed");
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
        expect(script.dealloc("newmem"), "Directive-driven local alloc should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.directives"), "Directive-driven script context destroy should succeed");
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
        expect(script.dealloc("newmem"), "Published alloc should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.publish"), "Published alloc script context destroy should succeed");
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

        expect(script.dealloc("newmem"), "Hook cave should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.hook"), "Hook script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);

        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.hook.explicit.label");
        AssemblyScript program(script);

        const auto hookAddress = session.resolveAddress("game.exe+0x240");
        const auto result = program.execute(R"(
alloc(newmem, 0x100, game.exe+0x240)
label(returnhere)

newmem:
  nop
  jmp returnhere

game.exe+0x240:
  jmp newmem
  nop
  nop
returnhere:
)");

        expect(result.segments.size() == 2, "Explicit cross-chunk label hook should emit a cave segment and a patch segment");

        const auto newmem = script.findLocalAllocation("newmem");
        expect(newmem.has_value(), "Explicit-label hook should create the cave allocation");
        const auto returnhere = script.findLabel("returnhere");
        expect(returnhere.has_value(), "Explicit label should be committed into script scope after assembly");
        expect(*returnhere == hookAddress + result.segments[1].emittedBytes, "Explicit label should resolve to the end of the hook-site chunk");

        const auto caveBytes = fake->read(newmem->address, result.segments[0].emittedBytes);
        const auto caveJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            newmem->address + 1,
            std::span<const std::byte>(caveBytes).subspan(1));
        expect(caveJumpTarget.has_value(), "Explicit-label cave should contain a decodable jump");
        expect(*caveJumpTarget == *returnhere, "Explicit cross-chunk label should drive the cave jump target");

        const auto hookBytes = fake->read(hookAddress, result.segments[1].emittedBytes);
        const auto hookJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            hookAddress,
            std::span<const std::byte>(hookBytes));
        expect(hookJumpTarget.has_value(), "Explicit-label hook site should contain a decodable jump");
        expect(*hookJumpTarget == newmem->address, "Explicit-label hook site should still jump into the cave");

        expect(script.dealloc("newmem"), "Explicit-label hook cave should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.hook.explicit.label"), "Explicit-label hook script context destroy should succeed");
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
        expect(script.dealloc("newmem"), "Failed hook cave alloc should still be deallocatable");
        expect(session.destroyScriptContext("assembly.script.hook.unbound.return"), "Failed hook script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.hook.implicit.return");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(newmem, 0x100, game.exe+0x280)

newmem:
  jmp returnhere

game.exe+0x280:
  jmp newmem
  nop
  nop
returnhere:
)");
            },
            "returnhere");
        expect(script.dealloc("newmem"), "Implicit-return hook cave should still be deallocatable after failure");
        expect(session.destroyScriptContext("assembly.script.hook.implicit.return"), "Implicit-return hook script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.full_access");

        const auto site = script.alloc({
            .name = "site",
            .size = 0x40,
            .protection = hexengine::core::ProtectionFlags::Read,
        });
        expect(!session.process().query(site.address)->isWritable(), "Test site should start read-only");

        AssemblyScript program(script);
        const auto result = program.execute(R"(
fullAccess(site, 0x40)
site:
  ret
)");

        expect(result.segments.size() == 1, "fullAccess script should still assemble the target segment");
        const auto region = session.process().query(site.address);
        expect(region.has_value(), "fullAccess target region should remain queryable");
        expect(region->isWritable(), "fullAccess should make the target range writable");
        expect(region->isExecutable(), "fullAccess should make the target range executable");

        expect(script.dealloc("site"), "fullAccess test alloc should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.full_access"), "fullAccess script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        const Address moduleBase = 0x1400'0000ull;
        const Address hookAddress = moduleBase + 0x220;
        fake->addModule("game.exe", moduleBase, 0x1000);
        fake->storeBytes(
            hookAddress,
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
        auto& script = session.createScriptContext("assembly.script.aob.hook");
        script.declareLabel("returnhere");
        script.bindLabel("returnhere", hookAddress + 5);

        AssemblyScript program(script);
        const auto result = program.execute(R"(
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)

newmem:
  nop
  jmp returnhere

injection:
  jmp newmem
)");

        expect(result.segments.size() == 2, "AOB-driven hook script should emit cave and hook-site segments");
        const auto injectionAddress = script.findLabel("injection");
        expect(injectionAddress.has_value(), "aobScanModule should bind the target name into script scope");
        expect(*injectionAddress == hookAddress, "aobScanModule should bind the first match address");

        const auto newmem = script.findLocalAllocation("newmem");
        expect(newmem.has_value(), "alloc(newmem, ..., injection) should create the hook cave");
        const auto caveBytes = fake->read(newmem->address, result.segments[0].emittedBytes);
        expect(caveBytes[0] == std::byte{0x90}, "AOB-driven hook cave should begin with NOP");

        const auto patchedHookBytes = fake->read(hookAddress, result.segments[1].emittedBytes);
        const auto hookJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            hookAddress,
            std::span<const std::byte>(patchedHookBytes));
        expect(hookJumpTarget.has_value(), "AOB-driven hook site should contain a decodable jump");
        expect(*hookJumpTarget == newmem->address, "AOB-driven hook site should jump into the cave");

        expect(script.dealloc("newmem"), "AOB-driven hook cave should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.aob.hook"), "AOB-driven hook script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        const Address moduleBase = 0x1400'0000ull;
        const Address hookAddress = moduleBase + 0x2A0;
        fake->addModule("game.exe", moduleBase, 0x1000);
        fake->storeBytes(
            hookAddress,
            std::array{
                std::byte{0x10},
                std::byte{0x20},
                std::byte{0x30},
                std::byte{0x40},
                std::byte{0x50},
                std::byte{0x60},
                std::byte{0x70},
                std::byte{0x80},
            });

        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.aob.hook.explicit.label");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
aobScanModule(injection, game.exe, 10 20 30 40 50 60 70 80)
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
)");

        expect(result.segments.size() == 2, "AOB-driven explicit-label hook should emit cave and hook-site segments");
        const auto returnhere = script.findLabel("returnhere");
        expect(returnhere.has_value(), "AOB-driven explicit label should bind into script scope");
        expect(*returnhere == hookAddress + result.segments[1].emittedBytes, "AOB-driven explicit label should resolve to the post-hook address");

        const auto newmem = script.findLocalAllocation("newmem");
        expect(newmem.has_value(), "AOB-driven explicit-label hook should allocate the cave");
        const auto caveBytes = fake->read(newmem->address, result.segments[0].emittedBytes);
        const auto caveJumpTarget = hexengine::tests::support::decodeRelativeControlTarget(
            newmem->address + 1,
            std::span<const std::byte>(caveBytes).subspan(1));
        expect(caveJumpTarget.has_value(), "AOB-driven explicit-label cave should contain a decodable jump");
        expect(*caveJumpTarget == *returnhere, "AOB-driven explicit label should drive the cave jump target");

        expect(script.dealloc("newmem"), "AOB-driven explicit-label hook cave should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.aob.hook.explicit.label"), "AOB-driven explicit-label hook context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->addModule("game.exe", 0x1400'0000ull);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.explicit.label.missing");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
alloc(newmem, 0x100, game.exe+0x2C0)
label(returnhere)

newmem:
  jmp returnhere

game.exe+0x2C0:
  jmp newmem
)");
            },
            "script label is not bound: returnhere");
        expect(script.dealloc("newmem"), "Missing explicit-label hook cave should still be deallocatable after failure");
        expect(session.destroyScriptContext("assembly.script.explicit.label.missing"), "Missing explicit-label hook context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        fake->setExecuteCodeHook([&](Address entryAddress) {
            const auto bytes = fake->read(entryAddress, 1);
            if (bytes.empty() || bytes[0] != std::byte{0xC3}) {
                fail("createThread should flush the assembled code before executeCode runs");
            }
        });

        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.create_thread");
        AssemblyScript program(script);

        const auto result = program.execute(R"(
alloc(worker, 0x40)
worker:
  ret
createThread(worker)
)");

        expect(result.segments.size() == 1, "createThread script should only need the worker segment");
        const auto worker = script.findLocalAllocation("worker");
        expect(worker.has_value(), "createThread script should allocate the worker code block");
        expect(fake->executedCodeAddresses().size() == 1, "createThread should call executeCode exactly once");
        expect(fake->executedCodeAddresses().front() == worker->address, "createThread should execute the resolved worker entrypoint");
        expect(script.dealloc("worker"), "createThread worker should be deallocatable");
        expect(session.destroyScriptContext("assembly.script.create_thread"), "createThread script context destroy should succeed");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("assembly.script.aob.missing");
        AssemblyScript program(script);

        expectThrows(
            [&] {
                (void)program.execute(R"(
aobScanModule(injection, game.exe, 41 42 13 37)
)");
            },
            "line 2");
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
