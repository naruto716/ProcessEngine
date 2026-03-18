# Lua Runtime

See also: [Wiki Home](README.md) | [Usage](usage.md) | [Assembly And Labels](assembly.md) | [Hooks](hooks.md) | [Testing](testing.md)

`hexengine` now has a separate Lua layer in:

- [`../include/hexengine/lua/lua_runtime.hpp`](../include/hexengine/lua/lua_runtime.hpp)
- [`../src/lua/lua_runtime.cpp`](../src/lua/lua_runtime.cpp)

This layer is intentionally:

- above `EngineSession`
- above `ScriptContext`
- above `AssemblyScript`
- CE-shaped at the Lua API surface

It does not replace the engine core. It orchestrates the existing engine through Lua.

## 1. Runtime Model

`LuaRuntime` owns:

- one Lua 5.4 state
- one dedicated runtime thread
- one persistent Lua environment per script id
- one hidden script context for global runs
- timer state

All Lua execution is serialized on that runtime thread:

- `runGlobal(...)`
- `runScript(...)`
- timer callbacks

That avoids races on the Lua state while still allowing timers to fire in the background.

## 2. Script Globals

The current Lua design deliberately differs from Cheat Engine here.

In this runtime:

- `local x = 1`
  - is local to the current chunk/function, normal Lua behavior
- `x = 1`
  - is stored in the persistent Lua environment for the current script id

So:

```lua
counter = (counter or 0) + 1
```

persists for:

- `runScript("feature.hp", ...)`

but is not visible to:

- `runScript("feature.ammo", ...)`

Global runs still use the shared Lua global table.

## 3. Supported CE-Style Globals

The public Lua surface uses CE-like names rather than custom `session:` / `script:` methods.

Currently supported:

- `getAddress(expr)`
- `getAddressSafe(expr)`
- `registerSymbol(name, address, donotsave?)`
- `unregisterSymbol(name)`
- `AOBScan(pattern, ...)`
- `AOBScanUnique(pattern, ...)`
- `AOBScanModule(moduleName, pattern, ...)`
- `AOBScanModuleUnique(moduleName, pattern, ...)`
- `readBytes(address, count, returnAsTable?)`
- `writeBytes(address, ...)`
- `readSmallInteger(address, signed?)`
- `readWord(address, signed?)`
- `writeSmallInteger(address, value)`
- `writeWord(address, value)`
- `readInteger(address, signed?)`
- `writeInteger(address, value)`
- `readQword(address)`
- `writeQword(address, value)`
- `readPointer(address)`
- `readFloat(address)`
- `writeFloat(address, value)`
- `readDouble(address)`
- `writeDouble(address, value)`
- `readString(address, maxLength, wideChar?)`
- `writeString(address, text, terminate?, wideChar?)`
- `fullAccess(address, size)`
- `autoAssemble(text, targetself?, disableInfo?)`
- `createTimer(...)`
- `sleep(milliseconds)`
- `targetIs64Bit()`
- `getOpenedProcessID()`
- `process`

Notes:

- `writeWord` / `readWord` are convenience aliases over `writeSmallInteger` / `readSmallInteger`
- `process` is the attached process main module name, matching common CE Lua usage
- address parameters accept either:
  - integer addresses
  - CE-style address strings such as `"game.exe+0x1234"` or `"newmem+8"`

## 4. Address Resolution

Lua address resolution uses the current script context first.

So in:

```lua
writeQword("newmem+8", 0x1122334455667788)
```

`newmem` resolves through the current script context if it is a local alloc-backed name or explicit script label.

If the name is not found locally, resolution falls back to:

- global registered symbols
- modules such as `game.exe`

For global Lua runs, the runtime uses a hidden script context id:

- `__lua.global__`

That lets `autoAssemble(...)` and later `getAddress(...)` calls share the same engine-side local names even outside a named script.

## 5. `autoAssemble(...)`

`autoAssemble(...)` delegates directly to the existing `AssemblyScript` engine layer.

That means the current AA behavior from native code also applies in Lua:

- `alloc(...)`
- `globalAlloc(...)`
- `dealloc(...)`
- `aobScan(...)`
- `aobScanModule(...)`
- `aobScanRegion(...)`
- `label(...)`
- `registerSymbol(...)`
- `unregisterSymbol(...)`
- `fullAccess(...)`
- `createThread(...)`

Example:

```lua
local ok, err = autoAssemble([[
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
]])

if not ok then
  error(err)
end
```

Current return shape:

- success:
  - `true`
- failure:
  - `false, errorMessage`

Current limitations:

- `targetself=true` is not supported
- `disableInfo` is not supported

## 6. `AOBScan` Results

`AOBScan(...)` and `AOBScanModule(...)` return a CE-style `StringList` userdata.

Supported members:

- `Count`
- `destroy()`
- `hits[0]`, `hits[1]`, ...

The entries are uppercase hex strings without a `0x` prefix.

Example:

```lua
local hits = AOBScan("41 42 13 37 C0 DE 7A E1")
if hits then
  print(hits.Count)
  print(hits[0])
  hits.destroy()
end
```

## 7. Timers

The current timer surface is intentionally small but CE-shaped:

- `createTimer()`
- `createTimer(enabled)`
- `createTimer(interval, callback)` for the one-shot helper form documented by CE

Timer userdata supports:

- `timer.Interval`
- `timer.Enabled`
- `timer.OnTimer`
- `timer.destroy()`

Example:

```lua
count = 0

timer = createTimer(false)
timer.Interval = 25
timer.OnTimer = function(sender)
  count = count + 1
  if count >= 10 then
    sender.Enabled = false
  end
end
timer.Enabled = true
```

One-shot helper:

```lua
createTimer(50, function()
  writeBytes("game.exe+0x1234", 0x90, 0x90)
end)
```

Current behavior:

- timer callbacks run on the Lua runtime thread
- they are serialized with normal `runGlobal(...)` / `runScript(...)` execution
- a timer callback error disables that timer and logs the error to `std::clog`
- destroying a script through `LuaRuntime::destroyScript(...)` cancels that script's timers

This is close to CE’s “single Lua execution thread” behavior even though the host implementation is different.

## 8. Script Destruction

`LuaRuntime::destroyScript(scriptId)` does 3 things:

1. cancels that script's timers
2. frees that script's local allocations through its `ScriptContext`
3. drops the persistent Lua environment for that script id

So after destroy:

- script globals are gone
- alloc-backed names are gone
- linked alloc-backed published symbols are unregistered

## 9. Current Limitations

The Lua runtime is intentionally constrained to the engine features that already exist.

Not implemented yet:

- CE’s full Lua object model
- form / UI objects
- owner-based `createTimer(owner, enabled)` semantics
- `createThreadNewState(...)`
- CE’s broader thread APIs
- `autoAssembleCheck(...)`
- `targetself` / `disableInfo` handling in `autoAssemble(...)`
- a full local-process CE API mirror

The engine-side AA limitations also still apply:

- implicit asm labels stay chunk-local
- explicit `label(returnhere)` is still the cross-chunk label path
- hook restore remains manual when patching raw sites through `autoAssemble(...)`

## 10. Tested Behavior

Lua-specific coverage is currently in:

- [`../tests/lua_runtime_test.cpp`](../tests/lua_runtime_test.cpp)
- [`../tests/memory_integration_cases.cpp`](../tests/memory_integration_cases.cpp)

The tests cover:

- script-scoped global persistence
- isolation between script ids
- CE-style typed memory reads/writes including `writeWord` and `writeQword`
- `AOBScan(...)` result handling through `StringList`
- `autoAssemble(...)` manual hook flow
- repeating timers
- one-shot timer helper
- real-process Lua hook assembly and timer writes through the Win32 backend
