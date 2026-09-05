# Yule hands-on testing

This is the short path for testing the framework features that cannot be proven by
native/unit tests alone. Launch `eggnoggplus.exe` normally, open the developer
console with backtick (`` ` ``), and keep `mods/modframework.log` open while testing.

## Automated baseline

Run these from the repository root while the game is closed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_core_native_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_v2_map_test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_map_script_test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_updater_test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_update_helper_test.ps1
```

The runners prepare the MinGW DLL search path before launching their test binaries.
They do not launch the game. A clean run establishes parser, rollback, network-envelope,
map-script, updater/recovery, Discord IPC, and audio-render regression coverage.

## Custom-map Eggnogg color

In Greggnogg's Map inspector, enable the Eggnogg color override and choose a
distinct color. Paint `E` and `^`, confirm the canvas preview, undo/redo the
setting, and export/reimport the package. Uncheck the override and verify the
export omits `rules.eggnogg_color`. Team goals `1`/`2` and tentacles should retain
their ordinary colors.

With the new framework build, test the package in both mirrored halves and enter
the goal: stationary/waving Eggnogg and goal particles should use the chosen
color. Switch to a map without the override and then a vanilla map to confirm
automatic colors return. Test two clients with the same build/package. Parser
and renderer unit coverage cannot replace this live visual pass.

## Sword-tile initial spawning

Use a room with a known number of `*` markers. Start immediately from the local
menu, repeat after waiting on the menu, and repeat through online setup and an
in-game restart. Each marker should initially create one sword. Pick up or move
the sword, leave the room, and revisit it to confirm normal reset spawning still
works. Also check a room with mines or `K` markers: their reset actions share the
initial-reset guard. For online testing both clients need the matching new build.

The automated lifecycle fixture reproduces the native countdown/first-update
ordering, but does not run the actual game. If duplicates persist, record the map,
room, local/online mode, and whether they appeared on initial start, restart, or a
room revisit. Those distinguish this repaired path from another spawning issue.

## Discord application and Rich Presence

The release default is compiled in and enabled even when
`mods/modframework.cfg` does not exist:

```ini
discord_application_id=1531027934004117664
discord_presence=1
```

With Discord desktop running, use:

```text
discord.app
discord.app 1531027934004117664
```

The first command must print that ID. The second must save it and queue a reconnect
without freezing or restarting the game. In **Mods > Framework**, toggle `Discord Rich
Presence` off and on; the same compact toggle is also available in the online hub's
Settings tab. Reopen the Mods menu and confirm the setting persisted. Then visit the main
menu, local gameplay, online hub, both queues, prematch, and an online match. Discord
should show only the coarse state documented in `DISCORD_RICH_PRESENCE.md`; it must never
show account, opponent, map, endpoint, or match information. Close/reopen Discord while
the game remains open and allow up to 15 seconds for reconnection.

## Built-in tunes versus bytebeat

Eggnogg's built-in playlist is contiguous: `data/tune.txt`, `data/tune1.txt`, and so
on. Those files use Eggnogg's postfix stack language, not JavaScript. Use:

```text
music.scan
music.rescan
music.play 2
music.play random
music.output_rate
music.output_rate 48000
```

`music.scan` must show the title and format status of every file. A numbering gap must
mark later files as ignored. `music.rescan` makes a compatible file added while the
game is open available immediately. `music.play <index>` forces a compatible track;
`music.play random` restores shuffle.

`data/tune8.txt` contains Gasman's original 255-byte “Steady On Tim” Dollchan formula,
including its assignments, arrow function, array lookup, and `random()` call.
`music.scan` must label it `dollchan-js`, and `music.play 8` must start it audibly at
its declared 44,100 Hz source rate. Listen through the first drum entrance (about 14.5
seconds) and compare the melody, delayed voice, drums, and noise envelope with the same
formula in Dollchan. JavaScript `random()` belongs to the private audio runtime and never
consumes gameplay RNG. Edit the expression, save, and confirm it restarts with a
`[music][tune8] Dollchan JavaScript active` log entry. Remove only
`sample_rate=44100`, save again, and confirm the log reports the 8,000 Hz default; then
restore `sample_rate=44100`. Remove `engine=dollchan` and confirm that the bounded parser
rejects the general JavaScript with a useful byte-offset error, then restore it.

`data/tune9.txt` is the roughly 76 KiB “Impromptu” Dollchan library entry. Force it
with `music.play 9` and confirm that it loads without the old 65,536-byte rejection and
plays as unsigned bytebeat at its native 32,000 Hz. `data/tune10.txt` is the current
44,100-Hz unsigned-bytebeat “Last Fountain”; force it with `music.play 10` and confirm
that it continues playing without an `InternalError: interrupted` stop.

Impromptu may take roughly a second to begin because the JavaScript producer fills its
complete bounded safety buffer before playback. It must then remain continuous, with no
`PCM producer underrun` warning. The producer runs outside the SDL callback; a warning
indicates that the machine cannot render that formula in real time and includes the
buffered-frame count.

After a full game restart, the log must contain
`[music] mixer output upgraded from 22050 Hz to 48000 Hz`. Compare both tracks with
Dollchan again: the old 22,050-Hz mixer discarded samples from these higher-rate
formulas, preserving tempo while aliasing their harmonics. The guarded regression now
checks both interleaved channels of the first 8,192 source frames against independent
Dollchan-compatible PCM hashes across consecutive 4,096-frame blocks.

Run `music.output_rate` to inspect the configured and currently active device rate.
Change it to `44100`, confirm the mixer reopens without changing a track's tempo, then
restore `48000`. Confirm `music_output_rate=48000` persisted in
`mods/modframework.cfg`. For a per-track test, add `output_rate=44100` to one marked
tune, save it, and verify status reports `(track override)` until another tune is
selected; then remove the override.

Copy additional Dollchan formulas after the marker and select the matching
`mode=bytebeat`, `signed-bytebeat`, `floatbeat`, or `funcbeat`. Scalar returns must play
in both channels; `[left,right]` must remain stereo. An unmarked JavaScript file still
reports `UNSUPPORTED_JAVASCRIPT` because rate and mode were not declared.

To test the implemented bytebeat/floatbeat engine itself, enable developer commands and
run these through the already loaded `options_demo` mod:

```text
dev on
lua.mod options_demo return mod.audio.play_bytebeat("t*((t>>12|t>>8)&63&t>>4)",{duration=4,loops=1,volume=.6})
lua.mod options_demo return mod.audio.play_bytebeat("sin(2*pi*220*time)*.3",{mode="floatbeat",sample_rate=22050,duration=2})
lua.mod options_demo local s=mod.audio.status(); return s.generated_chunks,s.generated_bytes
lua.mod options_demo return mod.audio.stop_sfx()
lua.mod options_demo return mod.audio.clear_generated()
dev off
```

Both expressions should be audible. Repeating the first call should report `cached=true`;
stop must halt owned channels and clear must release generated chunks. The full limits
are in `BYTEBEAT.md`. Generated sounds should still play when `SDL2_mixer.dll` is absent;
file-based WAV/OGG playback may use a different backend or be unavailable.

## V2 map, full tileset, and map Lua

Select `v2_symbolic_demo` in the custom map picker and follow the numbered acceptance
pass in `MAP_FORMAT.md` under “Acceptance fixture.” The important live checks are:

- ordinary native-prefix terrain renders from the map-wide `tiles.png` instead of
  turning black;
- walking into either half of the two-cell spring launches once, while swords, bodies,
  and the K hazard do not get relaunched;
- a body resting on the spring can still complete normal respawn;
- the active spring frame moves visually without moving its collision or terrain;
- the fan pushes across its full cell and reverses in the mirrored room;
- editing tint or `map.lua` hot-reloads, while invalid Lua keeps the last known-good
  behavior; and
- returning to ordinary maps restores the vanilla atlas/palette.

Watch for `[maps][v2_symbolic_demo] registered` and a `map content:` fingerprint in the
framework log. For online testing, both clients must log the same fingerprint.

## Online multiplayer

Use two separate clients against the live control server. Test casual, competitive,
friend challenge, private rematch, opponent disconnect, and one client closing during
prematch/countdown. Each path must either enter gameplay or return once to the hub with
a useful status; there must be no countdown freeze, hub/result loop, or fullscreen
win/lose page. An opponent leaving an active match should award the remaining player
the win exactly once. On an ordinary win, the native win animation/countdown must remain
visible; only after Eggnogg returns to main should the online hub open. Both clients must
agree on the winner without `conflicting match reports`.

First verify deployment/version behavior:

1. Update and rebuild both clients from the same source, deploy the matching
   `online_server`, and reconnect both clients. The server log's `[maps]` line for each
   account must show `protocols=3/3/17` plus identical nonzero `build=`, `exe=`, and
   `dll=` fingerprints; a created match must log that checked build and `p2p=17`.
2. Try one deliberately older or unadvertised client. It must receive a clear
   `Update Eggnogg+` incompatibility error before it enters a queue or challenge. It must
   never receive `match_found` against the current client.
3. Replace only one client's framework DLL with a different valid build that still speaks
   P2P v17. Both clients may enter the same queue, but the server must report the exact
   game/framework build mismatch and must not send either client `match_found`.
4. Do not use the displayed release label alone to decide compatibility. Both sides need
   the same control/match/P2P tuple and identical build/executable/DLL fingerprints; P2P
   v16 and v17 packet authentication and layouts are intentionally incompatible.

Test one pair across restrictive/mobile/CGNAT networks. The first log route may be
`public`; after the fresh-socket retry both clients must log the same `route=relay` and
finish prematch/gameplay. The server log should show
`direct path did not establish; enabling bounded UDP relay`. On a completed relayed
match it should also log the 15-second native-presentation grace, and packets must
continue through the visible win animation.

Before matchmaking, run the active connectivity check and leave the console open for up
to five seconds:

```text
online.troubleshoot
```

It must complete without freezing the game. A healthy setup reports PASS for both TCP
control and UDP rendezvous, including the public address/port observed by the server.
The target must match the configured `server_host:server_port`, even if Online was never
opened in this process. The named `Server-route adapter` must match Windows' route to that
host; an unrelated Radmin/overlay adapter may be noted as active but must not be called
the server route. VPN detection is explicitly heuristic. A private LAN address proves ordinary NAT but
does not prove CGNAT; compare the router's WAN IPv4 with the server-observed address as
instructed. Then, during a stuck or failed peer connection, run:

```text
net.diag
```

For deeper traces:

```text
dev on
ggpo.net rngtrace on
ggpo.net status
```

Compare both clients' `mods/desync_dump.log` and peer traces after any mismatch. The
full network matrix and remaining production gates are in `ONLINE_MULTIPLAYER.md`.

## Updater and recovery

Launch `eggnoggplus.exe` normally. Follow the production acceptance section in
`UPDATER.md` with a disposable release copy. Cover:

- no update, valid update, corrupt hash, interrupted download, and interrupted apply;
- the framework waiting until a safe menu, starting `YuleUpdater.exe`, closing the exact
  game process cleanly, applying the update, relaunching, and the helper exiting;
- interruption and verifying whole-set recovery on the next updater handoff;
- two game instances attempting an update; and
- exact forwarding of ordinary game arguments.

Do not test recovery against the only copy of a release. Keep the test channel version
newer than the installed copy, and verify `mods/updater.log` contains no credentials or
private online values.

## Discord LFG bridge

Deploy the service using `DISCORD_LFG_BOT.md`, one test channel, two Discord accounts,
and the public HTTPS redirect. A queue stay shorter than two seconds and any direct
friend challenge must create no message. Cover both queues, duplicate joins, queue changes,
manual leave, immediate match, disconnect, a Discord 429 response, and clean service
restart. Each posted message must be edited in place to its matched/left/changed/offline
state with no buttons, not deleted. The queue button must be on the left and the challenge
button on the right; both must open the registered `yule://` client route;
messages and logs must contain no match IDs, endpoints, credentials, ratings, tokens,
or mentions.

For the Windows handoff smoke test, close the game and run
`Start-Process 'yule://hub'` from a directory other than the installation directory.
Windows currently supplies `--yule-uri=yule://hub/`; the installed game must remain open,
show the online hub, and record `online.launch: accepted hub intent` in its own
`mods\modframework.log`. Repeat with requests, both queues, and an available canonical
friend challenge. While the first game remains open, repeat every link and confirm it
comes to the foreground and executes the action without creating a second game window.
Launching `eggnoggplus.exe` normally a second time must still work for local testing.
During an active match, a link must wait rather than forfeit. Test once with a remembered login and once after removing the remembered
credential and logging in manually. In both cases the original action must resume after
`auth_ok`; only `yule://hub` intentionally remains on the default hub page. No
caller-directory `data/` or `mods/` tree should be created. The HTTPS handoff tab should
close itself where browser policy allows; otherwise it must show the explicit manual-close
fallback instead of remaining blank.

For server-update acceptance, run `online_server/update_server.sh`, verify its local
TCP/UDP probe succeeds, and compare hashes/content of `users.json`, `ratings.json`, and
`server_secret.key` before and after. Force one bad service start in a disposable copy and
confirm the old application files are restored while those runtime files never move.

For repository-update acceptance, run
`python tests/repository_updater_static_test.py` and
`python tests/repository_updater_integration_test.py`. The integration test builds a
throwaway upstream and deployed checkout, then covers fast-forward replacement, upstream
file removal, dry-run behavior, refusal of local code edits, extra preserve paths, branch/
index advancement, and preservation of the complete live `online_server/` directory plus
untracked maps/operator files. On a disposable real checkout, also run
`tools/update_repository.sh --dry-run` before the live command and verify the dedicated
server process is never restarted.

## What still is not a live-complete feature

The custom-content API is a set of completed foundations, not yet the entire backlog:
declarative/V2 tiles and bounded map Lua exist, while programmable moving entities,
independent combat geometry, the map editor, expanded UI library, and Linux support
remain open in `TODO.txt`. Native postfix and marked bounded-bytebeat playlist tracks are
supported, and marked `engine=dollchan` tracks run in an isolated, memory- and
execution-bounded JavaScript runtime. Unmarked JavaScript remains unsupported.
