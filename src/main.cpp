#include <Windows.h>

#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

#include "hexengine/engine/win32_engine_factory.hpp"

namespace {

}  // namespace

int main() {
    try {
        using namespace hexengine::core;
        using namespace hexengine::engine;

        Win32EngineFactory factory;
        auto engine = factory.open(::GetCurrentProcessId());
        const auto mainModule = engine->process().mainModule();

        const auto allocation = engine->globalAlloc(AllocationRequest{
            .name = "example_page",
            .size = 0x1000,
            .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        });

        constexpr std::array<std::byte, 8> kPayload{
            std::byte{0xDE},
            std::byte{0xAD},
            std::byte{0xBE},
            std::byte{0xEF},
            std::byte{0x13},
            std::byte{0x37},
            std::byte{0xC0},
            std::byte{0xDE},
        };
        constexpr auto kPattern = "DE AD BE EF 13 37 C0 DE";

        engine->process().write(allocation.address, kPayload);
        const auto registeredSymbol = engine->registerSymbol("example_symbol", allocation.address, kPayload.size());

        const auto matched = engine->assertBytes(allocation.address, kPattern);
        const auto localHits = engine->scanner().scan(
            BytePattern::parse(kPattern),
            AddressRange{
                .start = allocation.address,
                .end = allocation.address + allocation.size,
            });
        const auto globalHits = engine->aobScan(kPattern);
        const auto region = engine->process().query(allocation.address);
        const auto symbol = engine->resolveSymbol("example_page");

        std::cout << "Main module: " << mainModule.name << " @ 0x" << std::hex << mainModule.base << std::dec << '\n';
        std::cout << "Allocation '" << allocation.name << "' @ 0x"
                  << std::hex << allocation.address << std::dec << ", size=" << allocation.size << '\n';
        std::cout << "Registered symbol '" << registeredSymbol.name << "' -> 0x" << std::hex
                  << registeredSymbol.address << std::dec << '\n';
        std::cout << "Assert result: " << (matched ? "ok" : "mismatch") << '\n';
        std::cout << "AOB hits in allocated page: " << localHits.size() << '\n';
        std::cout << "AOB hits in full process: " << globalHits.size() << '\n';
        if (symbol) {
            std::cout << "Resolved symbol '" << symbol->name << "' -> 0x" << std::hex << symbol->address << std::dec << '\n';
        }
        if (region) {
            std::cout << "Region protection flags: 0x"
                      << std::hex << static_cast<std::uint32_t>(region->protection) << std::dec
                      << ", readable=" << region->isReadable()
                      << ", writable=" << region->isWritable()
                      << ", executable=" << region->isExecutable()
                      << '\n';
        }

        const auto releasedAllocation = engine->deallocate("example_page");
        const auto releasedSymbol = engine->unregisterSymbol("example_symbol");
        std::cout << "Allocation released: " << releasedAllocation << '\n';
        std::cout << "Standalone symbol released: " << releasedSymbol << '\n';

        std::cout << "CE-style memory layer smoke test completed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Smoke test failed: " << exception.what() << '\n';
        return 1;
    }
}
