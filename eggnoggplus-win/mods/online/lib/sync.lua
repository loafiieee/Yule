-- lib/sync.lua
-- Backend selector for online sync.
--
-- Chooses rollback/prediction when the runtime exposes the required gameplay
-- APIs. Falls back to the legacy relay backend when the shipped executable does
-- not actually expose rollback hooks to Lua.

sync = sync or {}

local backend = nil
local backend_name = "none"
local backend_warning = nil
local last_error = nil

local function has_fn(tbl, key)
    return type(tbl) == "table" and type(tbl[key]) == "function"
end

local function probe_caps()
    local g = mod and mod.game or nil
    return {
        set_input       = has_fn(g, "set_input"),
        input_clear     = has_fn(g, "input_clear"),
        block_raw_input = has_fn(g, "block_raw_input"),
        snapshot        = has_fn(g, "snapshot"),
        apply_snapshot  = has_fn(g, "apply_snapshot"),
        simulate_ticks  = has_fn(g, "simulate_ticks"),
        sword_snapshot  = has_fn(g, "sword_snapshot"),
        apply_sword_snapshot = has_fn(g, "apply_sword_snapshot"),
    }
end

local function caps_string(c)
    return string.format(
        "set_input=%s input_clear=%s block_raw_input=%s snapshot=%s apply_snapshot=%s simulate_ticks=%s sword_snapshot=%s apply_sword_snapshot=%s",
        tostring(c.set_input),
        tostring(c.input_clear),
        tostring(c.block_raw_input),
        tostring(c.snapshot),
        tostring(c.apply_snapshot),
        tostring(c.simulate_ticks),
        tostring(c.sword_snapshot),
        tostring(c.apply_sword_snapshot)
    )
end

local function rollback_supported(c)
    return c.set_input and c.input_clear and c.block_raw_input
       and c.snapshot and c.apply_snapshot and c.simulate_ticks
end

local function relay_supported(c)
    return c.set_input and c.input_clear and c.block_raw_input
       and c.sword_snapshot and c.apply_sword_snapshot
end

local function call_backend(method, ...)
    if backend and type(backend[method]) == "function" then
        return backend[method](...)
    end
    if method == "get_error" then return last_error end
    if method == "stats" then
        return {
            mode = backend_name,
            backend_warning = backend_warning,
            error = last_error,
        }
    end
    return nil
end

local function choose_backend()
    local c = probe_caps()
    mod.log("[sync] API probe: " .. caps_string(c))

    if rollback_supported(c) then
        backend = mod.dofile("lib/sync_rollback.lua")
        backend_name = "rollback"
        backend_warning = nil
        mod.log("[sync] using rollback backend")
        return true
    end

    if relay_supported(c) then
        backend = mod.dofile("lib/sync_relay.lua")
        backend_name = "relay"
        backend_warning = "Rollback hooks missing in runtime; using compatibility relay backend."
        mod.warn("[sync] " .. backend_warning)
        return true
    end

    backend = nil
    backend_name = "none"
    backend_warning = nil
    last_error = "Required online sync APIs are unavailable."
    mod.warn("[sync] " .. last_error)
    return false
end

function sync.start(local_player_index, auth_role, seed)
    last_error = nil
    if not choose_backend() then
        return false
    end

    local ok = backend.start(local_player_index, auth_role, seed)
    if not ok then
        last_error = (type(backend.get_error) == "function" and backend.get_error()) or last_error
    else
        last_error = nil
    end
    return ok
end

function sync.stop()
    if backend and type(backend.stop) == "function" then
        backend.stop()
    end
    backend = nil
    backend_name = "none"
end

function sync.is_active()
    return backend ~= nil and type(backend.is_active) == "function" and backend.is_active() or false
end

function sync.get_error()
    if backend and type(backend.get_error) == "function" then
        return backend.get_error() or last_error
    end
    return last_error
end

function sync.apply_pending()
    return call_backend("apply_pending")
end

function sync.tick()
    return call_backend("tick")
end

function sync.frame()
    return call_backend("frame")
end

function sync.on_message(msg)
    return call_backend("on_message", msg)
end


function sync.mode()
    return backend_name
end

function sync.stats()
    local s = call_backend("stats") or {}
    s.mode = backend_name
    s.backend_warning = backend_warning
    return s
end

return sync
