# Testing

See also: [Wiki Home](README.md) | [Getting Started](getting-started.md)

This page explains how the current tests and benchmarks are organized.

## Integration Test

Files:

- [`../tests/memory_test_target.cpp`](../tests/memory_test_target.cpp)
- [`../tests/memory_integration_test.cpp`](../tests/memory_integration_test.cpp)
- [`../tests/memory_test_fixture.hpp`](../tests/memory_test_fixture.hpp)

The integration test launches a child process, attaches to it through the real engine, and validates end-to-end behavior.

It currently verifies:

- read/write
- AOB scanning, including region-limited scans
- symbol registration
- allocation and deallocation
- byte patch apply/restore
- NOP patch apply/restore with temporary write access
- `readMem` copy into a read-only destination with protection restore
- `executeCode` running a tiny remote stub through the public session API
- protection changes
- direct CE-style address resolution
- template-style pointer resolution
- CE-style pointer-expression resolution

Run it through `ctest`:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
ctest --test-dir build\msvc-debug -C Debug --output-on-failure
```

## Pattern Test

File:

- [`../tests/pattern_test.cpp`](../tests/pattern_test.cpp)

This is the focused correctness test for AOB pattern parsing and matching.

It is intentionally separate from remote process memory behavior.

## Address Resolver Test

File:

- [`../tests/address_resolver_test.cpp`](../tests/address_resolver_test.cpp)

This is the focused parser and expression-resolution test for:

- module and symbol lookup
- `+` / `-` arithmetic
- nested `[...]` dereference chains
- hyphenated symbol and module names
- 32-bit and 64-bit pointer dereference behavior
- malformed-expression failures

It uses a fake backend so these cases can be tested thoroughly without spawning a real process.

## Script Context Test

File:

- [`../tests/script_context_test.cpp`](../tests/script_context_test.cpp)

This is the focused scope and resolver-layering test for:

- session-global alloc semantics and reuse
- script-local alloc shadowing
- explicit publication with `registerSymbol(name)`
- label precedence and scoping
- script-context teardown without implicit cleanup

## Scan Benchmark

Files:

- [`../benchmarks/scan_benchmark_fixture.hpp`](../benchmarks/scan_benchmark_fixture.hpp)
- [`../benchmarks/scan_benchmark_target.cpp`](../benchmarks/scan_benchmark_target.cpp)
- [`../benchmarks/scan_benchmark.cpp`](../benchmarks/scan_benchmark.cpp)

The benchmark creates a larger synthetic process footprint and measures scan behavior under more game-like conditions.

Run it with MSVC release:

```powershell
.\scripts\build-msvc.ps1 -Configuration Release
.\build\msvc-release\Release\ce_scan_benchmark.exe --target .\build\msvc-release\Release\ce_scan_benchmark_target.exe
```

Optional example:

```powershell
.\build\msvc-release\Release\ce_scan_benchmark.exe --target .\build\msvc-release\Release\ce_scan_benchmark_target.exe --scale 2 --iterations 6 --warmup 1
```

`--scale 2` doubles the synthetic memory footprint.
