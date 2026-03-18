# Hooks

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Assembly And Labels](assembly.md) | [Patching](patching.md) | [Testing](testing.md)

This page documents the **current CE-style hook workflow** that `hexengine` supports today.

The intended use case is:

- you already know how the hook should look from CE, x64dbg, IDA, or your own reversing
- you import that manual logic into `AssemblyScript`
- the script itself can do the scan, cave alloc, and hook-site patch

This is not a "click one address and auto-build a trampoline" system. It is a runtime for the manual hook scripts people normally author first.

## 1. What Is Supported

You already have the pieces for a normal CE-style hook script:

- `aobScan(...)`, `aobScanModule(...)`, `aobScanRegion(...)` inside `AssemblyScript`
- `label(...)` inside `AssemblyScript`
- optional `fullAccess(...)` inside `AssemblyScript`
- `ScriptContext`
- local `alloc(...)`
- `registerSymbol(...)`
- explicit script labels like `returnhere`
- `AssemblyScript` for multi-target assembly
- `TextAssembler` / AsmTK / AsmJit for the actual encoding

This is enough for scripts written like:

```asm
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)

newmem:
  ; manually preserved / manually authored code
  jmp returnhere

injection:
  jmp newmem
```

## 2. What Is Not Supported

`hexengine` is not trying to invent hook logic for you.

You still decide:

- which AOB pattern identifies the hook site
- how many bytes are overwritten
- which original instructions must be retyped in the cave
- what the return address is
- how disable restores the site

That is exactly the normal manual-hook workflow.

## 3. Recommended Hook Flow

### Step 1: Keep the hook-site length explicit in the script

```cpp
const auto hits = session->aobScanModule("game.exe", "41 42 13 37 C0 D? 7A E1 5B AD F0 0D 55 AA 11 99");
if (hits.empty()) {
    throw std::runtime_error("Hook pattern not found");
}

const auto hookAddress = hits.front();
auto& script = session->createScriptContext("feature.hp_hook");
```

The current tested pattern is to declare `returnhere` explicitly in the script:

- `label(returnhere)` marks it as a cross-chunk script label
- `returnhere:` later binds it at the end of the hook-site chunk
- the cave chunk can defer until that label is known

### Step 2: Let the script scan and patch

This is the current tested `AssemblyScript` shape:

Example:

```cpp
#include "hexengine/engine/assembly_script.hpp"

hexengine::engine::AssemblyScript program(script);

const auto result = program.execute(R"(
aobScanModule(injection, game.exe, 41 42 13 37 C0 DE 7A E1)
alloc(newmem, 0x100, injection)
label(returnhere)
registerSymbol(newmem)

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

What happens:

- `aobScanModule(...)` binds `injection` into script scope
- `alloc(newmem, ...)` creates the cave and a same-name script label
- `label(returnhere)` creates an explicit cross-chunk script label in the unbound state
- `newmem:` starts the cave chunk
- the cave chunk initially defers because `returnhere` is not bound yet
- `injection:` starts the hook-site chunk because `injection` already resolves
- `returnhere:` binds the explicit script label to the end of the hook-site chunk
- the deferred cave chunk retries and now resolves `returnhere` through `ScriptContext`
- `jmp newmem` resolves through the script-local alloc-backed label
- the assembled cave and patch writes use temporary protection changes automatically if the target page is not writable

The `registerSymbol(newmem)` line is optional. Use it if you want `newmem` visible globally.

`fullAccess(...)` is still available when you explicitly want the site to stay RWX after the write, but it is no longer required just to let `AssemblyScript` patch the site.

### Step 3: Restore on disable

Because the hook-site write is assembled directly, disable is still manual.

Typical disable flow:

1. write the original hook-site bytes back
2. deallocate the cave
3. destroy the script context if you no longer need it

Example:

```cpp
const auto originalHookBytes = session->process().read(hookAddress, kOverwriteLength);

// ... enable hook ...

session->process().write(hookAddress, originalHookBytes);
script.dealloc("newmem");
session->destroyScriptContext("feature.hp_hook");
```

So right now:

- hook-site restore is manual
- cave cleanup is handled through `ScriptContext::dealloc(...)`
- linked alloc-backed symbols are removed automatically when the cave is deallocated

## 4. Preserving Original Instructions

For manual hooks, the expected workflow is still:

- inspect the original code yourself
- copy the original instructions into the cave manually as assembly text
- then jump back to `returnhere`

Example:

```asm
newmem:
  movss xmm0,[rax+30]
  addss xmm0,[rbx+34]
  jmp returnhere
```

This is fine.

What is **not** fine is expecting the engine to steal arbitrary instructions from the hook site and relocate them automatically. That is not implemented.

## 5. Safe Mental Model

Treat the current hook pipeline as:

- **CE-style assembly-script scheduler**
- **remote code emitter**
- **manual hook authoring runtime**

Do **not** treat it as:

- a general-purpose auto-hook installer for arbitrary addresses
- a raw-byte relocator
- a full `reassemble(...)` equivalent

That distinction matters.

## 6. End-to-End Example

```cpp
const auto hits = session->aobScanModule("game.exe", "41 42 13 37 C0 D? 7A E1 5B AD F0 0D 55 AA 11 99");
if (hits.empty()) {
    throw std::runtime_error("Hook pattern not found");
}

const auto hookAddress = hits.front();
const auto originalBytes = session->process().read(hookAddress, 8);

auto& script = session->createScriptContext("feature.hp_hook");

hexengine::engine::AssemblyScript program(script);
program.execute(R"(
aobScanModule(injection, game.exe, 41 42 13 37 C0 D? 7A E1 5B AD F0 0D 55 AA 11 99)
alloc(newmem, 0x1000, injection)
label(returnhere)
registerSymbol(newmem)

newmem:
  nop
  jmp returnhere

injection:
  jmp newmem
  nop
  nop
returnhere:
)");

// ... later, disable ...
session->process().write(hookAddress, originalBytes);
script.dealloc("newmem");
session->destroyScriptContext("feature.hp_hook");
```

Direct `game.exe+0x123456:` patch-site chunks are still supported, but the real workflow to optimize around is the scan-first script above.

If you need a non-assembly safe write from host code, use `EngineSession::writeBytes(...)`, which applies the same temporary-protection policy.

## 7. `createThread(...)` For Hook Helpers And Stubs

`AssemblyScript` now also supports:

```asm
createThread(worker)
```

This is useful for:

- one-shot helper stubs
- bootstrap code
- thread-based side effects that are easier to express in assembly than with direct memory writes

Current behavior:

- any active chunk is flushed first
- the entry expression is resolved through the script/session resolver
- `EngineSession::executeCode(...)` is called with that address

Minimal example:

```cpp
std::ostringstream scriptSource;
scriptSource << "alloc(worker, 0x100)\n"
             << "worker:\n"
             << "  mov rax, " << std::showbase << std::hex << writableAddress << "\n"
             << "  mov byte ptr [rax], 0x5B\n"
             << "  xor eax, eax\n"
             << "  ret\n"
             << "createThread(worker)\n";

hexengine::engine::AssemblyScript program(script);
program.execute(scriptSource.str());
```

The integration test covers this path against the real Win32 backend.

## 8. Current Testing Coverage

Manual hook-style behavior is currently covered in:

- [`../tests/assembly_script_test.cpp`](../tests/assembly_script_test.cpp)
- [`../tests/memory_integration_cases.cpp`](../tests/memory_integration_cases.cpp)

Those tests verify:

- `aobScanModule(...)` inside `AssemblyScript`
- near cave allocation
- explicit `label(returnhere)` cross-chunk binding
- cave assembly
- hook-site jump assembly
- jump target correctness
- symbol publication for the cave
- cleanup of the cave and linked symbols
- failure when `label(returnhere)` is declared but never defined
- failure when `returnhere:` is only implicit and therefore remains chunk-local
- `createThread(...)` flushing and executing a worker stub
- `fullAccess(...)` still working as an explicit directive when callers do want persistent RWX
