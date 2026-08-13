using System.IO;
using System.Text;

namespace OpenTrail.Loader;

public static class BoundedTextReader
{
    public static async Task<string> ReadAsync(
        TextReader reader,
        int maximumCharacters,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reader);
        if (maximumCharacters <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximumCharacters),
                "A positive text limit is required.");
        }

        var buffer = new char[Math.Min(4096, maximumCharacters)];
        var result = new StringBuilder(Math.Min(maximumCharacters, 8192));

        while (true)
        {
            var count = await reader.ReadAsync(buffer.AsMemory(), cancellationToken);
            if (count == 0)
            {
                return result.ToString();
            }

            if (result.Length > maximumCharacters - count)
            {
                throw new InvalidDataException("Inspection output exceeded its safe limit.");
            }

            result.Append(buffer, 0, count);
        }
    }
}
