#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "hexengine/engine/engine_session.hpp"
#include "hexengine/engine/script_context.hpp"
#include "support/fake_process_backend.hpp"

namespace {

using hexengine::core::Address;
using hexengine::engine::AllocationRequest;
using hexengine::engine::EngineSession;
using hexengine::engine::ScriptContext;
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

class ScopedClogCapture {
public:
    ScopedClogCapture()
        : previous_(std::clog.rdbuf(stream_.rdbuf())) {
    }

    ScopedClogCapture(const ScopedClogCapture&) = delete;
    ScopedClogCapture& operator=(const ScopedClogCapture&) = delete;

    ~ScopedClogCapture() {
        std::clog.rdbuf(previous_);
    }

    [[nodiscard]] std::string str() const {
        return stream_.str();
    }

private:
    std::ostringstream stream_;
    std::streambuf* previous_ = nullptr;
};

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
    auto backend = std::make_unique<FakeProcessBackend>(8);
    auto* fake = backend.get();
    fake->addModule("game.exe", 0x140000000ull);

    EngineSession session(std::move(backend));

    const auto globalFirst = session.globalAlloc(AllocationRequest{
        .name = "sharedmem",
        .size = 0x200,
    });
    const auto globalSecond = session.globalAlloc(AllocationRequest{
        .name = "sharedmem",
        .size = 0x100,
    });
    expect(globalFirst.address == globalSecond.address, "Global alloc should be reusable by name");
    expect(session.resolveAddress("sharedmem") == globalFirst.address, "Global alloc name should resolve session-wide");

    const auto sharedSymbol = session.resolveSymbol("sharedmem");
    expect(sharedSymbol.has_value(), "Global alloc should be visible as a global symbol");
    expect(sharedSymbol->address == globalFirst.address, "Global alloc symbol should point at the allocation");
    const auto sharedSymbolInRepo = session.symbols().find("sharedmem");
    expect(sharedSymbolInRepo.has_value(), "Global alloc should be stored in the real symbol repository");
    expect(sharedSymbolInRepo->address == globalFirst.address, "Global alloc repository symbol should point at the allocation");

    expectThrows(
        [&] { (void)session.registerSymbol("sharedmem", 0x1234); },
        "collides with an existing global allocation");

    auto& enableContext = session.createScriptContext("feature.enable");
    auto& disableContext = session.createScriptContext("feature.enable");
    expect(&enableContext == &disableContext, "The same script context id should return the same context instance");

    const auto localShadow = enableContext.alloc(AllocationRequest{
        .name = "sharedmem",
        .size = 0x80,
    });
    expect(enableContext.resolveAddress("sharedmem") == localShadow.address, "Local alloc should shadow global alloc names");
    expect(session.resolveAddress("sharedmem") == globalFirst.address, "Session resolution should still see the global alloc");
    expect(enableContext.dealloc("sharedmem"), "Local dealloc should release the shadowing local alloc");
    expect(fake->isAllocated(globalFirst.address), "Deallocating the local shadow should not affect the global alloc");

    const auto localPublished = enableContext.alloc(AllocationRequest{
        .name = "newmem",
        .size = 0x100,
    });
    expect(enableContext.resolveAddress("newmem") == localPublished.address, "Local alloc should resolve in the script context");
    expectThrows(
        [&] { (void)session.resolveAddress("newmem"); },
        "unknown symbol or module");

    const auto publishedLocal = enableContext.registerSymbol("newmem");
    expect(publishedLocal.address == localPublished.address, "registerSymbol(localName) should publish the local allocation address");
    expect(session.resolveAddress("newmem") == localPublished.address, "Published local alloc should resolve session-wide");

    const auto aliasSymbol = enableContext.registerSymbol(
        "newmem_alias",
        localPublished.address,
        localPublished.size,
        SymbolKind::Allocation,
        true);
    expect(aliasSymbol.address == localPublished.address, "Explicit alias registration should preserve the address");

    ScopedClogCapture deallocWarningCapture;
    expect(enableContext.dealloc("newmem"), "Local dealloc should free the local allocation");
    expect(!fake->isAllocated(localPublished.address), "Local dealloc should free the backing allocation");
    const auto deallocWarning = deallocWarningCapture.str();
    expect(deallocWarning.find("published symbol(s) still point") != std::string::npos, "Local dealloc should warn about dangling published symbols");
    expect(deallocWarning.find("newmem") != std::string::npos, "Local dealloc warning should mention the published local symbol");
    expect(deallocWarning.find("newmem_alias") != std::string::npos, "Local dealloc warning should mention alias symbols too");
    const auto publishedAfterLocalDealloc = session.resolveSymbol("newmem");
    expect(publishedAfterLocalDealloc.has_value(), "Explicitly registered symbol should survive local dealloc");
    expect(publishedAfterLocalDealloc->address == localPublished.address, "Published symbol should keep the original address");
    expect(enableContext.resolveAddress("newmem") == localPublished.address, "After local dealloc the script context should fall back to the published symbol");

    // Label tests — labels are now script-scoped, not pass-scoped
    expect(!enableContext.hasLabel("returnhere"), "Script context should not contain undeclared labels");
    enableContext.declareLabel("returnhere");
    expect(enableContext.hasLabel("returnhere"), "Declared label should be discoverable");
    expectThrows(
        [&] { (void)enableContext.resolveAddress("returnhere"); },
        "not bound");

    enableContext.bindLabel("returnhere", 0x5000);
    expect(enableContext.resolveAddress("returnhere+0x10") == 0x5010, "Bound label should resolve with arithmetic");
    enableContext.bindLabel("sharedmem", 0x6000);
    expect(enableContext.resolveAddress("sharedmem") == 0x6000, "Labels should shadow alloc and global names");

    // Labels persist with the script context (unlike old CodegenContext which was pass-scoped)
    expect(enableContext.hasLabel("returnhere"), "Labels should persist with the script context");
    expect(enableContext.resolveAddress("returnhere") == 0x5000, "Labels should still resolve after continued use");

    // registerSymbol should find and publish a bound label
    const auto publishedLabel = enableContext.registerSymbol("returnhere");
    expect(publishedLabel.address == 0x5000, "registerSymbol(labelName) should publish the label's bound address");
    expect(session.resolveAddress("returnhere") == 0x5000, "Published label should resolve session-wide");
    expect(session.unregisterSymbol("returnhere"), "Published label symbol should be removable");

    {
        auto& leakingContext = session.createScriptContext("feature.leak");
        const auto leaked = leakingContext.alloc(AllocationRequest{
            .name = "tempalloc",
            .size = 0x90,
        });
        expect(fake->isAllocated(leaked.address), "The fake backend should track local allocations");
        expect(session.destroyScriptContext("feature.leak"), "Destroying an existing script context should return true");
        expect(fake->isAllocated(leaked.address), "Destroying a script context should not implicitly clean up local allocations");
        expect(session.findScriptContext("feature.leak") == nullptr, "Destroyed script contexts should no longer be discoverable");
        fake->free(leaked.address);
    }

    expect(session.destroyScriptContext("feature.enable"), "Destroying a live script context should succeed");
    expect(!session.destroyScriptContext("feature.enable"), "Destroying the same script context twice should return false");

    expect(session.unregisterSymbol("newmem"), "Explicitly registered local symbol should be removable");
    expect(session.unregisterSymbol("newmem_alias"), "Explicitly registered alias should be removable");
    expect(session.deallocate("sharedmem"), "Global alloc should be deallocatable");
}

}  // namespace

int main() {
    try {
        runTests();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_script_context_test failed: " << exception.what() << '\n';
        return 1;
    }
}
