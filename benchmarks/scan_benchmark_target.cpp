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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cepipeline/memory/pattern.hpp"
#include "scan_benchmark_fixture.hpp"

namespace fs = std::filesystem;

namespace {

using cepipeline::memory::BytePattern;
using cepipeline::memory::PatternToken;

struct Options {
    fs::path manifestPath;
    fs::path stopFilePath;
    std::size_t scale = cepipeline::benchmarks::kDefaultScale;
};

struct RegionAllocation {
    std::byte* base = nullptr;
    std::size_t size = 0;
};

struct ProtectedRange {
    std::size_t start = 0;
    std::size_t end = 0;
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
        if (argument == L"--scale" && (i + 1) < argc) {
            options.scale = static_cast<std::size_t>(std::stoull(argv[++i], nullptr, 0));
            continue;
        }
    }

    if (options.manifestPath.empty() || options.stopFilePath.empty()) {
        throw std::invalid_argument("Expected --manifest <path> and --stop-file <path>");
    }
    if (options.scale == 0) {
        throw std::invalid_argument("Scale must be greater than zero");
    }

    return options;
}

void fillRegion(std::span<std::byte> region, std::uint64_t seed) {
    auto state = seed;
    auto nextByte = [&]() -> std::byte {
        state ^= (state << 13);
        state ^= (state >> 7);
        state ^= (state << 17);
        return std::byte{static_cast<unsigned char>(state & 0xFFu)};
    };

    for (auto& byte : region) {
        byte = nextByte();
    }
}

[[nodiscard]] std::byte chooseWildcardByte(const PatternToken& token, std::size_t salt) {
    std::uint8_t value = token.value;

    const auto lowNibble = static_cast<std::uint8_t>((0x0A + (salt * 3)) & 0x0F);
    const auto highNibble = static_cast<std::uint8_t>((0x05 + (salt * 5)) & 0x0F);

    if ((token.mask & 0xF0) != 0xF0) {
        value |= static_cast<std::uint8_t>(highNibble << 4);
    }
    if ((token.mask & 0x0F) != 0x0F) {
        value |= lowNibble;
    }

    return std::byte{value};
}

[[nodiscard]] std::vector<std::byte> materializePattern(std::string_view text) {
    const auto pattern = BytePattern::parse(text);
    std::vector<std::byte> bytes;
    bytes.reserve(pattern.size());

    const auto& tokens = pattern.tokens();
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        bytes.push_back(chooseWildcardByte(tokens[i], i + 1));
    }

    return bytes;
}

struct AnchorInfo {
    std::size_t offset = 0;
    std::vector<std::byte> bytes;
    std::optional<std::size_t> mismatchTokenIndex;
};

[[nodiscard]] AnchorInfo buildAnchorInfo(std::string_view text) {
    const auto pattern = BytePattern::parse(text);
    const auto& tokens = pattern.tokens();

    std::size_t bestOffset = 0;
    std::size_t bestLength = 0;
    std::size_t runOffset = 0;
    std::size_t runLength = 0;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].mask == 0xFF) {
            if (runLength == 0) {
                runOffset = i;
            }
            ++runLength;
            if (runLength > bestLength) {
                bestOffset = runOffset;
                bestLength = runLength;
            }
        } else {
            runLength = 0;
        }
    }

    AnchorInfo info;
    if (bestLength == 0 || bestLength == tokens.size()) {
        return info;
    }

    info.offset = bestOffset;
    info.bytes.reserve(bestLength);
    for (std::size_t i = 0; i < bestLength; ++i) {
        info.bytes.push_back(std::byte{tokens[bestOffset + i].value});
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto withinAnchor = i >= bestOffset && i < (bestOffset + bestLength);
        if (!withinAnchor && tokens[i].mask != 0x00) {
            info.mismatchTokenIndex = i;
            break;
        }
    }

    return info;
}

[[nodiscard]] std::byte chooseMismatchByte(const PatternToken& token, std::size_t salt) {
    for (std::size_t attempt = 0; attempt < 256; ++attempt) {
        const auto candidate = std::byte{static_cast<unsigned char>((salt * 37 + attempt) & 0xFFu)};
        if (!token.matches(candidate)) {
            return candidate;
        }
    }

    return std::byte{0x00};
}

void injectAnchorDecoys(
    std::span<std::byte> region,
    std::string_view patternText,
    std::span<const ProtectedRange> protectedRanges) {
    const auto anchor = buildAnchorInfo(patternText);
    if (anchor.bytes.empty() || !anchor.mismatchTokenIndex.has_value()) {
        return;
    }

    const auto pattern = BytePattern::parse(patternText);
    const auto& tokens = pattern.tokens();
    constexpr std::size_t kStride = 1 * cepipeline::benchmarks::kOneMegabyte;

    for (std::size_t offset = kStride; offset + tokens.size() < region.size(); offset += kStride) {
        const auto candidateStart = offset;
        const auto candidateEnd = offset + tokens.size();
        const auto overlapsProtectedRange = std::any_of(
            protectedRanges.begin(),
            protectedRanges.end(),
            [&](const ProtectedRange& range) {
                return candidateStart < range.end && range.start < candidateEnd;
            });
        if (overlapsProtectedRange) {
            continue;
        }

        std::copy(anchor.bytes.begin(), anchor.bytes.end(), region.begin() + static_cast<std::ptrdiff_t>(offset + anchor.offset));

        const auto mismatchIndex = *anchor.mismatchTokenIndex;
        region[offset + mismatchIndex] = chooseMismatchByte(tokens[mismatchIndex], offset + mismatchIndex);
    }
}

void writeManifest(const fs::path& manifestPath, std::span<const RegionAllocation> regions, std::size_t scale) {
    std::ofstream manifest(manifestPath, std::ios::binary | std::ios::trunc);
    if (!manifest) {
        throw std::runtime_error("Failed to open benchmark manifest file");
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
    manifest << "scale=" << scale << '\n';
    manifest << "region_count=" << regions.size() << '\n';
    for (std::size_t i = 0; i < regions.size(); ++i) {
        manifest << "region" << i << "_address=";
        manifest << std::hex << std::showbase << reinterpret_cast<std::uintptr_t>(regions[i].base);
        manifest << std::dec << std::noshowbase << '\n';
        manifest << "region" << i << "_size=" << regions[i].size << '\n';
    }
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::vector<RegionAllocation> regions;

    try {
        const auto options = parseArguments(argc, argv);
        std::array<std::vector<ProtectedRange>, cepipeline::benchmarks::kBaseRegionSizes.size()> protectedRanges;

        regions.reserve(cepipeline::benchmarks::kBaseRegionSizes.size());
        for (std::size_t i = 0; i < cepipeline::benchmarks::kBaseRegionSizes.size(); ++i) {
            const auto size = cepipeline::benchmarks::kBaseRegionSizes[i] * options.scale;
            auto* memory = static_cast<std::byte*>(::VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE | MEM_COMMIT,
                PAGE_READWRITE));
            if (memory == nullptr) {
                throw std::runtime_error("VirtualAlloc failed for benchmark region");
            }

            fillRegion(std::span(memory, size), 0x9E37'79B9'7F4A'7C15ull + i);
            regions.push_back(RegionAllocation{
                .base = memory,
                .size = size,
            });
        }

        for (const auto& spec : cepipeline::benchmarks::kPatternSpecs) {
            const auto pattern = BytePattern::parse(spec.patternText);
            protectedRanges[spec.regionIndex].push_back(ProtectedRange{
                .start = spec.offset,
                .end = spec.offset + pattern.size(),
            });
        }

        for (const auto& spec : cepipeline::benchmarks::kPatternSpecs) {
            auto& region = regions[spec.regionIndex];
            const auto bytes = materializePattern(spec.patternText);
            if (spec.offset + bytes.size() >= region.size) {
                throw std::runtime_error("Benchmark pattern does not fit inside its target region");
            }

            std::copy(
                bytes.begin(),
                bytes.end(),
                region.base + static_cast<std::ptrdiff_t>(spec.offset));

            if (spec.injectAnchorDecoys) {
                injectAnchorDecoys(
                    std::span(region.base, region.size),
                    spec.patternText,
                    protectedRanges[spec.regionIndex]);
            }
        }

        writeManifest(options.manifestPath, regions, options.scale);

        while (!fs::exists(options.stopFilePath)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        for (const auto& region : regions) {
            (void)::VirtualFree(region.base, 0, MEM_RELEASE);
        }

        return 0;
    } catch (const std::exception& exception) {
        for (const auto& region : regions) {
            if (region.base != nullptr) {
                (void)::VirtualFree(region.base, 0, MEM_RELEASE);
            }
        }

        std::cerr << "ce_scan_benchmark_target failed: " << exception.what() << '\n';
        return 1;
    }
}
