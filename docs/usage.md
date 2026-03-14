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

Direct scanner access is also available if you need to pass an explicit `BytePattern` or address range.

## 4. Resolve Pointer Chains

Template-style chain:

```cpp
auto finalAddress = session->resolvePointer(baseAddress, 0x18, 0x30);
auto value = session->readPointerValue<std::uint32_t>(baseAddress, 0x18, 0x30);
```

CE-style wrapper:

```cpp
auto finalAddress = session->resolvePointer("[[game.exe+0x123]+0x18]+0x30");
auto value = session->readPointerValue<std::uint32_t>("[[game.exe+0x123]+0x18]+0x30");
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

## 6. Allocate Memory

```cpp
using namespace hexengine;

const auto block = session->allocate(engine::AllocationRequest{
    .name = "myCodecave",
    .size = 0x1000,
    .protection = core::kReadWriteExecute,
    .scope = engine::AllocationScope::Local,
    .nearAddress = hookAddress,
});
```

Release it later:

```cpp
session->deallocate("myCodecave");
```

## 7. Change Protection

```cpp
session->fullAccess(address, size);
```

Or use the backend directly if you want specific protection flags:

```cpp
session->process().protect(address, size, hexengine::core::ProtectionFlags::Read | hexengine::core::ProtectionFlags::Write);
```

## 8. Apply And Restore Patches

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
    session->applyPatch("demo.patch", hookAddress, patch);

    (void)healthAddress;
    return 0;
}
```
