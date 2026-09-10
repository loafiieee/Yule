-- Example mod entrypoint, API revision 11 / ui.control_navigation and ui.themes.
-- Open with console command: mod.<your-mod-id>.open
-- These values belong to the demo; they do not change game settings.
local ui = mod.ui
local focus, amount, enabled = 1, 0.5, false
local direction, activate = 0, false
for _, binding in ipairs({
  {'previous', 'up'}, {'next', 'down'}, {'less', 'left'},
  {'more', 'right'}, {'confirm', 'enter'}, {'back', 'escape'},
}) do
  mod.input.bind(binding[1], binding[2], 'Demo ' .. binding[1])
end

assert(ui.define_state('navigation_demo', {
  enter = function() focus, direction, activate = 1, 0, false end,
  update = function()
    if mod.input.pressed('previous') then focus = (focus + 1) % 3 + 1 end
    if mod.input.pressed('next') then focus = focus % 3 + 1 end
    direction = 0
    if focus == 1 then
      if mod.input.pressed('less') then direction = -1 end
      if mod.input.pressed('more') then direction = 1 end
    end
    activate = mod.input.pressed('confirm')
    if mod.input.pressed('back') then ui.leave_state() end
  end,
  render = function()
    ui.begin_overlay()
    ui.text_at('Control navigation demo', 16, 24, 1, 1, 1, 1)
    amount = ui.slider('amount', amount, 0, 1, {
      x=16, y=52, w=200, h=26, focused=focus == 1,
      navigate=direction, keyboard_step=0.05,
    })
    enabled = ui.checkbox('enabled', enabled, {
      x=16, y=100, label='Enable demo effect',
      focused=focus == 2, activate=activate,
    })
    if ui.icon_button('close', 'Close', 16, 144, 96, 28, {
      focused=focus == 3, activate=activate,
    }) then ui.leave_state() end
    ui.end_overlay()
    direction, activate = 0, false
  end,
}))
mod.console.register('open', {
  help='Open the control navigation demo', arguments='raw',
  handler=function() return ui.enter_state('navigation_demo') end,
})

-- Optional theme picker through the console: mod.<your-mod-id>.theme high_contrast
mod.console.register('theme', {
  help='Choose a demo theme, or list available themes', arguments='raw',
  handler=function(text)
    local name = text:match('^%s*(.-)%s*$')
    if name == '' then return table.concat(ui.theme_names(), ', ') end
    if not ui.set_theme(name) then return 'Unknown theme: ' .. name end
    return 'Theme: ' .. name
  end,
})
