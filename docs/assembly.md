# Assembly And Label Model

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Hooks](hooks.md) | [Architecture](architecture.md) | [Testing](testing.md)

This page documents the current `hexengine` model for:

- script-local labels
- allocation-backed labels
- global symbols
- CE-like multi-target assembly scripts
- text assembly through AsmTK + AsmJit
- what persists across enable/disable style flows
- what deliberately stays private to one assembly pass

For the step-by-step manual hook workflow, see [Hooks](hooks.md).

The short version is:

- `ScriptContext` owns the persistent script-local names
- `alloc(name, ...)` also creates a same-name local label
- `registerSymbol(name)` publishes a local label or alloc globally
- `AssemblyScript` scans a CE-like source string and splits it into address-bound chunks
- `label(name)` marks an explicit script label that may be defined in one chunk and referenced from another
- `TextAssembler` uses AsmTK + AsmJit for one assembly pass
- labels declared only inside assembly text stay private to that pass
- local/global allocation teardown cleans up linked labels and symbols

## 1. The Three Name Spaces

There are 3 different concepts. Keeping them separate is intentional.

### Script labels

Stored in `ScriptContext`.

Files:

- [`../include/hexengine/engine/script_context.hpp`](../include/hexengine/engine/script_context.hpp)
- [`../src/engine/script_context.cpp`](../src/engine/script_context.cpp)

These are persistent script-local names:

- alloc-backed names like `newmem`
- explicitly declared labels like `returnhere`

They survive as long as the `ScriptContext` survives.

### Global symbols

Stored in `SymbolRepository`.

Files:

- [`../include/hexengine/engine/symbol_repository.hpp`](../include/hexengine/engine/symbol_repository.hpp)
- [`../src/engine/symbol_repository.cpp`](../src/engine/symbol_repository.cpp)

These are session-wide names published explicitly through:

- `EngineSession::registerSymbol(...)`
- `ScriptContext::registerSymbol(...)`
- `EngineSession::globalAlloc(...)`

They are visible to session-level resolution.

### Assembler labels

Stored only inside one `TextAssembler` / `RemoteAssembler` pass.

Files:

- [`../include/hexengine/engine/text_assembler.hpp`](../include/hexengine/engine/text_assembler.hpp)
- [`../src/engine/text_assembler.cpp`](../src/engine/text_assembler.cpp)
- [`../include/hexengine/engine/remote_assembler.hpp`](../include/hexengine/engine/remote_assembler.hpp)
- [`../src/engine/remote_assembler.cpp`](../src/engine/remote_assembler.cpp)

These are labels such as:

```asm
loop:
  dec ecx
  jne loop
```

They exist only while AsmTK/AsmJit are assembling that block.

They are not committed back into `ScriptContext`.

That is the current design on purpose. It avoids rebinding and stale-label issues across enable/disable/re-enable cycles.

## 1.5. CE-Like Assembly Scripts

For multi-origin assembly, `hexengine` now has a higher-level runner:

- [`../include/hexengine/engine/assembly_script.hpp`](../include/hexengine/engine/assembly_script.hpp)
- [`../src/engine/assembly_script.cpp`](../src/engine/assembly_script.cpp)

`AssemblyScript` is not a replacement for `TextAssembler`.

Instead:

- `AssemblyScript` scans a CE-like script
- decides where the current output address is
- splits the source into one or more single-target chunks
- feeds each chunk into its own `TextAssembler`

This keeps AsmJit in the role it is good at:

- one base address
- one label/fixup universe
- one emitted chunk at a time

while still allowing CE-style source like:

```asm
alloc(newmem1, 0x100)
alloc(newmem2, 0x100)

newmem1:
  ret

loopbegin:
  jmp loopbegin

newmem2:
  ret

game.exe+0x100:
  jmp newmem2
```

and scan-driven hook source like:

```asm
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)
label(returnhere)

newmem:
  nop
  jmp returnhere

injection:
  jmp newmem
  nop
  nop
returnhere:
```

## 1.6. Current-Address Rule

`AssemblyScript` treats `expr:` lines with one rule:

- if `expr` already resolves to an address at that point in the scan:
  - start a new chunk at that address
- otherwise:
  - treat it as an internal assembler label in the current chunk

Practical consequences:

- `alloc(newmem1, ...)` followed by `newmem1:` starts a chunk at `newmem1.address`
- `aobScan(...)`, `aobScanModule(...)`, or `aobScanRegion(...)` can create a script label before a later `label:` line is seen
- `game.exe+0x100:` starts a chunk at that resolved module-relative address
- `loopbegin:` inside an active chunk stays an internal AsmTK/AsmJit label
- `label(returnhere)` plus a later `returnhere:` lets `returnhere` cross chunk boundaries without promoting every implicit asm label into script scope

This is why internal loop labels still work without being promoted into script scope.

## 2. ScriptContext Ownership Rules

`ScriptContext` is the source of truth for persistent script-local names.

It currently owns:

- local allocations in `localAllocations_`
- local labels in `labels_`

### Local allocs

Calling:

```cpp
const auto cave = script.alloc({
    .name = "newmem",
    .size = 0x1000,
});
```

does 2 things:

1. allocates memory in the target process
2. inserts a same-name local label:

```text
label "newmem" -> cave.address
```

That means:

- `script.resolveAddress("newmem")` works immediately
- text assembly can reference `newmem` as an external address

### Explicit labels

Calling:

```cpp
script.declareLabel("returnhere");
script.bindLabel("returnhere", hookAddress + 5);
```

creates and binds a persistent script-local label.

That label:

- resolves inside the `ScriptContext`
- survives across later use of the same script context
- can be published globally with `script.registerSymbol("returnhere")`

### Important restriction

Script labels are the names you intentionally persist.

Internal labels from a text assembly block are not script labels unless you explicitly model them that way at a higher layer.

## 3. Symbol Kinds

`SymbolRecord` now carries kind only, not allocation metadata.

Kinds:

- `UserDefined`
- `Label`
- `Allocation`
- `Module`

Practical meaning:

- `Allocation`
  - symbol points at an allocation-backed address
- `Label`
  - symbol was published from a script label
- `Module`
  - symbol came from module lookup
- `UserDefined`
  - explicit generic address publication

Sizes and protections stay on `AllocationRecord`, not on `SymbolRecord`.

## 4. Allocation-Backed Cleanup

Allocations now track the labels and symbols linked to them.

Files:

- [`../include/hexengine/engine/allocation_repository.hpp`](../include/hexengine/engine/allocation_repository.hpp)
- [`../src/engine/script_context.cpp`](../src/engine/script_context.cpp)
- [`../src/engine/engine_session.cpp`](../src/engine/engine_session.cpp)

Each allocation record may contain:

- `linkedLabels`
- `linkedSymbols`

### Local allocation cleanup

For a local alloc:

```cpp
const auto cave = script.alloc({ .name = "newmem", .size = 0x1000 });
script.registerSymbol("newmem");
script.registerSymbol("newmem_alias", cave.address, SymbolKind::Allocation, true);
```

the local allocation tracks:

- label `newmem`
- symbol `newmem`
- symbol `newmem_alias`

Then:

```cpp
script.dealloc("newmem");
```

will:

1. remove linked local labels
2. unregister linked global symbols
3. free the target allocation

So alloc-backed symbol cleanup is now consistent and deterministic.

### Global allocation cleanup

For a global alloc:

```cpp
const auto block = session->globalAlloc({
    .name = "sharedmem",
    .size = 0x1000,
});

session->registerSymbol("sharedmem_alias", block.address, SymbolKind::Allocation, true);
```

`session->deallocate("sharedmem")` will unregister:

- the default global alloc symbol `sharedmem`
- any linked allocation aliases like `sharedmem_alias`

and then free the allocation.

## 5. TextAssembler Resolution Model

`TextAssembler` uses:

- AsmTK for text parsing
- AsmJit for encoding
- `RemoteAssembler` for remote-target emission

### Construction

```cpp
const auto cave = script.alloc({
    .name = "newmem",
    .size = 0x1000,
    .protection = hexengine::core::kReadWriteExecute,
});

hexengine::engine::TextAssembler assembler(script, cave);
```

### Parse-time unknown symbol resolution

When AsmTK sees an identifier it does not already know, `TextAssembler` resolves it in this order:

1. labels declared in the current text block
2. script-local labels
3. session-global symbols and modules

That means:

- `newmem` resolves because local allocs create same-name script labels
- `returnhere` resolves if you explicitly declared/bound it in `ScriptContext`
- `game.exe` resolves as a module through session lookup
- `global_target` resolves if you previously registered it as a global symbol

### What happens to labels declared inside text

Example:

```asm
  je returnhere
  nop
returnhere:
  ret
```

`returnhere` is an assembler-local label.

Current behavior:

- AsmTK/AsmJit resolve it for this pass
- `flush()` verifies it is resolved
- bytes are written to the remote cave
- the label is not copied into `ScriptContext`

So after flush:

- branches in that assembled block are valid
- `script.resolveAddress("returnhere")` does not work unless `returnhere` was already an explicit script label

This is intentional.

## 6. Why Assembler Labels Are Not Committed

Committing assembler labels into script state sounds attractive, but it creates hard lifecycle problems:

- enable -> disable -> enable rebinding conflicts
- deciding whether the second use of a name is a new definition or a reference
- cleanup of labels that point into freed code caves
- stale labels surviving after a script is reassembled at a different address

The current engine avoids those problems by keeping implicit asm labels private to one assembly pass.

That means `hexengine` currently prefers:

- explicit persistent script labels
- explicit alloc-backed local names
- pass-local internal assembler labels

over automatic promotion of all assembler labels into script scope.

`AssemblyScript` follows the same rule.

It may switch chunks at names that already resolve, but it still does not commit newly discovered internal assembler labels into `ScriptContext`.

## 7. Concrete Examples

### Example A: Local alloc used from text assembly

```cpp
const auto cave = script.alloc({
    .name = "newmem",
    .size = 0x1000,
});

TextAssembler assembler(script, cave);
assembler.append(R"(
  mov rax, newmem
  ret
)");
assembler.flush();
```

What happens:

- `alloc("newmem")` creates a bound script label `newmem`
- `newmem` in the text is resolved as an immediate address
- no extra script labels are created by the assembler pass

### Example B: Pure internal assembler labels

```cpp
assembler.append(R"(
loop:
  nop
  jmp loop
)");
assembler.flush();
```

What happens:

- `loop` exists only inside that AsmTK/AsmJit pass
- `loop` is not added to `ScriptContext`
- later `script.resolveAddress("loop")` fails

### Example C: Multi-target script execution

```cpp
#include "hexengine/engine/assembly_script.hpp"

hexengine::engine::AssemblyScript scriptProgram(script);

const auto result = scriptProgram.execute(R"(
alloc(newmem1, 0x100)
alloc(newmem2, 0x100)

newmem1:
  ret

loopbegin:
  jmp loopbegin

newmem2:
  ret

game.exe+0x100:
  jmp newmem2
)");
```

What happens:

- `alloc(...)` directives create the local alloc-backed labels first
- `newmem1:` starts chunk 1
- `loopbegin:` stays private to chunk 1
- `newmem2:` starts chunk 2 because `newmem2` already resolves
- `game.exe+0x100:` starts chunk 3 because the expression resolves
- each chunk is assembled with its own `TextAssembler`

### Example D: Internal label does not become a new target

```cpp
const auto result = scriptProgram.execute(R"(
alloc(newmem1, 0x100)

newmem1:
  ret

newmem2:
  ret
)");
```

Here `newmem2:` is not preexisting, so:

- it stays an internal assembler label
- the whole block assembles as one chunk
- `script.resolveAddress("newmem2")` still fails afterwards

### Example E: Explicit script label published globally

```cpp
script.declareLabel("returnhere");
script.bindLabel("returnhere", hookAddress + 5);

const auto published = script.registerSymbol("returnhere");
```

What happens:

- script-local label `returnhere` persists in the script context
- published symbol kind is `Label`
- session-wide resolution can now see `returnhere`

### Example F: Scan-driven manual hook with `returnhere`

```cpp
hexengine::engine::AssemblyScript program(script);
program.execute(R"(
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)
label(returnhere)

newmem:
  nop
  jmp returnhere

injection:
  jmp newmem
  nop
  nop
returnhere:
)");
```

Why this works:

- `newmem` is alloc-backed and therefore already resolves before `newmem:` is seen
- `label(returnhere)` creates an explicit unbound script label
- `aobScanModule(...)` binds `injection`, so `injection:` starts the hook-site chunk
- `returnhere:` binds that explicit script label after the hook-site chunk is assembled
- the cave chunk can then retry and resolve `returnhere`
- assembled writes use temporary protection changes automatically if needed
- internal labels inside the cave would still stay private to that chunk

## 8. Current Limitations

This is the current implemented model, not a final CE-complete lifecycle design.

Important limitations:

- text-assembly labels do not persist across passes
- there is no automatic "promote this asm label into script scope" feature
- `AssemblyScript` supports a focused CE-like subset, not full Auto Assembler yet
- current directives are:
  - `alloc(...)`
  - `globalAlloc(...)`
  - `dealloc(...)`
  - `aobScan(...)`
  - `aobScanModule(...)`
  - `aobScanRegion(...)`
  - `label(...)`
  - `fullAccess(...)`
  - `createThread(...)`
  - `registerSymbol(...)`
  - `unregisterSymbol(...)`
- if you need a persistent name across enable/disable, it must be:
  - an alloc-backed name
  - an explicit script label
  - or an explicit global symbol

That is the current boundary the engine keeps to stay predictable.

## 9. Related Tests

The current behavior is covered in:

- [`../tests/script_context_test.cpp`](../tests/script_context_test.cpp)
- [`../tests/text_assembler_test.cpp`](../tests/text_assembler_test.cpp)
- [`../tests/memory_integration_test.cpp`](../tests/memory_integration_test.cpp)

The important cases covered there are:

- local alloc auto-creates same-name script label
- alloc-backed symbols use `SymbolKind::Allocation`
- label-backed symbols use `SymbolKind::Label`
- local/global allocation teardown unregisters linked symbols
- internal asm labels stay private and can be reused in later passes
- unresolved assembler labels fail before flush
- CE-like assembly-script chunk splitting keeps known targets and internal labels separate
- assembly scripts fail if they emit code before any current address exists
