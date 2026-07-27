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
lowercase `[a-z0-9_]`, 1-24 byte contract. Windows and browsers canonicalize
authority-only custom URLs by appending one terminal slash (`yule://hub/`); the parser
accepts that single harmless normalization for each completed route while continuing to
reject doubled slashes and all other route expansion.

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

Protocol activation also inherits the browser's current working directory. The SDL proxy
normalizes the process working directory to the installed executable directory at the
start of process attachment, before crash reporting, logging, dependent DLL loading, or
game data access. Start Menu, Steam, browser, PowerShell, and direct executable launches
therefore resolve the same `data/` and `mods/` trees, and early crash reports stay beside
the installed game instead of being lost in an unwritable caller directory.

## Existing-process activation

The URI registration still points directly at `eggnoggplus.exe`; there is no persistent
launcher process. The first game which reaches SDL initialization owns a `Local\` mutex
and a message-only broker window for that Windows login session. A later process with a
valid parsed launch request locates the broker, grants its process foreground permission,
and sends one fixed-size `LaunchRequest` using bounded `WM_COPYDATA` plus
`SendMessageTimeout`. The receiver revalidates the closed action enum and canonical
challenge target before placing it in a locked single-slot queue.

Only after the running game acknowledges that copy does the activation process exit, and
it does so before calling the real `SDL_InitSubSystem`/`SDL_Init`. It therefore creates no
second game window. A broker startup/hang timeout fails open to the normal launch rather
than discarding an explicit user action. Ordinary executable launches have no online
intent, do not forward, and remain multi-instance for local two-client testing. A
forwarded intent received during pending/active gameplay waits; link activation cannot
become an out-of-band forfeit.

## Deferred runtime behavior

Windows command-line tokenization and UTF-8 conversion occur on the normal main-update
thread, never in `DllMain`. The runtime opens the hub through its existing deferred state
switch. Because that custom state no longer traverses the native menu update hook that
first parsed the URI, the hub update loop retains ownership of the pending request. It
processes control-server traffic first and pumps the launch dispatcher second, so either a
remembered or manually entered login resumes the requested action on the same tick as
`auth_ok`. It then:

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

- `tests/launch_request_test.c` covers every action, casing, Windows/browser terminal-slash
  normalization, duplicate identity, conflicts, malformed switches/envelopes, unknown
  routes, usernames, injection syntax, endpoints, and credential-shaped URIs.
- `tests/launch_ipc_test.c` runs a statically linked parent broker plus real child
  processes, proves exact challenge/queue forwarding, proves an ordinary direct child is
  not forwarded, and proves that child shutdown cannot close the parent's broker.
- `tests/online_launch_static_test.py` pins safe-thread ownership, existing authenticated
  action reuse, native-to-custom-state continuation after manual/remembered authentication,
  existing-process mutex/message handoff before real SDL initialization, foreground
  permission, match-safe deferral, timeout/cancel behavior, friend-snapshot gating,
  forbidden routes, and conservative installer ownership.
- `tests/installer_lifecycle_test.py` covers the isolated HKCU handler lifecycle alongside
  verified install/uninstall and incomplete-adoption rollback.
- `tests/run_core_native_tests.ps1` is the only launcher for the MinGW parser executable
  and includes both static tests.

Discord Rich Presence and the external Discord bot are implemented as separate
privacy-bounded consumers. The bot emits only these public links and never receives or
places private match tokens in embeds.
