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
    Write-Host 'Checking native-tick force integration...'
    & python 'tests\content_force_hooks_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Content force hook integration test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking native-layout per-map tileset integration...'
    & python 'tests\map_tileset_hooks_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Map tileset hook integration test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking bounded and padding-safe native atlas packing...'
    & python 'tests\map_atlas_loader_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Map atlas loader safety test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking retired map-registry generation cleanup...'
    & python 'tests\custom_maps_retirement_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Custom map retirement test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking mod content-registry native visual options...'
    & python 'tests\lua_content_native_visual_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Lua content native-visual test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking map.lua native-hook integration...'
    & python 'tests\map_script_hooks_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script hook integration test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking map.lua rollback-state integration...'
    & python 'tests\map_script_rollback_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script rollback integration test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Checking fail-closed online map.lua readiness...'
    & python 'tests\map_script_online_gate_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Online map script readiness test failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building deterministic content registry behavior tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\content_registry_test.c' 'content_registry.c' `
        -o 'build\content_registry_test.exe' -lbcrypt
    if ($LASTEXITCODE -ne 0) {
        throw "Content registry test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\content_registry_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Content registry tests failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building deterministic content tile interaction tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\content_tiles_test.c' 'content_tiles.c' 'content_registry.c' `
        -o 'build\content_tiles_test.exe' -lbcrypt
    if ($LASTEXITCODE -ne 0) {
        throw "Content tile test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\content_tiles_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Content tile interaction tests failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building generated-map render bridge tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\content_bridge_test.c' 'content_bridge.c' 'content_tiles.c' `
        'content_registry.c' 'custom_maps.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' 'log.c' `
        -o 'build\content_bridge_test.exe' -lbcrypt '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "Content bridge test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\content_bridge_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Content bridge tests failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building isolated deterministic map.lua runtime tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\map_script_test.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' `
        -o 'build\map_script_test.exe' '-lluajit-5.1' '-lbcrypt'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\map_script_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script tests failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building checked-in spring demo behavior tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\spring_demo_test.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' `
        -o 'build\spring_demo_test.exe' '-lluajit-5.1' '-lbcrypt'
    if ($LASTEXITCODE -ne 0) {
        throw "Spring demo test compilation failed with exit code $LASTEXITCODE."
    }
    & '.\build\spring_demo_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Spring demo tests failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Building the V2 package/fixture regression test...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        '-DCUSTOM_MAPS_TESTING' 'tests\custom_maps_v2_test.c' 'custom_maps.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' 'log.c' `
        -o 'build\custom_maps_v2_test.exe' -lbcrypt '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "V2 map test compilation failed with exit code $LASTEXITCODE."
    }

    & '.\build\custom_maps_v2_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "V2 map regression test failed with exit code $LASTEXITCODE."
    }
    Write-Host 'PASS: V2 collision presets, forces, sensors, temporary visual offsets, native underlay, mirroring, registry identity, demo package, and parser regressions are valid.' -ForegroundColor Green
    Write-Host 'The visual bridge still needs the short in-game check documented in MAP_FORMAT.md.'
} finally {
    Pop-Location -ErrorAction SilentlyContinue
}
