# Discord LFG Bot

The optional server-side LFG bridge posts one Discord message while an authenticated
player is waiting in a public casual or competitive queue. The message contains only the
canonical public username and queue name. Its two link buttons open the existing friend
challenge flow or the same public queue through the strict HTTPS-to-`yule://` handoff.

There is no direct-match link. Match IDs, ratings, control/P2P addresses, credentials,
rendezvous data, and authentication tokens are never passed to the bot or written to its
messages. Mentions are disabled.

## Architecture

`discord_lfg_bot.js` uses Discord's outbound HTTPS REST API directly. It does not open a
Gateway connection, request intents, accept Discord commands, or expose a new public bot
listener. Each player owns at most one tracked post:

- joining an unmatched public queue creates one message;
- a duplicate join does not create a second message;
- leaving, matching, disconnecting, or changing queue retires the exact owned message;
- a leave racing an unfinished create deletes the late-created message;
- orderly `SIGINT`/`SIGTERM` shutdown retires active posts before exit;
- requests are serialized, bounded, and honor Discord's returned `retry_after`.

An uncatchable process or machine kill can leave an old public LFG message behind. It
contains no private session data and its challenge button still goes through normal live
friend/presence/policy validation. Delete such stale bot-owned messages when recovering
from an unclean shutdown.

## Discord setup

Create a bot for the Yule Discord application, invite it only to the intended server, and
grant it `View Channel`, `Send Messages`, and `Embed Links` in one LFG channel. Keep the
bot token out of the repository, command history, logs, service unit, and world-readable
files.

Put the deployment values in a root-owned service environment file (mode `0600`):

```ini
DISCORD_LFG_ENABLED=1
DISCORD_LFG_BOT_TOKEN=replace_with_the_secret_bot_token
DISCORD_LFG_CHANNEL_ID=123456789012345678
DISCORD_LFG_PUBLIC_BASE_URL=https://play.example.com/yule
LFG_REDIRECT_HOST=127.0.0.1
LFG_REDIRECT_PORT=47880
```

The public base must be HTTPS and its path must be exactly `/yule`. The local redirect
listener deliberately binds loopback. Terminate TLS in the existing public reverse proxy
and forward only `/yule/` to it. For example:

```nginx
location ^~ /yule/ {
    proxy_pass http://127.0.0.1:47880;
    proxy_set_header Host $host;
}
```

Do not set `LFG_REDIRECT_ALLOW_REMOTE=1` in production. The redirect accepts only `GET`
and `HEAD` for these exact routes:

```text
/yule/hub
/yule/requests
/yule/queue/casual
/yule/queue/competitive
/yule/challenge/[a-z0-9_]{1,24}
```

Unknown, encoded, credential-bearing, query, fragment, backslash, direct-match, endpoint,
and token-shaped routes fail closed. Successful responses are non-cacheable 302 redirects
to the corresponding safe `yule://` intent.

## Verification

Run the dependency-free focused suite before deployment:

```powershell
cd online_server
npm run check
npm test
python ..\tests\discord_lfg_server_static_test.py
```

For live acceptance, use two accounts and cover both queues, duplicate joins, queue
changes, manual leave, immediate matchmaking, disconnect, clean service restart, Discord
429 recovery, and both buttons on a Windows installation with the registered `yule://`
handler. Inspect the Discord payload and service log to confirm the bot token and all
private match/control fields are absent.
