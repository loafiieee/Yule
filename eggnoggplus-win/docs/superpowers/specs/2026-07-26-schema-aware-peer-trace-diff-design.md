# Schema-Aware Peer Trace Diff Design

Status: implemented with focused guarded coverage.

## Goal

When paired rollback traces diverge, identify the first useful simulation location without
printing the captured state. Existing generic validation and SHA-256 reporting must remain
available for arbitrary and older trace payloads.

## Recognition boundary

The analyzer recognizes only the checked-in 32-bit canonical `EGG0` version 9 layout. It
requires the exact magic/version, 0x25688-byte header, 0x15c player/entity sizes, bounded
entity count, four-byte tile cells, sane dimensions, and an exact payload-size equation:

```text
header + 2 players + thing_count entities + tilemap_bytes == state length
```

Failure of any condition is not a trace-format error because older states remain valid
inputs to the generic comparator. It simply suppresses schema annotations. Native
compile-time assertions pin every major mirrored boundary; changing the C layout requires a
blob-version bump and a new analyzer decoder.

## Secret-safe report

The existing public record continues to expose only frame, length, finalized checksum, and
whole-state SHA-256. For two recognized states, JSON adds:

- SHA-256 for non-overlapping scalar header, map-script, transient, thing-info, room-info,
  particle, player 0, player 1, things, and tilemap components;
- ordered changed-component names;
- every differing native entity slot and the number of changed tile cells;
- the first exact scalar header field or map-script table entry;
- the first player plus known field;
- the first entity slot plus known field; or
- the first tile index, x/y coordinate, and byte within its four-byte cell.

Unknown bytes inside the still-opaque native slabs are labeled by component and relative
offset rather than given a false semantic name. No state byte or decoded gameplay value is
included in plain or JSON output.

## Compatibility and failure behavior

The generic first-byte/checksum/length/count behavior and exit codes are unchanged:
identical is 0, valid divergence is 1, invalid input/usage is 2. If two recognized states
have different valid layouts, each side receives its own first-location object. Plain output
adds one concise schema line; machine consumers receive the full hash/location object.

## Verification

Focused fixtures cover generic fallback, scalar header, map-script state entry, player
field, entity slot/field, and tile x/y/cell-byte differences. They also pin changed
components, entity indices, tile counts, secret-free JSON, uint32 frame wrap, checksum-only
divergence, count/length mismatch, truncation, size bounds, and broken sequencing. The
guarded native build proves the mirrored C size/offset assertions.
