# HexEngine Wiki

This folder is the documentation home for `hexengine`.

## Start Here

- [Getting Started](getting-started.md)
  Build, run, and test the project locally.

- [Usage](usage.md)
  How to open a session and use the current engine APIs.

- [Pointers](pointers.md)
  How CE-style address expressions and multilevel pointer resolution work.

- [Patching](patching.md)
  Named byte patches, NOP helpers, restore, and CE-style enable/disable semantics.

- [Hooks](hooks.md)
  How to build the current manual hook pipeline with scans, caves, `returnhere`, and multi-target assembly.

- [Assembly And Labels](assembly.md)
  Detailed ownership rules for script labels, global symbols, alloc-backed names, and AsmTK/AsmJit text assembly.

## Learn The Codebase

- [Architecture](architecture.md)
  High-level map of the layers and request flows.

- [Backends](backends.md)
  `IProcessBackend`, the Win32 backend, factory split, and future variants.

- [Testing](testing.md)
  Integration tests, pattern tests, and the scan benchmark.

## Current Scope

The engine currently covers:

- process attach
- module and region queries
- read/write/protect
- alloc/free
- CE-style script contexts with local alloc bindings
- symbol registration
- AOB scanning
- region-limited AOB scanning
- CE-style address expression resolution
- multilevel pointer-chain resolution
- script-scoped labels through `ScriptContext`
- text assembly through AsmTK + AsmJit, with pass-local internal assembler labels
- CE-style `readMem` byte copying
- target-code execution through `executeCode`
- named patch management
- manual CE-style hook construction with scans, near caves, and explicit jump-back labels

The script label system is the engine-side seam for future Lua integration. AsmJit and AsmTK are already used for remote code emission, but implicit assembler labels deliberately stay private to one assembly pass.

The engine does not yet try to be a full Cheat Engine replacement. It is the runtime foundation that future higher-level trainer features can build on.
