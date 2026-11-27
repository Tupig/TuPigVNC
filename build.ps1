<#
.SYNOPSIS
    TuPigVNC 一键构建（零环境配置）

.DESCRIPTION
    把仓库 clone 下来，直接运行本脚本即可，不需要预装任何工具：

      pwsh -File build.ps1                 # 构建（原生核心 + WPF 前端）
      pwsh -File build.ps1 -Installer     # 额外生成 Inno 安装包
      pwsh -File build.ps1 -Clean         # 先清空构建目录再构建

    所有工具链都在仓库内：
      tool/build/mingw64/    MinGW-w64 GCC 16.1（C/C++ 编译器 + 运行时）
      tool/build/cmake/      CMake 4.4
      tool/build/ninja/      Ninja 1.13
      tool/sdk/dotnet/       .NET 11 SDK（WPF 前端）
      tool/deps/x64/         预编译第三方静态库（zlib / libjpeg / pixman）
      tool/packaging/        Inno Setup（仅 -Installer 需要）

    构建产物全部落在 tmp/build/，不污染源码目录，也不写入仓库外任何位置。

.PARAMETER Installer
    构建完成后生成 Windows 安装包。

.PARAMETER Clean
    构建前清空 tmp/build/。

.PARAMETER Target
    只构建指定目标（默认构建全部）。可用值：Core / Wpf。
#>
[CmdletBinding()]
param(
    [switch]$Installer,
    [switch]$Clean,
    [ValidateSet('', 'Core', 'Wpf')]
    [string]$Target = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ============================================================
# 0. 路径初始化
# ============================================================

$root      = $PSScriptRoot
$toolDir   = Join-Path $root 'tool'
$buildRoot = Join-Path $root 'tmp/build'

# CMake 参数里的路径必须用正斜杠：反斜杠会被当成转义序列，
# 例如 G:\VNCView\tool 里的 \V 会报 "Invalid character escape"。
function ConvertTo-CMakePath([string]$path) {
    return $path.Replace('\', '/')
}

$mingwBin  = Join-Path $toolDir 'build/mingw64/bin'
$cmakeExe  = Join-Path $toolDir 'build/cmake/bin/cmake.exe'
$ninjaExe  = Join-Path $toolDir 'build/ninja/ninja.exe'
$dotnetExe = Join-Path $toolDir 'sdk/dotnet/11.0.100/dotnet.exe'
$depsDir   = Join-Path $toolDir 'deps/x64'
$isccExe   = Join-Path $toolDir 'packaging/innosetup/ISCC.exe'

# 原生构建树：与 WPF 工程里的 TupigVncBuildDir 保持一致
$nativeBuildDir = Join-Path $buildRoot 'native'
$coreDllPath    = Join-Path $nativeBuildDir 'ui/TuPigVNC.Core/TuPigVNC.Core.dll'
$publishDir     = Join-Path $buildRoot 'publish'

# ============================================================
# 1. 前置检查：所有工具必须在仓库内
# ============================================================

function Test-Toolchain {
    $required = [ordered]@{
        'MinGW GCC'   = Join-Path $mingwBin 'gcc.exe'
        'MinGW G++'   = Join-Path $mingwBin 'g++.exe'
        'MinGW 资源编译' = Join-Path $mingwBin 'windres.exe'
        'CMake'       = $cmakeExe
        'Ninja'       = $ninjaExe
        '.NET SDK'    = $dotnetExe
        'zlib'        = Join-Path $depsDir 'lib/libz.a'
        'libjpeg'     = Join-Path $depsDir 'lib/libjpeg.a'
        'pixman'      = Join-Path $depsDir 'lib/libpixman-1.a'
    }
    if ($Installer) { $required['Inno Setup'] = $isccExe }

    $missing = @()
    foreach ($name in $required.Keys) {
        $path = $required[$name]
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $missing += "  $name`n    $path"
        }
    }
    if ($missing.Count -gt 0) {
        throw ("工具链不完整。仓库应当自带全部依赖——`n" +
            ($missing -join "`n") +
            "`n`n请重新完整 clone 仓库（不要用 --depth 1 之外的裁剪）。")
    }
}

# ============================================================
# 2. 环境隔离：只使用仓库内工具，不继承系统 PATH
# ============================================================

function Set-BuildEnvironment {
    $script:previousEnv = @{}

    # PATH 只放仓库内工具，确保复现性——避免误用系统装的另一个 CMake/GCC
    $envVars = [ordered]@{
        'PATH'           = @($mingwBin,
                             (Join-Path $toolDir 'build/cmake/bin'),
                             (Join-Path $toolDir 'build/ninja'),
                             (Join-Path $toolDir 'sdk/dotnet/11.0.100'),
                             'C:\Windows\system32',
                             'C:\Windows') -join ';'

        # .NET 隔离
        'DOTNET_ROOT'              = Join-Path $toolDir 'sdk/dotnet/11.0.100'
        'DOTNET_MULTILEVEL_LOOKUP' = '0'
        'DOTNET_CLI_TELEMETRY_OPTOUT' = '1'
        'DOTNET_NOLOGO'            = '1'
        'DOTNET_CLI_HOME'          = Join-Path $root 'tmp/dotnet/cli-home'

        # NuGet 缓存全部落到 tmp
        'NUGET_PACKAGES'           = Join-Path $root 'tmp/nuget/packages'
        'NUGET_HTTP_CACHE_PATH'    = Join-Path $root 'tmp/nuget/http-cache'
        'NUGET_PLUGINS_CACHE_PATH' = Join-Path $root 'tmp/nuget/plugins-cache'

        # 临时目录
        'TEMP' = Join-Path $root 'tmp/build/temp'
        'TMP'  = Join-Path $root 'tmp/build/temp'
    }

    foreach ($name in $envVars.Keys) {
        $script:previousEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $envVars[$name], 'Process')
        if ($name -in @('DOTNET_CLI_HOME', 'NUGET_PACKAGES', 'NUGET_HTTP_CACHE_PATH',
                        'NUGET_PLUGINS_CACHE_PATH', 'TEMP', 'TMP')) {
            New-Item -ItemType Directory -Path $envVars[$name] -Force | Out-Null
        }
    }
}

function Restore-BuildEnvironment {
    if (-not $script:previousEnv) { return }
    foreach ($name in $script:previousEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $script:previousEnv[$name], 'Process')
    }
}

# ============================================================
# 3. 原生核心
# ============================================================

function Invoke-NativeConfigure {
    Write-Host '=== 配置原生构建 ===' -ForegroundColor Cyan

    # CMake 参数值统一转正斜杠，否则反斜杠会被当成转义序列
    $cmakeArgs = @(
        '-S', (ConvertTo-CMakePath $root)
        '-B', (ConvertTo-CMakePath $nativeBuildDir)
        '-G', 'Ninja'
        "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninjaExe)"
        '-DCMAKE_BUILD_TYPE=Release'
        "-DCMAKE_C_COMPILER=$(ConvertTo-CMakePath (Join-Path $mingwBin 'gcc.exe'))"
        "-DCMAKE_CXX_COMPILER=$(ConvertTo-CMakePath (Join-Path $mingwBin 'g++.exe'))"
        "-DCMAKE_RC_COMPILER=$(ConvertTo-CMakePath (Join-Path $mingwBin 'windres.exe'))"
        # 第三方静态库全部走仓库内相对路径
        "-DCMAKE_PREFIX_PATH=$(ConvertTo-CMakePath $depsDir)"
        "-DCMAKE_INCLUDE_PATH=$(ConvertTo-CMakePath (Join-Path $depsDir 'include'))"
        "-DCMAKE_LIBRARY_PATH=$(ConvertTo-CMakePath (Join-Path $depsDir 'lib'))"
        "-DZLIB_INCLUDE_DIR=$(ConvertTo-CMakePath (Join-Path $depsDir 'include'))"
        "-DZLIB_LIBRARY=$(ConvertTo-CMakePath (Join-Path $depsDir 'lib/libz.a'))"
        "-DJPEG_INCLUDE_DIR=$(ConvertTo-CMakePath (Join-Path $depsDir 'include'))"
        "-DJPEG_LIBRARY=$(ConvertTo-CMakePath (Join-Path $depsDir 'lib/libjpeg.a'))"
        "-DPIXMAN_INCLUDE_DIRS=$(ConvertTo-CMakePath (Join-Path $depsDir 'include/pixman-1'))"
        "-DPIXMAN_LIBRARIES=$(ConvertTo-CMakePath (Join-Path $depsDir 'lib/libpixman-1.a'))"
    )

    & $cmakeExe @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败（退出码 $LASTEXITCODE）"
    }
}

function Invoke-NativeBuild {
    Write-Host '=== 编译原生核心 DLL ===' -ForegroundColor Cyan

    & $cmakeExe --build $nativeBuildDir --target TuPigVNC.Core --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "原生核心编译失败（退出码 $LASTEXITCODE）"
    }

    if (-not (Test-Path -LiteralPath $coreDllPath -PathType Leaf)) {
        throw "编译未产出预期 DLL：$coreDllPath"
    }
    Write-Host "  [OK] $coreDllPath" -ForegroundColor Green
}

# ============================================================
# 4. WPF 前端
# ============================================================

function Invoke-WpfPublish {
    Write-Host '=== 发布 WPF 前端 ===' -ForegroundColor Cyan

    $csproj = Join-Path $root 'src/ui/TuPigVNC.Wpf/TuPigVNC.Wpf.csproj'

    & $dotnetExe publish $csproj `
        --configuration Release `
        --runtime win-x64 `
        --self-contained true `
        -p:Platform=x64 `
        "-p:TupigVncBuildDir=$nativeBuildDir" `
        "-p:RestoreConfigFile=$(Join-Path $root 'NuGet.Config')" `
        --output $publishDir
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet publish 失败（退出码 $LASTEXITCODE）"
    }

    # 用刚构建的 DLL 覆盖，避免 PreserveNewest 留下旧文件
    $destCore = Join-Path $publishDir 'TuPigVNC.Core.dll'
    Copy-Item -LiteralPath $coreDllPath -Destination $destCore -Force
    Write-Host "  [OK] $destCore" -ForegroundColor Green
}

# ============================================================
# 5. 产物校验
# ============================================================

function Test-PublishOutput {
    Write-Host '=== 校验产物 ===' -ForegroundColor Cyan

    $expected = @('TuPigVNC.exe', 'TuPigVNC.dll', 'TuPigVNC.Core.dll')
    foreach ($name in $expected) {
        $path = Join-Path $publishDir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "发布产物缺少 $name（$path）"
        }
        $size = [math]::Round((Get-Item -LiteralPath $path).Length / 1KB, 1)
        Write-Host "  [OK] $name ($size KB)" -ForegroundColor Green
    }
}

# ============================================================
# 6. Inno 安装包
# ============================================================

function Invoke-Installer {
    Write-Host '=== 生成安装包 ===' -ForegroundColor Cyan

    $iss = Join-Path $nativeBuildDir 'release/tigervnc.iss'
    if (-not (Test-Path -LiteralPath $iss -PathType Leaf)) {
        throw "缺少配置后的安装脚本：$iss"
    }

    Push-Location (Split-Path -Parent $iss)
    try {
        & $isccExe '/o.' '/DWIN64' '/FTuPigVNC_x64' $iss
        if ($LASTEXITCODE -ne 0) {
            throw "Inno Setup 编译失败（退出码 $LASTEXITCODE）"
        }
    } finally {
        Pop-Location
    }

    $setup = Join-Path $nativeBuildDir 'release/TuPigVNC_x64.exe'
    if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
        throw "未生成安装包：$setup"
    }
    Write-Host "  [OK] $setup" -ForegroundColor Green
}

# ============================================================
# 主流程
# ============================================================

$sw = [Diagnostics.Stopwatch]::StartNew()

try {
    Test-Toolchain

    if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
        Write-Host "清空构建目录 $buildRoot ..." -ForegroundColor Yellow
        Remove-Item -LiteralPath $buildRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

    Set-BuildEnvironment
    try {
        if ($Target -eq '' -or $Target -eq 'Core') {
            Invoke-NativeConfigure
            Invoke-NativeBuild
        }
        if ($Target -eq '' -or $Target -eq 'Wpf') {
            Invoke-WpfPublish
            Test-PublishOutput
        }
        if ($Installer) {
            Invoke-Installer
        }
    } finally {
        Restore-BuildEnvironment
    }

    $sw.Stop()
    Write-Host ''
    Write-Host ("构建完成，用时 {0:N1} 秒" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    Write-Host "  原生核心: $coreDllPath" -ForegroundColor Gray
    Write-Host "  发布目录: $publishDir" -ForegroundColor Gray
} catch {
    Write-Host ''
    Write-Host "构建失败：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
