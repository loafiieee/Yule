# Deploy TLS for the Yule online server

The development client and server now encrypt account, matchmaking and match
credential traffic with authenticated TLS. The existing live v17 service has
not been changed. Deploy the matching client and v18 server together after the
remaining game tests pass. Preserve the existing accounts, ratings, server
secret, EOS configuration and UserInfo tunnel.

## Local testing now

Without certificate files, `node server.js` listens only on `127.0.0.1:47778`.
In the test installation's `mods/online_hub.cfg`, use:

```ini
server_host=127.0.0.1
server_port=47778
server_tls=0
```

The game refuses this plaintext setting for any remote address, including LAN
addresses. `localhost` is mapped directly to `127.0.0.1`. For two PCs on a LAN,
use TLS and the public game hostname, with local DNS pointing that hostname at
the server. The certificate must still match the hostname and be trusted by
Windows. A self-signed certificate is rejected by the player build.

## Network and certificate setup

Use `eggnogg.loafiieee.com` as the game server certificate name. Keep
`auth.loafiieee.com` on the existing Cloudflare HTTPS tunnel to the loopback
UserInfo service at port 47781.

The game hostname needs a DNS record pointing to the server's public address
and a route for TCP 47778 and UDP 47778. On a home connection, forward both
ports to `192.168.0.143`. Use Cloudflare DNS only for this game record; the
ordinary Cloudflare HTTP proxy/tunnel does not carry this raw TLS protocol on
47778. This is separate from the existing HTTP UserInfo endpoint.

Obtain a publicly trusted certificate for `eggnogg.loafiieee.com`. A Cloudflare
Origin CA certificate alone is not trusted by normal Windows clients. DNS
validation with Let's Encrypt avoids opening port 80. On Ubuntu/Debian with
Certbot available through the package manager:

```bash
sudo apt update
sudo apt install certbot python3-certbot-dns-cloudflare
sudo install -d -m 700 /etc/letsencrypt/credentials
sudoedit /etc/letsencrypt/credentials/cloudflare.ini
```

Put the DNS API token in that root-owned file, never in chat or Git:

```ini
dns_cloudflare_api_token = YOUR_ZONE_SCOPED_TOKEN
```

Scope the token to DNS editing for the `loafiieee.com` zone. Then:

```bash
sudo chmod 600 /etc/letsencrypt/credentials/cloudflare.ini
sudo certbot certonly --dns-cloudflare \
  --dns-cloudflare-credentials /etc/letsencrypt/credentials/cloudflare.ini \
  --dns-cloudflare-propagation-seconds 60 \
  -d eggnogg.loafiieee.com
```

Follow Certbot's email and agreement prompts. See the
[official Cloudflare DNS plugin instructions](https://certbot-dns-cloudflare.readthedocs.io/en/stable/)
for installation and API token requirements.

## Give the existing service access to the certificate

Check the existing service first:

```bash
sudo systemctl cat eggnogg.service
```

The examples below assume its service user/group is `loaf`; use the actual
service group if different. Keep the TLS private key readable only by root and
that service group. The key stays on the server.

```bash
sudo install -d -o root -g loaf -m 750 /etc/eggnogg/tls
sudo install -o root -g loaf -m 644 \
  /etc/letsencrypt/live/eggnogg.loafiieee.com/fullchain.pem \
  /etc/eggnogg/tls/fullchain.pem
sudo install -o root -g loaf -m 640 \
  /etc/letsencrypt/live/eggnogg.loafiieee.com/privkey.pem \
  /etc/eggnogg/tls/privkey.pem
sudo systemctl edit eggnogg.service
```

Add a drop-in containing:

```ini
[Service]
Environment=HOST=0.0.0.0
Environment=PORT=47778
Environment=UDP_HOST=0.0.0.0
Environment=UDP_PORT=47778
Environment=TLS_CERT_FILE=/etc/eggnogg/tls/fullchain.pem
Environment=TLS_KEY_FILE=/etc/eggnogg/tls/privkey.pem
```

Preserve the service's existing WorkingDirectory, ExecStart, environment files,
account database paths, `SECRET_FILE`, and EOS settings. The server requires
both TLS files before it will bind publicly. A missing, invalid or unreadable
certificate must be corrected; there is no plaintext fallback.

## Coordinated rollout

1. Keep a copy of the current application and service configuration. Back up
   `users.json`, `ratings.json` and `server_secret.key` privately.
2. Finish the two-client game tests, including interruption/recovery and the
   Asia-to-Asia and US-to-Spain comparisons. Local SDK fixtures do not establish
   internet acceptance or production readiness.
3. In a maintenance window, install the matching v18 server application and
   development-tested client release. Keep the existing data and secret files.
4. Run `npm run check` from `online_server` before starting it.
5. After reviewing the TLS drop-in, activate the matching server:

```bash
sudo systemctl daemon-reload
sudo systemctl restart eggnogg.service
sudo systemctl status eggnogg.service --no-pager
```

The initial restart ends existing connections, so coordinate it with the client
update. Normal clients use `server_tls=1`, the game hostname, and port 47778.
Existing v17 clients are incompatible with this v18 migration.

Verify locally using the public certificate name, then from outside the LAN:

```bash
cd /path/to/your/existing/online_server
python3 check_deployment.py 127.0.0.1 47778 --server-name eggnogg.loafiieee.com
python3 check_deployment.py eggnogg.loafiieee.com 47778
```

These probes validate the TLS certificate and control capabilities, then check
UDP discovery. When using `update_server.sh`, the default certificate name is
`eggnogg.loafiieee.com`; set `YULE_TLS_SERVER_NAME` if you choose another name.
`YULE_LOCAL_PLAINTEXT=1` is only for a loopback development service.

## Certificate renewal without disconnecting matches

After a successful Certbot renewal, copy the renewed full chain/key into
`/etc/eggnogg/tls` using the permissions above, then send SIGHUP:

```bash
sudo systemctl kill --kill-who=main --signal=HUP eggnogg.service
```

Node reloads the certificate for future connections while existing connections
continue. Check for `[control] TLS certificate reloaded` and rerun the TLS
preflight. A failed reload retains the previous context. Add these copy and
signal commands to a root-owned Certbot deploy hook before relying on automatic
renewal; the copies do not update themselves.

## Rollback

Restore the saved application, matching client release and service settings
together. Preserve current account/ratings data and the server secret. The
normal TLS client cannot connect to a restored plaintext server and will not
downgrade. Restoring the old public plaintext deployment also restores its
credential exposure; use it only as a deliberate rollback decision.

## What the fixes preserve

Yule v18 packets remain authenticated with the same HMAC, direction keys,
session checks and replay window. The network clock now tracks elapsed time at
60 Hz, so catch-up polls do not inflate RTT or shorten timeouts. Losing EOS
Connect authentication no longer prevents transport cleanup. A verified PUID
can be renewed during a match, but cannot be replaced with another identity.

Only the terminal BYE uses EOS reliable unordered delivery. It has no later
gameplay packet to block. Match teardown retains the EOS carrier asynchronously
until its outgoing queue drains, with a 100 ms minimum and a 1.5 s maximum;
retained carriers are capped at four. SDK shutdown drains for at most 150 ms
before freeing all retained carriers. Normal INPUT, HELLO, state/correction and
palette packets retain unreliable unordered semantics. Packet bytes, rollback,
ACKs and correction behavior remain Yule's responsibility.

## Verification

The native networking suite, actual gameplay service-hook test, EOS lifecycle
regressions, real EOS forced-relay gameplay/disconnect fixtures, TLS backpressure
transfer, untrusted/expired/hostname certificate rejection, Windows automatic
certificate verification, and server match protocol over TLS have passed.
Two PCs running the full game over the internet still need acceptance testing.
