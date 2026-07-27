# Safe Online Launch Links Design

Status: implemented with native, static, installer, and guarded build coverage.

## Goal

Allow LFG posts, shortcuts, and external tools to open the online hub, show the social
inbox, join a named public queue, or begin the existing friend-challenge map flow. A link
must never supply credentials, control-server addresses, raw peer endpoints, match IDs, or
session/authentication tokens.

## Supported intents

The command line accepts these case-insensitive navigation/queue switches:

```text
--online
--requests
--queue=casual
--queue=competitive
--challenge=canonical_username
```

The installed per-user URI handler accepts:

```text
yule://hub
yule://requests
yule://queue/casual
yule://queue/competitive
yule://challenge/canonical_username
```

`yule://` by itself is an alias for the hub. Account names use the server's exact
lowercase `[a-z0-9_]`, 1-24 byte contract.

There is deliberately no direct `join`, endpoint, password, token, match, server, or
arbitrary command route. A challenge still recomputes compatible maps and opens the normal
map picker; it cannot bypass friendship, block, presence, online-policy, or server
validation.

## Parser and injection boundary

`launch_request.c` is a standalone bounded parser. Unknown ordinary game arguments are
ignored, while malformed relevant switches, malformed/unknown `yule:` routes, conflicts,
non-ASCII/control input, query strings, fragments, percent encoding, backslashes,
userinfo, and noncanonical targets reject the complete online intent.

Windows registers one quoted argument:

```text
"<game>\eggnoggplus.exe" "--yule-uri=%1"
```

The protocol envelope is accepted only when it is the process's single non-executable
argument. Quote-based argument splitting therefore invalidates the online request instead
of appending another action. Update recovery is independent: after a verified
transaction is staged, the running framework starts the one-shot updater and omits
already-consumed online intents from the relaunch.

## Deferred runtime behavior

Windows command-line tokenization and UTF-8 conversion occur on the normal main-update
thread, never in `DllMain`. The runtime opens the hub through its existing deferred state
switch, then:

- immediately completes a hub-only request;
- waits for authentication before inbox, queue, or challenge actions;
- lets remembered sign-in satisfy that wait without showing the login page;
- opens Friends and selects the first inbox item for `requests`;
- calls the existing server queue method exactly once for a queue request;
- waits for `friend_snapshot_end`, then invokes the existing compatible-map challenge
  picker for a challenge request.

The intent expires after 120 seconds. Leaving the hub cancels it. Missing, offline, blocked,
or busy friends use the existing readable status and policy checks. No raw P2P start path is
reachable from the dispatcher.

## Installer ownership

The installer optionally creates `HKCU\Software\Classes\yule`, so elevation is not
required. It refuses to overwrite an unrecognized existing handler. Its manifest records
the exact command, and uninstall removes the tree only when the full minimal registry
shape and command still match that receipt; modified or externally owned trees are
preserved. A failed/incomplete framework adoption cannot register the handler.

`ProtocolRegistryRoot` is an internal test seam. The isolated installer lifecycle test
uses a random non-Classes HKCU subtree, verifies registration and receipt equality,
verifies exact uninstall, and always cleans it up.

## Verification

- `tests/launch_request_test.c` covers every action, casing, duplicate identity,
  conflicts, malformed switches/envelopes, unknown routes, usernames, injection syntax,
  endpoints, and credential-shaped URIs.
- `tests/online_launch_static_test.py` pins safe-thread ownership, existing authenticated
  action reuse, timeout/cancel behavior, friend-snapshot gating, forbidden routes, and
  conservative installer ownership.
- `tests/installer_lifecycle_test.py` covers the isolated HKCU handler lifecycle alongside
  verified install/uninstall and incomplete-adoption rollback.
- `tests/run_core_native_tests.ps1` is the only launcher for the MinGW parser executable
  and includes both static tests.

Discord Rich Presence and the external Discord bot are implemented as separate
privacy-bounded consumers. The bot emits only these public links and never receives or
places private match tokens in embeds.
