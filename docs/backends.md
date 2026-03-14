# Backends

See also: [Wiki Home](README.md) | [Architecture](architecture.md)

This page covers the process backend seam and the current Win32 implementation.

## `IProcessBackend`

The backend contract is declared in:

- [`../include/hexengine/backend/process_backend.hpp`](../include/hexengine/backend/process_backend.hpp)

It defines the engine-facing process access API:

- `pid()`
- `pointerSize()`
- `modules()`
- `findModule()`
- `mainModule()`
- `query()`
- `regions()`
- `read()`
- `tryRead()`
- `write()`
- `tryWrite()`
- `protect()`
- `allocate()`
- `free()`

The purpose of the interface is simple:

- upper engine layers depend on this contract
- OS-specific code stays below it

Services like [`../include/hexengine/engine/address_resolver.hpp`](../include/hexengine/engine/address_resolver.hpp) and [`../include/hexengine/engine/pointer_resolver.hpp`](../include/hexengine/engine/pointer_resolver.hpp) intentionally live above this boundary. They consume module lookup and memory reads from the backend, but they are not backend responsibilities.

## `Win32ProcessBackend`

The current implementation lives in:

- [`../include/hexengine/backends/win32/win32_process_backend.hpp`](../include/hexengine/backends/win32/win32_process_backend.hpp)
- [`../src/backends/win32/win32_process_backend.cpp`](../src/backends/win32/win32_process_backend.cpp)

This class owns the Win32 details:

- `OpenProcess`
- Toolhelp module enumeration
- `VirtualQueryEx`
- `ReadProcessMemory`
- `WriteProcessMemory`
- `VirtualProtectEx`
- `VirtualAllocEx`
- `VirtualFreeEx`
- target pointer-size detection

The rest of the engine should not need to care about those APIs directly.

## Factories

The abstract factory seam is:

- [`../include/hexengine/engine/engine_factory.hpp`](../include/hexengine/engine/engine_factory.hpp)

The current concrete factory is:

- [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp)
- [`../src/engine/engine_factory.cpp`](../src/engine/engine_factory.cpp)

Why the split matters:

- callers can ask for an engine session
- the app layer does not need to construct the backend directly
- future variants can expose different factories behind the same seam

The factory surface is intentionally narrow:

- `open(pid)`

There is no `attachCurrent()` convenience on the reusable engine contract. If a test or demo wants to open the current process, it should explicitly call `open(::GetCurrentProcessId())` at the call site.

## Future Variants

This design is intended to support:

- mock backends for tests
- alternate user-mode backends
- driver-backed backends
- public vs private product variants built from the same core engine

The rule should stay simple:

- shared behavior lives above the backend
- backend-specific transport lives below it

That keeps the engine reusable and avoids forking business logic across variants.
