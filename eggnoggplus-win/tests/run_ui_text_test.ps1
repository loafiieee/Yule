[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$oldPath = $env:PATH
try {
    Push-Location $repo
    $search = @($repo, 'C:\msys64\mingw32\bin', 'C:\msys64\usr\bin')
    $env:PATH = ($search -join ';') + ';' + $env:PATH
    foreach ($dll in @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')) {
        if (-not ($search | Where-Object { Test-Path -LiteralPath (Join-Path $_ $dll) -PathType Leaf })) {
            throw "Required test runtime DLL is missing: $dll"
        }
    }
    $output = Join-Path $repo 'build\ui_text_test'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $exe = Join-Path $output 'ui_text_test.exe'
    & python tests/extract_hub_preview.py
    if ($LASTEXITCODE -ne 0) { throw 'Actual hub preview extraction failed.' }
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -DUI_TEXT_TEST -I. "-I$output" tests/ui_text_test.c ui_text.c -o $exe -lgdi32 -lopengl32 -lm
    if ($LASTEXITCODE -ne 0) { throw 'UI font test compilation failed.' }
    foreach ($width in @(960, 1280, 1920)) {
        & $exe (Join-Path $output "$width.bmp") $width
        if ($LASTEXITCODE -ne 0) { throw 'UI font render test failed.' }
    }
    & $exe (Join-Path $output 'settings.bmp') 1280 2
    if ($LASTEXITCODE -ne 0) { throw 'Settings font render test failed.' }
    foreach ($width in @(960, 1280, 1920)) {
        & $exe (Join-Path $output "mods-$width.bmp") $width 3
        if ($LASTEXITCODE -ne 0) { throw 'Mod manager font render test failed.' }
        & $exe (Join-Path $output "console-$width.bmp") $width 4
        if ($LASTEXITCODE -ne 0) { throw 'Console font render test failed.' }
        foreach ($state in @(@('login',5),@('queue',6),@('match',7))) {
            & $exe (Join-Path $output "$($state[0])-$width.bmp") $width $state[1]
            if ($LASTEXITCODE -ne 0) { throw "$($state[0]) render test failed." }
        }
    }
    Add-Type -AssemblyName System.Drawing
    Get-ChildItem -LiteralPath $output -Filter '*.bmp' | ForEach-Object {
        $bitmap = [System.Drawing.Image]::FromFile($_.FullName)
        try { $bitmap.Save([System.IO.Path]::ChangeExtension($_.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }
} finally {
    $env:PATH = $oldPath
    Pop-Location
}
