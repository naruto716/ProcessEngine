#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"
#include "hexengine/engine/text_assembler.hpp"
#include "support/fake_process_backend.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::AllocationRequest;
using hexengine::engine::EngineSession;
using hexengine::engine::TextAssembler;
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
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("labels.success");

        const auto cave = script.alloc(AllocationRequest{
            .name = "newmem",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);

        assembler.append(R"(
newmem:
  nop
  je returnhere
  ret
returnhere:
  ret
)");

        expect(script.hasLabel("newmem"), "Local alloc should already exist in the script label map before flush");
        expect(script.findLabel("newmem").value() == cave.address, "Local alloc label should already point at the cave base");
        expect(!script.hasLabel("returnhere"), "Forward-only asm labels should not exist before flush");
        const auto written = assembler.flush();
        expect(written > 0, "Valid text assembly should emit bytes");
        expect(script.hasLabel("newmem"), "Local alloc label should remain after assembly");
        expect(!script.hasLabel("returnhere"), "Asm-only labels should stay private to the assembler pass");
        expect(script.findLabel("newmem").value() == cave.address, "The base label should bind to the cave base");
        expectThrows(
            [&] { (void)session.resolveAddress("returnhere"); },
            "unknown symbol or module");
        expectThrows(
            [&] { (void)script.resolveAddress("returnhere"); },
            "unknown symbol or module");

        const auto bytes = fake->read(cave.address, assembler.offset());
        expect(bytes[0] == std::byte{0x90}, "First emitted byte should be NOP");
        expect(bytes.back() == std::byte{0xC3}, "Last emitted byte should be RET");

        expect(script.dealloc("newmem"), "The local cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        auto* fake = backend.get();
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("external.names");

        const auto helper = script.alloc(AllocationRequest{
            .name = "helper",
            .size = 0x80,
        });
        const auto cave = script.alloc(AllocationRequest{
            .name = "entry",
            .size = 0x100,
        });
        (void)session.registerSymbol("global_target", 0x1400'1234'5678ull);

        TextAssembler assembler(script, cave);
        assembler.append(R"(
  mov rax, helper
  call global_target
  movss xmm0, dword ptr [rax+30]
  vaddps ymm1, ymm2, ymm3
  fld dword ptr [rax]
  ret
)");

        const auto written = assembler.flush();
        expect(written > 0, "Text assembly should resolve external script and session names");
        expect(script.hasLabel("helper"), "Local alloc names should already be represented as script labels");
        expect(script.findLabel("helper").value() == helper.address, "Local alloc label should remain stable across assembly");
        expect(!script.hasLabel("global_target"), "Resolving a session symbol should not synthesize a parser label");

        const auto bytes = fake->read(cave.address, assembler.offset());
        expect(bytes.size() > 10, "Complex text assembly should emit a non-trivial byte stream");
        expect(bytes[0] == std::byte{0x48}, "mov rax, helper should start with a 64-bit REX prefix");
        expect(
            bytes[1] == std::byte{0xB8} || bytes[1] == std::byte{0xC7},
            "mov rax, helper should encode as either imm64 or sign-extended imm32");
        expect(helper.address != 0, "helper allocation should have a valid address");

        expect(script.dealloc("entry"), "The assembly cave should be deallocatable");
        expect(script.dealloc("helper"), "The referenced local allocation should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("incremental");

        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        assembler.append("start:\n  nop\n");
        expect(assembler.flush() == 1, "First flush should write the first parsed byte");
        expect(!script.hasLabel("start"), "Asm-only labels should not be committed after flush");

        assembler.append("tail:\n  ret\n");
        expect(assembler.flush() == 1, "Second flush should write only the newly emitted byte");
        expect(!script.hasLabel("tail"), "Later assembler-local labels should stay private");

        expect(script.dealloc("cave"), "The incremental assembly cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("invalid.syntax");

        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        expectThrows(
            [&] { assembler.append("mov eax, ["); },
            "parse failed");

        expect(script.dealloc("cave"), "The invalid-syntax cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("unresolved.parser.label");

        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        assembler.append("jmp missing_label\nret\n");
        expectThrows(
            [&] { (void)assembler.flush(); },
            "unresolved label(s): missing_label");

        expect(script.dealloc("cave"), "The unresolved-label cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("unresolved.script.label");

        script.declareLabel("shared_label");
        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        expectThrows(
            [&] { assembler.append("jmp shared_label\n"); },
            "script label is not bound");

        expect(script.dealloc("cave"), "The unbound-script-label cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("duplicate.label");

        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        expectThrows(
            [&] {
                assembler.append(R"(
dup:
  nop
dup:
  ret
)");
            },
            "parse failed");

        expect(script.dealloc("cave"), "The duplicate-label cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("asm.labels.private");

        const auto cave = script.alloc(AllocationRequest{
            .name = "cave",
            .size = 0x100,
        });

        TextAssembler assembler(script, cave);
        assembler.append(R"(
first:
  nop
second:
  ret
)");
        expect(assembler.flush() == 2, "Two one-byte instructions should flush two bytes");
        expectThrows(
            [&] { (void)session.resolveAddress("first"); },
            "unknown symbol or module");
        expectThrows(
            [&] { (void)session.resolveAddress("second"); },
            "unknown symbol or module");
        expectThrows(
            [&] { (void)script.resolveAddress("first"); },
            "unknown symbol or module");
        expectThrows(
            [&] { (void)script.resolveAddress("second"); },
            "unknown symbol or module");

        expect(script.dealloc("cave"), "The private-label cave should be deallocatable");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        EngineSession session(std::move(backend));
        auto& script = session.createScriptContext("asm.labels.reusable");

        const auto firstCave = script.alloc(AllocationRequest{
            .name = "cave_one",
            .size = 0x100,
        });
        TextAssembler first(script, firstCave);
        first.append("loop:\n  nop\n  jmp loop\n");
        expect(first.flush() > 0, "First assembly pass should flush successfully");
        expect(script.dealloc("cave_one"), "First cave should be deallocatable");

        const auto secondCave = script.alloc(AllocationRequest{
            .name = "cave_two",
            .size = 0x100,
        });
        TextAssembler second(script, secondCave);
        second.append("loop:\n  nop\n  jmp loop\n");
        expect(second.flush() > 0, "Reusing the same asm label name in a later pass should still work");
        expect(!script.hasLabel("loop"), "Reused asm-local labels should not leak into the script context");
        expect(script.dealloc("cave_two"), "Second cave should be deallocatable");
    }
}

}  // namespace

int main() {
    try {
        runTests();
        std::cout << "ce_text_assembler_test: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_text_assembler_test failed: " << exception.what() << '\n';
        return 1;
    }
}
