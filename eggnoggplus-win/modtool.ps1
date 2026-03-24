[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Command,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

Set-StrictMode -Version 2
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$DefaultModsDir = Join-Path $ScriptRoot "mods"
$SchemaPath = Join-Path $ScriptRoot "mod.schema.json"
$PackageFormat = "eggnoggplus-mod-package"
$PackageVersion = 1
$ModListMax = 32

function Write-ToolInfo {
    param([string]$Message)
    Write-Host $Message
}

function Write-ToolError {
    param([string]$Message)
    [Console]::Error.WriteLine($Message)
}

function Fail {
    param(
        [string]$Message,
        [int]$Code = 1
    )
    Write-ToolError $Message
    exit $Code
}

function Show-Usage {
    @"
Eggnogg+ Mod Tooling

Usage:
  .\modtool.cmd validate <mod-folder|package.zip>
  .\modtool.cmd pack <mod-folder> [--output <package.zip>] [--include-storage] [--include-binds]
  .\modtool.cmd install <mod-folder|package.zip> [--mods-dir <path>] [--force]
  .\modtool.cmd update <mod-folder|package.zip> [--mods-dir <path>] [--force]
  .\modtool.cmd uninstall <mod-id> [--mods-dir <path>]

Notes:
  - Packages are standard zip files with package metadata and a mod/ content root.
  - install/update/uninstall keep backups under mods\_modtool\backups.
  - pack excludes storage/binds by default for safer sharing.
"@ | Write-Host
}

function Ensure-Directory {
    param([string]$PathText)
    if (-not (Test-Path -LiteralPath $PathText)) {
        New-Item -ItemType Directory -Path $PathText | Out-Null
    }
}

function Remove-PathIfExists {
    param([string]$PathText)
    if (Test-Path -LiteralPath $PathText) {
        Remove-Item -LiteralPath $PathText -Recurse -Force
    }
}

function Resolve-AbsolutePath {
    param(
        [string]$PathText,
        [string]$BaseDir = (Get-Location).Path
    )
    if ([string]::IsNullOrWhiteSpace($PathText)) {
        return $null
    }
    if ([IO.Path]::IsPathRooted($PathText)) {
        return [IO.Path]::GetFullPath($PathText)
    }
    return [IO.Path]::GetFullPath((Join-Path $BaseDir $PathText))
}

function Normalize-RelPath {
    param([string]$PathText)
    if ($null -eq $PathText) { return "" }
    return (($PathText -replace "/", "\") -replace "\\+", "\").Trim()
}

function Normalize-ComparePath {
    param([string]$PathText)
    return (Normalize-RelPath $PathText).ToLowerInvariant()
}

function Convert-ToForwardSlashPath {
    param([string]$PathText)
    return ((Normalize-RelPath $PathText) -replace "\\", "/")
}

function Convert-ToSafeFileName {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return "mod" }
    $safe = $Text
    foreach ($ch in [IO.Path]::GetInvalidFileNameChars()) {
        $safe = $safe.Replace([string]$ch, "_")
    }
    $safe = $safe.Replace(" ", "_")
    if ([string]::IsNullOrWhiteSpace($safe)) { return "mod" }
    return $safe
}

function Test-SafeRelativePath {
    param([string]$PathText)
    if ([string]::IsNullOrWhiteSpace($PathText)) { return $false }
    if ($PathText.StartsWith("\") -or $PathText.StartsWith("/")) { return $false }
    if ($PathText -match "^[A-Za-z]:") { return $false }
    $segments = ($PathText -split "[\\/]+")
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment)) { return $false }
        if ($segment -eq "." -or $segment -eq "..") { return $false }
    }
    return $true
}

function Test-SafeFolderName {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $false }
    if ($Text -eq "." -or $Text -eq "..") { return $false }
    return ($Text.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -lt 0)
}

function Get-ObjectMemberNames {
    param($Object)
    if ($null -eq $Object) { return @() }
    return @($Object.PSObject.Properties | ForEach-Object { $_.Name })
}

function Test-HasProperty {
    param($Object, [string]$Name)
    return ($null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name])
}

function Get-PropertyValue {
    param($Object, [string]$Name)
    if (-not (Test-HasProperty $Object $Name)) { return $null }
    return $Object.PSObject.Properties[$Name].Value
}

function Convert-ToIntOrNull {
    param($Value)
    if ($null -eq $Value) { return $null }
    if ($Value -is [byte] -or $Value -is [int16] -or $Value -is [int32] -or $Value -is [int64]) {
        if ($Value -lt [int]::MinValue -or $Value -gt [int]::MaxValue) { return $null }
        return [int]$Value
    }
    if ($Value -is [double] -or $Value -is [single] -or $Value -is [decimal]) {
        if ([math]::Floor([double]$Value) -ne [double]$Value) { return $null }
        if ($Value -lt [int]::MinValue -or $Value -gt [int]::MaxValue) { return $null }
        return [int]$Value
    }
    return $null
}

function Parse-SemVersion {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }

    $buf = $Text.Trim()
    if ($buf.StartsWith("v") -or $buf.StartsWith("V")) {
        $buf = $buf.Substring(1)
    }

    $plusIndex = $buf.IndexOf("+")
    if ($plusIndex -ge 0) {
        $buf = $buf.Substring(0, $plusIndex)
    }

    $prerelease = ""
    $hasPrerelease = $false
    $dashIndex = $buf.IndexOf("-")
    if ($dashIndex -ge 0) {
        $prerelease = $buf.Substring($dashIndex + 1).Trim()
        $buf = $buf.Substring(0, $dashIndex)
        if ($prerelease.Length -gt 0) {
            $hasPrerelease = $true
        }
    }

    $parts = $buf.Split(".")
    if ($parts.Count -lt 1 -or $parts.Count -gt 3) { return $null }

    $vals = @(0, 0, 0)
    for ($i = 0; $i -lt $parts.Count; $i++) {
        if ($parts[$i] -notmatch "^\d+$") { return $null }
        $vals[$i] = [int]$parts[$i]
    }

    return [ordered]@{
        major = $vals[0]
        minor = $vals[1]
        patch = $vals[2]
        has_prerelease = $hasPrerelease
        prerelease = $prerelease
    }
}

function Test-NumericToken {
    param([string]$Text)
    return (-not [string]::IsNullOrEmpty($Text) -and $Text -match "^\d+$")
}

function Compare-Prerelease {
    param(
        [string]$A,
        [string]$B
    )
    $partsA = @()
    $partsB = @()
    if (-not [string]::IsNullOrEmpty($A)) { $partsA = $A.Split(".") }
    if (-not [string]::IsNullOrEmpty($B)) { $partsB = $B.Split(".") }

    $i = 0
    while ($true) {
        $ta = if ($i -lt $partsA.Count) { $partsA[$i] } else { "" }
        $tb = if ($i -lt $partsB.Count) { $partsB[$i] } else { "" }
        if (-not $ta -and -not $tb) { return 0 }
        if (-not $ta) { return -1 }
        if (-not $tb) { return 1 }

        $aNum = Test-NumericToken $ta
        $bNum = Test-NumericToken $tb
        if ($aNum -and $bNum) {
            $av = [int64]$ta
            $bv = [int64]$tb
            if ($av -lt $bv) { return -1 }
            if ($av -gt $bv) { return 1 }
        } elseif ($aNum -and -not $bNum) {
            return -1
        } elseif (-not $aNum -and $bNum) {
            return 1
        } else {
            $cmp = [string]::CompareOrdinal($ta, $tb)
            if ($cmp -lt 0) { return -1 }
            if ($cmp -gt 0) { return 1 }
        }
        $i++
    }
}

function Compare-SemVersionObjects {
    param($A, $B)
    if ($A.major -ne $B.major) { return [Math]::Sign($A.major - $B.major) }
    if ($A.minor -ne $B.minor) { return [Math]::Sign($A.minor - $B.minor) }
    if ($A.patch -ne $B.patch) { return [Math]::Sign($A.patch - $B.patch) }
    if (-not $A.has_prerelease -and -not $B.has_prerelease) { return 0 }
    if ($A.has_prerelease -and -not $B.has_prerelease) { return -1 }
    if (-not $A.has_prerelease -and $B.has_prerelease) { return 1 }
    return Compare-Prerelease $A.prerelease $B.prerelease
}

function Test-SemVersionClauseSyntax {
    param([string]$Clause)
    if ([string]::IsNullOrWhiteSpace($Clause)) { return $false }
    $token = $Clause.Trim()
    if ($token -eq "*" -or $token -ieq "x") { return $true }

    $verText = $token
    if ($token.StartsWith(">=") -or $token.StartsWith("<=") -or $token.StartsWith("==") -or $token.StartsWith("!=")) {
        $verText = $token.Substring(2)
    } elseif ($token.StartsWith(">") -or $token.StartsWith("<") -or $token.StartsWith("=") -or $token.StartsWith("^") -or $token.StartsWith("~")) {
        $verText = $token.Substring(1)
    }

    return ($null -ne (Parse-SemVersion $verText))
}

function Test-SemVersionRangeSyntax {
    param([string]$Range)
    if ([string]::IsNullOrWhiteSpace($Range) -or $Range -eq "*") { return $true }
    $clauses = @($Range -split "[,\s]+" | Where-Object { $_ -and $_.Trim().Length -gt 0 })
    if ($clauses.Count -eq 0) { return $true }
    foreach ($clause in $clauses) {
        if (-not (Test-SemVersionClauseSyntax $clause)) {
            return $false
        }
    }
    return $true
}

function Split-DependencySpec {
    param([string]$Spec)
    if ([string]::IsNullOrWhiteSpace($Spec)) { return $null }
    $trimmed = $Spec.Trim()
    $atIndex = $trimmed.IndexOf("@")
    $idPart = $trimmed
    $rangePart = ""

    if ($atIndex -ge 0) {
        $idPart = $trimmed.Substring(0, $atIndex).Trim()
        $rangePart = $trimmed.Substring($atIndex + 1).Trim()
    } else {
        $match = [regex]::Match($trimmed, "[<>=!~^]")
        if ($match.Success) {
            $idPart = $trimmed.Substring(0, $match.Index).Trim()
            $rangePart = $trimmed.Substring($match.Index).Trim()
        }
    }

    if ([string]::IsNullOrWhiteSpace($idPart)) { return $null }
    return [ordered]@{
        id = $idPart
        range = $rangePart
    }
}

function Read-JsonText {
    param(
        [string]$JsonText,
        [string]$Label
    )
    try {
        return ($JsonText | ConvertFrom-Json)
    } catch {
        Fail "Failed to parse JSON from ${Label}: $($_.Exception.Message)"
    }
}

function Read-JsonFile {
    param([string]$PathText)
    if (-not (Test-Path -LiteralPath $PathText -PathType Leaf)) {
        Fail "JSON file not found: $PathText"
    }
    $jsonText = Get-Content -LiteralPath $PathText -Raw
    return Read-JsonText -JsonText $jsonText -Label $PathText
}

function Read-ZipEntryText {
    param([IO.Compression.ZipArchiveEntry]$Entry)
    $reader = New-Object IO.StreamReader($Entry.Open())
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
    }
}

function Test-ManifestObject {
    param(
        $ManifestObject,
        [string]$SourceRoot,
        [string]$SourceLabel
    )

    $result = [ordered]@{
        Errors = @()
        Warnings = @()
        Manifest = $null
    }

    if ($null -eq $ManifestObject) {
        $result.Errors += "manifest is null"
        return $result
    }

    $knownFields = @(
        "id", "name", "version", "author", "description", "entry",
        "config", "storage", "binds",
        "api_version", "allow_api_mismatch", "priority",
        "depends", "optional_deps", "conflicts", "load_before", "load_after"
    )

    foreach ($propName in (Get-ObjectMemberNames $ManifestObject)) {
        if ($knownFields -notcontains $propName) {
            $result.Warnings += "unknown manifest field '$propName' will be ignored by the runtime"
        }
    }

    function Get-RequiredStringValue {
        param([string]$Name, [int]$MaxLength)
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            $result.Errors += "missing required string field '$Name'"
            return $null
        }
        $value = Get-PropertyValue $ManifestObject $Name
        if (-not ($value -is [string])) {
            $result.Errors += "field '$Name' must be a string"
            return $null
        }
        if ([string]::IsNullOrWhiteSpace($value)) {
            $result.Errors += "field '$Name' must not be empty"
            return $null
        }
        if ($value.Length -gt $MaxLength) {
            $result.Errors += "field '$Name' is too long (max=$MaxLength)"
        }
        return $value
    }

    function Get-OptionalStringValue {
        param([string]$Name, [int]$MaxLength, [string]$DefaultValue)
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            return $DefaultValue
        }
        $value = Get-PropertyValue $ManifestObject $Name
        if (-not ($value -is [string])) {
            $result.Errors += "field '$Name' must be a string"
            return $DefaultValue
        }
        if ($value.Length -gt $MaxLength) {
            $result.Errors += "field '$Name' is too long (max=$MaxLength)"
        }
        return $value
    }

    function Get-OptionalIntValue {
        param([string]$Name, [int]$DefaultValue)
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            return $DefaultValue
        }
        $value = Convert-ToIntOrNull (Get-PropertyValue $ManifestObject $Name)
        if ($null -eq $value) {
            $result.Errors += "field '$Name' must be an integer"
            return $DefaultValue
        }
        return $value
    }

    function Get-OptionalBoolValue {
        param([string]$Name, [bool]$DefaultValue)
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            return $DefaultValue
        }
        $value = Get-PropertyValue $ManifestObject $Name
        if (-not ($value -is [bool])) {
            $result.Errors += "field '$Name' must be a boolean"
            return $DefaultValue
        }
        return [bool]$value
    }

    function Get-IdListValue {
        param([string]$Name)
        $values = @()
        $seen = @{}
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            return $values
        }
        $raw = Get-PropertyValue $ManifestObject $Name
        if ($null -eq $raw -or -not ($raw -is [System.Array])) {
            $result.Errors += "field '$Name' must be an array of strings"
            return $values
        }
        if ($raw.Count -gt $ModListMax) {
            $result.Errors += "field '$Name' has too many entries (max=$ModListMax)"
        }
        foreach ($item in $raw) {
            if (-not ($item -is [string])) {
                $result.Errors += "field '$Name' must contain only strings"
                continue
            }
            if ([string]::IsNullOrWhiteSpace($item)) {
                $result.Errors += "field '$Name' contains an empty mod id"
                continue
            }
            if ($item.Length -gt 63) {
                $result.Errors += "field '$Name' contains a mod id that is too long (max=63)"
                continue
            }
            $key = $item.ToLowerInvariant()
            if (-not $seen.ContainsKey($key)) {
                $seen[$key] = $true
                $values += $item
            }
        }
        return $values
    }

    function Get-DepListValue {
        param([string]$Name)
        $values = @()
        $seen = @{}
        if (-not (Test-HasProperty $ManifestObject $Name)) {
            return $values
        }
        $raw = Get-PropertyValue $ManifestObject $Name
        if ($null -eq $raw -or -not ($raw -is [System.Array])) {
            $result.Errors += "field '$Name' must be an array of dependency specs"
            return $values
        }
        if ($raw.Count -gt $ModListMax) {
            $result.Errors += "field '$Name' has too many entries (max=$ModListMax)"
        }
        foreach ($item in $raw) {
            if (-not ($item -is [string])) {
                $result.Errors += "field '$Name' must contain only dependency strings"
                continue
            }
            $split = Split-DependencySpec $item
            if ($null -eq $split) {
                $result.Errors += "field '$Name' contains an invalid dependency spec '$item'"
                continue
            }
            if ($split.id.Length -gt 63) {
                $result.Errors += "field '$Name' dependency id '$($split.id)' is too long (max=63)"
                continue
            }
            if ($split.range.Length -gt 95) {
                $result.Errors += "field '$Name' dependency range for '$($split.id)' is too long (max=95)"
                continue
            }
            if ($split.range.Length -gt 0 -and -not (Test-SemVersionRangeSyntax $split.range)) {
                $result.Errors += "field '$Name' contains an invalid version range for '$($split.id)': '$($split.range)'"
                continue
            }
            $key = $split.id.ToLowerInvariant()
            if ($seen.ContainsKey($key)) {
                if ($seen[$key] -ne $split.range) {
                    $result.Errors += "field '$Name' duplicates dependency '$($split.id)' with a different version range"
                }
                continue
            }
            $seen[$key] = $split.range
            $values += $item
        }
        return $values
    }

    $id = Get-RequiredStringValue "id" 63
    $name = Get-RequiredStringValue "name" 63
    $version = Get-RequiredStringValue "version" 31
    $author = Get-RequiredStringValue "author" 63
    $entry = Get-RequiredStringValue "entry" 127
    $description = Get-OptionalStringValue "description" 255 ""
    $config = Get-OptionalStringValue "config" 127 ""
    $storage = Get-OptionalStringValue "storage" 127 "storage.cfg"
    $binds = Get-OptionalStringValue "binds" 127 "binds.cfg"
    $apiVersion = Get-OptionalIntValue "api_version" 1
    $allowApiMismatch = Get-OptionalBoolValue "allow_api_mismatch" $false
    $priority = Get-OptionalIntValue "priority" 0
    $depends = Get-DepListValue "depends"
    $optionalDeps = Get-DepListValue "optional_deps"
    $conflicts = Get-IdListValue "conflicts"
    $loadBefore = Get-IdListValue "load_before"
    $loadAfter = Get-IdListValue "load_after"
    $hasConfigField = Test-HasProperty $ManifestObject "config"

    if ($id -and -not (Test-SafeFolderName $id)) {
        $result.Errors += "field 'id' contains characters that are unsafe for install folder names"
    }
    if ($version -and -not (Parse-SemVersion $version)) {
        $result.Warnings += "field 'version' is not semver; this is allowed, but dependency-range comparisons may not work as expected"
    }

    foreach ($pathField in @(
        @{ Name = "entry"; Value = $entry; Required = $true; WarnIfMissing = $false },
        @{ Name = "config"; Value = $config; Required = $false; WarnIfMissing = $hasConfigField },
        @{ Name = "storage"; Value = $storage; Required = $false; WarnIfMissing = $false },
        @{ Name = "binds"; Value = $binds; Required = $false; WarnIfMissing = $false }
    )) {
        if ([string]::IsNullOrWhiteSpace($pathField.Value)) { continue }
        if (-not (Test-SafeRelativePath $pathField.Value)) {
            $result.Errors += "field '$($pathField.Name)' must be a safe relative path inside the mod folder"
            continue
        }
        if ($SourceRoot) {
            $abs = Resolve-AbsolutePath -PathText $pathField.Value -BaseDir $SourceRoot
            $exists = Test-Path -LiteralPath $abs -PathType Leaf
            if ($pathField.Required -and -not $exists) {
                $result.Errors += "required file '$($pathField.Value)' referenced by '$($pathField.Name)' was not found"
            } elseif ($pathField.WarnIfMissing -and -not $exists) {
                $result.Warnings += "optional file '$($pathField.Value)' referenced by '$($pathField.Name)' was not found"
            }
        }
    }

    $result.Manifest = [ordered]@{
        id = $id
        name = $name
        version = $version
        author = $author
        description = $description
        entry = $entry
        config = $config
        storage = $storage
        binds = $binds
        api_version = $apiVersion
        allow_api_mismatch = [bool]$allowApiMismatch
        priority = $priority
        depends = $depends
        optional_deps = $optionalDeps
        conflicts = $conflicts
        load_before = $loadBefore
        load_after = $loadAfter
        source = $SourceLabel
    }

    return $result
}

function Show-ValidationResult {
    param(
        [string]$Label,
        $Validation
    )
    foreach ($warning in $Validation.Warnings) {
        Write-ToolInfo "warning: $warning"
    }
    foreach ($error in $Validation.Errors) {
        Write-ToolError "error: $error"
    }
    if ($Validation.Errors.Count -eq 0) {
        $manifest = $Validation.Manifest
        Write-ToolInfo "valid: $Label"
        Write-ToolInfo ("schema: {0}" -f $SchemaPath)
        Write-ToolInfo ("mod: {0} ({1}) version={2}" -f $manifest.name, $manifest.id, $manifest.version)
    }
}

function Validate-ModDirectory {
    param([string]$DirectoryPath)
    $fullDir = Resolve-AbsolutePath $DirectoryPath
    if (-not (Test-Path -LiteralPath $fullDir -PathType Container)) {
        Fail "Mod folder not found: $DirectoryPath"
    }

    $manifestPath = Join-Path $fullDir "mod.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Fail "mod.json not found in $fullDir"
    }

    $manifestObject = Read-JsonFile $manifestPath
    $validation = Test-ManifestObject -ManifestObject $manifestObject -SourceRoot $fullDir -SourceLabel $fullDir
    Show-ValidationResult -Label $fullDir -Validation $validation
    if ($validation.Errors.Count -gt 0) {
        exit 1
    }
    return $validation
}

function Get-ModToolStatePaths {
    param([string]$ModsDir)
    $root = Join-Path $ModsDir "_modtool"
    $backups = Join-Path $root "backups"
    $installed = Join-Path $root "installed"
    $tmp = Join-Path $root "tmp"
    Ensure-Directory $root
    Ensure-Directory $backups
    Ensure-Directory $installed
    Ensure-Directory $tmp
    return [ordered]@{
        root = $root
        backups = $backups
        installed = $installed
        tmp = $tmp
    }
}

function New-TempDirectory {
    param([string]$ParentDir)
    Ensure-Directory $ParentDir
    $path = Join-Path $ParentDir ([guid]::NewGuid().ToString("N"))
    Ensure-Directory $path
    return $path
}

function Extract-PackageToDirectory {
    param(
        [string]$PackagePath,
        [string]$DestinationDir
    )

    $zip = [IO.Compression.ZipFile]::OpenRead($PackagePath)
    $packageObject = $null
    $packageEntryFound = $false
    $manifestEntryFound = $false

    try {
        foreach ($entry in $zip.Entries) {
            $entryName = ($entry.FullName -replace "\\", "/")
            if ([string]::IsNullOrWhiteSpace($entryName)) { continue }
            if ($entryName.EndsWith("/")) { continue }

            if ($entryName -ieq "package.json") {
                $packageEntryFound = $true
                $packageObject = Read-JsonText -JsonText (Read-ZipEntryText $entry) -Label "${PackagePath}::package.json"
                continue
            }

            if (-not $entryName.StartsWith("mod/")) {
                Fail "Package contains an unsafe top-level entry: $entryName"
            }

            $rel = $entryName.Substring(4)
            if (-not (Test-SafeRelativePath $rel)) {
                Fail "Package contains an unsafe path: $entryName"
            }

            if ((Convert-ToForwardSlashPath $rel).ToLowerInvariant() -eq "mod.json") {
                $manifestEntryFound = $true
            }

            $destPath = Join-Path $DestinationDir (Normalize-RelPath $rel)
            $destParent = Split-Path -Parent $destPath
            if ($destParent) { Ensure-Directory $destParent }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destPath, $true)
        }
    } finally {
        $zip.Dispose()
    }

    if (-not $packageEntryFound) {
        Fail "Package is missing package.json"
    }
    if (-not $manifestEntryFound) {
        Fail "Package is missing mod/mod.json"
    }
    if (-not (Test-HasProperty $packageObject "format") -or (Get-PropertyValue $packageObject "format") -ne $PackageFormat) {
        Fail "Package format is invalid or unsupported"
    }
    $pkgVersion = Convert-ToIntOrNull (Get-PropertyValue $packageObject "package_version")
    if ($null -eq $pkgVersion -or $pkgVersion -ne $PackageVersion) {
        Fail "Package version is invalid or unsupported"
    }

    return $packageObject
}

function Validate-PackageFile {
    param([string]$PackagePath)
    $fullPath = Resolve-AbsolutePath $PackagePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        Fail "Package not found: $PackagePath"
    }

    $state = Get-ModToolStatePaths $DefaultModsDir
    $stageDir = New-TempDirectory $state.tmp
    try {
        $packageObject = Extract-PackageToDirectory -PackagePath $fullPath -DestinationDir $stageDir
        $validation = Validate-ModDirectory $stageDir
        if ($validation.Errors.Count -eq 0) {
            if ((Get-PropertyValue $packageObject "mod_id") -ne $validation.Manifest.id) {
                Fail "Package metadata mod_id does not match mod/mod.json id"
            }
            if ((Get-PropertyValue $packageObject "mod_version") -ne $validation.Manifest.version) {
                Fail "Package metadata mod_version does not match mod/mod.json version"
            }
            Write-ToolInfo ("package: format={0} package_version={1}" -f (Get-PropertyValue $packageObject "format"), (Get-PropertyValue $packageObject "package_version"))
        }
    } finally {
        Remove-PathIfExists $stageDir
    }
}

function Copy-DirectoryContents {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )
    Ensure-Directory $DestinationDir
    $items = Get-ChildItem -LiteralPath $SourceDir -Force
    foreach ($item in $items) {
        $target = Join-Path $DestinationDir $item.Name
        if ($item.PSIsContainer) {
            Copy-Item -LiteralPath $item.FullName -Destination $target -Recurse -Force
        } else {
            $parent = Split-Path -Parent $target
            if ($parent) { Ensure-Directory $parent }
            Copy-Item -LiteralPath $item.FullName -Destination $target -Force
        }
    }
}

function New-BackupPath {
    param(
        [string]$BackupsDir,
        [string]$ModId,
        [string]$Suffix = ""
    )
    $stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss")
    $safeId = Convert-ToSafeFileName $ModId
    if ($Suffix) {
        return (Join-Path $BackupsDir ("{0}-{1}-{2}" -f $stamp, $safeId, $Suffix))
    }
    return (Join-Path $BackupsDir ("{0}-{1}" -f $stamp, $safeId))
}

function Write-InstallRecord {
    param(
        [string]$InstalledDir,
        [string]$ModsDir,
        $Manifest,
        [string]$SourceLabel,
        [string]$Action,
        [string]$BackupPath
    )
    $recordPath = Join-Path $InstalledDir ("{0}.json" -f (Convert-ToSafeFileName $Manifest.id))
    $record = [ordered]@{
        action = $Action
        source = $SourceLabel
        installed_at_utc = (Get-Date).ToUniversalTime().ToString("o")
        install_path = (Join-Path $ModsDir $Manifest.id)
        backup_path = $BackupPath
        manifest = $Manifest
    }
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath -Encoding UTF8
}

function Remove-InstallRecord {
    param(
        [string]$InstalledDir,
        [string]$ModId
    )
    $recordPath = Join-Path $InstalledDir ("{0}.json" -f (Convert-ToSafeFileName $ModId))
    if (Test-Path -LiteralPath $recordPath) {
        Remove-Item -LiteralPath $recordPath -Force
    }
}

function Compare-VersionTexts {
    param(
        [string]$A,
        [string]$B
    )
    $va = Parse-SemVersion $A
    $vb = Parse-SemVersion $B
    if ($null -eq $va -or $null -eq $vb) { return $null }
    return (Compare-SemVersionObjects $va $vb)
}

function Stage-InputSource {
    param(
        [string]$SourcePath,
        [string]$ModsDir
    )

    $fullSource = Resolve-AbsolutePath $SourcePath
    if (-not (Test-Path -LiteralPath $fullSource)) {
        Fail "Source path not found: $SourcePath"
    }

    $state = Get-ModToolStatePaths $ModsDir
    $stageDir = New-TempDirectory $state.tmp
    $sourceType = if (Test-Path -LiteralPath $fullSource -PathType Container) { "directory" } else { "package" }
    $packageMeta = $null

    try {
        if ($sourceType -eq "directory") {
            Copy-DirectoryContents -SourceDir $fullSource -DestinationDir $stageDir
        } else {
            $packageMeta = Extract-PackageToDirectory -PackagePath $fullSource -DestinationDir $stageDir
        }

        $validation = Validate-ModDirectory $stageDir
        if ($packageMeta) {
            if ((Get-PropertyValue $packageMeta "mod_id") -ne $validation.Manifest.id) {
                Fail "Package metadata mod_id does not match mod/mod.json id"
            }
            if ((Get-PropertyValue $packageMeta "mod_version") -ne $validation.Manifest.version) {
                Fail "Package metadata mod_version does not match mod/mod.json version"
            }
        }

        return [ordered]@{
            stage_dir = $stageDir
            source_type = $sourceType
            source_label = $fullSource
            manifest = $validation.Manifest
            warnings = $validation.Warnings
            package = $packageMeta
        }
    } catch {
        Remove-PathIfExists $stageDir
        throw
    }
}

function Install-StagedMod {
    param(
        $Stage,
        [string]$ModsDir,
        [string]$Action,
        [bool]$Force
    )

    $state = Get-ModToolStatePaths $ModsDir
    $manifest = $Stage.manifest
    $destDir = Join-Path $ModsDir $manifest.id
    $backupPath = $null
    $existingManifest = $null
    $destExists = Test-Path -LiteralPath $destDir -PathType Container

    if ($Action -eq "install" -and $destExists -and -not $Force) {
        Fail "Mod '$($manifest.id)' is already installed. Use update or pass --force."
    }
    if ($Action -eq "update" -and -not $destExists -and -not $Force) {
        Fail "Mod '$($manifest.id)' is not installed yet. Use install or pass --force."
    }

    if ($destExists -and (Test-Path -LiteralPath (Join-Path $destDir "mod.json") -PathType Leaf)) {
        $existingValidation = Test-ManifestObject -ManifestObject (Read-JsonFile (Join-Path $destDir "mod.json")) -SourceRoot $destDir -SourceLabel $destDir
        if ($existingValidation.Errors.Count -eq 0) {
            $existingManifest = $existingValidation.Manifest
        }
    }

    if ($existingManifest) {
        $cmp = Compare-VersionTexts $manifest.version $existingManifest.version
        if ($Action -eq "update" -and -not $Force -and $null -ne $cmp -and $cmp -lt 0) {
            Fail "Refusing to downgrade $($manifest.id) from $($existingManifest.version) to $($manifest.version) without --force."
        }
        if ($Action -eq "update" -and -not $Force -and $null -ne $cmp -and $cmp -eq 0) {
            Fail "Version $($manifest.version) of $($manifest.id) is already installed. Use --force to reinstall."
        }
    }

    if ($destExists) {
        $backupPath = New-BackupPath -BackupsDir $state.backups -ModId $manifest.id -Suffix $Action
        Move-Item -LiteralPath $destDir -Destination $backupPath
    }

    try {
        Copy-DirectoryContents -SourceDir $Stage.stage_dir -DestinationDir $destDir
        Write-InstallRecord -InstalledDir $state.installed -ModsDir $ModsDir -Manifest $manifest -SourceLabel $Stage.source_label -Action $Action -BackupPath $backupPath
    } catch {
        Remove-PathIfExists $destDir
        if ($backupPath -and (Test-Path -LiteralPath $backupPath)) {
            Move-Item -LiteralPath $backupPath -Destination $destDir
        }
        throw
    }

    Write-ToolInfo ("{0}: {1} ({2}) -> {3}" -f $Action, $manifest.name, $manifest.id, $destDir)
    if ($backupPath) {
        Write-ToolInfo ("backup: {0}" -f $backupPath)
    }
}

function Uninstall-Mod {
    param(
        [string]$ModId,
        [string]$ModsDir
    )

    if ([string]::IsNullOrWhiteSpace($ModId)) {
        Fail "Usage: modtool uninstall <mod-id> [--mods-dir <path>]"
    }
    $destDir = Join-Path $ModsDir $ModId
    if (-not (Test-Path -LiteralPath $destDir -PathType Container)) {
        Fail "Installed mod not found: $ModId"
    }

    $state = Get-ModToolStatePaths $ModsDir
    $backupPath = New-BackupPath -BackupsDir $state.backups -ModId $ModId -Suffix "uninstall"
    Move-Item -LiteralPath $destDir -Destination $backupPath
    Remove-InstallRecord -InstalledDir $state.installed -ModId $ModId
    Write-ToolInfo ("uninstall: {0} -> backup {1}" -f $ModId, $backupPath)
}

function Should-IncludePackedFile {
    param(
        [string]$RelativePath,
        $Manifest,
        [bool]$IncludeStorage,
        [bool]$IncludeBinds,
        [string]$OutputPath
    )

    $norm = Normalize-ComparePath $RelativePath
    $name = [IO.Path]::GetFileName($RelativePath).ToLowerInvariant()

    if ($OutputPath) {
        $outputLeaf = [IO.Path]::GetFileName($OutputPath).ToLowerInvariant()
        if ($name -eq $outputLeaf) { return $false }
    }
    if ($norm.StartsWith(".git\")) { return $false }
    if ($norm.StartsWith("_modtool\")) { return $false }
    if ($name -eq "thumbs.db" -or $name -eq "desktop.ini") { return $false }
    if ($name.EndsWith(".tmp") -or $name.EndsWith(".bak") -or $name.EndsWith(".log")) { return $false }
    if (-not $IncludeStorage -and $norm -eq (Normalize-ComparePath $Manifest.storage)) { return $false }
    if (-not $IncludeBinds -and $norm -eq (Normalize-ComparePath $Manifest.binds)) { return $false }
    return $true
}

function Add-StringZipEntry {
    param(
        [IO.Compression.ZipArchive]$Zip,
        [string]$EntryName,
        [string]$Content
    )
    $entry = $Zip.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Optimal)
    $writer = New-Object IO.StreamWriter($entry.Open())
    try {
        $writer.Write($Content)
    } finally {
        $writer.Dispose()
    }
}

function Add-FileZipEntry {
    param(
        [IO.Compression.ZipArchive]$Zip,
        [string]$EntryName,
        [string]$SourceFile
    )
    $entry = $Zip.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Optimal)
    $input = [IO.File]::OpenRead($SourceFile)
    $output = $entry.Open()
    try {
        $input.CopyTo($output)
    } finally {
        $output.Dispose()
        $input.Dispose()
    }
}

function Pack-ModDirectory {
    param(
        [string]$ModDir,
        [string]$OutputPath,
        [bool]$IncludeStorage,
        [bool]$IncludeBinds
    )

    $validation = Validate-ModDirectory $ModDir
    $manifest = $validation.Manifest
    $fullDir = Resolve-AbsolutePath $ModDir

    if (-not $OutputPath) {
        $distDir = Join-Path $ScriptRoot "dist"
        Ensure-Directory $distDir
        $fileName = "{0}-{1}.eggnoggmod.zip" -f (Convert-ToSafeFileName $manifest.id), (Convert-ToSafeFileName $manifest.version)
        $OutputPath = Join-Path $distDir $fileName
    } else {
        $OutputPath = Resolve-AbsolutePath $OutputPath
        $outputParent = Split-Path -Parent $OutputPath
        if ($outputParent) { Ensure-Directory $outputParent }
    }

    $allFiles = Get-ChildItem -LiteralPath $fullDir -Recurse -File -Force
    $includedFiles = @()
    $excludedFiles = @()
    foreach ($file in $allFiles) {
        $rel = $file.FullName.Substring($fullDir.Length).TrimStart('\', '/')
        if (Should-IncludePackedFile -RelativePath $rel -Manifest $manifest -IncludeStorage $IncludeStorage -IncludeBinds $IncludeBinds -OutputPath $OutputPath) {
            $includedFiles += [ordered]@{ rel = $rel; full = $file.FullName }
        } else {
            $excludedFiles += $rel
        }
    }

    if ($includedFiles.Count -eq 0) {
        Fail "No files were eligible for packaging from $fullDir"
    }

    if (Test-Path -LiteralPath $OutputPath) {
        Remove-Item -LiteralPath $OutputPath -Force
    }

    $packageMeta = [ordered]@{
        format = $PackageFormat
        package_version = $PackageVersion
        mod_id = $manifest.id
        mod_version = $manifest.version
        mod_name = $manifest.name
        created_utc = (Get-Date).ToUniversalTime().ToString("o")
        includes = [ordered]@{
            config = [bool](-not [string]::IsNullOrWhiteSpace($manifest.config))
            storage = [bool]$IncludeStorage
            binds = [bool]$IncludeBinds
        }
    }

    $fs = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $zip = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
    try {
        Add-StringZipEntry -Zip $zip -EntryName "package.json" -Content ($packageMeta | ConvertTo-Json -Depth 8)
        foreach ($fileInfo in $includedFiles) {
            $entryName = "mod/" + (Convert-ToForwardSlashPath $fileInfo.rel)
            Add-FileZipEntry -Zip $zip -EntryName $entryName -SourceFile $fileInfo.full
        }
    } finally {
        $zip.Dispose()
        $fs.Dispose()
    }

    Write-ToolInfo ("pack: {0} -> {1}" -f $manifest.id, $OutputPath)
    Write-ToolInfo ("files: included={0} excluded={1}" -f $includedFiles.Count, $excludedFiles.Count)
    if (-not $IncludeStorage) {
        Write-ToolInfo "note: storage was excluded by default"
    }
    if (-not $IncludeBinds) {
        Write-ToolInfo "note: binds were excluded by default"
    }
}

function Parse-Options {
    param([string[]]$Rest)
    $options = [ordered]@{
        positionals = @()
        mods_dir = $DefaultModsDir
        output = $null
        include_storage = $false
        include_binds = $false
        force = $false
    }

    if ($null -eq $Rest) {
        return $options
    }

    $i = 0
    while ($i -lt $Rest.Count) {
        $arg = $Rest[$i]
        switch -Regex ($arg) {
            "^--mods-dir$" {
                $i++
                if ($i -ge $Rest.Count) { Fail "--mods-dir requires a path" }
                $options.mods_dir = Resolve-AbsolutePath $Rest[$i]
            }
            "^--output$" {
                $i++
                if ($i -ge $Rest.Count) { Fail "--output requires a path" }
                $options.output = $Rest[$i]
            }
            "^--include-storage$" { $options.include_storage = $true }
            "^--include-binds$" { $options.include_binds = $true }
            "^--force$" { $options.force = $true }
            default { $options.positionals += $arg }
        }
        $i++
    }

    return $options
}

$parsed = Parse-Options $Arguments

switch ($Command.ToLowerInvariant()) {
    "help" {
        Show-Usage
        exit 0
    }
    "validate" {
        if ($parsed.positionals.Count -ne 1) {
            Show-Usage
            exit 1
        }
        $target = $parsed.positionals[0]
        if (Test-Path -LiteralPath $target -PathType Container) {
            [void](Validate-ModDirectory $target)
        } else {
            Validate-PackageFile $target
        }
    }
    "pack" {
        if ($parsed.positionals.Count -ne 1) {
            Show-Usage
            exit 1
        }
        Pack-ModDirectory -ModDir $parsed.positionals[0] -OutputPath $parsed.output -IncludeStorage $parsed.include_storage -IncludeBinds $parsed.include_binds
    }
    "install" {
        if ($parsed.positionals.Count -ne 1) {
            Show-Usage
            exit 1
        }
        $stage = $null
        try {
            $stage = Stage-InputSource -SourcePath $parsed.positionals[0] -ModsDir $parsed.mods_dir
            Install-StagedMod -Stage $stage -ModsDir $parsed.mods_dir -Action "install" -Force $parsed.force
        } finally {
            if ($stage -and $stage.stage_dir) {
                Remove-PathIfExists $stage.stage_dir
            }
        }
    }
    "update" {
        if ($parsed.positionals.Count -ne 1) {
            Show-Usage
            exit 1
        }
        $stage = $null
        try {
            $stage = Stage-InputSource -SourcePath $parsed.positionals[0] -ModsDir $parsed.mods_dir
            Install-StagedMod -Stage $stage -ModsDir $parsed.mods_dir -Action "update" -Force $parsed.force
        } finally {
            if ($stage -and $stage.stage_dir) {
                Remove-PathIfExists $stage.stage_dir
            }
        }
    }
    "uninstall" {
        if ($parsed.positionals.Count -ne 1) {
            Show-Usage
            exit 1
        }
        Uninstall-Mod -ModId $parsed.positionals[0] -ModsDir $parsed.mods_dir
    }
    default {
        Show-Usage
        exit 1
    }
}
