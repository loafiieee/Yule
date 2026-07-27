# One-Shot Updater Handoff

## Status

Completed source implementation and focused automated coverage. This supersedes
the always-used `YuleLauncher.exe` entry point while retaining the verified
Journal V3 transaction and recovery model.

## Boundary

`eggnoggplus.exe` is the normal Start Menu, Steam, direct-launch, and
`yule://` target. `YuleUpdater.exe` is a small stable Windows helper installed
beside it and excluded from `latest.json`.

The injected framework checks the release channel, downloads every payload,
verifies the complete set, fingerprints original targets, and writes the
durable transaction. It never renames its mapped `SDL2.dll`.

When the transaction is ready, the framework waits for a safe menu. It launches
the helper with the exact current process ID and non-ephemeral relaunch
arguments. Online and deep-link intents already consumed by the running process
are deliberately omitted.

## Handoff lifecycle

1. Resolve `YuleUpdater.exe` beside the loaded framework module and reject a
   missing, directory, or reparse-point helper.
2. Build one shell-free Windows command line with exact argument quoting and
   `--wait-pid=<current pid>`.
3. Start the helper once from a main/options/mods menu. Gameplay and online
   matches are never interrupted; the ready notification asks the player to
   return to a menu.
4. The helper opens the parent process before requesting `WM_CLOSE`, preventing
   PID reuse during the wait.
5. Wait up to two minutes for orderly exit. Never call `TerminateProcess`.
6. Enumerate processes and refuse to mutate the installation while another
   process is running the exact same `eggnoggplus.exe`.
7. Acquire the root-specific installation mutex and classify the staged
   transaction.
8. Reverify every staged replacement and every original target precondition.
9. Apply with write-through target-to-backup and staged-to-target moves, verify
   the installed set, commit, recover/rollback as required, and clean staging.
10. Verify the game executable and installed proxy are regular non-reparse
   files, relaunch the game, and exit the helper.

If the game does not close, no installation file is touched. If the helper
cannot start, the running game stays open and the durable transaction is
retained for a later retry.

## Journal V3

Each target records:

- installation-relative path;
- whether the original target existed;
- replacement size and SHA-256;
- original size and SHA-256 when present.

A pristine transaction is eligible only when the journal is structurally
valid, all paths remain safe, all staged files match, all originals still match
or remain absent, and no backup/recovery artifact already indicates mutation.

V1/V2 transactions remain recovery-compatible. Ambiguous or incomplete
evidence blocks the update without guessing.

## Entry points and packaging

- Start Menu, Steam, protocol registration, and ordinary launches target
  `eggnoggplus.exe`.
- The installer owns `YuleUpdater.exe` by exact size/SHA-256 receipt.
- The helper is statically linked against MinGW support libraries and must not
  import any replaceable release payload. Build and release checks reject that
  dependency cycle before publication.
- The release builder requires root/build updater byte identity, packages the
  self-contained helper beside the installer, and excludes it from the
  replaceable manifest.
- The helper logs only timestamp, result class, and bounded status to
  `mods/updater.log`; arguments and online/private values are never logged.

## Verification

- Guarded cut-point tests cover pristine, partial, complete-before-commit,
  corrupt-commit, changed-original, corrupt staging/journal, and legacy backup
  recovery.
- A benign relaunch child checks byte-exact argument forwarding without
  launching Eggnogg and a held second child proves same-install mutation is
  refused.
- Static coverage pins safe-state gating, process-ID handoff, orderly close
  before apply, absence of force termination, direct game entry points, and
  updater packaging.
- Installer lifecycle coverage verifies direct EXE routing, updater
  installation/receipt ownership, protocol registration, and conservative
  uninstall.
