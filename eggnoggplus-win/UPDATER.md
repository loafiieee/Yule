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
Every path segment ending in the updater-owned `.update-recovery` suffix is
forbidden in release manifests, and release paths must leave enough room for
that suffix.
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

An applying V2 journal is also rehashed as a complete set. If every original
release target already matches and no reserved quarantine exists, recovery
atomically promotes it to committed and completes forward. This covers a crash
after the last target move but before the commit write without rolling a mapped
framework DLL backward. Cleanup-only and reduced journals are never eligible;
reduced unresolved entries carry a non-authorizing digest sentinel so even a
matching subset cannot become a split-version commit.

Rollback does not try to delete a DLL that Windows may already have mapped.
Instead it renames the rejected target to `<target>.update-recovery`, restores
the `.old` file at the normal target path, and then tries to remove the
quarantine. If Windows keeps that renamed image alive, the updater retains both
the quarantine and an atomically rewritten cleanup-only journal, stops before
another check/install, and reports `recovery complete - restart`. Cleanup
entries use `had_original=0`, which older updater builds already understand.
The next clean launch removes the released quarantine, finalizes the journal,
and clears abandoned updater staging without demanding a second restart.

If one file is quarantined successfully while another file cannot yet be
recovered, the updater atomically publishes a reduced mixed journal: completed
entries are dropped, retained quarantines become cleanup-only entries, and only
the unresolved original entries remain. An older restored updater therefore
cleans the quarantine even if the other error persists, while keeping the
journal until that remaining file is safe.

For a running framework DLL, the updater currently attempts to rename the mapped
target to `.old` and install the staged file at the original path. It does not
unload itself or use a reboot/helper process. If Windows rejects that live-file
rename, apply fails and rolls back. If it succeeds, the process continues using
the already-mapped old code and the new DLL is loaded after exit/relaunch. The
actual loaded `SDL2.dll` case is not covered by the standalone test and still
requires release-machine verification.

The journal cannot make the direct-import bootstrap itself power-fail safe.
`eggnoggplus.exe` imports `SDL2.dll` before this updater can run, so interruption
between either pair of live-file renames can leave that pathname missing and
prevent automatic in-process recovery. Until a stable launcher/helper owns
those swaps, the conservative manual boundary is: close **every** game process;
only when the normal target is missing and the adjacent `.old` is a regular
file, restore `.old` to the missing pathname. Never overwrite an existing
target, and preserve the journal, staging directory, quarantine files, and log
so the next launch can verify and reconcile them.

Updater shutdown must run only from normal application code. It must never wait
for its worker from `DllMain`, where the Windows loader lock is held.

## Recovery messages

- `recovery complete - restart` means the interrupted transaction was verified
  and reconciled on disk, but this process may still be executing a different
  mapped image. Fully close every Eggnogg+ instance and launch again. Do not
  delete `.old`, `.update-recovery`, or the journal while a game process is open.
- `recovery blocked - see modframework.log` means the updater deliberately
  preserved the transaction because a journal, target, backup, path, or
  permission check could not be proven safe. Close all game instances and use
  **Retry** once. If it repeats, keep copies of `mods/update_staging` and the
  adjacent recovery files before repairing from a complete release package;
  the detailed non-destructive reason is in `mods/modframework.log`.

If a required DLL pathname is missing and the game cannot start far enough to
show either message, use only the closed-game, missing-target `.old` procedure
above. Do not delete the transaction evidence or copy over an existing target.

The updater never silently treats a missing backup or corrupt target as a
successful recovery, and it does not begin another installation while recovery
requires a restart.

## Verification

### Safe one-command test

From the repository folder, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
```

The script builds the updater test, starts a temporary HTTP server bound only to
`127.0.0.1`, and runs parsing, hashing, transactional apply, rollback/recovery,
complete-set roll-forward, simulated mapped-DLL single/multi-file restart
handoff, reduced-journal non-authorization, access-error retention, and a real
loopback download/install pass. The test redirects its installation root into
disposable `build\update_ext_*_tmp_*` folders and cleans them in `finally`, even
when the native test aborts. A final `PASS`
therefore does **not** replace the repository's live `SDL2.dll` or contact the
production update channel. The restart regression also consumes a cleanup-only
journal with the deletion semantics used by updater builds that predate the
quarantine feature, including a mixed case where quarantine cleanup succeeds
while a different original entry remains blocked across another retry.

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
a newer production release. It deterministically simulates the mapped-file
quarantine/relaunch state machine, but replacement of the real, currently
mapped `SDL2.dll`, production HTTPS, abrupt process termination, and two
simultaneously running game instances remain release-machine checks because a
standalone test cannot reproduce Windows' image-section locking behavior.

### Manual build command

The dependency-light regression executable is built from `update_ext.c` itself:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH
gcc -m32 -DUPDATE_EXT_TEST -Wall -Wextra -Werror -o build\update_test.exe update_ext.c -lwinhttp -lbcrypt -lws2_32
.\build\update_test.exe
```

Set `UPDATE_EXT_TEST_CHANNEL_BASE` to an HTTP loopback directory URL to include
the download, hash, transactional apply, and recovery integration case.
