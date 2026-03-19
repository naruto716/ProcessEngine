# Desktop Host Architecture

See also: [Wiki Home](README.md) | [Architecture](architecture.md) | [Usage](usage.md) | [Lua Runtime](lua.md) | [Testing](testing.md)

This page documents the current desktop presentation stack in detail.

It explains:

- how the C++ engine is exposed to managed code
- how the WPF shell is structured
- how the React + MUI frontend is hosted
- how WebView2 is handled
- how the one-EXE publish model works
- what happens when WebView2 Runtime is missing

## 1. System Map

The current desktop stack is:

```text
React + MUI frontend
  -> WebView2
      -> WPF shell
          -> NativeHexBridge (P/Invoke)
              -> hexengine_host.dll (C ABI)
                  -> LuaRuntime
                      -> EngineSession
                          -> Win32 backend
```

The key point is that the UI never talks directly to C++ objects.

The boundaries are:

- **web UI** talks to **WPF**
- **WPF** talks to **C ABI**
- **C ABI** talks to **LuaRuntime / EngineSession**

That keeps UI concerns, interop concerns, and runtime concerns separate.

Relevant files:

- [`../host/HexEngine.WebViewTest/HexEngine.WebViewTest.csproj`](../host/HexEngine.WebViewTest/HexEngine.WebViewTest.csproj)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml`](../host/HexEngine.WebViewTest/MainWindow.xaml)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml.cs`](../host/HexEngine.WebViewTest/MainWindow.xaml.cs)
- [`../host/HexEngine.WebViewTest/NativeHexBridge.cs`](../host/HexEngine.WebViewTest/NativeHexBridge.cs)
- [`../include/hexengine/host/host_api.h`](../include/hexengine/host/host_api.h)
- [`../src/host/host_api.cpp`](../src/host/host_api.cpp)
- [`../include/hexengine/lua/lua_runtime.hpp`](../include/hexengine/lua/lua_runtime.hpp)
- [`../src/lua/lua_runtime.cpp`](../src/lua/lua_runtime.cpp)

## 2. Native Runtime

The desktop host does not reimplement engine behavior.

It uses the same native layers already documented elsewhere:

- `EngineSession`
- `ScriptContext`
- `AssemblyScript`
- `TextAssembler`
- `LuaRuntime`

So when the desktop host says “run native Lua”, it is calling the real native `LuaRuntime`, not a managed reimplementation.

## 3. Native Bridge DLL

The C ABI lives in:

- [`../include/hexengine/host/host_api.h`](../include/hexengine/host/host_api.h)
- [`../src/host/host_api.cpp`](../src/host/host_api.cpp)

The bridge exports:

- `hexengine_host_get_current_process_id`
- `hexengine_host_create_runtime_for_pid`
- `hexengine_host_destroy_runtime`
- `hexengine_host_run_global`
- `hexengine_host_run_script`
- `hexengine_host_destroy_script`
- `hexengine_host_get_last_error`
- `hexengine_host_free_string`

### Why a C ABI

The project uses a C ABI because:

- C# should not bind directly to a C++ ABI
- handles are easier to manage than raw C++ object lifetimes
- UTF-8 string marshalling is straightforward
- the bridge can be reused by other future hosts

### Runtime ownership inside the bridge

The native bridge currently owns:

```text
RuntimeState
- unique_ptr<EngineSession>
- unique_ptr<LuaRuntime>
```

So one managed runtime handle maps to:

- one attached process session
- one native Lua runtime
- one set of script environments inside that Lua runtime

## 4. Managed Bridge

The managed wrapper is:

- [`../host/HexEngine.WebViewTest/NativeHexBridge.cs`](../host/HexEngine.WebViewTest/NativeHexBridge.cs)

Responsibilities:

- load the native bridge DLL
- wrap the runtime handle in a `SafeHandle`
- marshal UTF-8 strings
- expose `RunGlobal`, `RunScript`, and `DestroyScript`
- support both development-time DLL probing and published single-file loading

### Current load strategy

In development, it probes:

- `HEXENGINE_HOST_DLL`
- the app output directory
- `build\msvc-debug\Debug\hexengine_host.dll`
- `build\msvc-release\Release\hexengine_host.dll`

If none of those explicit development paths are present, the resolver returns `IntPtr.Zero`.

That deliberately lets the normal .NET bundled-native resolution take over for the single-file publish path.

So the load model is:

- **development**: explicit path probing
- **published app**: bundled/default native resolution

## 5. WPF Shell

The WPF host is now the real desktop shell.

Files:

- [`../host/HexEngine.WebViewTest/App.xaml`](../host/HexEngine.WebViewTest/App.xaml)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml`](../host/HexEngine.WebViewTest/MainWindow.xaml)
- [`../host/HexEngine.WebViewTest/MainWindow.xaml.cs`](../host/HexEngine.WebViewTest/MainWindow.xaml.cs)

### Why WPF

WPF was chosen because it gives:

- borderless windowing
- custom title bars and chrome
- native fallback UIs
- straightforward WebView2 hosting

This is a better fit for the intended desktop app shape than the old WinForms smoke host.

### Current shell responsibilities

`MainWindow` currently owns:

- custom borderless title bar
- minimize / maximize / close buttons
- status text
- WebView2 lifecycle
- native fallback page
- message dispatch between WebView and native bridge
- native bridge lifetime

The shell does not own engine state itself.

It is just the presentation host.

## 6. React + MUI Frontend

The web UI source lives in:

- [`../host/HexEngine.WebViewTest/ui/package.json`](../host/HexEngine.WebViewTest/ui/package.json)
- [`../host/HexEngine.WebViewTest/ui/vite.config.js`](../host/HexEngine.WebViewTest/ui/vite.config.js)
- [`../host/HexEngine.WebViewTest/ui/src/main.jsx`](../host/HexEngine.WebViewTest/ui/src/main.jsx)
- [`../host/HexEngine.WebViewTest/ui/src/App.jsx`](../host/HexEngine.WebViewTest/ui/src/App.jsx)

The built output lives in:

- [`../host/HexEngine.WebViewTest/www/index.html`](../host/HexEngine.WebViewTest/www/index.html)
- [`../host/HexEngine.WebViewTest/www/assets/app.js`](../host/HexEngine.WebViewTest/www/assets/app.js)

### Current frontend behavior

The React shell currently provides:

- state chips for host/bridge readiness
- buttons for native demo actions
- event stream rendering
- a styled desktop-shell presentation surface

It is entirely local.

There is no CDN dependency in the runtime path.

## 7. Frontend Asset Packaging

The frontend assets are not intended to ship as loose files in the final one-EXE model.

Instead:

1. the built `www/` bundle is included in the WPF project as resources
2. [`../host/HexEngine.WebViewTest/WebAssetExtractor.cs`](../host/HexEngine.WebViewTest/WebAssetExtractor.cs) extracts the known files at runtime into:
   - `%LocalAppData%\HexEngine.WebViewTest\web-assets`
3. WebView2 maps that extracted folder with:
   - `SetVirtualHostNameToFolderMapping(...)`

That keeps the runtime web bundle compatible with the single-file packaging strategy.

## 8. WebView2 Strategy

The product decision at this stage is:

- **do not bundle** the 250+ MB Fixed Version runtime
- rely on the installed Evergreen runtime
- if missing, show a native fallback and tell the user to install WebView2 from Microsoft

So the app optimizes for:

- small download
- one-EXE shipping
- acceptable handling for the small minority of missing-runtime machines

## 9. Native Fallback Page

If WebView2 cannot be initialized, the WPF shell switches to a native fallback view.

That fallback currently includes:

- a short explanation
- a Microsoft download button
- Retry
- Exit

The fallback is important because it means:

- the app does not crash on machines without WebView2 Runtime
- the user still sees a branded native surface
- the failure mode is controlled and understandable

## 10. Startup Flow

Current startup flow:

```text
launch app
  -> create MainWindow
  -> attach NativeHexBridge to current process
  -> extract web assets
  -> try create WebView2 environment
      -> if success:
           -> map extracted folder
           -> load index.html
      -> if failure:
           -> show native fallback page
```

More detailed form:

```text
MainWindow.Loaded
  -> InitializeNativeBridge()
      -> NativeHexBridge.AttachToCurrentProcess()
          -> hexengine_host_create_runtime_for_pid(GetCurrentProcessId())
          -> native bridge now owns EngineSession + LuaRuntime
  -> TryInitializeBrowserAsync()
      -> WebAssetExtractor.ExtractCurrentBundle()
      -> CoreWebView2Environment.CreateAsync(...)
      -> Browser.EnsureCoreWebView2Async(...)
      -> SetVirtualHostNameToFolderMapping(...)
      -> hook message events
      -> navigate to the React bundle
```

## 11. Message Flow

The UI bridge is JSON-based.

### Example command flow

React posts:

```json
{ "action": "runNativeGlobal" }
```

WPF handles it and calls:

- `NativeHexBridge.RunGlobal(...)`

The managed wrapper calls:

- `hexengine_host_run_global(...)`

The native bridge calls:

- `LuaRuntime::runGlobal(...)`

The result comes back as JSON, then WPF posts it back into WebView2.

So the full path is:

```text
React action
  -> WPF shell
      -> NativeHexBridge
          -> host_api.cpp
              -> LuaRuntime
                  -> EngineSession
  -> JSON result
      -> WPF shell
          -> postMessage
              -> React state update
```

## 12. Native Actions Implemented Today

The host currently wires these actions:

- `ping`
- `getHostInfo`
- `runNativeGlobal`
- `runNativeScriptCounter`
- `resetNativeScriptCounter`

### `runNativeGlobal`

This executes:

```lua
return process, getOpenedProcessID(), targetIs64Bit(), _VERSION
```

through the real native `LuaRuntime`.

### `runNativeScriptCounter`

This executes a persistent script-scoped counter through native Lua:

```lua
counter = (counter or 0) + 1
return counter, process
```

with script id:

- `webview.demo.counter`

That proves the host is not just calling one-shot native functions. It is using the persistent native script model.

### `resetNativeScriptCounter`

This destroys the script through the bridge:

- `LuaRuntime::destroyScript("webview.demo.counter")`

## 13. Single-File Publish Model

The project file currently enables:

- `SelfContained=true`
- `RuntimeIdentifier=win-x64`
- `PublishSingleFile=true`
- `IncludeNativeLibrariesForSelfExtract=true`
- `IncludeAllContentForSelfExtract=true`
- `WebView2LoaderPreference=Static`

### What this means

The intended shipping shape is:

- one main EXE on disk
- no separate `WebView2Loader.dll`
- bundled native pieces extracted as needed
- installed Evergreen WebView2 Runtime still required

This is “one EXE shipped to the user”, not “nothing ever extracts behind the scenes”.

The current publish output already collapses to the expected form for the host app.

## 14. Build Workflow

### Native layer

Build the native bridge first:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
```

### Frontend

If the React shell changed:

```powershell
cd host\HexEngine.WebViewTest\ui
npm install
npm run build
```

### Managed host

Build:

```powershell
dotnet build host\HexEngine.WebViewTest\HexEngine.WebViewTest.csproj
```

Publish:

```powershell
dotnet publish host\HexEngine.WebViewTest\HexEngine.WebViewTest.csproj -c Release
```

For real Release shipping, the Release native bridge should exist under:

- `build\msvc-release\Release\hexengine_host.dll`

## 15. Testing And Verification

### Automated native-bridge coverage

File:

- [`../tests/host_api_test.cpp`](../tests/host_api_test.cpp)

This verifies:

- runtime creation through the C ABI
- global Lua execution through the C ABI
- script-scoped Lua persistence through the C ABI
- script destruction through the C ABI

### Engine/Lua coverage below the host

Files:

- [`../tests/lua_runtime_test.cpp`](../tests/lua_runtime_test.cpp)
- [`../tests/memory_integration_cases.cpp`](../tests/memory_integration_cases.cpp)

These validate the native behaviors the desktop host relies on.

### Current gap

There are no automated WPF UI tests yet.

So current assurance is:

- native engine tested
- native bridge tested
- WPF host compiled
- single-file publish shape verified

but not full end-to-end UI automation.

## 16. Current Limitations

The desktop host is still deliberately narrow.

Not implemented yet:

- real installer/bootstrap flow for WebView2
- update channel
- managed UI automation
- richer desktop navigation/state management
- broader native bridge surface beyond the current Lua-centered API
- polished product UX around settings, persistence, and update handling

So this should be read as:

- a real architecture baseline
- a usable host proof
- not yet the finished desktop product
