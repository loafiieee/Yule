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

## Discord application and Rich Presence

The current development default is already in `mods/modframework.cfg`:

```ini
discord_application_id=1531027934004117664
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
the win exactly once.

During a match run:

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
and the public HTTPS redirect. Cover both queues, duplicate joins, queue changes,
manual leave, immediate match, disconnect, a Discord 429 response, and clean service
restart. Challenge and queue buttons must open the registered `yule://` client route;
messages and logs must contain no match IDs, endpoints, credentials, ratings, tokens,
or mentions.

## What still is not a live-complete feature

The custom-content API is a set of completed foundations, not yet the entire backlog:
declarative/V2 tiles and bounded map Lua exist, while programmable moving entities,
independent combat geometry, the map editor, expanded UI library, and Linux support
remain open in `TODO.txt`. Native postfix and marked bounded-bytebeat playlist tracks are
supported, and marked `engine=dollchan` tracks run in an isolated, memory- and
execution-bounded JavaScript runtime. Unmarked JavaScript remains unsupported.
