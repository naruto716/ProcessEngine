#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "hexengine/backend/process_backend.hpp"

namespace hexengine::tests::integration {

namespace fs = std::filesystem;

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

[[noreturn]] void fail(std::string_view message);
void expect(bool condition, std::string_view message);

template <typename Callback>
void expectThrows(Callback&& callback, std::string_view message) {
    try {
        callback();
    } catch (const std::exception&) {
        return;
    }

    fail(message);
}

class FixtureProcess {
public:
    explicit FixtureProcess(const fs::path& targetPath);
    FixtureProcess(const FixtureProcess&) = delete;
    FixtureProcess& operator=(const FixtureProcess&) = delete;
    ~FixtureProcess();

    [[nodiscard]] const Manifest& manifest() const noexcept;

private:
    void waitUntilReady();

    fs::path manifestPath_;
    fs::path stopFilePath_;
    PROCESS_INFORMATION processInformation_{};
    Manifest manifest_{};
};

[[nodiscard]] bool containsAddress(const std::vector<std::uintptr_t>& hits, std::uintptr_t address);

template <std::size_t Size>
[[nodiscard]] bool equalsBytes(
    const std::vector<std::byte>& actual,
    const std::array<std::byte, Size>& expected) {
    return actual.size() == expected.size()
        && std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

[[nodiscard]] std::uintptr_t distance(std::uintptr_t left, std::uintptr_t right);
[[nodiscard]] std::vector<std::byte> makeWriteByteStub(std::uintptr_t destination, std::byte value);

void waitForByteValue(
    const hexengine::backend::IProcessBackend& process,
    std::uintptr_t address,
    std::byte expectedValue,
    std::chrono::milliseconds timeout);

}  // namespace hexengine::tests::integration
