#include "memory_integration_cases.hpp"

#include <Windows.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include "hexengine/engine/assembly_script.hpp"
#include "hexengine/engine/script_context.hpp"
#include "hexengine/engine/win32_engine_factory.hpp"
#include "memory_integration_support.hpp"
#include "memory_test_fixture.hpp"

namespace hexengine::tests::integration {
namespace {

using namespace std::chrono_literals;

struct IntegrationContext {
    explicit IntegrationContext(const fs::path& targetPath)
        : fixture(targetPath),
          manifest(fixture.manifest()) {
        hexengine::engine::Win32EngineFactory factory;
        engine = factory.open(manifest.pid);
        process = &engine->process();
        mainModule = process->mainModule();
    }

    FixtureProcess fixture;
    Manifest manifest;
    std::unique_ptr<hexengine::engine::EngineSession> engine;
    hexengine::backend::IProcessBackend* process = nullptr;
    hexengine::core::ModuleInfo mainModule;
};

void verifyFixtureSanity(IntegrationContext& context) {
    expect(
        context.manifest.modulePatternSize == hexengine::tests::kModulePatternBytes.size(),
        "Unexpected module pattern size");
    expect(
        context.manifest.writableBufferSize == hexengine::tests::kWritableInitialBytes.size(),
        "Unexpected writable buffer size");
    expect(
        context.manifest.pagePatternOffset == hexengine::tests::kPagePatternOffset,
        "Unexpected page pattern offset");
    expect(
        context.mainModule.path == context.manifest.modulePath.string(),
        "Main module path mismatch");
}

void verifyReadWriteAndPatching(IntegrationContext& context) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    auto& process = *context.process;
    auto& engine = *context.engine;
    const auto& manifest = context.manifest;

    const auto initialBytes = process.read(manifest.writableBufferAddress, manifest.writableBufferSize);
    expect(equalsBytes(initialBytes, hexengine::tests::kWritableInitialBytes), "Initial remote bytes did not match fixture");

    process.write(manifest.writableBufferAddress, hexengine::tests::kWritableUpdatedBytes);
    const auto updatedBytes = process.read(manifest.writableBufferAddress, manifest.writableBufferSize);
    expect(equalsBytes(updatedBytes, hexengine::tests::kWritableUpdatedBytes), "Updated remote bytes did not round-trip");

    const std::array<std::byte, 4> writablePatchExpected{
        hexengine::tests::kWritableUpdatedBytes[4],
        hexengine::tests::kWritableUpdatedBytes[5],
        hexengine::tests::kWritableUpdatedBytes[6],
        hexengine::tests::kWritableUpdatedBytes[7],
    };
    const std::array<std::byte, 4> writablePatchReplacement{
        std::byte{0xAA},
        std::byte{0xBB},
        std::byte{0xCC},
        std::byte{0xDD},
    };

    const auto bytePatch = engine.applyPatch(
        "integration.byte.patch",
        manifest.writableBufferAddress + 4,
        writablePatchReplacement,
        writablePatchExpected);
    expect(bytePatch.address == manifest.writableBufferAddress + 4, "Byte patch recorded the wrong address");
    expect(
        bytePatch.originalBytes == std::vector<std::byte>(writablePatchExpected.begin(), writablePatchExpected.end()),
        "Byte patch did not capture the original bytes");
    expect(
        bytePatch.replacementBytes == std::vector<std::byte>(writablePatchReplacement.begin(), writablePatchReplacement.end()),
        "Byte patch did not capture the replacement bytes");

    const auto patchedWritableBytes = process.read(manifest.writableBufferAddress + 4, writablePatchReplacement.size());
    expect(equalsBytes(patchedWritableBytes, writablePatchReplacement), "Byte patch did not write the replacement bytes");

    const auto activeBytePatch = engine.patches().find("integration.byte.patch");
    expect(activeBytePatch.has_value(), "Byte patch should be listed as active");
    expect(activeBytePatch->kind == PatchKind::Bytes, "Byte patch should be recorded as a byte patch");

    expect(engine.restorePatch("integration.byte.patch"), "Byte patch restore failed");
    const auto restoredWritableBytes = process.read(manifest.writableBufferAddress, manifest.writableBufferSize);
    expect(equalsBytes(restoredWritableBytes, hexengine::tests::kWritableUpdatedBytes), "Byte patch did not restore the writable buffer");
    expect(!engine.restorePatch("integration.byte.patch"), "Second byte patch restore should return false");

    expectThrows(
        [&] {
            const std::array<std::byte, 4> wrongExpected{
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00},
            };
            (void)engine.applyPatch(
                "integration.bad.patch",
                manifest.writableBufferAddress + 4,
                writablePatchReplacement,
                wrongExpected);
        },
        "Patch expected-byte verification should fail");
}

void verifyScanningPointersAndSymbols(IntegrationContext& context) {
    auto& process = *context.process;
    auto& engine = *context.engine;
    const auto& manifest = context.manifest;
    const auto& mainModule = context.mainModule;

    expect(engine.assertBytes(manifest.modulePatternAddress, hexengine::tests::kModulePatternText), "Module pattern assert failed");

    const auto moduleHits = engine.aobScanModule(mainModule.name, hexengine::tests::kModulePatternWildcardText);
    expect(containsAddress(moduleHits, manifest.modulePatternAddress), "Module AOB scan did not find the known pattern");

    const auto pageHits = engine.aobScanRegion(
        manifest.pageAddress,
        manifest.pageAddress + manifest.pageSize,
        hexengine::tests::kPagePatternWildcardText);
    expect(containsAddress(pageHits, manifest.pagePatternAddress), "Page-limited scan did not find the page fixture pattern");
    expectThrows(
        [&] {
            (void)engine.aobScanRegion(
                manifest.pageAddress + manifest.pageSize,
                manifest.pageAddress,
                hexengine::tests::kPagePatternWildcardText);
        },
        "AOB scan region should reject an invalid range");

    const auto pointerAddress = engine.resolvePointer(
        manifest.pointerRootStorageAddress,
        hexengine::tests::kPointerFirstOffset,
        hexengine::tests::kPointerSecondOffset);
    expect(pointerAddress == manifest.pointerFinalAddress, "Pointer-chain template resolution returned the wrong address");

    const auto pointerValue = engine.readPointerValue<std::uint32_t>(
        manifest.pointerRootStorageAddress,
        hexengine::tests::kPointerFirstOffset,
        hexengine::tests::kPointerSecondOffset);
    expect(pointerValue == hexengine::tests::kPointerValue, "Pointer-chain template read returned the wrong value");

    const auto region = process.query(manifest.pageAddress);
    expect(region.has_value(), "Fixture page region query failed");
    expect(region->isCommitted(), "Fixture page should be committed");
    expect(region->isReadable(), "Fixture page should be readable");
    expect(region->isWritable(), "Fixture page should be writable");

    const auto moduleSymbol = engine.resolveSymbol(mainModule.name);
    expect(moduleSymbol.has_value(), "Module symbol resolution failed");
    expect(moduleSymbol->address == mainModule.base, "Module symbol did not resolve to the module base");

    const auto registered = engine.registerSymbol(
        "fixture.module.pattern",
        manifest.modulePatternAddress);
    expect(registered.address == manifest.modulePatternAddress, "Standalone symbol registration returned the wrong address");

    const auto resolved = engine.resolveSymbol("fixture.module.pattern");
    expect(resolved.has_value(), "Standalone symbol resolution failed");
    expect(resolved->address == manifest.modulePatternAddress, "Standalone symbol address mismatch");
    expect(engine.unregisterSymbol("fixture.module.pattern"), "Standalone symbol unregister failed");

    const auto pointerRootSymbol = engine.registerSymbol(
        "fixture.pointer.root",
        manifest.pointerRootStorageAddress);
    expect(pointerRootSymbol.address == manifest.pointerRootStorageAddress, "Pointer root symbol registration failed");
    const auto hyphenatedPointerRootSymbol = engine.registerSymbol(
        "fixture-pointer-root",
        manifest.pointerRootStorageAddress);
    expect(hyphenatedPointerRootSymbol.address == manifest.pointerRootStorageAddress, "Hyphenated pointer root symbol registration failed");

    const auto addressBySymbolExpression = engine.resolveAddress("fixture-pointer-root-0x10");
    expect(addressBySymbolExpression == manifest.pointerRootStorageAddress - 0x10, "Symbol subtraction expression returned the wrong address");

    const auto pointerBySymbol = engine.resolveAddress(
        "[[fixture.pointer.root+0x0]+0x18]+0x30");
    expect(pointerBySymbol == manifest.pointerFinalAddress, "Symbol-based pointer expression returned the wrong address");
    const auto pointerBySubtractExpression = engine.resolveAddress(
        "[[fixture.pointer.root+0x0]+0x18]+0x30-0x10+0x10");
    expect(pointerBySubtractExpression == manifest.pointerFinalAddress, "Nested address expression with subtraction returned the wrong address");

    std::ostringstream pointerExpression;
    pointerExpression << "[[" << mainModule.name << "+0x" << std::hex
                      << (manifest.pointerRootStorageAddress - mainModule.base)
                      << "]+0x18]+0x30";
    const auto pointerByModuleExpression = engine.resolveAddress(pointerExpression.str());
    expect(pointerByModuleExpression == manifest.pointerFinalAddress, "Module-based pointer expression returned the wrong address");

    std::ostringstream moduleAddressExpression;
    moduleAddressExpression << mainModule.name << "+0x" << std::hex
                            << (manifest.pointerRootStorageAddress - mainModule.base)
                            << "-0x10";
    const auto addressByModuleExpression = engine.resolveAddress(moduleAddressExpression.str());
    expect(addressByModuleExpression == manifest.pointerRootStorageAddress - 0x10, "Module subtraction expression returned the wrong address");

    const auto pointerValueByExpression = process.readValue<std::uint32_t>(engine.resolveAddress(pointerExpression.str()));
    expect(pointerValueByExpression == hexengine::tests::kPointerValue, "Pointer expression read returned the wrong value");
}

void verifyScriptContextLifecycle(IntegrationContext& context) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    auto& process = *context.process;
    auto& engine = *context.engine;
    const auto& manifest = context.manifest;

    auto& script = engine.createScriptContext("integration.feature");
    auto& sameScript = engine.createScriptContext("integration.feature");
    expect(&script == &sameScript, "Script contexts with the same id should be reused");

    const auto scriptLocal = script.alloc(AllocationRequest{
        .name = "newmem",
        .size = 0x1000,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        .nearAddress = manifest.modulePatternAddress,
    });
    expect(script.resolveAddress("newmem") == scriptLocal.address, "Script-local alloc did not resolve inside its context");
    expectThrows(
        [&] { (void)engine.resolveAddress("newmem"); },
        "Script-local alloc name should not resolve globally before publication");

    const auto publishedLocal = script.registerSymbol("newmem");
    expect(publishedLocal.address == scriptLocal.address, "registerSymbol(localName) published the wrong address");
    expect(engine.resolveAddress("newmem") == scriptLocal.address, "Published local alloc did not resolve globally");
    expect(script.dealloc("newmem"), "Script-local dealloc failed");
    const auto publishedAfterLocalDealloc = engine.resolveSymbol("newmem");
    expect(!publishedAfterLocalDealloc.has_value(), "Script-local dealloc should remove linked allocation symbols");
    expect(!engine.unregisterSymbol("newmem"), "Script-local dealloc should already have unregistered linked allocation symbols");
    expect(engine.destroyScriptContext("integration.feature"), "Destroying an existing script context should succeed");
    expect(engine.findScriptContext("integration.feature") == nullptr, "Destroyed script context should not be discoverable");

    auto& leakingScript = engine.createScriptContext("integration.leak");
    const auto leakedLocal = leakingScript.alloc(AllocationRequest{
        .name = "tempalloc",
        .size = 0x400,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
    });
    expect(engine.destroyScriptContext("integration.leak"), "Destroying a script context with live locals should still succeed");
    const auto leakedRegion = process.query(leakedLocal.address);
    expect(leakedRegion.has_value(), "Destroying a script context should not implicitly free local allocations");
    process.free(leakedLocal.address);
}

void verifyRuntimeAllocationAndPatchFlow(IntegrationContext& context) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    auto& process = *context.process;
    auto& engine = *context.engine;
    const auto& manifest = context.manifest;

    auto& runtimeScript = engine.createScriptContext("integration.runtime");

    const auto localAllocation = runtimeScript.alloc(AllocationRequest{
        .name = "integration.local.alloc",
        .size = 0x1000,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        .nearAddress = manifest.modulePatternAddress,
    });
    expect(localAllocation.address != 0, "Local allocation returned a null address");
    expect(distance(localAllocation.address, manifest.modulePatternAddress) <= 0x8000'0000ull, "Near allocation was not placed within +/-2GB");

    process.write(localAllocation.address, hexengine::tests::kPagePatternBytes);
    expect(
        engine.assertBytes(localAllocation.address, hexengine::tests::kPagePatternText),
        "Allocated page did not contain the expected bytes");

    const auto localSymbolInRepo = engine.symbols().find("integration.local.alloc");
    expect(!localSymbolInRepo.has_value(), "Local allocation should not be stored in the global symbol repository");
    expect(!engine.resolveSymbol("integration.local.alloc").has_value(), "Local allocation should not resolve as a global symbol");

    const auto readMemDestination = localAllocation.address + 0x100;
    process.write(readMemDestination, std::array<std::byte, hexengine::tests::kModulePatternBytes.size()>{});

    const std::array<std::byte, 5> localPatchExpected{
        hexengine::tests::kPagePatternBytes[0],
        hexengine::tests::kPagePatternBytes[1],
        hexengine::tests::kPagePatternBytes[2],
        hexengine::tests::kPagePatternBytes[3],
        hexengine::tests::kPagePatternBytes[4],
    };
    (void)process.protect(localAllocation.address, localAllocation.size, ProtectionFlags::Read);
    const auto localReadOnlyRegion = process.query(localAllocation.address);
    expect(localReadOnlyRegion.has_value(), "Read-only local region query failed");
    expect(!localReadOnlyRegion->isWritable(), "Local allocation should be read-only before the NOP patch");

    engine.readMem(manifest.modulePatternAddress, readMemDestination, manifest.modulePatternSize);
    const auto readMemBytes = process.read(readMemDestination, manifest.modulePatternSize);
    expect(equalsBytes(readMemBytes, hexengine::tests::kModulePatternBytes), "readMem did not copy the expected bytes");
    const auto regionAfterReadMem = process.query(localAllocation.address);
    expect(regionAfterReadMem.has_value(), "Region query after readMem failed");
    expect(!regionAfterReadMem->isWritable(), "readMem should restore the original protection after writing");

    const auto nopPatch = engine.applyNopPatch(
        "integration.nop.patch",
        localAllocation.address,
        localPatchExpected.size(),
        localPatchExpected);
    expect(nopPatch.kind == PatchKind::Nop, "NOP patch should be recorded as a NOP patch");

    const std::array<std::byte, 5> expectedNops{
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
    };
    const auto nopBytes = process.read(localAllocation.address, expectedNops.size());
    expect(equalsBytes(nopBytes, expectedNops), "NOP patch did not fill the target range with 0x90");

    const auto regionAfterNopPatch = process.query(localAllocation.address);
    expect(regionAfterNopPatch.has_value(), "Region query after NOP patch failed");
    expect(!regionAfterNopPatch->isWritable(), "NOP patch should restore the original protection after writing");

    expect(engine.restorePatch("integration.nop.patch"), "NOP patch restore failed");
    const auto restoredLocalBytes = process.read(localAllocation.address, localPatchExpected.size());
    expect(equalsBytes(restoredLocalBytes, localPatchExpected), "NOP patch did not restore the original bytes");
    const auto regionAfterNopRestore = process.query(localAllocation.address);
    expect(regionAfterNopRestore.has_value(), "Region query after NOP restore failed");
    expect(!regionAfterNopRestore->isWritable(), "NOP patch restore should keep the original protection");

    const auto protectionChange = engine.fullAccess(localAllocation.address, localAllocation.size);
    expect(protectionChange.current == kReadWriteExecute, "fullAccess did not request read/write/execute");

    const auto localRegion = process.query(localAllocation.address);
    expect(localRegion.has_value(), "Allocated region query failed");
    expect(localRegion->isReadable(), "Allocated region should be readable");
    expect(localRegion->isWritable(), "Allocated region should be writable after fullAccess");
    expect(localRegion->isExecutable(), "Allocated region should be executable after fullAccess");
    expect(runtimeScript.dealloc("integration.local.alloc"), "Local allocation deallocate failed");
    expect(!runtimeScript.dealloc("integration.local.alloc"), "Second local allocation deallocate should return false");

    process.writeValue<std::byte>(manifest.writableBufferAddress, std::byte{0x00});
    const auto executeAllocation = runtimeScript.alloc(AllocationRequest{
        .name = "integration.exec.alloc",
        .size = 0x1000,
        .protection = kReadWriteExecute,
    });
    const auto executeStub = makeWriteByteStub(manifest.writableBufferAddress, std::byte{0x5A});
    process.write(executeAllocation.address, executeStub);
    engine.executeCode(executeAllocation.address);
    waitForByteValue(process, manifest.writableBufferAddress, std::byte{0x5A}, 2s);
    std::this_thread::sleep_for(25ms);
    expect(runtimeScript.dealloc("integration.exec.alloc"), "Execute-code allocation deallocate failed");
}

void verifyAssemblyScriptFlow(IntegrationContext& context) {
    using namespace hexengine::engine;

    auto& process = *context.process;
    auto& engine = *context.engine;
    const auto& manifest = context.manifest;
    const auto& mainModule = context.mainModule;

    auto& assemblyScriptContext = engine.createScriptContext("integration.assembly.script");
    AssemblyScript program(assemblyScriptContext);

    std::ostringstream writableTargetExpression;
    writableTargetExpression << mainModule.name << "+0x" << std::hex
                             << (manifest.writableBufferAddress - mainModule.base);

    const auto originalPatchSite = process.read(manifest.writableBufferAddress, 8);

    std::ostringstream scriptSource;
    scriptSource << "alloc(integration.script.cave1, 0x100, " << writableTargetExpression.str() << ")\n"
                 << "alloc(integration.script.cave2, 0x100, " << writableTargetExpression.str() << ")\n"
                 << '\n'
                 << "integration.script.cave1:\n"
                 << "  ret\n"
                 << '\n'
                 << "loopbegin:\n"
                 << "  jmp loopbegin\n"
                 << '\n'
                 << "integration.script.cave2:\n"
                 << "  ret\n"
                 << '\n'
                 << writableTargetExpression.str() << ":\n"
                 << "  jmp integration.script.cave2\n";

    const auto assemblyResult = program.execute(scriptSource.str());
    expect(assemblyResult.segments.size() == 3, "AssemblyScript should split the real-process script into three segments");

    const auto caveOne = assemblyScriptContext.findLocalAllocation("integration.script.cave1");
    const auto caveTwo = assemblyScriptContext.findLocalAllocation("integration.script.cave2");
    expect(caveOne.has_value(), "AssemblyScript should create the first local allocation");
    expect(caveTwo.has_value(), "AssemblyScript should create the second local allocation");
    expect(assemblyResult.segments[0].address == caveOne->address, "The first segment should assemble into cave1");
    expect(assemblyResult.segments[1].address == caveTwo->address, "The second segment should assemble into cave2");
    expect(assemblyResult.segments[2].address == manifest.writableBufferAddress, "The third segment should patch the raw writable target");

    const auto caveOneBytes = process.read(caveOne->address, assemblyResult.segments[0].emittedBytes);
    expect(caveOneBytes[0] == std::byte{0xC3}, "The first emitted cave should begin with RET");
    expect(assemblyResult.segments[0].emittedBytes >= 3, "The first cave should also contain the internal loop");
    expect(!assemblyScriptContext.hasLabel("loopbegin"), "Implicit asm labels should remain private during the integration run");

    const auto caveTwoBytes = process.read(caveTwo->address, assemblyResult.segments[1].emittedBytes);
    expect(caveTwoBytes.size() == 1 && caveTwoBytes[0] == std::byte{0xC3}, "The second emitted cave should contain RET");

    const auto patchedBytes = process.read(manifest.writableBufferAddress, assemblyResult.segments[2].emittedBytes);
    expect(
        patchedBytes[0] == std::byte{0xE9} || patchedBytes[0] == std::byte{0xEB},
        "The raw target segment should write a jump opcode");

    process.write(manifest.writableBufferAddress, originalPatchSite);
    expect(assemblyScriptContext.dealloc("integration.script.cave1"), "First assembly-script cave dealloc failed");
    expect(assemblyScriptContext.dealloc("integration.script.cave2"), "Second assembly-script cave dealloc failed");
    expect(engine.destroyScriptContext("integration.assembly.script"), "Assembly-script context destroy failed");

    auto& failingAssemblyContext = engine.createScriptContext("integration.assembly.failure");
    AssemblyScript failingProgram(failingAssemblyContext);

    expectThrows(
        [&] {
            (void)failingProgram.execute(R"(
alloc(integration.script.bad, 0x40)
integration.script.bad:
  jmp missing_label
)");
        },
        "AssemblyScript should reject unresolved internal labels on a real process backend");

    expect(failingAssemblyContext.dealloc("integration.script.bad"), "Failed AssemblyScript alloc should still be deallocatable");
    expect(engine.destroyScriptContext("integration.assembly.failure"), "Failed assembly-script context destroy failed");
}

void verifyGlobalAllocationLifecycle(IntegrationContext& context) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    auto& engine = *context.engine;

    const auto globalFirst = engine.globalAlloc(AllocationRequest{
        .name = "integration.global.alloc",
        .size = 0x1000,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
    });
    const auto globalSecond = engine.globalAlloc(AllocationRequest{
        .name = "integration.global.alloc",
        .size = 0x800,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
    });
    expect(globalFirst.address == globalSecond.address, "Global allocation reuse did not return the original block");
    const auto globalSymbolInRepo = engine.symbols().find("integration.global.alloc");
    expect(globalSymbolInRepo.has_value(), "Global allocation should be stored in the symbol repository");
    expect(globalSymbolInRepo->address == globalFirst.address, "Global allocation repository symbol address mismatch");
    const auto globalSymbol = engine.resolveSymbol("integration.global.alloc");
    expect(globalSymbol.has_value(), "Global allocation should resolve as a global symbol");
    expect(globalSymbol->address == globalFirst.address, "Global allocation symbol address mismatch");
    const auto globalAlias = engine.registerSymbol("integration.global.alias", globalFirst.address, SymbolKind::Allocation, true);
    expect(globalAlias.address == globalFirst.address, "Global allocation alias registration returned the wrong address");
    expect(engine.deallocate("integration.global.alloc"), "Global allocation deallocate failed");
    expect(!engine.symbols().find("integration.global.alloc").has_value(), "Global allocation deallocate should unregister the allocation symbol");
    expect(!engine.symbols().find("integration.global.alias").has_value(), "Global allocation deallocate should unregister linked allocation aliases");
}

}  // namespace

void runMemoryIntegration(const fs::path& targetPath) {
    IntegrationContext context(targetPath);

    verifyFixtureSanity(context);
    verifyReadWriteAndPatching(context);
    verifyScanningPointersAndSymbols(context);
    verifyScriptContextLifecycle(context);
    verifyRuntimeAllocationAndPatchFlow(context);
    verifyAssemblyScriptFlow(context);
    verifyGlobalAllocationLifecycle(context);
}

}  // namespace hexengine::tests::integration
