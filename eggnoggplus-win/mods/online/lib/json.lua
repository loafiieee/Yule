-- lib/json.lua - recursive JSON encode/decode for the online mod.
-- Supports nested objects, arrays, strings, numbers, booleans and null.

json = json or {}

local function is_array(t)
    if type(t) ~= "table" then return false, 0 end
    local n = 0
    for k, _ in pairs(t) do
        if type(k) ~= "number" or k < 1 or k ~= math.floor(k) then
            return false, 0
        end
        if k > n then n = k end
    end
    for i = 1, n do
        if t[i] == nil then return false, 0 end
    end
    return true, n
end

local function escape_string(s)
    s = tostring(s)
    s = s:gsub('\\', '\\\\')
    s = s:gsub('"', '\\"')
    s = s:gsub('\b', '\\b')
    s = s:gsub('\f', '\\f')
    s = s:gsub('\n', '\\n')
    s = s:gsub('\r', '\\r')
    s = s:gsub('\t', '\\t')
    return '"' .. s .. '"'
end

local function encode_value(v, seen)
    local tv = type(v)
    if tv == "nil" then
        return "null"
    elseif tv == "string" then
        return escape_string(v)
    elseif tv == "number" then
        if v ~= v or v == math.huge or v == -math.huge then
            return "null"
        end
        if v == math.floor(v) and math.abs(v) < 1e15 then
            return tostring(math.floor(v))
        end
        return tostring(v)
    elseif tv == "boolean" then
        return v and "true" or "false"
    elseif tv == "table" then
        if seen[v] then error("circular reference in json.encode") end
        seen[v] = true
        local arr, n = is_array(v)
        local out = {}
        if arr then
            for i = 1, n do
                out[#out + 1] = encode_value(v[i], seen)
            end
            seen[v] = nil
            return "[" .. table.concat(out, ",") .. "]"
        end
        for k, val in pairs(v) do
            out[#out + 1] = escape_string(tostring(k)) .. ":" .. encode_value(val, seen)
        end
        seen[v] = nil
        return "{" .. table.concat(out, ",") .. "}"
    end
    return "null"
end

function json.encode(v)
    return encode_value(v, {})
end

function json.decode(s)
    if type(s) ~= "string" then return nil, "not a string" end
    local i = 1
    local len = #s

    local function skip_ws()
        while i <= len do
            local c = s:sub(i, i)
            if c == ' ' or c == '\t' or c == '\r' or c == '\n' then
                i = i + 1
            else
                break
            end
        end
    end

    local parse_value

    local function parse_string()
        if s:sub(i, i) ~= '"' then return nil, "expected string" end
        i = i + 1
        local buf = {}
        while i <= len do
            local c = s:sub(i, i)
            if c == '"' then
                i = i + 1
                return table.concat(buf)
            elseif c == '\\' then
                i = i + 1
                if i > len then return nil, "bad escape" end
                local e = s:sub(i, i)
                if e == '"' or e == '\\' or e == '/' then
                    buf[#buf + 1] = e
                elseif e == 'b' then
                    buf[#buf + 1] = '\b'
                elseif e == 'f' then
                    buf[#buf + 1] = '\f'
                elseif e == 'n' then
                    buf[#buf + 1] = '\n'
                elseif e == 'r' then
                    buf[#buf + 1] = '\r'
                elseif e == 't' then
                    buf[#buf + 1] = '\t'
                elseif e == 'u' then
                    local hex = s:sub(i + 1, i + 4)
                    if #hex ~= 4 or not hex:match("^[0-9a-fA-F]+$") then
                        return nil, "bad unicode escape"
                    end
                    local code = tonumber(hex, 16)
                    if code < 128 then
                        buf[#buf + 1] = string.char(code)
                    else
                        -- Keep non-ASCII escapes as UTF-8-ish replacement to avoid parser failure.
                        buf[#buf + 1] = "?"
                    end
                    i = i + 4
                else
                    return nil, "bad escape"
                end
            else
                buf[#buf + 1] = c
            end
            i = i + 1
        end
        return nil, "unterminated string"
    end

    local function parse_number()
        local start_i = i
        local pat = "^-?%d+%.?%d*[eE]?[+-]?%d*"
        local sub = s:sub(i)
        local num_s = sub:match(pat)
        if not num_s or num_s == "" or num_s == "-" then
            return nil, "bad number"
        end
        local n = tonumber(num_s)
        if n == nil then return nil, "bad number" end
        i = start_i + #num_s
        return n
    end

    local function parse_array()
        if s:sub(i, i) ~= '[' then return nil, "expected array" end
        i = i + 1
        local arr = {}
        skip_ws()
        if s:sub(i, i) == ']' then
            i = i + 1
            return arr
        end
        while i <= len do
            local v, err = parse_value()
            if err then return nil, err end
            arr[#arr + 1] = v
            skip_ws()
            local c = s:sub(i, i)
            if c == ']' then
                i = i + 1
                return arr
            elseif c == ',' then
                i = i + 1
                skip_ws()
            else
                return nil, "expected ',' or ']'"
            end
        end
        return nil, "unterminated array"
    end

    local function parse_object()
        if s:sub(i, i) ~= '{' then return nil, "expected object" end
        i = i + 1
        local obj = {}
        skip_ws()
        if s:sub(i, i) == '}' then
            i = i + 1
            return obj
        end
        while i <= len do
            skip_ws()
            local key, err = parse_string()
            if err then return nil, err end
            skip_ws()
            if s:sub(i, i) ~= ':' then return nil, "expected ':'" end
            i = i + 1
            skip_ws()
            local val, verr = parse_value()
            if verr then return nil, verr end
            obj[key] = val
            skip_ws()
            local c = s:sub(i, i)
            if c == '}' then
                i = i + 1
                return obj
            elseif c == ',' then
                i = i + 1
                skip_ws()
            else
                return nil, "expected ',' or '}'"
            end
        end
        return nil, "unterminated object"
    end

    function parse_value()
        skip_ws()
        local c = s:sub(i, i)
        if c == '"' then
            return parse_string()
        elseif c == '{' then
            return parse_object()
        elseif c == '[' then
            return parse_array()
        elseif c == '-' or c:match('%d') then
            return parse_number()
        elseif s:sub(i, i + 3) == "true" then
            i = i + 4
            return true
        elseif s:sub(i, i + 4) == "false" then
            i = i + 5
            return false
        elseif s:sub(i, i + 3) == "null" then
            i = i + 4
            return nil
        end
        return nil, "unexpected token"
    end

    skip_ws()
    local val, err = parse_value()
    if err then return nil, err end
    skip_ws()
    if i <= len then return nil, "trailing garbage" end
    return val, nil
end
