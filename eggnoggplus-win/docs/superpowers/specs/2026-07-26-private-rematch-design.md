# Bilateral Private Rematch Design

Status: implemented in source with focused automated coverage; live two-client acceptance
remains required.

## Product contract

A rematch is a short-lived private agreement attached to one confirmed, committed match
result. It is not "queue again": neither player can start it alone, no public opponent can
enter it, and it never inherits competitive rating changes. The exact previous map is used
only after the current server revalidates that both installed manifests still match it.

The result UI remains the compact bottom-right notification. When eligible, it grows only
enough to show the private/unranked label, wall-clock countdown, and primary/secondary
actions. Mouse buttons, keyboard `R`/`N`, and controller X/Y are equivalent. The ordinary
hub remains usable because the notification is non-modal.

## Server state and authority

After `finishMatch` commits a terminal result, the server may create:

```text
match_id, participant A, participant B, exact map key,
created_at, expires_at, accepted_by
```

Eligibility requires a committed match, both exact participants connected, and no block in
either direction. The default lifetime is 45 seconds. `REMATCH_TTL_MS` is bounded to
100 ms..5 minutes so tests can shorten it without permitting an unbounded production
record.

The server owns all transitions:

```text
eligible
  -> one vote: waiting for voter + offer for opponent
  -> two votes: remove offer, announce starting, create fresh private match
  -> decline / queue / block / disconnect / other match / expiry: closed
```

The fresh match goes through `makeMatch` with source `rematch`, no queue name, and the exact
prior map key. This regenerates match ID, roles, input/rendezvous data, seed, P2P tokens,
and packet-auth token. An empty queue name is intentional: all rematches are unranked,
including those following a competitive result.

Removing an offer also clears rematch availability from the replayable terminal message.
Terminal result retries therefore cannot resurrect a declined, expired, or otherwise
cancelled offer.

## Wire protocol

The required flat capability is `cap_private_rematch:1` in both `server_info` and
`auth_ok`. Deployment preflight requires it before the service is accepted.

Client messages:

- `rematch_request { match_id }`
- `rematch_decline { match_id }`

Server messages:

- `match_result` adds `rematch_available`, `rematch_expires_in`, and
  `rematch_unranked`;
- `rematch_waiting` confirms the first vote;
- `rematch_offer` identifies the exact opponent and remaining lifetime;
- `rematch_starting` precedes the fresh `match_found`;
- `rematch_declined`, `rematch_unavailable`, `rematch_expired`, and
  `rematch_closed` terminate the local offer.

Every message is bound to the prior positive `match_id`. The client admits it only while
that exact confirmed result is retained. An offer naming a different or non-canonical
opponent is ignored. A validated fresh `match_found` clears the old result only after
protocol, authentication-token, and map validation succeed.

## Cancellation and failure behavior

Joining either queue first sends a decline, then the queue request on the ordered TCP
stream. Closing an actionable result notification also declines. A lost control connection
clears local rematch state while server disconnect cleanup notifies the remaining player.
Local wall-clock expiry removes the controls immediately; server expiry independently
closes the authoritative record.

If the map becomes incompatible between result and acceptance, both players receive
`rematch_unavailable`. A `rematch_starting` state cannot be replaced by a queue request
while the fresh match message is in flight.

## Verification

The isolated server runtime test covers eligibility fields, first-vote waiting/offer,
second-vote start, exact-map reuse, fresh match identity, private/unranked queue metadata,
decline, terminal replay after decline, queue cancellation, block cancellation, offer-time
disconnect, and expiry in a real multi-client TCP session. Its Node log goes to a temporary
file so a long run cannot deadlock on an undrained subprocess pipe. Static contracts pin
all cancellation owners, capability gates, exact-ID client
messages, delayed result clearing, mouse/keyboard/controller controls, and deployment
preflight. The full guarded native suite and guarded DLL build remain the release gate.
