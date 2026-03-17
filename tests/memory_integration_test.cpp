#include <filesystem>
#include <iostream>
#include <optional>

#include "memory_integration_cases.hpp"
#include "memory_integration_support.hpp"

namespace fs = std::filesystem;

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
            hexengine::tests::integration::fail("Expected --target <path>");
        }

        hexengine::tests::integration::runMemoryIntegration(*targetPath);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ce_memory_integration_test failed: " << exception.what() << '\n';
        return 1;
    }
}
