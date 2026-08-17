[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\msys64\mingw32\bin'
$msysBin = 'C:\msys64\usr\bin'
$compiler = Join-Path $mingwBin 'gcc.exe'

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw '32-bit MinGW GCC was not found at C:\msys64\mingw32\bin\gcc.exe.'
}

Push-Location $repo
try {
    $env:PATH = "$repo;$mingwBin;$msysBin;$env:PATH"
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    & $compiler @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-static', '-static-libgcc',
        'tests\launch_request_test.c', 'launch_request.c', 'online_control.c',
        '-o', 'build\launch_request_test.exe'
    )
    if ($LASTEXITCODE -ne 0) {
        throw "launch_request_test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\launch_request_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "launch_request_test failed with exit code $LASTEXITCODE."
    }
    & $compiler @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-DLAUNCH_IPC_TEST', '-static', '-static-libgcc',
        'tests\launch_ipc_test.c', 'launch_ipc.c', 'launch_request.c',
        'online_control.c', '-o', 'build\launch_ipc_test.exe',
        '-luser32', '-lshell32'
    )
    if ($LASTEXITCODE -ne 0) {
        throw "launch_ipc_test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\launch_ipc_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "launch_ipc_test failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
