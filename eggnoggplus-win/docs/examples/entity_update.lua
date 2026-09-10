-- Save as map.lua in a V2 map folder.
-- Copy entity_package.json to that folder as entities.json. Offline-only for now.
entity.on_update("demo:orb", function(handle)
    local orb = entity.get(handle)
    if orb.x >= 96 then
        entity.set(handle, {vx = -0.5})
    elseif orb.x <= 48 then
        entity.set(handle, {vx = 0.5})
    end
end)
