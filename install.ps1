#Requires -Version 5.1
param(
    [string]$InstallDir = $(
        if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'Bluejs' }
        else { Join-Path $HOME '.blue' }
    ),
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

if ($Uninstall) {
    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
        Write-Host "Removed $InstallDir"
    }
    $userPath = [Environment]::GetEnvironmentVariable('PATH', 'User')
    if ($userPath) {
        $parts = $userPath -split ';' | Where-Object { $_ -ne '' -and ($_.TrimEnd('\') -ne $InstallDir.TrimEnd('\')) }
        [Environment]::SetEnvironmentVariable('PATH', ($parts -join ';'), 'User')
        Write-Host "Removed $InstallDir from user PATH."
    }
    exit 0
}

Write-Host ""
Write-Host "==> Bluejs installer"
Write-Host "    Install directory: $InstallDir"
Write-Host ""

$srcBin = Join-Path $ScriptDir 'blue.exe'
if (-not (Test-Path $srcBin)) {
    $srcBin = Join-Path $ScriptDir 'blue_bin.exe'
}
if (-not (Test-Path $srcBin)) {
    Write-Host "ERROR: blue.exe or blue_bin.exe not found in $ScriptDir." -ForegroundColor Red
    exit 1
}

$missing = @()
foreach ($f in @('vendor\js\esprima.js', 'vendor\js\babel.min.js', 'src', 'vendor\quickjs')) {
    if (-not (Test-Path (Join-Path $ScriptDir $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Host "ERROR: Missing required files: $($missing -join ', ')" -ForegroundColor Red
    Write-Host "Re-download and extract the full blue-windows-x86_64.zip archive."
    exit 1
}

Write-Host "Checking dependencies..."
$cxxFound = $false
$cxxName = ''
foreach ($cxx in @('cl.exe', 'g++.exe', 'clang++.exe', 'c++.exe')) {
    if (Get-Command $cxx -ErrorAction SilentlyContinue) {
        $cxxFound = $true
        $cxxName = $cxx
        break
    }
}
if ($cxxFound) {
    Write-Host "OK  C++ compiler: $cxxName"
} else {
    Write-Host "WARN  No C++ compiler found. Install Visual Studio Build Tools or MinGW." -ForegroundColor Yellow
}

$nodeFound = $false
if (Get-Command node -ErrorAction SilentlyContinue) {
    $nodeFound = $true
    Write-Host "OK  Node.js found"
} else {
    Write-Host "WARN  Node.js not found. Hybrid/npm builds need Node.js 14+." -ForegroundColor Yellow
}

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir | Out-Null
}

Copy-Item -Path $srcBin -Destination (Join-Path $InstallDir 'blue.exe') -Force
foreach ($dir in @('src', 'vendor', 'tools')) {
    $src = Join-Path $ScriptDir $dir
    if (Test-Path $src) {
        $dest = Join-Path $InstallDir $dir
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        Copy-Item -Path $src -Destination $dest -Recurse
        Write-Host "Copied: $dir"
    }
}

foreach ($f in @('logo.png', 'WebView2Loader.dll')) {
    $src = Join-Path $ScriptDir $f
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination (Join-Path $InstallDir $f) -Force
    }
}

if ($nodeFound) {
    $bundleDir = Join-Path $InstallDir 'tools\jsc-npm-bundle'
    if (Test-Path $bundleDir) {
        Push-Location $bundleDir
        try {
            npm install --no-audit --no-fund
        } catch {
            Write-Host "WARN  npm install failed in $bundleDir" -ForegroundColor Yellow
        }
        Pop-Location
    }
}

$userPath = [Environment]::GetEnvironmentVariable('PATH', 'User')
$pathParts = if ($userPath) { $userPath -split ';' | Where-Object { $_ -ne '' } } else { @() }
$already = $pathParts | Where-Object { $_.TrimEnd('\') -ieq $InstallDir.TrimEnd('\') }
if (-not $already) {
    [Environment]::SetEnvironmentVariable('PATH', (($pathParts + $InstallDir) -join ';'), 'User')
    Write-Host "Added to user PATH: $InstallDir"
    Write-Host "Restart your terminal for the PATH change to take effect."
}

Write-Host ""
Write-Host "OK  Bluejs installed successfully." -ForegroundColor Green
Write-Host "Try: blue --version"
