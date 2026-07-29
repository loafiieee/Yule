[CmdletBinding()]
param(
    [Parameter()]
    [string]$Destination = (Join-Path $PSScriptRoot '..\..\yule-src-public'),

    [Parameter()]
    [switch]$WorkingTree
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destinationRoot = [IO.Path]::GetFullPath($Destination)
$sourcePrefix = (& git -C $sourceRoot rev-parse --show-prefix).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourcePrefix)) {
    throw 'Could not determine the project prefix inside the private repository.'
}
$sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'Could not determine the private source commit.'
}

$sourceWithSeparator = $sourceRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($destinationRoot.Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationRoot.StartsWith($sourceWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Destination must be outside the private project source tree.'
}
if (Test-Path -LiteralPath $destinationRoot) {
    throw "Destination already exists: $destinationRoot"
}

$allowedRootFiles = @(
    'BYTEBEAT.md',
    'CONSOLE.md',
    'DISCORD_LFG_BOT.md',
    'DISCORD_RICH_PRESENCE.md',
    'MAP_FORMAT.md',
    'MODDING.md',
    'ONLINE_MULTIPLAYER.md',
    'PUBLIC_SOURCE.md',
    'SDL2.def',
    'TESTING.md',
    'UPDATER.md',
    'compile.sh',
    'genstubs.py',
    'mod.schema.json'
)
$allowedRootExtensions = @('.c', '.h', '.s')
$allowedTrees = @(
    'docs/',
    'docs-site/',
    'installer/',
    'maps/',
    'mods/',
    'online_server/',
    'tests/',
    'third_party/',
    'tools/'
)
$deniedExact = @(
    'mods/console_history.txt',
    'mods/crash.log',
    'mods/desync_dump.log',
    'mods/modframework.cfg',
    'mods/modframework.log',
    'mods/online_hub.cfg',
    'mods/updater_launcher.log',
    'online_server/ratings.json',
    'online_server/server.err.log',
    'online_server/server.log',
    'online_server/server_secret.key',
    'online_server/users.json'
)
$deniedPrefixes = @(
    'build/',
    'dist/',
    'ghidra/',
    '__pycache__/',
    'mods/_official_cosmetics/',
    'tools/public_source_overlay/'
)
$deniedExtensions = @(
    '.a', '.dll', '.dmp', '.exe', '.lib', '.log', '.o', '.obj', '.pdb',
    '.pyc', '.tmp', '.zip'
)

function Normalize-RepoPath([string]$Path) {
    return $Path.Replace('\', '/').TrimStart('/')
}

function Get-RelativePathCompat([string]$BasePath, [string]$ChildPath) {
    $baseFull = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    $childFull = [IO.Path]::GetFullPath($ChildPath)
    $baseUri = [Uri]$baseFull
    $childUri = [Uri]$childFull
    return [Uri]::UnescapeDataString(
        $baseUri.MakeRelativeUri($childUri).ToString()
    ).Replace('/', [IO.Path]::DirectorySeparatorChar)
}

function Test-PublicPath([string]$Path) {
    $pathValue = Normalize-RepoPath $Path
    if ($deniedExact -contains $pathValue) { return $false }
    foreach ($prefix in $deniedPrefixes) {
        if ($pathValue.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }
    $extension = [IO.Path]::GetExtension($pathValue).ToLowerInvariant()
    if ($deniedExtensions -contains $extension) { return $false }
    if ($allowedRootFiles -contains $pathValue) { return $true }
    if (-not $pathValue.Contains('/') -and
        $allowedRootExtensions -contains $extension) {
        return $true
    }
    foreach ($tree in $allowedTrees) {
        if ($pathValue.StartsWith($tree, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

$tracked = @(& git -C $sourceRoot ls-files -- .)
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate tracked project files.' }
$selected = @($tracked | ForEach-Object { Normalize-RepoPath $_ } |
    Where-Object { Test-PublicPath $_ } | Sort-Object -Unique)
if ($WorkingTree) {
    # These release-control files may be under review before their first commit.
    # Every other working-tree file must already be tracked.
    foreach ($reviewPath in @('PUBLIC_SOURCE.md', 'tools/export_public_source.ps1')) {
        if ((Test-Path -LiteralPath (Join-Path $sourceRoot $reviewPath)) -and
            $selected -notcontains $reviewPath) {
            $selected += $reviewPath
        }
    }
    $selected = @($selected | Sort-Object -Unique)
}
if ($selected.Count -lt 50) {
    throw "Public export allowlist unexpectedly selected only $($selected.Count) files."
}

$stagingRoot = "$destinationRoot.staging-$PID"
if (Test-Path -LiteralPath $stagingRoot) {
    throw "Staging path unexpectedly exists: $stagingRoot"
}
[IO.Directory]::CreateDirectory($stagingRoot) | Out-Null

try {
    if ($WorkingTree) {
        foreach ($repoPath in $selected) {
            $sourcePath = [IO.Path]::GetFullPath((Join-Path $sourceRoot $repoPath))
            if (-not $sourcePath.StartsWith($sourceWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Selected source escaped the project root: $repoPath"
            }
            $outputPath = Join-Path $stagingRoot $repoPath
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $outputPath
        }
    } else {
        $archivePath = Join-Path ([IO.Path]::GetTempPath()) "yule-public-$PID.zip"
        try {
            # `git -C $sourceRoot` makes pathspecs relative to this project
            # directory even though the private repository root is one level
            # above it.
            $gitPaths = @($selected | ForEach-Object {
                $_.Replace('\', '/')
            })
            & git -C $sourceRoot archive --format=zip --output=$archivePath HEAD -- @gitPaths
            if ($LASTEXITCODE -ne 0) { throw 'git archive failed.' }
            Expand-Archive -LiteralPath $archivePath -DestinationPath $stagingRoot
        } finally {
            if (Test-Path -LiteralPath $archivePath) {
                Remove-Item -LiteralPath $archivePath
            }
        }
    }

    $overlayRoot = Join-Path $sourceRoot 'tools\public_source_overlay'
    Get-ChildItem -LiteralPath $overlayRoot -Recurse -File | ForEach-Object {
        $relative = Get-RelativePathCompat $overlayRoot $_.FullName
        $outputPath = Join-Path $stagingRoot $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $outputPath -Force
    }

    $snapshot = [ordered]@{
        source_commit = $sourceCommit
        exported_at_utc = [DateTime]::UtcNow.ToString('o')
        source_mode = if ($WorkingTree) { 'working-tree-review' } else { 'committed-head' }
        inherited_history = $false
    }
    [IO.File]::WriteAllText(
        (Join-Path $stagingRoot 'SOURCE_SNAPSHOT.json'),
        (($snapshot | ConvertTo-Json) + [Environment]::NewLine),
        (New-Object Text.UTF8Encoding($false))
    )

    $forbiddenPaths = @(
        'online_server/users.json',
        'online_server/ratings.json',
        'online_server/server_secret.key',
        'mods/modframework.cfg',
        'mods/online_hub.cfg'
    )
    foreach ($forbiddenPath in $forbiddenPaths) {
        if (Test-Path -LiteralPath (Join-Path $stagingRoot $forbiddenPath)) {
            throw "Forbidden runtime file reached public output: $forbiddenPath"
        }
    }

    $textExtensions = @(
        '.bat', '.c', '.cfg', '.css', '.def', '.h', '.html', '.js', '.json',
        '.lua', '.md', '.ps1', '.py', '.s', '.sh', '.txt'
    )
    $secretPatterns = @(
        '-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----',
        '(?im)^\s*(DISCORD_TOKEN|LFG_BOT_TOKEN|ADMIN_TOKEN|JWT_SECRET|SERVER_SECRET|CLIENT_SECRET)\s*=\s*(?!example|replace|change-me|<|\$\{)[^\s#]{8,}\s*$',
        '(?i)\bgh[opsu]_[A-Za-z0-9_]{30,}\b',
        '(?i)\bsk-[A-Za-z0-9]{20,}\b'
    )
    foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -Recurse -File) {
        if ($textExtensions -notcontains $file.Extension.ToLowerInvariant()) { continue }
        $text = Get-Content -LiteralPath $file.FullName -Raw
        foreach ($pattern in $secretPatterns) {
            if ($text -match $pattern) {
                $relative = Get-RelativePathCompat $stagingRoot $file.FullName
                throw "Potential secret matched in public output: $relative"
            }
        }
    }

    Move-Item -LiteralPath $stagingRoot -Destination $destinationRoot
    Write-Host "Public source export complete: $destinationRoot"
    Write-Host "Files: $((Get-ChildItem -LiteralPath $destinationRoot -Recurse -File).Count)"
    Write-Host "Source commit: $sourceCommit"
    if ($WorkingTree) {
        Write-Warning 'This is a working-tree review export. Publish a committed-head export.'
    }
} catch {
    if (Test-Path -LiteralPath $stagingRoot) {
        $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
        $expectedPrefix = $destinationRoot + '.staging-'
        if ($resolvedStaging.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
        }
    }
    throw
}
