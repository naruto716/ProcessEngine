# Testing

See also: [Wiki Home](README.md) | [Getting Started](getting-started.md) | [Hooks](hooks.md)

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
- CE-like multi-target `AssemblyScript` execution against the real Win32 backend
- manual hook-style flow against the real backend: `aobScanModule(...)`, near alloc, cave assembly, hook-site jump patch, and cleanup without a prior `fullAccess(...)`
- `createThread(...)` inside `AssemblyScript` against the real Win32 backend
- byte patch apply/restore
- NOP patch apply/restore with temporary write access
- `EngineSession::writeBytes(...)` restoring protection after writing into a read-only region
- `readMem` copy into a read-only destination with protection restore
- `executeCode` running a tiny remote stub through the public session API
- protection changes
- direct CE-style address resolution
- template-style pointer resolution
- CE-style pointer-expression resolution
- alloc-backed symbol cleanup on local/global deallocation

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
- `SymbolKind::Allocation` vs `SymbolKind::Label`
- dealloc removing linked local labels
- dealloc unregistering linked allocation-backed global symbols
- script-context teardown without implicit cleanup

## Text Assembler Test

File:

- [`../tests/text_assembler_test.cpp`](../tests/text_assembler_test.cpp)

This is the focused AsmTK/AsmJit integration test for:

- text assembly into a remote code cave
- script-local name resolution from assembly text
- session-global symbol resolution from assembly text
- SSE / AVX / x87 instruction parsing through the text path
- unresolved internal assembler labels
- unresolved script labels referenced from assembly text
- duplicate parser labels
- assembler-local label reuse across separate passes

The important current rule validated here is:

- implicit labels declared only inside assembly text remain private to that assembly pass
- they do not become persistent script labels automatically

## Assembly Script Test

File:

- [`../tests/assembly_script_test.cpp`](../tests/assembly_script_test.cpp)

This is the focused CE-like scheduler test for:

- `alloc(...)` / `globalAlloc(...)` / `dealloc(...)` directives in scanned script text
- `aobScan(...)` / `aobScanModule(...)` / `aobScanRegion(...)` directives in scanned script text
- `label(...)` directives in scanned script text
- `fullAccess(...)` / `createThread(...)` directives in scanned script text
- `registerSymbol(...)` / `unregisterSymbol(...)` directives in scanned script text
- splitting a script into multiple assembly chunks when a `label:` or `expr:` line already resolves
- keeping brand-new labels as internal assembler labels in the current chunk
- assembling directly to raw patch sites like `game.exe+0x100:`
- scan-driven manual hook scripts with explicit `label(returnhere)` cross-chunk labels and jump-target verification
- scan-driven manual hook scripts succeeding without `fullAccess(...)`
- failure modes such as:
  - instructions before any current address exists
  - internal labels before any current address exists
  - unresolved complex target expressions
  - parse failures inside a chunk
  - unresolved internal assembler labels at flush time
  - unbound explicit return labels in a manual hook script
  - explicit `label(returnhere)` declarations that are never defined
  - implicit `returnhere:` labels that stay chunk-local and therefore cannot satisfy an earlier cave jump
  - missing AOB scan hits

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
