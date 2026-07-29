# Client P2P protocol gate

## Problem

The control server advertised its own control, match, and P2P versions, but it did not
record the version actually used by each authenticated game client. A server could
therefore create a match without proving that both peers spoke the same P2P wire format.
That failure is especially confusing when only one player has a newer local build: both
clients can reach matchmaking, then the authenticated UDP handshake never succeeds.

P2P v16 and v17 cannot safely interoperate. V17 changes both the direction-key domain and
the fixed packet layout used for state-layout identity, correction, input ACKs, and
checksum ACKs. Accepting the old prefix or relabeling v17 as v16 would weaken
authentication and misparse packets.

## Contract

After `auth_ok`, the client's `map_manifest` includes:

- `framework_version`: a bounded diagnostic release label;
- `control_protocol`: exact client control protocol;
- `match_protocol`: exact client match protocol; and
- `p2p_protocol`: exact client UDP wire protocol;
- `build_id`: the deterministic local compatibility fingerprint;
- `game_exe_id`: the loaded game executable fingerprint; and
- `framework_dll_id`: the loaded framework DLL fingerprint.

The server stores all seven values on the connection. Compatibility requires both the
exact protocol tuple and exact build/executable/DLL fingerprint tuple. The human-readable
framework label is diagnostic only.

An authenticated client may join a queue, request challenge maps, send a challenge, accept
a challenge, rematch, or enter `makeMatch` only when its protocol tuple equals the
server's required tuple and its build tuple is complete. Queue pairing also requires both
clients to have the same supported protocol and build tuples. `match_found` uses the
match's checked protocol tuple and includes bounded opponent release/build diagnostics.

The server advertises `cap_client_build_gate: 1`. The current client requires it before
login, preventing a stale server from accepting a manifest it cannot enforce. This moves
the immutable build check that already existed after P2P HELLO into the authenticated
matchmaking boundary without weakening the later defense-in-depth check.

Missing fields are incompatible. The server sends a clear update error and removes an
already queued client rather than guessing a legacy version. Two supported-protocol
clients with different build tuples remain unmatched and receive a clear error instead of
consuming a match. There is no unauthenticated or v16 fallback.

## Verification

- The isolated server runtime registers two v3/v3/v17 clients, exercises queue, challenge,
  rematch, rendezvous, relay, commit, result, and social flows, then proves an unadvertised
  client and an exact-build mismatch are rejected before matchmaking.
- Static checks pin the client manifest fields, capability, strict integer sanitizers, and
  every server gate.
- The guarded native prematch runner proves both direct and symmetric-relay v17 handshakes.
  Its concurrent child timeout cleanup kills processes before waiting, so a failing case
  produces diagnostics instead of leaking executables or hanging the test command.
