using System.Diagnostics;
using System.IO;

namespace OpenTrail.Loader;

public sealed class LoaderInspectionService
{
    private const int MaximumOutputCharacters = 256 * 1024;
    private const int MaximumErrorCharacters = 16 * 1024;

    private readonly Func<string?> _repositoryRootResolver;
    private readonly Func<CancellationToken, LoaderInspectionDocument> _packagedInspection;

    public LoaderInspectionService()
        : this(
            TryFindRepositoryRoot,
            WindowsSerialInspectionProvider.Inspect)
    {
    }

    public LoaderInspectionService(
        Func<string?> repositoryRootResolver,
        Func<CancellationToken, LoaderInspectionDocument> packagedInspection)
    {
        _repositoryRootResolver = repositoryRootResolver ??
            throw new ArgumentNullException(nameof(repositoryRootResolver));
        _packagedInspection = packagedInspection ??
            throw new ArgumentNullException(nameof(packagedInspection));
    }

    public async Task<LoaderInspectionDocument> RefreshAsync(
        CancellationToken cancellationToken = default)
    {
        var repositoryRoot = _repositoryRootResolver();
        if (repositoryRoot is null)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var packagedDocument = await Task.Run(
                () => _packagedInspection(cancellationToken),
                cancellationToken);
            packagedDocument.Validate();
            return packagedDocument;
        }

        var script = Path.Combine(
            repositoryRoot, "tools", "Get-OpenTrailLoaderInspection.py");
        var startInfo = new ProcessStartInfo
        {
            FileName = "python",
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        startInfo.ArgumentList.Add(script);

        using var process = new Process { StartInfo = startInfo };
        if (!process.Start())
        {
            throw new InvalidOperationException("Inspection process could not start.");
        }

        var outputTask = BoundedTextReader.ReadAsync(
            process.StandardOutput, MaximumOutputCharacters, cancellationToken);
        var errorTask = BoundedTextReader.ReadAsync(
            process.StandardError, MaximumErrorCharacters, cancellationToken);

        try
        {
            await Task.WhenAll(
                process.WaitForExitAsync(cancellationToken),
                outputTask,
                errorTask);
        }
        catch
        {
            await StopOwnedProcessAsync(process);
            throw;
        }

        var output = await outputTask;
        _ = await errorTask; // Never expose raw stderr through the UI.

        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException("Connected-device inspection did not complete.");
        }

        return LoaderInspectionDocument.Parse(output);
    }

    private static async Task StopOwnedProcessAsync(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync(CancellationToken.None)
                    .WaitAsync(TimeSpan.FromSeconds(5));
            }
        }
        catch
        {
            // Termination is best-effort. The original bounded error remains
            // the only result returned to the caller.
        }
    }

    private static string? TryFindRepositoryRoot()
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

        return null;
    }
}
