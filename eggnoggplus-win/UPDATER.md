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

## Publishing a release

Use this order for every release. Because the former `1.1` channel was briefly public,
that version is retired and must not be reused; use `1.2` for the next release:

1. Change `FRAMEWORK_VERSION` in `update_ext.h` to `"1.2"`.
2. Close Eggnogg+ and `YuleUpdater.exe`.
3. Run `bash compile.sh`.
4. Build the release:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\build_release.ps1 -Version 1.2 -Notes "Short release notes"
   ```

5. Upload the complete `dist/releases/1.2/` directory to a temporary directory
   on the server. Verify that each uploaded file's size and SHA-256 matches
   `dist/releases/latest.json`, then rename the temporary directory to `1.2`.
6. Upload `dist/releases/latest.json` to `latest.json.new`, validate its JSON,
   and atomically rename it to `latest.json` **last**.
7. Fetch the public `latest.json` and every public payload URL once to confirm
   the version, size, and digest before enabling an in-game install.

Publishing the payload directory before the channel document is mandatory. A
client can safely ignore an unadvertised directory; it cannot safely install a
manifest whose files are absent or whose DLL reports an older version.

`tools/build_release.ps1` fails before writing release output unless all three
versions match exactly: its `-Version`, `FRAMEWORK_VERSION` in `update_ext.h`,
and the unique version marker embedded in the deployed `SDL2.dll`. A verified
`build/SDL2_test.dll` must also exist and its SHA-256 must match the deployed DLL
exactly. The deployed `YuleUpdater.exe` must likewise match
`build/YuleUpdater.exe`. The publisher never replaces either deployed file.

Do not work around a version mismatch by editing only `latest.json`. If an
incorrect release was advertised, restore the previous valid `latest.json`
first, remove the bad version directory only after it is no longer advertised,
then rebuild the new version using the sequence above.

## Install guarantees

Every payload is downloaded to staging, checked for its declared size and
SHA-256 digest, and preflighted without renaming a live framework DLL. The
in-process updater records a durable Journal V3 transaction and reports
`verified update ready - restart`.

`YuleUpdater.exe` is installed beside the game and deliberately excluded from
`latest.json`, so an update never replaces the helper responsible for recovering
it. It is not the normal game entry point: Start Menu, Steam, and `yule://` all
launch `eggnoggplus.exe` directly.

The helper is statically linked against the MinGW runtime and release packaging
rejects any helper that imports `lua51.dll`, `libgcc_s_dw2-1.dll`,
`libwinpthread-1.dll`, or `SDL2_mixer.dll`. This is required: a helper may not
map a DLL that its own transaction renames and then attempts to delete.

When a verified transaction reaches `UPDATE_RESTART_PENDING`, the framework waits
until Eggnogg is in a safe menu rather than interrupting gameplay or an online match.
It then starts `YuleUpdater.exe` with the exact current process ID. The helper opens
that process before requesting `WM_CLOSE`, waits up to two minutes for orderly exit,
and never force-terminates it. It then enumerates processes and refuses to touch disk if
another process is running the exact same installed `eggnoggplus.exe`; the transaction
remains staged until every same-install instance is closed. Only after those checks does
it acquire the installation mutex and apply the pristine transaction or reconcile an
interrupted one.
It then relaunches `eggnoggplus.exe` with ordinary non-ephemeral game arguments and
exits. Online/deep-link intents already consumed by the old process are not replayed.

The helper is not required to start or use the DLL proxy, music engine, mods, online
hub, shortcuts, Steam entry, or deep links. It exists solely because a running process
cannot safely replace its mapped `SDL2.dll`. If it is missing or unsafe, the game stays
open, the verified transaction remains staged, and the framework retries the handoff
without touching installed files.

Journal V3 stores the expected replacement size/SHA-256 and the original target
size/SHA-256 (or explicit absence) for every file. During handoff the updater
rehashes both sides, rejects reparse points and changed originals, then uses
write-through target-to-`.old` and staged-to-target moves. It rehashes the whole
installed set before committing and deleting backups. Any partial swap rolls the
whole transaction back; incomplete or ambiguous recovery preserves the evidence
and blocks launch instead of guessing.

Legacy V1/V2, reduced cleanup, `.update-recovery`, and `.old` transactions remain
recovery-compatible. A complete applying V2/V3 set can finish forward; a corrupt
committed set rolls back when its complete backups exist. With no journal, a
missing normal target may consume its adjacent regular `.old` backup before
launch. This automatic closed-game recovery replaces the former manual
missing-target procedure.

The updater verifies that `eggnoggplus.exe` and `SDL2.dll` are regular
non-reparse files, forwards the original argument boundaries without invoking a
shell, and uses the installation root as the working directory. It logs only a
timestamp, result class, and bounded recovery status to
`mods/updater.log`; command-line values are never logged.

Opening `eggnoggplus.exe` directly is the supported path. A pristine transaction
found on boot remains staged until the framework reaches a safe menu and performs
the same one-shot handoff.

Updater shutdown must run only from normal application code. It must never wait
for its worker from `DllMain`, where the Windows loader lock is held.

## Recovery messages

- `verified update ready - restart` means downloads and the V3 transaction are
  durable. Return to a safe menu; the framework will close, update, and relaunch.
- An updater recovery dialog means it deliberately preserved the transaction
  because a journal, target, backup, path, or permission check could not be
  proven safe. Do not delete `.old`, `.update-recovery`, or
  `mods/update_staging`; inspect `mods/updater.log` and retain the
  evidence before repairing from a complete release package.

The updater never silently treats a missing backup or corrupt target as success,
and it does not stage another installation while a transaction remains pending.

## Verification

### Safe one-command test

From the repository folder, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
powershell -ExecutionPolicy Bypass -File .\tests\run_update_helper_test.ps1
```

The first script builds the updater test, starts a temporary HTTP server bound only to
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

The second guarded script builds the self-contained one-shot helper, rejects imports of
every replaceable release DLL, and uses a benign child
fixture. It exercises power-cut states in disposable roots and byte-exact
argument forwarding without starting the game. It also proves that a second process
running the exact target executable blocks disk changes. A final `PASS` confirms pristine,
partial, complete-before-commit, corrupt-commit, changed-original, corrupt
staging/journal, and legacy backup recovery behavior.

### In-game status check

For the non-destructive UI portion, start Eggnogg+, open **Mods > Framework**,
and confirm the initial `checking` state resolves to either `up to date`,
`update available`, or a usable `Retry` action. Toggle automatic updates off and
back on, close and reopen the menu, and confirm the setting persists. `Check` or
`Retry` must not freeze animation or input. Do not press `Install` merely to
manufacture a test; installation is meaningful only when the configured
channel offers a newer, correctly hashed release.

The two guarded tests above are the safe way to exercise staging and pre-import
apply without
a newer production release. It deterministically simulates the mapped-file
quarantine/relaunch state machine plus helper-owned swaps, but production HTTPS,
abrupt machine/process termination, and two simultaneously running instances
remain release-machine checks.

### Manual build command

The dependency-light regression executable is built from `update_ext.c` itself:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH
gcc -m32 -DUPDATE_EXT_TEST -Wall -Wextra -Werror -o build\update_test.exe update_ext.c -lwinhttp -lbcrypt -lws2_32
.\build\update_test.exe
```

Set `UPDATE_EXT_TEST_CHANNEL_BASE` to an HTTP loopback directory URL to include
the download, hash, transactional apply, and recovery integration case.
