#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$Config = "Release",
    [string]$DistDir = "",
    [string]$ArtifactDir = "",
    [string]$ZipName = "CodexRemoteBootstrap-Windows-x64.zip"
)

$ErrorActionPreference = "Stop"

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

if (-not $DistDir) {
    $DistDir = Join-Path $SourceDir "dist"
}
if (-not $ArtifactDir) {
    $ArtifactDir = Join-Path $SourceDir "artifacts"
}

function Get-AppVersion {
    $cmakePath = Join-Path $SourceDir "CMakeLists.txt"
    $text = Get-Content -LiteralPath $cmakePath -Raw
    if ($text -notmatch 'project\(\s*CodexRemoteBootstrap\s+VERSION\s+(\d+\.\d+\.\d+)') {
        throw "Unable to parse VERSION from CMakeLists.txt"
    }
    return $Matches[1]
}

function Find-AppExe {
    $names = @("CodexRemoteBootstrap.exe", "appCodexRemoteBootstrap.exe")
    $dirs = @(
        (Join-Path $BuildDir $Config),
        $BuildDir
    )
    foreach ($dir in $dirs) {
        foreach ($name in $names) {
            $candidate = Join-Path $dir $name
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Filter "CodexRemoteBootstrap.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }
    return $null
}

function Find-WinDeployQt {
    foreach ($name in @("windeployqt", "windeployqt6")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }
    if ($env:QT_ROOT_DIR) {
        foreach ($name in @("windeployqt.exe", "windeployqt6.exe")) {
            $candidate = Join-Path $env:QT_ROOT_DIR "bin\$name"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }
    return $null
}

$version = Get-AppVersion
$exe = Find-AppExe
if (-not $exe) {
    throw "CodexRemoteBootstrap.exe not found under $BuildDir"
}

$windeployqt = Find-WinDeployQt
if (-not $windeployqt) {
    throw "windeployqt not found. Install Qt tools or add the Qt bin directory to PATH."
}

Write-Host "App version : $version"
Write-Host "EXE         : $exe"
Write-Host "windeployqt : $windeployqt"

if (Test-Path -LiteralPath $DistDir) {
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $DistDir, $ArtifactDir | Out-Null

$distExe = Join-Path $DistDir "CodexRemoteBootstrap.exe"
Copy-Item -LiteralPath $exe -Destination $distExe -Force

$qmlDir = Join-Path $SourceDir "qml"
if (-not (Test-Path -LiteralPath $qmlDir)) {
    throw "QML directory not found: $qmlDir"
}

& $windeployqt `
    --release `
    --compiler-runtime `
    --qmldir $qmlDir `
    --no-translations `
    --no-opengl-sw `
    --skip-plugin-types qmltooling `
    $distExe
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Get-ChildItem -LiteralPath $DistDir -Filter *.pdb -Recurse | Remove-Item -Force

$license = Join-Path $SourceDir "LICENSE"
if (Test-Path -LiteralPath $license) {
    Copy-Item -LiteralPath $license -Destination (Join-Path $DistDir "LICENSE.txt") -Force
}

$readme = @"
Codex Remote Bootstrap v$version
Windows x64 portable build

Extract this folder and run CodexRemoteBootstrap.exe.
Qt runtime files are included; a local Qt SDK is not required.

This ZIP does not include OpenAI Codex CLI packages:
  codex-aarch64-unknown-linux-musl.tar.gz
  codex-x86_64-unknown-linux-musl.tar.gz

The application downloads and caches those packages itself:

  %LOCALAPPDATA%\CodexRemoteBootstrap\packages\

Requirements:
  Windows OpenSSH Client (ssh, scp, ssh-keygen in PATH)
"@
Set-Content -LiteralPath (Join-Path $DistDir "README.txt") -Value $readme -Encoding utf8

$zipPath = Join-Path $ArtifactDir $ZipName
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $DistDir "*") -DestinationPath $zipPath -CompressionLevel Optimal -Force

$hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
$sumLine = "$($hash.Hash)  $ZipName"
Set-Content -LiteralPath (Join-Path $ArtifactDir "SHA256SUMS.txt") -Value $sumLine -Encoding ascii

Write-Host "Packed      : $zipPath"
Write-Host "SHA256      : $sumLine"
