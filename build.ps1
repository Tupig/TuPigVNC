<#
.SYNOPSIS
    TuPigVNC - WPF 构建 + 打包

.DESCRIPTION
    1. 编译原生核心 TuPigVNC.Core.dll（CMake）
    2. 发布 WPF 前端（dotnet publish，self-contained）
    3. 校验产物布局
    4. 可选：生成 Inno Setup 安装包

.PARAMETER BuildDir
    CMake 构建目录，默认 E:\Agent\build

.PARAMETER Installer
    同时生成安装包

.EXAMPLE
    PowerShell -File build.ps1
    PowerShell -File build.ps1 -Installer
#>
param(
    [string]$BuildDir = "E:\Agent\build",
    [switch]$Installer
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$publishDir = Join-Path $BuildDir "publish"

Write-Host "=== 1/4 编译原生核心 DLL ===" -ForegroundColor Cyan
Push-Location $BuildDir
try {
    & cmake --build . --config Release --target TuPigVNC.Core -- -m
    if ($LASTEXITCODE -ne 0) { throw "TuPigVNC.Core build failed" }
} finally {
    Pop-Location
}

Write-Host "=== 2/4 发布 WPF 前端 ===" -ForegroundColor Cyan
# 不先删 publish 目录：dotnet publish 会覆盖，删除可能因 Explorer 占用句柄失败
& dotnet publish (Join-Path $root "ui\TuPigVNC.Wpf\TuPigVNC.Wpf.csproj") `
    -c Release `
    -p:Platform=x64 `
    --self-contained true `
    -p:TupigVncBuildDir=$BuildDir `
    -o $publishDir
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }

Write-Host "=== 3/4 校验产物 ===" -ForegroundColor Cyan
$checks = @(
    @{ Path = Join-Path $publishDir "TuPigVNC.exe";      Desc = "launcher" },
    @{ Path = Join-Path $publishDir "TuPigVNC.dll";      Desc = "managed app" },
    @{ Path = Join-Path $publishDir "TuPigVNC.Core.dll"; Desc = "native core" }
)
foreach ($c in $checks) {
    if (Test-Path $c.Path) {
        Write-Host "  [OK]   $($c.Desc): $($c.Path)" -ForegroundColor Green
    } else {
        Write-Host "  [MISS] $($c.Desc): $($c.Path)" -ForegroundColor Red
        throw "missing $($c.Desc)"
    }
}

if ($Installer) {
    Write-Host "=== 4/4 生成 Inno 安装包 ===" -ForegroundColor Cyan
    Push-Location $BuildDir
    try {
        & cmake --build . --config Release --target installer
        if ($LASTEXITCODE -ne 0) { throw "Inno installer build failed" }
    } finally {
        Pop-Location
    }
    $setup = Get-ChildItem "$BuildDir\release\TuPigVNC_x64.exe" -ErrorAction SilentlyContinue
    if ($setup) {
        Write-Host "  安装包: $($setup.FullName)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "完成。发布目录: $publishDir" -ForegroundColor Green
