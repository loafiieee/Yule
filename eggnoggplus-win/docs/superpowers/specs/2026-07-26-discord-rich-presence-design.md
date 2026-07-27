# Privacy-Bounded Discord Rich Presence Design

Status: source implementation and focused guarded coverage complete; registered
application/live Discord desktop acceptance remains.

## Goal and non-goals

Expose useful coarse Yule activity in Discord without creating a second identity,
authentication, matchmaking, or invitation channel. Discord is presentation only. It
must not influence simulation, rollback, online control state, or matchmaking policy.

This slice does not implement the separate Discord LFG bot, OAuth, Social SDK identity,
party membership, join/spectate secrets, or private match links.

## Privacy boundary

`discord_rpc_ext_pump` accepts one `DiscordRpcActivity` enum. The enum is the only data
crossing from hooks into Discord; there is no arbitrary string or metadata parameter.
The module owns all fixed details/state strings.

Allowed payload data:

- one fixed coarse details/state pair;
- process ID required to bind `SET_ACTIVITY`;
- local command nonce;
- start time for the current coarse state; and
- `instance=false`.

Forbidden payload data includes usernames, opponents, maps, Elo, match IDs, endpoints,
server names, credentials, session/rendezvous/authentication tokens, parties, buttons,
secrets, and arbitrary text. Tests render every enum and reject those schema fields.

Although Yule now owns strict public hub/requests/queue/challenge launch intents, this
integration publishes no clickable route. Discord's button boundary is URL-oriented. A
future public HTTPS redirect may target only those already-validated routes, but it must
have explicit ownership, abuse, and redirect validation before this contract expands.

## Configuration

`mods/modframework.cfg` contains:

```ini
discord_presence=1
discord_application_id=<public numeric application ID>
```

The ID is validated as 1-20 ASCII digits with a nonzero leading digit. Missing or invalid
IDs make the setting `UNAVAILABLE` and prevent pipe attempts. The ID can alternatively be
compiled in through `EGGNOGGPLUS_DISCORD_APPLICATION_ID`; a valid config value overrides
it, while an explicit invalid config value fails closed.

The online Settings tab toggles only `discord_presence` through the existing atomic,
serialized framework-config writer. It does not load online credentials or rewrite the
online account configuration. The ID is intentionally deployment-owned and has no
free-form in-game editor.

## IPC lifecycle

The module lazily boots from the normal main-update thread, never `DllMain`:

1. Load and validate the two local configuration keys.
2. Probe Discord's ten documented Windows named pipes with overlapped handles.
3. Send opcode 0 protocol-v1 handshake with the public Application ID.
4. Incrementally read the eight-byte little-endian frame header and at most 64 KiB of
   payload.
5. On READY, send fixed `SET_ACTIVITY` opcode-1 commands.
6. Answer bounded ping frames with pong; ignore unknown opcode-1 events.
7. On close, malformed/oversized frames, zero-byte reads, or failed I/O, close locally
   and retry after 15 seconds.

All pipe reads/writes use overlapped operations and zero-time completion polls. There is
no `Sleep`, `WaitNamedPipe`, worker thread, subprocess, third-party DLL, or network
request. State changes coalesce; writes are separated by 4.1 seconds. A missing Discord
desktop client is a normal silent retry condition.

The ordinary SDL quit path closes Discord and the existing updater worker on the normal
runtime thread. `DllMain` detach remains wait-free.

## Activity ownership

Hooks computes activity after online control service and before the native state update:

- pending server match owns prematch;
- active rollback/server match owns casual, competitive, or private match;
- current queue owns casual/competitive queue;
- online hub owns sign-in, Friends, or general hub;
- native game state without an online match owns local play;
- all other states own menus.

This ordering prevents a menu overlay during an online match from replacing the match
presence and prevents stale hub state from overriding queue/prematch state.

## Testing and acceptance

Guarded native coverage validates ID/config parsing, all activity JSON, forbidden-field
absence, explicit activity clearing, little-endian headers, exact lengths, and oversized
rejection. Static coverage requires the enum-only API, fixed mapper, no-wait overlapped
calls, throttling, bounded payloads, settings persistence, orderly quit, and build/test
linkage. The full core runner also relinks the production serializer graph with the
module.

Live acceptance needs a registered Yule Application ID and Discord desktop. Verify every
coarse state, disabled and unavailable states, Discord absent/restart behavior, rapid
state coalescing, and privacy with an IPC capture. That deployment prerequisite remains
in `TODO.txt`; it does not leave source implementation in the feature backlog.
