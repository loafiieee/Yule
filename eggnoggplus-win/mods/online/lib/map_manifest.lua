map_manifest = map_manifest or {}

local VANILLA_COUNT = 5
local cached_manifest = nil
local cached_serial = nil

local function norm(s)
    s = tostring(s or "")
    s = s:gsub("^%s+", "")
    s = s:gsub("%s+$", "")
    return s:lower()
end

local function safe_read(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read("*a")
    f:close()
    return data
end

local function weak_hash(text)
    local h = 2166136261
    local modp = 4294967291
    for i = 1, #text do
        h = (h * 16777619 + text:byte(i)) % modp
    end
    return string.format("%08x", h)
end

local function list_map_dirs()
    local dirs = {}
    local p = io.popen('dir /b /ad "maps" 2>nul')
    if not p then
        p = io.popen('ls -1 "maps" 2>/dev/null')
    end
    if not p then return dirs end
    for line in p:lines() do
        local name = tostring(line or "")
        name = name:gsub("^%s+", "")
        name = name:gsub("%s+$", "")
        if name ~= "" and name ~= "." and name ~= ".." then
            dirs[#dirs + 1] = name
        end
    end
    p:close()
    table.sort(dirs, function(a, b) return norm(a) < norm(b) end)
    return dirs
end

local function build_manifest()
    local items = {}
    local serial_parts = {}

    for i = 0, VANILLA_COUNT - 1 do
        items[#items + 1] = {
            key = "vanilla:" .. tostring(i),
            selector = i,
            label = "Vanilla " .. tostring(i + 1),
            kind = "vanilla",
        }
        serial_parts[#serial_parts + 1] = "v" .. tostring(i)
    end

    local custom = {}
    for _, dir in ipairs(list_map_dirs()) do
        local base = "maps/" .. dir
        local json_text = safe_read(base .. "/data.json")
        local map_text = safe_read(base .. "/data.map") or ""
        if json_text then
            local meta = nil
            local ok, decoded = pcall(function()
                return json.decode(json_text)
            end)
            if ok then meta = decoded end
            if type(meta) == "table" then
                local id = norm(meta.id ~= nil and meta.id or dir)
                if id ~= "" then
                    local name = tostring(meta.name or id)
                    local sort_order = tonumber(meta.sort_order) or 100
                    local sig = weak_hash(json_text .. "\n--MAP--\n" .. map_text)
                    custom[#custom + 1] = {
                        id = id,
                        name = name,
                        sort_order = sort_order,
                        sig = sig,
                    }
                end
            end
        end
    end

    table.sort(custom, function(a, b)
        if a.sort_order ~= b.sort_order then
            return a.sort_order < b.sort_order
        end
        local an = norm(a.name)
        local bn = norm(b.name)
        if an ~= bn then return an < bn end
        return a.id < b.id
    end)

    for idx, m in ipairs(custom) do
        local selector = VANILLA_COUNT + (idx - 1)
        local key = "custom:" .. m.id .. ":" .. m.sig
        items[#items + 1] = {
            key = key,
            selector = selector,
            label = m.name,
            kind = "custom",
            id = m.id,
            sig = m.sig,
        }
        serial_parts[#serial_parts + 1] = key .. "@" .. tostring(selector)
    end

    cached_manifest = items
    cached_serial = table.concat(serial_parts, "|")
    return items
end

function map_manifest.get()
    if cached_manifest then return cached_manifest end
    return build_manifest()
end

function map_manifest.count()
    return #(map_manifest.get() or {})
end

function map_manifest.selector_for_key(key)
    local want = norm(key)
    if want == "" then return nil end
    for _, item in ipairs(map_manifest.get() or {}) do
        if norm(item.key) == want then
            return tonumber(item.selector)
        end
    end
    return nil
end

function map_manifest.serial()
    if not cached_manifest then build_manifest() end
    return cached_serial or ""
end

function map_manifest.invalidate()
    cached_manifest = nil
    cached_serial = nil
end

function map_manifest.send()
    local maps = map_manifest.get()
    if not maps or proto.get_state() ~= "connected" then
        return false
    end
    return proto.send({
        type = "map_manifest",
        maps = maps,
        serial = map_manifest.serial(),
    })
end
