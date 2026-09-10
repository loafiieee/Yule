#include <stdio.h>
#include <assert.h>
#include <float.h>
#include <limits.h>
#include "../ui_geometry.h"
#include <string.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include "../ui_helpers.h"

static int run(lua_State* L, const char* code, const char* name) {
    if (luaL_loadbuffer(L, code, strlen(code), name) || lua_pcall(L, 0, 0, 0)) {
        fprintf(stderr, "%s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    return 1;
}

int main(void) {
    lua_State* L = luaL_newstate();
    int ok;
    if (!L) return 1;
    assert(ui_bounds_contains(0, 0, 10, 10, 0, 0));
    assert(!ui_bounds_contains(0, 0, 10, 10, 10, 5));
    assert(ui_bounds_contains(10, 0, 10, 10, 10, 5));
    assert(!ui_bounds_contains(0, 0, 10, 10, 5, 10));
    assert(!ui_bounds_contains(0.5f, 0, 10, 10, 0, 5));
    assert(ui_bounds_contains(-0.5f, 0, 10, 10, 0, 5));
    assert(!ui_bounds_contains(NAN, 0, 10, 10, 5, 5));
    assert(!ui_bounds_contains(0, INFINITY, 10, 10, 5, 5));
    assert(!ui_bounds_contains(0, 0, NAN, 10, 5, 5));
    assert(!ui_bounds_contains(0, 0, 10, INFINITY, 5, 5));
    assert(!ui_bounds_contains(0, 0, -1, 10, 0, 0));
    assert(!ui_bounds_contains(0, 0, 10, 0, 0, 0));
    assert(!ui_bounds_contains(FLT_MAX, 0, FLT_MAX, 10, INT_MAX, 5));
    luaL_openlibs(L);
    ok = run(L,
        "clicked=false mx=0 my=0 mod={ui={}} "
        "mod.ui.hitbox=function() return clicked,clicked,clicked end "
        "mod.ui.mouse_pos=function() return mx,my end "
        "mod.ui.rect=function(x,y,w,h) "
        "assert(x==x and y==y and w==w and h==h and w>=0 and h>=0) end "
        "mod.ui.border=mod.ui.rect mod.ui.text_at=function() end", "fixture") &&
        run(L, k_mod_ui_helpers_lua, "embedded UI library") &&
        run(L,
        "local ui=mod.ui "
        "ui.push_style({pad=17}) local before=ui.current_style() "
        "local function pack(...) return {n=select('#',...),...} end "
        "local values=pack(ui.with_style({pad=23},function(a,b) "
        "assert(a=='arg' and b==nil and ui.current_style().pad==23) "
        "ui.with_style({pad=31},function() assert(ui.current_style().pad==31) end) "
        "assert(ui.current_style().pad==23) ui.pop_style() ui.theme({pad=99}) "
        "ui.set_theme('default_dark') ui.push_style({pad=55}) return 'value',nil,false,nil end,'arg',nil)) "
        "assert(values.n==4 and values[1]=='value' and values[2]==nil and values[3]==false and values[4]==nil) "
        "assert(ui.current_style().pad==before.pad) "
        "local marker={} local success,err=pcall(ui.with_style,{pad=23},function() "
        "ui.push_style({pad=44}) error(marker) end) assert(not success and err==marker) "
        "assert(ui.current_style().pad==17) "
        "assert(not pcall(ui.with_style,nil,function() end)) "
        "assert(not pcall(ui.with_style,{},nil)) assert(ui.current_style().pad==17) "
        "ui.pop_style() assert(ui.current_style().pad==8) "
        "local cyc={pad=999} cyc.self=cyc "
        "assert(not pcall(ui.theme,cyc)) assert(ui.current_style().pad==8) "
        "assert(not pcall(ui.push_style,cyc)) assert(not pcall(ui.define_theme,'bad',cyc)) "
        "assert(not pcall(ui.with_style,cyc,function() error('must not run') end)) "
        "local deep={} local cursor=deep for i=1,40 do cursor.next={} cursor=cursor.next end "
        "assert(not pcall(ui.theme,deep)) assert(ui.current_style().pad==8) "
        "local co=coroutine.create(function() ui.with_style({pad=99},function() "
        "coroutine.yield('paused') assert(ui.current_style().pad==99) ui.set_theme('default_dark') "
        "ui.theme({pad=77}) coroutine.yield() assert(ui.current_style().pad==77) end) "
        "assert(ui.current_style().pad==8) end) "
        "local resumed,msg=coroutine.resume(co) assert(resumed and msg=='paused') "
        "assert(ui.current_style().pad==8) assert(coroutine.resume(co)) "
        "assert(ui.current_style().pad==8) assert(coroutine.resume(co)) "
        "assert(coroutine.status(co)=='dead' and ui.current_style().pad==8) "
        "clicked=true mx=25 my=25 "
        "local v,c=ui.progress_bar('p',0,0,0,100,100,{interactive=true}) assert(v==0.25 and c) "
        "v,c=ui.progress_bar('p',0,0,0,100,100,{interactive=true,reverse=true}) assert(v==0.75 and c) "
        "v,c=ui.progress_bar('p',0,0,0,100,100,{interactive=true,vertical=true}) assert(v==0.75 and c) "
        "v,c=ui.progress_bar('p',0,0,0,100,100,{interactive=true,vertical=true,reverse=true}) assert(v==0.25 and c) "
        "v,c=ui.progress_bar('p',0.4,0,0,100,100,{interactive=true,disabled=true}) assert(v==0.4 and not c) "
        "v,c=ui.progress_bar('p',0/0,0,0,0,100,{interactive=true}) assert(v==0 and not c) "
        "v,c=ui.progress_bar('p',0.4,0/0,0,100,100,{interactive=true}) assert(v==0.4 and not c) "
        "v,c=ui.progress_bar('p',0.4,0,0,math.huge,100,{interactive=true}) assert(v==0.4 and not c) "
        "v,c=ui.progress_bar(0.3,0,0,100,100,{}) assert(v==0.3 and not c) "
        "clicked=false v=ui.progress_bar('p',0/0,0,0,100,100,{}) assert(v==0) "
        "clicked=false local value,changed=ui.slider('nav',0,0,1,{navigate=1,keyboard_step=0.25,focused=true}) "
        "assert(value==0.25 and changed) "
        "value,changed=ui.slider('nav',1,0,1,{navigate=1}) assert(value==1 and not changed) "
        "value,changed=ui.slider('nav',0.5,0,1,{navigate=-1,step=0.25}) assert(value==0.25 and changed) "
        "value,changed=ui.slider('nav',0.5,0,1,{navigate=1,disabled=true}) assert(value==0.5 and not changed) "
        "value,changed=ui.slider('nav',5,5,5,{navigate=1}) assert(value==5 and not changed) "
        "assert(not pcall(ui.slider,'nav',0,0,1,{navigate=2})) "
        "assert(not pcall(ui.slider,'nav',0,0,1,{keyboard_step=math.huge})) "
        "assert(not pcall(ui.slider,'nav',0,0,1,{keyboard_step=0})) "
        "assert(ui.icon_button('b','B',0,0,20,20,{focused=true,activate=true})) "
        "assert(not ui.icon_button('b','B',0,0,20,20,{activate=true})) "
        "assert(not ui.icon_button('b','B',0,0,20,20,{focused=true,activate=true,disabled=true})) "
        "value,changed=ui.checkbox('c',false,{focused=true,activate=true}) assert(value and changed) "
        "clicked=true mx=0 value=ui.slider('nav',0.5,0,1,{x=0,y=0,w=100,navigate=1}) assert(value==0) clicked=false "
        "local registrations,frames,events=0,0,0 local render_count=0 local frame_fn "
        "ui.create_state=function() registrations=registrations+1 return true end "
        "mod.on_frame=function(fn) frames=frames+1 frame_fn=fn end "
        "mod.on_event=function() events=events+1 end ui.state_name=function() return 'demo' end "
        "assert(not ui.define_state('demo',123)) assert(not ui.define_state('demo',{render=true})) "
        "assert(registrations==0 and frames==0) "
        "assert(ui.define_state('demo',{render=function() render_count=render_count+1 end})) "
        "assert(frames==1 and events==1) frame_fn() assert(render_count==1) "
        "ui.create_state=function() return false,'capacity exhausted' end "
        "local ok,err=ui.define_state('demo',{render=function() error('bad replacement') end}) "
        "assert(not ok and err=='capacity exhausted') frame_fn() assert(render_count==2) "
        "assert(frames==1 and events==1) "
        "local current='demo' local leaves,renders,cursors=0,0,0 "
        "ui.state_name=function() return current end ui.create_state=function() return true end "
        "ui._set_default_cursor_visible=function() cursors=cursors+1 end "
        "assert(ui.define_state('demo',{update=function() current='next' end, "
        "render=function() renders=renders+1 end,leave=function() leaves=leaves+1 end,cursor=false})) "
        "frame_fn() assert(renders==0 and cursors==0) frame_fn() assert(leaves==1) "
        "assert(ui.define_state('redirect',{enter=function() current='next' end, "
        "render=function() error('stale enter render') end})) current='redirect' frame_fn() "
        "assert(current=='next') frame_fn() "
        "assert(ui.define_state('render_redirect',{render=function() current='next' end,cursor=false})) "
        "current='render_redirect' frame_fn() assert(cursors==0) frame_fn() "
        "assert(ui.define_state('leave_redirect',{leave=function() leaves=leaves+1 current='third' end})) "
        "current='leave_redirect' frame_fn() current='next' frame_fn() assert(current=='third') "
        "frame_fn() assert(leaves==2) "
        "local names=ui.theme_names() assert(names[1]=='default_dark' and names[2]=='high_contrast') "
        "names[1]='changed' assert(ui.theme_names()[1]=='default_dark') "
        "local partial={accent={0.2,0.3,0.4,1}} assert(ui.define_theme('mint',partial)) "
        "partial.accent[1]=9 assert(ui.set_theme('mint')) "
        "local themed=ui.current_style() assert(themed.accent[1]==0.2 and themed.gap==6 and themed.pad==8) "
        "ui.checkbox('partial',false,{x=0,y=0,label='Label'}) "
        "assert(ui.set_theme('high_contrast')) themed=ui.current_style() "
        "assert(themed.bg[1]==0 and themed.bg[4]==1 and themed.fg[1]==1 and themed.accent[3]==0) "
        "assert(not ui.set_theme('missing')) assert(ui.current_style().bg[1]==0) "
        "assert(ui.set_theme('default_dark')) "
        "local r,h,c=ui.grid_layout(10,20,250,5,{min_cell_w=120,gap=10}) "
        "assert(#r==5 and h==104 and c==2) "
        "assert(r[1].x==10 and r[2].x==140 and r[5].y==96 and r[1].w==120) "
        "r[1].w=999 assert(ui.grid_layout(0,0,250,5)[1].w~=999) "
        "r,h,c=ui.grid_layout(0,0,50,2) assert(c==1 and r[1].w==50 and h==62) "
        "r,h,c=ui.grid_layout(0,0,50,0) assert(#r==0 and h==0 and c==0) "
        "r,h,c=ui.grid_layout(0,0,500,3,{max_cols=1}) assert(c==1 and #r==3) "
        "for _,n in ipairs({-1,1.5,4097}) do assert(not pcall(ui.grid_layout,0,0,100,n)) end "
        "assert(not pcall(ui.grid_layout,0,0,0/0,2)) "
        "assert(not pcall(ui.grid_layout,0,0,100,2,{gap=-1})) "
        "assert(not pcall(ui.grid_layout,0,0,100,2,{max_cols=0})) "
        "local w=ui.list_window(100,20,60,0) assert(w.first==1 and w.last==3 and w.offset_y==0 and w.max_scroll==1940) "
        "w=ui.list_window(100,20,60,1) assert(w.first==1 and w.last==4 and w.offset_y==-1) "
        "w=ui.list_window(100,20,60,99999) assert(w.first==98 and w.last==100 and w.scroll==1940) "
        "w=ui.list_window(100,20,60,0,8) assert(w.first==6 and w.last==8 and w.scroll==100) "
        "w=ui.list_window(100,20,60,100,2) assert(w.first==2 and w.scroll==20) "
        "w=ui.list_window(0,20,60,100) assert(w.first==1 and w.last==0 and w.scroll==0) "
        "w=ui.list_window(2,20,60,-10) assert(w.first==1 and w.last==2 and w.scroll==0) "
        "w=ui.list_window(3,100,20,0,2) assert(w.first==2 and w.last==2 and w.scroll==100) "
        "assert(not pcall(ui.list_window,0,20,60,0,1)) "
        "assert(not pcall(ui.list_window,2,0,60,0)) "
        "assert(not pcall(ui.list_window,2,20,0/0,0)) "
        "assert(not pcall(ui.list_window,2,20,60,math.huge)) "
        "assert(not pcall(ui.list_window,2.5,20,60,0)) "
        "w=ui.list_window(1000000,20,60,1e13) assert(w.last==1000000 and w.last-w.first==2) "
        "local items={{id='a',label='A'},{id='b',label='B',disabled=true},{id='c',label='C'}} "
        "local key,changed=ui.navigate_items(items,'a',1) assert(key=='c' and changed) "
        "key,changed=ui.navigate_items(items,'c',1) assert(key=='c' and not changed) "
        "key,changed=ui.navigate_items(items,'c',1,{wrap=true}) assert(key=='a' and changed) "
        "key,changed=ui.navigate_items(items,'a',-1,{wrap=true}) assert(key=='c' and changed) "
        "assert(ui.navigate_items(items,nil,-1)=='c') assert(ui.navigate_items(items,nil,1)=='a') "
        "key,changed=ui.navigate_items({},nil,1) assert(key==nil and not changed) "
        "key,changed=ui.navigate_items({{id='a',disabled=true}},'a',1,{wrap=true}) assert(key=='a' and not changed) "
        "assert(ui.navigate_items(items,'b',-1)=='a') assert(ui.navigate_items(items,'a',1,{disabled=true})=='a') "
        "assert(not pcall(ui.navigate_items,items,'a',2)) assert(not pcall(ui.navigate_items,items,'a',0/0)) "
        "key,changed=ui.tabs('tabs',items,'a',{x=0,y=0,w=300,navigate=1}) assert(key=='c' and changed) "
        "key,changed=ui.tabs('tabs',items,'a',{x=0,y=0,w=300,navigate=1,item_opts={disabled=true}}) assert(key=='a' and not changed) "
        "key,changed=ui.item_grid('grid',items,'a',{x=0,y=0,navigate=1}) assert(key=='c' and changed) "
        "key,changed=ui.swatch_grid('colors',items,'a',{x=0,y=0,navigate=1}) assert(key=='c' and changed) "
        "clicked=true key,changed=ui.tabs('tabs',items,'a',{x=0,y=0,w=300,disabled=true}) assert(key=='a' and not changed) "
        "key,changed=ui.item_grid('grid',items,'a',{x=0,y=0,disabled=true}) assert(key=='a' and not changed) "
        "key,changed=ui.swatch_grid('colors',items,'a',{x=0,y=0,disabled=true}) assert(key=='a' and not changed) clicked=false "
        "clicked=true mx=0 local v=ui.slider('a',3,1,4,{x=0,y=0,w=100,step=2}) assert(v==1) "
        "mx=100 v=ui.slider('a',1,1,4,{x=0,y=0,w=100,step=2}) assert(v==4) "
        "mx=50 v=ui.slider('a',1,1,4,{x=0,y=0,w=100,step=2}) assert(v==3) "
        "v=ui.slider('a',0,5,5,{x=0,y=0,w=100}) assert(v==5) "
        "mx=0 v=ui.slider('a',0,5,1,{x=0,y=0,w=100}) assert(v==1) "
        "mx=100 v=ui.slider('a',2,1,4,{x=0,y=0,w=100,disabled=true}) assert(v==2) "
        "clicked=false local changed v,changed=ui.slider('a',10,1,4,{x=0,y=0,w=0,h=1}) assert(v==4 and changed) "
        "clicked=true mx=50 v=ui.slider('a',1,1,4,{x=0,y=0,w=100,step=1e-320}) assert(v==2.5) clicked=false "
        "v=ui.slider('a',0/0,0,1,{x=0,y=0,w=100}) assert(v==0)", "UI behavior tests");
    if (ok && luaL_dofile(L, "tests/ui_list_box_test.lua") != 0) {
        fprintf(stderr, "list box tests: %s\n", lua_tostring(L, -1));
        ok = 0;
    }
    lua_close(L);
    if (!ok) return 1;
    puts("UI helpers responsive layout and slider tests: OK");
    return 0;
}
