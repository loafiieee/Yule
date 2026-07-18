# Framework updater

The framework checks its release channel asynchronously after the first rendered
frame. Network or channel failures never block startup. The **Mods > Framework**
section shows the installed version, current check/install status, an automatic
updates toggle, and the applicable check/retry/install action. Automatic updates
default to on. When a newer version is found, a bounded launch notification is
shown in the top-right; it can be clicked away and expires after 12 seconds.

The framework version is `FRAMEWORK_VERSION` in `update_ext.h`. Release versions
are dot-separated decimal components and are compared without fixed-width integer
conversion, so components cannot overflow.

## Configuration

The updater preserves unrelated lines and comments in `mods/modframework.cfg`.
It owns these optional keys:

```ini
auto_update=1
update_channel_url=https://loafiieee.com/yule/releases/latest.json
```

If `auto_update` is absent it is treated as enabled. `update_channel_url` is a
development/release override; omit it to use the compiled default. HTTPS is
required, except for `localhost`, `127.0.0.1`, or `::1` test channels.

Framework code that needs to persist another setting can use
`update_ext_config_set(key, value)`. It uses the same installation-rooted,
cross-thread/cross-process serialized atomic rewrite. Keys are restricted to
1–63 ASCII bytes containing only letters, digits, `.`, `_`, and `-`; values are
limited to 4,096 bytes and may not contain CR or LF. It returns `1` after the
file replacement succeeds and `0` on validation or persistence failure. This is
a persistence helper only; callers remain responsible for updating their live
in-memory state (for example, use `update_ext_set_auto` for the updater toggle).

## Channel format

The channel document uses schema version 1:

```json
{
  "channel_version": 1,
  "version": "1.1.0",
  "base": "https://example.invalid/yule/releases/1.1.0/",
  "files": [
    {
      "path": "SDL2.dll",
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "size": 1234567,
      "overwrite": true
    }
  ]
}
```

`path`, `sha256`, and the exact byte `size` are required. `overwrite` defaults to
true. Paths are installation-relative and limited to URL-safe filename
characters. Absolute paths, traversal, alternate data streams, reserved Windows
device names, duplicate/colliding paths, and reparse-point escapes are rejected.
The manifest is limited to 32 files, 128 MiB per file, and 256 MiB total.

## Install guarantees

Every payload is downloaded to staging, checked for its declared size and
SHA-256 digest, and preflighted before any live file changes. Installation uses a
cross-process mutex, `.old` backups, write-through moves, and a transaction
journal. A failed swap is rolled back; an interrupted transaction is recovered on
the next check.

Journal V2 stores the expected installed size and SHA-256 for every target. A
committed transaction is fully rehashed before any backup is deleted. Valid
targets are kept; a missing/corrupt target rolls the complete set back when all
required backups exist. If an artifact is missing, the updater retains the
journal and remaining backups rather than guessing. Legacy V1 applying journals
can still roll back, while V1 committed journals fail safely because they lack
the digest needed to authorize backup deletion.

For a running framework DLL, the updater currently attempts to rename the mapped
target to `.old` and install the staged file at the original path. It does not
unload itself or use a reboot/helper process. If Windows rejects that live-file
rename, apply fails and rolls back. If it succeeds, the process continues using
the already-mapped old code and the new DLL is loaded after exit/relaunch. The
actual loaded `SDL2.dll` case is not covered by the standalone test and still
requires release-machine verification.

Updater shutdown must run only from normal application code. It must never wait
for its worker from `DllMain`, where the Windows loader lock is held.

## Verification

### Safe one-command test

From the repository folder, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
```

The script builds the updater test, starts a temporary HTTP server bound only to
`127.0.0.1`, and runs parsing, hashing, transactional apply, rollback/recovery,
and a real loopback download/install pass. The test redirects its installation
root into a disposable `build\update_ext_http_tmp_*` folder. A final `PASS`
therefore does **not** replace the repository's live `SDL2.dll` or contact the
production update channel.

The expected last lines are:

```text
ALL OK
PASS: updater tests completed in disposable build directories; the live SDL2.dll was not changed.
```

The temporary server and test directories are stopped/removed automatically,
including when a test fails.

### In-game status check

For the non-destructive UI portion, start Eggnogg+, open **Mods > Framework**,
and confirm the initial `checking` state resolves to either `up to date`,
`update available`, or a usable `Retry` action. Toggle automatic updates off and
back on, close and reopen the menu, and confirm the setting persists. `Check` or
`Retry` must not freeze animation or input. Do not press `Install` merely to
manufacture a test; installation is meaningful only when the configured
channel offers a newer, correctly hashed release.

The one-command test above is the safe way to exercise an actual apply without
a newer production release. Replacement of the real, currently mapped
`SDL2.dll`, production HTTPS, abrupt process termination, and two simultaneously
running game instances remain release-machine checks because a standalone test
cannot reproduce Windows' live module-locking behavior.

### Manual build command

The dependency-light regression executable is built from `update_ext.c` itself:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH
gcc -m32 -DUPDATE_EXT_TEST -Wall -Wextra -Werror -o build\update_test.exe update_ext.c -lwinhttp -lbcrypt -lws2_32
.\build\update_test.exe
```

Set `UPDATE_EXT_TEST_CHANNEL_BASE` to an HTTP loopback directory URL to include
the download, hash, transactional apply, and recovery integration case.
