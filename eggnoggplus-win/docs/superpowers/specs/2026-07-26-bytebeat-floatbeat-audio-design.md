# Bounded Bytebeat/Floatbeat Audio Design

Status: source implementation, author documentation, bounded expression/render tests,
isolated JIT/fallback Dollchan compatibility tests, and static Lua ownership coverage
complete; live mixer/audio acceptance remains.

## Goal

Complete the missing procedural-audio portion of the existing `mod.audio` system with a
useful JS-256-style expression surface, floatbeat, actionable diagnostics, deterministic
rendering, strict CPU/memory limits, exact mod ownership, and no unsafe code execution or
temporary-file dependency.

The established audio system already supplies mod-relative file SFX/music, per-mod SFX
channels and volume, single-owner music, native Eggnogg synth IDs, SDL_mixer decoding, and
a WAV-only WinMM fallback. This design extends that system instead of opening a competing
audio device.

## Compiler boundary

`bytebeat_ext.c` is dependency-light and contains a fixed-capacity parser and evaluator.
It does not embed JavaScript or Lua. Expressions have a maximum of 1,024 bytes, 512 AST
nodes, and 48 evaluation levels.

The grammar provides numeric literals (decimal, hexadecimal, binary), sample/time/rate
variables, constants, parentheses, JavaScript-style unary/arithmetic/shift/comparison/
equality/bitwise/logical/conditional operators, right-associative power, and a fixed set
of one-to-three-argument math functions. `Math.` is accepted only for those fixed
functions/constants. Unknown identifiers, property access, calls, trailing syntax, and
over-budget trees fail with a byte offset and bounded diagnostic.

Bitwise conversion uses explicit JavaScript-style 32-bit wrapping. Logical and conditional
nodes short-circuit. The evaluator has no state, allocation, random input, I/O, callbacks,
or author-defined function dispatch.

## Dollchan playlist compatibility boundary

The bounded compiler remains the only engine exposed through `mod.audio`. Native
`data/tune*.txt` playlist files may explicitly opt into `engine=dollchan`. Supported
Windows systems use the system `Chakra.dll` hosting API for its JIT; it is resolved only
from the Windows system directory and receives no host objects. The vendored QuickJS-NG
v0.15.0 core remains the compatibility fallback without `quickjs-libc`, `std`, or `os`.
Neither path has a host filesystem, process, shell, DOM, or network API.

The playlist wrapper mirrors Dollchan's `audio-processor.mjs`: bytebeat,
signed-bytebeat, and floatbeat source is compiled as a function of integer `t`;
funcbeat source is a factory returning a function of `(time, sampleRate)`; every Math
member is also a short name; `int` aliases `floor`; scalar output is mono and a
two-element array is stereo. A bounded `t=0` validation call intentionally preserves
the reference player's initialization behavior.

Numeric channel coercion and output mapping are:

- bytebeat: `(value & 255) / 127.5 - 1`;
- signed bytebeat: `((value + 128) & 255) / 127.5 - 1`; and
- floatbeat/funcbeat: finite numeric output clamped to `[-1,1]`.

NaN or failed channel coercion holds that channel's previous output, as in Dollchan.
The runtime has an 8-MiB source cap and 64-MiB memory cap. Compilation gets a
source-size-scaled 100-5,000 ms deadline. Producer work is evaluated in sequential blocks
of at most 4,096 source frames and gets twice the corresponding playback duration plus
25 ms, clamped to 50-250 ms. This gives valid high-rate library songs enough bounded
JIT time without converting an infinite loop into an unbounded worker stall.
Chakra is created with reliable script interruption and a timer-queue watchdog;
QuickJS uses its interrupt callback and a 256-KiB stack cap.

Each track owns a fresh context and PRNG; it cannot touch gameplay RNG or deterministic
state. A dedicated producer owns that runtime and fills an eight-block, 32,768-frame
stereo PCM ring before playback begins. The real-time callback performs only nonblocking
reads and rate conversion; it never enters JavaScript, allocates, or waits. Selection
changes, Music Off, shutdown, and hot reload detach the stream under the callback lock,
then stop/join the producer before freeing its runtime.

## Mixer-rate boundary

Ghidra and instruction-level inspection show that `app_state_init` requests a 22,050-Hz
stereo SDL stream through `mad_init_audio_stream` at `0x404240`. The callback's third
argument is the resulting mixer rate. Evaluating a formula at 32,000 or 44,100 Hz and
then feeding that callback at 22,050 Hz preserves tempo, but the vanilla zero-order
phase converter discards source samples and aliases upper harmonics.

An early seven-byte, whole-instruction detour changes only the known vanilla 22,050-Hz
request to the configured mixer rate, defaulting to 48,000 Hz. This is the highest
accepted playlist authoring rate, so by default every
supported source is rendered 1:1 or upsampled rather than destructively downsampled.
The track's declared source rate remains independent and still controls `t` and
funcbeat's `sampleRate`. `music_output_rate` and `music.output_rate` accept
8,000-192,000 Hz; optional playlist `output_rate=` temporarily overrides the global
device rate without affecting tempo or pitch. Live changes close/reopen SDL synchronously
on the main thread. Unrelated explicit mixer-rate requests pass through unchanged;
if detour installation fails, vanilla audio initialization remains intact and a warning
identifies the reduced-fidelity path.

## Rendering

One compiled program renders either:

- unsigned-byte bytebeat: low eight bits mapped from 0..255 to signed 16-bit PCM; or
- floatbeat: finite output clamped from -1..1 to signed 16-bit PCM, with non-finite output
  replaced by silence.

The renderer applies finite gain and an optional symmetric click-suppression fade, then
produces a canonical little-endian mono PCM WAV. Sample rate is 4-48 kHz, output is capped
at 262,144 samples, and `node_count * sample_count` may not exceed 8,388,608. These limits
are validated both in the Lua request layer and the standalone renderer.

## Lua ownership and cache

`mod.audio.bytebeat_info` compiles and validates without audio initialization or rendering.
`mod.audio.play_bytebeat` validates, renders, caches, and plays through a bounded native
PCM voice pool mixed into Eggnogg's existing audio callback. It does not open a second
legacy SDL audio device.

The cache key contains the full expression plus every render-affecting option. A 64-bit
hash is display-only; lookup compares the exact retained key. Each mod may retain at most
16 unique generated chunks and 4 MiB of decoded generated PCM. Repeated exact requests
reuse the chunk. A mod owns at most eight simultaneous generated voices, with 32 voices
globally. `clear_generated` halts owned voices before freeing generated PCM and keys but
preserves file caches. Mod unload performs the same complete ownership cleanup.
Diagnostics expose generated chunk count and exact PCM bytes.

The generated PCM16 lives in one bounded cache allocation owned by its mod. The game/Lua
thread performs all parsing, rendering, allocation, cache mutation, and cleanup. The audio
callback only mixes already-rendered PCM; it allocates nothing and uses a nonblocking
lock attempt so gameplay cannot wait on the real-time callback or vice versa.

Generated audio is independent of both SDL_mixer and WinMM. File audio retains those
backends, but a missing or incompatible SDL_mixer DLL cannot disable generated bytebeat.
No generated-audio path writes a temporary file.

## Determinism and online policy

Expression evaluation is deterministic for one binary/platform but is presentation only.
Generated audio state and cache content never enter rollback state, checksums, map identity,
online fingerprints, gameplay RNG, or the map-local Lua sandbox. Online gameplay mods
remain governed by the existing suspension policy.

## Verification

The guarded native test compiles the bounded engine with strict warnings and covers grammar,
precedence, signed/unsigned shifts, short-circuit/conditional behavior, math aliases,
continuous time, diagnostics, length/rate/sample/gain budgets, exact U8 and float sample
mapping, fades, and canonical WAV fields. A separate guarded JavaScript test pins the
original “Steady On Tim” formula against Dollchan-derived reference bytes before its
randomized drum section. The first 8,192 stereo PCM frames of 76-KiB “Impromptu” at
32,000 Hz and “Last Fountain” at 44,100 Hz must match independent Dollchan-compatible
reference hashes across consecutive blocks. The suite also covers arrow functions,
assignments, stereo arrays, all four modes, Math aliases, host-module isolation, syntax
errors, and runaway-loop interruption. A threaded stream test pins complete prebuffering,
ordered PCM, nonblocking consumption, and zero expected underruns. Static tests prohibit
JavaScript in the mixer callback and pin the configurable 22,050-Hz detour, public Lua
registration, full-key caching,
cache/PCM/operation limits, synchronous safe decoder ordering, no temporary files or
quick-load API, cleanup, diagnostics, build source discovery, and the guarded runner.

Live acceptance remains in `TODO.txt`: mixer playback, audible examples, loop/stop,
cache reuse/exhaustion/clear, unload, volume/fades, and missing-mixer failure.
