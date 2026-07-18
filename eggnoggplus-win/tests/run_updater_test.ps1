[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$server = $null
$oldTestBase = $env:UPDATE_EXT_TEST_CHANNEL_BASE

try {
    Push-Location $repo

    $mingwBin = 'C:\msys64\mingw32\bin'
    if (Test-Path (Join-Path $mingwBin 'gcc.exe')) {
        $env:PATH = "$mingwBin;$env:PATH"
    }
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found. Install MSYS2 MinGW32 or add gcc.exe to PATH.'
    }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        throw 'Python was not found. It is used only for the temporary loopback web server.'
    }

    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    Write-Host 'Building the isolated updater regression test...'
    & gcc -m32 -std=c11 -DUPDATE_EXT_TEST -Wall -Wextra -Werror -pedantic `
        -o 'build\update_test.exe' 'update_ext.c' -lwinhttp -lbcrypt -lws2_32
    if ($LASTEXITCODE -ne 0) {
        throw "Updater test compilation failed with exit code $LASTEXITCODE."
    }

    $probe = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0)
    $probe.Start()
    $port = ([System.Net.IPEndPoint]$probe.LocalEndpoint).Port
    $probe.Stop()

    $server = Start-Process -FilePath $python.Source `
        -ArgumentList @('-m', 'http.server', "$port", '--bind', '127.0.0.1') `
        -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    $base = "http://127.0.0.1:$port/"
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "${base}README.txt" `
                -TimeoutSec 1
            if ($response.StatusCode -eq 200) {
                $ready = $true
                break
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $ready) {
        throw 'The temporary loopback update server did not start.'
    }

    $env:UPDATE_EXT_TEST_CHANNEL_BASE = $base
    Write-Host "Running parser, hashing, transaction, recovery, and loopback download/apply tests..."
    & '.\build\update_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Updater regression test failed with exit code $LASTEXITCODE."
    }
    Write-Host 'PASS: updater tests completed in disposable build directories; the live SDL2.dll was not changed.' -ForegroundColor Green
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
    $env:UPDATE_EXT_TEST_CHANNEL_BASE = $oldTestBase
    Pop-Location -ErrorAction SilentlyContinue
}
