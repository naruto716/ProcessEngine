cmake_minimum_required(VERSION 3.25)

project(CEPipelineTest VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(hexengine_engine
    src/backends/win32/win32_process_backend.cpp
    src/core/pattern.cpp
    src/engine/allocation_repository.cpp
    src/engine/allocation_service.cpp
    src/engine/engine_factory.cpp
    src/engine/engine_session.cpp
    src/engine/process_scanner.cpp
    src/engine/symbol_repository.cpp
)

target_compile_features(hexengine_engine PUBLIC cxx_std_20)
target_include_directories(hexengine_engine PUBLIC include)

if (WIN32)
    target_compile_definitions(hexengine_engine PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    message(FATAL_ERROR "This starter currently implements the CE-style memory layer for Windows only.")
endif()

add_executable(ce_pipeline_test
    src/main.cpp
)

target_compile_features(ce_pipeline_test PRIVATE cxx_std_20)
target_link_libraries(ce_pipeline_test PRIVATE hexengine_engine)

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
target_link_libraries(ce_memory_integration_test PRIVATE hexengine_engine)

add_executable(ce_pattern_test
    tests/pattern_test.cpp
)

target_compile_features(ce_pattern_test PRIVATE cxx_std_20)
target_include_directories(ce_pattern_test PRIVATE include tests)
target_link_libraries(ce_pattern_test PRIVATE hexengine_engine)

add_executable(ce_scan_benchmark_target
    benchmarks/scan_benchmark_target.cpp
)

target_compile_features(ce_scan_benchmark_target PRIVATE cxx_std_20)
target_include_directories(ce_scan_benchmark_target PRIVATE include benchmarks)
target_link_libraries(ce_scan_benchmark_target PRIVATE hexengine_engine)

add_executable(ce_scan_benchmark
    benchmarks/scan_benchmark.cpp
)

target_compile_features(ce_scan_benchmark PRIVATE cxx_std_20)
target_include_directories(ce_scan_benchmark PRIVATE include benchmarks)
target_link_libraries(ce_scan_benchmark PRIVATE hexengine_engine)

add_test(
    NAME ce_memory_integration
    COMMAND ce_memory_integration_test --target $<TARGET_FILE:ce_memory_test_target>
)

add_test(
    NAME ce_pattern
    COMMAND ce_pattern_test
)
