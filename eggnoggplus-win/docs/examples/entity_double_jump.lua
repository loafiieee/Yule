-- Map API 32. Pair with entity_double_jump.json as entities.json.
-- This uses general input, grounded, object visibility, and per-player state APIs.
map.on_tick(function()
    for _, player in ipairs(map.players()) do
        local key = "v:p:" .. player.player .. ":extra_jump"
        if player.grounded then
            map.state[key] = true
        elseif player.pressed.jump and
               not (player.previously_grounded and player.vy < 0) and
               map.state[key] then
            map.set_player_velocity(player.player, player.vx, -4)
            map.state[key] = false
        end

        local indicator = entity.find("jump_indicator_" .. player.player)
        if indicator then
            entity.set(indicator, {
                x = player.x,
                y = player.y - 13,
                visible = map.state[key] == true
            })
        end
    end
end)

entity.on_player_contact("demo:jump_refill", function(handle, contact)
    if contact.region_name == "pickup" then
        map.state["v:p:" .. contact.player .. ":extra_jump"] = true
        entity.remove(handle)
    end
end)
