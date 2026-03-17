cmake_minimum_required(VERSION 3.25)

project(CEPipelineTest VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    asmjit
    GIT_REPOSITORY https://github.com/asmjit/asmjit.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    asmtk
    GIT_REPOSITORY https://github.com/asmjit/asmtk.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(ASMJIT_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(asmjit)

FetchContent_GetProperties(asmtk)
if (NOT asmtk_POPULATED)
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(asmtk)

    add_library(asmtk STATIC
        ${asmtk_SOURCE_DIR}/src/asmtk/asmparser.cpp
        ${asmtk_SOURCE_DIR}/src/asmtk/asmtokenizer.cpp
    )

    target_compile_features(asmtk PUBLIC cxx_std_20)
    target_compile_definitions(asmtk PUBLIC ASMTK_STATIC)
    target_include_directories(asmtk PUBLIC ${asmtk_SOURCE_DIR}/src)
    target_link_libraries(asmtk PUBLIC asmjit::asmjit)

    add_library(asmjit::asmtk ALIAS asmtk)
endif()

add_library(hexengine_engine
    src/backends/win32/win32_process_backend.cpp
    src/engine/address_resolver.cpp
    src/engine/assembly_script.cpp
    src/core/pattern.cpp
    src/engine/allocation_repository.cpp
    src/engine/allocation_service.cpp
    src/engine/engine_factory.cpp
    src/engine/engine_session.cpp
    src/engine/patch_repository.cpp
    src/engine/patch_service.cpp
    src/engine/pointer_resolver.cpp
    src/engine/process_scanner.cpp
    src/engine/remote_assembler.cpp
    src/engine/script_context.cpp
    src/engine/symbol_repository.cpp
    src/engine/text_assembler.cpp
)

target_compile_features(hexengine_engine PUBLIC cxx_std_20)
target_include_directories(hexengine_engine PUBLIC include)
target_link_libraries(hexengine_engine PUBLIC asmjit::asmjit asmjit::asmtk)

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
    tests/memory_integration_cases.cpp
    tests/memory_integration_support.cpp
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

add_executable(ce_address_resolver_test
    tests/address_resolver_test.cpp
)

target_compile_features(ce_address_resolver_test PRIVATE cxx_std_20)
target_include_directories(ce_address_resolver_test PRIVATE include tests)
target_link_libraries(ce_address_resolver_test PRIVATE hexengine_engine)

add_executable(ce_script_context_test
    tests/script_context_test.cpp
)

target_compile_features(ce_script_context_test PRIVATE cxx_std_20)
target_include_directories(ce_script_context_test PRIVATE include tests)
target_link_libraries(ce_script_context_test PRIVATE hexengine_engine)

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

add_test(
    NAME ce_address_resolver
    COMMAND $<TARGET_FILE:ce_address_resolver_test>
)

add_test(
    NAME ce_script_context
    COMMAND $<TARGET_FILE:ce_script_context_test>
)

add_executable(ce_remote_assembler_test
    tests/remote_assembler_test.cpp
)

target_compile_features(ce_remote_assembler_test PRIVATE cxx_std_20)
target_include_directories(ce_remote_assembler_test PRIVATE include tests)
target_link_libraries(ce_remote_assembler_test PRIVATE hexengine_engine)

add_test(
    NAME ce_remote_assembler
    COMMAND ce_remote_assembler_test
)

add_executable(ce_text_assembler_test
    tests/text_assembler_test.cpp
)

target_compile_features(ce_text_assembler_test PRIVATE cxx_std_20)
target_include_directories(ce_text_assembler_test PRIVATE include tests)
target_link_libraries(ce_text_assembler_test PRIVATE hexengine_engine)

add_test(
    NAME ce_text_assembler
    COMMAND ce_text_assembler_test
)

add_executable(ce_assembly_script_test
    tests/assembly_script_test.cpp
)

target_compile_features(ce_assembly_script_test PRIVATE cxx_std_20)
target_include_directories(ce_assembly_script_test PRIVATE include tests)
target_link_libraries(ce_assembly_script_test PRIVATE hexengine_engine)

add_test(
    NAME ce_assembly_script
    COMMAND ce_assembly_script_test
)
