# Online UDP Relay and State-Preserving Server Update

**Date:** 2026-07-26
**Status:** Source implementation and isolated runtime coverage complete; public
two-machine NAT acceptance remains release QA.

## Problem

The rendezvous service could correctly observe and publish both public UDP endpoints while
neither NAT admitted peer-to-peer packets. Repeating direct hole punching with new sockets
does not solve symmetric NAT, CGNAT, or restrictive mobile/hotspot policy. Separately,
updating the live checkout with broad Git operations risks mixing application changes with
the account, rating, secret, log, and environment state stored beside `server.js`.

## Coordinated relay fallback

Every match starts with the existing deterministic loopback/LAN/public choice. Endpoint
registrations carry an internal generation which advances only when the observed source or
client local port changes. A second generation is evidence that the client opened a fresh
socket after the bounded direct attempt. When relay is enabled, either peer reaching that
generation atomically sets `match.force_relay`, clears both notification keys, and sends
both clients the same `relay` route. This avoids asymmetric direct/relay pinning.

The marker is not a server-supplied hostname. The client resolves it through the same
configured control hostname which already passed configuration and DNS validation, using
the server-provided UDP port.

The Node UDP listener distinguishes JSON discovery traffic from the fixed GGPO packet
prefix. Binary forwarding requires all of:

- protocol-v17 magic/version, a known packet type, and 44-2,048 bytes;
- an observed source endpoint currently owned by an active match participant;
- `sender_player` matching the server's recorded host/join player assignment;
- a recorded destination endpoint for the exact opponent;
- no more than 600 packets or 768 KiB from that endpoint in the current second.

Forwarding is one input datagram to one authenticated opponent endpoint, so it is not an
amplifier or arbitrary target proxy. The server does not mutate packets. The peer remains
responsible for the existing end-to-end HMAC, session, sender, and replay-window checks.
Every result, abort, and disconnect removes relay endpoint ownership.

This is a prematch connectivity fallback. Established-match resume, authenticated NAT
rebinding, adaptive route migration, congestion control, relay health display, IPv6, and
multi-region relay selection remain separate work.

## Server application update

`online_server/update_server.sh` runs from the deployed checkout as its normal service
user. It shallow-clones a configurable repository/ref to a new temporary directory and
runs JavaScript syntax/unit tests, static security/lifecycle tests, and the isolated
two-client match protocol test before stopping the live service.

The staging walk excludes runtime state by both exact name and class:

- `users.json`, `ratings.json`, and `server_secret.key`;
- logs, keys, environment files, PID files, and sockets;
- `node_modules` and interpreter caches.

Only staged regular application files are copied. Before each replacement, the exact
existing file is copied to a disposable rollback tree; newly introduced paths are tracked
separately. The service is then started and must remain active and pass the local TCP plus
UDP deployment probe. Failure stops it, restores overwritten files, removes only newly
introduced application files, and attempts to restart the prior version. Runtime state is
never moved, deleted, copied from Git, or included in rollback.

## Verification

- `tests/online_server_match_protocol_test.py` proves direct publication, a fresh-socket
  generation, symmetric relay notification, player-slot admission, and byte-exact binary
  forwarding.
- `tests/online_server_auth_static_test.py` pins relay ownership, bounds, symmetric switch,
  result cleanup, and winner-slot/account mapping.
- `tests/server_updater_static_test.py` pins clone-before-stop validation, protected state,
  rollback, health probing, and the absence of broad destructive Git/rsync operations.
- `node --check online_server/server.js`, the complete server test suite, and the full
  proxy compile cover syntax/link integration.
