#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hexengine/engine/win32_engine_factory.hpp"
#include "memory_test_fixture.hpp"

namespace fs = std::filesystem;

namespace {

using namespace std::chrono_literals;

struct Manifest {
    std::uint32_t pid = 0;
    fs::path modulePath;
    std::uintptr_t modulePatternAddress = 0;
    std::size_t modulePatternSize = 0;
    std::uintptr_t writableBufferAddress = 0;
    std::size_t writableBufferSize = 0;
    std::uintptr_t pageAddress = 0;
    std::size_t pageSize = 0;
    std::uintptr_t pagePatternAddress = 0;
    std::size_t pagePatternOffset = 0;
    std::uintptr_t pointerRootStorageAddress = 0;
    std::uintptr_t pointerFinalAddress = 0;
};

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Callback>
void expectThrows(Callback&& callback, std::string_view message) {
    try {
        callback();
    } catch (const std::exception&) {
        return;
    }

    fail(message);
}

[[nodiscard]] std::wstring quoteArgument(std::wstring_view value) {
    std::wstring result;
    result.push_back(L'"');
    for (const auto ch : value) {
        if (ch == L'"') {
            result.append(L"\\\"");
        } else {
            result.push_back(ch);
        }
    }
    result.push_back(L'"');
    return result;
}

[[nodiscard]] std::wstring widen(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const auto size = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        throw std::runtime_error("MultiByteToWideChar failed");
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    const auto written = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size);
    if (written != size) {
        throw std::runtime_error("MultiByteToWideChar returned an unexpected size");
    }

    return result;
}

[[nodiscard]] std::wstring uniqueSuffix() {
    std::wostringstream stream;
    stream << ::GetCurrentProcessId() << L'_' << ::GetTickCount64();
    return stream.str();
}

[[nodiscard]] fs::path buildTempPath(std::wstring_view stem, std::wstring_view extension) {
    auto path = fs::temp_directory_path();
    path /= std::wstring(stem) + L"_" + uniqueSuffix() + std::wstring(extension);
    return path;
}

[[nodiscard]] std::uintptr_t parseAddress(const std::unordered_map<std::string, std::string>& values, std::string_view key) {
    const auto iterator = values.find(std::string(key));
    if (iterator == values.end()) {
        fail("Missing manifest address entry");
    }

    return static_cast<std::uintptr_t>(std::stoull(iterator->second, nullptr, 0));
}

[[nodiscard]] std::size_t parseSize(const std::unordered_map<std::string, std::string>& values, std::string_view key) {
    const auto iterator = values.find(std::string(key));
    if (iterator == values.end()) {
        fail("Missing manifest size entry");
    }

    return static_cast<std::size_t>(std::stoull(iterator->second, nullptr, 0));
}

[[nodiscard]] std::string parseString(const std::unordered_map<std::string, std::string>& values, std::string_view key) {
    const auto iterator = values.find(std::string(key));
    if (iterator == values.end()) {
        fail("Missing manifest string entry");
    }

    return iterator->second;
}

[[nodiscard]] Manifest loadManifest(const fs::path& manifestPath) {
    std::ifstream manifest(manifestPath, std::ios::binary);
    if (!manifest) {
        fail("Unable to open manifest");
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(manifest, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        values.emplace(line.substr(0, separator), line.substr(separator + 1));
    }

    const auto ready = parseString(values, "ready");
    expect(ready == "1", "Fixture manifest was not marked ready");

    return Manifest{
        .pid = static_cast<std::uint32_t>(std::stoul(parseString(values, "pid"), nullptr, 0)),
        .modulePath = widen(parseString(values, "module_path")),
        .modulePatternAddress = parseAddress(values, "module_pattern_address"),
        .modulePatternSize = parseSize(values, "module_pattern_size"),
        .writableBufferAddress = parseAddress(values, "writable_buffer_address"),
        .writableBufferSize = parseSize(values, "writable_buffer_size"),
        .pageAddress = parseAddress(values, "page_address"),
        .pageSize = parseSize(values, "page_size"),
        .pagePatternAddress = parseAddress(values, "page_pattern_address"),
        .pagePatternOffset = parseSize(values, "page_pattern_offset"),
        .pointerRootStorageAddress = parseAddress(values, "pointer_root_storage_address"),
        .pointerFinalAddress = parseAddress(values, "pointer_final_address"),
    };
}

class FixtureProcess {
public:
    explicit FixtureProcess(const fs::path& targetPath)
        : manifestPath_(buildTempPath(L"ce_memory_manifest", L".txt")),
          stopFilePath_(buildTempPath(L"ce_memory_stop", L".flag")) {
        const std::wstring commandLine = quoteArgument(targetPath.native())
            + L" --manifest " + quoteArgument(manifestPath_.native())
            + L" --stop-file " + quoteArgument(stopFilePath_.native());

        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);

        if (::CreateProcessW(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                targetPath.parent_path().c_str(),
                &startupInfo,
                &processInformation_) == FALSE) {
            throw std::runtime_error("CreateProcessW failed for fixture target");
        }

        waitUntilReady();
    }

    FixtureProcess(const FixtureProcess&) = delete;
    FixtureProcess& operator=(const FixtureProcess&) = delete;

    ~FixtureProcess() {
        const auto stopDirectory = stopFilePath_.parent_path();
        if (!stopDirectory.empty()) {
            std::error_code error;
            fs::create_directories(stopDirectory, error);
        }

        {
            std::ofstream stop(stopFilePath_, std::ios::binary | std::ios::trunc);
            (void)stop;
        }

        if (processInformation_.hProcess != nullptr) {
            (void)::WaitForSingleObject(processInformation_.hProcess, 5000);
        }

        if (processInformation_.hThread != nullptr) {
            ::CloseHandle(processInformation_.hThread);
        }
        if (processInformation_.hProcess != nullptr) {
            ::CloseHandle(processInformation_.hProcess);
        }

        std::error_code error;
        fs::remove(manifestPath_, error);
        fs::remove(stopFilePath_, error);
    }

    [[nodiscard]] const Manifest& manifest() const noexcept {
        return manifest_;
    }

private:
    void waitUntilReady() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;

        while (std::chrono::steady_clock::now() < deadline) {
            if (fs::exists(manifestPath_)) {
                manifest_ = loadManifest(manifestPath_);
                return;
            }

            const auto wait = ::WaitForSingleObject(processInformation_.hProcess, 50);
            if (wait == WAIT_OBJECT_0) {
                fail("Fixture process exited before it wrote its manifest");
            }

            std::this_thread::sleep_for(25ms);
        }

        fail("Timed out waiting for fixture manifest");
    }

    fs::path manifestPath_;
    fs::path stopFilePath_;
    PROCESS_INFORMATION processInformation_{};
    Manifest manifest_{};
};

[[nodiscard]] bool containsAddress(const std::vector<std::uintptr_t>& hits, std::uintptr_t address) {
    return std::find(hits.begin(), hits.end(), address) != hits.end();
}

template <std::size_t Size>
[[nodiscard]] bool equalsBytes(
    const std::vector<std::byte>& actual,
    const std::array<std::byte, Size>& expected) {
    return actual.size() == expected.size()
        && std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

[[nodiscard]] std::uintptr_t distance(std::uintptr_t left, std::uintptr_t right) {
    return left >= right ? left - right : right - left;
}

void runIntegration(const fs::path& targetPath) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    FixtureProcess fixture(targetPath);
    const auto& manifest = fixture.manifest();

    expect(manifest.modulePatternSize == hexengine::tests::kModulePatternBytes.size(), "Unexpected module pattern size");
    expect(manifest.writableBufferSize == hexengine::tests::kWritableInitialBytes.size(), "Unexpected writable buffer size");
    expect(manifest.pagePatternOffset == hexengine::tests::kPagePatternOffset, "Unexpected page pattern offset");

    Win32EngineFactory factory;
    auto engine = factory.open(manifest.pid);
    auto& process = engine->process();
    const auto mainModule = process.mainModule();

    expect(mainModule.path == manifest.modulePath.string(), "Main module path mismatch");

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

    const auto bytePatch = engine->applyPatch(
        "integration.byte.patch",
        manifest.writableBufferAddress + 4,
        writablePatchReplacement,
        writablePatchExpected);
    expect(bytePatch.address == manifest.writableBufferAddress + 4, "Byte patch recorded the wrong address");
    expect(bytePatch.originalBytes
        == std::vector<std::byte>(writablePatchExpected.begin(), writablePatchExpected.end()),
        "Byte patch did not capture the original bytes");
    expect(bytePatch.replacementBytes
        == std::vector<std::byte>(writablePatchReplacement.begin(), writablePatchReplacement.end()),
        "Byte patch did not capture the replacement bytes");

    const auto patchedWritableBytes = process.read(manifest.writableBufferAddress + 4, writablePatchReplacement.size());
    expect(equalsBytes(patchedWritableBytes, writablePatchReplacement), "Byte patch did not write the replacement bytes");

    const auto activeBytePatch = engine->patches().find("integration.byte.patch");
    expect(activeBytePatch.has_value(), "Byte patch should be listed as active");
    expect(activeBytePatch->kind == PatchKind::Bytes, "Byte patch should be recorded as a byte patch");

    expect(engine->restorePatch("integration.byte.patch"), "Byte patch restore failed");
    const auto restoredWritableBytes = process.read(manifest.writableBufferAddress, manifest.writableBufferSize);
    expect(equalsBytes(restoredWritableBytes, hexengine::tests::kWritableUpdatedBytes), "Byte patch did not restore the writable buffer");
    expect(!engine->restorePatch("integration.byte.patch"), "Second byte patch restore should return false");

    expectThrows(
        [&] {
            const std::array<std::byte, 4> wrongExpected{
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00},
            };
            (void)engine->applyPatch(
                "integration.bad.patch",
                manifest.writableBufferAddress + 4,
                writablePatchReplacement,
                wrongExpected);
        },
        "Patch expected-byte verification should fail");

    expect(engine->assertBytes(manifest.modulePatternAddress, hexengine::tests::kModulePatternText), "Module pattern assert failed");

    const auto moduleHits = engine->aobScanModule(mainModule.name, hexengine::tests::kModulePatternWildcardText);
    expect(containsAddress(moduleHits, manifest.modulePatternAddress), "Module AOB scan did not find the known pattern");

    const auto pageHits = engine->scanner().scan(
        BytePattern::parse(hexengine::tests::kPagePatternWildcardText),
        AddressRange{
            .start = manifest.pageAddress,
            .end = manifest.pageAddress + manifest.pageSize,
        });
    expect(containsAddress(pageHits, manifest.pagePatternAddress), "Page-limited scan did not find the page fixture pattern");

    const auto pointerAddress = engine->resolvePointer(
        manifest.pointerRootStorageAddress,
        hexengine::tests::kPointerFirstOffset,
        hexengine::tests::kPointerSecondOffset);
    expect(pointerAddress == manifest.pointerFinalAddress, "Pointer-chain template resolution returned the wrong address");

    const auto pointerValue = engine->readPointerValue<std::uint32_t>(
        manifest.pointerRootStorageAddress,
        hexengine::tests::kPointerFirstOffset,
        hexengine::tests::kPointerSecondOffset);
    expect(pointerValue == hexengine::tests::kPointerValue, "Pointer-chain template read returned the wrong value");

    const auto region = process.query(manifest.pageAddress);
    expect(region.has_value(), "Fixture page region query failed");
    expect(region->isCommitted(), "Fixture page should be committed");
    expect(region->isReadable(), "Fixture page should be readable");
    expect(region->isWritable(), "Fixture page should be writable");

    const auto moduleSymbol = engine->resolveSymbol(mainModule.name);
    expect(moduleSymbol.has_value(), "Module symbol resolution failed");
    expect(moduleSymbol->address == mainModule.base, "Module symbol did not resolve to the module base");

    const auto registered = engine->registerSymbol(
        "fixture.module.pattern",
        manifest.modulePatternAddress,
        manifest.modulePatternSize);
    expect(registered.address == manifest.modulePatternAddress, "Standalone symbol registration returned the wrong address");

    const auto resolved = engine->resolveSymbol("fixture.module.pattern");
    expect(resolved.has_value(), "Standalone symbol resolution failed");
    expect(resolved->address == manifest.modulePatternAddress, "Standalone symbol address mismatch");
    expect(engine->unregisterSymbol("fixture.module.pattern"), "Standalone symbol unregister failed");

    const auto pointerRootSymbol = engine->registerSymbol(
        "fixture.pointer.root",
        manifest.pointerRootStorageAddress,
        sizeof(std::uintptr_t));
    expect(pointerRootSymbol.address == manifest.pointerRootStorageAddress, "Pointer root symbol registration failed");
    const auto pointerBySymbol = engine->resolvePointer(
        "[[fixture.pointer.root+0x0]+0x18]+0x30");
    expect(pointerBySymbol == manifest.pointerFinalAddress, "Symbol-based pointer expression returned the wrong address");

    std::ostringstream pointerExpression;
    pointerExpression << "[[" << mainModule.name << "+0x" << std::hex
                      << (manifest.pointerRootStorageAddress - mainModule.base)
                      << "]+0x18]+0x30";
    const auto pointerByModuleExpression = engine->resolvePointer(pointerExpression.str());
    expect(pointerByModuleExpression == manifest.pointerFinalAddress, "Module-based pointer expression returned the wrong address");

    const auto pointerValueByExpression = engine->readPointerValue<std::uint32_t>(pointerExpression.str());
    expect(pointerValueByExpression == hexengine::tests::kPointerValue, "Pointer expression read returned the wrong value");

    const auto localAllocation = engine->allocate(AllocationRequest{
        .name = "integration.local.alloc",
        .size = 0x1000,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        .scope = AllocationScope::Local,
        .nearAddress = manifest.modulePatternAddress,
    });
    expect(localAllocation.address != 0, "Local allocation returned a null address");
    expect(distance(localAllocation.address, manifest.modulePatternAddress) <= 0x8000'0000ull, "Near allocation was not placed within +/-2GB");

    process.write(localAllocation.address, hexengine::tests::kPagePatternBytes);
    expect(
        engine->assertBytes(localAllocation.address, hexengine::tests::kPagePatternText),
        "Allocated page did not contain the expected bytes");

    const auto localSymbol = engine->resolveSymbol("integration.local.alloc");
    expect(localSymbol.has_value(), "Allocation-backed symbol resolution failed");
    expect(localSymbol->address == localAllocation.address, "Allocation-backed symbol address mismatch");

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

    const auto nopPatch = engine->applyNopPatch(
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

    expect(engine->restorePatch("integration.nop.patch"), "NOP patch restore failed");
    const auto restoredLocalBytes = process.read(localAllocation.address, localPatchExpected.size());
    expect(equalsBytes(restoredLocalBytes, localPatchExpected), "NOP patch did not restore the original bytes");
    const auto regionAfterNopRestore = process.query(localAllocation.address);
    expect(regionAfterNopRestore.has_value(), "Region query after NOP restore failed");
    expect(!regionAfterNopRestore->isWritable(), "NOP patch restore should keep the original protection");

    const auto protectionChange = engine->fullAccess(localAllocation.address, localAllocation.size);
    expect(protectionChange.current == kReadWriteExecute, "fullAccess did not request read/write/execute");

    const auto localRegion = process.query(localAllocation.address);
    expect(localRegion.has_value(), "Allocated region query failed");
    expect(localRegion->isReadable(), "Allocated region should be readable");
    expect(localRegion->isWritable(), "Allocated region should be writable after fullAccess");
    expect(localRegion->isExecutable(), "Allocated region should be executable after fullAccess");
    expect(engine->deallocate("integration.local.alloc"), "Local allocation deallocate failed");
    expect(!engine->deallocate("integration.local.alloc"), "Second local allocation deallocate should return false");

    const auto globalFirst = engine->allocate(AllocationRequest{
        .name = "integration.global.alloc",
        .size = 0x1000,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        .scope = AllocationScope::Global,
    });
    const auto globalSecond = engine->allocate(AllocationRequest{
        .name = "integration.global.alloc",
        .size = 0x800,
        .protection = ProtectionFlags::Read | ProtectionFlags::Write,
        .scope = AllocationScope::Global,
    });
    expect(globalFirst.address == globalSecond.address, "Global allocation reuse did not return the original block");
    expect(engine->deallocate("integration.global.alloc"), "Global allocation deallocate failed");
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        std::optional<fs::path> targetPath;
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view argument = argv[i];
            if (argument == L"--target" && (i + 1) < argc) {
                targetPath = argv[++i];
            }
        }

        if (!targetPath.has_value()) {
            fail("Expected --target <path>");
        }

        runIntegration(*targetPath);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_memory_integration_test failed: " << exception.what() << '\n';
        return 1;
    }
}
