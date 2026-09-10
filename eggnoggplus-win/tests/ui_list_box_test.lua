local ui = mod.ui
local old_hitbox = ui.hitbox
local hits, click_id, mx, my = {}, nil, 0, 0
ui.hitbox = function(id, x, y, w, h)
  hits[#hits + 1] = {id=id,x=x,y=y,w=w,h=h}
  return id == click_id, id == click_id, id == click_id
end
ui.mouse_pos = function() return mx, my end
local items = {}
for i=1,100 do items[i] = {id=i,label='Room '..i,disabled=i==2} end
local function list(selected, opts)
  opts = opts or {}
  opts.x, opts.y, opts.w, opts.h, opts.row_h = 10, 20, 200, 100, 20
  hits = {}
  return ui.list_box('rooms',items,selected,opts)
end
local key, changed, scroll, activated, count = list(1)
assert(key==1 and not changed and scroll==0 and not activated and count==100)
assert(#hits==6) -- one scrollbar and five visible rows, not all 100
for i=2,#hits do assert(hits[i].y>=20 and hits[i].y+hits[i].h<=120) end
key,changed,scroll = list(1,{navigate='next'})
assert(key==3 and changed and scroll==0)
key,changed,scroll = list(3,{navigate='page_down'})
assert(key==8 and changed and scroll==3)
key,changed,scroll = list(8,{navigate='page_up',scroll=3})
assert(key==3 and changed and scroll==2)
key,changed,scroll = list(3,{navigate='last'})
assert(key==100 and scroll==95)
key,changed,scroll = list(100,{navigate='first',scroll=95})
assert(key==1 and scroll==0)
key,changed,scroll = list(100,{scroll=-99,reveal=true})
assert(key==100 and scroll==95)
key,changed,scroll = list(1,{scroll_rows=3})
assert(key==1 and not changed and scroll==3)
key,changed,scroll = list(1,{scroll_rows=999})
assert(scroll==95)
key,changed,scroll,activated,count = list(1,{filter='ROOM 99',activate=true,focused=true})
assert(key==1 and not changed and scroll==0 and not activated and count==1)
key,changed,scroll,activated,count = list(1,{filter='Room 99',navigate='next',activate=true,focused=true})
assert(key==99 and changed and activated and count==1)
key,changed,scroll,activated,count = list(1,{filter='[',navigate='next'})
assert(key==1 and not changed and count==0 and #hits==0)
key,changed,scroll,activated = list(1,{navigate='next',scroll_rows=4,activate=true,focused=true,disabled=true})
assert(key==1 and not changed and scroll==0 and not activated)
click_id='rooms:row:3'
key,changed,scroll,activated = list(1)
assert(key==3 and changed and activated)
click_id='rooms:row:2'
key,changed,scroll,activated = list(1)
assert(key==1 and not changed and not activated)
click_id='rooms:scroll' my=120
key,changed,scroll = list(1)
assert(key==1 and not changed and scroll==95)
click_id=nil
assert(not pcall(list,1,{navigate='bad'}))
assert(not pcall(list,1,{filter={}}))
assert(not pcall(list,1,{scroll=0/0}))
assert(not pcall(ui.list_box,'bad',{{id=1},{id=1}},1))
assert(not pcall(ui.list_box,'bad',items,1,{h=8,row_h=20}))
key,changed,scroll,activated,count = ui.list_box('empty',{},nil,{})
assert(key==nil and not changed and scroll==0 and not activated and count==0)
print('List box filtering, bounded rendering, navigation and pointer tests: OK')

-- Resizing/filtering clamps stale offsets and pointer bounds stay inside the box.
click_id=nil
key,changed,scroll,activated,count = list(100,{filter='Room 99',scroll=95})
assert(scroll==0 and count==1 and not changed)
local large={}
for i=1,4096 do large[i]={id=i,label='Entry '..i} end
hits={}
ui.list_box('large',large,4096,{x=0,y=0,w=200,h=100,row_h=20,reveal=true})
assert(#hits==6)
large[4097]={id=4097,label='Too many'}
assert(not pcall(ui.list_box,'large',large,1,{}))
local measure, text_at = ui.measure_text, ui.text_at
local drawn={}
ui.measure_text=function(text,scale) return #text*8*scale,9*scale end
ui.text_at=function(text,x,y,scale)
  assert(x>=0 and x+#text*8*scale<=32)
  drawn[#drawn+1]=text
end
ui.list_box('narrow',{{id=1,label=string.rep('X',100)..'\nY'}},1,{x=0,y=0,w=32,h=8,row_h=8})
assert(#drawn==1 and #drawn[1]<100 and not drawn[1]:find('\n',1,true))
ui.measure_text,ui.text_at=measure,text_at
-- Exercise the checked-in demo's registration and update/render callbacks.
local define_state = ui.define_state
local spec, handlers, pressed = nil, {}, {}
ui.define_state=function(name,value) assert(name=='list_browser') spec=value return true end
mod.input={bind=function() end,pressed=function(name) return pressed[name] or false end}
mod.console={register=function(name,opts) handlers[name]=opts.handler end}
ui.enter_state=function(name) assert(name=='list_browser') return true end
ui.leave_state=function() end
ui.begin_overlay=function() end
ui.end_overlay=function() end
assert(loadfile('docs/examples/ui_list_browser.lua'))()
assert(handlers.open())
spec.enter() spec.update() spec.render()
pressed.page_down=true spec.update() spec.render() pressed.page_down=nil
assert(handlers.filter('Room 199')=='Filter: Room 199')
pressed.next=true spec.update() spec.render() pressed.next=nil
pressed.confirm=true spec.update() spec.render() pressed.confirm=nil
ui.define_state=define_state
print('List browser demo and clipping checks: OK')

ui.hitbox = old_hitbox
