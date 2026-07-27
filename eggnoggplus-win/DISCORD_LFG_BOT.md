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

- remaining in an unmatched public queue for two seconds creates one message;
- a duplicate join does not create a second message;
- direct friend challenges never create an LFG message;
- leaving, matching, disconnecting, changing queue, or shutdown edits the exact message
  into an inactive state and removes both buttons instead of deleting it;
- leaving during the two-second delay creates nothing, while a leave racing an in-flight
  Discord create edits the late-created message immediately;
- requests are serialized, bounded, and honor Discord's returned `retry_after`.

An uncatchable process or machine kill can leave an old public LFG message behind. It
contains no private session data and its challenge button still goes through normal live
friend/presence/policy validation. Delete such stale bot-owned messages when recovering
from an unclean shutdown.

## Discord setup

1. Open the Discord Developer Portal and select the existing Yule application (or create
   one), then use its **Bot** page to obtain a bot token. The bot token is the secret this
   service needs; the application ID, public key, OAuth client secret, and Rich Presence
   application ID are not substitutes.
2. Create the intended LFG text channel. In Discord desktop, enable
   **User Settings > Advanced > Developer Mode**, right-click that channel, and choose
   **Copy Channel ID**.
3. On the application's installation/OAuth page, make a guild install using only the
   `bot` scope and permission integer `19456`, which is `View Channel` (`1024`) plus
   `Send Messages` (`2048`) plus `Embed Links` (`16384`). An equivalent authorization URL
   is:

   ```text
   https://discord.com/oauth2/authorize?client_id=APPLICATION_ID&scope=bot&permissions=19456
   ```

4. Install it only into the intended Discord server. If the channel has restrictive role
   overrides, explicitly allow those same three permissions for the bot in that channel.
   It needs no Administrator, Manage Messages, Gateway intent, slash-command, webhook,
   DM, or member-list permission.

The bot deliberately never connects to Discord's Gateway, so an offline-looking presence
is expected and is not a health check. Keep its token out of the repository, shell command
history, logs, systemd unit text, and world-readable files.

## `loaf-server1` deployment

The live control service already runs as user `loaf` from:

```text
/home/loaf/Yule/eggnoggplus-win/online_server
```

It can host the Discord bridge and loopback redirect in the same Node process; do not
create a second bot daemon. Ensure the deployed directory contains the matching
`server.js`, `discord_lfg_bot.js`, `lfg_redirect.js`, and `package.json`, then run:

```bash
cd /home/loaf/Yule/eggnoggplus-win/online_server
node --version                         # must be 18 or newer
npm run check
npm test
python3 ../tests/discord_lfg_server_static_test.py
```

Create a root-owned environment file. Enter the token interactively in the editor so it
does not enter shell history:

```bash
sudo install -d -o root -g root -m 0755 /etc/eggnogg
sudoedit /etc/eggnogg/lfg.env
sudo chown root:root /etc/eggnogg/lfg.env
sudo chmod 0600 /etc/eggnogg/lfg.env
```

Use these values, replacing only the token and copied channel ID:

```ini
DISCORD_LFG_ENABLED=1
DISCORD_LFG_BOT_TOKEN=replace_with_the_secret_bot_token
DISCORD_LFG_CHANNEL_ID=123456789012345678
DISCORD_LFG_PUBLIC_BASE_URL=https://loafiieee.com/yule
DISCORD_LFG_POST_DELAY_MS=2000
LFG_REDIRECT_HOST=127.0.0.1
LFG_REDIRECT_PORT=47880
```

Add the environment file to the existing `eggnogg.service` with a drop-in:

```bash
sudo systemctl edit eggnogg
```

The drop-in contents are:

```ini
[Service]
EnvironmentFile=/etc/eggnogg/lfg.env
```

Apply it:

```bash
sudo systemctl daemon-reload
sudo systemctl restart eggnogg
sudo systemctl status eggnogg --no-pager
tail -n 100 /home/loaf/Yule/eggnoggplus-win/online_server/server.log
tail -n 100 /home/loaf/Yule/eggnoggplus-win/online_server/server.err.log
```

Startup should include:

```text
[lfg] strict redirect listening on 127.0.0.1:47880
```

Do not use `systemctl show ... -p Environment` while troubleshooting: it prints the bot
token. If the token is ever pasted into chat, committed, logged, or printed in a terminal
capture, reset it in Discord and replace the environment-file value.

## HTTPS reverse proxy

The public base must be HTTPS and its path must be exactly `/yule`. The loopback listener
must remain private; do not open or port-forward `47880`, and do not set
`LFG_REDIRECT_ALLOW_REMOTE=1`.

`https://loafiieee.com/yule/releases/latest.json` already uses the `/yule/` namespace for
updates. A broad `location /yule/` proxy would break the updater. In the existing nginx
TLS `server` block for `loafiieee.com`, retain the current releases/static configuration
and proxy only the completed LFG route shapes:

```nginx
location ~ "^/yule/(?:hub|requests|queue/(?:casual|competitive)|challenge/[a-z0-9_]{1,24})$" {
    proxy_pass http://127.0.0.1:47880;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-Proto https;
}
```

If the current site has a broader `location ^~ /yule/` block, nginx will skip the regex
above. Narrow that block to the actual updater subtree (for example
`location ^~ /yule/releases/`) or replace the LFG regex with exact route locations. Do
not remove the existing updater file mapping.

Then validate and reload nginx:

```bash
sudo nginx -t
sudo systemctl reload nginx
```

The redirect accepts only `GET` and `HEAD` for these exact routes:

```text
/yule/hub
/yule/requests
/yule/queue/casual
/yule/queue/competitive
/yule/challenge/[a-z0-9_]{1,24}
```

Unknown, encoded, credential-bearing, query, fragment, backslash, direct-match, endpoint,
and token-shaped routes fail closed. A browser `GET` returns a non-cacheable landing page
which launches the corresponding safe `yule://` intent, waits briefly, and calls
`window.close()`. Browsers may refuse to close a tab they did not consider script-opened;
the page then says Yule was opened and leaves a manual link. `HEAD` remains a non-cacheable
302 with the exact `Location` for deployment checks.

## Verification

Verify the private listener, public proxy, and unaffected update channel:

```bash
curl -I http://127.0.0.1:47880/yule/hub
curl -I https://loafiieee.com/yule/hub
curl -I https://loafiieee.com/yule/queue/casual
curl -I https://loafiieee.com/yule/releases/latest.json
sudo ss -ltnp | grep 47880
```

The first three `HEAD` requests must return `302` with the matching
`Location: yule://...` and
`Cache-Control: no-store`. The release manifest must keep its existing successful
response rather than becoming an LFG `404`. `ss` must show `127.0.0.1:47880`, never
`0.0.0.0:47880`.

For live acceptance:

1. With nobody else waiting, sign in as account A and join Casual. Nothing should appear
   during the first two seconds. Then one Discord LFG post should appear and the log should contain
   `Discord LFG post created for account_a/casual`.
2. Leave the queue. The exact post must remain but change to **Queue closed**, with no
   buttons. The log should say it was updated.
3. Join Casual again. On a second Windows computer/account, click **Join Casual Queue**.
   The browser should redirect through the public HTTPS URL, ask to open Yule, and the
   client must finish the queue action even if login is required first. When matchmaking
   succeeds, the waiting post must change to **Match found**. If browser policy permits,
   the handoff tab closes itself; otherwise its fallback explicitly says it can be closed.
4. Make the two game accounts accepted friends, leave A available in a queue, and click
   **Challenge A** on B's computer. The client should resume the target challenge after
   authentication and open the ordinary compatible-map challenge flow.
5. Repeat with Competitive, queue switching, client disconnect, an immediate match where
   another player was already waiting, and a clean `systemctl restart eggnogg`. Also send
   and accept a direct friend challenge while neither account is publicly queued; it must
   create no Discord LFG message.

## Updating the live server safely

Run the repository-owned updater as the normal `loaf` account; it invokes `sudo` only for
the systemd stop/start:

```bash
cd /home/loaf/Yule/eggnoggplus-win/online_server
chmod +x update_server.sh
./update_server.sh
```

It clones the latest `main`, runs the complete server checks before downtime, replaces
only application files, restarts `eggnogg.service`, and probes TCP plus UDP locally.
`users.json`, `ratings.json`, `server_secret.key`, logs, environment files, PID/socket
files, caches, and `node_modules` are never staged. A failed start or protocol probe
restores the exact previous application files. Overrides are available through
`YULE_REPOSITORY_URL`, `YULE_REPOSITORY_REF`, `YULE_SERVICE_NAME`,
`YULE_SERVER_PORT`, and `YULE_TARGET_ROOT`.

Inspect the Discord payload and service logs to confirm the bot token and all private
match IDs, endpoints, credentials, ratings, and control/P2P tokens are absent. A hard kill
can leave a harmless stale post because in-memory message ownership is lost; delete that
post manually.
