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
- CE-style `readMem` byte copying
- target-code execution through `executeCode`
- named patch management

The script label system is the engine-side seam for future AsmJit and Lua integration. This repo does not yet embed either runtime.

The engine does not yet try to be a full Cheat Engine replacement. It is the runtime foundation that future higher-level trainer features can build on.
