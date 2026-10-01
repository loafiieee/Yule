[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$server = $null
$oldTestBase = $env:UPDATE_EXT_TEST_CHANNEL_BASE

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
    Assert-RequiredRuntimeDlls @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')
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

    # Include a newly introduced DLL name; the updater must install files
    # that were absent from older releases, without a filename allowlist.
    $channelFixture = Join-Path $repo 'build\eos_update_channel'
    New-Item -ItemType Directory -Force -Path $channelFixture | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'README.txt') -Destination $channelFixture -Force
    [IO.File]::WriteAllText((Join-Path $channelFixture 'EOSSDK-Win32-Shipping.dll'),
        'Updater fixture only; this is not an executable SDK binary.')
    $betaFixture = Join-Path $channelFixture 'beta_runtime'
    New-Item -ItemType Directory -Force -Path $betaFixture | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $betaFixture 'SDL2.dll'),
        [Text.Encoding]::ASCII.GetBytes("YULE_CHANNEL_SWITCH=1`0YULE_FRAMEWORK_VERSION=99.0`0beta fixture"))
    [IO.File]::WriteAllText((Join-Path $betaFixture 'NewTransportRuntime.dll'), 'New beta runtime fixture.')
    $betaManifest = [ordered]@{
        channel_version = 1; release_channel = 'beta'; channel_switch = 1
        version = '99.0'; base = "http://127.0.0.1:$port/beta_runtime/"
        server_host = 'beta.loafiieee.com'; server_port = 47782; server_tls = $true
        files = @('SDL2.dll', 'NewTransportRuntime.dll') | ForEach-Object {
            $fixturePath = Join-Path $betaFixture $_
            [ordered]@{path=$_; sha256=(Get-FileHash $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
                size=(Get-Item $fixturePath).Length; overwrite=$true}
        }
    }
    [IO.File]::WriteAllText((Join-Path $channelFixture 'beta.json'), ($betaManifest | ConvertTo-Json -Depth 5))
    $server = Start-Job -ScriptBlock {
        param($pythonPath, $listenPort, $workingDirectory)
        Set-Location -LiteralPath $workingDirectory
        & $pythonPath '-m' 'http.server' $listenPort '--bind' '127.0.0.1'
        if ($LASTEXITCODE -ne 0) {
            throw "Temporary loopback server exited with code $LASTEXITCODE."
        }
    } -ArgumentList $python.Source, $port, $channelFixture
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
            if ($server.State -in @('Completed', 'Failed', 'Stopped', 'Disconnected')) {
                $serverOutput = (Receive-Job -Job $server -Keep `
                    -ErrorAction SilentlyContinue | Out-String).Trim()
                if ($serverOutput) {
                    throw "The temporary loopback update server exited early: $serverOutput"
                }
                throw "The temporary loopback update server exited early ($($server.State))."
            }
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
    if ($server) {
        Stop-Job -Job $server -ErrorAction SilentlyContinue
        Remove-Job -Job $server -Force -ErrorAction SilentlyContinue
    }
    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'build'))
    if (Test-Path -LiteralPath $buildRoot) {
        Get-ChildItem -LiteralPath $buildRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -like 'update_ext_test_tmp_*' -or
                $_.Name -like 'update_ext_http_tmp_*' -or
                $_.Name -eq 'eos_update_channel'
            } |
            ForEach-Object {
                $candidate = [System.IO.Path]::GetFullPath($_.FullName)
                if ([System.IO.Path]::GetDirectoryName($candidate) -eq $buildRoot) {
                    Remove-Item -LiteralPath $candidate -Recurse -Force `
                        -ErrorAction SilentlyContinue
                }
            }
    }
    $env:UPDATE_EXT_TEST_CHANNEL_BASE = $oldTestBase
    Pop-Location -ErrorAction SilentlyContinue
}
