# Usage

See also: [Wiki Home](README.md) | [Architecture](architecture.md) | [Assembly And Labels](assembly.md) | [Hooks](hooks.md) | [Pointers](pointers.md) | [Patching](patching.md)

This page shows the normal way to use `hexengine` from native application code.

## 1. Open A Session

```cpp
#include "hexengine/engine/win32_engine_factory.hpp"

hexengine::engine::Win32EngineFactory factory;
auto session = factory.open(pid);
```

Relevant code:

- [`../include/hexengine/engine/win32_engine_factory.hpp`](../include/hexengine/engine/win32_engine_factory.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)

## 2. Read And Write Memory

Low-level process access is still available through the backend owned by the session:

```cpp
auto bytes = session->process().read(address, size);
session->process().write(address, bytesToWrite);
auto region = session->process().query(address);
```

Use this for direct operations that do not need higher-level helpers.

## 3. Scan For Patterns

Full-process scan:

```cpp
auto hits = session->aobScan("48 8B ?? ?? ?? 89");
```

Module-limited scan:

```cpp
auto hits = session->aobScanModule("game.exe", "48 8B ?? ?? ?? 89");
```

Region-limited scan:

```cpp
auto hits = session->aobScanRegion(moduleBase, moduleBase + moduleSize, "48 8B ?? ?? ?? 89");
```

`aobScanRegion(start, stop, pattern)` uses a half-open range: `start` is included and `stop` is exclusive.

Direct scanner access is also available if you need to pass an explicit `BytePattern`.

## 4. Resolve Addresses And Pointer Chains

Direct CE-style address resolution:

```cpp
auto hookAddress = session->resolveAddress("game.exe+0x1234-0x20");
auto playerBase = session->resolveAddress("[[player_base+0x10]+0x30]");
```

Template-style chain:

```cpp
auto finalAddress = session->resolvePointer(baseAddress, 0x18, 0x30);
auto value = session->readPointerValue<std::uint32_t>(baseAddress, 0x18, 0x30);
```

CE-style wrapper:

```cpp
auto finalAddress = session->resolveAddress("[[game.exe+0x123]+0x18]+0x30");
auto value = session->process().readValue<std::uint32_t>(finalAddress);
```

For more detail, see [Pointers](pointers.md).

## 5. Manage Symbols

Register:

```cpp
session->registerSymbol("player_base", address);
```

Resolve:

```cpp
auto symbol = session->resolveSymbol("player_base");
```

Unregister:

```cpp
session->unregisterSymbol("player_base");
```

Current symbol kinds are:

- `UserDefined`
- `Label`
- `Allocation`
- `Module`

Symbols carry only publication metadata:

- name
- address
- kind
- persistence flag

Allocation size and protection live on allocation records, not on symbols.

## 6. Global Alloc From The Session

```cpp
using namespace hexengine;

const auto block = session->globalAlloc(engine::AllocationRequest{
    .name = "sharedmem",
    .size = 0x1000,
    .protection = core::kReadWriteExecute,
    .nearAddress = hookAddress,
});
```

Release it later:

```cpp
session->deallocate("sharedmem");
```

Use `globalAlloc(...)` for CE-style `globalalloc` semantics.
`globalAlloc(...)` publishes the allocation name as a session-global symbol.
Global allocations also track linked allocation aliases so `deallocate(...)` can unregister them consistently.

## 7. Script-Local Allocations And Labels

Use a `ScriptContext` when you need CE-style local names shared by related scripts such as enable/disable:

```cpp
auto& script = session->createScriptContext("feature.infinite_health");

const auto local = script.alloc(hexengine::engine::AllocationRequest{
    .name = "newmem",
    .size = 0x1000,
    .protection = hexengine::core::kReadWriteExecute,
    .nearAddress = hookAddress,
});

auto localAddress = script.resolveAddress("newmem");
script.registerSymbol("newmem");
```

Important behavior:

- script-local alloc names resolve only inside that `ScriptContext`
- local allocs also create a same-name script label immediately
- `registerSymbol(name)` explicitly publishes a local name globally
- deallocating a local alloc removes linked local labels and unregisters linked alloc-backed global symbols
- related scripts should reuse the same script-context id if they need shared local names

Labels can be declared and bound directly on the script context:

```cpp
script.declareLabel("returnhere");
script.bindLabel("returnhere", hookAddress + 5);
auto returnAddress = script.resolveAddress("returnhere");
script.registerSymbol("returnhere");  // publishes the label globally
```

Labels are script-scoped: they persist as long as the `ScriptContext` exists, just like local allocations. Labels shadow alloc names and global names when resolving addresses within the script context.

If you publish a script label with `registerSymbol(name)`, the resulting global symbol uses `SymbolKind::Label`.

`AssemblyScript` also supports an explicit `label(name)` directive for cross-chunk labels such as CE-style `returnhere`.

## 8. Assemble Remote Code

`TextAssembler` is the current text-assembly facade over AsmTK + AsmJit:

```cpp
#include "hexengine/engine/text_assembler.hpp"

const auto cave = script.alloc(hexengine::engine::AllocationRequest{
    .name = "newmem",
    .size = 0x1000,
    .protection = hexengine::core::kReadWriteExecute,
});

hexengine::engine::TextAssembler assembler(script, cave);
assembler.append(R"(
  mov rax, newmem
loop:
  nop
  jmp loop
)");
assembler.flush();
```

Important behavior:

- local alloc names like `newmem` resolve through the script context
- published symbols and modules resolve through the session
- labels declared only in the assembly text, such as `loop:`, stay private to that assembly pass
- `flush()` rejects unresolved assembler labels before writing bytes

If you need a label to persist across enable/disable style flows, do not rely on an implicit asm label. Use:

- an alloc-backed name like `newmem`
- an explicit script label via `declareLabel(...)` / `bindLabel(...)`
- or an explicit global symbol via `registerSymbol(...)`

For the detailed ownership model, see [Assembly And Labels](assembly.md).

## 9. Run A CE-Like Multi-Target Assembly Script

If you want CE-style "current address" behavior across multiple targets, use `AssemblyScript`:

```cpp
#include "hexengine/engine/assembly_script.hpp"

hexengine::engine::AssemblyScript program(script);

const auto result = program.execute(R"(
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

Current behavior:

- `alloc(...)` and `globalAlloc(...)` directives create names before later segment headers are resolved
- a line like `newmem1:` starts a new chunk only if `newmem1` already resolves at that point
- a brand-new line like `loopbegin:` inside an active chunk stays an internal assembler label
- each resulting chunk is assembled with its own `TextAssembler`
- implicit assembler labels still do not become persistent script labels

Supported script directives today:

- `alloc(name[, size[, nearAddress]])`
- `globalAlloc(name[, size[, nearAddress]])`
- `dealloc(name)`
- `aobScan(name, pattern)`
- `aobScanModule(name, module, pattern)`
- `aobScanRegion(name, start, stop, pattern)`
- `label(name)`
- `fullAccess(address, size)`
- `createThread(entry)`
- `registerSymbol(name)`
- `unregisterSymbol(name)`

If `size` is omitted for `alloc(...)` or `globalAlloc(...)`, the current default is `0x1000`.

## 10. Build A Manual Hook

The current hook workflow is CE-like and scan-driven:

1. find the hook site
2. decide the overwrite length yourself
3. declare `label(returnhere)` in the script
4. let `AssemblyScript` scan the site, allocate a cave near it, and patch it
5. restore the hook site manually on disable

Minimal example:

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

// disable
session->process().write(hookAddress, originalBytes);
script.dealloc("newmem");
session->destroyScriptContext("feature.hp_hook");
```

Important current limitation:

- direct raw-site assembly through `AssemblyScript` does not create a `PatchRecord`
- it also does not automatically preserve original bytes or validate expected bytes
- hook-site restore is still manual today
- the assembled write itself now handles temporary page-protection changes automatically, so `fullAccess(...)` is optional rather than required

For the full model and caveats, see [Hooks](hooks.md).

## 11. Safe Writes Without Manual `fullAccess`

If you need to write bytes directly from host code without manually changing protection first, use:

```cpp
session->writeBytes(address, bytes);
```

Current behavior:

- if the target region is already writable, it writes directly
- otherwise it temporarily makes the range writable
- after the write, it restores the previous protection

`AssemblyScript` patch writes and `readMem(...)` use the same temporary-protection policy.

## 12. Run A Thread Stub From `AssemblyScript`

`AssemblyScript` can execute a resolved entrypoint with:

```asm
createThread(worker)
```

Example:

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

Current behavior:

- the active chunk is flushed first
- `worker` resolves through the script context
- `EngineSession::executeCode(...)` is called with that address

## 13. Change Protection

```cpp
session->fullAccess(address, size);
```

Or use the backend directly if you want specific protection flags:

```cpp
session->process().protect(address, size, hexengine::core::ProtectionFlags::Read | hexengine::core::ProtectionFlags::Write);
```

## 14. Copy Bytes With `readMem`

`readMem` is the CE-style byte-copy helper:

```cpp
session->readMem(sourceAddress, destinationAddress, 16);
```

In `hexengine`, this means:

- read `size` bytes from `sourceAddress`
- write those bytes to `destinationAddress`
- temporarily make the destination writable if needed
- restore the previous protection after the write

## 15. Execute Remote Code

`executeCode` is the engine-level "run this entrypoint inside the target process" operation:

```cpp
session->executeCode(entryAddress);
```

At the common engine interface level, this only means:

- `entryAddress` is executable code in the target process
- ask the backend to run that code asynchronously

The backend decides how to do that. In the Win32 backend, this is implemented with `CreateRemoteThread`.

## 16. Apply And Restore Patches

Byte patch:

```cpp
const std::array<std::byte, 4> replacement{
    std::byte{0x90},
    std::byte{0x90},
    std::byte{0x90},
    std::byte{0x90},
};

const std::array<std::byte, 4> expected{
    std::byte{0x89},
    std::byte{0x54},
    std::byte{0x24},
    std::byte{0x10},
};

session->applyPatch("player.hp.patch", hookAddress, replacement, expected);
```

NOP patch:

```cpp
session->applyNopPatch("player.hp.nop", hookAddress, 6);
```

Restore:

```cpp
session->restorePatch("player.hp.patch");
session->restorePatch("player.hp.nop");
```

## Minimal Example

```cpp
#include <array>
#include <cstddef>

#include "hexengine/engine/allocation_repository.hpp"
#include "hexengine/engine/win32_engine_factory.hpp"

int main() {
    using namespace hexengine;

    engine::Win32EngineFactory factory;
    auto session = factory.open(/* pid */ 1234);

    const auto hits = session->aobScanModule("game.exe", "48 8B ?? ?? ?? 89");
    if (hits.empty()) {
        return 1;
    }

    const auto hookAddress = hits.front();
    const auto healthAddress = session->resolvePointer(hookAddress, 0x18, 0x30);

    auto& script = session->createScriptContext("demo.feature");
    const auto cave = script.alloc(engine::AllocationRequest{
        .name = "newmem",
        .size = 0x1000,
        .protection = core::kReadWriteExecute,
        .nearAddress = hookAddress,
    });

    session->registerSymbol("player_base", cave.address);

    const std::array<std::byte, 5> patch{
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
    };
    session->applyPatch("demo.patch", hookAddress, patch);

    (void)healthAddress;
    return 0;
}
```
