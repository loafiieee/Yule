# Online Gameplay-Mod Classification and Suspension

**Date:** 2026-07-17  
**Status:** Implemented for framework-managed Lua mods; this is a determinism boundary, not a complete anti-cheat system  
**Primary code:** `lua_manager.c/.h`, online lifecycle and Mods UI in `hooks.c`

## Purpose

Rollback peers must not run local-only Lua that changes simulation state. The framework
therefore classifies gameplay-affecting mods from actual API use and temporarily suspends
them for the complete server-managed match window. Cosmetic/read-only mods remain active.

Classification is automatic. There is no trusted manifest checkbox that a mod author can
mislabel.

## Safety window

`lua_manager_online_suspend_begin` is called as soon as the server assigns a valid match,
before connection attempts, countdown initialization, or synchronized state capture.
Suspension remains active through retries, countdown, prematch synchronization, gameplay,
and result/disconnect cleanup. `lua_manager_online_suspend_end` runs only after those
paths have released the match.

Both calls are idempotent. Repeated begin calls reassert neutral native overrides rather
than briefly resuming mods. Suspension changes only in-memory runtime state; it does not
rewrite a mod's enabled flag or config and cannot leave a mod disabled on disk after a
crash.

## Classification policy

Each loaded mod has a sticky session-level `gameplay_affecting` flag and first reason.
It is set when the mod:

- registers `mod.on_tick` or `mod.on_tick_post`;
- calls any state-changing `mod.game` binding;
- registers bot/input/menu gameplay providers through those guarded bindings;
- begins/registers/commits a `mod.content` transaction; or
- changes the `delta_time` event value.

Read-only game queries, logging, ordinary drawing/UI, and callbacks that do not alter the
clock do not classify an owner by themselves.

Every mutating `mod.game` function is exported as an owner-aware closure. It marks the
calling owner before argument parsing or mutation. If that happens during the online
window, the same call returns `false, error` before touching native state and the owner is
suspended immediately.

Content operations are conservative because even a declarative tile's native fallback
can affect collision/update behavior. Starting a content transaction is enough to mark
the owner gameplay-affecting.

For `delta_time`, a cosmetic event handler may run with the real delta. If it changes the
value during online play, the framework restores the real delta before use, classifies
the owner, and stops further dispatch to it. Thus the detecting mutation never affects
the online clock.

## What suspension does

`mod_is_runtime_active` requires the owner to be enabled and not gameplay-suspended.
Suspended owners do not receive normal runtime dispatch, including:

- delta, tick, tick-post, frame, generic event, and key callbacks;
- config action callbacks;
- bot/input-provider and menu-mode activation; and
- layout/UI interaction paths that use runtime-active owner checks.

The framework also clears transient state at suspension entry:

- tick input and persistent input overrides for both players;
- raw-input blocks;
- player body-hidden and sword-offset overrides;
- queued block-next-tick state;
- armed AI match state; and
- manual time scale, restoring `1.0`.

For each suspended owner, bind pressed/down/released state and UI hitboxes are cleared,
and native buttons owned by that mod are detached. Resume requests a full layout refresh
so safe UI can be rebuilt from current state.

The Lua state and callback references remain loaded. Suspension deliberately does not
call `on_unload`/`on_load`, which could have gameplay side effects and would turn a
temporary safety boundary into a persistent lifecycle change.

## Escape-hatch guards

The less-obvious mutation paths are part of the boundary:

- enable/disable and manual/hot mod reload are locked while suspension is active;
- filesystem hot reload remains pending until the match ends;
- config mutation/actions, input binding mutation, storage mutation/migration/save,
  `mod.dofile`, and interop provide/require calls reject a suspended owner;
- console evaluation targeted at a suspended mod is rejected;
- cached interop service functions are wrapped with a provider runtime-active check, so
  retaining a table before matchmaking cannot bypass suspension;
- native button pointer actions and native state transitions are blocked for every mod,
  including cosmetic mods, during online play because they can indirectly leave/reset a
  match; and
- `on_unload` is never invoked for an owner while it is suspended.

Loaded-mod slots are reserved before executing mod chunks. Owner pointers captured by
Lua closures therefore remain stable even with more than the original small allocation
of mods; growing the array cannot leave cached closures pointing into freed memory.

## Mods UI behavior

The Mods page remains inspectable during online play. For a suspended owner it shows:

- `SUSPENDED (ONLINE)` in the header;
- the Enabled value as `SUSPENDED`;
- the recorded classification reason; and
- configuration and bind rows as non-selectable.

Toggle/adjust/action handlers also recheck suspension, so stale selection state cannot
perform a mutation after the rows rebuild.

## Failure policy and invariants

- A mutator discovered during online play must classify first and fail before mutation.
- Runtime dispatch snapshots must stop as soon as their owner becomes suspended.
- A cosmetic owner may not use native state-transition actions during the safety window.
- No reload or mod-set change may invalidate captured owner pointers mid-match.
- The online clock is always real-time scale `1.0`.
- Begin/end and abort/retry paths must never create a one-frame resume gap.
- Config files and enabled flags are unchanged by temporary suspension.

If classification cannot prove that a raw external action is safe, this layer does not
pretend it can. It governs framework-managed Lua API and callback paths only.

## Verification

`tests/lua_online_suspension_static_test.py` audits the coupled native-address code. It
asserts that:

- every exposed deterministic `mod.game` mutator is an owner-aware guarded closure;
- content begin/register/commit uses the gameplay guard;
- runtime callback dispatch checks owner activity;
- config/bind/storage/dofile/interop mutation paths have lifecycle guards;
- native pointer actions and state transitions fail closed online;
- unload, reload, enable/disable, console eval, and cached interop paths retain guards;
  and
- stable mod slots are reserved before closures are created.

Manual verification should run at least one tick/bot/speed mod and one cosmetic-only mod:

1. enter queue and observe gameplay mods suspend at assignment, before countdown work;
2. confirm cosmetic drawing continues;
3. attempt config, bind, console eval, cached interop, native action, and mid-match mutator
   calls;
4. open the Mods page and verify rows are locked and explained;
5. abort during each connection attempt and finish a normal match; and
6. verify owners resume once, configs are unchanged, input/body/time-scale overrides are
   neutral, and layout is rebuilt.

## Boundary and remaining work

This is not protection against another injected DLL, native code patches, a modified game
executable, debugger writes, LuaJIT FFI/raw process access, or a malicious server.

Server-managed matches do have two narrow compatibility gates outside this suspension
layer: the peers exchange the current non-cryptographic 32-bit game/framework build
fingerprints and abort before gameplay when they differ, and each client must resolve the
server-advertised nonempty `map_key` exactly. Those checks catch ordinary version or map
selection mistakes; because the identifiers are peer-reported and are not an integrity
proof, they are not anti-cheat.

The current flow does not yet fingerprint or enforce the enabled gameplay-mod set,
content-registry state, relevant config/rules, or every state-affecting asset between
peers.

It clears known transient framework overrides but cannot generically reverse every native
write a mod may have performed before matchmaking. The native match reset provides a
clean normal start, but production ranked play still needs a complete compatibility
manifest, explicit allowed-content policy, and stronger process/integrity controls.
