# Secure Online “Remember Me” Credentials

**Date:** 2026-07-17  
**Status:** Implemented; live Windows Credential Manager roundtrip is opt-in for tests  
**Primary code:** `credential_ext.c/.h`, online login lifecycle/UI in `hooks.c`

## Goals

“Remember me” should remove repeated password entry without ever writing a password to
`mods/online_hub.cfg`, a log, or another plaintext project file. It must be opt-in, bound
to the exact account/server identity, and easy to disable.

The implementation uses the current Windows user's Generic Credential store through
`CredReadA`, `CredWriteA`, and `CredDeleteA`. It does not implement project-owned
encryption or retain an encryption key in the game directory.

This is a local-storage and in-process lifecycle improvement, not transport security.
The current online control protocol sends newline-delimited JSON over raw TCP and does
not use TLS. Remember me submits the password through that existing channel exactly as a
manually typed login does; it does not make authentication safe from an on-path observer.

## Login UI contract

Remember me is off by default. On the login gateway it is a compact, conventional
checkbox—not a full-height setting card:

- a square box is drawn with a check mark only when enabled;
- the adjacent label is **Remember me**;
- the full compact label line is clickable and keyboard/controller selectable; and
- the checkbox line is approximately `22..38` scaled pixels high, with an `18+` pixel
  square, rather than consuming another username/password field row.

Storage implementation details are intentionally absent from player-facing labels and
success/cleanup messages. The UI says only `Remember me`, `Remember me enabled/disabled`,
or a generic remembered-login failure that tells the player to enter a password.

The login layout accounts for two fields, this compact checkbox, two actions, server
text, status text, and footer. At small window sizes it reduces gaps and field height and
moves the form upward so the controls stay inside the panel.

Passwords accept up to 256 bytes in the online UI. They are rendered only as asterisks
while present or being edited.

## Persistent config contract

`mods/online_hub.cfg` may contain:

```ini
username=Alice
remember_me=1
server_host=eggnogg.loafiieee.com
server_port=47778
```

It never contains a `password=` line. The checkbox preference and username are harmless
lookup metadata; the secret is stored only after successful authentication.

## Credential identity

Each stored password uses `CRED_TYPE_GENERIC`, `CRED_PERSIST_LOCAL_MACHINE`, and this
versioned target shape:

```text
EggnoggPlus/YuleOnline/v1/server/<server>/port/<port>/user/<username>
```

Server ASCII is normalized to lowercase. Username bytes remain case-sensitive. Each
component is percent-encoded before joining, preventing separators or literal percent
sequences from creating target collisions. Empty identities, whitespace/control
characters in the server, control characters or leading/trailing spaces in the username,
and ports outside `1..65535` are rejected.

The account name returned by `auth_ok` is authoritative. A successful login stores under
that canonical username only after the client verifies the exact server contract
`[a-z0-9_]{1,24}`. A missing, oversized, wrong-typed, uppercase, or otherwise
non-canonical account name rejects `auth_ok`, disconnects, and clears the pending secret.
If a valid canonical name differs from the submitted spelling, the credential under the
submitted identity is deleted so only the server-canonical account target remains.

## Secret lifecycle

The normal path is:

1. The user enables the checkbox and enters a password, or a prior credential is loaded
   for the configured identity.
2. The password is sent to the control server over the existing raw-TCP authentication
   channel; Credential Manager does not alter or encrypt that transport.
3. Only `auth_ok` permits `CredWriteA`.
4. The password is written as blob bytes without a trailing NUL.
5. In-memory online password state is securely cleared immediately after the result is
   handled.

TCP connection and authentication each have a 10-second wall-clock deadline. Until an
authentication result arrives, connection failure, timeout, send failure, malformed
server input, or an invalid `auth_ok` identity all use the same disconnect path and erase
the pending password, including while the login hub remains open.

Enabling the checkbox does not store an unverified password. If no matching credential
exists, the UI leaves the normal login form available without describing the backend.

When config loads with Remember me enabled, the framework attempts to read the credential
for the exact host, port, and username. An empty, oversized, embedded-NUL, or otherwise
malformed blob is rejected. Read output is cleared before every attempt, including error
paths. A record classified as malformed or too large for the 257-byte login buffer is
immediately deleted using that same exact identity. Cleanup failures remain distinct in
the logs and use generic player-facing copy, so the framework never claims a failed
cleanup succeeded.

A valid remembered credential automatically starts a normal login when the player opens
the online hub. The username/password/register gateway is not rendered and its mouse,
keyboard, and controller actions are suppressed while this authentication is pending;
the standard hub shell shows only a short opening/signing-in state. `auth_ok` transitions
directly to the authenticated Play/Friends/Settings menus. A connection failure, timeout,
or rejected credential securely clears the in-memory password and restores the ordinary
login gateway for manual recovery. Only a password whose source flag came from the exact
credential lookup may take this automatic path, so a manually typed password is never
submitted merely by opening the hub.

Turning Remember me off immediately attempts to delete the matching credential and clears
memory if that credential supplied the current password. A cleanup error uses generic UI
copy and detailed non-secret logging, but the preference still becomes off rather than
pretending storage succeeded.

Changing username or server identity deletes the old target, clears the old in-memory
password, and, when Remember me remains enabled, looks up the new exact target. This
prevents a credential from one server/account being submitted to another.

If the server rejects a password that was loaded from Credential Manager, that credential
is deleted and memory is cleared. A manually entered failed password is left available in
the login form for correction; it was never stored.

## Memory and logging policy

`credential_ext_secure_zero` uses Windows `SecureZeroMemory` so the compiler cannot
optimize secret erasure away.

The implementation clears:

- password output buffers before credential reads;
- the local blob copy and credential structure after writes;
- Credential Manager blob memory before `CredFree`;
- escaped password and full authentication JSON buffers immediately after send;
- text-capture buffers on commit and cancellation; and
- the online password buffer after authentication, identity change, or leaving an
  unauthenticated login context.

Credential logs include only server, port, username, operation/result, and a Windows error
description. Passwords, password lengths from normal auth, auth JSON, and credential blobs
must never be logged.

## Failure policy

- Credential Manager unavailable: login still succeeds with the entered password; the UI
  says only that Remember me could not be updated.
- Credential absent: prompt normally; do not treat it as corruption.
- Credential malformed or too large: reject and clear it; require manual entry.
- Output buffer too small: return the required length without exposing a partial secret.
- Saved credential rejected by the server: delete it so launch cannot loop on a stale
  password.
- Toggle-off delete failure: disable remembering, report generic cleanup failure, and
  never claim deletion.
- Invalid identity: do not call the Windows credential APIs.

## Verification

`tests/credential_ext_test.c` covers:

- stable target construction and server case normalization;
- username case sensitivity and separator/percent collision resistance;
- target-buffer sizing;
- host, port, username, and secret bounds;
- error-result naming;
- read-buffer clearing on invalid input; and
- secure zeroing.

Its live write/read/delete roundtrip is intentionally disabled by default because it
touches the user's real Credential Manager. Enable it with:

```powershell
$env:EGGNOGGPLUS_CREDENTIAL_TEST_LIVE='1'
.\build\credential_ext_test.exe
```

The test uses a process-unique `.invalid` identity and deletes it before and after the
roundtrip.

`tests/credential_hooks_static_test.py` verifies integration invariants: config never
writes a password, auth buffers are cleared, storage occurs only after `auth_ok`, a loaded
rejected credential is deleted, malformed/oversized records are deleted before memory is
cleared, capture buffers are cleared, the login renderer contains the compact
checkbox/check mark without storage-backend copy, remembered credentials trigger guarded
auto-auth before row construction, the gateway stays hidden during that pending path, and
the build links `credential_ext.c` with `advapi32`.
`tests/online_control_hooks_static_test.py` additionally pins the pending secret cleanup
and canonical `auth_ok` ordering.

Manual UI verification must cover mouse, keyboard, and controller toggling; canonicalized
account names; identity switching; small F1 windows; save/read/delete errors; direct
authenticated-menu entry after a full restart; rejection returning to the login form; and
the config file remaining free of plaintext secrets.

## Security boundary and out of scope

Windows Credential Manager protects the secret at rest for the current Windows account.
It cannot protect against malware, a debugger reading this process during login, a
keylogger, a compromised Windows account, or a compromised authentication server.
Because the present control connection has no TLS, it also cannot protect the password
from network interception or an active man-in-the-middle. Transport encryption and
server authentication are required before the login protocol can be described as
end-to-end secure.

Also out of scope:

- synchronizing credentials across machines;
- storing session tokens instead of passwords;
- TLS-protected, server-authenticated online control transport;
- password reset/account recovery UX; and
- non-Windows credential backends.
