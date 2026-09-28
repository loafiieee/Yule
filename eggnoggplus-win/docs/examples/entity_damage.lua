-- Pair with entity_damage.json as entities.json.
-- Change the placement room to an ID in your V2 map.

entity.on_spawn("demo:crate", function(handle)
    entity.enable_health(handle, 10)
end)

-- This crate accepts damage only from this map's blade object type. More object
-- types can be accepted with `or`, variables, teams, or any other deterministic rule.
entity.on_damage_filter("demo:crate", function(handle, damage)
    return damage.source_type == "demo:blade"
end)

entity.on_contact("demo:blade", function(handle, contact)
    if contact.self_region_name == "attack" and
       contact.other_region_name == "receiver" and
       entity.value(handle, "cooldown") == nil then
        if entity.damage(contact.other, 3, handle) then
            entity.set_value(handle, "cooldown", 12)
        end
    end
end)

entity.on_update("demo:blade", function(handle)
    local cooldown = entity.value(handle, "cooldown")
    if cooldown ~= nil then
        if cooldown <= 1 then entity.set_value(handle, "cooldown", nil)
        else entity.set_value(handle, "cooldown", cooldown - 1) end
    end
end)

entity.on_damage("demo:crate", function(handle, damage)
    entity.set(handle, { animation_tick = 0 })
end)

entity.on_defeated("demo:crate", function(handle)
    entity.remove(handle)
end)
