# Yule release builder (owner-side).
# Produces the uploadable channel tree + the distributable installer zip:
#   dist/releases/latest.json
#   dist/releases/<version>/{SDL2.dll, lua51.dll, libgcc_s_dw2-1.dll, SDL2_mixer.dll}
#   dist/installer/{INSTALL.bat, UNINSTALL.bat, install.ps1(artwork embedded)}
#   dist/EGGNOGG+_framework_installer.zip
# Upload the contents of dist/releases/ to https://loafiieee.com/yule/releases/ .
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$GameDir = '',
    [string]$OutDir = '',
    [string]$ChannelBase = 'https://loafiieee.com/yule/releases',
    [string]$ArtworkDir = 'C:\Program Files (x86)\Steam\userdata\1423819074\config\grid',
    [string]$ArtworkAppId = '2231133229',
    [string]$IconPath = '',   # default: installer\assets\steam_icon.png (resolved below)
    [string]$Notes = ''
)

$ErrorActionPreference = 'Stop'
# ($PSScriptRoot is empty inside param() defaults under `powershell -File`, so
#  path defaults are resolved here instead)
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = Split-Path -Parent $toolsDir       # eggnoggplus-win/
if (-not $GameDir)  { $GameDir = $repoDir }
if (-not $OutDir)   { $OutDir = Join-Path $repoDir 'dist' }
if (-not $IconPath) { $IconPath = Join-Path $repoDir 'installer\assets\steam_icon.png' }

if ($Version -notmatch '^[0-9]+(?:\.[0-9]+)*$') {
    throw "invalid release version '$Version' (expected dot-separated decimal components)"
}
$versionHeader = Join-Path $repoDir 'update_ext.h'
$versionHeaderText = Get-Content -LiteralPath $versionHeader -Raw
$sourceVersionMatch = [regex]::Match(
    $versionHeaderText,
    '#define\s+FRAMEWORK_VERSION\s+"([^"]+)"'
)
if (-not $sourceVersionMatch.Success) {
    throw "cannot read FRAMEWORK_VERSION from $versionHeader"
}
$sourceVersion = $sourceVersionMatch.Groups[1].Value
if ($sourceVersion -cne $Version) {
    throw @"
Refusing to advertise release $Version because update_ext.h still declares
FRAMEWORK_VERSION "$sourceVersion". Update the source version, close Eggnogg+,
run bash compile.sh, and retry with that exact version.
"@
}

# libwinpthread-1.dll is a transitive import of libgcc_s_dw2-1.dll - without it a
# fresh vanilla install fails to boot with "libwinpthread-1.dll is missing".
$ReleaseFiles = @('SDL2.dll', 'lua51.dll', 'libgcc_s_dw2-1.dll', 'libwinpthread-1.dll', 'SDL2_mixer.dll')

# --- sanity: the SDL2.dll being shipped must be the framework proxy ---------
$sdl = Join-Path $GameDir 'SDL2.dll'
if (-not (Test-Path -LiteralPath $sdl -PathType Leaf)) { throw "no SDL2.dll in $GameDir" }
$built = Join-Path $GameDir 'build\SDL2_test.dll'
if (-not (Test-Path -LiteralPath $built -PathType Leaf)) {
    throw @"
Refusing to package without the verified build\SDL2_test.dll artifact.
Close every Eggnogg+ process and run bash compile.sh, then confirm SDL2.dll and
build\SDL2_test.dll have matching SHA-256 hashes before publishing. No installed
file was changed.
"@
}
$bytes = [IO.File]::ReadAllBytes($sdl)
$marker = [Text.Encoding]::ASCII.GetBytes('modframework')
$found = $false
for ($i = 0; $i -le $bytes.Length - $marker.Length -and -not $found; $i++) {
    $ok = $true
    for ($j = 0; $j -lt $marker.Length; $j++) { if ($bytes[$i + $j] -ne $marker[$j]) { $ok = $false; break } }
    if ($ok) { $found = $true }
}
if (-not $found) { throw "SDL2.dll in $GameDir does not look like the framework proxy (no marker)" }
$binaryText = [Text.Encoding]::ASCII.GetString($bytes)
$binaryVersionMatch = [regex]::Match(
    $binaryText,
    'YULE_FRAMEWORK_VERSION=([0-9]+(?:\.[0-9]+)*)'
)
if (-not $binaryVersionMatch.Success) {
    throw @"
Refusing to package SDL2.dll because it has no compiled framework-version marker.
Close Eggnogg+, run bash compile.sh, and retry. No release output was changed.
"@
}
$binaryVersion = $binaryVersionMatch.Groups[1].Value
if ($binaryVersion -cne $Version) {
    throw @"
Refusing to advertise release $Version because the deployed SDL2.dll was compiled
as FRAMEWORK_VERSION "$binaryVersion". Close Eggnogg+, run bash compile.sh after
updating update_ext.h, and retry. No release output was changed.
"@
}
$deployedHash = (Get-FileHash -LiteralPath $sdl -Algorithm SHA256).Hash
$verifiedHash = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
if ($deployedHash -ne $verifiedHash) {
    throw @"
Refusing to package a stale deployed SDL2.dll: it differs from build\SDL2_test.dll.
Deployed SHA-256: $deployedHash
Verified SHA-256: $verifiedHash
Close every Eggnogg+ process, run bash compile.sh so it can install the exact
verified build artifact, confirm the two files have matching SHA-256 hashes,
then run tools\build_release.ps1 again. No installed file was changed.
"@
}
$updater = Join-Path $GameDir 'YuleUpdater.exe'
$builtUpdater = Join-Path $GameDir 'build\YuleUpdater.exe'
if (-not (Test-Path -LiteralPath $updater -PathType Leaf) -or
    -not (Test-Path -LiteralPath $builtUpdater -PathType Leaf)) {
    throw @"
Refusing to package without the verified one-shot updater and its build artifact.
Run bash compile.sh, then retry. No installed file was changed.
"@
}
$updaterHash = (Get-FileHash -LiteralPath $updater -Algorithm SHA256).Hash
$builtUpdaterHash = (Get-FileHash -LiteralPath $builtUpdater -Algorithm SHA256).Hash
if ($updaterHash -ne $builtUpdaterHash) {
    throw @"
Refusing to package a stale YuleUpdater.exe: it differs from build\YuleUpdater.exe.
Run bash compile.sh, then retry. No installed file was changed.
"@
}
$objdumpCommand = Get-Command objdump -ErrorAction SilentlyContinue
$objdumpPath = if ($objdumpCommand) { $objdumpCommand.Source } else { '' }
if (-not $objdumpPath) {
    $standardObjdump = 'C:\msys64\mingw32\bin\objdump.exe'
    if (Test-Path -LiteralPath $standardObjdump -PathType Leaf) {
        $objdumpPath = $standardObjdump
    } else {
        throw "Cannot audit YuleUpdater.exe imports because objdump was not found."
    }
}
$updaterImports = (& $objdumpPath -p $updater | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "Cannot audit YuleUpdater.exe imports (objdump exit $LASTEXITCODE)."
}
foreach ($replaceableImport in @(
    'SDL2.dll',
    'lua51.dll',
    'libgcc_s_dw2-1.dll',
    'libwinpthread-1.dll',
    'SDL2_mixer.dll'
)) {
    if ($updaterImports -match
        ('DLL Name:\s*' + [regex]::Escape($replaceableImport))) {
        throw @"
Refusing to package YuleUpdater.exe because it imports replaceable payload
$replaceableImport. Rebuild the helper as a self-contained executable before
publishing; otherwise it can lock its own .old backup during an update.
"@
    }
}

# --- channel tree ------------------------------------------------------------
$relDir = Join-Path $OutDir "releases\$Version"
New-Item -ItemType Directory -Path $relDir -Force | Out-Null
$fileEntries = @()
foreach ($name in $ReleaseFiles) {
    $src = Join-Path $GameDir $name
    if (-not (Test-Path $src)) {
        if ($name -eq 'SDL2_mixer.dll') { Write-Warning "skipping optional $name (not found)"; continue }
        throw "missing release file: $src"
    }
    Copy-Item $src (Join-Path $relDir $name) -Force
    $fileEntries += [ordered]@{
        path      = $name
        sha256    = (Get-FileHash $src -Algorithm SHA256).Hash.ToLowerInvariant()
        size      = (Get-Item $src).Length
        overwrite = $true
    }
}
$latest = [ordered]@{
    channel_version = 1
    version         = $Version
    notes           = $Notes
    base            = "$ChannelBase/$Version/"
    files           = $fileEntries
}
$latestPath = Join-Path $OutDir 'releases\latest.json'
$latest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $latestPath
Write-Host "channel:   $latestPath (+ $($fileEntries.Count) files under releases\$Version\)"

# --- installer with embedded artwork -----------------------------------------
$tpl = Get-Content -LiteralPath (Join-Path $repoDir 'installer\install.ps1') -Raw
$slotFiles = [ordered]@{
    grid    = "$ArtworkAppId.png"
    gridp   = "$ArtworkAppId" + 'p.png'
    hero    = "$ArtworkAppId" + '_hero.png'
    logo    = "$ArtworkAppId" + '_logo.png'
    logopos = "$ArtworkAppId.json"
}
$artLines = @('$Artwork = @{')
foreach ($k in $slotFiles.Keys) {
    $b64 = ''
    if ($slotFiles[$k]) {
        $p = Join-Path $ArtworkDir $slotFiles[$k]
        if (Test-Path -LiteralPath $p) {
            $b64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($p))
        } else {
            Write-Warning "artwork slot '$k' missing ($p) - shipping empty"
        }
    }
    $artLines += "    $k = '$b64';"
}
$iconB64 = ''
if ($IconPath -and (Test-Path -LiteralPath $IconPath)) {
    $iconB64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($IconPath))
} else {
    Write-Warning "icon missing ($IconPath) - installer will fall back to the exe icon"
}
$artLines += "    icon = '$iconB64';"
$artLines += '}'
$artBlock = $artLines -join "`r`n"
$pattern = '(?s)# ==ARTWORK-BEGIN==.*?# ==ARTWORK-END=='
if ($tpl -notmatch $pattern) { throw 'installer template is missing the ARTWORK markers' }
$tpl = [regex]::Replace($tpl, $pattern, "# ==ARTWORK-BEGIN== (embedded by build_release.ps1)`r`n$artBlock`r`n# ==ARTWORK-END==")

$instDir = Join-Path $OutDir 'installer'
New-Item -ItemType Directory -Path $instDir -Force | Out-Null
Set-Content -LiteralPath (Join-Path $instDir 'install.ps1') -Value $tpl
Copy-Item (Join-Path $repoDir 'installer\INSTALL.bat') (Join-Path $instDir 'INSTALL.bat') -Force
Copy-Item (Join-Path $repoDir 'installer\UNINSTALL.bat') (Join-Path $instDir 'UNINSTALL.bat') -Force
Copy-Item $updater (Join-Path $instDir 'YuleUpdater.exe') -Force

$zipPath = Join-Path $OutDir 'EGGNOGG+_framework_installer.zip'
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $instDir '*') -DestinationPath $zipPath
Write-Host "installer: $zipPath"
Write-Host ''
Write-Host "Publish releases\$Version first and verify every payload URL/hash."
Write-Host "Publish releases\latest.json atomically LAST; then publish the installer zip."
