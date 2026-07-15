<#
Builds tools/firmware-updater/main.c and fetches dfu-util.exe (the actual
customer-facing flashing tool for --target=dfu). Together these are the
complete toolset a customer needs -- everything lands in .\build\
(gitignored), so a fresh checkout + `.\build.ps1` on any machine with
MSYS2 installed reproduces a working setup with no manual steps.

MSYS2 itself is NOT bundled with Windows and is a real manual install
(https://www.msys2.org/, then `pacman -S mingw-w64-x86_64-gcc`) -- not
every machine that just wants to *use* this tool should need a whole
dev toolchain for a ~150-line C program. That's what .\dist\ is for: a
git-tracked, prebuilt copy of firmware-updater.exe + dfu-util.exe: pull
the repo, no compiler needed, just run what's in dist\.

Only rebuild + republish to dist\ when main.c actually changes:
    .\build.ps1 -Publish

st-flash.exe (--target=stlink) is NOT fetched or published here -- that's
a dev/testing-only path (see FIRMWARE_UPDATER.md) with an awkward
system-wide chip database dependency that isn't worth scripting into a
customer-facing build step. Set it up manually per FIRMWARE_UPDATER.md if
you need to test against a Nucleo.

Run from PowerShell: .\build.ps1  (or  .\build.ps1 -Publish)
#>

param(
    [switch]$Publish
)

$ErrorActionPreference = "Continue"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build"
$distDir = Join-Path $root "dist"
$msys2 = "C:\msys64"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# --- 1. Compile main.c --------------------------------------------------
$gcc = "$msys2\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) {
    throw "MinGW gcc not found at $gcc. Install MSYS2 (mingw-w64-x86_64-gcc package) first."
}

$outExe = Join-Path $buildDir "firmware-updater.exe"
& $gcc -O2 -Wall -o $outExe (Join-Path $root "main.c")
if ($LASTEXITCODE -ne 0) { throw "main.c build failed" }
Write-Host "==> Built: $outExe"

# --- 2. Fetch dfu-util.exe ------------------------------------------------
# Using dfu-util-static.exe from the official release, renamed to
# dfu-util.exe (the literal filename main.c expects next to it). Chose the
# *static* build deliberately: the regular dfu-util.exe in the same zip
# needs libusb-1.0.dll alongside it (confirmed on hardware: fails with
# STATUS_DLL_NOT_FOUND without it), while the static build has zero
# runtime dependencies -- nothing else to bundle, nothing to forget.
$dfuUtilExe = Join-Path $buildDir "dfu-util.exe"
if (-not (Test-Path $dfuUtilExe)) {
    Write-Host "==> Downloading dfu-util..."
    $dfuZip = Join-Path $buildDir "dfu-util-win64.zip"
    Invoke-WebRequest -Uri "https://dfu-util.sourceforge.net/releases/dfu-util-0.9-win64.zip" -OutFile $dfuZip
    $dfuTmp = Join-Path $buildDir "dfu-util-tmp"
    Expand-Archive -Path $dfuZip -DestinationPath $dfuTmp -Force
    Copy-Item (Join-Path $dfuTmp "dfu-util-0.9-win64\dfu-util-static.exe") $dfuUtilExe -Force
    Remove-Item -Recurse -Force $dfuTmp
    Remove-Item -Force $dfuZip
}
if (-not (Test-Path $dfuUtilExe)) { throw "dfu-util.exe fetch failed" }
Write-Host "==> Ready: $dfuUtilExe"

Write-Host "==> Done. $buildDir now has the complete customer-facing toolset"
Write-Host "    (firmware-updater.exe + dfu-util.exe)."

# --- 3. Publish to dist\ (git-tracked), only when asked ------------------
if ($Publish) {
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
    Copy-Item $outExe (Join-Path $distDir "firmware-updater.exe") -Force
    Copy-Item $dfuUtilExe (Join-Path $distDir "dfu-util.exe") -Force
    Write-Host "==> Published to $distDir -- remember to git add + commit this."
}
