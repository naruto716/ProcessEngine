#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <asmjit/x86.h>

#include "hexengine/engine/remote_assembler.hpp"
#include "support/fake_process_backend.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::RemoteAssembler;
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
        const Address caveBase = 0x7FF0'0000'0000ull;

        RemoteAssembler ra(*backend, caveBase, 256);

        expect(ra.baseAddress() == caveBase, "baseAddress should match construction argument");
        expect(ra.offset() == 0, "Fresh assembler should have zero offset");
        expect(ra.currentAddress() == caveBase, "currentAddress should equal baseAddress initially");
        expect(ra.capacity() == 256, "capacity should match construction argument");

        auto& a = ra.assembler();
        a.nop();
        a.nop();
        a.ret();

        expect(ra.offset() == 3, "Should have emitted 3 bytes");
        expect(ra.currentAddress() == caveBase + 3, "currentAddress should advance");

        const auto written = ra.flush();
        expect(written == 3, "flush() should report 3 bytes written");

        const auto bytes = backend->read(caveBase, 3);
        expect(bytes[0] == std::byte{0x90}, "First byte should be NOP (0x90)");
        expect(bytes[1] == std::byte{0x90}, "Second byte should be NOP (0x90)");
        expect(bytes[2] == std::byte{0xC3}, "Third byte should be RET (0xC3)");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        const Address caveBase = 0x1000;

        RemoteAssembler ra(*backend, caveBase, 256);
        auto& a = ra.assembler();

        a.nop();
        expect(ra.flush() == 1, "First flush should write 1 byte");
        expect(ra.flush() == 0, "Flush with no new bytes should write 0");

        a.nop();
        a.ret();
        expect(ra.flush() == 2, "Second content flush should write 2 new bytes");

        const auto bytes = backend->read(caveBase, 3);
        expect(bytes[0] == std::byte{0x90}, "Byte 0 should be NOP");
        expect(bytes[1] == std::byte{0x90}, "Byte 1 should be NOP");
        expect(bytes[2] == std::byte{0xC3}, "Byte 2 should be RET");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        const Address caveBase = 0x2000;

        RemoteAssembler ra(*backend, caveBase, 256);
        auto& a = ra.assembler();

        a.push(asmjit::x86::rbp);
        a.mov(asmjit::x86::rbp, asmjit::x86::rsp);
        a.pop(asmjit::x86::rbp);
        a.ret();

        ra.flush();

        const auto bytes = backend->read(caveBase, ra.offset());
        expect(bytes.size() >= 5, "push rbp + mov rbp,rsp + pop rbp + ret should be at least 5 bytes");
        expect(bytes[0] == std::byte{0x55}, "push rbp should encode as 0x55");

        const auto popIndex = ra.offset() - 2;
        expect(bytes[popIndex] == std::byte{0x5D}, "pop rbp should encode as 0x5D");
        expect(bytes.back() == std::byte{0xC3}, "Last byte should be RET (0xC3)");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(4);
        const Address caveBase = 0x0040'0000;

        RemoteAssembler ra(*backend, caveBase, 64);
        auto& a = ra.assembler();

        a.push(asmjit::x86::ebp);
        a.mov(asmjit::x86::ebp, asmjit::x86::esp);
        a.pop(asmjit::x86::ebp);
        a.ret();

        ra.flush();

        const auto bytes = backend->read(caveBase, ra.offset());
        expect(bytes[0] == std::byte{0x55}, "push ebp should encode as 0x55 in 32-bit mode");
        expect(bytes.back() == std::byte{0xC3}, "Last byte should be RET (0xC3) in 32-bit mode");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        const Address caveBase = 0x3000;

        RemoteAssembler ra(*backend, caveBase, 2);
        auto& a = ra.assembler();

        a.nop();
        a.nop();
        a.nop(); // 3 bytes - exceeds 2-byte cave

        expectThrows(
            [&] { ra.flush(); },
            "code cave");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        expectThrows(
            [&] { RemoteAssembler(*backend, 0x4000, 0); },
            "cave size");
    }

    {
        auto backend = std::make_unique<FakeProcessBackend>(8);
        const Address caveBase = 0x7FF0'0001'0000ull;
        const Address jumpTarget = 0x7FF0'0002'0000ull;

        RemoteAssembler ra(*backend, caveBase, 256);
        auto& a = ra.assembler();

        a.mov(asmjit::x86::rax, jumpTarget);
        a.jmp(asmjit::x86::rax);

        const auto written = ra.flush();
        expect(written > 0, "Should emit bytes for mov+jmp");

        const auto bytes = backend->read(caveBase, ra.offset());
        expect(bytes.size() >= 12, "mov rax,imm64 + jmp rax should be at least 12 bytes");
        expect(bytes[0] == std::byte{0x48}, "REX.W prefix for mov rax,imm64");
        expect(bytes[1] == std::byte{0xB8}, "opcode for mov rax,imm64");
    }
}

}  // namespace

int main() {
    try {
        runTests();
        std::cout << "ce_remote_assembler_test: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_remote_assembler_test failed: " << exception.what() << '\n';
        return 1;
    }
}
