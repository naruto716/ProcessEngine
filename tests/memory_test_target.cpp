#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "memory_test_fixture.hpp"

namespace fs = std::filesystem;

namespace {

alignas(16) std::array<std::byte, hexengine::tests::kModulePatternBytes.size()> g_modulePattern =
    hexengine::tests::kModulePatternBytes;
alignas(16) std::array<std::byte, hexengine::tests::kWritableInitialBytes.size()> g_writableBuffer =
    hexengine::tests::kWritableInitialBytes;

struct Options {
    fs::path manifestPath;
    fs::path stopFilePath;
};

[[nodiscard]] std::string narrow(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const auto size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("WideCharToMultiByte failed");
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    const auto written = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    if (written != size) {
        throw std::runtime_error("WideCharToMultiByte returned an unexpected size");
    }

    return result;
}

[[nodiscard]] Options parseArguments(int argc, wchar_t* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument = argv[i];
        if (argument == L"--manifest" && (i + 1) < argc) {
            options.manifestPath = argv[++i];
            continue;
        }
        if (argument == L"--stop-file" && (i + 1) < argc) {
            options.stopFilePath = argv[++i];
            continue;
        }
    }

    if (options.manifestPath.empty() || options.stopFilePath.empty()) {
        throw std::invalid_argument("Expected --manifest <path> and --stop-file <path>");
    }

    return options;
}

void writeManifest(
    const fs::path& manifestPath,
    std::uintptr_t pageAddress,
    std::size_t pageSize,
    std::uintptr_t pagePatternAddress) {
    std::ofstream manifest(manifestPath, std::ios::binary | std::ios::trunc);
    if (!manifest) {
        throw std::runtime_error("Failed to open manifest file");
    }

    const auto modulePath = []() -> std::wstring {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            const auto written = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (written == 0) {
                throw std::runtime_error("GetModuleFileNameW failed");
            }
            if (written < path.size()) {
                path.resize(written);
                return path;
            }
            path.resize(path.size() * 2);
        }
    }();

    manifest << "ready=1\n";
    manifest << "pid=" << ::GetCurrentProcessId() << '\n';
    manifest << "module_path=" << narrow(modulePath) << '\n';
    manifest << std::hex << std::showbase;
    manifest << "module_pattern_address=" << reinterpret_cast<std::uintptr_t>(g_modulePattern.data()) << '\n';
    manifest << "writable_buffer_address=" << reinterpret_cast<std::uintptr_t>(g_writableBuffer.data()) << '\n';
    manifest << "page_address=" << pageAddress << '\n';
    manifest << "page_pattern_address=" << pagePatternAddress << '\n';
    manifest << std::dec << std::noshowbase;
    manifest << "module_pattern_size=" << g_modulePattern.size() << '\n';
    manifest << "writable_buffer_size=" << g_writableBuffer.size() << '\n';
    manifest << "page_size=" << pageSize << '\n';
    manifest << "page_pattern_offset=" << hexengine::tests::kPagePatternOffset << '\n';
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        const auto options = parseArguments(argc, argv);

        SYSTEM_INFO systemInfo{};
        ::GetNativeSystemInfo(&systemInfo);

        auto* page = static_cast<std::byte*>(::VirtualAlloc(
            nullptr,
            systemInfo.dwPageSize,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE));
        if (page == nullptr) {
            throw std::runtime_error("VirtualAlloc failed");
        }

        std::fill(page, page + systemInfo.dwPageSize, std::byte{0xCC});
        std::copy(
            hexengine::tests::kPagePatternBytes.begin(),
            hexengine::tests::kPagePatternBytes.end(),
            page + hexengine::tests::kPagePatternOffset);

        const auto pagePatternAddress = reinterpret_cast<std::uintptr_t>(page + hexengine::tests::kPagePatternOffset);
        writeManifest(
            options.manifestPath,
            reinterpret_cast<std::uintptr_t>(page),
            systemInfo.dwPageSize,
            pagePatternAddress);

        while (!fs::exists(options.stopFilePath)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        (void)::VirtualFree(page, 0, MEM_RELEASE);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "memory_test_target failed: " << exception.what() << '\n';
        return 1;
    }
}
