# EOS beta deployment and owner testing

Updated on 2026-09-30. Stable **1.932** and opt-in beta **2.0.0** are public.
This file retains its original name from the private test setup.

## Player update channels

| Channel | Manifest | Runtime | Matchmaking |
| --- | --- | --- | --- |
| Stable | `https://loafiieee.com/yule/releases/latest.json` | 1.932, native v17 with channel switching | Existing stable server, port 47778 |
| Beta | `https://loafiieee.com/yule/releases/beta.json` | 2.0.0, EOS/native v18, TLS, Tandy2K UI and drop shadows | `beta.loafiieee.com:47782`, TLS |

Players first update to 1.932. To opt in:

```text
update.channel beta
```

Return to the main menu and allow the verified install and automatic restart.
After opting in, Mod Options → Framework → Beta testing provides the same
switch. To return to stable:

```text
update.channel stable
```

Finish any match before switching. The runtime and active profile change
together. Verified channel caches avoid downloading the same runtime on each
switch. Do not rename loaded DLLs or edit the active profile to switch channels.

Beta automatically signs in to EOS Connect after normal Yule sign-in. Epic
account login is not required. `transport=auto` chooses EOS when both clients
have verified EOS identities and capabilities; native v18 remains available.

Beta has a separate account store. Registrations, password changes and friend
changes are not automatically synchronized with stable. The beta started with
a copy of 125 production accounts. Players who registered later can register
in beta through its normal account UI.

## Your installed and development copies

Both copies now use the public manifest URLs:

```ini
update_channel_url=https://loafiieee.com/yule/releases/latest.json
update_beta_url=https://loafiieee.com/yule/releases/beta.json
```

The installed copy is `%LOCALAPPDATA%\EGGNOGG+`; the other is this development
directory. At publication, both were verified beta 2.0.0 installations. Their
channel caches and profiles were verified and refreshed to public download
URLs. Backups of the previous owner configuration are under
`build/private_beta/public-channel-owner-config-backups/`.

The localhost preview under `build/private_beta/preview/releases/` remains an
optional development fixture. Normal player downloads do not use it.

For transport comparisons:

```text
ggpo.net transport auto
ggpo.net eosrelay auto
ggpo.net eosrelay force
ggpo.net transport native
```

Restore `transport auto` and `eosrelay auto` after comparisons. Gameplay ping
continues to come from Yule's authenticated packet RTT. Route diagnostics use
the route EOS actually reports.

## Server configuration

| Component | Location / setting |
| --- | --- |
| Beta service | `eggnogg-beta.service`, enabled at boot, runs as `loaf` |
| Beta application | `/home/loaf/yule-beta/eggnoggplus-win/online_server` |
| Beta data and environment | `/home/loaf/yule-beta-data/`, mode 0700 |
| Beta control | TLS TCP 47782 |
| Beta native discovery/relay | UDP 47782 |
| EOS UserInfo | Loopback TCP 47781 in the beta Node process |
| Public UserInfo route | `https://auth.loafiieee.com/eos/userinfo` |
| EOS verification runtime | `/home/loaf/yule-eos-runtime/` |
| Beta certificate copies | `/etc/eggnogg-beta/tls/`, key root:loaf mode 0640 |
| Certificate renewal hook | `/etc/letsencrypt/renewal-hooks/deploy/yule-beta-tls` |
| Beta DNS | DNS-only CNAME `beta.loafiieee.com` → `eggnogg.loafiieee.com`, TTL 300 |
| Dynamic IP updates | Existing `cloudflare-dns-update.timer` updates the target A record |
| Server firewall | TCP/UDP 47782 allowed for internet clients |
| Router forwarding | Enabled TCP + UDP 47782 → `192.168.0.143:47782`; saved by the owner |

The UserInfo listener remains loopback-only and uses the existing Cloudflare
tunnel ingress in `/etc/cloudflared/config.yml`. No new tunnel ingress, Nginx
route or management port was needed. The public game hostname uses DNS only;
the client reaches the TLS matchmaking service directly.

Production `eggnogg.service` continues to run from
`/home/loaf/Yule/eggnoggplus-win/online_server` on port 47778 with its existing
v17 accounts, ratings, Discord integration and native relay. Publication did not
restart production, beta, Nginx, cloudflared or the website.

## Inspect or update the beta server

Connect using your existing SSH access to `loaf@192.168.0.143`, then inspect:

```bash
sudo systemctl status eggnogg-beta.service --no-pager
sudo journalctl -u eggnogg-beta.service -n 50 --no-pager
python3 /home/loaf/yule-beta/eggnoggplus-win/online_server/check_deployment.py \
  beta.loafiieee.com 47782
```

For a future beta application update:

1. Prepare and test the matching beta source separately. Run its Node checks and
   tests before deployment.
2. Back up the current beta application and beta data. Preserve `users.json`,
   `ratings.json`, `server_secret.key`, `identity-migration-snapshot.json`, and
   the private environment files.
3. Install application code into
   `/home/loaf/yule-beta/eggnoggplus-win/online_server`. Do not replace its data
   with development databases or run the production updater against beta using
   its default path, service or Git ref.
4. Restart only the beta application:
   `sudo systemctl restart eggnogg-beta.service`.
5. Run the deployment check above and verify sign-in, EOS identity bootstrap,
   matchmaking, and gameplay with matching clients.
6. Publish the new, uniquely versioned beta payload folder first. Verify public
   HTTPS sizes and SHA-256 hashes, then replace `beta.json` atomically. Leave
   `latest.json` selecting the stable release.

The certificate renewal hook already copies renewed certificates and sends
SIGHUP to beta. DNS follows the existing dynamic IP updater through the CNAME.
Normal application updates do not need router, Nginx or cloudflared changes.

## Account identity preservation

All 125 imported account IDs and creation timestamps were checked against
`/home/loaf/yule-beta-data/identity-migration-snapshot.json`; they are unchanged.
All 127 beta account IDs, including the two test accounts, are unique.

Before refreshing the production snapshot or later rolling EOS into stable,
reconcile imported accounts using the retained IDs and original creation
metadata. Preserve the mapping; do not regenerate IDs or merge unrelated
accounts solely by username. Beta-only accounts remain separate.

When stable also issues EOS tokens, deploy a shared UserInfo broker/router
before enabling both services. Today's token registry belongs to the beta
process; it cannot validate another process's tokens.

## Published artifacts and verification

Public beta files are under
`https://loafiieee.com/yule/releases/beta/2.0.0/`. The release includes the EOS
Win32 runtime and the restricted GameClient credential that was approved for
client distribution. It contains no Yule server HMAC key, account database,
password hashes, TrustedServer credential or private signing key.

Stable installers are available at:

- `https://loafiieee.com/yule/EGGNOGG+_framework_installer_windows.zip`
- `https://loafiieee.com/yule/EGGNOGG+_framework_installer_linux.zip`

The itch.io uploads and existing website download button were not changed.

Publication checks passed:

- Exact public stable and beta manifest bytes and all payload sizes/hashes.
- Trusted TLS server information and UDP discovery through the public beta
  hostname from the owner's Windows client.
- TCP reachability from three external internet nodes.
- Public EOS UserInfo rejects unauthenticated requests with HTTP 401.
- Live EOS Connect/PUID bootstrap, EOS preference, native fallback, disjoint
  capability rejection and cross-account signed PUID rejection.
- Imported account identity preservation and owner runtime/cache verification.

External TCP probes and the owner public-hostname UDP check do not establish
gameplay quality across two different internet networks. The Asia↔Asia and
US↔Spain native/EOS/forced-relay comparisons remain acceptance gates before EOS
is promoted to normal stable gameplay. Stable remains native v17.

Local release candidates and receipts are under `build/private_beta/`.
Server publication receipts and backups are retained privately under
`/home/loaf/yule-beta-staging/stable-publication-1.932/` and
`/home/loaf/yule-beta-staging/beta-publication-2.0.0/`. Published version folders
are immutable; future byte changes need a new beta version.
