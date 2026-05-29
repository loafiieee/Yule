param(
  [string]$BaseUrl = "https://loafiieee.com/eggnogg/cosmetics/v1",
  [string]$Version = "",
  [switch]$ImportSheet,
  [string]$SheetPath = "..\assets\hats.png",
  [string]$ManifestPath = "manifest.json",
  [string]$NewHatId = "",
  [string]$NewHatName = "",
  [switch]$Force
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$CellW = 32
$CellH = 32

Add-Type -AssemblyName System.Drawing

function Join-Root([string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
  return Join-Path $Root $Path
}

function Get-Sha256Hex([string]$Path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
      $hash = $sha.ComputeHash($stream)
      return (($hash | ForEach-Object { $_.ToString("x2") }) -join "")
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Read-JsonObject([string]$Path) {
  if (!(Test-Path -LiteralPath $Path)) { return $null }
  return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Write-JsonObject([string]$Path, $Object) {
  $dir = Split-Path -Parent $Path
  if ($dir -and !(Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
  }
  $Object | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Save-Png($Bitmap, [string]$Path) {
  $dir = Split-Path -Parent $Path
  if ($dir -and !(Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
  }
  $Bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
}

function New-TransparentPng([string]$Path) {
  $bmp = New-Object System.Drawing.Bitmap -ArgumentList @($CellW, $CellH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try {
    Save-Png $bmp $Path
  } finally {
    $bmp.Dispose()
  }
}

function Test-HatId([string]$Id) {
  return $Id -match "^[A-Za-z0-9_.-]{1,64}$"
}

function Title-FromId([string]$Id) {
  return (($Id -replace "[-_.]+", " ").Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries) |
    ForEach-Object { $_.Substring(0,1).ToUpperInvariant() + $_.Substring(1) }) -join " "
}

function Default-HatMeta([string]$Id, [string]$Name) {
  if (!$Name) { $Name = Title-FromId $Id }
  return [ordered]@{
    id = $Id
    name = $Name
    motion = $true
    scale = 0.65
    x = 0.0
    y = 10.5
    bob = 0.5
    tilt = 5.0
    drag = 0.3
    allowed_online = $true
  }
}

function New-HatScaffold([string]$Id, [string]$Name) {
  if (!(Test-HatId $Id)) { throw "Invalid hat id '$Id'. Use letters, numbers, dash, underscore, or dot." }
  $hatDir = Join-Root "hats"
  if (!(Test-Path -LiteralPath $hatDir)) {
    New-Item -ItemType Directory -Path $hatDir | Out-Null
  }
  $pngPath = Join-Path $hatDir "$Id.png"
  $jsonPath = Join-Path $hatDir "$Id.json"
  if ((Test-Path -LiteralPath $pngPath) -and !$Force) { throw "$pngPath already exists. Use -Force to overwrite." }
  if ((Test-Path -LiteralPath $jsonPath) -and !$Force) { throw "$jsonPath already exists. Use -Force to overwrite." }
  New-TransparentPng $pngPath
  Write-JsonObject $jsonPath (Default-HatMeta $Id $Name)
  Write-Host "Created $pngPath"
  Write-Host "Created $jsonPath"
}

function Import-HatsFromSheet {
  $sheetFull = Join-Root $SheetPath
  $manifestFull = Join-Root $ManifestPath
  if (!(Test-Path -LiteralPath $sheetFull)) { throw "Sheet not found: $sheetFull" }
  if (!(Test-Path -LiteralPath $manifestFull)) { throw "Manifest not found: $manifestFull" }

  $manifest = Read-JsonObject $manifestFull
  if (!$manifest -or !$manifest.hats) { throw "Manifest has no hats list: $manifestFull" }

  $hatDir = Join-Root "hats"
  if (!(Test-Path -LiteralPath $hatDir)) {
    New-Item -ItemType Directory -Path $hatDir | Out-Null
  }

  $sheet = [System.Drawing.Bitmap]::FromFile($sheetFull)
  try {
    foreach ($hat in $manifest.hats) {
      $id = [string]$hat.id
      if (!(Test-HatId $id)) { throw "Invalid hat id in manifest: $id" }
      $idx = [int]$hat.sprite_index
      $srcX = $idx * $CellW
      if ($srcX + $CellW -gt $sheet.Width -or $CellH -gt $sheet.Height) {
        throw "Sprite index $idx for '$id' is outside $sheetFull"
      }

      $pngPath = Join-Path $hatDir "$id.png"
      $jsonPath = Join-Path $hatDir "$id.json"
      if ((Test-Path -LiteralPath $pngPath) -and !$Force) { throw "$pngPath already exists. Use -Force to overwrite." }
      if ((Test-Path -LiteralPath $jsonPath) -and !$Force) { throw "$jsonPath already exists. Use -Force to overwrite." }

      $frame = New-Object System.Drawing.Bitmap -ArgumentList @($CellW, $CellH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
      try {
        $gfx = [System.Drawing.Graphics]::FromImage($frame)
        try {
          $gfx.Clear([System.Drawing.Color]::Transparent)
          $gfx.DrawImage($sheet,
            (New-Object System.Drawing.Rectangle -ArgumentList @(0, 0, $CellW, $CellH)),
            (New-Object System.Drawing.Rectangle -ArgumentList @($srcX, 0, $CellW, $CellH)),
            [System.Drawing.GraphicsUnit]::Pixel)
        } finally {
          $gfx.Dispose()
        }
        Save-Png $frame $pngPath
      } finally {
        $frame.Dispose()
      }

      $meta = [ordered]@{
        id = $id
        name = [string]$hat.name
        motion = [bool]$hat.motion
        scale = [double]$hat.scale
        x = if ($null -ne $hat.x) { [double]$hat.x } else { 0.0 }
        y = [double]$hat.y
        bob = [double]$hat.bob
        tilt = [double]$hat.tilt
        drag = [double]$hat.drag
        allowed_online = if ($null -ne $hat.allowed_online) { [bool]$hat.allowed_online } else { $true }
      }
      Write-JsonObject $jsonPath $meta
      Write-Host "Imported $id"
    }
  } finally {
    $sheet.Dispose()
  }
}

function Get-HatDefinitions {
  $hatDir = Join-Root "hats"
  if (!(Test-Path -LiteralPath $hatDir)) { throw "No hats folder. Add hats/*.png first, or run with -NewHatId <id>." }

  $items = @()
  foreach ($png in Get-ChildItem -LiteralPath $hatDir -Filter "*.png" | Sort-Object Name) {
    if ($png.BaseName.StartsWith("_")) { continue }
    $metaPath = [System.IO.Path]::ChangeExtension($png.FullName, ".json")
    $meta = Read-JsonObject $metaPath
    if ($meta -and $meta.enabled -eq $false) { continue }

    $id = if ($meta -and $meta.id) { [string]$meta.id } else { $png.BaseName }
    if (!(Test-HatId $id)) { throw "Invalid hat id '$id' in $($png.FullName)" }

    $img = [System.Drawing.Bitmap]::FromFile($png.FullName)
    try {
      if ($img.Width -ne $CellW -or $img.Height -ne $CellH) {
        throw "$($png.FullName) must be ${CellW}x${CellH}; got $($img.Width)x$($img.Height)"
      }
    } finally {
      $img.Dispose()
    }

    $items += [pscustomobject]@{
      Png = $png.FullName
      Order = if ($meta -and $null -ne $meta.order) { [int]$meta.order } else { 100000 }
      Manifest = [ordered]@{
        id = $id
        name = if ($meta -and $meta.name) { [string]$meta.name } else { Title-FromId $id }
        sprite_index = 0
        motion = if ($meta -and $null -ne $meta.motion) { [bool]$meta.motion } else { $true }
        scale = if ($meta -and $null -ne $meta.scale) { [double]$meta.scale } else { 0.65 }
        x = if ($meta -and $null -ne $meta.x) { [double]$meta.x } else { 0.0 }
        y = if ($meta -and $null -ne $meta.y) { [double]$meta.y } else { 10.5 }
        bob = if ($meta -and $null -ne $meta.bob) { [double]$meta.bob } else { 0.5 }
        tilt = if ($meta -and $null -ne $meta.tilt) { [double]$meta.tilt } else { 5.0 }
        drag = if ($meta -and $null -ne $meta.drag) { [double]$meta.drag } else { 0.3 }
        allowed_online = if ($meta -and $null -ne $meta.allowed_online) { [bool]$meta.allowed_online } else { $true }
      }
    }
  }
  return @($items | Sort-Object Order, { $_.Manifest.id })
}

if ($NewHatId) {
  New-HatScaffold $NewHatId $NewHatName
  if (!$ImportSheet) { return }
}

if ($ImportSheet) {
  Import-HatsFromSheet
}

$defs = @(Get-HatDefinitions)
if ($defs.Count -eq 0) { throw "No enabled hats found in hats/*.png" }

$assetsDir = Join-Root "assets"
if (!(Test-Path -LiteralPath $assetsDir)) {
  New-Item -ItemType Directory -Path $assetsDir | Out-Null
}

$sheetPathOut = Join-Path $assetsDir "hats.png"
$sheet = New-Object System.Drawing.Bitmap -ArgumentList @(($defs.Count * $CellW), $CellH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
  $gfx = [System.Drawing.Graphics]::FromImage($sheet)
  try {
    $gfx.Clear([System.Drawing.Color]::Transparent)
    for ($i = 0; $i -lt $defs.Count; $i++) {
      $src = [System.Drawing.Bitmap]::FromFile($defs[$i].Png)
      try {
        $gfx.DrawImageUnscaled($src, $i * $CellW, 0)
      } finally {
        $src.Dispose()
      }
      $defs[$i].Manifest.sprite_index = $i
    }
  } finally {
    $gfx.Dispose()
  }
  Save-Png $sheet $sheetPathOut
} finally {
  $sheet.Dispose()
}

$sha = Get-Sha256Hex $sheetPathOut
if (!$Version) { $Version = Get-Date -Format "yyyy-MM-dd.HHmmss" }

$manifest = [ordered]@{
  schema = 1
  version = $Version
  assets = [ordered]@{
    hats = [ordered]@{
      url = "$BaseUrl/assets/hats.png"
      sha256 = $sha
      cell_w = $CellW
      cell_h = $CellH
    }
  }
  hats = @($defs | ForEach-Object { $_.Manifest })
}

$manifestPathOut = Join-Root "manifest.json"
Write-JsonObject $manifestPathOut $manifest

Write-Host "Wrote $manifestPathOut"
Write-Host "Wrote $sheetPathOut"
Write-Host "SHA256 $sha"
