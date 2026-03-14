cmake_minimum_required(VERSION 3.25)

project(CEPipelineTest VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(ce_runtime_memory
    src/memory/allocation_manager.cpp
    src/memory/pattern.cpp
    src/memory/process_memory.cpp
    src/memory/runtime_memory_model.cpp
    src/memory/symbol_table.cpp
)

target_compile_features(ce_runtime_memory PUBLIC cxx_std_20)
target_include_directories(ce_runtime_memory PUBLIC include)

if (WIN32)
    target_compile_definitions(ce_runtime_memory PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    message(FATAL_ERROR "This starter currently implements the CE-style memory layer for Windows only.")
endif()

add_executable(ce_pipeline_test
    src/main.cpp
)

target_compile_features(ce_pipeline_test PRIVATE cxx_std_20)
target_link_libraries(ce_pipeline_test PRIVATE ce_runtime_memory)

add_executable(ce_memory_test_target
    tests/memory_test_target.cpp
)

target_compile_features(ce_memory_test_target PRIVATE cxx_std_20)
target_include_directories(ce_memory_test_target PRIVATE include tests)

add_executable(ce_memory_integration_test
    tests/memory_integration_test.cpp
)

target_compile_features(ce_memory_integration_test PRIVATE cxx_std_20)
target_include_directories(ce_memory_integration_test PRIVATE include tests)
target_link_libraries(ce_memory_integration_test PRIVATE ce_runtime_memory)

add_executable(ce_pattern_test
    tests/pattern_test.cpp
)

target_compile_features(ce_pattern_test PRIVATE cxx_std_20)
target_include_directories(ce_pattern_test PRIVATE include tests)
target_link_libraries(ce_pattern_test PRIVATE ce_runtime_memory)

add_executable(ce_scan_benchmark_target
    benchmarks/scan_benchmark_target.cpp
)

target_compile_features(ce_scan_benchmark_target PRIVATE cxx_std_20)
target_include_directories(ce_scan_benchmark_target PRIVATE include benchmarks)
target_link_libraries(ce_scan_benchmark_target PRIVATE ce_runtime_memory)

add_executable(ce_scan_benchmark
    benchmarks/scan_benchmark.cpp
)

target_compile_features(ce_scan_benchmark PRIVATE cxx_std_20)
target_include_directories(ce_scan_benchmark PRIVATE include benchmarks)
target_link_libraries(ce_scan_benchmark PRIVATE ce_runtime_memory)

add_test(
    NAME ce_memory_integration
    COMMAND ce_memory_integration_test --target $<TARGET_FILE:ce_memory_test_target>
)

add_test(
    NAME ce_pattern
    COMMAND ce_pattern_test
)
