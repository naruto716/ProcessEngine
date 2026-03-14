# HexEngine Wiki

This folder is the documentation home for `hexengine`.

## Start Here

- [Getting Started](getting-started.md)
  Build, run, and test the project locally.

- [Usage](usage.md)
  How to open a session and use the current engine APIs.

- [Pointers](pointers.md)
  How multilevel pointer resolution works, including CE-style address strings.

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
- symbol registration
- AOB scanning
- multilevel pointer-chain resolution
- named patch management

The engine does not yet try to be a full Cheat Engine replacement. It is the runtime foundation that future higher-level trainer features can build on.
