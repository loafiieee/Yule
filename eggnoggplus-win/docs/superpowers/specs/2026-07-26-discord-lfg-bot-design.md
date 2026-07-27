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

The normal matcher runs before a post is requested, avoiding a create/delete burst for an
immediate match. Each waiting username owns one generation-tagged record and at most one
Discord message. Duplicate joins coalesce. Queue change, leave, match assignment,
disconnect, or orderly process shutdown retires the exact recorded message. If leave
races create, the newly returned message ID is deleted before it can become owned by a
stale generation.

The adapter tracks at most 2,048 users and serializes outbound work. It observes Discord's
429 `retry_after`, bounds retries and response size, retries transient 5xx failures, and
uses a ten-second request timeout. Discord failure is logged as an LFG integration error
and never blocks or changes matchmaking.

## Link redirect

Discord link buttons require HTTPS. A dependency-free HTTP handler runs on loopback behind
the deployment's TLS reverse proxy. It accepts only `GET`/`HEAD` and the exact `/yule`
routes implemented by the native launch parser, then returns a no-store 302 to the
corresponding `yule://` URI. Encodings, queries, fragments, credentials, backslashes,
unknown actions, direct match/join actions, and arbitrary usernames reject.

The redirect cannot bind a public address unless an explicit development-only override
is set. Production keeps it on loopback.

## Discord surface

The integration is outbound REST only: no Gateway session, intents, slash commands,
interactions endpoint, or inbound bot listener. A queue message contains one embed and two
ordinary link buttons:

1. challenge the named player through normal friend/map/presence policy;
2. join the same casual or competitive public queue.

## Verification

Node tests cover config rejection, payload privacy, duplicate coalescing, exact deletion,
create/leave races, orderly close, 429 handling, canonical links, redirect security
headers/methods, and loopback enforcement. A static integration test pins server call
ordering, minimum-data handoff, lifecycle removal, and signal cleanup. The guarded core
runner owns the full test invocation.

Deployment QA still requires a real bot/channel, public HTTPS proxy, registered Windows
URI handler, both queues/buttons, immediate match, disconnect, clean restart, and rate
limit observation.
