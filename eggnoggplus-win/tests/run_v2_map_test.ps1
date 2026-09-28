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

    function Invoke-CheckedTest([string]$Path, [string]$FailureLabel) {
        # Windows PowerShell promotes a native program's stderr to ErrorRecord
        # objects. Several runtime tests intentionally exercise and print
        # fail-closed diagnostics, so judge them by their process exit code
        # while still keeping the combined output visible in CI logs.
        $command = '"' + $Path + '" 2>&1'
        & $env:ComSpec /d /s /c $command
        if ($LASTEXITCODE -ne 0) {
            throw "$FailureLabel failed with exit code $LASTEXITCODE."
        }
    }

    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found. Install MSYS2 MinGW32 or add gcc.exe to PATH.'
    }
    Assert-RequiredRuntimeDlls @('lua51.dll', 'libgcc_s_dw2-1.dll', 'libwinpthread-1.dll')

    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    Write-Host 'Building bounded two-dimensional room graph validation tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\room_graph_test.c' 'room_graph.c' `
        -o 'build\room_graph_test.exe'
    if ($LASTEXITCODE -ne 0) {
        throw "Room graph test compilation failed with exit code $LASTEXITCODE."
    }
    Invoke-CheckedTest '.\build\room_graph_test.exe' 'Room graph tests'

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

    Write-Host 'Checking variable-room bounds, movement and spawn integration...'
    & python 'tests\variable_room_hooks_static_test.py'
    if ($LASTEXITCODE -ne 0) {
        throw "Variable-room hook integration test failed with exit code $LASTEXITCODE."
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
    Invoke-CheckedTest '.\build\content_registry_test.exe' 'Content registry tests'

    Write-Host 'Building deterministic content tile interaction tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\content_tiles_test.c' 'content_tiles.c' 'content_registry.c' `
        -o 'build\content_tiles_test.exe' -lbcrypt
    if ($LASTEXITCODE -ne 0) {
        throw "Content tile test compilation failed with exit code $LASTEXITCODE."
    }
    Invoke-CheckedTest '.\build\content_tiles_test.exe' 'Content tile interaction tests'

    Write-Host 'Building generated-map render bridge tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\content_bridge_test.c' 'content_bridge.c' 'content_tiles.c' `
        'content_registry.c' 'custom_maps.c' 'room_graph.c' 'map_ambiance.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' 'log.c' `
        -o 'build\content_bridge_test.exe' -lbcrypt '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "Content bridge test compilation failed with exit code $LASTEXITCODE."
    }
    Invoke-CheckedTest '.\build\content_bridge_test.exe' 'Content bridge tests'

    Write-Host 'Building isolated deterministic map.lua runtime tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\map_script_test.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' `
        -o 'build\map_script_test.exe' '-lluajit-5.1' '-lbcrypt'
    if ($LASTEXITCODE -ne 0) {
        throw "Map script test compilation failed with exit code $LASTEXITCODE."
    }
    Invoke-CheckedTest '.\build\map_script_test.exe' 'Map script tests'

    Write-Host 'Building checked-in spring demo behavior tests...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        'tests\spring_demo_test.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' `
        -o 'build\spring_demo_test.exe' '-lluajit-5.1' '-lbcrypt'
    if ($LASTEXITCODE -ne 0) {
        throw "Spring demo test compilation failed with exit code $LASTEXITCODE."
    }
    Invoke-CheckedTest '.\build\spring_demo_test.exe' 'Spring demo tests'

    Write-Host 'Building the V2 package/fixture regression test...'
    & gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
        '-DCUSTOM_MAPS_TESTING' 'tests\custom_maps_v2_test.c' 'custom_maps.c' 'room_graph.c' 'map_ambiance.c' 'content_registry.c' 'map_script.c' 'entity_world.c' 'entity_lua.c' 'entity_package.c' 'entity_package_json.c' 'mod_json.c' 'log.c' `
        -o 'build\custom_maps_v2_test.exe' '-Wl,--stack,8388608' -lbcrypt '-lluajit-5.1'
    if ($LASTEXITCODE -ne 0) {
        throw "V2 map test compilation failed with exit code $LASTEXITCODE."
    }

    Invoke-CheckedTest '.\build\custom_maps_v2_test.exe' 'V2 map regression test'
    Write-Host 'PASS: V2 collision presets, forces, sensors, temporary visual offsets, native underlay, mirroring, registry identity, demo package, and parser regressions are valid.' -ForegroundColor Green
    Write-Host 'The visual bridge still needs the short in-game check documented in MAP_FORMAT.md.'
} finally {
    Pop-Location -ErrorAction SilentlyContinue
}
