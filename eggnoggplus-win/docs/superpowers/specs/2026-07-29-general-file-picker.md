# General file picker

## Goal

Replace the character-package-specific public picker with one general,
owner-aware API that lets a mod describe the file types a user may select
without accepting unbounded strings into a Win32 common-dialog buffer.

## Public contract

```lua
local path, err = mod.fs.pick_file({
  title = "Choose an image",
  filters = {
    { name = "PNG images (*.png)", patterns = { "*.png" } },
    { name = "Images", patterns = { "*.png", "*.jpg", "*.webp" } }
  },
  allow_all = false,
  filter_index = 1
})
```

- `options` is required.
- `title` defaults to `Select file` and is limited to 512 UTF-8 bytes.
- `filters` is optional and has at most 16 entries.
- A filter requires a nonempty display `name` of at most 160 UTF-8 bytes and
  1-16 `patterns`, each at most 96 bytes.
- With no filters, the framework supplies `All files (*.*)`.
- `allow_all = true` appends that entry to a nonempty filter list.
- `filter_index` is a one-based initial selection and must refer to a resulting
  filter.
- Success returns one absolute UTF-8 path. User cancellation returns
  `nil, "cancelled"`. Other common-dialog failures return `nil` plus a bounded
  diagnostic.

The legacy `mod.fs.pick_character_file([title])` remains callable for API-1
mods and delegates to the same Unicode dialog path. It is deprecated and is not
the primary documentation example.

The adjacent `mod.fs.pick_folder([title])` picker is also an owner-bound
wide-character dialog with strict UTF-8 title/result conversion. Recursive
`find_file` traversal remains depth-bounded and skips directory reparse points
so a selected tree cannot escape through a junction or cycle indefinitely.

## Safety and ownership

`pick_file` and the compatibility wrapper are owner-bound closures. A retained
closure fails if its owning mod is disabled. They are presentation/user-action
APIs and do not classify a mod as gameplay-affecting.

Lua strings are checked for embedded NULs and strict UTF-8 before conversion.
Patterns reject controls, path separators, drive separators, semicolons, and
Windows shell/file metacharacters. The native filter is assembled into a
zeroed 4096-wide-character buffer with a checked append for every label and
pattern group. The returned path uses a 32768-wide-character buffer and is
converted back to UTF-8.

The dialog uses `GetOpenFileNameW` with Explorer, existing-file,
existing-path, and no-current-directory-change flags. It is synchronous and
modal, so documentation requires an explicit user action and forbids calling
it every frame or tick.

## Verification

`tests/lua_fs_picker_static_test.py`, included in the guarded core runner,
checks:

- public registration is an owner-bound closure;
- count/buffer limits and pattern validation remain present;
- the wide-character common dialog and strict UTF conversions are used;
- cancellation is distinguished from `CommDlgExtendedError`;
- the compatibility wrapper remains exported;
- the general API and deprecation guidance remain documented.

Live acceptance: trigger the API from a test mod, select a file under a Unicode
and legacy-long path, verify each filter, cancel once, then disable/reload the
owner and confirm a retained closure cannot open the dialog.
