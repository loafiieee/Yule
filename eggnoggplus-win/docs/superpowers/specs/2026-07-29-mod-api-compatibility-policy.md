# Mod API compatibility policy

## Compatibility model

The public Lua framework API is identified by:

- a **major**, changed only for an incompatible signature/behavior change or
  ordinary removal;
- an additive **revision**, increased when public contracts are added without
  breaking mods targeting the same major;
- stable **capability identifiers**, used for machine-readable feature
  discovery.

API 1 revisions must continue to load a valid legacy manifest containing only
`"api_version": 1`. Its omitted minimum revision is zero.

## Manifest contract

```json
{
  "api_version": 1,
  "api_revision": 1,
  "api_requires": ["content.tiles.v1", "fs.pick_file"]
}
```

- `api_version` is the required major.
- `api_revision` is the minimum additive revision and defaults to zero.
- `api_requires` is an optional unique array of no more than 32 identifiers.
- Identifiers are 1-63 bytes and match `^[a-z][a-z0-9._-]*$`.

The discovery scan checks the complete requirement before dependency ordering.
The loader repeats it immediately before the entry script as defense in depth.
A major mismatch, newer revision, malformed requirement, or unavailable
capability rejects without entry-script side effects.

`allow_api_mismatch` and `LUNA_ALLOW_API_MISMATCH` bypass any compatibility
failure with a warning. They are diagnostic/local-development escapes, never a
release strategy.

## Runtime contract

Every ordinary mod receives:

```lua
mod.api.major
mod.api.revision
mod.api.capabilities
mod.api.has(name)
mod.api.require(name)
```

`capabilities` is a sorted read-only-by-convention array. `has` returns false
for unknown or malformed names so an optional branch stays simple. `require`
returns true when present, false plus an explanation for a well-formed missing
identifier, and raises for malformed identifiers.

`mod.framework_api` remains the API-major compatibility alias.
`mod.framework_api_revision` exposes the additive revision for API-1 code that
prefers a flat field. `mod.info()` reports the manifest's required major and
revision.

The `framework.api` console command reports all three build-level values:
major, revision, and capability count.

## Evolution and deprecation

- Additive APIs increment the revision.
- A discoverable optional subsystem gets one stable capability identifier.
- Existing identifiers do not change meaning or disappear within a major.
- Deprecation documentation names the replacement.
- Deprecated members remain usable through the rest of the major.
- Ordinary removal or incompatible reuse requires the next major.
- An emergency security repair may turn unsafe behavior into a bounded failure
  within a major and must be documented in release notes.

## Verification

`tests/mod_api_test.c` exercises the production registry and compatibility
checker for legacy revision zero, current revision, future major/revision,
valid/missing/malformed capabilities, array bounds, sorted uniqueness, and
bounded diagnostics.

`tests/mod_api_integration_static_test.py` pins manifest parsing, scan/load
enforcement, Lua discovery registration, console diagnostics, schema, docs,
build linkage, and guarded test-runner inclusion. The docs audit independently
requires every exported callable to have a full reference entry.
