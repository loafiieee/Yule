[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\msys64\mingw32\bin'
$msysBin = 'C:\msys64\usr\bin'
$tempRoot = $null

function Assert-RequiredRuntimeDlls([string[]]$Names) {
    $searchDirectories = @($repo, $mingwBin, $msysBin)
    foreach ($name in $Names) {
        if (-not ($searchDirectories | Where-Object {
            Test-Path -LiteralPath (Join-Path $_ $name) -PathType Leaf
        })) {
            throw "Required test runtime DLL '$name' was not found. Refusing to launch MinGW test executables."
        }
    }
}

function ConvertTo-WindowsCommandLineArgument([string]$Value) {
    $builder = New-Object Text.StringBuilder
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) {
            $slashes++
            continue
        }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $slashes) + 1)
            [void]$builder.Append([char]34)
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) {
            [void]$builder.Append([char]92, $slashes)
            $slashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($slashes -gt 0) {
        [void]$builder.Append([char]92, 2 * $slashes)
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

try {
    Push-Location $repo
    $env:PATH = "$repo;$mingwBin;$msysBin;$env:PATH"
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found.'
    }
    Assert-RequiredRuntimeDlls @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null

    Write-Host 'Building standalone updater-helper cut-point tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -static-libgcc `
        -o 'build\update_helper_test.exe' 'tests\update_helper_test.c' `
        -lwinhttp -lbcrypt -lws2_32
    if ($LASTEXITCODE -ne 0) {
        throw "Updater-helper test compilation failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building guarded updater relaunch-forwarding fixtures...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -static -static-libgcc `
        -DUPDATE_EXT_HELPER -DUPDATE_EXT_HELPER_TEST -municode -mwindows `
        -o 'build\YuleUpdater_test.exe' 'updater_helper.c' 'update_ext.c' `
        -lwinhttp -lbcrypt -lws2_32 -lshell32 -luser32
    if ($LASTEXITCODE -ne 0) {
        throw "Updater test compilation failed with exit code $LASTEXITCODE."
    }
    $updaterImports = (& objdump -p 'build\YuleUpdater_test.exe' | Out-String)
    foreach ($replaceableDll in @(
        'SDL2.dll',
        'lua51.dll',
        'libgcc_s_dw2-1.dll',
        'libwinpthread-1.dll',
        'SDL2_mixer.dll'
    )) {
        if ($updaterImports -match
            ('DLL Name:\s*' + [regex]::Escape($replaceableDll))) {
            throw "Updater test build imports replaceable payload $replaceableDll."
        }
    }
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -static-libgcc `
        -municode -o 'build\launcher_arg_child.exe' `
        'tests\launcher_arg_child.c'
    if ($LASTEXITCODE -ne 0) {
        throw "Launcher child fixture compilation failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Running helper power-cut/recovery matrix...'
    & '.\build\update_helper_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Updater-helper tests failed with exit code $LASTEXITCODE."
    }

    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build'))
    $tempRoot = Join-Path $buildRoot ("launcher_forward_tmp_" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $tempRoot 'mods') | Out-Null
    $child = Join-Path $tempRoot 'launcher_arg_child.exe'
    Copy-Item -LiteralPath 'build\launcher_arg_child.exe' -Destination $child
    [IO.File]::WriteAllBytes((Join-Path $tempRoot 'SDL2.dll'), [byte[]](1, 2, 3, 4))
    $argOutput = Join-Path $tempRoot 'args.txt'
    $oldArgOutput = $env:YULE_LAUNCHER_ARG_OUTPUT
    $oldHoldMs = $env:YULE_LAUNCHER_HOLD_MS
    $env:YULE_LAUNCHER_ARG_OUTPUT = $argOutput
    try {
        $holdOutput = Join-Path $tempRoot 'holding-args.txt'
        $holdInfo = New-Object Diagnostics.ProcessStartInfo
        $holdInfo.FileName = $child
        $holdInfo.UseShellExecute = $false
        $holdInfo.CreateNoWindow = $true
        $holdInfo.EnvironmentVariables['YULE_LAUNCHER_ARG_OUTPUT'] = $holdOutput
        $holdInfo.EnvironmentVariables['YULE_LAUNCHER_HOLD_MS'] = '10000'
        $holdingChild = [Diagnostics.Process]::Start($holdInfo)
        try {
            for ($attempt = 0; $attempt -lt 100 -and
                 -not (Test-Path -LiteralPath $holdOutput -PathType Leaf);
                 $attempt++) {
                Start-Sleep -Milliseconds 25
            }
            if ($holdingChild.HasExited) {
                throw 'The same-install test process exited before updater preflight.'
            }
            $guardArguments = @(
                '--test-no-wait',
                '--recover-only',
                "--test-root=$tempRoot",
                "--test-game=$child"
            )
            $guardInfo = New-Object Diagnostics.ProcessStartInfo
            $guardInfo.FileName = Join-Path $repo 'build\YuleUpdater_test.exe'
            $guardInfo.Arguments = (($guardArguments | ForEach-Object {
                ConvertTo-WindowsCommandLineArgument $_
            }) -join ' ')
            $guardInfo.UseShellExecute = $false
            $guardInfo.CreateNoWindow = $true
            $guard = [Diagnostics.Process]::Start($guardInfo)
            $guard.WaitForExit()
            if ($guard.ExitCode -ne 2) {
                throw "Updater did not block another same-install process (exit $($guard.ExitCode))."
            }
        } finally {
            if ($holdingChild -and -not $holdingChild.HasExited) {
                $holdingChild.Kill()
                $holdingChild.WaitForExit()
            }
        }

        $arguments = @(
            '--test-no-wait',
            "--test-root=$tempRoot",
            "--test-game=$child",
            'plain',
            'two words',
            'quote"inside',
            'trail\',
            '--yule-uri=yule://queue/casual?x=1&y=2'
        )
        $processInfo = New-Object Diagnostics.ProcessStartInfo
        $processInfo.FileName = Join-Path $repo 'build\YuleUpdater_test.exe'
        $processInfo.Arguments = (($arguments | ForEach-Object {
            ConvertTo-WindowsCommandLineArgument $_
        }) -join ' ')
        $processInfo.UseShellExecute = $false
        $processInfo.CreateNoWindow = $true
        $process = [Diagnostics.Process]::Start($processInfo)
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "Updater relaunch fixture failed with exit code $($process.ExitCode)."
        }
        for ($attempt = 0; $attempt -lt 100 -and
             -not (Test-Path -LiteralPath $argOutput -PathType Leaf); $attempt++) {
            Start-Sleep -Milliseconds 50
        }
        if (-not (Test-Path -LiteralPath $argOutput -PathType Leaf)) {
            throw 'The benign launcher child did not produce its argument report.'
        }
        $actual = @(Get-Content -LiteralPath $argOutput)
        $expected = @(
            '6',
            $child,
            'plain',
            'two words',
            'quote"inside',
            'trail\',
            '--yule-uri=yule://queue/casual?x=1&y=2'
        )
        if ($actual.Count -ne $expected.Count) {
            throw "Launcher argument count mismatch: got $($actual.Count), expected $($expected.Count)."
        }
        for ($i = 0; $i -lt $expected.Count; $i++) {
            if ($actual[$i] -cne $expected[$i]) {
                throw "Launcher argument $i mismatch: got '$($actual[$i])', expected '$($expected[$i])'."
            }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $tempRoot 'mods\updater.log') -PathType Leaf)) {
            throw 'The updater did not write its privacy-safe recovery log.'
        }
    } finally {
        if ($null -eq $oldArgOutput) {
            Remove-Item Env:YULE_LAUNCHER_ARG_OUTPUT -ErrorAction SilentlyContinue
        } else {
            $env:YULE_LAUNCHER_ARG_OUTPUT = $oldArgOutput
        }
        if ($null -eq $oldHoldMs) {
            Remove-Item Env:YULE_LAUNCHER_HOLD_MS -ErrorAction SilentlyContinue
        } else {
            $env:YULE_LAUNCHER_HOLD_MS = $oldHoldMs
        }
    }

    Write-Host 'PASS: one-shot updater helper and relaunch forwarding tests completed in disposable roots.' -ForegroundColor Green
} finally {
    if ($tempRoot) {
        $resolvedBuild = [IO.Path]::GetFullPath((Join-Path $repo 'build'))
        $resolvedTemp = [IO.Path]::GetFullPath($tempRoot)
        if ([IO.Path]::GetDirectoryName($resolvedTemp) -eq $resolvedBuild -and
            (Split-Path -Leaf $resolvedTemp) -like 'launcher_forward_tmp_*') {
            Remove-Item -LiteralPath $resolvedTemp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Pop-Location -ErrorAction SilentlyContinue
}
