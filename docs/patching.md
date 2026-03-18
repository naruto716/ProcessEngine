# Patching

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Hooks](hooks.md) | [Architecture](architecture.md) | [Testing](testing.md)

This page covers the current patch API in `hexengine`.

## Mental Model

Cheat Engine patching is usually written as:

- `[ENABLE]` write replacement bytes
- `[DISABLE]` write the original bytes back
- optionally `assert(...)` first

`hexengine` packages that into a session-scoped patch service:

- apply a named patch
- store the original bytes automatically
- restore the patch later by name

The patch state lives only inside the current `EngineSession`.

Relevant code:

- [`../include/hexengine/engine/patch_repository.hpp`](../include/hexengine/engine/patch_repository.hpp)
- [`../include/hexengine/engine/patch_service.hpp`](../include/hexengine/engine/patch_service.hpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)
- [`../src/engine/patch_service.cpp`](../src/engine/patch_service.cpp)

## Apply A Byte Patch

Use the `EngineSession` convenience wrapper for normal byte replacement:

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

session->applyPatch("player.hp.freeze", hookAddress, replacement, expected);
```

What happens internally:

1. read the current bytes from the target
2. compare them to `expected` if you supplied it
3. temporarily make the target range writable if needed
4. write the replacement bytes
5. store the original bytes in the patch repository

## Restore A Patch

```cpp
session->restorePatch("player.hp.freeze");
```

That writes the saved original bytes back and removes the patch record from the session.

## Apply A NOP Patch

For a common CE-style `db 90 90 90 ...` patch, use the dedicated helper:

```cpp
session->applyNopPatch("player.hp.nop", hookAddress, 6);
```

Or with an expected-byte precondition:

```cpp
session->applyNopPatch("player.hp.nop", hookAddress, 6, expectedBytes);
```

This writes `0x90` `size` times and still stores the original bytes for restore.

## Use The Service Directly

If you want more explicit control, call the service through the session:

```cpp
hexengine::engine::PatchRequest request{
    .name = "player.hp.patch",
    .address = hookAddress,
    .replacement = std::vector<std::byte>{
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
    },
    .expected = std::vector<std::byte>{
        std::byte{0x89},
        std::byte{0x54},
        std::byte{0x24},
        std::byte{0x10},
    },
};

const auto record = session->patches().apply(request);
```

`PatchRecord` tells you:

- patch name
- target address
- original bytes
- replacement bytes
- patch kind

## Protection Handling

You do not need to call `fullAccess()` before every patch.

`PatchService` checks the target region:

- if it is already writable, it writes directly
- if it is not writable, it temporarily changes protection
- after the write, it restores the previous protection

This is tested in the real integration test at [`../tests/memory_integration_test.cpp`](../tests/memory_integration_test.cpp).

## Current Scope

The current patch layer supports:

- named byte patches
- expected-byte verification
- NOP patches
- restore by patch name
- automatic temporary write access

It does not yet support:

- multi-write transactions
- hook-site patch tracking for `AssemblyScript`
- automatic hook installation
- instruction relocation
- `reassemble`-style relocation

So if you use `AssemblyScript` to write directly into a hook site, patch restore is still manual. See [Hooks](hooks.md).
