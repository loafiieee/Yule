# Online Control Transport Reliability

**Date:** 2026-07-17  
**Status:** Bounded transport, strict receive/parser, and authentication lifecycle implemented; transport encryption remains open  
**Primary code:** `net_ext.c/.h`, `online_control.c/.h`, online lifecycle in `hooks.c`

## Scope and security boundary

`net_ext` is the main-thread, nonblocking TCP client used for newline-delimited online
control JSON. This slice prevents local short-write truncation and false-positive async
connect completion. It deliberately does not describe the channel as secure: the current
connection is raw TCP with no TLS, certificate validation, confidentiality, or server
authentication. Username/password login therefore remains observable and modifiable by
an on-path attacker until a TLS-capable control protocol is deployed.

## Logical-send contract

Each connection owns a bounded 128 KiB copied output queue. `net_send(slot, data, len)`
has message-atomic acceptance semantics:

- `len` means the complete logical message was copied and accepted;
- `0` means local backpressure left insufficient room for the whole message, and no
  prefix was accepted;
- `-1` means invalid input or a socket failure; and
- after a positive return the caller may immediately reuse, clear, or free `data`.

The implementation first drains any queued output, compacts only when tail space is too
small, checks capacity for the complete new message, copies it, and drains again. A
Winsock short write advances the queue by exactly the written count. `WSAEWOULDBLOCK`
preserves the unsent suffix for a later poll. Other errors close and clear the slot.

`net_recv` attempts an output drain before polling input, so the online hub's existing
per-frame receive pump also progresses backpressured output without blocking gameplay.
No retry loop sleeps or waits for socket readiness on the game thread.

Messages larger than 128 KiB are refused atomically. Protocol callers must bound their
messages below that value or introduce application-level framing/chunking. The custom-map
manifest sender now performs a count-only pass, rejects JSON above 96 KiB, allocates the
exact array plus a checked envelope, rebuilds it, and requires the second size to match.
Allocation, registry-change, formatting, and queue-acceptance failures disconnect the
newly authenticated control session instead of leaving the server's default/stale map
list active. A truncated JSON prefix is never sent.

## Async-connect completion

A nonblocking TCP socket becoming writable does not by itself prove that `connect`
succeeded. After `select` reports the socket writable or exceptional,
`net_check_connect` reads `SO_ERROR` with `getsockopt`:

- zero transitions the slot to connected;
- a nonzero socket error closes the slot and returns failure; and
- no readiness leaves the connection pending.

This prevents a refused connection from being exposed to callers as live and only
failing on its first send.

## Receive framing and strict JSON contract

The server stream is newline-delimited. A complete line may contain at most 8,191 bytes
before its newline. The client disconnects on a larger line, an embedded NUL, or an
incomplete stream that fills the 65,535-byte receive capacity. It never clips a line,
drops a prefix and continues, or attempts to recover framing after an overrun.

`online_control` validates the complete line before `hooks.c` dispatches its `type`.
Online messages are deliberately a small, flat JSON object protocol:

- values may be strings, JSON numbers, booleans, or null;
- nested objects and arrays are rejected rather than searched recursively;
- at most 64 fields and 63 decoded bytes per key are accepted;
- decoded duplicate keys are rejected, including escaped aliases such as `type` and
  `ty\u0070e`;
- malformed/unterminated escapes, invalid UTF-8, Unicode surrogate errors, unescaped or
  escaped control characters, trailing commas, and trailing data are rejected; and
- integer extraction accepts only a syntactic integer in the platform `int` range.

String extraction is all-or-nothing. If the destination cannot hold the decoded value
and its NUL terminator, the destination is cleared and `OUTPUT_TOO_SMALL` is returned.
There is no truncated-success state. A malformed line, missing/non-string/oversized
message `type`, or invalid `auth_ok` identity closes the control connection.

The server's unauthenticated `server_info` welcome and request response obey this same
flat contract. They advertise scalar `control_protocol`, `match_protocol`,
`p2p_protocol`, and `cap_p2p_auth` values; `auth_ok` repeats them. This lets deployment
preflight and the client reject an outdated match server without adding an array or
nested capability object that the strict parser would reject. After TCP completion the
client explicitly requests `server_info` and waits to validate the exact required values
before sending its login/register request. It revalidates the repeated fields in
`auth_ok`, and match setup independently checks the repeated match/P2P versions.

## Connect and authentication deadlines

The main-thread lifecycle has two independent wrap-safe wall-clock deadlines:

- TCP connect gets 10 seconds after `net_connect` has returned a nonblocking socket; and
- the capability handshake plus authentication gets 10 seconds after TCP completion and
  before a valid `auth_ok`/`auth_fail` completes it.

DNS resolution still happens inside `net_connect`, so the connect deadline cannot bound a
blocking system resolver call. Once a socket exists, no update-count or frame-rate
assumption affects either deadline.

Authentication source JSON and escape buffers are erased immediately after atomic send
acceptance. The password owner remains live only while a response is pending. Connect
failure, connect timeout, send failure, disconnect, malformed server input, invalid
`auth_ok`, or authentication timeout all funnel through disconnect cleanup and securely
erase that owner. `auth_fail` also disarms the deadline; a manually entered password may
remain for correction, while a rejected Credential Manager password is deleted.

The server-returned `auth_ok.username` is required and must be canonical
`[a-z0-9_]{1,24}` before authenticated state or credential storage is enabled. For a
`match_found`, a missing/empty legacy `map_key` may retain the numeric selector, but every
nonempty key must resolve to the exact installed map. An unknown, changed, wrong-typed,
or oversized key aborts prematch instead of silently playing selector zero or another
map.

## Resource and lifecycle policy

- There are at most four connection slots.
- Queue storage is fixed and bounded; a peer cannot grow it through remote behavior.
- Closing or fatally failing a slot uses `SecureZeroMemory` on the entire connection,
  including any queued authentication JSON.
- The queue is process memory only and is never persisted or logged.
- The API remains main-thread-only and intentionally has no internal worker thread.

The queue can briefly contain authentication JSON, including a password, until Winsock
accepts it or the connection closes. Callers still securely clear their source buffers
after `net_send`; TLS and a session-token login design are required to improve the wider
secret-exposure boundary.

## Verification

`tests/net_ext_test.c` runs a real loopback listener and verifies:

- a 100,000-byte patterned message arrives byte-for-byte;
- the caller can overwrite its source buffer immediately after `net_send`;
- an over-capacity message is rejected without sending a prefix;
- queued output progresses through nonblocking polling; and
- a connection to a bound-but-non-listening port never reports success.

The focused strict build is:

```powershell
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
  -D_WIN32_WINNT=0x0601 tests\net_ext_test.c net_ext.c `
  -o build\net_ext_test.exe -lws2_32
.\build\net_ext_test.exe
```

The strict parser/lifecycle coverage is:

```powershell
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic `
  tests\online_control_test.c online_control.c `
  -o build\online_control_test.exe
.\build\online_control_test.exe
python tests\online_control_hooks_static_test.py
python tests\online_flow_integration_static_test.py
```

The C suite covers valid flat objects, escaped and raw Unicode, exact-capacity output,
duplicate/escaped keys, nested values, malformed syntax and UTF-8, field/key caps,
integer overflow/type rules, canonical account names, and deadline wraparound. The
source-integration suites pin deadline ownership, secret cleanup, validation-before-
dispatch, oversize/NUL disconnects, canonical `auth_ok`, map fail-closed behavior,
complete bounded manifest construction, and malformed Credential Manager record deletion.

Manual integration QA should still force a small OS send buffer or delayed control
server reads, submit a large valid map manifest, and confirm that subsequent JSON lines
remain framed correctly.
