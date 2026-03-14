# CEPipelineTest

This repo now contains a minimal C++ + CMake starter with two working Windows toolchains:

- MSVC through Visual Studio 2026 Insiders
- MinGW-w64 through MSYS2

## What is installed and working

Your machine currently has both of these working:

- Visual Studio Community 2026 Insiders
- MSVC 19.50
- Windows SDK 10.0.26100.0
- Visual Studio bundled CMake 4.2.3-msvc3
- MSYS2 `g++`
- MSYS2 `mingw32-make`
- standalone CMake from Kitware

The old Visual Studio install was the broken one. The current 2026 install is usable.

## Files in this starter

- `CMakeLists.txt`: the main CMake project file
- `CMakePresets.json`: named configure/build presets for both MSVC and MinGW
- `include/cepipeline/memory/*`: CE-style memory headers
- `src/memory/*`: Win32 memory backend and runtime model
- `src/main.cpp`: a smoke test for the memory layer
- `scripts/enter-msvc-env.ps1`: loads the Visual Studio developer environment into the current PowerShell session
- `scripts/check-msvc-env.ps1`: checks `cl` and Visual Studio's bundled CMake
- `scripts/build-msvc.ps1`: configures and builds the project with MSVC
- `scripts/check-mingw-env.ps1`: prints the tool versions that matter
- `scripts/build-mingw.ps1`: configures and builds the project with MinGW

## Recommended workflow

If you want the Windows-native Microsoft toolchain, use MSVC:

```powershell
. .\scripts\enter-msvc-env.ps1
.\scripts\check-msvc-env.ps1
.\scripts\build-msvc.ps1 -Configuration Debug -Run
```

Important: the leading `.` and space in `. .\scripts\enter-msvc-env.ps1` matter. That updates the current PowerShell session.

If you want the existing GCC/MinGW path instead:

```powershell
.\scripts\check-mingw-env.ps1
.\scripts\build-mingw.ps1 -Configuration Debug -Run
```

## One-command builds

MSVC release:

```powershell
.\scripts\build-msvc.ps1 -Configuration Release
```

MinGW release:

```powershell
.\scripts\build-mingw.ps1 -Configuration Release
```

## What the core pieces do

### `CMakeLists.txt`

This is the project definition. It says:

- the minimum CMake version
- the project name
- which source files build into the executable
- which C++ language standard to use

### `CMakePresets.json`

This is a modern CMake convenience file. It stores named build configurations so you can say `cmake --preset msvc-debug` instead of passing generator, architecture, and output paths by hand every time.

### MSVC path

This setup uses:

- `cl.exe` as the compiler
- MSBuild as the build engine
- Visual Studio's bundled `cmake.exe` to generate the solution/build files

The `msvc-*` presets use the `Visual Studio 18 2026` generator and build into `build/msvc-*`.

### MinGW path

This setup also still supports:

- `g++` as the compiler
- `mingw32-make` as the build tool
- Kitware `cmake.exe` to generate MinGW makefiles

The `mingw-*` presets pin the compiler and make program explicitly.

### Memory layer

The project now has a first-pass CE-style memory subsystem in the `ce_runtime_memory` library:

- `ProcessMemory`: open a process, enumerate modules and pages, read and write memory, change page protection, allocate and free remote memory, and scan AOB patterns
- `SymbolTable`: case-insensitive registered symbols similar to CE's global symbol table behavior
- `AllocationManager`: tracks named `alloc` and `globalAlloc` style regions and auto-registers them as symbols
- `RuntimeMemoryModel`: the higher-level facade that combines process memory, symbols, allocations, `aobScan`, `assert`, and `fullAccess`

The smoke test in `src/main.cpp` attaches to the current process, allocates a page, writes bytes, registers symbols, scans for a pattern, and frees the allocation again.

## Running tests

The project now also has an actual integration test harness:

- `ce_memory_test_target`: a tiny child process that exposes known module data, a writable buffer, and a dynamically allocated page
- `ce_memory_integration_test`: launches that child, attaches to it through the real memory API, and verifies read/write, AOB scans, allocation, symbol registration, and protection changes

With MSVC, build first and then run:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
ctest --test-dir build\msvc-debug -C Debug --output-on-failure
```

With MinGW:

```powershell
.\scripts\build-mingw.ps1 -Configuration Debug
ctest --test-dir build\mingw-debug --output-on-failure
```

## Scan Benchmark

There is also a manual benchmark harness for AOB scanning:

- `ce_scan_benchmark_target`: launches a child process with a game-like committed memory footprint across several large regions
- `ce_scan_benchmark`: attaches to that child and times range-limited vs full-process scans for short, long, wildcard-heavy, nibble-heavy, and no-anchor patterns

Build it with the normal MSVC path, then run:

```powershell
.\scripts\build-msvc.ps1 -Configuration Release
.\build\msvc-release\Release\ce_scan_benchmark.exe --target .\build\msvc-release\Release\ce_scan_benchmark_target.exe
```

Optional knobs:

```powershell
.\build\msvc-release\Release\ce_scan_benchmark.exe --target .\build\msvc-release\Release\ce_scan_benchmark_target.exe --scale 2 --iterations 6 --warmup 1
```

`--scale 2` doubles the committed benchmark footprint, so the child process looks more like a memory-heavy game.

## Useful next commands

Check the MSVC toolchain:

```powershell
.\scripts\check-msvc-env.ps1
```

Check the MinGW toolchain:

```powershell
.\scripts\check-mingw-env.ps1
```

Configure an MSVC release build:

```powershell
& 'D:\VIsualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --preset msvc-release
```

Build MSVC release:

```powershell
& 'D:\VIsualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset build-msvc-release
```

## Next step for your pipeline

Once this basic scaffold is working, the next practical step is to add your first real dependencies:

- Lua
- AsmJit
- a small parser or translator layer for AA script fragments

At that point we can either:

1. keep using plain CMake with `FetchContent`, or
2. switch to a package manager such as `vcpkg` for dependencies

For your case, plain CMake first is the cleaner learning path. Since you are targeting Cheat Engine and Windows-specific low-level work, MSVC is probably the better default compiler now that the new Visual Studio install is healthy.
