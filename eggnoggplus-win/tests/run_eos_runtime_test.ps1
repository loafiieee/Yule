[CmdletBinding()]
param([string]$SdkDir = 'C:\Users\potato\Downloads\eos-sdk-1.18.1.2-minimal')

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mingw = 'C:\msys64\mingw32\bin'
$msys = 'C:\msys64\usr\bin'
$oldPath = $env:PATH
try {
    Push-Location $repo
    $env:PATH = "$repo;$mingw;$msys;$env:PATH"
    foreach ($name in @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')) {
        $found = @($repo, $mingw, $msys) | Where-Object {
            Test-Path -LiteralPath (Join-Path $_ $name) -PathType Leaf
        }
        if (-not $found) { throw "Required test runtime DLL is missing: $name" }
    }
    $sdkInclude = Join-Path $SdkDir 'Include'
    $sdkRuntime = Join-Path $SdkDir 'Bin\EOSSDK-Win32-Shipping.dll'
    if (-not (Test-Path -LiteralPath (Join-Path $sdkInclude 'eos_sdk.h')) -or
        -not (Test-Path -LiteralPath $sdkRuntime -PathType Leaf)) {
        throw 'The Win32 EOS C SDK is required for this test.'
    }
    $testDir = Join-Path $repo 'build\eos_runtime_test'
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    Copy-Item -LiteralPath $sdkRuntime -Destination $testDir -Force
    $apiList = @('static const struct { const char* name; unsigned int bytes; } eos_test_apis[] = {')
    foreach ($source in @('eos_runtime.c', 'ggpo_transport_eos.c')) {
        $text = Get-Content -LiteralPath (Join-Path $repo $source) -Raw
        foreach ($match in [regex]::Matches($text, 'LOAD\(\w+,\s*(EOS_\w+),\s*(\d+)\);')) {
            $apiList += ('    {"' + $match.Groups[1].Value + '", ' + $match.Groups[2].Value + '},')
        }
    }
    if ($apiList.Count -lt 2) { throw 'No production EOS entry points found.' }
    $apiList += '};'
    $apiList | Set-Content -LiteralPath (Join-Path $testDir 'eos_api_list.h') -Encoding ASCII
    $exe = Join-Path $testDir 'eos_runtime_test.exe'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -DYULE_ENABLE_EOS `
        -isystem $sdkInclude "-I$testDir" tests/eos_runtime_test.c eos_runtime.c -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'EOS runtime test compilation failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'EOS runtime test failed.' }
} finally {
    $env:PATH = $oldPath
    Pop-Location
}
