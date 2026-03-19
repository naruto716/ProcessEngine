# HexEngine WebView Test

This is a WPF + WebView2 desktop shell over the native `hexengine_host` bridge.

Current purpose:

- use a borderless native desktop window with custom chrome
- host a local React + MUI app inside WebView2
- call the native `hexengine_host` bridge and return real engine results to the page
- stay compatible with a one-EXE publish shape
- fall back to a native WPF page when WebView2 Runtime is missing

## Build Native First

The WebView host loads `hexengine_host.dll` at runtime.

Build the native targets first:

```powershell
.\scripts\build-msvc.ps1 -Configuration Debug
```

That produces:

```text
build\msvc-debug\Debug\hexengine_host.dll
```

The host will look there automatically in a normal repo checkout.

If you want to load the DLL from somewhere else, set:

```powershell
$env:HEXENGINE_HOST_DLL = "D:\path\to\hexengine_host.dll"
```

## Build Host

The checked-in `www` folder already contains the built React bundle, so a normal .NET build is enough.

```powershell
dotnet build host\HexEngine.WebViewTest\HexEngine.WebViewTest.csproj
```

If you edit the React frontend under `host\HexEngine.WebViewTest\ui`, rebuild it with:

```powershell
cd host\HexEngine.WebViewTest\ui
npm install
npm run build
```

## Run

```powershell
dotnet run --project host\HexEngine.WebViewTest\HexEngine.WebViewTest.csproj
```

## Publish As One EXE

```powershell
dotnet publish host\HexEngine.WebViewTest\HexEngine.WebViewTest.csproj -c Release
```

The project is configured for:

- `SelfContained=true`
- `PublishSingleFile=true`
- `IncludeNativeLibrariesForSelfExtract=true`
- `WebView2LoaderPreference=Static`

So the release output is intended to collapse to a single shipped executable while still extracting native pieces at runtime as needed.

## WebView2 Missing Fallback

If WebView2 Runtime is not installed on the target machine, the app does not crash.

Instead it shows a native WPF fallback page with:

- an explanation
- a button that opens Microsoft's WebView2 download page
- a retry button
- an exit button

## Current bridge actions

The page sends JSON envelopes like:

```json
{ "action": "ping" }
```

Supported actions:

- `ping`
- `getHostInfo`
- `runNativeGlobal`
- `runNativeScriptCounter`
- `resetNativeScriptCounter`

The native actions call into `hexengine_host.dll`, which attaches to the current process and runs the C++ `LuaRuntime`.

`runNativeGlobal` executes a real global Lua chunk:

```lua
return process, getOpenedProcessID(), targetIs64Bit(), _VERSION
```

`runNativeScriptCounter` executes a real script-scoped Lua chunk:

```lua
counter = (counter or 0) + 1
return counter, process
```

So each button press proves the full path:

```text
React UI -> WPF host -> hexengine_host.dll -> LuaRuntime -> JSON result -> WPF host -> React UI
```
