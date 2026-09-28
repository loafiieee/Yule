-- Map API 34. Pair with entity_projectile.json as entities.json.
-- This is a reusable managed projectile pattern, not a hardcoded weapon type.

entity.on_spawn("demo:target", function(handle)
    entity.set_value(handle, "health", 10)
end)

entity.on_update("demo:launcher", function(handle)
    if not entity.has_value(handle, "fired") then
        local origin = entity.get(handle)
        entity.spawn("demo:projectile", {
            x = origin.x + 8,
            y = origin.y,
            vx = 2,
            values = { owner = handle, damage = 4, lifetime = 90 },
        })
        entity.set_value(handle, "fired", true)
    end
end)

entity.on_update("demo:projectile", function(handle)
    local lifetime = entity.change_value(handle, "lifetime", -1)
    if lifetime <= 0 then entity.remove(handle) end
end)

entity.on_contact("demo:projectile", function(handle, contact)
    if contact.self_region_name == "attack" and
       contact.other_region_name == "receiver" then
        local owner = entity.value(handle, "owner")
        local source = owner and entity.exists(owner) and owner or nil
        entity.damage(contact.other, entity.value(handle, "damage"), source)
        entity.remove(handle)
    end
end)

entity.on_damage("demo:target", function(handle, damage)
    local health = entity.change_value(handle, "health", -damage.amount)
    if health <= 0 then entity.remove(handle) end
end)
