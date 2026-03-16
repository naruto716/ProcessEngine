# Usage

See also: [Wiki Home](README.md) | [Architecture](architecture.md) | [Pointers](pointers.md) | [Patching](patching.md)

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
session->registerSymbol("player_base", address, size);
```

Resolve:

```cpp
auto symbol = session->resolveSymbol("player_base");
```

Unregister:

```cpp
session->unregisterSymbol("player_base");
```

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
- `registerSymbol(name)` explicitly publishes a local name globally
- deallocating a local alloc does not unregister published global symbols; `hexengine` warns, but stays CE-compatible
- related scripts should reuse the same script-context id if they need shared local names

Labels can be declared and bound directly on the script context:

```cpp
script.declareLabel("returnhere");
script.bindLabel("returnhere", hookAddress + 5);
auto returnAddress = script.resolveAddress("returnhere");
script.registerSymbol("returnhere");  // publishes the label globally
```

Labels are script-scoped — they persist as long as the `ScriptContext` exists, just like local allocations. Labels shadow alloc names and global names when resolving addresses within the script context.

## 8. Change Protection

```cpp
session->fullAccess(address, size);
```

Or use the backend directly if you want specific protection flags:

```cpp
session->process().protect(address, size, hexengine::core::ProtectionFlags::Read | hexengine::core::ProtectionFlags::Write);
```

## 9. Copy Bytes With `readMem`

`readMem` is the CE-style byte-copy helper:

```cpp
session->readMem(sourceAddress, destinationAddress, 16);
```

In `hexengine`, this means:

- read `size` bytes from `sourceAddress`
- write those bytes to `destinationAddress`
- temporarily make the destination writable if needed
- restore the previous protection after the write

## 10. Execute Remote Code

`executeCode` is the engine-level "run this entrypoint inside the target process" operation:

```cpp
session->executeCode(entryAddress);
```

At the common engine interface level, this only means:

- `entryAddress` is executable code in the target process
- ask the backend to run that code asynchronously

The backend decides how to do that. In the Win32 backend, this is implemented with `CreateRemoteThread`.

## 11. Apply And Restore Patches

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

    session->registerSymbol("player_base", cave.address, cave.size);
    session->fullAccess(hookAddress, 16);

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
