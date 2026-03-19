using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using Microsoft.Web.WebView2.Core;

namespace HexEngine.WebViewTest;

public partial class MainWindow : Window
{
    private const string AppHostName = "app.hexengine.local";
    private const string DemoScriptId = "webview.demo.counter";
    private const string RuntimeDownloadUrl = "https://developer.microsoft.com/en-us/microsoft-edge/webview2/";

    private NativeHexBridge? nativeBridge;
    private string? nativeBridgeError;
    private bool browserInitialized;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closed += (_, _) => nativeBridge?.Dispose();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        InitializeNativeBridge();
        await TryInitializeBrowserAsync();
    }

    private void InitializeNativeBridge()
    {
        try {
            nativeBridge = NativeHexBridge.AttachToCurrentProcess();
            nativeBridgeError = null;
        } catch (Exception exception) {
            nativeBridgeError = exception.Message;
        }

        NativeBridgePathText.Text = nativeBridge is not null
            ? $"Native bridge: {NativeHexBridge.LoadedLibraryPath ?? "bundled"}"
            : $"Native bridge unavailable: {nativeBridgeError}";
    }

    private async Task TryInitializeBrowserAsync()
    {
        if (browserInitialized) {
            return;
        }

        try {
            StatusText.Text = "Preparing web shell...";
            var extractedAssets = WebAssetExtractor.ExtractCurrentBundle();
            var userDataFolder = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "HexEngine.WebViewTest",
                "WebView2");

            Directory.CreateDirectory(userDataFolder);

            var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: userDataFolder);
            await Browser.EnsureCoreWebView2Async(environment);

            Browser.CoreWebView2.SetVirtualHostNameToFolderMapping(
                AppHostName,
                extractedAssets,
                CoreWebView2HostResourceAccessKind.Allow);

            Browser.CoreWebView2.Settings.AreDevToolsEnabled = true;
            Browser.CoreWebView2.Settings.IsWebMessageEnabled = true;
            Browser.CoreWebView2.WebMessageReceived += Browser_OnWebMessageReceived;
            Browser.CoreWebView2.NavigationCompleted += Browser_OnNavigationCompleted;

            Browser.Source = new Uri($"https://{AppHostName}/index.html");
            Browser.Visibility = Visibility.Visible;
            FallbackPanel.Visibility = Visibility.Collapsed;
            browserInitialized = true;
            StatusText.Text = "Loading React shell...";
        } catch (WebView2RuntimeNotFoundException exception) {
            ShowFallback(
                "This machine does not currently have Microsoft Edge WebView2 Runtime installed. Install it from Microsoft's site, then click Retry.",
                exception.Message);
        } catch (Exception exception) {
            ShowFallback(
                "WebView2 initialization failed before the shell could start. Retry if the runtime was just installed, otherwise inspect the details below.",
                exception.Message);
        }
    }

    private void ShowFallback(string message, string details)
    {
        Browser.Visibility = Visibility.Collapsed;
        FallbackPanel.Visibility = Visibility.Visible;
        FallbackMessageText.Text = message;
        FallbackDetailText.Text = details;
        StatusText.Text = "Native fallback active";
    }

    private void Browser_OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        StatusText.Text = e.IsSuccess ? "Web shell ready" : $"Navigation failed: {e.WebErrorStatus}";
        PostHostEvent("host-ready", new
        {
            processId = Environment.ProcessId,
            framework = Environment.Version.ToString(),
            currentDirectory = Environment.CurrentDirectory,
            nativeBridgeLoaded = nativeBridge is not null,
            nativeBridgePath = NativeHexBridge.LoadedLibraryPath,
            nativeBridgeError,
            shell = "wpf-single-file",
        });
    }

    private void Browser_OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        try {
            using var document = JsonDocument.Parse(e.WebMessageAsJson);
            var root = document.RootElement;
            var action = root.TryGetProperty("action", out var actionProp) ? actionProp.GetString() : null;

            switch (action) {
            case "ping":
                StatusText.Text = "Ping received";
                PostHostEvent("pong", new
                {
                    message = "pong",
                    processId = Environment.ProcessId,
                    timestamp = DateTimeOffset.UtcNow,
                });
                break;

            case "getHostInfo":
                StatusText.Text = "Sending host info";
                PostHostEvent("host-info", new
                {
                    machineName = Environment.MachineName,
                    osVersion = Environment.OSVersion.VersionString,
                    framework = Environment.Version.ToString(),
                    processId = Environment.ProcessId,
                    currentDirectory = Environment.CurrentDirectory,
                    nativeBridgeLoaded = nativeBridge is not null,
                    nativeBridgePath = NativeHexBridge.LoadedLibraryPath,
                    nativeBridgeError,
                    webViewVersion = CoreWebView2Environment.GetAvailableBrowserVersionString(),
                });
                break;

            case "runNativeGlobal":
                StatusText.Text = "Running native Lua demo";
                PostHostEvent("native-global", new
                {
                    values = RunNativeGlobalDemo(),
                    nativeBridgePath = NativeHexBridge.LoadedLibraryPath,
                });
                break;

            case "runNativeScriptCounter":
                StatusText.Text = "Incrementing native counter";
                PostHostEvent("native-script-counter", new
                {
                    scriptId = DemoScriptId,
                    values = RunNativeScriptCounter(),
                });
                break;

            case "resetNativeScriptCounter":
                StatusText.Text = "Resetting native counter";
                EnsureNativeBridge().DestroyScript(DemoScriptId);
                PostHostEvent("native-script-reset", new
                {
                    scriptId = DemoScriptId,
                    reset = true,
                });
                break;

            default:
                throw new InvalidOperationException($"Unknown action: {action ?? "(null)"}");
            }
        } catch (Exception exception) {
            StatusText.Text = "Host message handling failed";
            PostHostEvent("error", new
            {
                message = exception.Message,
            });
        }
    }

    private JsonElement RunNativeGlobalDemo()
    {
        using var result = EnsureNativeBridge().RunGlobal(
            "return process, getOpenedProcessID(), targetIs64Bit(), _VERSION",
            "wpf.native.global");
        return result.RootElement.Clone();
    }

    private JsonElement RunNativeScriptCounter()
    {
        using var result = EnsureNativeBridge().RunScript(
            DemoScriptId,
            "counter = (counter or 0) + 1; return counter, process",
            "wpf.native.counter");
        return result.RootElement.Clone();
    }

    private NativeHexBridge EnsureNativeBridge()
    {
        if (nativeBridge is null) {
            throw new InvalidOperationException(nativeBridgeError ?? "Native bridge is not available.");
        }

        return nativeBridge;
    }

    private void PostHostEvent(string type, object payload)
    {
        if (Browser.CoreWebView2 is null) {
            return;
        }

        var envelope = JsonSerializer.Serialize(new
        {
            type,
            payload,
        });

        Browser.CoreWebView2.PostWebMessageAsJson(envelope);
    }

    private void TitleBar_OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2) {
            ToggleMaximizeRestore();
            return;
        }

        DragMove();
    }

    private void MinimizeButton_OnClick(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    private void MaximizeButton_OnClick(object sender, RoutedEventArgs e)
    {
        ToggleMaximizeRestore();
    }

    private void ToggleMaximizeRestore()
    {
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    }

    private void CloseButton_OnClick(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void DevToolsButton_OnClick(object sender, RoutedEventArgs e)
    {
        Browser.CoreWebView2?.OpenDevToolsWindow();
    }

    private void DownloadRuntimeButton_OnClick(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo
        {
            FileName = RuntimeDownloadUrl,
            UseShellExecute = true,
        });
    }

    private async void RetryButton_OnClick(object sender, RoutedEventArgs e)
    {
        await TryInitializeBrowserAsync();
    }

    private void ExitButton_OnClick(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
