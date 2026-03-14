#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hexengine/engine/engine_factory.hpp"
#include "scan_benchmark_fixture.hpp"

namespace fs = std::filesystem;

namespace {

using namespace std::chrono_literals;

struct Manifest {
    std::uint32_t pid = 0;
    std::size_t scale = 1;
    std::vector<std::uintptr_t> regionAddresses;
    std::vector<std::size_t> regionSizes;
};

struct Options {
    fs::path targetPath;
    std::size_t scale = hexengine::benchmarks::kDefaultScale;
    std::size_t iterations = hexengine::benchmarks::kDefaultIterations;
    std::size_t warmupIterations = hexengine::benchmarks::kDefaultWarmupIterations;
};

struct BenchmarkStats {
    std::size_t iterations = 0;
    std::size_t matches = 0;
    double averageMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(std::string(message));
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
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

[[nodiscard]] Options parseArguments(int argc, wchar_t* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument = argv[i];
        if (argument == L"--target" && (i + 1) < argc) {
            options.targetPath = argv[++i];
            continue;
        }
        if (argument == L"--scale" && (i + 1) < argc) {
            options.scale = static_cast<std::size_t>(std::stoull(argv[++i], nullptr, 0));
            continue;
        }
        if (argument == L"--iterations" && (i + 1) < argc) {
            options.iterations = static_cast<std::size_t>(std::stoull(argv[++i], nullptr, 0));
            continue;
        }
        if (argument == L"--warmup" && (i + 1) < argc) {
            options.warmupIterations = static_cast<std::size_t>(std::stoull(argv[++i], nullptr, 0));
            continue;
        }
    }

    if (options.targetPath.empty()) {
        fail("Expected --target <path>");
    }
    if (options.scale == 0 || options.iterations == 0) {
        fail("Scale and iteration counts must be greater than zero");
    }

    return options;
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
        fail("Unable to open benchmark manifest");
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

    expect(parseString(values, "ready") == "1", "Benchmark fixture manifest was not marked ready");

    const auto regionCount = parseSize(values, "region_count");
    Manifest result{
        .pid = static_cast<std::uint32_t>(std::stoul(parseString(values, "pid"), nullptr, 0)),
        .scale = parseSize(values, "scale"),
    };
    result.regionAddresses.reserve(regionCount);
    result.regionSizes.reserve(regionCount);

    for (std::size_t i = 0; i < regionCount; ++i) {
        const auto prefix = std::string("region") + std::to_string(i);
        result.regionAddresses.push_back(parseAddress(values, prefix + "_address"));
        result.regionSizes.push_back(parseSize(values, prefix + "_size"));
    }

    return result;
}

class BenchmarkFixtureProcess {
public:
    BenchmarkFixtureProcess(const fs::path& targetPath, std::size_t scale)
        : manifestPath_(buildTempPath(L"ce_scan_benchmark_manifest", L".txt")),
          stopFilePath_(buildTempPath(L"ce_scan_benchmark_stop", L".flag")) {
        const std::wstring commandLine = quoteArgument(targetPath.native())
            + L" --manifest " + quoteArgument(manifestPath_.native())
            + L" --stop-file " + quoteArgument(stopFilePath_.native())
            + L" --scale " + std::to_wstring(scale);

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
            fail("CreateProcessW failed for benchmark target");
        }

        waitUntilReady();
    }

    BenchmarkFixtureProcess(const BenchmarkFixtureProcess&) = delete;
    BenchmarkFixtureProcess& operator=(const BenchmarkFixtureProcess&) = delete;

    ~BenchmarkFixtureProcess() {
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
        const auto deadline = std::chrono::steady_clock::now() + 20s;

        while (std::chrono::steady_clock::now() < deadline) {
            if (fs::exists(manifestPath_)) {
                manifest_ = loadManifest(manifestPath_);
                return;
            }

            const auto wait = ::WaitForSingleObject(processInformation_.hProcess, 50);
            if (wait == WAIT_OBJECT_0) {
                fail("Benchmark target exited before writing its manifest");
            }

            std::this_thread::sleep_for(25ms);
        }

        fail("Timed out waiting for benchmark manifest");
    }

    fs::path manifestPath_;
    fs::path stopFilePath_;
    PROCESS_INFORMATION processInformation_{};
    Manifest manifest_{};
};

[[nodiscard]] bool containsAddress(const std::vector<std::uintptr_t>& hits, std::uintptr_t address) {
    return std::find(hits.begin(), hits.end(), address) != hits.end();
}

[[nodiscard]] bool containsOffset(const std::vector<std::size_t>& hits, std::size_t offset) {
    return std::find(hits.begin(), hits.end(), offset) != hits.end();
}

[[nodiscard]] bool matchesNaiveAt(
    const hexengine::core::BytePattern& pattern,
    std::span<const std::byte> bytes,
    std::size_t offset) {
    const auto& tokens = pattern.tokens();
    if (offset > bytes.size() || bytes.size() - offset < tokens.size()) {
        return false;
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (!tokens[i].matches(bytes[offset + i])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::vector<std::size_t> findAllNaive(
    const hexengine::core::BytePattern& pattern,
    std::span<const std::byte> bytes) {
    std::vector<std::size_t> offsets;

    if (pattern.empty() || bytes.size() < pattern.size()) {
        return offsets;
    }

    const auto lastStart = bytes.size() - pattern.size();
    for (std::size_t offset = 0; offset <= lastStart; ++offset) {
        if (matchesNaiveAt(pattern, bytes, offset)) {
            offsets.push_back(offset);
        }
    }

    return offsets;
}

template <typename ScanFn>
[[nodiscard]] BenchmarkStats benchmarkPattern(
    ScanFn&& scan,
    std::uintptr_t expectedAddress,
    std::size_t warmupIterations,
    std::size_t iterations) {
    for (std::size_t i = 0; i < warmupIterations; ++i) {
        const auto warmupHits = scan();
        expect(containsAddress(warmupHits, expectedAddress), "Warmup scan missed the expected pattern");
    }

    BenchmarkStats stats{
        .iterations = iterations,
        .matches = 0,
        .averageMs = 0.0,
        .minMs = std::numeric_limits<double>::max(),
        .maxMs = 0.0,
    };

    double totalMs = 0.0;
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto hits = scan();
        const auto end = std::chrono::steady_clock::now();

        expect(containsAddress(hits, expectedAddress), "Benchmark scan missed the expected pattern");

        const auto ms = std::chrono::duration<double, std::milli>(end - start).count();
        totalMs += ms;
        stats.minMs = std::min(stats.minMs, ms);
        stats.maxMs = std::max(stats.maxMs, ms);
        stats.matches = hits.size();
    }

    stats.averageMs = totalMs / static_cast<double>(iterations);
    if (stats.minMs == std::numeric_limits<double>::max()) {
        stats.minMs = 0.0;
    }
    return stats;
}

template <typename ScanFn>
[[nodiscard]] BenchmarkStats benchmarkMatcher(
    ScanFn&& scan,
    std::size_t expectedOffset,
    std::size_t warmupIterations,
    std::size_t iterations) {
    for (std::size_t i = 0; i < warmupIterations; ++i) {
        const auto warmupHits = scan();
        expect(containsOffset(warmupHits, expectedOffset), "Warmup matcher missed the expected pattern");
    }

    BenchmarkStats stats{
        .iterations = iterations,
        .matches = 0,
        .averageMs = 0.0,
        .minMs = std::numeric_limits<double>::max(),
        .maxMs = 0.0,
    };

    double totalMs = 0.0;
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto hits = scan();
        const auto end = std::chrono::steady_clock::now();

        expect(containsOffset(hits, expectedOffset), "Matcher benchmark missed the expected pattern");

        const auto ms = std::chrono::duration<double, std::milli>(end - start).count();
        totalMs += ms;
        stats.minMs = std::min(stats.minMs, ms);
        stats.maxMs = std::max(stats.maxMs, ms);
        stats.matches = hits.size();
    }

    stats.averageMs = totalMs / static_cast<double>(iterations);
    if (stats.minMs == std::numeric_limits<double>::max()) {
        stats.minMs = 0.0;
    }
    return stats;
}

void printStats(
    std::string_view patternName,
    std::string_view scope,
    const BenchmarkStats& stats) {
    std::cout << std::left << std::setw(18) << patternName
              << std::left << std::setw(10) << scope
              << std::right << std::setw(6) << stats.iterations
              << std::setw(8) << stats.matches
              << std::setw(12) << std::fixed << std::setprecision(3) << stats.averageMs
              << std::setw(12) << stats.minMs
              << std::setw(12) << stats.maxMs
              << '\n';
}

void runBenchmark(const Options& options) {
    using namespace hexengine::core;
    using namespace hexengine::engine;

    BenchmarkFixtureProcess fixture(options.targetPath, options.scale);
    const auto& manifest = fixture.manifest();

    Win32EngineFactory factory;
    auto engine = factory.open(manifest.pid);
    auto& process = engine->process();

    std::size_t totalBytes = 0;
    for (const auto size : manifest.regionSizes) {
        totalBytes += size;
    }

    std::cout << "Scan benchmark target footprint: "
              << (totalBytes / hexengine::benchmarks::kOneMegabyte)
              << " MB across " << manifest.regionSizes.size() << " committed regions\n";
    std::cout << "\nRemote scan benchmark\n";
    std::cout << std::left << std::setw(18) << "Pattern"
              << std::left << std::setw(10) << "Scope"
              << std::right << std::setw(6) << "Iter"
              << std::setw(8) << "Hits"
              << std::setw(12) << "Avg ms"
              << std::setw(12) << "Min"
              << std::setw(12) << "Max"
              << '\n';

    for (const auto& spec : hexengine::benchmarks::kPatternSpecs) {
        const BytePattern pattern = BytePattern::parse(spec.patternText);
        const auto expectedAddress = manifest.regionAddresses[spec.regionIndex] + spec.offset;
        const auto regionRange = AddressRange{
            .start = manifest.regionAddresses[spec.regionIndex],
            .end = manifest.regionAddresses[spec.regionIndex] + manifest.regionSizes[spec.regionIndex],
        };

        const auto regionStats = benchmarkPattern(
            [&]() {
                return engine->scanner().scan(pattern, regionRange);
            },
            expectedAddress,
            options.warmupIterations,
            options.iterations);
        printStats(spec.name, "region", regionStats);

        const auto processStats = benchmarkPattern(
            [&]() {
                return engine->scanner().scan(pattern);
            },
            expectedAddress,
            0,
            options.iterations);
        printStats(spec.name, "process", processStats);
    }

    std::cout << "\nMatcher-only benchmark (region bytes already local)\n";
    std::cout << std::left << std::setw(18) << "Pattern"
              << std::left << std::setw(10) << "Algo"
              << std::right << std::setw(6) << "Iter"
              << std::setw(8) << "Hits"
              << std::setw(12) << "Avg ms"
              << std::setw(12) << "Min"
              << std::setw(12) << "Max"
              << '\n';

    for (const auto& spec : hexengine::benchmarks::kPatternSpecs) {
        const BytePattern pattern = BytePattern::parse(spec.patternText);
        const auto regionStart = manifest.regionAddresses[spec.regionIndex];
        const auto regionSize = manifest.regionSizes[spec.regionIndex];
        const auto regionBytes = process.read(regionStart, regionSize);

        const auto anchoredStats = benchmarkMatcher(
            [&]() {
                return pattern.findAll(regionBytes);
            },
            spec.offset,
            options.warmupIterations,
            options.iterations);
        printStats(spec.name, "anchored", anchoredStats);

        const auto naiveStats = benchmarkMatcher(
            [&]() {
                return findAllNaive(pattern, regionBytes);
            },
            spec.offset,
            0,
            options.iterations);
        printStats(spec.name, "naive", naiveStats);
    }
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        const auto options = parseArguments(argc, argv);
        runBenchmark(options);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_scan_benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
