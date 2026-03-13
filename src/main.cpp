#include <Windows.h>

#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

#include "cepipeline/memory/runtime_memory_model.hpp"

namespace {

std::string scopeName(cepipeline::memory::AllocationScope scope) {
    switch (scope) {
    case cepipeline::memory::AllocationScope::Local:
        return "local";
    case cepipeline::memory::AllocationScope::Global:
        return "global";
    }

    return "unknown";
}

}  // namespace

int main() {
    try {
        using namespace cepipeline::memory;

        auto runtime = RuntimeMemoryModel::attachCurrent();
        const auto mainModule = runtime.process().mainModule();

        const auto allocation = runtime.allocate(AllocationRequest{
            .name = "example_page",
            .size = 0x1000,
            .protection = PAGE_READWRITE,
            .scope = AllocationScope::Global,
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

        runtime.process().write(allocation.address, kPayload);
        const auto registeredSymbol = runtime.registerSymbol("example_symbol", allocation.address, kPayload.size());

        const auto matched = runtime.assertBytes(allocation.address, kPattern);
        const auto localHits = runtime.process().scan(
            BytePattern::parse(kPattern),
            AddressRange{
                .start = allocation.address,
                .end = allocation.address + allocation.size,
            });
        const auto globalHits = runtime.aobScan(kPattern);
        const auto region = runtime.process().query(allocation.address);
        const auto symbol = runtime.resolveSymbol("example_page");

        std::cout << "Main module: " << mainModule.name << " @ 0x" << std::hex << mainModule.base << std::dec << '\n';
        std::cout << "Allocation '" << allocation.name << "' (" << scopeName(allocation.scope) << ") @ 0x"
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
            std::cout << "Region protection: 0x" << std::hex << region->protection << std::dec
                      << ", readable=" << region->isReadable()
                      << ", writable=" << region->isWritable()
                      << ", executable=" << region->isExecutable()
                      << '\n';
        }

        const auto releasedAllocation = runtime.deallocate("example_page");
        const auto releasedSymbol = runtime.unregisterSymbol("example_symbol");
        std::cout << "Allocation released: " << releasedAllocation << '\n';
        std::cout << "Standalone symbol released: " << releasedSymbol << '\n';

        std::cout << "CE-style memory layer smoke test completed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Smoke test failed: " << exception.what() << '\n';
        return 1;
    }
}
