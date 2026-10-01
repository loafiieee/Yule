# Stable and beta releases

The actual deployment and owner test copies are documented in
[PRIVATE_BETA_TESTING.md](PRIVATE_BETA_TESTING.md). Public stable is 1.932, the
native v17 bridge with channel switching. Public opt-in beta is 2.0.0, the EOS
v18 client, using TLS matchmaking at `beta.loafiieee.com:47782`. Both owner
copies now use the public manifests.

## Player controls

Opt in from the game console:

```text
update.channel beta
```

Return to the main menu. The game verifies the beta runtime, closes through
`YuleUpdater.exe`, installs it, and restarts. After opting in, **Mod Options →
Framework → Beta testing** appears. Turn it off to return to stable; turn it
on to return to beta. `update.channel stable` also works. `update.channel`
prints the current channel. No developer mode is required.

Finish an online match first. A switch signs out of online play and blocks
sign-in while it is being prepared. Restarts occur from the existing safe
menus, never during a match.

| Channel | Manifest | Matchmaking |
| --- | --- | --- |
| Stable | `https://loafiieee.com/yule/releases/latest.json` | Saved stable server settings |
| Beta | `https://loafiieee.com/yule/releases/beta.json` | `beta.loafiieee.com:47782`, TLS |

Stable settings use `mods/online_hub.cfg`; beta uses `mods/online_hub.beta.cfg`.
Beta's endpoint comes from its release profile and overrides the saved server
address. Remembered passwords stay scoped to hostname, port and username.
The first beta sign-in may require entering the password once.

## Caches and recovery

Runtimes live in `mods/update_channels/stable/` and
`mods/update_channels/beta/`, with manifests recording hashes and sizes. The
updater preserves the current runtime before replacing it. Support DLLs and
the restricted EOS GameClient credential accompany the release that needs
them. Unused optional files may remain beside the game; the selected DLL
determines which libraries it uses. `YuleUpdater.exe` is the shared helper.

Switching to a verified cache needs no payload download. On launch, that
channel checks its own manifest; a newer release uses the normal update flow.
Returning to an older stable version is allowed. The active profile,
`mods/update_channel.json`, commits last in the same recoverable transaction
as the runtime. The running process keeps its current channel until restart.

Damaged caches are rejected and can be downloaded again. A damaged active
profile blocks online sign-in. The updated Windows and Linux installers
repair the runtime/profile together after verifying the payload. Do not
rename loaded DLLs or edit/delete the active profile to switch channels.

## Publish a stable bridge first

Players need a stable build containing the command, cache switcher and menu
toggle before opting in. Returning to an older DLL without these controls
would remove the ability to switch back. Switchable manifests require
`channel_switch: 1`; their DLLs must contain `YULE_CHANNEL_SWITCH=1`. After
opt-in, updates that remove switching support are rejected.

The public service is still v17; this development tree is v18 with TLS.
Backport the updater and controls to the existing stable source while
preserving its v17 gameplay ABI and control connection behavior. Do not
publish this entire development tree as a v17-compatible bridge. Alternatively,
coordinate a tested stable client/server upgrade before launching beta.

The 1.932 bridge and 2.0.0 beta were published on 2026-09-30. Building future
release previews alone does not publish their files or change the server.

## Build a beta release

Use a unique numeric version for each changed payload. The builder requires
`update_ext.h`, the built DLL and installed DLL to agree. Example after
compiling a version declared as `2.0.1`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_release.ps1 `
  -Version 2.0.1 -ReleaseChannel beta `
  -OutDir dist/beta -Notes 'EOS and networking community test'
```

This produces `dist/beta/releases/beta.json` and
`dist/beta/releases/beta/2.0.1/`. Beta packaging does not overwrite stable
`latest.json` or installer archives. EOS builds automatically include the
runtime and restricted client credential. Backend secrets are not payloads.

Upload `/yule/releases/beta/2.0.1/` first, verify all payload URLs, sizes and
SHA-256 hashes, then publish `/yule/releases/beta.json` atomically. Never reuse
a version for different bytes. Stable packaging uses `-ReleaseChannel stable`
(the default) and its existing URLs.

For disposable localhost updater tests, `update_beta_url` overrides the beta
manifest URL; `update_channel_url` retains its stable meaning. Nonlocal
manifest and payload URLs require HTTPS. Players need neither setting.

## Beta server setup alongside `online_server`

Keep production running on 47778. These example paths describe a separate
service. Check the existing service's working directory and any isolated v18
listener already using 47782 before starting another process.

1. Put the matching tested beta source in a separate checkout, for example
   `/home/loaf/yule-beta/eggnoggplus-win/online_server`. Include
   `release_channel.js`, TLS support, EOS modules and v18 negotiation. Run
   `npm run check` and `npm test` there.
2. Create `/home/loaf/yule-beta-data`, owned by the service user, mode 0700.
   Keep `users.json`, `ratings.json` and `server_secret.key` there, outside
   the source tree. Beta startup refuses default production store paths.
3. To reuse Yule logins, take a consistent private copy of production
   `users.json` into the beta data directory before its first start. Preserve
   `account_id`, salts and password hashes. Do not generate new account IDs.
   Beta account/password/friend changes remain in beta; this is a snapshot,
   not live synchronization. Leave beta ratings and its secret absent so the
   server creates fresh ones. Never share a writable JSON store between Node
   processes.
4. Add a **DNS only** record for `beta.loafiieee.com` pointing at the public
   server address. Forward/allow **TCP 47782 and UDP 47782** to
   `192.168.0.143`. TCP carries TLS account/matchmaking traffic; UDP preserves
   native transport and relay. The ordinary Cloudflare HTTP proxy/tunnel
   does not carry this control socket.
5. Follow [TLS deployment](online_server/TLS_DEPLOYMENT.md), substituting
   `beta.loafiieee.com`, port 47782 and separate certificate paths. Obtain a
   publicly trusted certificate. Copy its chain/key into
   `/etc/eggnogg-beta/tls/` using the same root/service-group permissions and
   renewal-hook pattern. A Cloudflare Origin CA certificate does not work
   for the Windows game client.
6. Copy [beta.env.example](online_server/beta.env.example) into a private
   mode-0600 environment file. Add the configured EOS IDs, restricted client
   credential and `EOS_VERIFY_BINARY` from
   `/home/loaf/yule-eos-runtime/eos.env`. Keep credentials off Git and command
   lines. Do not inherit production Discord bot/admin settings.
7. During this migration, v17 stable does not issue EOS tokens. Beta can own
   the existing UserInfo listener on `127.0.0.1:47781`, reached through
   `auth.loafiieee.com`. Stop the old isolated test listener/SSH reverse
   forward occupying that port before handing it to beta. UserInfo must
   run in the process that issues its tokens: they are stored in memory.
   Verify Connect login and signed PUID proof using a beta account. If
   stable later also uses EOS, a shared UserInfo broker/router is needed
   before both processes issue tokens. Pointing the provider at just one
   process would reject the other's tokens. Retain the existing provider
   and stable account `sub` mapping; no Epic login is needed.

Example `/etc/systemd/system/eggnogg-beta.service`:

```ini
[Unit]
Description=Yule beta online server
After=network-online.target
Wants=network-online.target

[Service]
User=loaf
Group=loaf
WorkingDirectory=/home/loaf/yule-beta/eggnoggplus-win/online_server
EnvironmentFile=/home/loaf/yule-beta-data/beta.env
ExecStart=/usr/bin/node server.js
Restart=on-failure
UMask=0077

[Install]
WantedBy=multi-user.target
```

After confirming paths and resolving old test listeners:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now eggnogg-beta.service
python3 /home/loaf/yule-beta/eggnoggplus-win/online_server/check_deployment.py \
  127.0.0.1 47782 --server-name beta.loafiieee.com
```

Also probe `beta.loafiieee.com 47782` from outside the server network and verify
`server_info.release_channel` is `beta`. Stable/legacy login attempts must
receive the channel error. A beta client pointed at stable must reject its
channel before submitting credentials.

## Before inviting testers

- Stable bridge working against the stable service.
- Beta DNS/TLS/ports, account snapshot, EOS Connect and v18 server verified.
- First beta download, stable return and cached beta return tested in game.
- Installer repair and interrupted update recovery verified.
- Two-machine native, EOS auto and forced-relay game tests pass.

The remaining Asia-to-Asia and US-to-Spain production acceptance tests still
apply. To pause new beta updates, retain the last working manifest and payload.
Players can return to cached stable. Stop only `eggnogg-beta.service` if beta
needs to be taken offline.
