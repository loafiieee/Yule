[CmdletBinding()]
param(
    [switch]$LiveCredential
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$mingwBin = 'C:\msys64\mingw32\bin'
$msysBin = 'C:\msys64\usr\bin'
$oldCredentialOptIn = $env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE

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

function Invoke-NativeTest([string]$Name, [string]$Output, [string[]]$Arguments) {
    Write-Host "Building $Name..."
    & gcc @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name compilation failed with exit code $LASTEXITCODE."
    }
    & $Output
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

try {
    Push-Location $repo
    $env:PATH = "$repo;$mingwBin;$msysBin;$env:PATH"
    if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
        throw '32-bit MinGW GCC was not found. Install MSYS2 MinGW32 or add gcc.exe to PATH.'
    }
    Assert-RequiredRuntimeDlls @('libgcc_s_dw2-1.dll', 'libwinpthread-1.dll', 'lua51.dll')
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null

    if ($LiveCredential) {
        $env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE = '1'
    } else {
        Remove-Item Env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE -ErrorAction SilentlyContinue
    }

    Invoke-NativeTest 'credential lifecycle tests' '.\build\credential_ext_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\credential_ext_test.c', 'credential_ext.c',
        '-o', 'build\credential_ext_test.exe', '-ladvapi32'
    )
    Invoke-NativeTest 'nonblocking TCP transport tests' '.\build\net_ext_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-D_WIN32_WINNT=0x0601', 'tests\net_ext_test.c', 'net_ext.c',
        '-o', 'build\net_ext_test.exe', '-lws2_32', '-liphlpapi'
    )
    Invoke-NativeTest 'online control parser/lifecycle tests' '.\build\online_control_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\online_control_test.c', 'online_control.c',
        '-o', 'build\online_control_test.exe'
    )
    Invoke-NativeTest 'mod API compatibility/capability tests' '.\build\mod_api_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\mod_api_test.c', 'mod_api.c',
        '-o', 'build\mod_api_test.exe'
    )
    Invoke-NativeTest 'removable mod callback list tests' '.\build\mod_callbacks_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\mod_callbacks_test.c', 'mod_callbacks.c',
        '-o', 'build\mod_callbacks_test.exe', '-lluajit-5.1'
    )
    Invoke-NativeTest 'mod.http response/lifecycle tests' '.\build\mod_http_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-D_WIN32_WINNT=0x0601',
        'tests\mod_http_test.c', 'mod_http.c',
        '-o', 'build\mod_http_test.exe', '-lluajit-5.1', '-lwinhttp', '-lws2_32'
    )
    Invoke-NativeTest 'bounded Lua JSON parser/encoder tests' '.\build\mod_json_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\mod_json_test.c', 'mod_json.c',
        '-o', 'build\mod_json_test.exe', '-lluajit-5.1', '-lm'
    )
    Invoke-NativeTest 'bounded text utility tests' '.\build\text_util_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\text_util_test.c', 'text_util.c',
        '-o', 'build\text_util_test.exe'
    )
    Invoke-NativeTest 'RGBA image utility tests' '.\build\image_util_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\image_util_test.c', 'image_util.c',
        '-o', 'build\image_util_test.exe'
    )
    Invoke-NativeTest 'console command catalog tests' '.\build\console_catalog_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\console_catalog_test.c', 'console_catalog.c',
        '-o', 'build\console_catalog_test.exe'
    )
    Invoke-NativeTest 'console parser tests' '.\build\console_parse_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\console_parse_test.c', 'console_parse.c',
        '-o', 'build\console_parse_test.exe', '-lm'
    )
    Invoke-NativeTest 'bounded command history tests' '.\build\command_history_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\command_history_test.c', 'command_history.c', 'text_util.c',
        '-o', 'build\command_history_test.exe'
    )
    Invoke-NativeTest 'safe launch request parser tests' '.\build\launch_request_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\launch_request_test.c', 'launch_request.c', 'online_control.c',
        '-o', 'build\launch_request_test.exe'
    )
    Invoke-NativeTest 'existing-process launch IPC tests' '.\build\launch_ipc_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-DLAUNCH_IPC_TEST', '-static', '-static-libgcc',
        'tests\launch_ipc_test.c', 'launch_ipc.c', 'launch_request.c', 'online_control.c',
        '-o', 'build\launch_ipc_test.exe', '-luser32', '-lshell32'
    )
    Invoke-NativeTest 'window policy tests' '.\build\window_policy_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\window_policy_test.c',
        '-o', 'build\window_policy_test.exe'
    )
    Invoke-NativeTest 'canonical floating-point tick tests' '.\build\fp_control_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\fp_control_test.c', 'fp_control.c',
        '-o', 'build\fp_control_test.exe'
    )
    Invoke-NativeTest 'canonical rollback envelope codec tests' '.\build\rollback_schema_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\rollback_schema_test.c', 'rollback_schema.c',
        '-o', 'build\rollback_schema_test.exe'
    )
    Invoke-NativeTest 'privacy-bounded Discord Rich Presence tests' '.\build\discord_rpc_ext_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-D_WIN32_WINNT=0x0601', '-DDISCORD_RPC_EXT_TEST',
        'tests\discord_rpc_ext_test.c', 'discord_rpc_ext.c',
        '-o', 'build\discord_rpc_ext_test.exe', '-lkernel32'
    )
    Invoke-NativeTest 'bounded bytebeat expression/render tests' '.\build\bytebeat_ext_test.exe' @(
        '-m32', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        'tests\bytebeat_ext_test.c', 'bytebeat_ext.c',
        '-o', 'build\bytebeat_ext_test.exe', '-lm'
    )
    Invoke-NativeTest 'sandboxed Dollchan JavaScript compatibility tests' '.\build\bytebeat_js_test.exe' @(
        '-m32', '-O2', '-std=c11', '-Wall', '-Wextra', '-pedantic',
        'tests\bytebeat_js_test.c', 'bytebeat_js.c', 'bytebeat_chakra.c',
        'bytebeat_ext.c',
        'third_party\quickjs-ng\quickjs-amalgam.c',
        '-w', '-o', 'build\bytebeat_js_test.exe', '-lkernel32', '-lm'
    )
    Invoke-NativeTest 'ahead-of-time Dollchan PCM stream tests' '.\build\bytebeat_stream_test.exe' @(
        '-m32', '-O2', '-std=c11', '-Wall', '-Wextra', '-pedantic',
        'tests\bytebeat_stream_test.c', 'bytebeat_stream.c',
        'bytebeat_js.c', 'bytebeat_chakra.c',
        'third_party\quickjs-ng\quickjs-amalgam.c',
        '-w', '-o', 'build\bytebeat_stream_test.exe',
        '-lkernel32', '-lm'
    )
    Invoke-NativeTest 'native rollback serializer tests' '.\build\state_serializer_test.exe' @(
        '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-pedantic',
        '-ffunction-sections', '-fdata-sections',
        '-DEGGNOGGPLUS_SERIALIZER_TESTING',
        'tests\state_serializer_test.c',
        'dllmain.c', 'stubs.c', 'hooks.c', 'image_util.c', 'text_util.c',
        'console_catalog.c', 'console_parse.c', 'command_history.c',
        'custom_maps.c',
        'content_registry.c', 'content_tiles.c', 'content_bridge.c',
        'map_script.c', 'cursor_ext.c', 'credential_ext.c', 'discord_rpc_ext.c',
        'bytebeat_ext.c', 'bytebeat_chakra.c', 'bytebeat_js.c',
        'bytebeat_stream.c',
        'third_party\quickjs-ng\quickjs-amalgam.c',
        '-w',
        'online_control.c', 'launch_request.c', 'launch_ipc.c',
        'lua_manager.c', 'mod_api.c', 'mod_callbacks.c', 'mod_fs.c', 'mod_http.c', 'mod_json.c',
        'ggpo_ext.c',
        'ggpo_loopback.c', 'ggpo_local.c', 'ggpo_net.c',
        'fp_control.c', 'rollback_schema.c',
        'font_ext.c', 'texture_ext.c', 'log.c', 'net_ext.c', 'update_ext.c',
        '-o', 'build\state_serializer_test.exe',
        '-Wl,--gc-sections',
        '-lkernel32', '-luser32', '-ladvapi32', '-lopengl32',
        '-lluajit-5.1', '-lws2_32', '-liphlpapi', '-lwinhttp', '-lbcrypt',
        '-lcomdlg32', '-lshell32', '-lole32', '-lm'
    )

    & node --test 'tests\online_admin_server_test.js' 'tests\discord_lfg_bot_test.js' 'tests\lfg_redirect_test.js'
    if ($LASTEXITCODE -ne 0) {
        throw "Discord LFG bot/redirect tests failed with exit code $LASTEXITCODE."
    }

    foreach ($script in @(
        'tests\compile_sources_static_test.py',
        'tests\release_packaging_static_test.py',
        'tests\public_source_export_static_test.py',
        'tests\credential_hooks_static_test.py',
        'tests\online_control_hooks_static_test.py',
        'tests\online_flow_integration_static_test.py',
        'tests\online_cursor_static_test.py',
        'tests\online_match_hud_static_test.py',
        'tests\online_menu_tick_static_test.py',
        'tests\online_troubleshooter_static_test.py',
        'tests\online_admin_integration_static_test.py',
        'tests\friend_challenge_map_picker_static_test.py',
        'tests\social_controls_static_test.py',
        'tests\private_rematch_static_test.py',
        'tests\online_launch_static_test.py',
        'tests\updater_handoff_static_test.py',
        'tests\frame_boundary_capture_static_test.py',
        'tests\desync_repro_static_test.py',
        'tests\peer_trace_diff_test.py',
        'tests\fp_control_hooks_static_test.py',
        'tests\simulation_width_static_test.py',
        'tests\player_colour_static_test.py',
        'tests\discord_presence_static_test.py',
        'tests\bytebeat_lua_static_test.py',
        'tests\lua_http_lifecycle_static_test.py',
        'tests\greggnogg_ui_static_test.py',
        'tests\lua_subscription_static_test.py',
        'tests\lua_online_api_static_test.py',
        'tests\lua_fs_picker_static_test.py',
        'tests\mod_api_integration_static_test.py',
        'tests\mod_json_integration_static_test.py',
        'tests\bytebeat_js_static_test.py',
        'tests\music_console_static_test.py',
        'tests\console_catalog_static_test.py',
        'tests\discord_lfg_server_static_test.py',
        'tests\server_updater_static_test.py',
        'tests\repository_updater_static_test.py',
        'tests\installer_lifecycle_test.py',
        'tests\net_backpressure_static_test.py',
        'tests\window_runtime_static_test.py'
    )) {
        & python $script
        if ($LASTEXITCODE -ne 0) {
            throw "$script failed with exit code $LASTEXITCODE."
        }
    }

    Write-Host 'PASS: guarded credential, TCP/control, FP, rollback envelope, serializer, and window native/static tests completed.' -ForegroundColor Green
} finally {
    if ($null -eq $oldCredentialOptIn) {
        Remove-Item Env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE -ErrorAction SilentlyContinue
    } else {
        $env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE = $oldCredentialOptIn
    }
    Pop-Location -ErrorAction SilentlyContinue
}
