# Discord Rich Presence

Yule includes a dependency-free Discord desktop Rich Presence client. It reports only a
small fixed set of public activity classes and never sends gameplay or account identity.
The integration is optional, local-only, nonblocking, and disabled automatically until a
valid Discord Application ID is configured.

## Deployment setup

1. Create the public Yule application in the Discord Developer Portal.
2. Copy its numeric Application ID.
3. Add this key to `mods/modframework.cfg`:

   ```ini
   discord_application_id=12345678901234567890
   discord_presence=1
   ```

4. Start `eggnoggplus.exe` normally.

The ID is a public application identifier, not a client secret. A release may instead
provide it at compile time with
`-DEGGNOGGPLUS_DISCORD_APPLICATION_ID=\"12345678901234567890\"`. An explicit valid
configuration value takes precedence; an explicit invalid value fails closed to
`UNAVAILABLE`. Builds default to the Yule application ID
`1531027934004117664` even when the configuration key is absent; the shipped
development configuration records the same value explicitly.

The same public ID can be inspected or changed without restarting:

```text
discord.app
discord.app 1531027934004117664
```

`discord.app <id>` validates the snowflake, atomically updates
`discord_application_id` in `mods/modframework.cfg`, closes any old application pipe,
and queues a nonblocking reconnect for the new application.

**Mods > Framework** and the online hub's Settings tab both expose a compact
`Discord Rich Presence` On/Off control. They update the same live state, write only
`discord_presence`, and preserve unrelated framework settings. The public Application ID
remains out of the account-oriented UI, but is configurable with `discord.app`.

## Published activity

The native API accepts a closed enum and maps it to fixed text:

| Game state | Details | State |
| --- | --- | --- |
| Menus | In Menus | Available |
| Offline gameplay | Local Match | Playing Locally |
| Online hub | Online Hub | Available |
| Authentication | Online Hub | Signing In |
| Friends/challenges | Online Hub | Friends & Challenges |
| Casual queue | Looking for a Match | Casual Queue |
| Competitive queue | Looking for a Match | Competitive Queue |
| Prematch | Online Match | Connecting |
| Casual match | Online Match | Casual |
| Competitive match | Online Match | Competitive |
| Friend match | Online Match | Friend Match |

Each activity includes only the local process ID, a local command nonce, and the time the
coarse activity class began. It deliberately omits:

- username, opponent, map, Elo, match ID, server, endpoint, and route health;
- passwords, control sessions, P2P rendezvous/authentication tokens, and private links;
- Discord party, secret, join/spectate, or instance fields;
- buttons and arbitrary URLs.

The completed `yule://` routes remain suitable for explicit user-created LFG links, but
Discord activity does not publish them yet. Discord buttons require a supported public
URL boundary; a private-token route or an unverified redirect bridge would weaken the
existing launch-intent contract.

## Runtime behavior

The client uses Discord's documented local Windows named pipes
`\\?\pipe\discord-ipc-0` through `discord-ipc-9`, protocol version 1, and
`SET_ACTIVITY`. All reads and writes are overlapped. The game thread never waits for
Discord, sleeps, starts a subprocess, performs network access, or loads a third-party
runtime.

Incoming frames are capped at 64 KiB. Unknown events are ignored, close/malformed/failed
I/O disconnects locally, and ping receives a bounded pong. Reconnect attempts are spaced
15 seconds apart. State changes coalesce and activity writes are spaced by 4.1 seconds,
staying below Discord's five-updates-per-20-seconds limit.

If Discord is absent or closed, gameplay continues normally. Disabling the setting or
receiving the ordinary SDL quit event closes the pipe without waiting. Process exit is
also safe.

Logs contain only unavailable/connected/disconnected classes. They never include the
Application ID, activity frame, pipe payload, or any online value.

## Verification

Automated coverage:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_core_native_tests.ps1
python tests/discord_presence_static_test.py
```

The guarded native test covers strict Application ID/config parsing, every fixed activity,
privacy-field exclusion, null clearing, frame encoding, and size bounds. Static coverage
pins the closed enum, overlapped/no-wait IPC, rate limit, settings persistence, runtime
pump, orderly shutdown, and build linkage.

Release acceptance still requires the real registered Yule Application ID and a running
Discord desktop client. Exercise every table row, the On/Off toggle, Discord absent at
startup, Discord restart while the game remains open, and rapid menu/queue transitions.
Inspect the rendered activity and a local IPC trace to confirm no private field is
published.
