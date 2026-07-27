# Persistent Social Block/Mute Design

Date: 2026-07-26

## Goal

Replace the client-only block stub with durable, server-authoritative social controls that
are reachable and usable with mouse, keyboard, and controller. Blocking must protect every
server interaction without leaking the target's presence; muting must suppress notification
noise without silently hiding an actionable challenge. Friend presence should describe
useful queue/match state without trusting user-supplied text.

## Data and protocol

Each account has normalized, unique `blocked_users` and `muted_users` arrays. Entries must
be canonical existing usernames, cannot name the owning account, and persist in
`users.json`. Existing account records migrate to empty arrays through `ensureUserShape`.

The server advertises scalar `cap_social_controls:1` in both `server_info` and `auth_ok`.
The current client requires it before sending credentials. Control protocol remains v3:
the new message types are additive and old v3 clients safely ignore snapshot fields they
do not understand.

Client requests:

- `friend_mute { username, muted: 0|1 }`
- `friend_block { username }`
- `friend_unblock { username }`

Successful mutations return `social_update { action, username }`, followed by an
authoritative snapshot. `friend` and incoming `challenge` records carry `muted:0|1`.
The owner's block list uses `blocked_user { username }` and deliberately contains no Elo,
presence, endpoint, or other profile data.

Friend records also carry one server-derived `presence` value: `offline`, `online`,
`queue_casual`, `queue_competitive`, `match_setup`, or `in_match`. No client request can
set this value. The server refreshes the affected social snapshots on queue join/leave,
match creation, gameplay commit, result/cancel, and disconnect.

## Semantics

Mute is allowed only for a current, unblocked friend. It affects only the muting account.
An incoming challenge from that friend remains in the Friends inbox, with its selected map
and ordinary accept/decline behavior, but the client does not open the bottom-right
challenge toast.

Block is asymmetric storage with bilateral enforcement. Creating a block:

1. adds the target to the caller's block list and removes any caller-side mute;
2. removes friendship and stale mutes in both directions;
3. removes pending friend requests and challenges in both directions;
4. refreshes both online clients without explicitly telling the target why.

If either account blocks the other, friend request, friend acceptance, challenge-map
query, challenge creation, and challenge acceptance fail generically as unavailable or
expired. Casual and competitive matchmaking skip that pair, with `makeMatch` retaining a
defense-in-depth check. Existing committed matches are not interrupted by a social action.
Unblock removes only the block; it never recreates a friendship or request.

## Client UX and accessibility

A friend row opens a social menu through right-click, `C`, or controller X/Y. Online
friends offer Challenge, Mute/Unmute, Block, and Unfriend; offline friends omit Challenge;
blocked users offer only Unblock. Arrow/W/S or D-pad navigation wraps, Enter/Space/A
confirms, Escape/Back/B closes, mouse motion selects, and the wheel navigates. The footer
advertises the non-pointer shortcut.

Blocked rows are listed separately with a red treatment. Muted friend rows retain
online/offline presence and add a `muted` label. Mutations wait for the server's
`social_update` and snapshot instead of pretending a failed network write succeeded.
Queue, setup, and in-match states have distinct readable labels/colors. Challenge controls
are omitted for setup/in-match friends and both challenge endpoints enforce the same busy
rule server-side.

## Verification

- `node --check online_server/server.js`
- `python tests/online_server_match_protocol_test.py`
- `python tests/social_controls_static_test.py`
- `powershell -ExecutionPolicy Bypass -File tests/run_core_native_tests.ps1`
- `bash compile.sh`

The isolated server test covers durable mute state, muted-but-actionable challenges,
block-list privacy, pending challenge removal, generic rejection, blocked-pair queue
exclusion, non-restoring unblock, and queue/setup/committed/restored presence transitions.
Static coverage pins all server enforcement and lifecycle-refresh paths, the required
capability, client parsing/toast/presence behavior, and accessible context controls.
