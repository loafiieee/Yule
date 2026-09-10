-- API revision 12 / ui.list_box. A complete bounded list browser demo.
-- Console: mod.<id>.open; mod.<id>.filter Room 12
local ui = mod.ui
local items, selected, scroll, filter = {}, 1, 0, ''
local command, activate, message = 'none', false, ''
for i=1,200 do items[i] = {id=i, label='Room '..i, disabled=i%13==0} end
for _, pair in ipairs({{'previous','up'},{'next','down'},{'page_up','pageup'},
  {'page_down','pagedown'},{'first','home'},{'last','end'},{'confirm','enter'},{'back','escape'}}) do
  mod.input.bind(pair[1],pair[2],'List demo '..pair[1])
end
assert(ui.define_state('list_browser', {
  enter=function() command, activate = 'none', false end,
  update=function()
    command = 'none'
    for _, name in ipairs({'previous','next','page_up','page_down','first','last'}) do
      if mod.input.pressed(name) then command=name break end
    end
    activate=mod.input.pressed('confirm')
    if mod.input.pressed('back') then ui.leave_state() end
  end,
  render=function()
    ui.begin_overlay()
    ui.text_at('Room browser demo',16,24,1,1,1,1)
    local changed, activated, count
    selected,changed,scroll,activated,count=ui.list_box('rooms',items,selected,{
      x=16,y=44,w=260,h=168,row_h=28,scroll=scroll,filter=filter,
      focused=true,navigate=command,activate=activate,
    })
    if activated then message='Opened demo room '..selected end
    ui.text_at(count..' matches. '..message,16,234,0.75,1,1,1)
    ui.end_overlay()
    command,activate='none',false
  end,
}))
mod.console.register('open',{help='Open list browser demo',arguments='raw',
  handler=function() return ui.enter_state('list_browser') end})
mod.console.register('filter',{help='Filter room labels; empty clears filter',arguments='raw',
  handler=function(text) filter=text scroll=0 return 'Filter: '..text end})
