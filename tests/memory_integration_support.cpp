#include "memory_integration_support.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace hexengine::tests::integration {
namespace {

using namespace std::chrono_literals;

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

}  // namespace

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

FixtureProcess::FixtureProcess(const fs::path& targetPath)
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

FixtureProcess::~FixtureProcess() {
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

const Manifest& FixtureProcess::manifest() const noexcept {
    return manifest_;
}

void FixtureProcess::waitUntilReady() {
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

bool containsAddress(const std::vector<std::uintptr_t>& hits, std::uintptr_t address) {
    return std::find(hits.begin(), hits.end(), address) != hits.end();
}

std::uintptr_t distance(std::uintptr_t left, std::uintptr_t right) {
    return left >= right ? left - right : right - left;
}

std::vector<std::byte> makeWriteByteStub(std::uintptr_t destination, std::byte value) {
#if defined(_WIN64)
    std::vector<std::byte> code{
        std::byte{0x48},
        std::byte{0xB8},
    };

    const auto addressBytes = std::bit_cast<std::array<std::byte, sizeof(std::uint64_t)>>(
        static_cast<std::uint64_t>(destination));
    code.insert(code.end(), addressBytes.begin(), addressBytes.end());
    code.push_back(std::byte{0xC6});
    code.push_back(std::byte{0x00});
    code.push_back(value);
    code.push_back(std::byte{0x31});
    code.push_back(std::byte{0xC0});
    code.push_back(std::byte{0xC3});
    return code;
#else
    std::vector<std::byte> code{
        std::byte{0xB8},
    };

    const auto addressBytes = std::bit_cast<std::array<std::byte, sizeof(std::uint32_t)>>(
        static_cast<std::uint32_t>(destination));
    code.insert(code.end(), addressBytes.begin(), addressBytes.end());
    code.push_back(std::byte{0xC6});
    code.push_back(std::byte{0x00});
    code.push_back(value);
    code.push_back(std::byte{0x31});
    code.push_back(std::byte{0xC0});
    code.push_back(std::byte{0xC2});
    code.push_back(std::byte{0x04});
    code.push_back(std::byte{0x00});
    return code;
#endif
}

void waitForByteValue(
    const hexengine::backend::IProcessBackend& process,
    std::uintptr_t address,
    std::byte expectedValue,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (process.readValue<std::byte>(address) == expectedValue) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    fail("Timed out waiting for executeCode to update the target byte");
}

}  // namespace hexengine::tests::integration
