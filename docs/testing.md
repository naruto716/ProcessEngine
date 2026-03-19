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
- Lua-driven CE-style hooks and timer writes through the real Win32 backend

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

## Lua Runtime Test

File:

- [`../tests/lua_runtime_test.cpp`](../tests/lua_runtime_test.cpp)

This is the focused CE-style Lua test for:

- script-scoped global persistence per script id
- isolation between different script ids
- CE-style typed memory helpers such as `writeWord(...)` and `writeQword(...)`
- `AOBScan(...)` returning a `StringList`-style object
- `autoAssemble(...)` using the existing `AssemblyScript` pipeline
- repeating timers through `createTimer(false)` + `Interval` / `OnTimer` / `Enabled`
- one-shot `createTimer(interval, callback)` behavior
- script teardown cancelling timers and dropping script-local Lua state

## Host API Test

File:

- [`../tests/host_api_test.cpp`](../tests/host_api_test.cpp)

This is the focused native-bridge smoke test for:

- opening a bridge runtime against the current process
- `hexengine_host_run_global(...)`
- `hexengine_host_run_script(...)`
- script-scoped Lua persistence through the host DLL
- `hexengine_host_destroy_script(...)`

This validates the C ABI that the desktop host uses, without needing to automate the WPF shell itself.

## Desktop Host Build Verification

The WPF/React desktop host currently has compile/publish verification rather than automated UI tests.

Relevant files:

- [`../host/HexEngine.WebViewTest/HexEngine.WebViewTest.csproj`](../host/HexEngine.WebViewTest/HexEngine.WebViewTest.csproj)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml`](../host/HexEngine.WebViewTest/MainWindow.xaml)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml.cs`](../host/HexEngine.WebViewTest/MainWindow.xaml.cs)
- [`../host/HexEngine.WebViewTest/ui/src/App.jsx`](../host/HexEngine.WebViewTest/ui/src/App.jsx)

Current verification done during development:

- React bundle build through `npm run build`
- WPF host build through `dotnet build`
- one-EXE output shape through `dotnet publish`

What is not automated yet:

- interactive WebView UI testing
- fallback-page UI automation
- WPF window-behavior automation

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
