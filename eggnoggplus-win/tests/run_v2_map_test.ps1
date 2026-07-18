[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

try {
    Push-Location $repo
    $mingwBin = 'C:\msys64\mingw32\bin'
    if (Test-Path (Join-Path $mingwBin 'gcc.exe')) {
        $env:PATH = "$mingwBin;$env:PATH"
    }
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found. Install MSYS2 MinGW32 or add gcc.exe to PATH.'
    }

    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    Write-Host 'Building the V2 package/fixture regression test...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\custom_maps_v2_test.c' 'custom_maps.c' 'content_registry.c' 'log.c' `
        -o 'build\custom_maps_v2_test.exe' -lbcrypt
    if ($LASTEXITCODE -ne 0) {
        throw "V2 map test compilation failed with exit code $LASTEXITCODE."
    }

    & '.\build\custom_maps_v2_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "V2 map regression test failed with exit code $LASTEXITCODE."
    }
    Write-Host 'PASS: the shipped V2 demo package and parser regression cases are valid.' -ForegroundColor Green
    Write-Host 'The visual bridge still needs the short in-game check documented in MAP_FORMAT.md.'
} finally {
    Pop-Location -ErrorAction SilentlyContinue
}
