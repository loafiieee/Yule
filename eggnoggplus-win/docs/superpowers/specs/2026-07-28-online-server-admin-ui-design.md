# Online Server LAN Admin UI Design

**Date:** 2026-07-28  
**Status:** Initial maintenance surface and automated HTTP coverage complete; production
LAN deployment acceptance remains required  
**Primary code:** `online_server/admin_server.js`, `online_server/server.js`,
`tests/online_admin_server_test.js`

## Boundary and startup

Administration runs on a dedicated dependency-free HTTP listener, never the public
TCP/UDP gameplay port. The default bind is `127.0.0.1:47779`; LAN use must set
`ADMIN_HOST` to the machine's specific RFC1918/ULA address. Wildcard, hostname, and
public-address binds fail closed unless the explicit emergency override
`ADMIN_ALLOW_WILDCARD=1` is set. Requests from non-private source addresses are rejected
even if the listener is accidentally routed. `ADMIN_ENABLED=0` disables the listener.

There is intentionally no admin password or login page. Possession of private-LAN access
is the authorization boundary. Plain HTTP is acceptable only on the operator's trusted
isolated LAN or through an SSH tunnel; a reverse proxy with authenticated TLS is required
across an untrusted network.

## Request safety

The first private-client GET automatically creates a random 256-bit, IP-bound,
eight-hour session cookie with `HttpOnly` and `SameSite=Strict`; the session table is
capped at 64 and `ADMIN_COOKIE_SECURE=1` adds the `Secure` attribute for HTTPS-only
access. The session is not authentication—it owns a separate random CSRF token. Every
state change is POST-only and requires that token, preventing an unrelated webpage from
submitting maintenance actions through the operator's browser. Bodies are capped at
64 KiB. Responses are no-store and set a closed CSP, no-referrer, no-sniff, and
frame-denial headers.

## Maintenance surface

The dashboard shows online/account/queue/match counts and searchable account rows. It
supports:

- reset password, with a fresh salt and immediate session disconnect;
- ban with a persisted bounded reason, immediate queue/match cleanup through normal
  disconnect handling, and login denial;
- unban;
- force disconnect;
- reset Elo and MMR to configured defaults.

No credential hashes, salts, server secrets, match tokens, P2P endpoints, or raw JSON
records are rendered. Operations use the server's existing normalization, persistence,
rating-signature, messaging, and disconnect paths rather than editing files in the UI
module.

## Operations and follow-up

`update_server.sh` stages the new application module while continuing to preserve JSON,
keys, environment files, and logs. The listener participates in orderly SIGINT/SIGTERM
shutdown. Automated tests cover bind restrictions, disabled startup, security headers,
automatic private-client sessions, CSRF rejection, and action dispatch.

Account deletion, append-only audit records, granular administrator roles, backup/
restore controls, TLS termination, and durable transactional database storage remain
future production-hardening work.
