# EOS gameplay transport migration

Status: the v18 packet layout, native transport extraction, EOS Connect/P2P
development implementation, matchmaking identity proof, and transport policy
are in place. The restricted EOS client and `Yule Account` OpenID UserInfo
provider are configured in the Live sandbox. A live EOS Connect login and
server-side ID-token verification passed using the staged server; a
cross-account PUID substitution was rejected. A Windows local-server test also
passed Yule login, public UserInfo, EOS Connect, and server-side PUID proof.
The v18 server currently runs locally for testing. Normal player rollout remains
gated on deployment of the tested TLS client/server, gameplay tests, and separate
internet-network validation. Do not deploy the v18
server alone: its version gate rejects v17 clients.

## Boundaries

Only the datagram carrier changes. Yule keeps its rollback, input redundancy,
ACKs, authenticated `send_tick`/`tick_echo` RTT, state sync, corrections,
checksums, HMAC, replay window, session IDs, and build/layout validation. Native
UDP and the existing server relay stay available.

The final policy is `auto` (EOS first when both authenticated clients advertise
EOS and Connect login succeeds), `eos` (require EOS), or `native` (require native
UDP). No match may switch carrier after matchmaking without a new server-approved
match. The server must reject disjoint capability sets before `match_found`.

## Wire audit

The packed v17 normal packet was 1292 bytes. One
`GgpoNetPacketStateSummary` is exactly 148 bytes: 4 bytes for its frame and
144 bytes for `LuaGameStateRollbackSummary`. Reducing two diagnostic summaries
to one yields an exact **1144-byte v18 packet**. Checksums remain a separate
32-entry array and are still responsible for desync detection. The 64 input
entries, 512-bit selective input ACK, cumulative checksum ACK, correction
control, and authentication prefix are unchanged.

| Packet type | Maximum bytes | EOS limit 1170 |
| --- | ---: | --- |
| INPUT, HELLO, BYE, state ACK, resync request, correction control | 1144 | fits |
| State chunk, including initial and correction state | 1004 | fits |
| Palette preference | 72 | fits |
| Generic cosmetic asset chunk | 1041 in v17 | removed in v18 |
| Generic cosmetic profile | 2116 in v17 | removed in v18 |

The generic cosmetic packet and API paths were already disabled and have now
been removed. The distinct palette preference exchange is active and remains
part of prematch setup. Every remaining P2P packet type has a compile-time
assertion against the 1170-byte EOS limit.

## Native extraction checkpoint

`ggpo_transport.h` defines a tagged peer identity. Native identities contain an
IPv4 address and UDP port; EOS identities contain a PUID, socket name, and
channel. `ggpo_transport_native.c` owns Winsock startup, socket creation,
resolution, candidate endpoints, cached probe endpoint, send, receive, and
closure. The `ggpo_net` packet handler sees only the tagged peer, and still
verifies HMAC and replay state before accepting a different peer source or
session. The matchmaking server and `hooks.c` still drive the native
direct-to-relay retry generations; EOS matches bypass that path.

## Implementation and remaining validation

1. Use the pinned EOS C SDK **1.18.1.2-CL47370208** for the 32-bit build. It
   contains `EOSSDK-Win32-Shipping.dll`, `EOSSDK-Win32-Shipping.lib`, and
   headers defining `EOS_P2P_MAX_PACKET_SIZE` as 1170. The Portal's 1.19.0.3,
   1.19.0.7, 1.19.1.2, and 1.19.2.1 packages omit the Win32 EOS pair. Bind
   packet assertions to the pinned SDK constant. A compile/link probe with
   `gcc -m32` and the pinned Win32 import library has passed.
2. The Portal UserInfo provider and stable account mapping have been tested
   through live Connect Login/CreateUser and signed ID-token verification.
   The server accepts a PUID only when the verified OpenID account ID equals
   the authenticated Yule account ID.
3. EOS platform ticking, P2P send/receive, bounded queues, connection
   notifications, relay policy, and unreliable gameplay delivery are behind
   the transport interface. The server negotiates capabilities separately
   from v18 and hands the matched PUID/socket/channel to each client.
4. Finish authenticated TLS for Yule's control channel. Then run two Windows
   clients through prematch, state/correction transfer, interruption, and
   disconnect tests across two real networks. Measure native, EOS automatic,
   and EOS forced relay for Asia-to-Asia and US-to-Spain. Production `auto`
   must wait for those results.

## Connect identity design and required validation

The selected credential type is Connect's **OpenID provider access token**
(`EOS_ECT_OPENID_ACCESS_TOKEN`) passed to `EOS_Connect_Login`, followed by
`EOS_Connect_CreateUser` only for `EOS_InvalidUser`. After normal Yule account
authentication, the backend issues a short-lived bearer token for Connect. The
token is an opaque 32-byte random bearer credential. The Portal's OpenID
UserInfo provider must query `GET https://auth.loafiieee.com/eos/userinfo`
with `Authorization: Bearer <token>`. The endpoint returns JSON fields `sub`
(immutable account ID) and `nickname` (display name), and rejects expired or
unknown tokens. The UserInfo request format passed a live EOS Connect test.
The verified account ID
must be an immutable Yule account ID, not the username or a device-installation
ID. Token verification, any signing key, and provider management credentials
stay on the backend. The client never receives password hashes or
`online_server/server_secret.key`.

The `online_server` account store is keyed by username. `storage.js` now assigns
each legacy record a stable random `account_id` at startup and each new record
one at registration. Malformed or duplicate IDs stop startup. The OpenID
provider returns that ID as `sub`; usernames remain display and login names.
`eos_connect_token_request` requires an authenticated control session and
returns a three-minute token only when `EOS_USERINFO_PORT` is configured.
The UserInfo listener binds to `127.0.0.1` by default and must be exposed
through a dedicated HTTPS route, never directly through the public firewall.

The client should send its EOS Connect ID token to the already authenticated
Yule control connection. The backend must verify the EOS signature, issuer,
audience, expiry, and PUID, and confirm that EOS maps that PUID to the same
OpenID provider account ID as the authenticated immutable Yule account. The
server uses `EOS_Connect_VerifyIdToken` and checks the returned OpenID account
ID against the authenticated Yule account. A staged live test proved this
binding and rejected another account's PUID.
`match_found` should carry `transport`, the selected opponent's verified PUID,
and a match-specific EOS P2P socket name/channel, as well as the existing
`p2p_auth_token`. The local client must accept EOS packets only from that PUID
on that socket/channel, then apply Yule's HMAC/replay/session checks as usual.

The OpenID UserInfo flow and server verification passed with the Live
deployment on September 28, 2026. Device ID is never the Yule identity.

## Developer Portal and server setup

1. Create an EOS product. For a Game Services-only product, the Portal may leave
   the Dev sandbox disabled; use the existing Live sandbox / Live Deployment
   for initial Connect and P2P testing. This does not require Epic Games Store
   publishing. Use a separate private Dev sandbox if Epic makes one available
   later. Create a custom game-client policy with **User Required**. EOS P2P
   transport has no separate feature/action in the Portal policy list; keep
   unrelated actions (Lobbies, Sessions, Anti-Cheat, storage, etc.) disabled.
   The Yule Portal currently shows Product ID
   `a90d288fedff4672b082ed3e92a12484`, Live Sandbox ID
   `02dd8fe04e294815881f54986c73629a`, and Live Deployment ID
   `746ee96def484dcc8bbc9806343352b2`. The custom policy named
   `Yule P2P Transport` has User Required enabled and all feature actions
   disabled. The `Yule Windows Client` uses that policy; its Client ID is
   `xyza7891PRKu95tnw9S2svEZhSaWbVkn`. The Client Secret remains masked in
   the Portal and is not recorded in this file. Its trusted-server IP allow
   list and EAS redirect URL are blank.
   Live Connect login passed with these policy settings. Do not use a
   TrustedServer policy in the client. Do not enable
   unrelated Epic Account Services or Store features just to unlock Dev.
2. The OpenID **UserInfo Endpoint** provider is configured. Its Description is
   `Yule Account`, UserInfo API
   Endpoint to `https://auth.loafiieee.com/eos/userinfo`, HTTP Method to
   `GET`, AccountId field to `sub`, and DisplayName field to `nickname`.
   The existing `loaf-tunnel` on `loaf-server1` is healthy but **locally
   managed**: Cloudflare's dashboard cannot edit its routes. Add the `auth`
   hostname to its local ingress config, targeting the loopback UserInfo
   listener, then add the matching Cloudflare DNS tunnel record. Verify the
   public endpoint returns 401 without a bearer credential; this was verified
   before saving the EOS provider. It is associated with the Live sandbox.
   Keep provider management credentials out of the repo.
   The server's working SSH endpoint from this LAN is
   `loaf@192.168.0.143:22`; `eggnogg.loafiieee.com:22` reaches a different
   SSH endpoint. The tunnel config is `/etc/cloudflared/config.yml`, owned by
   root, and the route should target `http://localhost:47781`. Passwordless
   sudo is unavailable, so the operator must perform any privileged config
   install/reload step locally; never transmit the sudo password to the
   client or in chat.
3. Download the Windows **C SDK**, confirm it contains an x86/Win32 import
   library, headers, and `EOSSDK-Win32-Shipping.dll` compatible with this
   repository's `gcc -m32` build. The selected archive is
   `C:\Users\potato\Downloads\EOS-SDK-47370208-Release-v1.18.1.2.zip`.
   Keep the SDK archives outside the repository and do not mix binaries or
   headers from different releases.
4. Record Product ID, Sandbox ID, Deployment ID, GameClient Client ID, and its
   SDK client credential for this deployment. The SDK platform options require
   the client credential, so treat the GameClient credential as recoverable from
   the shipped `eos_client_secret.txt` and constrain it with Client Policy.
   Keep the file out of Git. It must be distinct
   from server signing keys and any TrustedServer credential.
   On this workstation the restricted credential is stored at
   `%LOCALAPPDATA%\EGGNOGG+\mods\eos_client_secret.txt`. The build copies it
   to the game directory when `EOS_CLIENT_SECRET_FILE` is supplied. Both files
   are ignored by Git. Example from MSYS2 in the repository root:

   ```bash
   EOS_SDK_DIR=/c/Users/potato/Downloads/eos-sdk-1.18.1.2-minimal \
   EOS_CLIENT_SECRET_FILE=/c/Users/potato/AppData/Local/EGGNOGG+/mods/eos_client_secret.txt \
   ./compile.sh
   ```

   This puts `EOSSDK-Win32-Shipping.dll` and `eos_client_secret.txt` beside
   the game executable. The release builder detects EOS-enabled `SDL2.dll`,
   requires both runtime files, and adds them to the update manifest. The
   GameClient credential is recoverable by players; only the restricted client
   policy makes shipping it appropriate. Never put the Yule server secret or a
   TrustedServer credential in this file.
   Existing installers download every entry in `latest.json.files`, including
   newly introduced DLLs. The in-game updater does so only for a newer framework
   version: increment `FRAMEWORK_VERSION`, rebuild, and regenerate the release
   manifest rather than adding files under an unchanged version. Upload the
   versioned payloads first and publish `latest.json` last.
   The Win32 EOS runtime also requires the x86 Microsoft Visual C++ runtime
   (`MSVCP140.dll` and `VCRUNTIME140.dll`). Yule loads dependencies from the game
   directory and Windows system directory, and resolves the SDK's Win32 stdcall
   export names. Run `powershell -File tests/run_eos_runtime_test.ps1 -SdkDir ...`
   to check Windows platform creation, all required Connect/P2P exports, and
   shutdown without signing in or launching the game.
5. After control-channel TLS and two-machine tests pass, deploy the updated `online_server`
   and matching v18 client release together. Keep the existing TCP listener and
   native UDP probe/relay listener during migration. Keep `DB`, `RATINGS`, and
   `SECRET_FILE` at their current private paths. Do not copy or regenerate the
   current `server_secret.key` for EOS.
6. The server accepts EOS capability only after verified PUID registration.
   Matchmaking selects EOS when both clients support it and includes the
   bound peer identity in `match_found`. Native-only matches continue through
   the current probe and relay path.

No firewall or DNS change for the existing Yule TCP/UDP control service is
needed for the native fallback. The OpenID UserInfo endpoint already has its
dedicated public HTTPS route; its origin listener is loopback only.

The development client/server now implement authenticated TLS for the control
channel. Windows Schannel verifies the certificate chain and hostname; Node
requires a certificate for public listeners. The live v17 service remains raw
TCP until a coordinated rollout. See `online_server/TLS_DEPLOYMENT.md`; deploy
TLS before releasing EOS account bootstrap to normal players.

### Rollout of the existing `online_server` service

Do **not** deploy this intermediate v18 server by itself. When the TLS-enabled
client/server and two-machine tests are ready, use a maintenance window to
distribute matching versions together. Keep `eggnogg.service`, TCP port 47778,
and UDP port 47778 for native fallback. The game release contains the Win32
EOS SDK DLL and restricted GameClient credential. The Node server uses the
matching Linux SDK runtime only for `EOS_Connect_VerifyIdToken`.

The isolated v18 staging instance is prepared for loopback TCP/UDP 47782 and
UserInfo 47781. It is currently stopped for local Windows tests. The public
`auth.loafiieee.com` tunnel targets 47781, which an SSH reverse forward now
connects to the local Windows UserInfo listener. The local game server remains
at `localhost:47778`. See `online_server/README.md` for local startup and tunnel
commands. The live v17 service on `loaf-server1` remains on 47778 and does not
load EOS settings.
Before starting the live v18 service with UserInfo port 47781, stop that SSH
reverse forward. If the isolated stage has been restarted, also stop its
`node server.js` process **after verifying its cwd is**
`/home/loaf/yule-eos-build/yule-eos-server-stage`; do not stop the live
`/home/loaf/Yule/eggnoggplus-win/online_server` process by mistake.

The prepared private server runtime is `/home/loaf/yule-eos-runtime/`:
`eos_verify_token`, the matching `libEOSSDK-Linux-Shipping.so`, and `eos.env`
(mode 0600). `eos.env` defines `EOS_PRODUCT_ID`, `EOS_SANDBOX_ID`,
`EOS_DEPLOYMENT_ID`, `EOS_CLIENT_ID`, `EOS_CLIENT_SECRET`,
`EOS_VERIFY_BINARY`, `EOS_USERINFO_HOST=127.0.0.1`, and
`EOS_USERINFO_PORT=47781`. Keep this file private. It contains the restricted
GameClient credential, not the Yule server HMAC key. In the rollout maintenance
window, add this drop-in with `sudo systemctl edit eggnogg`:

```ini
[Service]
EnvironmentFile=/home/loaf/yule-eos-runtime/eos.env
```

Run `sudo systemctl daemon-reload` before restarting. The existing
`/etc/eggnogg/lfg.env` remains loaded. Do not enable this drop-in while the
v17 service is live; its UserInfo listener would conflict with staging.

Before the coordinated rollout, stop the service and back up the current
account, ratings, and secret files using the existing offline tool and the
same `DB`, `RATINGS`, and `SECRET_FILE` paths as the service:

```bash
cd /home/loaf/Yule/eggnoggplus-win/online_server
sudo systemctl stop eggnogg
node maintenance.js backup /private/backups/yule-before-eos --offline
node maintenance.js verify /private/backups/yule-before-eos
sudo systemctl start eggnogg
```

The private backup parent must already exist outside the application tree;
choose a new snapshot directory for the command.
When the coordinated release is published, ensure its v18 source has been
committed to the repository branch used by `update_server.sh` (the updater
clones the branch; it does not use this local uncommitted workspace). Run
`./update_server.sh` from the server directory and then
`python3 check_deployment.py` against the public host.
The updater preserves `users.json`, `ratings.json`, `server_secret.key`, logs,
and local environment files. The `auth.loafiieee.com/eos/userinfo` endpoint
should again return 401 without a bearer token after the live server starts.
Do not replace the service with a second `node server.js` process. Keep the
native relay running throughout EOS validation.

## Test and release gate

### Local gameplay regression results (29 September 2026)

The GAME update now services EOS and the Yule control channel while simulation
is running or waiting for input. The transport's service operation also ticks
EOS, so standalone GGPO callers do not depend on menu callbacks. Rollback replay
uses the native trampoline and never runs the control pump. The executable
`tests/gameplay_service_test.py` exercises the actual gameplay hook through
3,600 updates, blocked ticks, prematch, and a callback that switches to the hub.

Yule sign-in starts EOS Connect automatically. A queue request made during
bootstrap waits and proceeds after server PUID verification in both `auto` and
`eos`; Cancel clears that pending request. Errors/timeouts give `auto` its native
fallback and keep `eos` fail-closed. A new account sign-in releases an idle EOS
platform to prevent reuse of the previous account's PUID.

The guarded opt-in runner `tests/eos_gameplay_live_test.py --live` uses two
distinct local Yule accounts/PUIDs and the production GGPO protocol/transport
with a deterministic state fixture. It never launches `eggnoggplus.exe`.
Completed cases:

| EOS routing | Case | Result |
| --- | --- | --- |
| Forced relay | 2,048 confirmed frames, 20% simulated loss, delay and bilateral replay | Pass, all confirmed state bytes identical, 75.4 seconds |
| Auto (EOS reported direct) | Same gameplay/history test | Pass, 74.4 seconds |
| Forced relay | Correction after ring reuse, state transfer and backpressure | Pass, matching corrected checksum, 83.3 seconds |
| Forced relay | Authenticated disconnect during gameplay | Pass, 69.0 seconds |
| Forced relay | Tampered HMAC payloads | Pass, rejected before peer/session adoption |

This verifies actual Win32 EOS networking and the Yule protocol, including
prematch, palette, initial state, inputs, ACKs, checksums, replay and correction.
It does **not** reproduce the full game simulation, rendering, distinct ISP
conditions or an actual EOS interruption event. Those remain acceptance gates.
The complete native prematch suite passed after the service change. The guarded
core/serializer/static suite passed as well. Concurrent game instances now use
separate logs: the first owns `mods/modframework.log`, further instances write
`mods/modframework.<pid>.log` instead of truncating each other's evidence.

Custom UI embeds unmodified Px437 Tandy2K from the Oldschool PC Font Pack v2.2
(VileR, CC BY-SA 4.0), as requested. A 2x source atlas allows compact intermediate
sizes without forcing body text to 2x or headings to 3x. Menu advances use actual
glyph ink bounds and a consistent gap; the console retains the original eight
pixel fixed cells. Whole-pixel sizes use nearest sampling and intermediate sizes
use filtered sampling. No synthetic bold or aspect stretching is applied. Text
measurement, centering and console cursor positions use the same metrics. Body,
caption and heading sizes follow a shared responsive scale, capped on large
windows. No OS font installation is needed. The hub uses a smaller centered
panel, aligned header/tabs and row centers, and title-case labels; network
settings are collapsed under Advanced settings and transport details remain
developer diagnostics. Login no longer has a second nested frame or displays
the raw server endpoint. Settings omit redundant headings and the default ready
message. Rendering, keyboard scrolling and row hit testing share the same list
origin. `tests/run_ui_text_test.ps1` renders actual hub, login, matchmaking,
console and mod manager code in a hidden test window at 960, 1280 and 1920
widths. It checks GL state
restoration, private font loading and measured alignment. Its PNGs are visual
fixtures, not screenshots of a running game. Release manifests include
`Tandy2K-LICENSE.txt` and `Tandy2K-ATTRIBUTION.txt`; the font bytes are already
inside `SDL2.dll`.

The guarded core/native suite and prematch rollback suite pass, including the
native direct/relay impairment, correction, disconnect, authentication, and
replay cases. The EOS-enabled
Win32 build succeeds with compile-time packet bounds; the release dry run
contained the Win32 EOS runtime and restricted client credential. The Node
server suite passes 41 tests, and its match protocol test passes. A staged
Live-deployment Connect test verified the account-to-PUID binding and rejected
cross-account substitution. A separate two-process Linux EOS SDK smoke test
exchanged unreliable P2P packets on both an EOS-reported direct route and an
EOS-reported forced relay route. That test found a relay negotiation failure
when sends disabled EOS auto-accept; Yule's EOS transport now permits
auto-accept only for the server-matched PUID/socket, while the incoming peer
filter and Yule HMAC/replay checks remain mandatory.
The client now listens for EOS Connect auth-expiration and login-status events.
It obtains a fresh short-lived Yule bearer token and reauthenticates the same
PUID before expiry. If EOS reports logout, the client withdraws its verified
EOS capability from the Yule server and starts a new Connect login; an active
gameplay session still relies on Yule's own authenticated packet state.

A development client payload is at
`build/eos_dev_client_v18_20260928.zip` with a file hash manifest and test
instructions. It is for a separate test installation with a matching v18
server. It is **not** a published release, and the public v17 server will
reject it.

Still required before normal player rollout: deploy the tested TLS control
channel and verify its public certificate, two Windows game clients testing every EOS-carried packet
class, HMAC tampering/replay, prematch and correction transfers, queue bounds,
interruption and disconnect, capability mismatch, and `auto`/`eos`/`native`
behavior. Run native, EOS automatic, and EOS forced relay for the problematic
Asia-to-Asia match and a known-good US-to-Spain match. Record gameplay RTT,
rollback count, input delay, corrections, disconnects, and route only when EOS
exposes it reliably. Production `auto` remains gated on those results.

## EOS references

- [Connect interface API](https://dev.epicgames.com/docs/api-ref/interfaces/connect)
- [P2P interface API](https://dev.epicgames.com/docs/api-ref/interfaces/p-2-p)
- [EOS product and SDK credentials](https://dev.epicgames.com/docs/dev-portal/product-management)
- [EOS relay policy API](https://dev.epicgames.com/docs/api-ref/functions/eos-p-2-p-set-relay-control)
