[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

try {
    Push-Location $repo
    $mingwBin = 'C:\msys64\mingw32\bin'
    $msysBin = 'C:\msys64\usr\bin'
    $env:PATH = "$repo;$mingwBin;$msysBin;$env:PATH"

    function Assert-RequiredRuntimeDlls([string[]]$Names) {
        $searchDirectories = @($repo, $mingwBin, $msysBin)
        foreach ($name in $Names) {
            $available = $false
            foreach ($directory in $searchDirectories) {
                if (Test-Path -LiteralPath (Join-Path $directory $name) -PathType Leaf) {
                    $available = $true
                    break
                }
            }
            if (-not $available) {
                throw "Required test runtime DLL '$name' was not found in the repository or MSYS2 runtime directories. Refusing to launch test executables."
            }
        }
    }

    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found. Install MSYS2 MinGW32 or add gcc.exe to PATH.'
    }
    Assert-RequiredRuntimeDlls @('lua51.dll', 'libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    Write-Host 'Building isolated deterministic map.lua runtime tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\map_script_test.c' 'map_script.c' `
        -o 'build\map_script_test.exe' '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\map_script_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script tests failed with exit code $LASTEXITCODE."
    }
    Write-Host 'Building checked-in spring demo behavior tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\spring_demo_test.c' 'map_script.c' `
        -o 'build\spring_demo_test.exe' '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "Spring demo test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\spring_demo_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Spring demo tests failed with exit code $LASTEXITCODE."
    }
    Write-Host 'PASS: sandboxing, contact sensors, temporary visual offsets, rollback replay, contact phases, and faults are deterministic.' -ForegroundColor Green
} finally {
    Pop-Location -ErrorAction SilentlyContinue
}
