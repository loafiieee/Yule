# EGGNOGG+ Framework (Yule) installer
# Windows PowerShell 5.1 compatible. Double-click INSTALL.bat to run.
#
# What it does (each step asks Y/n):
#   1. finds your EGGNOGG+ copy (next to this script, via a previous install, or asks)
#   2. moves it to %LOCALAPPDATA%\EGGNOGG+ (safe from Downloads cleanups)
#   3. installs/updates the framework DLLs from the release channel
#      (renames the vanilla SDL2.dll to SDL2_real.dll on first install)
#   4. Start Menu shortcut (makes it show up in Windows search)
#   5. adds it to Steam as a non-Steam game with artwork
#   6. turns the framework log console off by default
#   7. writes the Yule manifest so future updates are automatic
param(
    [string]$ChannelUrl = 'https://loafiieee.com/yule/releases/latest.json',
    # internal / testing switches
    [string]$OriginalRoot = '',
    [switch]$NoReexec,
    [string]$GamePath = '',
    [string]$InstallDir = '',
    [string]$ManifestDir = '',
    [switch]$SkipSteam,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$AppName = 'EGGNOGG+'
$ExeName = 'eggnoggplus.exe'
if (-not $InstallDir)  { $InstallDir  = Join-Path $env:LOCALAPPDATA 'EGGNOGG+' }
if (-not $ManifestDir) { $ManifestDir = Join-Path $env:LOCALAPPDATA 'Yule' }
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

function Pick-Folder([string]$description) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = $description
        $dlg.ShowNewFolderButton = $false
        $result = $dlg.ShowDialog((New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }))
        if ($result -eq [System.Windows.Forms.DialogResult]::OK) { return $dlg.SelectedPath }
        return ''
    } catch {
        return Read-Host $description
    }
}

function Get-Sha256([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Ensure-Dir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}

function Download-Verified([string]$url, [string]$dest, [string]$sha256) {
    $tmp = "$dest.yule-tmp"
    foreach ($try in 1..2) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
            $got = (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash.ToLowerInvariant()
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

function Test-GameRunning {
    return $null -ne (Get-Process -Name 'eggnoggplus' -ErrorAction SilentlyContinue)
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

# ------------------------------------------------------------ re-exec -------

if (-not $NoReexec) {
    $selfDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $tempCopy = Join-Path $env:TEMP ("yule_install_" + [Guid]::NewGuid().ToString('N') + ".ps1")
    Copy-Item -LiteralPath $MyInvocation.MyCommand.Path -Destination $tempCopy -Force
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$tempCopy`"",
                 '-NoReexec', '-OriginalRoot', "`"$selfDir`"", '-ChannelUrl', "`"$ChannelUrl`"")
    if ($GamePath)    { $argList += @('-GamePath', "`"$GamePath`"") }
    if ($InstallDir -ne (Join-Path $env:LOCALAPPDATA 'EGGNOGG+')) { $argList += @('-InstallDir', "`"$InstallDir`"") }
    if ($SkipSteam)   { $argList += '-SkipSteam' }
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
Say '  EGGNOGG+ framework (Yule) installer' 'White'
Say '  ------------------------------------' 'DarkGray'

# ------------------------------------------------------- 1: locate game -----

Head 'Locating your EGGNOGG+ folder'
$gameDir = ''
if ($GamePath -and (Test-Path (Join-Path $GamePath $ExeName))) {
    $gameDir = (Resolve-Path $GamePath).Path
} elseif (Test-Path (Join-Path $OriginalRoot $ExeName)) {
    $gameDir = $OriginalRoot
} elseif (Test-Path (Join-Path $OriginalRoot "EGGNOGG+\$ExeName")) {
    $gameDir = Join-Path $OriginalRoot 'EGGNOGG+'
} elseif ($manifest -and $manifest.install_dir -and (Test-Path (Join-Path $manifest.install_dir $ExeName))) {
    $gameDir = $manifest.install_dir
    Say "Found your previous install." 'Green'
} else {
    Say "I couldn't find $ExeName next to this script or from a previous install."
    Say 'Pick your EGGNOGG+ folder in the dialog (the folder containing eggnoggplus.exe)...'
    $typed = Pick-Folder "Select your EGGNOGG+ folder (the one containing $ExeName)"
    if ($typed -and (Test-Path (Join-Path $typed $ExeName))) {
        $gameDir = (Resolve-Path $typed).Path
    } elseif ($typed) {
        Say "No $ExeName in `"$typed`"." 'Yellow'
    }
}
if (-not $gameDir) {
    Say ''
    Say "No EGGNOGG+ copy found. Get the game first, put this installer next to its folder, and run it again." 'Yellow'
    Read-Host 'Press Enter to exit' | Out-Null
    exit 1
}
Say "Game folder: $gameDir" 'Green'

while (Test-GameRunning) {
    Say 'EGGNOGG+ is currently running - please close it.' 'Yellow'
    if (-not (Ask-YN 'Check again?')) { Say 'Aborted.'; exit 1 }
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
$syncOk = $true
if (-not (Test-Path -LiteralPath $sdlReal)) {
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
if ($syncOk) {
    try {
        $latest = Invoke-RestMethod -Uri $ChannelUrl -UseBasicParsing
        $channelVersion = [string]$latest.version
        Say "Channel version: $channelVersion"
        $changed = 0
        foreach ($f in $latest.files) {
            $dest = Join-Path $gameDir $f.path
            Ensure-Dir (Split-Path -Parent $dest)
            $have = Get-Sha256 $dest
            if ($have -eq $f.sha256.ToLowerInvariant()) { continue }
            if ((Test-Path -LiteralPath $dest) -and $f.overwrite -eq $false) { continue }
            Say "  updating $($f.path)..."
            if (Download-Verified ($latest.base + $f.path) $dest $f.sha256) { $changed++ }
            else { Say "  FAILED: $($f.path) (kept existing file)" 'Red'; $syncOk = $false }
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
    }
}

# ------------------------------------------------- 4: start menu ------------

Head 'Start Menu shortcut (Windows search)'
$shortcutPath = ''
if (Ask-YN 'Add EGGNOGG+ to the Start Menu so it shows up in the search bar?') {
    $iconLoc = (Join-Path $gameDir $ExeName) + ',0'
    if ($Artwork.icon) {
        try {
            $icoPath = Join-Path $gameDir 'eggnoggplus.ico'
            New-IcoFromPngBase64 $Artwork.icon $icoPath
            $iconLoc = "$icoPath,0"
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

# ------------------------------------------------- 5: steam -----------------

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

# ---------------------------------------------- 6: log console off ----------

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

# ---------------------------------------------- 7: manifest -----------------

Head 'Writing the Yule manifest'
Ensure-Dir $ManifestDir
$stepsObj = New-Object PSObject
foreach ($k in $steps.Keys) { $stepsObj | Add-Member -MemberType NoteProperty -Name $k -Value $steps[$k] }
$m = [ordered]@{
    manifest_version    = 1
    install_dir         = $gameDir
    framework_version   = $channelVersion
    channel_url         = $ChannelUrl
    installed_at        = (Get-Date -Format 's')
    installer_version   = 1
    moved_by_installer  = $movedByInstaller
    adopted_vanilla     = $adoptedVanilla
    start_menu_shortcut = $shortcutPath
    steam               = [ordered]@{ applied = $steamApplied; appid = $appId; accounts = $steamAccounts }
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
