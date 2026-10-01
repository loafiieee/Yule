[CmdletBinding()]
param(
    [string]$SdkDir = 'C:\Users\potato\Downloads\eos-sdk-1.18.1.2-minimal',
    [string]$ClientSecretFile = '',
    [int]$UserInfoPort = 47781,
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$serverDir = $PSScriptRoot
$repo = Split-Path -Parent $serverDir
$mingw = 'C:\msys64\mingw32\bin'
$msys = 'C:\msys64\usr\bin'
$oldPath = $env:PATH
$oldEnv = @{}
$settings = @{}
if (-not $ClientSecretFile) { $ClientSecretFile = Join-Path $repo 'eos_client_secret.txt' }
try {
    Push-Location $serverDir
    $env:PATH = "$repo;$mingw;$msys;$env:PATH"
    foreach ($name in @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')) {
        $found = @($repo, $mingw, $msys) | Where-Object {
            Test-Path -LiteralPath (Join-Path $_ $name) -PathType Leaf
        }
        if (-not $found) { throw "Required compiler runtime DLL is missing: $name" }
    }
    if ($UserInfoPort -lt 1 -or $UserInfoPort -gt 65535) { throw 'Invalid UserInfo port.' }
    $include = Join-Path $SdkDir 'Include'
    $library = Join-Path $SdkDir 'Lib\EOSSDK-Win32-Shipping.lib'
    $runtime = Join-Path $SdkDir 'Bin\EOSSDK-Win32-Shipping.dll'
    foreach ($file in @((Join-Path $include 'eos_sdk.h'), $library, $runtime, $ClientSecretFile)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw 'The Win32 EOS SDK or restricted client credential is missing.'
        }
    }
    $credential = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $ClientSecretFile).Path).Trim()
    if ($credential -notmatch '^[A-Za-z0-9_-]{16,256}$') { throw 'Invalid client credential format.' }
    $runtimeDir = Join-Path $serverDir 'eos_runtime'
    New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
    $verifier = Join-Path $runtimeDir 'eos_verify_token.exe'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -static -static-libgcc `
        -isystem $include eos_verify_token.c $library -o $verifier
    if ($LASTEXITCODE -ne 0) { throw 'EOS verifier compilation failed.' }
    Copy-Item -LiteralPath $runtime -Destination $runtimeDir -Force
    $settings = @{
        HOST = '127.0.0.1'
        UDP_HOST = '127.0.0.1'
        TLS_CERT_FILE = ''
        TLS_KEY_FILE = ''
        EOS_PRODUCT_ID = 'a90d288fedff4672b082ed3e92a12484'
        EOS_SANDBOX_ID = '02dd8fe04e294815881f54986c73629a'
        EOS_DEPLOYMENT_ID = '746ee96def484dcc8bbc9806343352b2'
        EOS_CLIENT_ID = 'xyza7891PRKu95tnw9S2svEZhSaWbVkn'
        EOS_CLIENT_SECRET = $credential
        EOS_VERIFY_BINARY = $verifier
        EOS_USERINFO_HOST = '127.0.0.1'
        EOS_USERINFO_PORT = [string]$UserInfoPort
    }
    foreach ($key in $settings.Keys) {
        $oldEnv[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $settings[$key], 'Process')
    }
    Write-Host "EOS verifier prepared. UserInfo endpoint: http://127.0.0.1:$UserInfoPort/eos/userinfo"
    if (-not $PrepareOnly) {
        Write-Host 'auth.loafiieee.com must route to this UserInfo listener for EOS login.'
        & node server.js
        if ($LASTEXITCODE -ne 0) { throw "Local server exited with code $LASTEXITCODE." }
    }
} finally {
    foreach ($key in $oldEnv.Keys) {
        [Environment]::SetEnvironmentVariable($key, $oldEnv[$key], 'Process')
    }
    $credential = $null
    $settings.Clear()
    $env:PATH = $oldPath
    Pop-Location
}
