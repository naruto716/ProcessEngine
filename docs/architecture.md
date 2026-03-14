# Architecture

See also: [Wiki Home](README.md) | [Getting Started](getting-started.md) | [Usage](usage.md) | [Pointers](pointers.md) | [Patching](patching.md) | [Backends](backends.md) | [Testing](testing.md)

This page is the high-level map of `hexengine`.

## Mental Model

Think of the project as 4 layers:

1. `core`
- neutral value types and pattern parsing

2. `backend`
- how a process is accessed

3. `backends/win32`
- the current concrete implementation

4. `engine`
- session-level services built on top of a backend

At runtime, the stack looks like this:

```text
caller
  -> engine::Win32EngineFactory
      -> engine::EngineSession
          -> backend::IProcessBackend
              -> backends::win32::Win32ProcessBackend
          -> engine::ProcessScanner
          -> engine::SymbolRepository
          -> engine::PointerResolver
          -> engine::AllocationRepository
          -> engine::AllocationService
          -> engine::PatchRepository
          -> engine::PatchService
```

Relevant code:

- [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)
- [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)
- [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp)

## Why It Is Split This Way

The split is there for practical reasons:

- the engine should be reusable from a future app or webview host
- the transport may change later
- CE-like behavior should not be fused to Win32 code

That is why:

- process access lives behind `IProcessBackend`
- session behavior lives in `EngineSession`
- scanners and pointer walkers live above the backend
- state lives in repositories, not in the raw backend

## Layer Summary

### `core`

Files:

- [`../include/hexengine/core/memory_types.hpp`](../include/hexengine/core/memory_types.hpp)
- [`../include/hexengine/core/pattern.hpp`](../include/hexengine/core/pattern.hpp)
- [`../src/core/pattern.cpp`](../src/core/pattern.cpp)

This layer gives the engine a stable vocabulary:

- `Address`
- `ProcessId`
- `ModuleInfo`
- `MemoryRegion`
- `ProtectionFlags`
- `AllocationBlock`
- `BytePattern`

### `backend`

Files:

- [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)

This is the process-access contract:

- module enumeration
- region queries
- read/write
- protect
- allocate/free
- target pointer-size reporting

### `backends/win32`

Files:

- [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)

This is where Win32-specific logic lives:

- `OpenProcess`
- `VirtualQueryEx`
- `ReadProcessMemory`
- `WriteProcessMemory`
- `VirtualProtectEx`
- `VirtualAllocEx`
- `VirtualFreeEx`
- pointer-size detection

### `engine`

Files:

- [`../include/hexengine/engine/process_scanner.hpp`](../include/hexengine/engine/process_scanner.hpp)
- [`../include/hexengine/engine/symbol_repository.hpp`](../include/hexengine/engine/symbol_repository.hpp)
- [`../include/hexengine/engine/pointer_resolver.hpp`](../include/hexengine/engine/pointer_resolver.hpp)
- [`../include/hexengine/engine/allocation_repository.hpp`](../include/hexengine/engine/allocation_repository.hpp)
- [`../include/hexengine/engine/allocation_service.hpp`](../include/hexengine/engine/allocation_service.hpp)
- [`../include/hexengine/engine/patch_repository.hpp`](../include/hexengine/engine/patch_repository.hpp)
- [`../include/hexengine/engine/patch_service.hpp`](../include/hexengine/engine/patch_service.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)

This is where reusable engine behavior starts:

- scan memory
- manage symbols
- resolve pointer chains
- manage named allocations
- manage named patches
- expose a single session object to callers

## Main Runtime Flows

### Attach To A Process

```text
caller
  -> Win32EngineFactory::open(pid)
      -> Win32ProcessBackend::open(pid, access)
      -> EngineSession(...)
```

### Scan For A Pattern

```text
caller
  -> EngineSession::aobScan(...)
      -> BytePattern::parse(...)
      -> ProcessScanner::scan(...)
          -> IProcessBackend::regions(...)
          -> IProcessBackend::tryRead(...)
          -> BytePattern::findAll(...)
```

### Resolve A Pointer Chain

```text
caller
  -> EngineSession::resolvePointer(base, 0x18, 0x30)
      -> PointerResolver::resolve(...)
          -> IProcessBackend::readValue<uint32_t/uint64_t>(...)
```

Or with the CE-style wrapper:

```text
caller
  -> EngineSession::resolvePointer("[[game.exe+0x123]+0x18]+0x30")
      -> PointerResolver::resolve(expression)
```

### Allocate And Register

```text
caller
  -> EngineSession::allocate(request)
      -> AllocationService::allocate(request)
          -> IProcessBackend::allocate(...)
          -> AllocationRepository::upsert(...)
          -> SymbolRepository::registerSymbol(...)
```

### Apply And Restore A Patch

```text
caller
  -> EngineSession::applyPatch(...)
      -> PatchService::apply(...)
          -> IProcessBackend::read(...)
          -> IProcessBackend::query(...)
          -> IProcessBackend::protect(...)   // only if needed
          -> IProcessBackend::write(...)
          -> PatchRepository::upsert(...)
```

Restore:

```text
caller
  -> EngineSession::restorePatch(name)
      -> PatchService::restore(name)
          -> IProcessBackend::query(...)
          -> IProcessBackend::protect(...)   // only if needed
          -> IProcessBackend::write(...)
          -> PatchRepository::erase(...)
```

## What To Read Next

- For the backend contract and Win32 details: [Backends](backends.md)
- For normal engine use from app code: [Usage](usage.md)
- For pointer-chain semantics: [Pointers](pointers.md)
- For tests and benchmarks: [Testing](testing.md)
