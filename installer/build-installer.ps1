# Builds the release, collects the files and compiles the setup (needs Inno Setup 6).
# Usage (from the repository root):  powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
  $cmake = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { throw 'cmake not found (install Visual Studio 2022 with C++ or CMake)' }

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
  $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
            "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found - https://jrsoftware.org/isdl.php' }

# A running app locks EDOverlayBoard.exe (linker error LNK1104) - close it first.
$running = Get-Process EDOverlayBoard -ErrorAction SilentlyContinue
if ($running) {
  Write-Host 'Closing the running ED Overlay Board ...'
  $running | Stop-Process -Force
  Start-Sleep -Seconds 1
}
# A running OpenXR game keeps the layer DLL loaded (and locked).
$layerDll = Join-Path $root 'build\bin\Release\EDOverlayBoard_Layer.dll'
if (Test-Path $layerDll) {
  try { [IO.File]::Open($layerDll, 'Open', 'ReadWrite', 'None').Close() }
  catch { throw 'EDOverlayBoard_Layer.dll is in use - please close Elite Dangerous (or other OpenXR games) first.' }
}

$version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(EDOverlayBoard VERSION ([0-9.]+)').Groups[1].Value
Write-Host "ED Overlay Board $version"

& $cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE) { throw 'configure failed' }
& $cmake --build build --config Release
if ($LASTEXITCODE) { throw 'build failed' }

$dist = Join-Path $root 'dist\EDOverlayBoard'
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
& $cmake --install build --config Release --prefix $dist
if ($LASTEXITCODE) { throw 'install failed' }

& $iscc "/DAppVersion=$version" "/DSourceDir=$dist" installer\EDOverlayBoard.iss
if ($LASTEXITCODE) { throw 'Inno Setup failed' }
Write-Host "Setup: dist\EDOverlayBoard-Setup-$version.exe"
