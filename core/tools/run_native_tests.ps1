<#
.SYNOPSIS
    Build and run the Daly decoder unit tests with MSVC.

.DESCRIPTION
    `pio test -e native` is the portable way to run these tests, but PlatformIO's
    native platform needs a host GCC on PATH. This script is the fallback for a
    Windows box that has Visual Studio but no GCC: it compiles the same two
    translation units with cl.exe, with AddressSanitizer on, and runs them.

    Same sources, same stub Arduino.h, same assertions -- only the compiler differs.

.EXAMPLE
    pwsh -File tools/run_native_tests.ps1
#>

[CmdletBinding()]
param(
    # Directory to place build output in. Defaults to ../.pio/msvc-native.
    [string] $OutDir
)

$ErrorActionPreference = 'Stop'

$core = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $OutDir) {
    $OutDir = Join-Path $core '.pio\msvc-native'
}

# --- locate a Visual Studio build environment -------------------------------

$vcvars = Get-ChildItem -Path @(
    "${env:ProgramFiles}\Microsoft Visual Studio",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
) -Filter 'vcvars64.bat' -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $vcvars) {
    throw "vcvars64.bat not found. Install the MSVC C++ workload, or run 'pio test -e native' with a host GCC instead."
}

Write-Host "Using $($vcvars.FullName)"

# vcvars only sets variables in its own cmd process, so run it and import the result.
& cmd.exe /c "`"$($vcvars.FullName)`" > nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
    }
}

# --- build ------------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$exe = Join-Path $OutDir 'test_daly_decoder.exe'

$sources = @(
    (Join-Path $core 'src\daly_100_bms.cpp'),
    (Join-Path $core 'src\bms_telemetry_codec.cpp'),
    (Join-Path $core 'test\test_daly_decoder\test_main.cpp')
)

$clArgs = @(
    '/nologo'
    '/std:c++17'
    '/EHsc'
    '/W3'
    '/Zi'
    '/fsanitize=address'
    '/DROVER_DEBUG=0'
    "/I$(Join-Path $core 'include')"
    "/I$(Join-Path $core 'test\native_stubs')"
) + $sources + @(
    "/Fe:$exe"
    "/Fo:$OutDir\"
    "/Fd:$OutDir\"
)

Write-Host 'Building...'
& cl @clArgs

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)."
}

# --- run --------------------------------------------------------------------

Write-Host ''
Write-Host 'Running...'
& $exe
$testExit = $LASTEXITCODE

Write-Host ''
if ($testExit -eq 0) {
    Write-Host 'PASS' -ForegroundColor Green
} else {
    Write-Host "FAIL (exit $testExit)" -ForegroundColor Red
}

exit $testExit
