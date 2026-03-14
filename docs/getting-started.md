# Getting Started

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Testing](testing.md)

This page is the shortest path to building and running the project.

## Toolchains

The repo currently supports:

- MSVC through Visual Studio
- MinGW-w64 through MSYS2

MSVC is the better default for this project because the engine is Windows-specific.

## Recommended MSVC Workflow

```powershell
. .\scripts\enter-msvc-env.ps1
.\scripts\check-msvc-env.ps1
.\scripts\build-msvc.ps1 -Configuration Debug -Run
```

Important:

- the leading `. ` in `. .\scripts\enter-msvc-env.ps1` matters
- it updates the current PowerShell session

## MinGW Workflow

```powershell
.\scripts\check-mingw-env.ps1
.\scripts\build-mingw.ps1 -Configuration Debug -Run
```

## Run Tests

MSVC:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
ctest --test-dir build\msvc-debug -C Debug --output-on-failure
```

MinGW:

```powershell
.\scripts\build-mingw.ps1 -Configuration Debug
ctest --test-dir build\mingw-debug --output-on-failure
```

## Repo Map

- `include/hexengine/core/*`
  engine-neutral types and pattern parsing
- `include/hexengine/backend/*`
  backend interfaces
- `include/hexengine/backends/win32/*`
  Win32 backend
- `include/hexengine/engine/*`
  session, pointer, scan, symbol, and allocation services
- `src/*`
  implementations
- `tests/*`
  correctness tests
- `benchmarks/*`
  scan benchmark harness

## Next Pages

- To understand the design: [Architecture](architecture.md)
- To start using the engine APIs: [Usage](usage.md)
