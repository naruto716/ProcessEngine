#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "hexengine/engine/address_resolver.hpp"
#include "hexengine/engine/symbol_repository.hpp"
#include "support/fake_process_backend.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::AddressResolver;
using hexengine::engine::SymbolKind;
using hexengine::engine::SymbolRecord;
using hexengine::engine::SymbolRepository;
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
        fail(
            std::string("Exception message did not contain the expected text. Expected fragment: \"")
            + std::string(messagePart)
            + "\". Actual: \""
            + exception.what()
            + '"');
    }

    fail("Expected the operation to throw");
}

[[nodiscard]] AddressResolver makeResolver(FakeProcessBackend& process, SymbolRepository& symbols) {
    return AddressResolver(process, symbols);
}

void registerSymbol(SymbolRepository& symbols, std::string_view name, Address address) {
    symbols.registerSymbol(SymbolRecord{
        .name = std::string(name),
        .address = address,
        .size = 0,
        .kind = SymbolKind::UserDefined,
        .persistent = true,
    });
}

void run64BitTests() {
    FakeProcessBackend process(8);
    process.addModule("game.exe", 0x140000000ull);
    process.addModule("game-test.exe", 0x180000000ull);
    process.addModule("mod-0x20.dll", 0x190000000ull);

    process.storeValue<std::uint64_t>(0x1000, 0x2000ull);
    process.storeValue<std::uint64_t>(0x2010, 0x3000ull);
    process.storeValue<std::uint64_t>(0x3038, 0x4444ull);
    process.storeValue<std::uint64_t>(0x140000500ull, 0x8000ull);
    process.storeValue<std::uint64_t>(0x8010, 0x9000ull);

    SymbolRepository symbols;
    registerSymbol(symbols, "player_base", 0x1000);
    registerSymbol(symbols, "fixture-pointer-root", 0x1000);
    registerSymbol(symbols, "fixture", 0x5555);
    registerSymbol(symbols, "1234", 0x9999);
    registerSymbol(symbols, "feature-0x10", 0x7777);
    registerSymbol(symbols, "game.exe", 0x2222);
    registerSymbol(symbols, "CAFEBABE", 0x1111);
    registerSymbol(symbols, "fixture-inner-root", 0x1000);

    const auto resolver = makeResolver(process, symbols);

    expect(resolver.resolve("1234") == 0x1234, "Bare numbers should resolve as hex");
    expect(resolver.resolve("0xABCD") == 0xABCD, "0x-prefixed numbers should resolve as hex");
    expect(resolver.resolve("player_base") == 0x1000, "Named symbol resolution failed");
    expect(resolver.resolve("game.exe") == 0x2222, "Symbol should win over a module with the same name");
    expect(resolver.resolve("game-test.exe") == 0x180000000ull, "Hyphenated module resolution failed");
    expect(resolver.resolve("mod-0x20.dll") == 0x190000000ull, "Module names containing -0xNN should resolve as whole tokens");
    expect(resolver.resolve("game-test.exe-0x20") == 0x17FFFFFE0ull, "Hyphenated module subtraction failed");
    expect(resolver.resolve("game-test.exe+0x1234-0x20") == 0x180001214ull, "Module arithmetic failed");
    expect(resolver.resolve("game.exe+0x1234-0x20") == 0x3436, "Symbol arithmetic should still win over module lookup");
    expect(resolver.resolve("  game-test.exe + 0x20 - 0x10 ") == 0x180000010ull, "Whitespace arithmetic failed");
    expect(resolver.resolve("[0x1000]") == 0x2000, "Single dereference failed");
    expect(resolver.resolve("[player_base]") == 0x2000, "Symbol-based dereference failed");
    expect(resolver.resolve("[[player_base]+0x10]+0x30") == 0x3030, "Nested pointer chain failed");
    expect(resolver.resolve("[ [[player_base]+0x10] + 0x38 ]") == 0x4444, "Nested pointer chain with spaces failed");
    expect(resolver.resolve("fixture-pointer-root") == 0x1000, "Hyphenated symbol resolution failed");
    expect(resolver.resolve("fixture-pointer-root-0x10") == 0x0FF0, "Hyphenated symbol subtraction failed");
    expect(resolver.resolve("feature-0x10") == 0x7777, "Whole-token symbol resolution should beat subtraction splitting");
    expect(resolver.resolve("feature-0x10-0x10") == 0x7767, "Symbol subtraction after a whole-token symbol failed");
    expect(resolver.resolve("fixture-inner-root-0x10-0x10") == 0x0FE0, "Rightmost-split subtraction chain failed");
    expect(resolver.resolve("1234-0x10") == 0x1224, "Literal subtraction failed");
    expect(resolver.resolve("[[fixture-pointer-root]+0x10]+0x30-0x8") == 0x3028, "Dereference plus subtraction failed");
    expect(resolver.resolve("[[game-test.exe-0x3FFFFB00]+0x10]") == 0x9000ull, "Module-based dereference with subtraction failed");
    expect(resolver.resolve("[player_base+0x1010]") == 0x3000ull, "Symbol-based bracket dereference with arithmetic failed");
    expect(resolver.resolve("[0x1000]+0x10-0x8") == 0x2008, "Arithmetic after dereference failed");
    expect(resolver.resolve("CAFEBABE") == 0xCAFEBABEull, "Hex-looking names should still resolve as hex literals first");

    expectThrows([&] { (void)resolver.resolve(""); }, "expected a symbol");
    expectThrows([&] { (void)resolver.resolve("[]"); }, "expected a symbol");
    expectThrows([&] { (void)resolver.resolve("[ ]"); }, "expected a symbol");
    expectThrows([&] { (void)resolver.resolve("[0x1000"); }, "missing a closing");
    expectThrows([&] { (void)resolver.resolve("[player_base]]"); }, "unexpected trailing");
    expectThrows([&] { (void)resolver.resolve("0x"); }, "invalid number");
    expectThrows([&] { (void)resolver.resolve("unknown_symbol"); }, "unknown symbol or module");
    expectThrows([&] { (void)resolver.resolve("game.exe+"); }, "expected a symbol");
    expectThrows([&] { (void)resolver.resolve("game.exe++0x10"); }, "expected a symbol");
    expectThrows([&] { (void)resolver.resolve("game.exe--0x10"); }, "unknown symbol or module");
    expectThrows([&] { (void)resolver.resolve("0x10 trailing"); }, "unexpected trailing");
    expectThrows([&] { (void)resolver.resolve("0x10-0x20"); }, "underflowed");
    expectThrows([&] { (void)resolver.resolve("FFFFFFFFFFFFFFFF+1"); }, "overflowed");
}

void run32BitTests() {
    FakeProcessBackend process(4);
    process.addModule("game32.exe", 0x400000);

    process.storeValue<std::uint32_t>(0x5000, 0x6000u);
    process.storeValue<std::uint32_t>(0x6020, 0x7000u);

    SymbolRepository symbols;
    registerSymbol(symbols, "player32", 0x5000);
    const auto resolver = makeResolver(process, symbols);

    expect(resolver.resolve("[player32]") == 0x6000u, "32-bit single dereference failed");
    expect(resolver.resolve("[[player32]+0x20]") == 0x7000u, "32-bit nested dereference failed");
    expect(resolver.resolve("game32.exe+0x20-0x10") == 0x400010u, "32-bit module arithmetic failed");
}

}  // namespace

int main() {
    try {
        run64BitTests();
        run32BitTests();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_address_resolver_test failed: " << exception.what() << '\n';
        return 1;
    }
}
