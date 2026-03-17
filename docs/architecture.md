# Architecture

See also: [Wiki Home](README.md) | [Getting Started](getting-started.md) | [Usage](usage.md) | [Assembly And Labels](assembly.md) | [Pointers](pointers.md) | [Patching](patching.md) | [Backends](backends.md) | [Testing](testing.md)

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
          -> engine::ScriptContext (optional, per CE script)
          -> engine::AssemblyScript (optional, per CE-like multi-target script)
          -> engine::TextAssembler (optional, per assembly pass)
              -> engine::RemoteAssembler
          -> engine::ProcessScanner
          -> engine::SymbolRepository
          -> engine::AddressResolver
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
- execute code at a target entrypoint
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
- `CreateRemoteThread`
- pointer-size detection

### `engine`

Files:

- [`../include/hexengine/engine/process_scanner.hpp`](../include/hexengine/engine/process_scanner.hpp)
- [`../include/hexengine/engine/symbol_repository.hpp`](../include/hexengine/engine/symbol_repository.hpp)
- [`../include/hexengine/engine/address_resolver.hpp`](../include/hexengine/engine/address_resolver.hpp)
- [`../include/hexengine/engine/pointer_resolver.hpp`](../include/hexengine/engine/pointer_resolver.hpp)
- [`../include/hexengine/engine/allocation_repository.hpp`](../include/hexengine/engine/allocation_repository.hpp)
- [`../include/hexengine/engine/allocation_service.hpp`](../include/hexengine/engine/allocation_service.hpp)
- [`../include/hexengine/engine/script_context.hpp`](../include/hexengine/engine/script_context.hpp)
- [`../include/hexengine/engine/assembly_script.hpp`](../include/hexengine/engine/assembly_script.hpp)
- [`../include/hexengine/engine/text_assembler.hpp`](../include/hexengine/engine/text_assembler.hpp)
- [`../include/hexengine/engine/remote_assembler.hpp`](../include/hexengine/engine/remote_assembler.hpp)
- [`../include/hexengine/engine/patch_repository.hpp`](../include/hexengine/engine/patch_repository.hpp)
- [`../include/hexengine/engine/patch_service.hpp`](../include/hexengine/engine/patch_service.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)

This is where reusable engine behavior starts:

- scan memory
- scan explicit address windows
- manage symbols
- resolve CE-style address expressions
- resolve pointer chains
- copy bytes with CE-style `readMem`
- execute target code through the backend
- manage session-global allocations
- manage script-local allocations and labels
- scan and schedule CE-like multi-target assembly scripts
- assemble text into remote memory through AsmTK + AsmJit
- manage named patches
- expose a single session object to callers

## Name Ownership Model

The current engine deliberately separates 3 kinds of names:

1. script-local labels
2. session-global symbols
3. assembler-local labels

### Script-local labels

Owned by `ScriptContext`.

These are the persistent names visible to:

- `ScriptContext::resolveAddress(...)`
- local enable/disable style flows that reuse the same script context

Local allocations create a same-name script label automatically.

### Session-global symbols

Owned by `SymbolRepository`.

These are published names visible to:

- `EngineSession::resolveAddress(...)`
- `EngineSession::resolveSymbol(...)`
- any script context that falls back to session lookup

### Assembler-local labels

Owned only by `TextAssembler` / `RemoteAssembler` during one assembly pass.

These are internal branch/fixup labels from the current text block.

They are intentionally not committed back into `ScriptContext`.

That keeps enable/disable/re-enable flows from turning into a rebinding problem.

For the detailed rationale, see [Assembly And Labels](assembly.md).

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

Region-limited scan follows the same path, but starts with:

```text
caller
  -> EngineSession::aobScanRegion(start, stop, pattern)
      -> ProcessScanner::scan(..., AddressRange{start, stop})
```

### Resolve An Address Expression

```text
caller
  -> EngineSession::resolveAddress("game.exe+0x1234-0x20")
      -> AddressResolver::resolve(...)
          -> symbol lookup / module lookup / pointer dereference
```

With a script context:

```text
caller
  -> ScriptContext::resolveAddress("newmem")
      -> local script names
      -> registered symbols / modules / literals
```

### Resolve A Pointer Chain

```text
caller
  -> EngineSession::resolvePointer(base, 0x18, 0x30)
      -> PointerResolver::resolve(...)
          -> IProcessBackend::readValue<uint32_t/uint64_t>(...)
```

Or with a CE-style address string:

```text
caller
  -> EngineSession::resolveAddress("[[game.exe+0x123]+0x18]+0x30")
      -> AddressResolver::resolve(expression)
```

### Allocate And Register

```text
caller
  -> EngineSession::globalAlloc(request)
      -> AllocationService::allocate(request)
          -> IProcessBackend::allocate(...)
          -> AllocationRepository::upsert(...)
      -> SymbolRepository::registerSymbol(...)
```

Allocation records also track any linked labels and linked symbols so teardown can unregister them consistently.

Script-local allocation uses the script context instead:

```text
caller
  -> ScriptContext::alloc(request)
      -> IProcessBackend::allocate(...)
      -> local AllocationRepository::upsert(...)
      -> local label map insert(name -> address)
```

Local alloc names stay inside the script context until explicitly published, but they already resolve locally because `alloc(name, ...)` also creates a same-name script label.

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

### Copy Bytes With `readMem`

```text
caller
  -> EngineSession::readMem(source, destination, size)
      -> IProcessBackend::read(source, size)
      -> IProcessBackend::query(destination)
      -> IProcessBackend::protect(...)   // only if needed
      -> IProcessBackend::write(destination, bytes)
```

### Execute Target Code

```text
caller
  -> EngineSession::executeCode(entry)
      -> IProcessBackend::executeCode(entry)
          -> backend-specific execution path
```

### Script Labels

```text
caller
  -> ScriptContext::declareLabel / bindLabel
      -> labels shadow alloc and global names within the script context
      -> registerSymbol(labelName) publishes a label globally with SymbolKind::Label
```

### Text Assembly

```text
caller
  -> TextAssembler(script, cave)
      -> AsmTK parses instruction text
      -> unknown symbols resolve through:
           1. current block labels
           2. script labels
           3. session symbols/modules
      -> AsmJit encodes bytes into RemoteAssembler
      -> TextAssembler::flush()
           -> fail if unresolved assembler labels remain
           -> RemoteAssembler::flush()
           -> write new bytes into the remote process
```

Implicit labels declared in the current text block remain assembler-local and do not become script labels automatically.

### CE-Like Multi-Target Assembly Scripts

```text
caller
  -> AssemblyScript(script)
      -> scan directives and lines in source order
      -> execute directives such as alloc/globalAlloc/registerSymbol
      -> when `expr:` already resolves:
           -> flush current chunk
           -> start a new TextAssembler at that address
      -> when `label:` does not yet resolve:
           -> keep it as an internal assembler label in the current chunk
      -> flush each chunk through TextAssembler / RemoteAssembler
```

This keeps the high-level scheduler separate from the single-base AsmJit emitter.

## What To Read Next

- For the backend contract and Win32 details: [Backends](backends.md)
- For normal engine use from app code: [Usage](usage.md)
- For pointer-chain semantics: [Pointers](pointers.md)
- For tests and benchmarks: [Testing](testing.md)
