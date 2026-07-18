# Asynchronous Framework Version Check and Transactional Updater

**Date:** 2026-07-17  
**Status:** Implemented; production-channel and locked-file behavior still require release-machine verification  
**Primary code:** `update_ext.c/.h`, updater UI in `hooks.c`, pre-swap dispatch in `dllmain.c`

## Purpose

The updater checks the Yule release channel without delaying game startup, exposes clear
status and controls in **Mods > Framework**, and can install a verified update so it takes
effect after restart. It adopts the release-channel contract used by the standalone
installer while adding crash recovery and cross-process serialization.

This component updates framework files only. It does not update the base game, maps,
mods, configs, save data, or account data.

## Runtime lifecycle

`hooks_update_on_pre_swap` boots the updater after the first rendered frame. This keeps
WinHTTP, worker creation, and recovery outside `DllMain` and outside the Windows loader
lock. Boot is idempotent and starts one background check worker.

The public status machine is:

```text
IDLE -> CHECKING -> UP_TO_DATE
                 -> AVAILABLE -> APPLYING -> RESTART_PENDING
                 -> ERROR
```

Manual checks are allowed from `IDLE`, `UP_TO_DATE`, or `ERROR`. Apply is accepted only
from `AVAILABLE`. Worker state and published strings are synchronized so repeated or
racing UI activations do not create concurrent workers or expose half-written status.

`FRAMEWORK_VERSION` in `update_ext.h` is the installed framework version. Versions are
dot-separated decimal components. Comparison is numeric, treats missing trailing zero
components as equal, and compares component strings without converting them to a fixed
integer, so very large components cannot overflow.

An orderly `update_ext_shutdown` API exists for normal application code, but it must
never be called from `DllMain`. Process exit without joining the worker remains safe
because recovery is journal-based.

## User experience

The Framework section always displays:

- installed framework version;
- automatic updates toggle;
- current update status; and
- the applicable Check, Retry, or Install action.

Automatic updates default to enabled. Enabling them while an update is already available
starts apply. The updater-owned `mods/modframework.cfg` keys are:

```ini
auto_update=1
update_channel_url=https://loafiieee.com/yule/releases/latest.json
```

The channel override is intended for development/testing. Config writes are atomic and
preserve unrelated lines and comments. `update_ext_config_set(key, value)` exposes that
same rooted writer to other framework subsystems: an in-process critical section and a
root-specific cross-process mutex serialize its read/modify/write cycle, and the final
file replacement is write-through. Keys accept only letters, digits, `.`, `_`, and `-`;
keys are nonempty and at most 63 ASCII bytes. Values are capped at 4,096 bytes and
cannot contain CR/LF. The function returns `1` on a successful persisted replacement
and `0` on failure. It does not mutate a caller subsystem's live state; dedicated APIs
such as `update_ext_set_auto` still own those in-memory transitions.

When an update becomes available, is applying, finishes, or needs attention, the runtime
may show a top-right notification. It scales to the viewport, is clickable to dismiss,
and expires after 12 seconds with bounded fade-in/out. Opening the Mods page also
dismisses it. An offline launch does not raise an error toast.

## Channel contract

The default channel is:

```text
https://loafiieee.com/yule/releases/latest.json
```

Schema version 1 requires:

```json
{
  "channel_version": 1,
  "version": "1.1.0",
  "base": "https://example.invalid/yule/releases/1.1.0/",
  "files": [{
    "path": "SDL2.dll",
    "sha256": "<64 hex characters>",
    "size": 1234567,
    "overwrite": true
  }]
}
```

`overwrite` defaults to true. A false value installs a missing file but preserves an
existing one.

Limits are enforced before download:

- manifest: 1 MiB;
- at most 32 files;
- path: at most 240 bytes;
- one file: at most 128 MiB; and
- declared total: at most 256 MiB.

HTTPS is mandatory except for `localhost`, `127.0.0.1`, and `::1` test channels. URL
credentials are rejected and redirects are disabled. Each request has an 8-second
timeout and must return HTTP 200. The manifest base cannot include query or fragment
data.

## Path and filesystem security

Channel paths are installation-relative and URL-safe. Validation rejects:

- absolute or drive-qualified paths;
- empty segments, `.` and `..`;
- alternate data streams and query/fragment characters;
- trailing dots/spaces and reserved Windows device names;
- any path segment ending in the updater-owned `.update-recovery` suffix;
- case-insensitive duplicates;
- collisions between a target and another target's `.old` backup; and
- overlong paths.

Every existing parent and target must be a normal directory/file, not a reparse point.
Missing parents may be created only beneath the updater's resolved installation root.
Staging cleanup does not recurse through reparse-point directories.

## Download and verification

Each payload is downloaded to `mods/update_staging`, then checked against both declared
byte size and SHA-256. A failed download or verification is retried once. After every
individual file passes, the complete staged set is hashed again before any live target
is moved.

The implementation uses Windows BCrypt for SHA-256. A manifest digest is not a signature:
integrity therefore depends on the HTTPS channel being controlled by the project. Code
signing or a separately signed manifest remains future hardening.

## Atomic install and recovery

Apply takes a named cross-process install mutex. Another game instance cannot swap the
same installation concurrently.

The transaction is:

1. recover any prior journal and reconcile legacy `.old` files;
2. download and verify the entire staged set;
3. write an uncommitted V2 transaction journal containing every path, original-file
   flag, expected installed byte size, and expected installed SHA-256;
4. move each old target to `<target>.old` with write-through;
5. move each staged file into place with write-through;
6. hash every installed target again; and
7. mark the journal committed.

If a move or post-install hash fails, replacements are rolled back in reverse order. If
rollback itself is incomplete, the journal remains for the next launch.

For an uncommitted V2 journal, recovery first hashes the complete declared release set.
If every target is present with the exact declared size and SHA-256, and no recovery
quarantine exists, the interrupted apply is promoted to `committed` and completed
forward. This covers the common interruption after the last target move but before the
commit record reached disk, without needlessly replacing a framework DLL that the new
process may already have mapped. Cleanup-only and reduced mixed journals are not eligible
for this promotion. Any other uncommitted journal takes the rollback path.

For a committed V2 journal, recovery hashes the complete installed target set before
deleting even one `.old` backup or clearing the journal. If every target matches, backups
are cleaned and the journal is removed. If any target is missing or mismatched and every
required original backup is still available, recovery rewrites the phase to `applying`
before attempting a whole-transaction rollback. If rollback artifacts are incomplete,
recovery makes no destructive guess and retains the journal and remaining files for the
next run or manual repair.

Rollback renames an installed target to `<target>.update-recovery` before restoring its
`.old` file. This permits disk repair when Windows allows a mapped image to be renamed but
not deleted. If the quarantine cannot yet be deleted, recovery writes a bounded,
backward-compatible cleanup journal and reports restart required. A mixed recovery writes
only unresolved original entries plus retained cleanup entries, so completed work is not
repeated. Every unresolved entry—whether sourced from V1 or V2—uses a syntactically valid
but deliberately non-authorizing digest sentinel. Applying rollback does not consume that
digest, and the sentinel prevents a later matching subset from being promoted as though it
were the complete release. Any mutation that restores or removes an original release
target requires a restart; a later cleanup-only launch that merely deletes an
already-released quarantine does not require an additional restart.

V1 `applying` journals remain backward-recoverable. A V1 `committed` journal contains no
expected digest, so it cannot prove that an existing target is the installed payload; it
fails safely and retains the journal/backups instead of deleting them. Structurally
corrupt journals are likewise not guessed through.

### Loaded DLL replacement boundary

The current installer does not unload framework modules, schedule a reboot-time move,
or launch a stable external replacement helper. It attempts a write-through rename of
the live target (for example `SDL2.dll`) to `.old`, then moves the staged file into the
target path. If Windows permits that rename for the mapped image, the current process
continues executing its already-mapped old code and the new target is loaded only after
restart; the committed backup is intentionally left for next-launch verification. If the
rename is rejected (for example by a sharing/locking condition), apply reports failure
and uses the normal rollback path.

The journal makes interrupted state diagnosable and recoverable once updater code can
run; it cannot make the direct-import bootstrap itself power-fail safe. A crash or power
loss between `target -> .old` and `staged -> target`, or between `target ->
.update-recovery` and `.old -> target`, can leave a directly imported DLL pathname
missing. Windows loads those imports before this in-process updater executes, so that
specific state cannot self-repair on the next game launch. Write-through requests improve
flush ordering but are not claimed as an atomic power-failure transaction.

Manual recovery for that bootstrap boundary is intentionally conservative: close every
game instance; only if the normal target is missing and the adjacent `.old` is a regular
file, restore that `.old` to the missing pathname. Never overwrite an existing target.
Leave the journal, staging directory, log, and every `.update-recovery` file in place for
verified reconciliation on the next launch. A small stable launcher or out-of-process
recovery helper, shipped outside the replacement set, is required before every
interruption point can be called automatically boot-recoverable.

The standalone regression suite does not load and replace the actual injected
`SDL2.dll`, so successful replacement of the mapped proxy is not claimed as automated
coverage. It remains a release-machine test on every supported Windows version.

## Failure policy

- No network or HTTP failure can block game startup.
- An unavailable channel produces quiet `IDLE` status and a log warning.
- A malformed/unsafe manifest produces `ERROR`; nothing is downloaded or swapped.
- A hash or size mismatch produces `ERROR`; the live install remains unchanged.
- A busy mutex reports that another instance is updating.
- Interrupted or failed swaps recover or retain enough journal state to retry recovery.
- A loader-blocking missing DLL requires the documented closed-game `.old` restore until
  an out-of-process recovery helper exists.
- UI check/apply calls are no-ops in incompatible states.

## Verification

The standalone `UPDATE_EXT_TEST` suite covers:

- version ordering, including components larger than 64-bit integers;
- SHA-256 known vectors;
- strict JSON and duplicate-field handling;
- unsafe, reserved, duplicate, and colliding paths;
- config preservation;
- V2 journal size/SHA persistence;
- applying-journal complete-set roll-forward and cleanup-journal exclusion;
- reduced V1/V2 digest-sentinel non-authorization and access-error retention;
- interrupted recovery, whole-set committed verification, valid committed cleanup,
  mapped-target quarantine handoff, mixed reduced journals, corrupt/missing target
  rollback, missing-backup retention, reserved recovery-namespace rejection, and V1
  compatibility;
- validated, comment-preserving public config writes; and
- check-worker status gating.

Build and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
```

The wrapper compiles from `update_ext.c`, starts a temporary server bound only to
`127.0.0.1`, sets `UPDATE_EXT_TEST_CHANNEL_BASE`, and therefore always includes a real
download/hash/apply/recovery integration case. The test redirects updater root state to
disposable `build\update_ext_*_tmp_*` directories and must report explicitly that the
live `SDL2.dll` was not changed. `UPDATER.md` retains the direct compiler command for
developers who need to run the dependency-light suite without its HTTP case.

Release verification still required:

- offline launch and slow network do not delay rendering;
- auto on/off and manual Check/Retry/Install transitions in the Mods UI;
- locked framework DLL replacement on each supported Windows version;
- abrupt termination at each journal phase followed by recovery;
- two instances contending for the same install; and
- the actual production HTTPS channel.

## Out of scope

- Updating the base game or user content.
- Rollback to an older release selected by the user.
- Delta/binary-patch downloads.
- Cryptographic release signatures or Authenticode verification.
- Restarting the game automatically.
- A stable out-of-process updater/recovery launcher.
- Running blocking worker shutdown under the loader lock.
