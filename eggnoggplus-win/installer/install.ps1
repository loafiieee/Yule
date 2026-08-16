# EGGNOGG+ Framework (Yule) installer
# Windows PowerShell 5.1 compatible. Double-click INSTALL.bat or UNINSTALL.bat.
#
# What it does (each step asks Y/n):
#   1. finds your EGGNOGG+ copy (next to this script, via a previous install, or asks
#      you to select eggnoggplus.exe)
#   2. moves it to %LOCALAPPDATA%\EGGNOGG+ (safe from Downloads cleanups)
#   3. installs the one-shot updater and framework DLLs from the release channel
#      (renames the vanilla SDL2.dll to SDL2_real.dll on first install)
#   4. Start Menu shortcut (makes it show up in Windows search)
#   5. registers safe yule:// links for the installed executable
#   6. adds it to Steam as a non-Steam game with artwork
#   7. turns the framework log console off by default
#   8. writes the Yule manifest so future updates and safe uninstall are automatic
param(
    [string]$ChannelUrl = 'https://loafiieee.com/yule/releases/latest.json',
    # internal / testing switches
    [string]$OriginalRoot = '',
    [switch]$NoReexec,
    [string]$GamePath = '',
    [string]$InstallDir = '',
    [string]$ManifestDir = '',
    [string]$ProtocolRegistryRoot = '',
    [switch]$SkipSteam,
    [switch]$SkipShortcut,
    [switch]$SkipProtocol,
    [switch]$Uninstall,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$AppName = 'EGGNOGG+'
$ExeName = 'eggnoggplus.exe'
$UpdaterName = 'YuleUpdater.exe'
$LegacyLauncherName = 'YuleLauncher.exe'
if (-not $InstallDir)  { $InstallDir  = Join-Path $env:LOCALAPPDATA 'EGGNOGG+' }
if (-not $ManifestDir) { $ManifestDir = Join-Path $env:LOCALAPPDATA 'Yule' }
if (-not $ProtocolRegistryRoot) { $ProtocolRegistryRoot = 'HKCU:\Software\Classes\yule' }
$ManifestPath = Join-Path $ManifestDir 'install.json'

# ==ARTWORK-BEGIN== (base64 blobs injected by tools/build_release.ps1; '' = skip slot)
$Artwork = @{ grid = ''; gridp = ''; hero = ''; logo = ''; logopos = ''; icon = '' }
# ==ARTWORK-END==

# ---------------------------------------------------------------- helpers ---

function Say([string]$msg, [string]$color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Head([string]$msg) { Write-Host ''; Write-Host "== $msg" -ForegroundColor Cyan }

function Ask-YN([string]$question) {
    if ($Yes) { Say "$question [Y/n] -> y (auto)"; return $true }
    while ($true) {
        $r = Read-Host "$question [Y/n]"
        if ($r -eq '' -or $r -match '^[yY]') { return $true }
        if ($r -match '^[nN]') { return $false }
    }
}

function Get-YuleProtocolCommand([string]$gameDir) {
    $exe = Join-Path $gameDir $ExeName
    return ('"{0}" "--yule-uri=%1"' -f $exe)
}

function Get-RegistryDefault([string]$path) {
    try {
        return [string](Get-Item -LiteralPath $path -ErrorAction Stop).GetValue('')
    } catch {
        return ''
    }
}

function Test-OwnedYuleProtocol([string]$expectedCommand) {
    $root = $ProtocolRegistryRoot
    $shell = Join-Path $root 'shell'
    $open = Join-Path $shell 'open'
    $command = Join-Path $open 'command'
    if (-not $expectedCommand -or
        -not (Test-Path -LiteralPath $command) -or
        (Get-RegistryDefault $command) -cne $expectedCommand) {
        return $false
    }
    try {
        $rootKey = Get-Item -LiteralPath $root -ErrorAction Stop
        $shellKey = Get-Item -LiteralPath $shell -ErrorAction Stop
        $openKey = Get-Item -LiteralPath $open -ErrorAction Stop
        $commandKey = Get-Item -LiteralPath $command -ErrorAction Stop
        $rootValues = @($rootKey.GetValueNames() | Sort-Object)
        $rootChildren = @($rootKey.GetSubKeyNames())
        $shellChildren = @($shellKey.GetSubKeyNames())
        $openChildren = @($openKey.GetSubKeyNames())
        $commandValues = @($commandKey.GetValueNames())
        if ($rootChildren.Count -ne 1 -or $rootChildren[0] -cne 'shell' -or
            $shellChildren.Count -ne 1 -or $shellChildren[0] -cne 'open' -or
            $openChildren.Count -ne 1 -or $openChildren[0] -cne 'command' -or
            @($commandKey.GetSubKeyNames()).Count -ne 0 -or
            @($shellKey.GetValueNames()).Count -ne 0 -or
            @($openKey.GetValueNames()).Count -ne 0 -or
            $commandValues.Count -ne 1 -or $commandValues[0] -cne '' -or
            $rootValues.Count -ne 2 -or
            $rootValues[0] -cne '' -or
            $rootValues[1] -cne 'URL Protocol') {
            return $false
        }
        return $true
    } catch {
        return $false
    }
}

function Remove-OwnedYuleProtocol($installedManifest) {
    $root = $ProtocolRegistryRoot
    if (-not $installedManifest -or
        -not $installedManifest.deep_link_protocol -or
        -not $installedManifest.deep_link_protocol.applied) {
        Say 'No installer-managed yule:// handler recorded.'
        return
    }
    $expected = [string]$installedManifest.deep_link_protocol.command
    if (Test-OwnedYuleProtocol $expected) {
        Remove-Item -LiteralPath $root -Recurse -Force
        Say 'yule:// link handler removed.' 'Green'
    } else {
        Say 'Preserved modified or externally owned yule:// link handler.' 'Yellow'
    }
}

function Resolve-GameDirectory([string]$path) {
    if (-not $path) { return '' }
    try {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $file = Get-Item -LiteralPath $path
            if ($file.Name -ieq $ExeName) {
                return $file.Directory.FullName
            }
            return ''
        }
        if (Test-Path -LiteralPath $path -PathType Container) {
            $directory = (Resolve-Path -LiteralPath $path).Path
            if (Test-Path -LiteralPath (Join-Path $directory $ExeName) -PathType Leaf) {
                return $directory
            }
        }
    } catch {}
    return ''
}

function Pick-GameExecutable([string]$description) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = $description
        $dlg.Filter = "EGGNOGG+ ($ExeName)|$ExeName|Programs (*.exe)|*.exe"
        $dlg.CheckFileExists = $true
        $dlg.CheckPathExists = $true
        $dlg.Multiselect = $false
        $dlg.FileName = $ExeName
        $result = $dlg.ShowDialog((New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }))
        if ($result -eq [System.Windows.Forms.DialogResult]::OK) { return $dlg.FileName }
        return ''
    } catch {
        return Read-Host $description
    }
}

function Resolve-SafeManagedPath([string]$root, [string]$relative) {
    if (-not $root -or -not $relative -or
        [IO.Path]::IsPathRooted($relative) -or
        $relative.IndexOf([char]0) -ge 0) {
        return ''
    }
    $normalized = $relative.Replace('/', '\')
    foreach ($part in $normalized.Split('\')) {
        if (-not $part -or $part -eq '.' -or $part -eq '..' -or
            $part.Contains(':')) {
            return ''
        }
    }
    try {
        $rootFull = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
        $full = [IO.Path]::GetFullPath((Join-Path $rootFull $normalized))
        if (-not $full.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
            return ''
        }
        return $full
    } catch {
        return ''
    }
}

function Get-Sha256([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.File]::OpenRead($path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash($stream)
        return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Ensure-Dir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}

function Download-Verified([string]$url, [string]$dest, [string]$sha256, [long]$size = -1) {
    $tmp = "$dest.yule-tmp"
    foreach ($try in 1..2) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
            if ($size -ge 0 -and (Get-Item -LiteralPath $tmp).Length -ne $size) {
                throw "size mismatch (expected $size bytes)"
            }
            $got = Get-Sha256 $tmp
            if ($got -eq $sha256.ToLowerInvariant()) {
                Move-Item -LiteralPath $tmp -Destination $dest -Force
                return $true
            }
            Say "  hash mismatch on $url (try $try)" 'Yellow'
        } catch {
            Say "  download failed: $($_.Exception.Message) (try $try)" 'Yellow'
        }
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
    return $false
}

function Install-VerifiedLocalFile([string]$source, [string]$dest) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "required installer payload is missing: $source"
    }
    $sourceItem = Get-Item -LiteralPath $source -Force
    if ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "required installer payload is a reparse point: $source"
    }
    $sourceFull = [IO.Path]::GetFullPath($source)
    $destFull = [IO.Path]::GetFullPath($dest)
    $expectedHash = Get-Sha256 $sourceFull
    $expectedSize = $sourceItem.Length
    if ($sourceFull -ieq $destFull) {
        return [ordered]@{
            hash = $expectedHash
            size = $expectedSize
            changed = $false
        }
    }
    Ensure-Dir (Split-Path -Parent $destFull)
    $tmp = "$destFull.yule-tmp"
    try {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        Copy-Item -LiteralPath $sourceFull -Destination $tmp -Force
        $tmpItem = Get-Item -LiteralPath $tmp -Force
        if (($tmpItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $tmpItem.Length -ne $expectedSize -or
            (Get-Sha256 $tmp) -ne $expectedHash) {
            throw 'copied helper did not match its bundled source'
        }
        Move-Item -LiteralPath $tmp -Destination $destFull -Force
        if ((Get-Sha256 $destFull) -ne $expectedHash) {
            throw 'installed helper failed post-replacement verification'
        }
        return [ordered]@{
            hash = $expectedHash
            size = $expectedSize
            changed = $true
        }
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
}

function Test-IsProxyDll([string]$path) {
    # our framework proxy contains the ASCII marker "modframework"; the real SDL2 does not
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    $bytes = [IO.File]::ReadAllBytes($path)
    $marker = [Text.Encoding]::ASCII.GetBytes('modframework')
    for ($i = 0; $i -le $bytes.Length - $marker.Length; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $marker.Length; $j++) {
            if ($bytes[$i + $j] -ne $marker[$j]) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

function Test-GameRunning([string[]]$TargetExecutables) {
    $targets = @{}
    foreach ($target in $TargetExecutables) {
        if (-not $target) { continue }
        try {
            $targets[[IO.Path]::GetFullPath($target)] = $true
        } catch {
            # A malformed target is already rejected by the surrounding install
            # preflight; do not broaden this process check to unrelated copies.
        }
    }
    foreach ($process in @(Get-Process -Name 'eggnoggplus' -ErrorAction SilentlyContinue)) {
        try {
            if ($process.Path -and $targets.ContainsKey(
                    [IO.Path]::GetFullPath($process.Path))) {
                return $true
            }
        } catch {
            # If Windows hides the path, conservatively assume that this process
            # could own the selected file.
            return $true
        }
    }
    return $false
}

# Wraps the embedded icon PNG into a real .ico (single 256x256 PNG-compressed
# entry, supported since Vista) so Start Menu shortcuts get proper art - .lnk
# icons cannot point at a bare .png.
function New-IcoFromPngBase64([string]$b64, [string]$dest) {
    Add-Type -AssemblyName System.Drawing
    $srcBytes = [Convert]::FromBase64String($b64)
    $srcMs = New-Object IO.MemoryStream (, $srcBytes)
    $src = [System.Drawing.Image]::FromStream($srcMs)
    $bmp = New-Object System.Drawing.Bitmap 256, 256
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.DrawImage($src, 0, 0, 256, 256)
    $g.Dispose()
    $pngMs = New-Object IO.MemoryStream
    $bmp.Save($pngMs, [System.Drawing.Imaging.ImageFormat]::Png)
    $png = $pngMs.ToArray()
    $bmp.Dispose(); $src.Dispose()
    $out = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter ($out)
    $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]1)   # ICONDIR: reserved, type=icon, count=1
    $bw.Write([byte]0); $bw.Write([byte]0)                             # 256x256 encoded as 0,0
    $bw.Write([byte]0); $bw.Write([byte]0)                             # palette, reserved
    $bw.Write([uint16]1); $bw.Write([uint16]32)                        # planes, bpp
    $bw.Write([int]$png.Length); $bw.Write([int]22)                    # data size, offset
    $bw.Write($png)
    $bw.Flush()
    [IO.File]::WriteAllBytes($dest, $out.ToArray())
    $bw.Dispose()
}

# ------------------------------------------------------------------ crc32 ---

$script:CrcTable = $null
function Get-Crc32([byte[]]$bytes) {
    if (-not $script:CrcTable) {
        $script:CrcTable = New-Object 'uint32[]' 256
        for ($n = 0; $n -lt 256; $n++) {
            $c = [uint32]$n
            for ($k = 0; $k -lt 8; $k++) {
                if ($c -band 1) { $c = 0xEDB88320 -bxor ($c -shr 1) } else { $c = $c -shr 1 }
            }
            $script:CrcTable[$n] = $c
        }
    }
    $crc = [uint32]::MaxValue
    foreach ($b in $bytes) {
        $crc = $script:CrcTable[($crc -bxor $b) -band 0xFF] -bxor ($crc -shr 8)
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Get-ShortcutAppId([string]$exeQuoted, [string]$appName) {
    $crc = Get-Crc32 ([Text.Encoding]::UTF8.GetBytes($exeQuoted + $appName))
    return [uint32]($crc -bor 0x80000000)
}

# --------------------------------------------------------------- vdf i/o ----
# Binary KeyValues: 0x00 map(name\0 ... 0x08), 0x01 string(name\0 value\0),
# 0x02 int32(name\0 4 bytes LE). File = root-map contents.

function Read-VdfString([byte[]]$b, [ref]$i) {
    $start = $i.Value
    while ($b[$i.Value] -ne 0) { $i.Value++ }
    $s = [Text.Encoding]::UTF8.GetString($b, $start, $i.Value - $start)
    $i.Value++
    return $s
}

function Read-VdfMap([byte[]]$b, [ref]$i) {
    $map = New-Object System.Collections.Specialized.OrderedDictionary
    while ($i.Value -lt $b.Length) {
        $type = $b[$i.Value]; $i.Value++
        if ($type -eq 8) { break }
        $name = Read-VdfString $b $i
        switch ($type) {
            0 { $map[$name] = Read-VdfMap $b $i }
            1 { $map[$name] = Read-VdfString $b $i }
            2 { $map[$name] = [BitConverter]::ToUInt32($b, $i.Value); $i.Value += 4 }
            default { throw "vdf: unknown field type $type at offset $($i.Value)" }
        }
    }
    return $map
}

function Write-VdfMap($map, [IO.MemoryStream]$ms) {
    foreach ($key in $map.Keys) {
        $val = $map[$key]
        $nameBytes = [Text.Encoding]::UTF8.GetBytes([string]$key)
        if ($val -is [System.Collections.Specialized.OrderedDictionary]) {
            $ms.WriteByte(0); $ms.Write($nameBytes, 0, $nameBytes.Length); $ms.WriteByte(0)
            Write-VdfMap $val $ms
        } elseif ($val -is [string]) {
            $vb = [Text.Encoding]::UTF8.GetBytes($val)
            $ms.WriteByte(1); $ms.Write($nameBytes, 0, $nameBytes.Length); $ms.WriteByte(0)
            $ms.Write($vb, 0, $vb.Length); $ms.WriteByte(0)
        } else {
            $ib = [BitConverter]::GetBytes([uint32]$val)
            $ms.WriteByte(2); $ms.Write($nameBytes, 0, $nameBytes.Length); $ms.WriteByte(0)
            $ms.Write($ib, 0, 4)
        }
    }
    $ms.WriteByte(8)
}

function Read-ShortcutsVdf([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        $root = New-Object System.Collections.Specialized.OrderedDictionary
        $root['shortcuts'] = New-Object System.Collections.Specialized.OrderedDictionary
        return $root
    }
    $b = [IO.File]::ReadAllBytes($path)
    $i = [ref]0
    $root = New-Object System.Collections.Specialized.OrderedDictionary
    while ($i.Value -lt $b.Length) {
        $type = $b[$i.Value]; $i.Value++
        if ($type -eq 8) { break }
        $name = Read-VdfString $b $i
        if ($type -ne 0) { throw 'vdf: unexpected root field' }
        $root[$name] = Read-VdfMap $b $i
    }
    if (-not $root.Contains('shortcuts')) {
        $root['shortcuts'] = New-Object System.Collections.Specialized.OrderedDictionary
    }
    return $root
}

function Write-ShortcutsVdf([string]$path, $root) {
    $ms = New-Object IO.MemoryStream
    foreach ($key in $root.Keys) {
        $nameBytes = [Text.Encoding]::UTF8.GetBytes([string]$key)
        $ms.WriteByte(0); $ms.Write($nameBytes, 0, $nameBytes.Length); $ms.WriteByte(0)
        Write-VdfMap $root[$key] $ms
    }
    $ms.WriteByte(8)
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

function New-ShortcutEntry([uint32]$appid, [string]$exeQuoted, [string]$startDir, [string]$icon) {
    $e = New-Object System.Collections.Specialized.OrderedDictionary
    $e['appid'] = $appid
    $e['AppName'] = 'Eggnogg+'
    $e['Exe'] = $exeQuoted
    $e['StartDir'] = $startDir
    $e['icon'] = $icon
    $e['ShortcutPath'] = ''
    $e['LaunchOptions'] = ''
    $e['IsHidden'] = [uint32]0
    $e['AllowDesktopConfig'] = [uint32]1
    $e['AllowOverlay'] = [uint32]1
    $e['OpenVR'] = [uint32]0
    $e['Devkit'] = [uint32]0
    $e['DevkitGameID'] = ''
    $e['DevkitOverrideAppID'] = [uint32]0
    $e['LastPlayTime'] = [uint32]0
    $e['FlatpakAppID'] = ''
    $e['tags'] = New-Object System.Collections.Specialized.OrderedDictionary
    return $e
}

function Remove-InstalledSteamShortcut($installedManifest, [string]$gameDir) {
    if ($SkipSteam) {
        Say 'Steam cleanup skipped by switch.' 'Yellow'
        return
    }
    if (-not $installedManifest -or -not $installedManifest.steam -or
        -not $installedManifest.steam.applied) {
        Say 'No installer-managed Steam shortcut recorded.'
        return
    }
    $steamPath = ''
    try { $steamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath } catch {}
    if (-not $steamPath -or -not (Test-Path -LiteralPath $steamPath -PathType Container)) {
        Say 'Steam is not installed; recorded shortcut cleanup skipped.' 'Yellow'
        return
    }

    $steamWasRunning = $false
    if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) {
        if (-not (Ask-YN 'Steam must close before its EGGNOGG+ shortcut is removed. Close it now?')) {
            Say 'Steam shortcut cleanup skipped while Steam is running.' 'Yellow'
            return
        }
        $steamWasRunning = $true
        Start-Process -FilePath (Join-Path $steamPath 'steam.exe') -ArgumentList '-shutdown'
        $waited = 0
        while ((Get-Process -Name 'steam' -ErrorAction SilentlyContinue) -and $waited -lt 30) {
            Start-Sleep -Seconds 1
            $waited++
        }
        if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) {
            if (Ask-YN "Steam didn't close gracefully. Force-close it?") {
                Stop-Process -Name 'steam' -Force
                Start-Sleep -Seconds 2
            } else {
                Say 'Steam shortcut cleanup skipped.' 'Yellow'
                return
            }
        }
    }

    $expectedExeName = if ($installedManifest.launcher_executable) {
        $LegacyLauncherName
    } else {
        # Backward-compatible cleanup for installer-v2 receipts created before
        # the stable launcher existed.
        $ExeName
    }
    $expectedExe = [IO.Path]::GetFullPath(
        (Join-Path $gameDir $expectedExeName)
    )
    $recordedAppId = [uint32]0
    try { $recordedAppId = [uint32]$installedManifest.steam.appid } catch {}
    foreach ($accountName in @($installedManifest.steam.accounts)) {
        if ([string]$accountName -notmatch '^\d+$') { continue }
        $cfgDir = Join-Path (Join-Path (Join-Path $steamPath 'userdata') ([string]$accountName)) 'config'
        $vdfPath = Join-Path $cfgDir 'shortcuts.vdf'
        if (-not (Test-Path -LiteralPath $vdfPath -PathType Leaf)) { continue }
        try {
            Copy-Item -LiteralPath $vdfPath -Destination ("$vdfPath.bak-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) -Force
            $root = Read-ShortcutsVdf $vdfPath
            $shorts = $root['shortcuts']
            $removed = $false
            foreach ($key in @($shorts.Keys)) {
                $entry = $shorts[$key]
                $entryName = ''
                $entryExe = ''
                $entryAppId = [uint32]0
                foreach ($fieldName in @('AppName', 'appname')) {
                    if ($entry.Contains($fieldName)) { $entryName = [string]$entry[$fieldName]; break }
                }
                foreach ($fieldName in @('Exe', 'exe')) {
                    if ($entry.Contains($fieldName)) { $entryExe = ([string]$entry[$fieldName]).Trim('"'); break }
                }
                foreach ($fieldName in @('appid', 'AppId')) {
                    if ($entry.Contains($fieldName)) {
                        try { $entryAppId = [uint32]$entry[$fieldName] } catch {}
                        break
                    }
                }
                $sameExe = $false
                try {
                    $sameExe = [IO.Path]::GetFullPath($entryExe) -ieq $expectedExe
                } catch {}
                if ($sameExe -and
                    (($recordedAppId -ne 0 -and $entryAppId -eq $recordedAppId) -or
                     $entryName -match '^(?i:eggnogg\+?)$')) {
                    $shorts.Remove($key)
                    $removed = $true
                }
            }
            if ($removed) {
                Write-ShortcutsVdf $vdfPath $root
                Say "  account $accountName`: shortcut removed" 'Green'
            }
            if ($recordedAppId -ne 0) {
                $gridDir = Join-Path $cfgDir 'grid'
                foreach ($name in @(
                    "$recordedAppId.png",
                    ("$recordedAppId" + 'p.png'),
                    ("$recordedAppId" + '_hero.png'),
                    ("$recordedAppId" + '_logo.png'),
                    ("$recordedAppId" + '_icon.png'),
                    ("$recordedAppId" + '.json')
                )) {
                    Remove-Item -LiteralPath (Join-Path $gridDir $name) -Force -ErrorAction SilentlyContinue
                }
            }
        } catch {
            Say "  account $accountName`: cleanup failed ($($_.Exception.Message)); restoring backup" 'Red'
            $backup = Get-ChildItem "$vdfPath.bak-*" -ErrorAction SilentlyContinue |
                Sort-Object Name | Select-Object -Last 1
            if ($backup) { Copy-Item -LiteralPath $backup.FullName -Destination $vdfPath -Force }
        }
    }
    if ($steamWasRunning) {
        Say 'Restarting Steam...'
        Start-Process -FilePath (Join-Path $steamPath 'steam.exe')
    }
}

# ------------------------------------------------------------ re-exec -------

if (-not $NoReexec) {
    $selfDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $tempCopy = Join-Path $env:TEMP ("yule_install_" + [Guid]::NewGuid().ToString('N') + ".ps1")
    Copy-Item -LiteralPath $MyInvocation.MyCommand.Path -Destination $tempCopy -Force
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$tempCopy`"",
                 '-NoReexec', '-OriginalRoot', "`"$selfDir`"", '-ChannelUrl', "`"$ChannelUrl`"")
    if ($GamePath)    { $argList += @('-GamePath', "`"$GamePath`"") }
    if ($InstallDir -ne (Join-Path $env:LOCALAPPDATA 'EGGNOGG+')) { $argList += @('-InstallDir', "`"$InstallDir`"") }
    if ($ManifestDir -ne (Join-Path $env:LOCALAPPDATA 'Yule')) { $argList += @('-ManifestDir', "`"$ManifestDir`"") }
    if ($ProtocolRegistryRoot -ne 'HKCU:\Software\Classes\yule') { $argList += @('-ProtocolRegistryRoot', "`"$ProtocolRegistryRoot`"") }
    if ($SkipSteam)   { $argList += '-SkipSteam' }
    if ($SkipShortcut){ $argList += '-SkipShortcut' }
    if ($SkipProtocol){ $argList += '-SkipProtocol' }
    if ($Uninstall)   { $argList += '-Uninstall' }
    if ($Yes)         { $argList += '-Yes' }
    Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Wait -NoNewWindow
    exit $LASTEXITCODE
}
if (-not $OriginalRoot) { $OriginalRoot = Split-Path -Parent $MyInvocation.MyCommand.Path }

$steps = New-Object System.Collections.Specialized.OrderedDictionary
$manifest = $null
if (Test-Path -LiteralPath $ManifestPath) {
    try { $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json } catch { $manifest = $null }
}

Say ''
Say ('  EGGNOGG+ framework (Yule) ' + $(if ($Uninstall) { 'uninstaller' } else { 'installer' })) 'White'
Say '  ------------------------------------' 'DarkGray'

# ------------------------------------------------------- 1: locate game -----

Head 'Locating your EGGNOGG+ folder'
$gameDir = Resolve-GameDirectory $GamePath
if ($gameDir) {
    Say 'Using the explicitly selected game executable.' 'Green'
} elseif (Test-Path (Join-Path $OriginalRoot $ExeName)) {
    $gameDir = $OriginalRoot
} elseif (Test-Path (Join-Path $OriginalRoot "EGGNOGG+\$ExeName")) {
    $gameDir = Join-Path $OriginalRoot 'EGGNOGG+'
} elseif ($manifest -and $manifest.install_dir -and (Test-Path (Join-Path $manifest.install_dir $ExeName))) {
    $gameDir = $manifest.install_dir
    Say "Found your previous install." 'Green'
} else {
    Say "I couldn't find $ExeName next to this script or from a previous install."
    Say "Select $ExeName itself in the file dialog..."
    $typed = Pick-GameExecutable "Select $ExeName"
    $selectedDir = Resolve-GameDirectory $typed
    if ($selectedDir) {
        $gameDir = $selectedDir
    } elseif ($typed) {
        Say "`"$typed`" is not $ExeName." 'Yellow'
    }
}
if (-not $gameDir) {
    Say ''
    Say "No EGGNOGG+ copy found. Get the game first, put this installer next to its folder, and run it again." 'Yellow'
    Read-Host 'Press Enter to exit' | Out-Null
    exit 1
}
Say "Game folder: $gameDir" 'Green'

$protectedGameExecutables = @(
    (Join-Path $gameDir $ExeName),
    (Join-Path $InstallDir $ExeName)
)
while (Test-GameRunning $protectedGameExecutables) {
    Say 'EGGNOGG+ is currently running - please close it.' 'Yellow'
    if (-not (Ask-YN 'Check again?')) { Say 'Aborted.'; exit 1 }
}

if ($Uninstall) {
    Head 'Removing the Yule framework'
    $sdl = Join-Path $gameDir 'SDL2.dll'
    $sdlReal = Join-Path $gameDir 'SDL2_real.dll'
    $proxyInstalled = (Test-Path -LiteralPath $sdl -PathType Leaf) -and
        (Test-IsProxyDll $sdl)
    if ($proxyInstalled -and -not (Test-Path -LiteralPath $sdlReal -PathType Leaf)) {
        Say 'Cannot uninstall safely: SDL2.dll is the framework proxy, but SDL2_real.dll is missing.' 'Red'
        Say 'Restore a vanilla SDL2_real.dll from a clean game copy, then run UNINSTALL.bat again.' 'Red'
        if (-not $Yes) { Read-Host 'Press Enter to close' | Out-Null }
        exit 1
    }
    if (-not (Ask-YN "Restore the vanilla game in `"$gameDir`" and remove installer-managed files?")) {
        Say 'Uninstall cancelled.'
        exit 0
    }

    Remove-InstalledSteamShortcut $manifest $gameDir
    Remove-OwnedYuleProtocol $manifest

    $defaultShortcut = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Microsoft\Windows\Start Menu\Programs\EGGNOGG+.lnk'
    if ($manifest -and $manifest.start_menu_shortcut -and
        ([string]$manifest.start_menu_shortcut -ieq $defaultShortcut)) {
        Remove-Item -LiteralPath $defaultShortcut -Force -ErrorAction SilentlyContinue
        Say 'Start Menu shortcut removed.' 'Green'
    }

    $removedManaged = 0
    $preservedManaged = 0
    foreach ($entry in @($(if ($manifest) { $manifest.managed_files } else { @() }))) {
        $relative = [string]$entry.path
        if (-not $relative -or $relative -ieq 'SDL2.dll') { continue }
        $managedPath = Resolve-SafeManagedPath $gameDir $relative
        $recordedHash = ([string]$entry.sha256).ToLowerInvariant()
        if (-not $managedPath -or $recordedHash -notmatch '^[0-9a-f]{64}$') {
            $preservedManaged++
            continue
        }
        if (-not (Test-Path -LiteralPath $managedPath -PathType Leaf)) { continue }
        if ((Get-Sha256 $managedPath) -eq $recordedHash) {
            Remove-Item -LiteralPath $managedPath -Force
            $removedManaged++
        } else {
            Say "Preserved modified file: $relative" 'Yellow'
            $preservedManaged++
        }
    }

    if ($proxyInstalled) {
        Remove-Item -LiteralPath $sdl -Force
        Move-Item -LiteralPath $sdlReal -Destination $sdl
        Say 'Vanilla SDL2.dll restored.' 'Green'
    } elseif (-not (Test-Path -LiteralPath $sdl -PathType Leaf) -and
            (Test-Path -LiteralPath $sdlReal -PathType Leaf)) {
        Move-Item -LiteralPath $sdlReal -Destination $sdl
        Say 'Missing SDL2.dll restored from SDL2_real.dll.' 'Green'
    } else {
        Say 'SDL2.dll was already vanilla; left it unchanged.' 'Green'
    }

    $localManifest = Join-Path $gameDir 'install.json'
    Remove-Item -LiteralPath $localManifest -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $ManifestPath -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $ManifestDir -PathType Container) {
        $remaining = Get-ChildItem -LiteralPath $ManifestDir -Force -ErrorAction SilentlyContinue
        if (-not $remaining) {
            Remove-Item -LiteralPath $ManifestDir -Force -ErrorAction SilentlyContinue
        }
    }

    Say ''
    Say '  Yule framework removed; the game and all user mods/configuration remain.' 'White'
    Say "  Managed files removed: $removedManaged" 'DarkGray'
    Say "  Modified/untracked files preserved: $preservedManaged" 'DarkGray'
    if (-not $Yes) { Read-Host 'Press Enter to close' | Out-Null }
    exit 0
}

# ------------------------------------------------------- 2: move ------------

Head "Move to a safe location ($InstallDir)"
$movedByInstaller = $false
if ((Resolve-Path $gameDir).Path.TrimEnd('\') -ieq $InstallDir.TrimEnd('\')) {
    Say 'Already there - nothing to move.' 'Green'
    $steps['move'] = 'already-there'
} elseif (Test-Path (Join-Path $InstallDir $ExeName)) {
    Say "An install already exists at $InstallDir - using it (maintenance mode)." 'Green'
    Say "(Your copy at $gameDir is untouched.)"
    $gameDir = $InstallDir
    $steps['move'] = 'existing-used'
} elseif (Ask-YN "Move the game from `"$gameDir`" to `"$InstallDir`"?") {
    $srcRoot = [IO.Path]::GetPathRoot($gameDir)
    $dstRoot = [IO.Path]::GetPathRoot($InstallDir)
    Ensure-Dir (Split-Path -Parent $InstallDir)
    if ($srcRoot -ieq $dstRoot) {
        Move-Item -LiteralPath $gameDir -Destination $InstallDir
    } else {
        Copy-Item -LiteralPath $gameDir -Destination $InstallDir -Recurse
        $srcCount = (Get-ChildItem -LiteralPath $gameDir -Recurse -File | Measure-Object -Sum Length)
        $dstCount = (Get-ChildItem -LiteralPath $InstallDir -Recurse -File | Measure-Object -Sum Length)
        if ($srcCount.Count -eq $dstCount.Count -and $srcCount.Sum -eq $dstCount.Sum) {
            try {
                Remove-Item -LiteralPath $gameDir -Recurse -Force
            } catch {
                Set-Content -Path (Join-Path $gameDir 'MOVED - SAFE TO DELETE.txt') -Value "This copy was moved to $InstallDir by the Yule installer. You can delete this folder."
                Say 'Old folder is locked by something; left a MOVED - SAFE TO DELETE.txt marker in it.' 'Yellow'
            }
        } else {
            throw 'copy verification failed - original left untouched'
        }
    }
    $gameDir = $InstallDir
    $movedByInstaller = $true
    $steps['move'] = 'ok'
    Say "Moved. Game now lives at $gameDir" 'Green'
} else {
    $steps['move'] = 'declined'
    Say "OK - staying at $gameDir"
}

# ------------------------------------------ 3: framework install/update -----

Head 'Installing/updating the framework (from the release channel)'
$adoptedVanilla = $false
$sdl = Join-Path $gameDir 'SDL2.dll'
$sdlReal = Join-Path $gameDir 'SDL2_real.dll'
$updater = Join-Path $gameDir $UpdaterName
$updaterSource = Join-Path $OriginalRoot $UpdaterName
if (-not (Test-Path -LiteralPath $updaterSource -PathType Leaf) -and
    (Test-Path -LiteralPath $updater -PathType Leaf)) {
    $updaterSource = $updater
}
$syncOk = $true
if (-not (Test-Path -LiteralPath $updaterSource -PathType Leaf)) {
    Say "The installer package is missing $UpdaterName; no framework files were changed." 'Red'
    $steps['sync'] = 'error-no-updater'
    $syncOk = $false
}
if ($syncOk -and -not (Test-Path -LiteralPath $sdlReal)) {
    if ((Test-Path -LiteralPath $sdl) -and -not (Test-IsProxyDll $sdl)) {
        Rename-Item -LiteralPath $sdl -NewName 'SDL2_real.dll'
        $adoptedVanilla = $true
        Say 'Vanilla copy detected: kept the original SDL2.dll as SDL2_real.dll.' 'Green'
    } elseif (Test-Path -LiteralPath $sdl) {
        Say 'This copy has the framework SDL2.dll but no SDL2_real.dll - it would not boot.' 'Red'
        Say 'Restore the original SDL2.dll (from a fresh EGGNOGG+ download) as SDL2_real.dll, then re-run.' 'Red'
        $steps['sync'] = 'error-half-install'
        $syncOk = $false
    } else {
        Say "No SDL2.dll in $gameDir - this doesn't look like a working EGGNOGG+ folder." 'Red'
        $steps['sync'] = 'error-no-sdl'
        $syncOk = $false
    }
}
$channelVersion = ''
$newFilesInstalled = @()
$managedFileMap = New-Object System.Collections.Specialized.OrderedDictionary
if ($manifest -and $manifest.managed_files) {
    foreach ($entry in @($manifest.managed_files)) {
        $priorPath = [string]$entry.path
        $priorHash = ([string]$entry.sha256).ToLowerInvariant()
        $priorSize = -1L
        try { $priorSize = [long]$entry.size } catch {}
        if ((Resolve-SafeManagedPath $gameDir $priorPath) -and
            $priorHash -match '^[0-9a-f]{64}$' -and $priorSize -ge 0) {
            $managedFileMap[$priorPath.ToLowerInvariant()] = [ordered]@{
                path = $priorPath
                sha256 = $priorHash
                size = $priorSize
            }
        }
    }
}
if ($syncOk) {
    try {
        $latest = Invoke-RestMethod -Uri $ChannelUrl -UseBasicParsing
        $channelVersion = [string]$latest.version
        Say "Channel version: $channelVersion"
        $changed = 0
        foreach ($f in $latest.files) {
            $relative = [string]$f.path
            $expectedHash = ([string]$f.sha256).ToLowerInvariant()
            $expectedSize = -1L
            try { $expectedSize = [long]$f.size } catch {}
            $dest = Resolve-SafeManagedPath $gameDir $relative
            if (-not $dest -or $expectedHash -notmatch '^[0-9a-f]{64}$' -or
                $expectedSize -lt 0) {
                throw "release channel contains an invalid managed file entry: $relative"
            }
            Ensure-Dir (Split-Path -Parent $dest)
            $have = Get-Sha256 $dest
            $wasMissing = -not (Test-Path -LiteralPath $dest -PathType Leaf)
            if ($have -eq $expectedHash) {
                $managedFileMap[$relative.ToLowerInvariant()] = [ordered]@{
                    path = $relative
                    sha256 = $expectedHash
                    size = $expectedSize
                }
                continue
            }
            if ((Test-Path -LiteralPath $dest) -and $f.overwrite -eq $false) { continue }
            Say "  updating $relative..."
            if (Download-Verified ($latest.base + $relative) $dest $expectedHash $expectedSize) {
                $changed++
                if ($wasMissing) {
                    $newFilesInstalled += [ordered]@{
                        path = $dest
                        relative = $relative
                        sha256 = $expectedHash
                    }
                }
                $managedFileMap[$relative.ToLowerInvariant()] = [ordered]@{
                    path = $relative
                    sha256 = $expectedHash
                    size = $expectedSize
                }
            } else {
                Say "  FAILED: $relative (kept existing file)" 'Red'
                $syncOk = $false
            }
        }
        if ($syncOk) {
            $steps['sync'] = 'ok'
            Say ("Framework up to date ({0} file(s) changed)." -f $changed) 'Green'
        } else {
            $steps['sync'] = 'partial'
        }
    } catch {
        Say "Couldn't reach the release channel ($($_.Exception.Message))." 'Yellow'
        Say 'Continuing without the update - whatever is installed stays as-is.'
        $steps['sync'] = 'skipped-offline'
        $syncOk = $false
    }
}
if ($syncOk) {
    try {
        $updaterWasMissing = -not (Test-Path -LiteralPath $updater -PathType Leaf)
        $updaterReceipt = Install-VerifiedLocalFile $updaterSource $updater
        if ($updaterWasMissing) {
            $newFilesInstalled += [ordered]@{
                path = $updater
                relative = $UpdaterName
                sha256 = $updaterReceipt.hash
            }
        }
        $managedFileMap[$UpdaterName.ToLowerInvariant()] = [ordered]@{
            path = $UpdaterName
            sha256 = $updaterReceipt.hash
            size = $updaterReceipt.size
        }
        Say "One-shot update helper verified." 'Green'
    } catch {
        Say "Couldn't install the update helper ($($_.Exception.Message))." 'Red'
        $steps['sync'] = 'error-updater'
        $syncOk = $false
    }
}
if ($adoptedVanilla -and
    (-not $syncOk -or -not (Test-Path -LiteralPath $sdl -PathType Leaf) -or
     -not (Test-IsProxyDll $sdl))) {
    foreach ($newFile in $newFilesInstalled) {
        $newPath = [string]$newFile.path
        if ($newPath -ieq $sdl) { continue }
        if ((Test-Path -LiteralPath $newPath -PathType Leaf) -and
            (Get-Sha256 $newPath) -eq ([string]$newFile.sha256)) {
            Remove-Item -LiteralPath $newPath -Force
            $managedFileMap.Remove(([string]$newFile.relative).ToLowerInvariant())
        }
    }
    if (Test-Path -LiteralPath $sdl -PathType Leaf) {
        Remove-Item -LiteralPath $sdl -Force
    }
    if (Test-Path -LiteralPath $sdlReal -PathType Leaf) {
        Move-Item -LiteralPath $sdlReal -Destination $sdl
    }
    $managedFileMap.Remove('sdl2.dll')
    $adoptedVanilla = $false
    $steps['sync'] = 'rolled-back-incomplete'
    Say 'The complete framework set was unavailable; restored the original vanilla SDL2.dll.' 'Yellow'
}

# ------------------------------------------------- 4: start menu ------------

Head 'Start Menu shortcut (Windows search)'
$shortcutPath = ''
if ($SkipShortcut) {
    $steps['shortcut'] = 'skipped-switch'
} elseif (Ask-YN 'Add EGGNOGG+ to the Start Menu so it shows up in the search bar?') {
    $iconLoc = (Join-Path $gameDir $ExeName) + ',0'
    if ($Artwork.icon) {
        try {
            $icoPath = Join-Path $gameDir 'eggnoggplus.ico'
            New-IcoFromPngBase64 $Artwork.icon $icoPath
            $iconLoc = "$icoPath,0"
            $managedFileMap['eggnoggplus.ico'] = [ordered]@{
                path = 'eggnoggplus.ico'
                sha256 = Get-Sha256 $icoPath
                size = (Get-Item -LiteralPath $icoPath).Length
            }
        } catch {
            Say "  (couldn't build the icon file: $($_.Exception.Message) - using the exe icon)" 'Yellow'
        }
    }
    $shortcutPath = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Microsoft\Windows\Start Menu\Programs\EGGNOGG+.lnk'
    $wsh = New-Object -ComObject WScript.Shell
    $lnk = $wsh.CreateShortcut($shortcutPath)
    $lnk.TargetPath = Join-Path $gameDir $ExeName
    $lnk.WorkingDirectory = $gameDir
    $lnk.IconLocation = $iconLoc
    $lnk.Save()
    $steps['shortcut'] = 'ok'
    Say 'Done - search for "eggnogg" in the Start Menu.' 'Green'
} else {
    $steps['shortcut'] = 'declined'
}

# -------------------------------------------- 5: yule protocol ---------------

Head 'yule:// links'
$protocolApplied = $false
$protocolCommand = ''
$protocolRoot = $ProtocolRegistryRoot
$protocolCommandPath = Join-Path $protocolRoot 'shell\open\command'
if (-not $syncOk -or -not (Test-Path -LiteralPath $sdl -PathType Leaf) -or
    -not (Test-IsProxyDll $sdl)) {
    $steps['deep_links'] = 'skipped-incomplete-framework'
} elseif ($SkipProtocol) {
    $steps['deep_links'] = 'skipped-switch'
} elseif (Ask-YN 'Open safe yule:// hub, queue, request, challenge, and map-preview links with EGGNOGG+?') {
    $protocolCommand = Get-YuleProtocolCommand $gameDir
    $existingCommand = Get-RegistryDefault $protocolCommandPath
    $previousOwned = $manifest -and $manifest.deep_link_protocol -and
        $manifest.deep_link_protocol.applied -and
        ([string]$manifest.deep_link_protocol.command -ceq $existingCommand) -and
        (Test-OwnedYuleProtocol $existingCommand)
    if ((Test-Path -LiteralPath $protocolRoot) -and
        $existingCommand -and -not $previousOwned) {
        Say 'An externally owned yule:// handler already exists; left it unchanged.' 'Yellow'
        $steps['deep_links'] = 'skipped-existing-owner'
        $protocolCommand = ''
    } else {
        if ((Test-Path -LiteralPath $protocolRoot) -and -not $previousOwned) {
            Say 'An unrecognized yule:// registry tree already exists; left it unchanged.' 'Yellow'
            $steps['deep_links'] = 'skipped-existing-owner'
            $protocolCommand = ''
        } else {
            if ($previousOwned) {
                Remove-Item -LiteralPath $protocolRoot -Recurse -Force
            }
            New-Item -Path $protocolCommandPath -Force | Out-Null
            Set-Item -LiteralPath $protocolRoot -Value 'URL:Yule Protocol'
            New-ItemProperty -LiteralPath $protocolRoot -Name 'URL Protocol' `
                -Value '' -PropertyType String -Force | Out-Null
            Set-Item -LiteralPath $protocolCommandPath -Value $protocolCommand
            $protocolApplied = Test-OwnedYuleProtocol $protocolCommand
            if (-not $protocolApplied) {
                Remove-Item -LiteralPath $protocolRoot -Recurse -Force -ErrorAction SilentlyContinue
                throw 'The yule:// handler could not be verified after registration.'
            }
            $steps['deep_links'] = 'ok'
            Say 'Safe yule:// links registered for this Windows account.' 'Green'
        }
    }
} else {
    $steps['deep_links'] = 'declined'
}

# ------------------------------------------------- 6: steam -----------------

Head 'Steam (non-Steam game + artwork)'
$steamApplied = $false
$steamAccounts = @()
$appId = [uint32]0
if ($SkipSteam) {
    $steps['steam'] = 'skipped-switch'
} else {
    $steamPath = ''
    try { $steamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath } catch {}
    if (-not $steamPath -or -not (Test-Path $steamPath)) {
        Say 'Steam not found - skipping.' 'Yellow'
        $steps['steam'] = 'no-steam'
    } elseif (Ask-YN 'Add EGGNOGG+ to Steam (with artwork)?') {
        $steamWasRunning = $false
        if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) {
            if (Ask-YN 'Steam is running and must be closed to add the shortcut. Close it now?') {
                $steamWasRunning = $true
                Start-Process -FilePath (Join-Path $steamPath 'steam.exe') -ArgumentList '-shutdown'
                $waited = 0
                while ((Get-Process -Name 'steam' -ErrorAction SilentlyContinue) -and $waited -lt 30) {
                    Start-Sleep -Seconds 1; $waited++
                }
                if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) {
                    if (Ask-YN "Steam didn't close gracefully. Force-close it?") {
                        Stop-Process -Name 'steam' -Force; Start-Sleep -Seconds 2
                    } else {
                        Say 'Leaving Steam alone - skipping the Steam step.' 'Yellow'
                        $steps['steam'] = 'skipped-running'
                    }
                }
            } else {
                $steps['steam'] = 'skipped-running'
            }
        }
        if (-not $steps.Contains('steam')) {
            $exeQuoted = '"' + (Join-Path $gameDir $ExeName) + '"'
            $startDirQuoted = '"' + $gameDir + '"'
            $appId = Get-ShortcutAppId $exeQuoted 'Eggnogg+'
            $accounts = Get-ChildItem (Join-Path $steamPath 'userdata') -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^\d+$' -and (Test-Path (Join-Path $_.FullName 'config')) }
            foreach ($acct in $accounts) {
                $cfgDir = Join-Path $acct.FullName 'config'
                $vdfPath = Join-Path $cfgDir 'shortcuts.vdf'
                try {
                    if (Test-Path -LiteralPath $vdfPath) {
                        Copy-Item -LiteralPath $vdfPath -Destination ("$vdfPath.bak-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) -Force
                    }
                    $root = Read-ShortcutsVdf $vdfPath
                    $shorts = $root['shortcuts']
                    # replace an existing Eggnogg entry, else append at the next index
                    $targetKey = $null
                    foreach ($k in @($shorts.Keys)) {
                        $entry = $shorts[$k]
                        foreach ($fieldName in @('AppName', 'appname')) {
                            if ($entry.Contains($fieldName) -and ([string]$entry[$fieldName]) -match '(?i)eggnogg') { $targetKey = $k; break }
                        }
                        if ($targetKey) { break }
                    }
                    if (-not $targetKey) {
                        $max = -1
                        foreach ($k in $shorts.Keys) { $n = 0; if ([int]::TryParse($k, [ref]$n) -and $n -gt $max) { $max = $n } }
                        $targetKey = [string]($max + 1)
                    }
                    $gridDir = Join-Path $cfgDir 'grid'
                    Ensure-Dir $gridDir
                    $iconPath = Join-Path $gameDir $ExeName
                    if ($Artwork.icon) {
                        $iconPath = Join-Path $gridDir "$appId`_icon.png"
                        [IO.File]::WriteAllBytes($iconPath, [Convert]::FromBase64String($Artwork.icon))
                    }
                    $shorts[$targetKey] = New-ShortcutEntry $appId $exeQuoted $startDirQuoted $iconPath
                    Write-ShortcutsVdf $vdfPath $root
                    # artwork (renamed to this shortcut's appid)
                    $slots = @(
                        @{ key = 'grid';    file = "$appId.png" },
                        @{ key = 'gridp';   file = "$appId" + "p.png" },
                        @{ key = 'hero';    file = "$appId" + "_hero.png" },
                        @{ key = 'logo';    file = "$appId" + "_logo.png" },
                        @{ key = 'logopos'; file = "$appId.json" }
                    )
                    foreach ($slot in $slots) {
                        $b64 = $Artwork[$slot.key]
                        if ($b64) {
                            [IO.File]::WriteAllBytes((Join-Path $gridDir $slot.file), [Convert]::FromBase64String($b64))
                        }
                    }
                    $steamAccounts += $acct.Name
                    Say "  account $($acct.Name): shortcut + artwork applied" 'Green'
                } catch {
                    Say "  account $($acct.Name): FAILED ($($_.Exception.Message)) - restoring backup" 'Red'
                    $bak = Get-ChildItem "$vdfPath.bak-*" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
                    if ($bak) { Copy-Item -LiteralPath $bak.FullName -Destination $vdfPath -Force }
                }
            }
            $steamApplied = $steamAccounts.Count -gt 0
            $steps['steam'] = if ($steamApplied) { 'ok' } else { 'no-accounts' }
            if ($steamWasRunning) {
                Say 'Restarting Steam...'
                Start-Process -FilePath (Join-Path $steamPath 'steam.exe')
            }
        }
    } else {
        $steps['steam'] = 'declined'
    }
}

# ---------------------------------------------- 7: log console off ----------

Head 'Framework log console'
$modsDir = Join-Path $gameDir 'mods'
Ensure-Dir $modsDir
$fwCfg = Join-Path $modsDir 'modframework.cfg'
$lines = @()
if (Test-Path -LiteralPath $fwCfg) {
    $lines = @(Get-Content -LiteralPath $fwCfg | Where-Object { $_ -notmatch '^\s*show_log_console' })
}
$lines += 'show_log_console=0'
Set-Content -LiteralPath $fwCfg -Value $lines
$steps['log_console'] = 'ok'
Say 'Log console defaults to hidden (re-enable any time in Options -> Mods).' 'Green'

# ---------------------------------------------- 8: manifest -----------------

Head 'Writing the Yule manifest'
Ensure-Dir $ManifestDir
$stepsObj = New-Object PSObject
foreach ($k in $steps.Keys) { $stepsObj | Add-Member -MemberType NoteProperty -Name $k -Value $steps[$k] }
$m = [ordered]@{
    manifest_version    = 2
    install_dir         = $gameDir
    game_executable     = (Join-Path $gameDir $ExeName)
    updater_executable  = (Join-Path $gameDir $UpdaterName)
    framework_version   = $channelVersion
    channel_url         = $ChannelUrl
    installed_at        = (Get-Date -Format 's')
    installer_version   = 2
    moved_by_installer  = $movedByInstaller
    adopted_vanilla     = $adoptedVanilla
    start_menu_shortcut = $shortcutPath
    deep_link_protocol  = [ordered]@{ applied = $protocolApplied; command = $protocolCommand }
    steam               = [ordered]@{ applied = $steamApplied; appid = $appId; accounts = $steamAccounts }
    managed_files       = @($managedFileMap.Values)
    steps               = $stepsObj
}
$json = $m | ConvertTo-Json -Depth 5
Set-Content -LiteralPath $ManifestPath -Value $json
Set-Content -LiteralPath (Join-Path $gameDir 'install.json') -Value $json
Say "Manifest: $ManifestPath" 'Green'

# ---------------------------------------------------------------- done ------

Say ''
Say '  All done!' 'White'
Say "  EGGNOGG+ lives at: $gameDir" 'White'
foreach ($k in $steps.Keys) { Say ("    {0,-12} {1}" -f $k, $steps[$k]) 'DarkGray' }
Say ''
if (-not $Yes) { Read-Host 'Press Enter to close' | Out-Null }
exit 0
