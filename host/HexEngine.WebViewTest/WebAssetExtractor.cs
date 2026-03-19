using System.IO;
using System.Windows;

namespace HexEngine.WebViewTest;

internal static class WebAssetExtractor
{
    private static readonly string[] AssetPaths =
    {
        "www/index.html",
        "www/assets/app.js",
    };

    public static string ExtractCurrentBundle()
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "HexEngine.WebViewTest",
            "web-assets");

        Directory.CreateDirectory(root);

        foreach (var assetPath in AssetPaths) {
            var resourceStream = Application.GetResourceStream(new Uri(assetPath, UriKind.Relative));
            if (resourceStream is null) {
                throw new FileNotFoundException($"Embedded web asset was not found: {assetPath}");
            }

            using var input = resourceStream.Stream;
            var outputPath = Path.Combine(root, assetPath["www/".Length..].Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            using var output = File.Create(outputPath);
            input.CopyTo(output);
        }

        return root;
    }
}
