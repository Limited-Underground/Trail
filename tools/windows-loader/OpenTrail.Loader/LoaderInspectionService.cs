using System.Diagnostics;
using System.IO;

namespace OpenTrail.Loader;

public sealed class LoaderInspectionService
{
    public async Task<LoaderInspectionDocument> RefreshAsync(
        CancellationToken cancellationToken = default)
    {
        var repositoryRoot = FindRepositoryRoot();
        var script = Path.Combine(
            repositoryRoot, "tools", "Get-OpenTrailLoaderInspection.py");
        var startInfo = new ProcessStartInfo
        {
            FileName = "python",
            Arguments = $"\"{script}\"",
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        using var process = new Process { StartInfo = startInfo };
        if (!process.Start())
        {
            throw new InvalidOperationException("Inspection process could not start.");
        }

        var outputTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var errorTask = process.StandardError.ReadToEndAsync(cancellationToken);
        await process.WaitForExitAsync(cancellationToken);
        var output = await outputTask;
        _ = await errorTask; // Never expose raw stderr through the UI.

        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException("Connected-device inspection did not complete.");
        }

        return LoaderInspectionDocument.Parse(output);
    }

    private static string FindRepositoryRoot()
    {
        foreach (var startingPath in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
        {
            var directory = new DirectoryInfo(startingPath);
            while (directory is not null)
            {
                var marker = Path.Combine(
                    directory.FullName, "tools", "Get-OpenTrailLoaderInspection.py");
                if (File.Exists(marker))
                {
                    return directory.FullName;
                }

                directory = directory.Parent;
            }
        }

        throw new DirectoryNotFoundException(
            "OpenTrail development files were not found. Packaged inspection is not implemented.");
    }
}
