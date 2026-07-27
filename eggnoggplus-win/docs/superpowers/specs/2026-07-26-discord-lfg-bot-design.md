# Discord LFG Bot Design

Status: source implementation and focused automated coverage complete; real Discord,
public HTTPS, and installed-client acceptance remain release QA.

## Goal

Advertise a player who is genuinely waiting in the server's casual or competitive queue
without introducing a second matchmaking authority or exposing private connection data.
The message links back into the already validated public queue and friend-challenge
flows.

## Trust and privacy boundary

The control server calls the bot with exactly a canonical username and one closed queue
enum. The bot receives no password, Elo/MMR, match ID, peer/control endpoint,
rendezvous/probe data, or authentication token. Discord mentions are disabled. Link
buttons target the strict public HTTPS redirect, never a direct game session.

The bot token is deployment-only secret state. Source, payloads, and normal logs contain
no token. The public application/channel IDs and player queue announcement are not
secrets.

## Lifecycle

The normal matcher runs before a post is requested. A player must remain in the public
queue for two seconds before the request is made, so immediate matches and short waits
create nothing. Direct friend challenges never call the bot. Each waiting username owns
one generation-tagged record and at most one Discord message. Duplicate joins coalesce.
Queue change, leave, match assignment, disconnect, or orderly process shutdown PATCHes
the exact recorded message into a reason-specific inactive embed and removes its buttons.
If leave races an already in-flight create, the newly returned message ID is immediately
PATCHed by that stale generation rather than deleted or left actionable.
A matched embed is retained for a configurable 24-hour default, then the bot DELETEs that
exact bot-owned message. Other inactive states remain as queue history. The timer is
unreferenced and bounded to seven days; a process restart can leave an old matched embed
which is harmless and may be removed manually.

The adapter tracks at most 2,048 users and serializes outbound work. It observes Discord's
429 `retry_after`, bounds retries and response size, retries transient 5xx failures, and
uses a ten-second request timeout. Discord failure is logged as an LFG integration error
and never blocks or changes matchmaking.

## Link redirect

Discord link buttons require HTTPS. A dependency-free HTTP handler runs on loopback behind
the deployment's TLS reverse proxy. It accepts only `GET`/`HEAD` and the exact `/yule`
routes implemented by the native launch parser. `HEAD` returns a no-store 302 with the
exact protocol `Location`. Browser `GET` returns a nonce-CSP landing document, immediately
navigates to the corresponding `yule://` URI, then attempts `window.close()`. Because a
browser may refuse to close a tab it did not classify as script-opened, a delayed fallback
changes the page copy and retains one manual launch link. Encodings, queries, fragments, credentials, backslashes,
unknown actions, direct match/join actions, and arbitrary usernames reject.

The redirect cannot bind a public address unless an explicit development-only override
is set. Production keeps it on loopback. The live deployment already serves update
manifests below `/yule/releases/`, so its TLS proxy matches only the completed
hub/requests/queue/challenge route shapes; a broad `/yule/` proxy is forbidden because it
would shadow the updater.

The existing `eggnogg.service` loads the Discord token and channel/base configuration from
a separate root-owned `0600` `EnvironmentFile`. The service file, repository, logs, and
shell history contain no secret. The same Node process owns the control server, Discord
REST bridge, and loopback redirect.

## Discord surface

The integration is outbound REST only: no Gateway session, intents, slash commands,
interactions endpoint, or inbound bot listener. A queue message contains one embed and two
ordinary link buttons:

1. challenge the named player through normal friend/map/presence policy;
2. join the same casual or competitive public queue.

## Verification

Node tests cover config rejection, payload privacy, the two-second/no-short-wait boundary,
duplicate coalescing, exact in-place lifecycle edits, matched-only delayed deletion,
create/leave races, orderly close,
429 handling, canonical links, self-close fallback markup/CSP, redirect security
headers/methods, and loopback enforcement. A static integration test pins server call
ordering, minimum-data handoff, lifecycle removal, and signal cleanup. The guarded core
runner owns the full test invocation.

Deployment QA still requires a real bot/channel, public HTTPS proxy, registered Windows
URI handler, remembered and manual-login continuation into the requested action, both
queues/buttons, immediate match, disconnect, clean restart, an unaffected updater release
URL, and rate-limit observation.
