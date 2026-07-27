# Bytebeat and Floatbeat

Yule's `mod.audio` API can compile compact music expressions into bounded in-memory
audio chunks. It supports classic unsigned 8-bit bytebeat and normalized floating-point
floatbeat without embedding JavaScript, invoking a shell, writing a temporary file, or
running author code on the audio thread.

## Quick start

```lua
local ok, info_or_error = mod.audio.play_bytebeat(
  "t * ((t >> 12 | t >> 8) & 63 & t >> 4)",
  {
    mode = "bytebeat",
    sample_rate = 8000,
    duration = 8,
    fade_ms = 8,
    gain = 0.7,
    volume = 0.8,
  }
)

if not ok then
  mod.log.error(info_or_error)
else
  mod.log.info(("rendered %d samples with %d nodes")
    :format(info_or_error.samples, info_or_error.nodes))
end
```

A floatbeat example:

```lua
mod.audio.play_bytebeat(
  "sin(2*pi*220*time)*0.25 + sin(2*pi*330*time)*0.15",
  {
    mode = "floatbeat",
    sample_rate = 22050,
    duration = 4,
  }
)
```

## API

### `play_bytebeat(expression [, options]) -> true, info | false, error`

Compiles, renders, caches, and plays an expression on one of the calling mod's owned
generated-audio voices. The returned info table contains:

- `mode`: `bytebeat` or `floatbeat`;
- `sample_rate`, `samples`, and exact rounded `duration`;
- compiled `nodes`, evaluation `depth`, and estimated `operations`;
- exact cached `pcm_bytes`, `backend="native_pcm"`, and the compatible
  `wav_bytes` export-size estimate;
- `cached`: true when an exact prior render was reused; and
- `channel`: the owned native generated-audio voice selected for playback.

Options:

| Key | Default | Contract |
| --- | ---: | --- |
| `mode` | `"bytebeat"` | `"bytebeat"`/`"u8"` or `"floatbeat"`/`"float"` |
| `sample_rate` | `8000` | 4,000 through 48,000 Hz |
| `duration` | `8` | 0.01 through 30 seconds, subject to sample/operation limits |
| `fade_ms` | `8` | 0 through 1,000 ms and no more than half the sound |
| `gain` | `1` | finite 0 through 4, applied while rendering |
| `volume` | `1` | finite 0 through 1, multiplied by the mod SFX volume |
| `loops` | `0` | -1 forever, otherwise 0 through 1,000 repeats |
| `ticks` | `-1` | -1 unlimited, otherwise at most 600,000 ms |

Generated audio does not require `SDL2_mixer.dll`. It is mixed into Eggnogg's existing
audio callback by a bounded native PCM voice pool, so it does not try to open a second
legacy SDL audio device. `SDL2_mixer` and the WinMM fallback remain relevant only to
file-based audio.

### `bytebeat_info(expression [, options]) -> info | nil, error`

Runs the exact parser and option/resource validation without initializing audio, rendering
samples, or changing playback. Use it in authoring/config UIs. Syntax errors include a
zero-based UTF-8 byte offset:

```text
bytebeat error at byte 8: expected ')' after function arguments
```

### Resource and playback helpers

- `stop_sfx() -> stopped_channels` halts the calling mod's owned SFX channels.
- `clear_generated() -> removed_chunks` first halts those channels, frees only generated
  chunks, and retains cached file sounds.
- `status() -> table` reports backend state, mod volume scalars, total/generated chunk
  counts, exact generated PCM bytes, cache limits, and
  `generated_backend="native_pcm"`.

Existing `play_sfx`, `play_music`, `stop_music`, `set_sfx_volume`, and
`set_music_volume` continue to work as documented in `MODDING.md`.

## Expression language

The expression is parsed by a purpose-built numeric compiler. It is JS-256-style, not
general JavaScript: there are no strings, arrays, assignments, loops, properties (except
the fixed `Math.` aliases), callbacks, allocation, filesystem, network, globals, `eval`,
or ambient random source.

Variables/constants:

- `t`: zero-based integer sample index;
- `sr`, `sampleRate`, `sample_rate`: selected sample rate;
- `time`, `seconds`: `t / sr`;
- `pi`, `PI`, `Math.PI`; and
- `e`, `E`.

Operators, from high to low precedence:

```text
( )
+ - ~ !                 unary
**                      power (right associative)
* / %
+ -
<< >> >>>
< <= > >=
== === != !==
&
^
|
&&
||
condition ? yes : no
```

Bitwise operands use JavaScript-style signed/unsigned 32-bit conversion and shift counts
are masked to 0-31. Arithmetic remains floating point, allowing the same compiler to
serve floatbeat. Logical operators and the conditional operator short-circuit and return
their selected operand value.

Functions may be written directly or with a `Math.` prefix:

```text
sin cos tan asin acos atan atan2
abs floor ceil round trunc int fract sign
sqrt log exp pow
min max clamp
noise
```

`noise(value)` is a pure deterministic hash in `[0,1)`. `noise(t)` is suitable for
repeatable percussion/noise without touching the game RNG or storing mutable generator
state.

`bytebeat` mode converts the final value to unsigned 32-bit, keeps its low byte, and maps
0..255 to signed 16-bit PCM. `floatbeat` mode maps finite -1..1 directly to signed 16-bit
PCM and clamps out-of-range values; non-finite floatbeat output becomes silence. The
optional symmetric fade reduces clicks at both chunk edges.

## Resource limits and ownership

Validation is fail-closed:

- 1,024 expression bytes;
- 512 syntax-tree nodes;
- 48 evaluation levels;
- 262,144 output samples;
- 8,388,608 estimated node evaluations per render;
- 16 distinct generated chunks per mod; and
- 4 MiB decoded generated PCM per mod;
- 8 simultaneous generated voices per mod; and
- 32 simultaneous generated voices globally.

Exact expression/options keys reuse a cached chunk. The full key is retained and compared,
so a short display hash collision cannot substitute different audio. Rendering writes
PCM16 directly into one bounded cache allocation; there is no temporary file, SDL RW
object, or mixer-owned memory chunk. Mod unload and `clear_generated()` stop every voice
owned by that mod before freeing the PCM and exact cache keys.

Generation occurs on the normal Lua/game callback thread and never in the SDL audio
callback. The operation limit bounds that synchronous work, but authors should still
pre-generate sounds during load/state entry rather than producing many unique expressions
during a frame. The audio callback performs no allocation and never waits for the voice
lock; when the game thread is updating the pool, that callback simply skips one generated
mix pass.

Generated sounds are presentation only. They do not enter game state, rollback snapshots,
checksums, online fingerprints, or gameplay RNG.

## Native playlist tracks

A contiguous `data/tune*.txt` file can opt into the same bounded bytebeat compiler with
a marker comment followed by one expression:

```text
$"Track title"
( yule:bytebeat sample_rate=44100 volume=0.25 )
t*((t>>12|t>>8)&63&t>>4)
```

For formulas authored for the
[Dollchan Bytebeat Composer](https://dollchan.net/bytebeat/), select the isolated
JavaScript engine and keep the source unchanged:

```text
$"Track title"
( yule:bytebeat engine=dollchan mode=bytebeat sample_rate=44100 volume=0.25 )
M=T=>(/* original Dollchan expression */),M(t/5000)
```

`sample_rate` accepts 4,000 through 48,000 Hz and defaults to 8,000 Hz;
`volume` accepts 0 through 1 and defaults to 0.25. `engine` defaults to `bounded`.
An optional `output_rate=8000..192000` changes the game mixer only while that
track is selected; if omitted, the global `music_output_rate` setting is used.
Source and mixer rates are independent: `sample_rate` controls the formula's
`t`, tempo, and pitch, while `output_rate` controls only the SDL device.
The Dollchan engine accepts `mode=bytebeat`, `signed-bytebeat`, `floatbeat`, or
`funcbeat`; the bounded engine accepts bytebeat mode.

The bounded expression surface remains limited to 1,024 bytes, 512 nodes, 48 levels,
and 25,165,824 node evaluations per source second. It allocates nothing in the audio
callback.

`engine=dollchan` uses the isolated system Chakra JIT on supported Windows versions,
with the vendored core-only QuickJS-NG runtime as a compatibility fallback. Both
backends follow Dollchan's execution shape: ordinary modes receive integer sample index
`t`, funcbeat receives `(time, sampleRate)`, all `Math` members are available as short
names, `int` is `floor`, a scalar return is mono, and a two-element array is stereo.
Output conversion matches Dollchan:

- bytebeat: `(value & 255) / 127.5 - 1`;
- signed bytebeat: `((value + 128) & 255) / 127.5 - 1`; and
- floatbeat/funcbeat: clamp numeric output to `-1..1`.

JavaScript source is limited to 8 MiB rather than the former 65,536-byte ceiling, so
large Dollchan library entries and their embedded song data remain intact. Each track
gets a private 64 MiB heap, adaptive 100-5,000 ms compilation/validation deadline, and
an interruptible 50-250 ms producer-work deadline. QuickJS fallback contexts additionally
use a 256 KiB stack ceiling. The producer evaluates up to 4,096 contiguous source
frames per JavaScript call, matching the block loop used by an AudioWorklet instead of
crossing the native/JavaScript boundary once per sample.

JavaScript does not execute in the real-time mixer callback. A dedicated
above-normal-priority producer renders eight 4,096-frame blocks ahead into a bounded
stereo PCM ring. Playback starts only after the complete safety buffer is ready; the
callback performs nonblocking reads and source-rate conversion. This keeps expensive
library tracks such as “Impromptu” sample-accurate without callback deadline gaps.
Unexpected underruns are counted and logged with the remaining buffer depth.

Eggnogg's vanilla SDL mixer request is 22,050 Hz, which is too low to carry the upper
harmonics of 32,000-48,000 Hz Dollchan formulas without aliasing. The framework defaults
that one known request to 48,000 Hz before the audio device opens. A track's declared
`sample_rate` still controls `t` and funcbeat's `sampleRate`; the higher mixer rate only
prevents destructive downsampling. Set `music_output_rate=48000` in
`mods/modframework.cfg` or run `music.output_rate 48000`; valid mixer rates are
8,000 through 192,000 Hz and a live change synchronously reopens SDL on the main thread.
Other explicit native audio-rate requests are left unchanged, and failure to install the
early hook preserves the vanilla initialization path.

The context receives no host objects or modules: there is no filesystem, process, shell,
DOM, or network bridge. The Chakra DLL is loaded only from the Windows system directory,
and its reliable-script-interrupt and memory-limit facilities are required. QuickJS
fallback still excludes `quickjs-libc`, `std`, and `os`. `random()` belongs to that
track's private JavaScript context and never consumes Eggnogg's gameplay RNG.

These tracks participate in the normal native shuffle and music On/Off setting.
Selection or source-file changes reset playback to `t=0`, and edits hot-reload using
the file timestamp and size. Unmarked JavaScript is still rejected because it has no
declared rate or playback mode.

Console authoring controls:

```text
music.scan
music.rescan
music.play 8
music.play random
music.output_rate
music.output_rate 48000
```

`music.scan` distinguishes `native-postfix`, `bounded-bytebeat`, `dollchan-js`,
unsupported unmarked JavaScript, and files hidden behind a numbering gap.

## Testing

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_core_native_tests.ps1
python tests/bytebeat_lua_static_test.py
```

The native suite covers bounded parsing/rendering plus isolated Dollchan JavaScript:
the original tune 8 formula is checked against reference byte output; tune 9
“Impromptu” and the current tune 10 “Last Fountain” are checked against independent
Dollchan-compatible hashes for both channels across their first 8,192 source frames.
That also proves large-source handling, native 32,000/44,100 Hz declarations, sequential
block state, numeric coercion, byte wrapping, volume, and PCM conversion. Coverage also
includes arrow functions, assignments, arrays/stereo, every playback mode, Math aliases,
host-module isolation, syntax diagnostics, memory/execution limits, and runaway-loop
interruption. A threaded stream regression verifies complete prebuffering, ordered PCM,
nonblocking consumption, and zero normal underruns; static coverage prevents JavaScript
from returning to the real-time callback and pins configurable mixer output, Lua API, exact cache
ownership, playlist callback/selection wiring, no temporary-file/quick-load path,
resource counters, build linkage, and guarded runner.

Release acceptance should play both examples, loop and stop them, reuse a cached render,
fill/clear the unique-chunk limit, test volume/fade extremes, unload/reload the mod, and
confirm generated sounds still play with `SDL2_mixer.dll` deliberately unavailable.
