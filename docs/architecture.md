# HexEngine Architecture Walkthrough

This document explains how the current `hexengine` codebase is structured, why it is split this way, and how the main flows move through the code.

The goal is practical understanding, not architecture theater. If you are coming back from older C++ and Win32 code, the easiest mental model is:

- `core` defines the engine's neutral data model
- `backend` defines how a process is accessed
- `backends/win32` is the current concrete implementation
- `engine` composes backend access with scanner, symbol, and allocation behavior

## 1. The Big Picture

At runtime, the layering currently looks like this:

```text
caller
  -> engine::Win32EngineFactory
      -> engine::EngineSession
          -> backend::IProcessBackend
              -> backends::win32::Win32ProcessBackend
          -> engine::ProcessScanner
          -> engine::SymbolRepository
          -> engine::AllocationRepository
          -> engine::AllocationService
```

The important design choice is that the engine no longer talks directly in terms of `HANDLE`, `DWORD`, or raw Win32 APIs outside the Win32 backend. That keeps the upper layers reusable.

Relevant code:

- `IProcessBackend`: [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)
- `Win32ProcessBackend`: [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- `EngineSession`: [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)
- `IEngineFactory`: [`../include/hexengine/engine/engine_factory.hpp`](../include/hexengine/engine/engine_factory.hpp)
- `Win32EngineFactory`: [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp)

## 2. Why The Code Is Split This Way

The current split is aimed at three real requirements:

1. The engine must be reusable from a future app layer, likely a webview host.
2. The memory transport can change later.
   Examples: a mock backend for tests, a driver-backed backend, or a different user-mode backend.
3. CE-like logic such as symbol registration, allocation tracking, and AOB scanning should not be fused to Win32 transport code.

That is why:

- process access lives behind `IProcessBackend`
- session-level behavior lives in `EngineSession`
- stateful concerns are isolated in repositories and services
- the scanner depends on a backend interface, not on Win32 directly

## 3. Repository Map

These are the directories you should read in order.

### `include/hexengine/core` and `src/core`

This is the engine-neutral layer.

- [`../include/hexengine/core/memory_types.hpp`](../include/hexengine/core/memory_types.hpp)
  Defines `Address`, `ProcessId`, `MemoryRegion`, `ModuleInfo`, `ProtectionFlags`, `AllocationBlock`, and other small value types.
- [`../include/hexengine/core/pattern.hpp`](../include/hexengine/core/pattern.hpp)
  Declares `PatternToken` and `BytePattern`.
- [`../src/core/pattern.cpp`](../src/core/pattern.cpp)
  Implements parsing and matching for AOB patterns, including wildcard and nibble-wildcard support.

### `include/hexengine/backend`

This is the backend service-provider interface.

- [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)
  Declares `IProcessBackend`, which is the main abstraction seam for process memory access.

### `include/hexengine/backends/win32` and `src/backends/win32`

This is the current concrete backend.

- [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)

This layer owns:

- `OpenProcess`
- module enumeration
- `VirtualQueryEx`
- `ReadProcessMemory`
- `WriteProcessMemory`
- `VirtualProtectEx`
- `VirtualAllocEx`
- `VirtualFreeEx`

### `include/hexengine/engine` and `src/engine`

This is the orchestration layer.

- [`../include/hexengine/engine/process_scanner.hpp`](../include/hexengine/engine/process_scanner.hpp)
- [`../include/hexengine/engine/symbol_repository.hpp`](../include/hexengine/engine/symbol_repository.hpp)
- [`../include/hexengine/engine/allocation_repository.hpp`](../include/hexengine/engine/allocation_repository.hpp)
- [`../include/hexengine/engine/allocation_service.hpp`](../include/hexengine/engine/allocation_service.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)
- [`../include/hexengine/engine/engine_factory.hpp`](../include/hexengine/engine/engine_factory.hpp)
- [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp)

The engine layer is where CE-style behavior starts to appear.

## 4. Core Types: The Neutral Language Of The Engine

If you want to understand the rest of the code, start with [`../include/hexengine/core/memory_types.hpp`](../include/hexengine/core/memory_types.hpp).

This file is important because it replaces raw OS types with engine-owned types:

- `Address`
- `ProcessId`
- `MemoryState`
- `MemoryType`
- `ProtectionFlags`
- `AddressRange`
- `ModuleInfo`
- `MemoryRegion`
- `ProtectionChange`
- `AllocationBlock`

This gives the rest of the engine a stable vocabulary.

Example:

- `Win32ProcessBackend` converts `PAGE_EXECUTE_READWRITE` into `core::ProtectionFlags`
- `ProcessScanner` only sees `MemoryRegion::isScanCandidate()`
- `EngineSession` only sees `core::Address` and `core::ProtectionChange`

That is the point of this layer. The engine code above it does not have to care about Win32 constants.

## 5. The Backend Interface

`IProcessBackend` in [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp) is the main transport seam.

It answers these categories of questions:

- identity
  - `pid()`
- module discovery
  - `modules()`
  - `findModule()`
  - `mainModule()`
- virtual memory inspection
  - `query()`
  - `regions()`
- memory transfer
  - `read()`
  - `tryRead()`
  - `write()`
  - `tryWrite()`
- protection changes
  - `protect()`
- allocation
  - `allocate()`
  - `free()`

This is intentionally a coarse-grained interface. It is not trying to make every tiny behavior virtual. It gives the engine one clean object to depend on.

### Why this matters

Because of this interface, the upper layers do not care whether memory access is implemented by:

- standard Win32 APIs
- a test double
- a kernel-mode driver bridge

As long as the backend honors the contract, the rest of the engine can stay the same.

## 6. The Win32 Backend

The current backend implementation is `Win32ProcessBackend` in:

- [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)

This class is responsible for translating between Win32 and the engine model.

### 6.1 Type translation

One of the backend's main jobs is converting Win32 flags to engine-neutral values:

- `toProtectionFlags(...)`
- `toWin32Protection(...)`
- `toMemoryState(...)`
- `toMemoryType(...)`

Those helpers are in [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp).

This is the boundary where:

- `PAGE_READWRITE` becomes `ProtectionFlags::Read | ProtectionFlags::Write`
- `MEM_COMMIT` becomes `MemoryState::Committed`
- `MEM_IMAGE` becomes `MemoryType::Image`

### 6.2 Region and module enumeration

The backend owns:

- module snapshots via `CreateToolhelp32Snapshot`
- region walking via `VirtualQueryEx`

That logic now lives in the backend instead of leaking through the engine.

### 6.3 Near allocation

The near-allocation logic also belongs here because it is transport-specific. It depends on:

- system allocation granularity
- virtual address space limits
- `VirtualQueryEx`
- `VirtualAllocEx`

The main helpers are:

- `nearestAllocBaseInFreeRegion(...)`
- `distanceToRegion(...)`
- `tryAllocateInRegion(...)`

all in [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp).

The engine layer above this does not need to know how Win32 near-allocation works. It only asks for `allocate(size, protection, nearAddress)`.

## 7. The Pattern Engine

The AOB parser and matcher live in:

- [`../include/hexengine/core/pattern.hpp`](../include/hexengine/core/pattern.hpp)
- [`../src/core/pattern.cpp`](../src/core/pattern.cpp)

This is a pure in-process algorithm layer. It does not know anything about remote processes.

### Supported pattern syntax

The parser supports:

- exact bytes like `48`
- full wildcards like `?` and `??`
- nibble wildcards like `4?` and `?F`

### Current optimization strategy

`BytePattern::findAll(...)` does not just do naive offset-by-offset scanning anymore.

It now:

1. finds the longest exact-byte anchor during `rebuildAnchor()`
2. uses `std::boyer_moore_horspool_searcher` on that anchor
3. runs full wildcard-aware verification only when the anchor hits

If a pattern has no usable exact anchor, it falls back to the simple path.

This keeps the algorithm correct for CE-style wildcard signatures while improving the common exact-anchor case.

## 8. ProcessScanner: Transport-Agnostic Memory Scanning

`ProcessScanner` is declared in [`../include/hexengine/engine/process_scanner.hpp`](../include/hexengine/engine/process_scanner.hpp) and implemented in [`../src/engine/process_scanner.cpp`](../src/engine/process_scanner.cpp).

This class is important because it keeps scan logic out of the backend.

The backend knows how to:

- enumerate regions
- read bytes

The scanner knows how to:

- filter scan-candidate regions
- read region data in chunks
- preserve overlap between chunks
- invoke the pattern matcher
- translate buffer offsets back into process addresses

### Why this split is good

If the scanner lived inside the Win32 backend:

- scanning would be harder to test separately
- any future mock or driver backend would have to duplicate scanning policy

By keeping `ProcessScanner` above `IProcessBackend`, scanning becomes a reusable engine service instead of a Win32 feature.

## 9. Repositories: In-Memory Session State

Two engine classes hold session-local state:

- `SymbolRepository`
- `AllocationRepository`

They are declared in:

- [`../include/hexengine/engine/symbol_repository.hpp`](../include/hexengine/engine/symbol_repository.hpp)
- [`../include/hexengine/engine/allocation_repository.hpp`](../include/hexengine/engine/allocation_repository.hpp)

### 9.1 SymbolRepository

`SymbolRepository` stores session-visible symbols in a case-insensitive map.

This is the current foundation for CE-like behaviors such as:

- `registersymbol`
- `unregistersymbol`
- allocation-backed symbol names
- module-name resolution fallback

The repository itself is intentionally simple:

- register
- unregister
- find
- list
- clear

It does not perform OS work.

### 9.2 AllocationRepository

`AllocationRepository` stores named allocations and their metadata:

- name
- address
- size
- protection
- scope

The repository is also intentionally simple. It does not call `VirtualAllocEx` or `VirtualFreeEx`. It only stores state.

This is an important design point:

- repositories store engine state
- services perform operations

## 10. AllocationService: CE-Style Allocation Policy

`AllocationService` is declared in [`../include/hexengine/engine/allocation_service.hpp`](../include/hexengine/engine/allocation_service.hpp) and implemented in [`../src/engine/allocation_service.cpp`](../src/engine/allocation_service.cpp).

This class is the first real policy layer above raw process memory access.

It composes:

- `IProcessBackend`
- `SymbolRepository`
- `AllocationRepository`

### Responsibilities

`AllocationService` currently handles:

- validating allocation requests
- preventing duplicate local allocations
- reusing existing global allocations when allowed
- calling backend allocation
- recording allocation metadata
- registering allocation-backed symbols
- deallocating and cleaning up symbols and records

This is exactly the kind of logic that should not live inside the raw backend.

## 11. EngineSession: The Main Engine Object

`EngineSession` is the main façade for callers.

Declaration:

- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)

Implementation:

- [`../src/engine/engine_session.cpp`](../src/engine/engine_session.cpp)

### What it owns

`EngineSession` composes:

- one backend instance
- one scanner
- one symbol repository
- one allocation repository
- one allocation service

That means one `EngineSession` is effectively:

> one attached engine runtime for one target process

### Why it exists

Without `EngineSession`, callers would have to manually wire:

- the backend
- the scanner
- the symbol state
- the allocation state

every time. That would make the app layer messy and error-prone.

### Convenience methods

`EngineSession` exposes high-level operations that are already close to CE use cases:

- `registerSymbol(...)`
- `resolveSymbol(...)`
- `allocate(...)`
- `deallocate(...)`
- `aobScan(...)`
- `aobScanModule(...)`
- `assertBytes(...)`
- `fullAccess(...)`

Internally, those methods delegate to the right lower layer.

Examples:

- `aobScan(...)` parses a `BytePattern` and forwards to `ProcessScanner`
- `allocate(...)` forwards to `AllocationService`
- `fullAccess(...)` forwards to backend `protect(...)`
- `resolveSymbol(...)` first checks session symbols, then falls back to backend module lookup

## 12. EngineFactory: Construction Boundary

`IEngineFactory` is declared in [`../include/hexengine/engine/engine_factory.hpp`](../include/hexengine/engine/engine_factory.hpp).

`Win32EngineFactory` is declared in [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp) and implemented in [`../src/engine/engine_factory.cpp`](../src/engine/engine_factory.cpp).

The purpose of the factory is simple:

- callers should ask for an engine session
- callers should not construct backends manually unless they are doing something specialized

Current factory operations:

- `open(pid)`
- `attachCurrent()`

Right now `Win32EngineFactory` builds:

- `Win32ProcessBackend`
- wrapped inside `EngineSession`

Later, this pattern is the seam for:

- `MockEngineFactory`
- `DriverEngineFactory`

### Do we need `IEngineFactory`?

Short answer: not strictly, but it is a reasonable seam here.

You do not need a base interface just to make multiple factory classes expose the same methods. In C++, separate classes can simply have the same method names and signatures.

You do want `IEngineFactory` when you need runtime polymorphism, for example:

- the app host chooses the backend type from config
- tests inject a mock factory into the same app code path
- the UI layer stores "some factory" without caring which concrete backend it creates

If the codebase only ever constructs `Win32EngineFactory` directly, the interface is optional and could be removed. If you expect `Win32`, `Mock`, and `Driver` factories to be selected behind one app-facing seam, keeping `IEngineFactory` is the cleaner production choice.

The current design is using the interface as that seam.

## 13. Common Execution Paths

The easiest way to understand the architecture is to follow the common request flows.

### 13.1 Attach to a process

Flow:

```text
caller
  -> Win32EngineFactory::open(pid)
      -> Win32ProcessBackend::open(pid, access)
      -> EngineSession(...)
```

Relevant code:

- [`../include/hexengine/engine/engine_factory.hpp`](../include/hexengine/engine/engine_factory.hpp)
- [`../src/engine/engine_factory.cpp`](../src/engine/engine_factory.cpp)
- [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)

### 13.2 Scan for an AOB pattern

Flow:

```text
caller
  -> EngineSession::aobScan("48 8B ?? ??")
      -> BytePattern::parse(...)
      -> ProcessScanner::scan(...)
          -> IProcessBackend::regions(...)
          -> IProcessBackend::tryRead(...)
          -> BytePattern::findAll(...)
```

Relevant code:

- [`../src/engine/engine_session.cpp`](../src/engine/engine_session.cpp)
- [`../src/engine/process_scanner.cpp`](../src/engine/process_scanner.cpp)
- [`../src/core/pattern.cpp`](../src/core/pattern.cpp)

### 13.3 Scan inside one module

Flow:

```text
caller
  -> EngineSession::aobScanModule("game.exe", pattern)
      -> ProcessScanner::scanModule(...)
          -> IProcessBackend::findModule(...)
          -> ProcessScanner::scan(...)
```

This keeps module lookup in the backend and scan policy in the scanner.

### 13.4 Allocate memory and register a symbol

Flow:

```text
caller
  -> EngineSession::allocate(request)
      -> AllocationService::allocate(request)
          -> IProcessBackend::allocate(...)
          -> AllocationRepository::upsert(...)
          -> SymbolRepository::registerSymbol(...)
```

Relevant code:

- [`../src/engine/engine_session.cpp`](../src/engine/engine_session.cpp)
- [`../src/engine/allocation_service.cpp`](../src/engine/allocation_service.cpp)

### 13.5 Change protection

Flow:

```text
caller
  -> EngineSession::fullAccess(address, size)
      -> IProcessBackend::protect(address, size, kReadWriteExecute)
```

This is a good example of a high-level CE-like helper that still stays thin.

## 14. Minimal Usage Example

This is what normal engine usage should look like from native application code.

```cpp
#include <array>
#include <cstddef>

#include "hexengine/engine/allocation_repository.hpp"
#include "hexengine/engine/win32_engine_factory.hpp"

int main() {
    using namespace hexengine;

    engine::Win32EngineFactory factory;
    auto session = factory.open(/* pid */ 1234);

    auto hits = session->aobScanModule("game.exe", "48 8B ?? ?? ?? 89");
    if (hits.empty()) {
        return 1;
    }

    const auto hookAddress = hits.front();

    const auto cave = session->allocate(engine::AllocationRequest{
        .name = "myCodecave",
        .size = 0x1000,
        .protection = core::kReadWriteExecute,
        .scope = engine::AllocationScope::Local,
        .nearAddress = hookAddress,
    });

    session->registerSymbol("player_base", cave.address, cave.size);
    session->fullAccess(hookAddress, 16);

    const std::array<std::byte, 5> patch{
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
    };
    session->process().write(hookAddress, patch);

    return 0;
}
```

This example shows the intended split:

- the factory opens the session
- the session is the main engine object
- low-level reads and writes still exist through `process()`
- higher-level helpers like scan, allocate, symbol registration, and `fullAccess` stay on the session

## 15. How This Fits A Future WebView App

The current code is the engine layer, not the application boundary.

For a future webview application, the recommended shape is:

```text
webview UI
  -> app command layer
      -> engine host/session manager
          -> hexengine::engine::EngineSession
```

What the app layer should do:

- own engine sessions
- assign session ids
- translate UI commands into engine calls
- translate engine results into serializable DTOs
- centralize error handling and logging

What the app layer should not do:

- call Win32 APIs directly
- duplicate scan logic
- manually juggle symbol and allocation state

That separation will keep the engine testable and keep the UI thin.

## 16. How To Add Another Backend

The clean path for a new backend is:

1. implement `IProcessBackend`
2. keep OS-specific details inside that implementation
3. reuse `ProcessScanner`, `AllocationService`, `SymbolRepository`, and `EngineSession`

### Example: Mock backend

A mock backend should implement:

- synthetic module list
- synthetic memory map
- in-memory read/write/protect/allocate/free

That would let the engine run pure in-process tests without launching a child process.

### Example: Driver backend

A future driver-backed backend would still present the same surface:

- `regions()`
- `read()`
- `write()`
- `protect()`
- `allocate()`
- `free()`

The engine layer would not need major changes.

This is the main pay-off of the current split.

## 17. How The Tests Reflect The Architecture

The tests already exercise the design seams.

### Integration test

Files:

- [`../tests/memory_test_target.cpp`](../tests/memory_test_target.cpp)
- [`../tests/memory_integration_test.cpp`](../tests/memory_integration_test.cpp)
- [`../tests/memory_test_fixture.hpp`](../tests/memory_test_fixture.hpp)

This path verifies the real Win32 backend and the real engine session against a child process.

It proves:

- backend transport works
- scanner works on remote memory
- allocation service works
- symbol resolution works
- protection changes work

### Pattern test

Files:

- [`../tests/pattern_test.cpp`](../tests/pattern_test.cpp)

This isolates the pattern parser and matcher from process-memory concerns.

### Benchmark

Files:

- [`../benchmarks/scan_benchmark_fixture.hpp`](../benchmarks/scan_benchmark_fixture.hpp)
- [`../benchmarks/scan_benchmark_target.cpp`](../benchmarks/scan_benchmark_target.cpp)
- [`../benchmarks/scan_benchmark.cpp`](../benchmarks/scan_benchmark.cpp)

The benchmark exercises the scanner and pattern engine under a larger synthetic footprint that behaves more like a game process.

## 18. What Is Still Missing

This architecture is a good base, but it is not the final engine yet.

The obvious next layers are:

- expression and symbol resolution beyond module names
- AA command services on top of the memory engine
- patch transactions and rollback support
- thread creation helpers
- a host layer for app/webview integration
- a mock backend implementation
- a driver-backed backend implementation

In other words:

- the engine transport and session foundation are now in place
- the future CE-like script runtime should build on top of this, not beside it

## 19. Practical Reading Order

If you want to understand the code quickly, read in this order:

1. [`../include/hexengine/core/memory_types.hpp`](../include/hexengine/core/memory_types.hpp)
2. [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)
3. [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)
4. [`../src/engine/engine_session.cpp`](../src/engine/engine_session.cpp)
5. [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)
6. [`../src/engine/process_scanner.cpp`](../src/engine/process_scanner.cpp)
7. [`../src/engine/allocation_service.cpp`](../src/engine/allocation_service.cpp)
8. [`../src/core/pattern.cpp`](../src/core/pattern.cpp)

That sequence goes from the stable data model to the backend contract, then to composition, then to the concrete implementation details.
