-- Map API 35. Pair with entity_signal.json as entities.json.
-- Change the placement room to an ID in your V2 map.

entity.on_contact("demo:switch", function(handle, contact)
    if contact.self_region_name == "trigger" and
       contact.other_region_name == "receiver" and
       entity.value(handle, "used") == nil then
        if entity.signal(contact.other, "open", 2, handle) then
            entity.set_value(handle, "used", true)
        end
    end
end)

entity.on_signal("demo:door", function(handle, signal)
    if signal.name == "open" and signal.source_type == "demo:switch" then
        entity.set_value(handle, "open_amount", signal.value or 1)
        entity.set(handle, { visible = false })
    end
end)
