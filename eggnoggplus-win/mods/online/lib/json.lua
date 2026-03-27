-- lib/json.lua  –  minimal JSON encode/decode for flat string/number/boolean objects.
-- Sets the global `json` table in the mod environment.
-- Only handles objects one level deep – sufficient for our wire protocol.

json = json or {}

-- Encode a Lua table to a JSON string (flat objects only).
function json.encode(t)
    local parts = {}
    for k, v in pairs(t) do
        local ks = '"' .. tostring(k) .. '"'
        local vs
        local tv = type(v)
        if tv == "string" then
            local s = v
            s = s:gsub('\\', '\\\\')
            s = s:gsub('"',  '\\"')
            s = s:gsub('\n', '\\n')
            s = s:gsub('\r', '\\r')
            vs = '"' .. s .. '"'
        elseif tv == "number" then
            -- Emit integers without a decimal point for cleaner output.
            if v == math.floor(v) and math.abs(v) < 1e15 then
                vs = tostring(math.floor(v))
            else
                vs = tostring(v)
            end
        elseif tv == "boolean" then
            vs = v and "true" or "false"
        else
            vs = "null"
        end
        parts[#parts + 1] = ks .. ":" .. vs
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

-- Decode a flat JSON object.  Returns table, nil on success or nil, errmsg.
function json.decode(s)
    if type(s) ~= "string" then return nil, "not a string" end
    s = s:match("^%s*(.-)%s*$") -- trim
    if s == "" then return nil, "empty" end
    if s:sub(1,1) ~= "{" or s:sub(-1,-1) ~= "}" then
        return nil, "not a JSON object"
    end
    s = s:sub(2, -2) -- strip outer braces

    local t   = {}
    local len = #s
    local i   = 1

    local function skip()
        while i <= len and s:sub(i,i):match("%s") do i = i + 1 end
    end

    local function read_string()
        if s:sub(i,i) ~= '"' then return nil end
        i = i + 1
        local buf = {}
        while i <= len do
            local c = s:sub(i,i)
            if c == '"' then i = i + 1; return table.concat(buf) end
            if c == '\\' then
                i = i + 1
                local e = s:sub(i,i)
                if     e == '"'  then buf[#buf+1] = '"'
                elseif e == '\\' then buf[#buf+1] = '\\'
                elseif e == '/'  then buf[#buf+1] = '/'
                elseif e == 'n'  then buf[#buf+1] = '\n'
                elseif e == 'r'  then buf[#buf+1] = '\r'
                elseif e == 't'  then buf[#buf+1] = '\t'
                else                  buf[#buf+1] = e
                end
            else
                buf[#buf+1] = c
            end
            i = i + 1
        end
        return nil -- unterminated
    end

    local function read_value()
        skip()
        local c = s:sub(i,i)
        if c == '"' then
            return read_string()
        elseif c == 't' then
            if s:sub(i, i+3) == "true"  then i = i + 4; return true  end
        elseif c == 'f' then
            if s:sub(i, i+4) == "false" then i = i + 5; return false end
        elseif c == 'n' then
            if s:sub(i, i+3) == "null"  then i = i + 4; return nil   end
        elseif c:match("[%-0-9]") then
            local ns, ne = s:find("[%-0-9%.eE%+]+", i)
            if ns then
                local num = tonumber(s:sub(ns, ne))
                i = ne + 1
                return num
            end
        end
        return nil
    end

    while i <= len do
        skip()
        if i > len then break end
        local c = s:sub(i,i)
        if c == ',' then
            i = i + 1
        elseif c == '"' then
            local key = read_string()
            if not key then break end
            skip()
            if s:sub(i,i) ~= ':' then break end
            i = i + 1
            local val = read_value()
            t[key] = val
        else
            i = i + 1 -- skip unexpected token
        end
    end

    return t, nil
end
