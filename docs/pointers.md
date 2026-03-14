# Pointers

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Architecture](architecture.md)

This page explains CE-style address expressions and multilevel pointer-chain support in `hexengine`.

## What Exists

The engine provides:

- CE-style address expression resolution
- base-address plus offset-chain resolution
- typed pointer-chain reads
- CE-style string wrappers for the useful subset of address expressions

Relevant code:

- [`../include/hexengine/engine/address_resolver.hpp`](../include/hexengine/engine/address_resolver.hpp)
- [`../src/engine/address_resolver.cpp`](../src/engine/address_resolver.cpp)
- [`../include/hexengine/engine/pointer_resolver.hpp`](../include/hexengine/engine/pointer_resolver.hpp)
- [`../src/engine/pointer_resolver.cpp`](../src/engine/pointer_resolver.cpp)
- [`../include/hexengine/engine/engine_session.hpp`](../include/hexengine/engine/engine_session.hpp)

## Address Expressions

Example:

```cpp
auto hookAddress = session->resolveAddress("game.exe+0x1234-0x20");
auto playerBase = session->resolveAddress("[[player_base+0x10]+0x30]");
```

Supported pieces:

- module names
- registered symbols
- hex literals
- nested `[...]` dereference chains
- `+` and `-` offsets

This is intentionally a useful subset, not a full AA expression parser.

## Template-Style Resolution

Example:

```cpp
auto finalAddress = session->resolvePointer(baseAddress, 0x18, 0x30);
auto value = session->readPointerValue<std::uint32_t>(baseAddress, 0x18, 0x30);
```

The chain semantics are:

```text
current = baseAddress
current = *(pointer*)current + 0x18
current = *(pointer*)current + 0x30
return current
```

That means `resolvePointer(...)` returns the final address, not the final value. Use `readPointerValue<T>(...)` if you want the typed value at that final address.

## CE-Style Wrapper

Example:

```cpp
auto finalAddress = session->resolveAddress("[[game.exe+0x123]+0x18]+0x30");
auto value = session->process().readValue<std::uint32_t>(finalAddress);
```

String expressions are resolved by `AddressResolver`. `PointerResolver` remains the explicit helper for base-address plus offset-chain walking.

Supported pieces:

- module names
- registered symbols
- hex literals
- nested `[...]` dereference chains
- `+` and `-` offsets

This is intentionally a useful subset, not a full AA expression parser.

## Important Behavior

### Pointer size follows the target

Pointer walking uses the target process pointer size reported by the backend, not just the host process pointer size.

That matters for future mixed-bitness support and is part of [`IProcessBackend`](../include/hexengine/backend/process_backend.hpp).

### Bare numbers are hex

The string wrapper treats bare numeric tokens as hexadecimal.

Examples:

- `1234`
- `0x1234`

both resolve as hex literals.

### Expression scope

The string wrapper resolves names in this order:

1. hex literal
2. registered symbol
3. module name

If none match, resolution fails.

### Subtraction and hyphenated names

The resolver supports subtraction in CE-style strings:

```cpp
auto address = session->resolveAddress("game.exe+0x1234-0x20");
```

Hyphenated symbol names are still handled correctly. The parser first tries to resolve the whole token, then treats `-` as subtraction only if the full token is not a known symbol or module.

## What This Is Not

This is not:

- a Cheat Engine pointer scanner
- a full AA expression parser
- a replacement for complex CE address syntax

It is the engine-side helper for the useful pointer-chain cases trainer features normally need.
