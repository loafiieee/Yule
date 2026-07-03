# Yule release builder (owner-side).
# Produces the uploadable channel tree + the distributable installer zip:
#   dist/releases/latest.json
#   dist/releases/<version>/{SDL2.dll, lua51.dll, libgcc_s_dw2-1.dll, SDL2_mixer.dll}
#   dist/installer/{INSTALL.bat, install.ps1(artwork embedded)}
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

# libwinpthread-1.dll is a transitive import of libgcc_s_dw2-1.dll - without it a
# fresh vanilla install fails to boot with "libwinpthread-1.dll is missing".
$ReleaseFiles = @('SDL2.dll', 'lua51.dll', 'libgcc_s_dw2-1.dll', 'libwinpthread-1.dll', 'SDL2_mixer.dll')

# --- sanity: the SDL2.dll being shipped must be the framework proxy ---------
$sdl = Join-Path $GameDir 'SDL2.dll'
if (-not (Test-Path $sdl)) { throw "no SDL2.dll in $GameDir" }
$bytes = [IO.File]::ReadAllBytes($sdl)
$marker = [Text.Encoding]::ASCII.GetBytes('modframework')
$found = $false
for ($i = 0; $i -le $bytes.Length - $marker.Length -and -not $found; $i++) {
    $ok = $true
    for ($j = 0; $j -lt $marker.Length; $j++) { if ($bytes[$i + $j] -ne $marker[$j]) { $ok = $false; break } }
    if ($ok) { $found = $true }
}
if (-not $found) { throw "SDL2.dll in $GameDir does not look like the framework proxy (no marker)" }
$built = Join-Path $GameDir 'build\SDL2_test.dll'
if (Test-Path $built) {
    if ((Get-FileHash $sdl).Hash -ne (Get-FileHash $built).Hash) {
        Write-Warning 'deployed SDL2.dll differs from build\SDL2_test.dll - shipping the DEPLOYED one'
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

$zipPath = Join-Path $OutDir 'EGGNOGG+_framework_installer.zip'
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $instDir '*') -DestinationPath $zipPath
Write-Host "installer: $zipPath"
Write-Host ''
Write-Host "upload dist\releases\* so that $ChannelBase/latest.json resolves, then publish the zip."
