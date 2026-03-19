using System.ComponentModel;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

namespace HexEngine.WebViewTest;

internal sealed class NativeHexBridge : IDisposable
{
    private sealed class RuntimeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private RuntimeHandle()
            : base(true)
        {
        }

        protected override bool ReleaseHandle()
        {
            NativeMethods.hexengine_host_destroy_runtime(handle);
            return true;
        }
    }

    private static readonly object LoadSync = new();
    private static IntPtr loadedLibraryHandle;
    private static string? loadedLibraryPath;

    private readonly RuntimeHandle runtime;

    static NativeHexBridge()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeHexBridge).Assembly, ResolveLibrary);
    }

    private NativeHexBridge(RuntimeHandle runtime)
    {
        this.runtime = runtime;
    }

    public static string? LoadedLibraryPath => loadedLibraryPath;

    public static NativeHexBridge AttachToCurrentProcess()
    {
        try {
            var pid = NativeMethods.hexengine_host_get_current_process_id();
            var runtime = NativeMethods.hexengine_host_create_runtime_for_pid(pid);
            if (runtime.IsInvalid) {
                runtime.Dispose();
                throw new InvalidOperationException(GetLastErrorOrFallback("Failed to create native runtime."));
            }

            loadedLibraryPath ??= "bundled/default";

            return new NativeHexBridge(runtime);
        } catch (DllNotFoundException exception) {
            throw new InvalidOperationException(
                "hexengine_host.dll could not be loaded. Build the native bridge for development or publish the app with the bundled native runtime.",
                exception);
        }
    }

    public JsonDocument RunGlobal(string source, string chunkName = "webview.global")
    {
        return RunJson((out IntPtr json) => NativeMethods.hexengine_host_run_global(runtime, source, chunkName, out json));
    }

    public JsonDocument RunScript(string scriptId, string source, string chunkName = "webview.script")
    {
        return RunJson((out IntPtr json) =>
            NativeMethods.hexengine_host_run_script(runtime, scriptId, source, chunkName, out json));
    }

    public bool DestroyScript(string scriptId)
    {
        var result = NativeMethods.hexengine_host_destroy_script(runtime, scriptId);
        if (result == 0) {
            throw new InvalidOperationException(GetLastErrorOrFallback($"Failed to destroy script '{scriptId}'."));
        }

        return true;
    }

    public void Dispose()
    {
        runtime.Dispose();
    }

    private JsonDocument RunJson(RunNativeCallback callback)
    {
        var ok = callback(out var jsonPointer);
        try {
            if (ok == 0) {
                throw new InvalidOperationException(GetLastErrorOrFallback("Native bridge call failed."));
            }

            var json = PtrToUtf8String(jsonPointer);
            return JsonDocument.Parse(json);
        } finally {
            if (jsonPointer != IntPtr.Zero) {
                NativeMethods.hexengine_host_free_string(jsonPointer);
            }
        }
    }

    private static string PtrToUtf8String(IntPtr pointer)
    {
        return Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
    }

    private static string GetLastErrorOrFallback(string fallback)
    {
        var pointer = NativeMethods.hexengine_host_get_last_error();
        if (pointer == IntPtr.Zero) {
            return fallback;
        }

        return PtrToUtf8String(pointer);
    }

    private static IntPtr ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, "hexengine_host", StringComparison.Ordinal)) {
            return IntPtr.Zero;
        }

        lock (LoadSync) {
            if (loadedLibraryHandle != IntPtr.Zero) {
                return loadedLibraryHandle;
            }

            foreach (var candidate in EnumerateCandidates()) {
                if (!File.Exists(candidate)) {
                    continue;
                }

                loadedLibraryPath = candidate;
                loadedLibraryHandle = NativeLibrary.Load(candidate);
                return loadedLibraryHandle;
            }
        }

        return IntPtr.Zero;
    }

    private static IEnumerable<string> EnumerateCandidates()
    {
        var configured = Environment.GetEnvironmentVariable("HEXENGINE_HOST_DLL");
        if (!string.IsNullOrWhiteSpace(configured)) {
            yield return Path.GetFullPath(configured);
        }

        yield return Path.Combine(AppContext.BaseDirectory, "hexengine_host.dll");

        var baseDir = AppContext.BaseDirectory;
        yield return Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "msvc-debug", "Debug", "hexengine_host.dll"));
        yield return Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "msvc-release", "Release", "hexengine_host.dll"));
    }

    private delegate int RunNativeCallback(out IntPtr jsonPointer);

    private static class NativeMethods
    {
        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern uint hexengine_host_get_current_process_id();

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern RuntimeHandle hexengine_host_create_runtime_for_pid(uint pid);

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern void hexengine_host_destroy_runtime(IntPtr runtime);

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern int hexengine_host_run_global(
            RuntimeHandle runtime,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string source,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string chunkName,
            out IntPtr json);

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern int hexengine_host_run_script(
            RuntimeHandle runtime,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptId,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string source,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string chunkName,
            out IntPtr json);

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern int hexengine_host_destroy_script(
            RuntimeHandle runtime,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptId);

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern IntPtr hexengine_host_get_last_error();

        [DllImport("hexengine_host", ExactSpelling = true)]
        internal static extern void hexengine_host_free_string(IntPtr value);
    }
}
