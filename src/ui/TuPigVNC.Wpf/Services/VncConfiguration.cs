using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// Standalone file document; does not configure a session or supply core defaults.
/// Keys are case insensitive. Duplicate keys (including known aliases) are rejected,
/// rather than using the native reader's last-successful-value behavior. Legacy keys
/// are retained, not migrated or applied. Missing and empty values remain distinct.
/// Unedited lines, order, spelling, BOM and line endings are preserved, except secrets.
/// Limits deliberately reject oversized input instead of native 256-byte truncation.
/// </summary>
public sealed class VncConfiguration
{
    public const string Identifier = "TuPigVNC Configuration file Version 1.0";
    public const int MaxFileBytes = 1024 * 1024;
    public const int MaxLineBytes = 16 * 1024;
    public const int MaxValueBytes = 8 * 1024;
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private sealed record Line(string Text, string Ending, string? Key = null, string? Value = null);
    private readonly List<Line> lines = new();
    private bool bom;
    private string newline = "\n";

    private static string Canonical(string key) => key.ToUpperInvariant() switch
    {
        "FULLCOLOUR" => "FULLCOLOR",
        "LOWCOLOURLEVEL" => "LOWCOLORLEVEL",
        _ => key.ToUpperInvariant()
    };

    // Secrets are intentionally excluded even when supplied through an unknown-key API.
    // Unknown arbitrary fields cannot be classified by their value; callers must never
    // put credentials in extension fields or comments.
    private static bool Sensitive(string key)
    {
        string k = key.Replace("-", "").Replace("_", "").Replace(".", "");
        return k.Contains("password", StringComparison.OrdinalIgnoreCase)
            || k.Contains("passwd", StringComparison.OrdinalIgnoreCase)
            || k.Contains("token", StringComparison.OrdinalIgnoreCase)
            || k.Contains("secret", StringComparison.OrdinalIgnoreCase)
            || k.Contains("credential", StringComparison.OrdinalIgnoreCase)
            || k.Contains("privatekey", StringComparison.OrdinalIgnoreCase)
            || k.Contains("apikey", StringComparison.OrdinalIgnoreCase)
            || k.Equals("username", StringComparison.OrdinalIgnoreCase)
            || k.Equals("user", StringComparison.OrdinalIgnoreCase);
    }

    public static VncConfiguration Create() => Parse(Identifier + "\n\n");

    public static VncConfiguration Load(string path)
    {
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (input.Length > MaxFileBytes) throw new InvalidDataException("Configuration exceeds file size limit.");
        using var memory = new MemoryStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = input.Read(buffer, 0, buffer.Length)) != 0)
        {
            if (memory.Length + count > MaxFileBytes) throw new InvalidDataException("Configuration exceeds file size limit.");
            memory.Write(buffer, 0, count);
        }
        return Parse(Utf8.GetString(memory.ToArray()));
    }

    public static VncConfiguration Parse(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (Utf8.GetByteCount(text) > MaxFileBytes) throw new InvalidDataException("Configuration exceeds file size limit.");
        if (text.Contains('\0')) throw new InvalidDataException("NUL is not allowed.");
        var document = new VncConfiguration();
        if (text.StartsWith('\uFEFF')) { document.bom = true; text = text[1..]; }
        var keys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int position = 0, number = 0;
        while (position < text.Length)
        {
            int start = position;
            while (position < text.Length && text[position] != '\r' && text[position] != '\n') position++;
            string raw = text[start..position];
            string ending = "";
            if (position < text.Length)
            {
                if (text[position] == '\r')
                {
                    if (position + 1 >= text.Length || text[position + 1] != '\n')
                        throw new InvalidDataException($"Line {number + 1}: bare CR is not a line ending.");
                    ending = "\r\n"; position += 2;
                }
                else { ending = "\n"; position++; }
            }
            number++;
            if (Utf8.GetByteCount(raw) > MaxLineBytes) throw new InvalidDataException($"Line {number}: line too long.");
            if (number == 1)
            {
                if (raw != Identifier) throw new InvalidDataException("Invalid configuration header.");
                if (ending.Length != 0) document.newline = ending;
                document.lines.Add(new(raw, ending));
                continue;
            }
            if (raw.Length == 0 || raw[0] == '#') { document.lines.Add(new(raw, ending)); continue; }
            int equals = raw.IndexOf('=');
            if (equals <= 0) throw new InvalidDataException($"Line {number}: expected key=value.");
            string key = raw[..equals];
            ValidateKey(key);
            if (!keys.Add(Canonical(key))) throw new InvalidDataException($"Line {number}: duplicate key {key}.");
            string value = Decode(raw[(equals + 1)..]);
            ValidateValue(key, value);
            if (!Sensitive(key)) document.lines.Add(new(raw, ending, key, value));
        }
        if (number == 0) throw new InvalidDataException("Missing configuration header.");
        return document;
    }

    public string? Get(string key)
    {
        ValidateKey(key);
        return lines.FirstOrDefault(l => l.Key != null && Canonical(l.Key) == Canonical(key))?.Value;
    }
    public string? ServerName { get => Get("ServerName"); set => Set("ServerName", value); }
    /// <summary>Full ordered text; no splitting, normalization or inferred security defaults.</summary>
    public string? SecurityTypes { get => Get("SecurityTypes"); set => Set("SecurityTypes", value); }
    public int? GetInteger(string key) => Get(key) is { } value ? ParseInteger(value) : null;
    public bool? GetBoolean(string key) => Get(key) is { } value ? ParseBoolean(value) : null;

    /// <summary>Null removes a key; empty remains an explicit empty value. Validation is transactional.</summary>
    public void Set(string key, string? value)
    {
        ValidateKey(key);
        if (Sensitive(key)) throw new ArgumentException("Credential fields cannot be persisted.", nameof(key));
        if (value != null) ValidateValue(key, value);
        int index = lines.FindIndex(l => l.Key != null && Canonical(l.Key) == Canonical(key));
        if (value == null) { if (index >= 0) lines.RemoveAt(index); return; }
        string spelling = index >= 0 ? lines[index].Key! : key;
        string raw = spelling + "=" + Encode(value);
        if (Utf8.GetByteCount(raw) > MaxLineBytes) throw new InvalidDataException("Line too long.");
        var candidate = new List<Line>(lines);
        if (index >= 0) candidate[index] = new(raw, lines[index].Ending, spelling, value);
        else
        {
            if (candidate[^1].Ending.Length == 0) candidate[^1] = candidate[^1] with { Ending = newline };
            candidate.Add(new(raw, newline, spelling, value));
        }
        if (Utf8.GetByteCount(Render(candidate)) > MaxFileBytes) throw new InvalidDataException("Configuration exceeds file size limit.");
        lines.Clear(); lines.AddRange(candidate);
    }

    public string Serialize() => Render(lines);
    private string Render(IEnumerable<Line> source) => (bom ? "\uFEFF" : "") + string.Concat(source.Select(l => l.Text + l.Ending));

    /// <summary>
    /// Writes and flushes a unique same-directory CreateNew file, then atomically replaces
    /// an existing file (or moves without overwrite for a new file). No delete/copy fallback.
    /// A failed commit leaves the original intact; cleanup never masks the original error.
    /// </summary>
    public void Save(string path)
    {
        byte[] data = Utf8.GetBytes(Serialize());
        if (data.Length > MaxFileBytes) throw new InvalidDataException("Configuration exceeds file size limit.");
        string target = Path.GetFullPath(path);
        string temporary = Path.Combine(Path.GetDirectoryName(target)!, ".vnc-" + Guid.NewGuid().ToString("N") + ".tmp");
        bool created = false;
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                created = true;
                output.Write(data);
                output.Flush(true);
            }
            if (File.Exists(target)) File.Replace(temporary, target, null);
            else File.Move(temporary, target);
        }
        finally
        {
            if (created)
            {
                try { File.Delete(temporary); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    private static void ValidateKey(string key)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (key.Length == 0 || key.Any(c => !(char.IsAsciiLetterOrDigit(c) || c is '_' or '-' or '.')))
            throw new InvalidDataException("Invalid configuration key.");
    }
    private static void ValidateValue(string key, string value)
    {
        if (value.Contains('\0') || Utf8.GetByteCount(value) > MaxValueBytes)
            throw new InvalidDataException($"{key}: NUL or oversized value.");
        string k = Canonical(key);
        if (k is "COMPRESSLEVEL" or "QUALITYLEVEL" or "LOWCOLORLEVEL" or "POINTEREVENTINTERVAL")
        {
            int n = ParseInteger(value);
            int max = k == "LOWCOLORLEVEL" ? 2 : k == "POINTEREVENTINTERVAL" ? int.MaxValue : 9;
            if (n < 0 || n > max) throw new InvalidDataException($"{key}: value out of range.");
        }
        if (BooleanKeys.Contains(k)) _ = ParseBoolean(value);
        if (k == "FULLSCREENSELECTEDMONITORS" && value.Length != 0)
            foreach (string item in value.Split(','))
                if (ParseInteger(item) < 1) throw new InvalidDataException("Monitor indices must be positive.");
        if (k == "SECURITYTYPES" && string.IsNullOrWhiteSpace(value))
            throw new InvalidDataException("An explicit SecurityTypes list must not be empty.");
    }
    private static readonly HashSet<string> BooleanKeys = new(StringComparer.OrdinalIgnoreCase)
    {
        "AutoSelect", "FullColor", "CustomCompressLevel", "NoJPEG", "FullScreen", "FullScreenAllMonitors",
        "DotWhenNoCursor", "ViewOnly", "EmulateMiddleButton", "AlwaysCursor", "AcceptClipboard", "SendClipboard",
        "SendPrimary", "SetPrimary", "FullscreenSystemKeys", "Shared", "ReconnectOnError", "Audio", "Maximize",
        "RemoteResize", "AlertOnFatalError", "listen"
    };
    private static bool ParseBoolean(string value) => value.ToUpperInvariant() switch
    {
        "" or "1" or "ON" or "TRUE" or "YES" => true,
        "0" or "OFF" or "FALSE" or "NO" => false,
        _ => throw new InvalidDataException("Invalid boolean value.")
    };
    // Native integers use strtol(base 0): preserve decimal, octal and hex spelling,
    // but reject whitespace, empty values, junk and overflow instead of coercing them.
    private static int ParseInteger(string value)
    {
        if (value.Length == 0) throw new InvalidDataException("Invalid integer.");
        int position = value[0] is '+' or '-' ? 1 : 0;
        bool negative = value[0] == '-';
        int radix = 10;
        if (value.Length > position + 1 && value[position] == '0')
        {
            radix = 8;
            if (value[position + 1] is 'x' or 'X') { radix = 16; position += 2; }
        }
        if (position == value.Length) throw new InvalidDataException("Invalid integer.");
        long result = 0;
        for (; position < value.Length; position++)
        {
            char c = value[position];
            int digit = c is >= '0' and <= '9' ? c - '0' : c is >= 'a' and <= 'f' ? c - 'a' + 10 : c is >= 'A' and <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0 || digit >= radix) throw new InvalidDataException("Invalid integer.");
            result = result * radix + digit;
            if (result > (negative ? 2147483648L : int.MaxValue)) throw new InvalidDataException("Integer overflow.");
        }
        return (int)(negative ? -result : result);
    }
    public static string Encode(string value) => value.Replace("\\", "\\\\").Replace("\n", "\\n").Replace("\r", "\\r");
    public static string Decode(string value)
    {
        var result = new StringBuilder(value.Length);
        for (int i = 0; i < value.Length; i++)
        {
            if (value[i] != '\\') { result.Append(value[i]); continue; }
            if (++i == value.Length) throw new InvalidDataException("Incomplete escape.");
            result.Append(value[i] switch
            {
                '\\' => '\\', 'n' => '\n', 'r' => '\r',
                _ => throw new InvalidDataException("Unknown escape; only \\\\, \\n and \\r are supported.")
            });
        }
        return result.ToString();
    }
}
