<#
.SYNOPSIS
    校验仓库内离线依赖，不下载、不安装、不执行被校验的文件。
#>
[CmdletBinding()]
param(
    [string]$ManifestPath = (Join-Path $PSScriptRoot '../manifests/dependencies.json')
)
$ErrorActionPreference = 'Stop'
$toolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or @($manifest.files).Count -eq 0) {
    throw '依赖清单版本无效或文件列表为空。'
}
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$bytes = 0L
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    if ($relative -notmatch '^(sdk|packages|packaging)/' -or
        $relative.Contains('\') -or $relative.Contains(':') -or
        @($relative.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0) {
        throw "依赖路径不合法：$relative"
    }
    if (-not $seen.Add($relative)) { throw "依赖路径重复：$relative" }
    if ([string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [long]$entry.bytes -lt 0) {
        throw "依赖校验信息不合法：$relative"
    }
    $path = Join-Path $toolRoot $relative
    # 不跟随指向仓库外的链接，也不允许依赖目录用链接冒充。
    $current = $toolRoot
    foreach ($part in $relative.Split('/')) {
        $current = Join-Path $current $part
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "依赖路径不能包含链接：$relative"
        }
    }
    $file = Get-Item -LiteralPath $path -Force
    if ($file.PSIsContainer -or $file.Length -ne [long]$entry.bytes) {
        throw "依赖大小不符：$relative"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($hash -ne $entry.sha256) { throw "依赖 SHA-256 不符：$relative" }
    $bytes += $file.Length
}
Write-Output "PASS dependencies: $($seen.Count) files, $bytes bytes; no download or installation"
