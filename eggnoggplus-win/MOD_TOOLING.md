# Mod Tooling

`modtool` is the standalone packaging and install helper for Eggnogg+ mods.

On Windows, use the wrapper:

```powershell
.\modtool.cmd help
```

You can also call the script directly:

```powershell
powershell -ExecutionPolicy Bypass -File .\modtool.ps1 help
```

## Commands

### `validate`

Validate either a loose mod folder or a packaged mod zip:

```powershell
.\modtool.cmd validate .\mods\speedhack
.\modtool.cmd validate .\dist\poc_speedhack-uwu.eggnoggmod.zip
```

What it checks:

- `mod.json` parses as JSON
- required fields and types are present
- field lengths fit the runtime loader limits
- dependency specs are well-formed
- install paths are safe relative paths
- package archives use the expected metadata and content layout

## `pack`

Package a mod folder into a shareable zip:

```powershell
.\modtool.cmd pack .\mods\speedhack
.\modtool.cmd pack .\mods\speedhack --output .\dist\speedhack.zip
```

Behavior:

- validates the mod before packing
- writes a standard zip with `package.json` and a `mod/` content root
- excludes `storage` and `binds` files by default for safer sharing
- skips obvious local clutter like `.git`, `_modtool`, `*.log`, `*.tmp`, and `*.bak`

Optional flags:

- `--include-storage`
- `--include-binds`

## `install`

Install a packaged mod or a loose mod folder into `mods/`:

```powershell
.\modtool.cmd install .\dist\speedhack.zip
.\modtool.cmd install C:\temp\my_mod --mods-dir .\mods
```

Behavior:

- stages the input safely before touching the live `mods/` folder
- validates the mod/package before install
- writes install metadata under `mods\_modtool\installed`
- refuses to overwrite an existing install unless you use `--force`

## `update`

Update an already-installed mod from a package or loose folder:

```powershell
.\modtool.cmd update .\dist\speedhack.zip
```

Behavior:

- backs up the current installed folder before replacing it
- refuses semver-detected downgrades or same-version reinstalls unless you use `--force`
- restores the backup automatically if the copy step fails

## `uninstall`

Uninstall by mod id:

```powershell
.\modtool.cmd uninstall poc_speedhack
```

Behavior:

- moves the installed folder into `mods\_modtool\backups`
- removes the install metadata record
- does not hard-delete the mod immediately

## Package Format

Packages are regular zip files with this layout:

```text
package.json
mod/
  mod.json
  main.lua
  ...
```

`package.json` contains:

- `format`
- `package_version`
- `mod_id`
- `mod_version`
- `mod_name`
- `created_utc`
- `includes`

## Safety Notes

- The installer rejects path traversal and absolute paths inside packages.
- Packaging excludes user state (`storage`, `binds`) by default.
- Install/update/uninstall keep backups in `mods\_modtool\backups`.
- Runtime ignores `_modtool/`, so the bookkeeping directory will not be treated as a mod.
