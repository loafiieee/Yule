 'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const L=require('./logic-blocks.js'),B=require('./vendor/blockly/blockly_compressed.js');
function tick(){const workspace=new B.Workspace(),event=workspace.newBlock('greg_tick');return {workspace,event};}
test('room doorway blocks keep stable ids while compiling current native connection numbers',()=>{
 const original=[{id:'start_hall',index:1,label:'Door 1 · start right → hall left'},{id:'hall_vault',index:2,label:'Door 2 · hall top → vault bottom'}];
 L.setRoomConnections(original);
 const {workspace,event}=tick(),lock=workspace.newBlock('greg_exit_lock'),branch=workspace.newBlock('controls_if'),locked=workspace.newBlock('greg_exit_locked');
 event.getInput('DO').connection.connect(lock.previousConnection);lock.setFieldValue('2','CONNECTION');lock.setFieldValue('true','LOCKED');lock.nextConnection.connect(branch.previousConnection);locked.setFieldValue('1','CONNECTION');branch.getInput('IF0').connection.connect(locked.outputConnection);
 assert.equal(lock.getFieldValue('CONNECTION'),'hall_vault');assert.equal(locked.getFieldValue('CONNECTION'),'start_hall');
 assert.deepEqual(lock.getField('CONNECTION').getOptions(false),[['Door 1 · start right → hall left','start_hall'],['Door 2 · hall top → vault bottom','hall_vault']]);
 const saved=L.save(workspace);assert.match(saved.source,/map\.set_exit_locked\(2, true\)/);assert.match(saved.source,/map\.exit_locked\(1\)/);
 L.setRoomConnections([{id:'hall_vault',index:1,label:'Door 1 · hall top → vault bottom'},{id:'start_hall',index:2,label:'Door 2 · start right → hall left'}]);assert.match(L.generate(workspace),/map\.set_exit_locked\(1, true\)/);assert.match(L.generate(workspace),/map\.exit_locked\(2\)/);
 L.setRoomConnections(original);
 const restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),saved.source);
 workspace.dispose();restored.dispose();L.setRoomConnections([]);
});

test('room metadata blocks expose placed and source room identity without coordinate guesses',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),setCount=w.newBlock('greg_variable_set'),count=w.newBlock('greg_room_count'),setStart=w.newBlock('greg_variable_set'),start=w.newBlock('greg_room_start'),setSource=w.newBlock('greg_variable_set'),source=w.newBlock('greg_room_number'),sourceRoom=w.newBlock('greg_player_value'),setName=w.newBlock('greg_variable_set'),name=w.newBlock('greg_room_text'),nameRoom=w.newBlock('math_number'),setMirror=w.newBlock('greg_variable_set'),mirror=w.newBlock('greg_room_condition'),mirrorRoom=w.newBlock('math_number');
 event.getInput('DO').connection.connect(setCount.previousConnection);setCount.setFieldValue('map','SCOPE');setCount.setFieldValue('rooms','NAME');setCount.getInput('VALUE').connection.connect(count.outputConnection);setCount.nextConnection.connect(setStart.previousConnection);setStart.setFieldValue('map','SCOPE');setStart.setFieldValue('start','NAME');setStart.getInput('VALUE').connection.connect(start.outputConnection);
 setStart.nextConnection.connect(setSource.previousConnection);setSource.setFieldValue('map','SCOPE');setSource.setFieldValue('source','NAME');source.setFieldValue('source_room','PROPERTY');sourceRoom.setFieldValue('1','PLAYER');sourceRoom.setFieldValue('room','PROPERTY');source.getInput('ROOM').connection.connect(sourceRoom.outputConnection);setSource.getInput('VALUE').connection.connect(source.outputConnection);
 setSource.nextConnection.connect(setName.previousConnection);setName.setFieldValue('map','SCOPE');setName.setFieldValue('room_name','NAME');name.setFieldValue('id','PROPERTY');nameRoom.setFieldValue(2,'NUM');name.getInput('ROOM').connection.connect(nameRoom.outputConnection);setName.getInput('VALUE').connection.connect(name.outputConnection);
 setName.nextConnection.connect(setMirror.previousConnection);setMirror.setFieldValue('map','SCOPE');setMirror.setFieldValue('mirrored','NAME');mirror.setFieldValue('mirrored','PROPERTY');mirrorRoom.setFieldValue(2,'NUM');mirror.getInput('ROOM').connection.connect(mirrorRoom.outputConnection);setMirror.getInput('VALUE').connection.connect(mirror.outputConnection);
 const saved=L.save(w);assert.match(saved.source,/map\.room_count\(\)/);assert.match(saved.source,/map\.start_room\(\)/);assert.match(saved.source,/map\.room_info\([^\n]+\)\.source_room/);assert.match(saved.source,/map\.room_info\(2\)\.id/);assert.match(saved.source,/map\.room_info\(2\)\.mirrored/);
 const copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);const game=L.toolbox.contents.find(category=>category.name==='Game').contents.map(item=>item.type);assert.ok(['greg_room_count','greg_room_start','greg_room_number','greg_room_text','greg_room_condition'].every(type=>game.includes(type)));w.dispose();copy.dispose();
});

test('scripted room move uses destination-local coordinates and current-player scope',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_player_action_event'),move=w.newBlock('greg_player_room_move'),room=w.newBlock('math_number'),x=w.newBlock('math_number'),y=w.newBlock('math_number');event.setFieldValue('any','PLAYER');event.setFieldValue('pressed','EDGE');event.setFieldValue('up','ACTION');event.getInput('DO').connection.connect(move.previousConnection);move.setFieldValue('current','PLAYER');room.setFieldValue(3,'NUM');x.setFieldValue(24,'NUM');y.setFieldValue(80,'NUM');move.getInput('ROOM').connection.connect(room.outputConnection);move.getInput('X').connection.connect(x.outputConnection);move.getInput('Y').connection.connect(y.outputConnection);const saved=L.save(w);assert.match(saved.source,/map\.move_player_to_room\(player\.player, 3, 24, 80\)/);const copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);w.dispose();copy.dispose();
});
test('authored point camera blocks compile to draw-only map controls and round trip',()=>{
 const {workspace,event}=tick(),point=workspace.newBlock('greg_camera_point'),clear=workspace.newBlock('greg_camera_clear'),x=workspace.newBlock('math_number'),y=workspace.newBlock('math_number'),zoom=workspace.newBlock('math_number');
 event.getInput('DO').connection.connect(point.previousConnection);point.nextConnection.connect(clear.previousConnection);
 x.setFieldValue(120,'NUM');y.setFieldValue(80,'NUM');zoom.setFieldValue(0.5,'NUM');
 point.getInput('X').connection.connect(x.outputConnection);point.getInput('Y').connection.connect(y.outputConnection);point.getInput('ZOOM').connection.connect(zoom.outputConnection);
 const saved=L.save(workspace);assert.match(saved.source,/map\.set_camera_point\(120, 80, 0\.5\)/);assert.match(saved.source,/map\.clear_camera\(\)/);
 const copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);
 const game=L.toolbox.contents.find(category=>category.name==='Game').contents.map(item=>item.type);
 assert.ok(['greg_camera_point','greg_camera_clear','greg_camera_number','greg_camera_active'].every(type=>game.includes(type)));
 workspace.dispose();copy.dispose();
});
test('connected player action generates a native map callback and round trips as blocks',()=>{
 const {workspace,event}=tick(),action=workspace.newBlock('greg_velocity'),number=workspace.newBlock('math_number');
 number.setFieldValue(6,'NUM');event.getInput('DO').connection.connect(action.previousConnection);action.getInput('X').connection.connect(number.outputConnection);
 const saved=L.save(workspace);assert.match(saved.source,/map\.on_tick/);assert.match(saved.source,/map\.set_player_velocity\(1, 6, 0\)/);
 const restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),saved.source);
 workspace.dispose();restored.dispose();
});
test('invalid contexts and duplicate event handlers reject before replacing Lua',()=>{
 const {workspace}=tick();workspace.newBlock('greg_tick');assert.throws(()=>L.generate(workspace),/one block/);workspace.dispose();
 const standalone=new B.Workspace();standalone.newBlock('greg_defeat');assert.throws(()=>L.generate(standalone),/inside an event/);standalone.dispose();
 const {workspace:w,event}=tick(),action=w.newBlock('greg_entity_set');event.getInput('DO').connection.connect(action.previousConnection);assert.throws(()=>L.generate(w),/object update or creation/);w.dispose();
});
test('handwritten Lua survives mode switching byte for byte',()=>{
 const w=new B.Workspace(),source='-- manual\nmap.on_tick(function() end)';
 assert.equal(L.restore(w,null,source),false);assert.equal(L.generate(w),source);
 const saved=L.save(w);assert.equal(L.restore(w,saved,source),true);assert.equal(L.generate(w),source);w.dispose();
});
test('malformed or mismatched metadata falls back to preserved Lua without partial blocks',()=>{
 const w=new B.Workspace(),source='-- preserved';
 assert.equal(L.restore(w,{source,workspace:{blocks:{languageVersion:0,blocks:[{type:'not_a_block'}]}}},source),false);
 assert.equal(L.generate(w),source);assert.equal(w.getAllBlocks(false).length,1);w.dispose();
});

test('object movement and animation controls match the native runtime fixture',()=>{
 const source=require('./logic-runtime-fixture')();
 assert.equal(source,require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-object-motion.lua','utf8'));
 assert.equal(require('./logic-runtime-fixture')(true),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-object-remove.lua','utf8'));
 assert.match(source,/animation_paused = true/);
 assert.match(source,/map.tick/);
});
test('object visual scale is a general per-instance property',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),set=w.newBlock('greg_entity_set'),setY=w.newBlock('greg_entity_set'),number=w.newBlock('math_number'),read=w.newBlock('greg_entity_get');event.setFieldValue('demo:orb','TYPE');event.setFieldValue('update','EVENT');
 event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('scale_x','PROPERTY');set.getInput('VALUE').connection.connect(number.outputConnection);number.setFieldValue(1.5,'NUM');
 set.nextConnection.connect(setY.previousConnection);setY.setFieldValue('scale_y','PROPERTY');setY.getInput('VALUE').connection.connect(read.outputConnection);read.setFieldValue('scale_y','PROPERTY');const source=L.generate(w);assert.match(source,/entity\.set\(handle, \{scale_x = 1\.5\}\)/);assert.match(source,/entity\.set\(handle, \{scale_y = entity\.get\(handle\)\.scale_y\}\)/);w.dispose();
});
test('object animation speed uses the same general numeric property blocks',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),set=w.newBlock('greg_entity_set'),number=w.newBlock('math_number');event.setFieldValue('demo:orb','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('animation_speed','PROPERTY');set.getInput('VALUE').connection.connect(number.outputConnection);number.setFieldValue(0.5,'NUM');assert.match(L.generate(w),/entity\.set\(handle, \{animation_speed = 0\.5\}\)/);w.dispose();
});
test('object offsets, tint and draw order compile to general presentation properties',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),offset=w.newBlock('greg_entity_set'),number=w.newBlock('math_number'),tint=w.newBlock('greg_entity_tint'),layer=w.newBlock('greg_entity_layer'),reset=w.newBlock('greg_entity_tint_reset');event.setFieldValue('demo:orb','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(offset.previousConnection);offset.setFieldValue('visual_offset_x','PROPERTY');number.setFieldValue(2.5,'NUM');offset.getInput('VALUE').connection.connect(number.outputConnection);offset.nextConnection.connect(tint.previousConnection);tint.setFieldValue('#12ab34ff','COLOR');tint.nextConnection.connect(layer.previousConnection);layer.setFieldValue('behind','LAYER');layer.nextConnection.connect(reset.previousConnection);
 const source=L.generate(w);assert.match(source,/visual_offset_x = 2\.5/);assert.match(source,/visual_tint = '#12AB34FF'/);assert.match(source,/draw_layer = 'behind'/);assert.match(source,/visual_tint = 'default'/);w.dispose();
});
test('player color and visibility blocks compile to scoped presentation overrides',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_player_health_event'),tint=w.newBlock('greg_player_tint'),resetTint=w.newBlock('greg_player_tint_reset'),visible=w.newBlock('greg_player_visibility'),yes=w.newBlock('logic_boolean'),reset=w.newBlock('greg_player_presentation_reset');
 event.setFieldValue('1','PLAYER');event.setFieldValue('health_changed','EVENT');event.getInput('DO').connection.connect(tint.previousConnection);
 tint.setFieldValue('current','PLAYER');tint.setFieldValue('clothing_tint','CHANNEL');tint.setFieldValue('#12ab34ff','COLOR');tint.nextConnection.connect(resetTint.previousConnection);
 resetTint.setFieldValue('current','PLAYER');resetTint.setFieldValue('skin_tint','CHANNEL');resetTint.nextConnection.connect(visible.previousConnection);
 visible.setFieldValue('current','PLAYER');visible.getInput('VISIBLE').connection.connect(yes.outputConnection);yes.setFieldValue('TRUE','BOOL');visible.nextConnection.connect(reset.previousConnection);reset.setFieldValue('current','PLAYER');
 const source=L.generate(w);assert.match(source,/map\.set_player_presentation\(player, \{clothing_tint = '#12AB34FF'\}\)/);assert.match(source,/\{skin_tint = 'default'\}/);assert.match(source,/\{body_visible = true\}/);assert.match(source,/map\.reset_player_presentation\(player\)/);w.dispose();
});
test('player sprite blocks use and inspect current-map object visuals without changing native state',()=>{
 L.setObjectTypes([{key:'demo:hero',label:'Hero',animations:[{name:'jump'}]}]);const w=new B.Workspace(),event=w.newBlock('greg_player_action_event'),sprite=w.newBlock('greg_player_sprite'),branch=w.newBlock('controls_if'),isSprite=w.newBlock('greg_player_sprite_is'),clear=w.newBlock('greg_player_sprite_clear');
 event.setFieldValue('2','PLAYER');event.setFieldValue('pressed','EDGE');event.setFieldValue('jump','ACTION');event.getInput('DO').connection.connect(sprite.previousConnection);sprite.setFieldValue('current','PLAYER');sprite.setFieldValue('demo:hero','TYPE');sprite.setFieldValue('jump','ANIMATION');sprite.setFieldValue('restart','RESTART');sprite.nextConnection.connect(branch.previousConnection);branch.getInput('IF0').connection.connect(isSprite.outputConnection);isSprite.setFieldValue('current','PLAYER');isSprite.setFieldValue('demo:hero','TYPE');isSprite.setFieldValue('jump','ANIMATION');branch.nextConnection.connect(clear.previousConnection);clear.setFieldValue('current','PLAYER');
 assert.ok(sprite.getField('ANIMATION').getOptions(false).some(option=>option[1]==='jump'));assert.ok(isSprite.getField('ANIMATION').getOptions(false).some(option=>option[1]==='jump'));
 sprite.setFieldValue('restore','RESTORE');const source=L.generate(w);assert.match(source,/map\.set_player_sprite\(player\.player, ['"]demo:hero['"], ['"]jump['"], true, true\)/);assert.match(source,/map\.player_sprite\(player\.player\)/);assert.match(source,/picture\.animation == 'jump'/);assert.match(source,/map\.clear_player_sprite\(player\.player\)/);w.dispose();L.setObjectTypes([]);
});
test('player custom picture completion is a general condition',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),branch=w.newBlock('controls_if'),finished=w.newBlock('greg_player_sprite_finished');
 event.getInput('DO').connection.connect(branch.previousConnection);branch.getInput('IF0').connection.connect(finished.outputConnection);finished.setFieldValue('2','PLAYER');
 const source=L.generate(w);assert.match(source,/map\.player_sprite\(2\)/);assert.match(source,/picture\.finished/);w.dispose();
});
test('player custom picture completion event fires from the rollback edge and merges with tick logic',()=>{
 const w=new B.Workspace(),tickEvent=w.newBlock('greg_tick'),event=w.newBlock('greg_player_sprite_event'),clear=w.newBlock('greg_player_sprite_clear');event.setFieldValue('2','PLAYER');event.getInput('DO').connection.connect(clear.previousConnection);clear.setFieldValue('current','PLAYER');
 const source=L.generate(w);assert.equal((source.match(/map\.on_tick/g)||[]).length,1);assert.match(source,/player\.player == 2 and player\.custom_sprite_just_finished/);assert.match(source,/map\.clear_player_sprite\(player\.player\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
});
test('player lifecycle events expose spawn, respawn and room-change history without map variables',()=>{
 const w=new B.Workspace(),tickEvent=w.newBlock('greg_tick'),spawn=w.newBlock('greg_player_lifecycle_event'),respawn=w.newBlock('greg_player_lifecycle_event'),room=w.newBlock('greg_player_lifecycle_event'),set=w.newBlock('greg_variable_set'),previous=w.newBlock('greg_player_value');
 spawn.setFieldValue('1','PLAYER');spawn.setFieldValue('spawned','EVENT');respawn.setFieldValue('2','PLAYER');respawn.setFieldValue('respawned','EVENT');room.setFieldValue('any','PLAYER');room.setFieldValue('room_left','EVENT');room.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('map','SCOPE');set.setFieldValue('old_room','NAME');previous.setFieldValue('previous_room','PROPERTY');set.getInput('VALUE').connection.connect(previous.outputConnection);
 const source=L.generate(w);assert.equal((source.match(/map\.on_tick/g)||[]).length,1);assert.match(source,/player\.player == 1 and player\.spawned/);assert.match(source,/player\.player == 2 and player\.respawned/);assert.match(source,/player\.room_changed/);assert.match(source,/player\.previous_room/);assert.doesNotMatch(source,/__yule|lifecycle.*map\.state/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
});
test('player picture transform blocks expose general draw controls',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),number=w.newBlock('greg_player_sprite_number'),value=w.newBlock('math_number'),tint=w.newBlock('greg_player_sprite_tint'),layer=w.newBlock('greg_player_sprite_layer'),mirror=w.newBlock('greg_player_sprite_mirrored'),yes=w.newBlock('logic_boolean'),visibility=w.newBlock('greg_player_sprite_visibility'),no=w.newBlock('logic_boolean'),reset=w.newBlock('greg_player_sprite_transform_reset');
 event.getInput('DO').connection.connect(number.previousConnection);number.setFieldValue('2','PLAYER');number.setFieldValue('animation_speed','PROPERTY');value.setFieldValue(1.5,'NUM');number.getInput('VALUE').connection.connect(value.outputConnection);number.nextConnection.connect(tint.previousConnection);tint.setFieldValue('2','PLAYER');tint.setFieldValue('#80a0c0ff','COLOR');tint.nextConnection.connect(layer.previousConnection);layer.setFieldValue('2','PLAYER');layer.setFieldValue('front','LAYER');layer.nextConnection.connect(mirror.previousConnection);mirror.setFieldValue('2','PLAYER');yes.setFieldValue('TRUE','BOOL');mirror.getInput('VALUE').connection.connect(yes.outputConnection);mirror.nextConnection.connect(visibility.previousConnection);visibility.setFieldValue('2','PLAYER');no.setFieldValue('FALSE','BOOL');visibility.getInput('VALUE').connection.connect(no.outputConnection);visibility.nextConnection.connect(reset.previousConnection);reset.setFieldValue('2','PLAYER');
 const source=L.generate(w);assert.match(source,/animation_speed = 1\.5/);assert.match(source,/tint = '#80A0C0FF'/);assert.match(source,/draw_layer = 'front'/);assert.match(source,/mirrored = true/);assert.match(source,/visible = false/);assert.match(source,/map\.reset_player_sprite_transform\(2\)/);w.dispose();
});
test('object rotation is a general numeric presentation property',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),set=w.newBlock('greg_entity_set'),number=w.newBlock('math_number');event.setFieldValue('demo:orb','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('visual_rotation','PROPERTY');number.setFieldValue(90,'NUM');set.getInput('VALUE').connection.connect(number.outputConnection);assert.match(L.generate(w),/visual_rotation = 90/);w.dispose();
});
test('named animations and completion compile from the current object design without special meanings',()=>{
 L.setObjectTypes([{key:'demo:orb',label:'Orb',animations:[{name:'idle'},{name:'open'}]}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),play=w.newBlock('greg_entity_animation'),condition=w.newBlock('controls_if'),both=w.newBlock('logic_operation'),isAnimation=w.newBlock('greg_entity_animation_is'),finished=w.newBlock('greg_entity_animation_finished');event.setFieldValue('demo:orb','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(play.previousConnection);play.setFieldValue('open','ANIMATION');play.setFieldValue('continue','RESTART');play.nextConnection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(both.outputConnection);both.setFieldValue('AND','OP');both.getInput('A').connection.connect(isAnimation.outputConnection);both.getInput('B').connection.connect(finished.outputConnection);isAnimation.setFieldValue('open','ANIMATION');
 const source=L.generate(w);assert.match(source,/entity\.play_animation\(handle, ['"]open['"], false\)/);assert.match(source,/entity\.animation\(handle\) == ['"]open['"]/);assert.match(source,/entity\.animation_status\(handle\)\.finished/);w.dispose();L.setObjectTypes([]);
});
test('animation completion is an object event and receives the finished object',()=>{
 L.setObjectTypes([{key:'demo:door',animations:[{name:'open'}]}]);const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),reset=w.newBlock('greg_entity_animation');event.setFieldValue('demo:door','TYPE');event.setFieldValue('animation_finish','EVENT');event.getInput('DO').connection.connect(reset.previousConnection);reset.setFieldValue('default','ANIMATION');const source=L.generate(w);assert.match(source,/entity\.on_animation_finish\(['"]demo:door['"], function\(handle, animation\)/);assert.match(source,/entity\.play_animation\(handle, ['"]default['"], true\)/);w.dispose();L.setObjectTypes([]);
});

test('remove-this-object ends its statement stack and requires a live object event',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),remove=w.newBlock('greg_entity_remove');event.setFieldValue('demo:orb','TYPE');
 event.getInput('DO').connection.connect(remove.previousConnection);assert.equal(remove.nextConnection,null);assert.match(L.generate(w),/entity.remove/);
 event.setFieldValue('remove','EVENT');assert.throws(()=>L.generate(w),/update or creation/);w.dispose();
});

test('object picker uses current-map designs and preserves missing serialized IDs',()=>{
 L.setObjectTypes([{key:'demo:orb',label:'Orb'},{key:'demo:pad',label:'Launch pad'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event');
 assert.equal(event.getFieldValue('TYPE'),'demo:orb');
 assert.deepEqual(event.getField('TYPE').getOptions(false),[['Orb','demo:orb'],['Launch pad','demo:pad']]);
 event.setFieldValue('old:removed','TYPE');const saved=L.save(w);
 const restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);
 const field=restored.getTopBlocks()[0].getField('TYPE');assert.equal(field.getValue(),'old:removed');
 assert.ok(field.getOptions(false).some(option=>option[0]==='Missing object: old:removed'));
 w.dispose();restored.dispose();L.setObjectTypes([]);
});

test('object callback blocks round trip independently and retain handwritten bodies',()=>{
 const w=new B.Workspace();const source='-- authored body\nentity.set(handle, {vx=2})';
 L.restoreCallback(w,null,source,'demo:orb','update');assert.equal(L.saveCallback(w,'demo:orb','update').source,source);
 const saved=L.saveCallback(w,'demo:orb','update');assert.equal(L.restoreCallback(w,saved,source,'demo:orb','update'),true);
 w.clear();L.restoreCallback(w,null,'','demo:orb','update');const action=w.newBlock('greg_entity_flag');w.getTopBlocks().find(b=>b.type==='greg_entity_event').getInput('DO').connection.connect(action.previousConnection);
 const generated=L.saveCallback(w,'demo:orb','update');assert.equal(generated.source,'entity.set(handle, {animation_paused = false})');
 assert.equal(L.restoreCallback(w,generated,generated.source,'demo:orb','update'),true);w.dispose();
});
test('timing, deterministic random and math blocks generate inside repeated actions',()=>{
 const {workspace:w,event}=tick(),loop=w.newBlock('controls_repeat_ext'),set=w.newBlock('greg_state_set'),random=w.newBlock('greg_random');
 event.getInput('DO').connection.connect(loop.previousConnection);loop.getInput('DO').connection.connect(set.previousConnection);set.getInput('VALUE').connection.connect(random.outputConnection);
 const generated=L.generate(w);assert.match(generated,/local low, high = 1, 10/);assert.match(generated,/if low > high then low, high = high, low end/);assert.match(generated,/map.random\(low, high\)/);w.dispose();
});

test('general math helpers cover clamping, interpolation, ranges and 2D geometry',()=>{
 const {workspace:w,event}=tick(),types=['greg_math_clamp','greg_math_lerp','greg_math_map_range','greg_math_distance','greg_math_direction'];let previous=null;
 types.forEach((type,index)=>{const set=w.newBlock('greg_state_set'),expression=w.newBlock(type);set.setFieldValue('math_'+index,'KEY');set.getInput('VALUE').connection.connect(expression.outputConnection);if(previous)previous.nextConnection.connect(set.previousConnection);else event.getInput('DO').connection.connect(set.previousConnection);previous=set;});
 ['sign','sin_deg','round'].forEach((fn,index)=>{const set=w.newBlock('greg_state_set'),expression=w.newBlock('greg_math_function');set.setFieldValue('function_'+index,'KEY');expression.setFieldValue(fn,'FUNCTION');set.getInput('VALUE').connection.connect(expression.outputConnection);previous.nextConnection.connect(set.previousConnection);previous=set;});
 const source=L.generate(w);assert.match(source,/if low > high then low, high = high, low end/);assert.match(source,/a \+ \(target - a\) \* amount/);assert.match(source,/if in_low == in_high then return out_low end/);assert.match(source,/root = \(root \+ normalized \/ root\) \* 0\.5/);assert.match(source,/local function atan_degrees/);assert.match(source,/n < 0 and -1 or \(n > 0 and 1 or 0\)/);assert.match(source,/local term, result = radians, radians/);assert.match(source,/n < 0 and math\.ceil\(n - 0\.5\) or math\.floor\(n \+ 0\.5\)/);assert.doesNotMatch(source,/math\.(sqrt|atan2|sin|cos|tan|pi)/);
 const math=L.toolbox.contents.find(category=>category.name==='Math').contents.map(item=>item.type);types.forEach(type=>assert.ok(math.includes(type),type+' is exposed in Math'));
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
});

test('while and counted for loops are supported, serialized, and exposed in Logic',()=>{
 const w=new B.Workspace(),counter=w.getVariableMap().createVariable('step'),event=w.newBlock('greg_tick'),whileLoop=w.newBlock('controls_whileUntil'),keepGoing=w.newBlock('logic_boolean'),forLoop=w.newBlock('controls_for'),from=w.newBlock('math_number'),to=w.newBlock('math_number'),by=w.newBlock('math_number'),set=w.newBlock('greg_state_set'),number=w.newBlock('math_number');
 event.getInput('DO').connection.connect(whileLoop.previousConnection);whileLoop.setFieldValue('WHILE','MODE');keepGoing.setFieldValue('FALSE','BOOL');whileLoop.getInput('BOOL').connection.connect(keepGoing.outputConnection);whileLoop.getInput('DO').connection.connect(forLoop.previousConnection);forLoop.setFieldValue(counter.getId(),'VAR');from.setFieldValue(1,'NUM');to.setFieldValue(3,'NUM');by.setFieldValue(1,'NUM');forLoop.getInput('FROM').connection.connect(from.outputConnection);forLoop.getInput('TO').connection.connect(to.outputConnection);forLoop.getInput('BY').connection.connect(by.outputConnection);forLoop.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('looped','KEY');number.setFieldValue(1,'NUM');set.getInput('VALUE').connection.connect(number.outputConnection);
 const source=L.generate(w);assert.match(source,/while false do/);assert.match(source,/for step = 1, 3, 1 do/);assert.match(source,/map\.state\['looped'\] = 1/);
 const logic=L.toolbox.contents.find(category=>category.name==='Logic').contents.map(item=>item.type);assert.ok(logic.includes('controls_whileUntil'));assert.ok(logic.includes('controls_for'));
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
});

test('stop-this-loop emits Lua break and rejects use outside a loop',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),loop=w.newBlock('controls_whileUntil'),condition=w.newBlock('logic_boolean'),stop=w.newBlock('greg_break');event.getInput('DO').connection.connect(loop.previousConnection);condition.setFieldValue('TRUE','BOOL');loop.getInput('BOOL').connection.connect(condition.outputConnection);loop.getInput('DO').connection.connect(stop.previousConnection);assert.match(L.generate(w),/while true do\n    break\n  end/);
 const logic=L.toolbox.contents.find(category=>category.name==='Logic').contents.map(item=>item.type);assert.ok(logic.includes('greg_break'));stop.unplug();loop.dispose(false);event.getInput('DO').connection.connect(stop.previousConnection);assert.throws(()=>L.generate(w),/belongs inside a repeat, while, for, player, or object loop/);w.dispose();
});

test('bounded lists support deterministic creation, search, indexing and iteration',()=>{
 const w=new B.Workspace(),item=w.getVariableMap().createVariable('item'),event=w.newBlock('greg_tick'),loop=w.newBlock('controls_forEach'),list=w.newBlock('lists_create_with'),first=w.newBlock('math_number'),second=w.newBlock('math_number'),third=w.newBlock('math_number'),store=w.newBlock('greg_state_set'),get=w.newBlock('variables_get');
 first.setFieldValue(3,'NUM');second.setFieldValue(5,'NUM');third.setFieldValue(8,'NUM');list.getInput('ADD0').connection.connect(first.outputConnection);list.getInput('ADD1').connection.connect(second.outputConnection);list.getInput('ADD2').connection.connect(third.outputConnection);
 loop.setFieldValue(item.getId(),'VAR');event.getInput('DO').connection.connect(loop.previousConnection);loop.getInput('LIST').connection.connect(list.outputConnection);loop.getInput('DO').connection.connect(store.previousConnection);store.setFieldValue('seen','KEY');get.setFieldValue(item.getId(),'VAR');store.getInput('VALUE').connection.connect(get.outputConnection);
 const source=L.generate(w);assert.match(source,/local list = \{3, 5, 8\}/);assert.match(source,/List items cannot be empty/);assert.match(source,/if #list > 32/);assert.match(source,/for _, item in ipairs\(list\) do/);assert.match(source,/map\.state\['seen'\] = item/);const copy=new B.Workspace(),saved=L.save(w);assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);copy.dispose();
 const lists=L.toolbox.contents.find(category=>category.name==='Data').contents.map(entry=>entry.type);assert.ok(lists.includes('controls_forEach')&&lists.includes('lists_getIndex')&&lists.includes('lists_indexOf'));
 w.dispose();
 const q=new B.Workspace(),qEvent=q.newBlock('greg_tick'),qStore=q.newBlock('greg_state_set'),search=q.newBlock('lists_indexOf'),searchList=q.newBlock('lists_create_with'),listed=q.newBlock('math_number'),needle=q.newBlock('math_number');searchList.itemCount_=1;searchList.updateShape_();qEvent.getInput('DO').connection.connect(qStore.previousConnection);qStore.getInput('VALUE').connection.connect(search.outputConnection);search.setFieldValue('LAST','END');listed.setFieldValue(2,'NUM');needle.setFieldValue(2,'NUM');searchList.getInput('ADD0').connection.connect(listed.outputConnection);search.getInput('VALUE').connection.connect(searchList.outputConnection);search.getInput('FIND').connection.connect(needle.outputConnection);assert.match(L.generate(q),/for i = #list, 1, -1 do/);assert.match(L.generate(q),/rawequal\(list\[i\], wanted\)/);q.dispose();
 const r=new B.Workspace(),rEvent=r.newBlock('greg_tick'),rStore=r.newBlock('greg_state_set'),read=r.newBlock('lists_getIndex'),readList=r.newBlock('lists_create_with');readList.itemCount_=0;readList.updateShape_();rEvent.getInput('DO').connection.connect(rStore.previousConnection);rStore.getInput('VALUE').connection.connect(read.outputConnection);read.setFieldValue('GET','MODE');read.setFieldValue('RANDOM','WHERE');read.getInput('VALUE').connection.connect(readList.outputConnection);assert.match(L.generate(r),/map\.random\(1, #list\)/);read.setFieldValue('REMOVE','MODE');rStore.nextConnection.connect(read.previousConnection);assert.throws(()=>L.generate(r),/read-only/);r.dispose();
});

test('vector data blocks compose temporary positions and directions',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),store=w.newBlock('greg_state_set'),component=w.newBlock('greg_vector_component'),normalized=w.newBlock('greg_vector_normalize'),vector=w.newBlock('greg_vector_create'),x=w.newBlock('math_number'),y=w.newBlock('math_number');event.getInput('DO').connection.connect(store.previousConnection);store.setFieldValue('normal_x','KEY');store.getInput('VALUE').connection.connect(component.outputConnection);component.setFieldValue('x','COMPONENT');component.getInput('VECTOR').connection.connect(normalized.outputConnection);normalized.getInput('VECTOR').connection.connect(vector.outputConnection);x.setFieldValue(3,'NUM');y.setFieldValue(4,'NUM');vector.getInput('X').connection.connect(x.outputConnection);vector.getInput('Y').connection.connect(y.outputConnection);
 const source=L.generate(w);assert.match(source,/return \{x = x, y = y\}/);assert.match(source,/root = \(root \+ normalized \/ root\) \* 0\.5/);assert.doesNotMatch(source,/math\.sqrt/);assert.match(source,/return vector\.x/);const copy=new B.Workspace(),saved=L.save(w);assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
 const math=new B.Workspace(),mathEvent=math.newBlock('greg_tick'),mathStore=math.newBlock('greg_state_set'),length=math.newBlock('greg_vector_length'),sum=math.newBlock('greg_vector_math'),a=math.newBlock('greg_vector_create'),b=math.newBlock('greg_vector_create');mathEvent.getInput('DO').connection.connect(mathStore.previousConnection);mathStore.getInput('VALUE').connection.connect(length.outputConnection);length.getInput('VECTOR').connection.connect(sum.outputConnection);sum.setFieldValue('subtract','OP');sum.getInput('A').connection.connect(a.outputConnection);sum.getInput('B').connection.connect(b.outputConnection);for(const block of [a,b]){for(const name of ['X','Y']){const number=math.newBlock('math_number');number.setFieldValue(block===a?2:1,'NUM');block.getInput(name).connection.connect(number.outputConnection);}}const mathSource=L.generate(math);assert.match(mathSource,/x = a\.x - b\.x/);assert.match(mathSource,/Vector math needs two vectors/);math.dispose();
 const conversion=L.toolbox.contents.find(category=>category.name==='Data').contents.map(entry=>entry.type);assert.ok(conversion.includes('greg_vector_scale')&&conversion.includes('greg_vector_normalize')&&conversion.includes('greg_to_text')&&conversion.includes('greg_to_boolean')&&conversion.includes('greg_to_number'));
 const converted=new B.Workspace(),convertedEvent=converted.newBlock('greg_tick'),convertedStore=converted.newBlock('greg_state_set'),toNumber=converted.newBlock('greg_to_number'),text=converted.newBlock('text'),fallback=converted.newBlock('math_number');convertedEvent.getInput('DO').connection.connect(convertedStore.previousConnection);convertedStore.getInput('VALUE').connection.connect(toNumber.outputConnection);toNumber.getInput('VALUE').connection.connect(text.outputConnection);toNumber.getInput('FALLBACK').connection.connect(fallback.outputConnection);text.setFieldValue('-12.5','TEXT');fallback.setFieldValue(99,'NUM');const convertedSource=L.generate(converted);assert.match(convertedSource,/string\.byte\(input, i\)/);assert.match(convertedSource,/number = number \* sign/);assert.match(convertedSource,/return fallback/);converted.dispose();
});

test('repeat-until and stop-event compile as ordinary control flow',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),loop=w.newBlock('controls_whileUntil'),done=w.newBlock('logic_boolean'),stop=w.newBlock('greg_stop_script');loop.setFieldValue('UNTIL','MODE');done.setFieldValue('TRUE','BOOL');event.getInput('DO').connection.connect(loop.previousConnection);loop.getInput('BOOL').connection.connect(done.outputConnection);loop.getInput('DO').connection.connect(stop.previousConnection);
 const source=L.generate(w);assert.match(source,/while not true do/);assert.match(source,/return/);const logic=L.toolbox.contents.find(category=>category.name==='Logic').contents.map(entry=>entry.type);assert.ok(logic.includes('greg_stop_script'));w.dispose();
 const invalid=new B.Workspace(),orphan=invalid.newBlock('greg_stop_script');assert.throws(()=>L.generate(invalid),/Connect actions and values inside an event|belongs inside an event or function/);orphan.dispose(false);invalid.dispose();
});

test('functions support parameters and returned values under the same VM budget',()=>{
 const w=new B.Workspace(),parameter=w.getVariableMap().createVariable('amount',undefined,'test-function-amount'),definition=w.newBlock('procedures_defreturn');definition.arguments_=['amount'];definition.argumentVarModels_=[parameter];definition.updateParams_();definition.setFieldValue('twice','NAME');
 const get=w.newBlock('variables_get'),two=w.newBlock('math_number'),multiply=w.newBlock('greg_math');get.setFieldValue(parameter.getId(),'VAR');two.setFieldValue(2,'NUM');multiply.setFieldValue('*','OP');multiply.getInput('A').connection.connect(get.outputConnection);multiply.getInput('B').connection.connect(two.outputConnection);definition.getInput('RETURN').connection.connect(multiply.outputConnection);
 const event=w.newBlock('greg_tick'),set=w.newBlock('greg_state_set'),call=w.newBlock('procedures_callreturn'),three=w.newBlock('math_number');call.arguments_=['amount'];call.argumentVarModels_=[parameter];call.renameProcedure('', 'twice');call.updateShape_();three.setFieldValue(3,'NUM');call.getInput('ARG0').connection.connect(three.outputConnection);event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('result','KEY');set.getInput('VALUE').connection.connect(call.outputConnection);
 const source=L.generate(w);assert.match(source,/local function twice\(amount\)/);assert.match(source,/return \(amount \* 2\)/);assert.match(source,/map\.state\['result'\] = twice\(3\)/);const saved=L.save(w);assert.ok(saved.workspace.blocks.blocks.some(block=>block.type==='procedures_defreturn'&&block.extraState&&block.extraState.params&&block.extraState.params[0].name==='amount'));assert.ok(L.toolbox.contents.some(category=>category.name==='Functions'&&category.custom==='PROCEDURE'));w.dispose();
});

test('parameterless functions round trip and calls require a matching definition',()=>{
 const w=new B.Workspace(),definition=w.newBlock('procedures_defreturn'),one=w.newBlock('math_number'),event=w.newBlock('greg_tick'),set=w.newBlock('greg_state_set'),call=w.newBlock('procedures_callreturn');definition.setFieldValue('one','NAME');one.setFieldValue(1,'NUM');definition.getInput('RETURN').connection.connect(one.outputConnection);call.renameProcedure('', 'one');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('answer','KEY');set.getInput('VALUE').connection.connect(call.outputConnection);
 const saved=L.save(w),copy=new B.Workspace();assert.match(saved.source,/function one\(\)/);assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);definition.dispose(false);assert.throws(()=>L.generate(w),/matching function definition/);w.dispose();copy.dispose();
});

test('object callbacks keep reusable functions local and retain this-object context',()=>{
 L.setObjectTypes([{key:'demo:orb',label:'Orb'}]);const w=new B.Workspace(),root=w.newBlock('greg_entity_event'),definition=w.newBlock('procedures_defnoreturn'),set=w.newBlock('greg_entity_set'),number=w.newBlock('math_number'),call=w.newBlock('procedures_callnoreturn');root.setFieldValue('demo:orb','TYPE');root.setFieldValue('update','EVENT');definition.setFieldValue('move_orb','NAME');set.setFieldValue('x','PROPERTY');number.setFieldValue(7,'NUM');set.getInput('VALUE').connection.connect(number.outputConnection);definition.getInput('STACK').connection.connect(set.previousConnection);call.renameProcedure('', 'move_orb');root.getInput('DO').connection.connect(call.previousConnection);
 const saved=L.saveCallback(w,'demo:orb','update');assert.match(saved.source,/^local function move_orb\(\)/);assert.match(saved.source,/entity\.set\(handle, \{x = 7\}\)/);assert.match(saved.source,/move_orb\(\)$/);assert.doesNotMatch(saved.source,/entity\.on_update/);
 const copy=new B.Workspace();assert.equal(L.restoreCallback(copy,saved,saved.source,'demo:orb','update'),true);assert.equal(L.saveCallback(copy,'demo:orb','update').source,saved.source);assert.equal(copy.getTopBlocks().filter(block=>block.type==='procedures_defnoreturn').length,1);w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('player reporters select either slot and this player in incoming-damage checks',()=>{
 L.setObjectTypes([{key:'demo:target',label:'Target'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),allow=w.newBlock('greg_damage_filter_set'),holding=w.newBlock('greg_player_action');event.setFieldValue('demo:target','TYPE');event.setFieldValue('damage_filter','EVENT');event.getInput('DO').connection.connect(allow.previousConnection);holding.setFieldValue('current','PLAYER');holding.setFieldValue('input','EDGE');holding.setFieldValue('attack','ACTION');allow.getInput('ALLOWED').connection.connect(holding.outputConnection);
 const source=L.generate(w);assert.match(source,/local selected_player = damage\.source_player/);assert.match(source,/candidate\['input'\]\['attack'\]/);assert.match(source,/return false/);
 const tickEvent=w.newBlock('greg_tick'),set=w.newBlock('greg_state_set'),health=w.newBlock('greg_player_value');tickEvent.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('p2_health','KEY');health.setFieldValue('2','PLAYER');health.setFieldValue('health','PROPERTY');set.getInput('VALUE').connection.connect(health.outputConnection);const withHealth=L.generate(w);assert.match(withHealth,/local selected_player = 2/);assert.match(withHealth,/candidate\['health'\] or 0/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,withHealth),true);assert.equal(L.generate(copy),withHealth);w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('active-player loop supplies observations and a scoped action target',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),action=w.newBlock('greg_velocity'),x=w.newBlock('greg_player_value');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(action.previousConnection);action.setFieldValue('current','PLAYER');x.setFieldValue('vx','PROPERTY');action.getInput('X').connection.connect(x.outputConnection);
 const source=L.generate(w);assert.match(source,/ipairs\(map.players\(\)\)/);assert.match(source,/map.set_player_velocity\(player.player, player.vx, 0\)/);
 action.unplug();event.getInput('DO').connection.disconnect();players.dispose();event.getInput('DO').connection.connect(action.previousConnection);assert.throws(()=>L.generate(w),/for-each-player/);w.dispose();
});

test('player references flow through general observations and actions',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),players=w.newBlock('greg_players'),branch=w.newBlock('controls_if'),exists=w.newBlock('greg_player_reference_exists'),existsPlayer=w.newBlock('greg_player_reference'),velocity=w.newBlock('greg_player_reference_velocity'),velocityPlayer=w.newBlock('greg_player_reference'),x=w.newBlock('greg_player_reference_number'),xPlayer=w.newBlock('greg_player_reference'),zero=w.newBlock('math_number'),storeText=w.newBlock('greg_variable_set'),text=w.newBlock('greg_player_reference_text'),textPlayer=w.newBlock('greg_player_reference'),storeCondition=w.newBlock('greg_variable_set'),condition=w.newBlock('greg_player_reference_condition'),conditionPlayer=w.newBlock('greg_player_reference');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(branch.previousConnection);existsPlayer.setFieldValue('current','PLAYER');exists.getInput('PLAYER').connection.connect(existsPlayer.outputConnection);branch.getInput('IF0').connection.connect(exists.outputConnection);branch.getInput('DO0').connection.connect(velocity.previousConnection);
 velocityPlayer.setFieldValue('current','PLAYER');velocity.getInput('PLAYER').connection.connect(velocityPlayer.outputConnection);xPlayer.setFieldValue('current','PLAYER');x.setFieldValue('x','PROPERTY');x.getInput('PLAYER').connection.connect(xPlayer.outputConnection);velocity.getInput('X').connection.connect(x.outputConnection);zero.setFieldValue(0,'NUM');velocity.getInput('Y').connection.connect(zero.outputConnection);
 velocity.nextConnection.connect(storeText.previousConnection);storeText.setFieldValue('map','SCOPE');storeText.setFieldValue('player_picture','NAME');textPlayer.setFieldValue('current','PLAYER');text.setFieldValue('custom_sprite_type','PROPERTY');text.getInput('PLAYER').connection.connect(textPlayer.outputConnection);storeText.getInput('VALUE').connection.connect(text.outputConnection);
 storeText.nextConnection.connect(storeCondition.previousConnection);storeCondition.setFieldValue('map','SCOPE');storeCondition.setFieldValue('player_grounded','NAME');conditionPlayer.setFieldValue('current','PLAYER');condition.setFieldValue('grounded','PROPERTY');condition.getInput('PLAYER').connection.connect(conditionPlayer.outputConnection);storeCondition.getInput('VALUE').connection.connect(condition.outputConnection);
 const saved=L.save(w);assert.match(saved.source,/local selected_player = player\.player/);assert.match(saved.source,/candidate\['x'\]/);assert.match(saved.source,/candidate\['custom_sprite_type'\]/);assert.match(saved.source,/candidate\['grounded'\]/);assert.match(saved.source,/map\.set_player_velocity\(selected_player/);
 const copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);w.dispose();copy.dispose();
});

test('player reference position, damage, and defeat accept reusable fixed-player values',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),move=w.newBlock('greg_player_reference_position'),movePlayer=w.newBlock('greg_player_reference'),x=w.newBlock('math_number'),y=w.newBlock('math_number'),damage=w.newBlock('greg_player_reference_damage'),damagePlayer=w.newBlock('greg_player_reference'),amount=w.newBlock('math_number'),defeat=w.newBlock('greg_player_reference_defeat'),defeatPlayer=w.newBlock('greg_player_reference');event.getInput('DO').connection.connect(move.previousConnection);movePlayer.setFieldValue('2','PLAYER');move.getInput('PLAYER').connection.connect(movePlayer.outputConnection);x.setFieldValue(100,'NUM');y.setFieldValue(50,'NUM');move.getInput('X').connection.connect(x.outputConnection);move.getInput('Y').connection.connect(y.outputConnection);move.nextConnection.connect(damage.previousConnection);damagePlayer.setFieldValue('2','PLAYER');damage.getInput('PLAYER').connection.connect(damagePlayer.outputConnection);amount.setFieldValue(3,'NUM');damage.getInput('AMOUNT').connection.connect(amount.outputConnection);damage.nextConnection.connect(defeat.previousConnection);defeatPlayer.setFieldValue('2','PLAYER');defeat.getInput('PLAYER').connection.connect(defeatPlayer.outputConnection);
 const source=L.generate(w);assert.match(source,/map\.set_player_position\(selected_player, 100, 50\)/);assert.match(source,/map\.damage_player\(selected_player, 3\)/);assert.match(source,/map\.defeat_player\(selected_player\)/);
 const playerBlocks=L.toolbox.contents.find(category=>category.name==='Players').contents.map(item=>item.type);assert.ok(['greg_player_reference','greg_player_reference_exists','greg_player_reference_number','greg_player_reference_text','greg_player_reference_condition','greg_player_reference_velocity','greg_player_reference_position','greg_player_reference_damage','greg_player_reference_defeat'].every(type=>playerBlocks.includes(type)));w.dispose();
});

test('player reference consumers reject missing values and this-player without a player scope',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),move=w.newBlock('greg_player_reference_position');event.getInput('DO').connection.connect(move.previousConnection);assert.throws(()=>L.generate(w),/Connect a player/);w.dispose();
 const current=new B.Workspace(),tickEvent=current.newBlock('greg_tick'),reference=current.newBlock('greg_player_reference'),set=current.newBlock('greg_variable_set');tickEvent.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('map','SCOPE');set.setFieldValue('player','NAME');set.getInput('VALUE').connection.connect(reference.outputConnection);assert.throws(()=>L.generate(current),/This player reporter belongs/);current.dispose();
});
test('player numeric observations expose room, facing, native state, collision and palette data',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),set=w.newBlock('greg_variable_set'),value=w.newBlock('greg_player_value');event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('map','SCOPE');set.setFieldValue('observed','NAME');value.setFieldValue('facing','PROPERTY');set.getInput('VALUE').connection.connect(value.outputConnection);assert.match(L.generate(w),/player\.facing/);w.dispose();
});
test('player picture and appearance observations are available as typed beginner values',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),setText=w.newBlock('greg_variable_set'),text=w.newBlock('greg_player_text'),setFrame=w.newBlock('greg_variable_set'),frame=w.newBlock('greg_player_value'),branch=w.newBlock('controls_if'),condition=w.newBlock('greg_player_condition');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(setText.previousConnection);setText.setFieldValue('map','SCOPE');setText.setFieldValue('picture_name','NAME');text.setFieldValue('custom_sprite_animation','PROPERTY');setText.getInput('VALUE').connection.connect(text.outputConnection);setText.nextConnection.connect(setFrame.previousConnection);setFrame.setFieldValue('map','SCOPE');setFrame.setFieldValue('picture_frame','NAME');frame.setFieldValue('custom_sprite_frame','PROPERTY');setFrame.getInput('VALUE').connection.connect(frame.outputConnection);setFrame.nextConnection.connect(branch.previousConnection);condition.setFieldValue('custom_sprite_restore','PROPERTY');branch.getInput('IF0').connection.connect(condition.outputConnection);
 const source=L.generate(w);assert.match(source,/\(player\.custom_sprite_animation or ''\)/);assert.match(source,/\(player\.custom_sprite_frame or 0\)/);assert.match(source,/player\.custom_sprite_restore/);assert.ok(L.toolbox.contents.find(category=>category.name==='Players').contents.some(item=>item.type==='greg_player_text'));w.dispose();
});
test('player position block supports fixed and current players',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),move=w.newBlock('greg_player_position'),x=w.newBlock('math_number'),y=w.newBlock('math_number');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(move.previousConnection);move.setFieldValue('current','PLAYER');x.setFieldValue(544,'NUM');y.setFieldValue(96,'NUM');move.getInput('X').connection.connect(x.outputConnection);move.getInput('Y').connection.connect(y.outputConnection);
 assert.match(L.generate(w),/map\.set_player_position\(player\.player, 544, 96\)/);
 move.setFieldValue('2','PLAYER');assert.match(L.generate(w),/map\.set_player_position\(2, 544, 96\)/);w.dispose();
});
test('player action events are rebinding-aware and provide this-player scope',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_player_action_event'),move=w.newBlock('greg_player_position'),x=w.newBlock('greg_player_value'),y=w.newBlock('math_number');
 event.setFieldValue('2','PLAYER');event.setFieldValue('released','EDGE');event.setFieldValue('jump','ACTION');
 event.getInput('DO').connection.connect(move.previousConnection);move.setFieldValue('current','PLAYER');move.getInput('X').connection.connect(x.outputConnection);x.setFieldValue('x','PROPERTY');move.getInput('Y').connection.connect(y.outputConnection);y.setFieldValue(96,'NUM');
 const source=L.generate(w);assert.match(source,/for _, player in ipairs\(map\.players\(\)\)/);assert.match(source,/player\.player == 2 and player\.released\.jump/);assert.match(source,/map\.set_player_position\(player\.player, player\.x, 96\)/);
 const saved=L.save(w),restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),source);restored.dispose();w.dispose();
});
test('player action events share the one deterministic tick callback',()=>{
 const w=new B.Workspace(),tickEvent=w.newBlock('greg_tick'),jump=w.newBlock('greg_player_action_event'),attack=w.newBlock('greg_player_action_event');jump.setFieldValue('pressed','EDGE');jump.setFieldValue('jump','ACTION');attack.setFieldValue('released','EDGE');attack.setFieldValue('attack','ACTION');const source=L.generate(w);assert.equal((source.match(/map\.on_tick/g)||[]).length,1);assert.match(source,/player\.pressed\.jump/);assert.match(source,/player\.released\.attack/);const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);copy.dispose();
 const duplicate=w.newBlock('greg_player_action_event');duplicate.setFieldValue('pressed','EDGE');duplicate.setFieldValue('jump','ACTION');assert.throws(()=>L.generate(w),/Only one block is allowed for each event/);w.dispose();
});
test('generic player action values cover every rollback-derived edge',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),condition=w.newBlock('controls_if'),action=w.newBlock('greg_player_action');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(action.outputConnection);
 action.setFieldValue('released','EDGE');action.setFieldValue('menu','ACTION');assert.match(L.generate(w),/player\.released\.menu/);
 action.setFieldValue('pressed','EDGE');action.setFieldValue('left','ACTION');assert.match(L.generate(w),/player\.pressed\.left/);
 action.setFieldValue('input','EDGE');action.setFieldValue('attack','ACTION');assert.match(L.generate(w),/player\.input\.attack/);w.dispose();
});
test('player state and input conditions expose general boolean primitives',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),condition=w.newBlock('controls_if'),pressed=w.newBlock('greg_player_condition');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(pressed.outputConnection);
 pressed.setFieldValue('pressed.jump','PROPERTY');assert.match(L.generate(w),/player\.pressed\.jump/);
 pressed.setFieldValue('grounded','PROPERTY');assert.match(L.generate(w),/player\.grounded/);
 pressed.setFieldValue('has_sword','PROPERTY');assert.match(L.generate(w),/player\.has_sword/);w.dispose();
});

test('player-loop native fixture is generated by the real block editor',()=>{assert.equal(require('./player-block-fixture')(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-players.lua','utf8'));});

test('player/object overlap condition matches its native fixture',()=>{assert.equal(require('./player-block-fixture').touching(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-player-touching.lua','utf8'));});

test('player/object overlap offers and generates current-object named areas',()=>{
 L.setObjectTypes([{key:'demo:launch_pad',label:'Launch pad',regions:[{name:'trigger',role:'sensor'},{name:'body',role:'body'}]}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),players=w.newBlock('greg_players'),touching=w.newBlock('greg_player_touching');
 event.setFieldValue('demo:launch_pad','TYPE');event.getInput('DO').connection.connect(players.previousConnection);
 const condition=w.newBlock('controls_if');players.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(touching.outputConnection);
 assert.ok(touching.getField('ROLE').getOptions(false).some(option=>option[1]==='name:trigger'));
 touching.setFieldValue('name:trigger','ROLE');assert.match(L.generate(w),/entity\.player_contacts\(handle, 'name:trigger'\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);
 w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('object-type loop supplies this-object context in map logic',()=>{const source=require('./logic-runtime-fixture').group();assert.equal(source,require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-object-group.lua','utf8'));assert.match(source,/entity.exists/);assert.match(source,/entity.type/);});

test('named-object block uses current-map placements and supplies object context',()=>{
 L.setPlacements([{name:'jump_indicator_1'},{name:'jump_indicator_2'}]);
 const {workspace:w,event}=tick(),named=w.newBlock('greg_entity_named'),move=w.newBlock('greg_entity_set'),x=w.newBlock('math_number');
 event.getInput('DO').connection.connect(named.previousConnection);named.setFieldValue('jump_indicator_2','NAME');named.getInput('DO').connection.connect(move.previousConnection);move.setFieldValue('x','PROPERTY');x.setFieldValue(12,'NUM');move.getInput('VALUE').connection.connect(x.outputConnection);
 assert.deepEqual(named.getField('NAME').getOptions(false),[['jump_indicator_1','jump_indicator_1'],['jump_indicator_2','jump_indicator_2']]);
 const saved=L.save(w);assert.match(saved.source,/entity\.find\('jump_indicator_2'\)/);assert.match(saved.source,/entity\.set\(handle, \{x = 12\}\)/);
 const restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),saved.source);
 w.dispose();restored.dispose();L.setPlacements([]);
});

test('object references can be passed to general inspect, change, and remove blocks',()=>{
 L.setPlacements([{name:'main_door',label:'Main door'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),branch=w.newBlock('controls_if'),exists=w.newBlock('greg_object_reference_exists'),existsTarget=w.newBlock('greg_object_named_value'),set=w.newBlock('greg_object_reference_set'),setTarget=w.newBlock('greg_object_named_value'),x=w.newBlock('greg_object_reference_number'),xTarget=w.newBlock('greg_object_named_value'),store=w.newBlock('greg_variable_set'),type=w.newBlock('greg_object_reference_type'),typeTarget=w.newBlock('greg_object_named_value'),remove=w.newBlock('greg_object_reference_remove'),removeTarget=w.newBlock('greg_object_named_value');
 event.getInput('DO').connection.connect(branch.previousConnection);existsTarget.setFieldValue('main_door','NAME');exists.getInput('OBJECT').connection.connect(existsTarget.outputConnection);branch.getInput('IF0').connection.connect(exists.outputConnection);branch.getInput('DO0').connection.connect(set.previousConnection);
 setTarget.setFieldValue('main_door','NAME');set.getInput('OBJECT').connection.connect(setTarget.outputConnection);set.setFieldValue('x','PROPERTY');xTarget.setFieldValue('main_door','NAME');x.getInput('OBJECT').connection.connect(xTarget.outputConnection);set.getInput('VALUE').connection.connect(x.outputConnection);
 set.nextConnection.connect(store.previousConnection);store.setFieldValue('map','SCOPE');store.setFieldValue('kind','NAME');typeTarget.setFieldValue('main_door','NAME');type.getInput('OBJECT').connection.connect(typeTarget.outputConnection);store.getInput('VALUE').connection.connect(type.outputConnection);
 store.nextConnection.connect(remove.previousConnection);removeTarget.setFieldValue('main_door','NAME');remove.getInput('OBJECT').connection.connect(removeTarget.outputConnection);
 assert.ok(existsTarget.getField('NAME').getOptions(false).some(option=>option[0]==='Main door'&&option[1]==='main_door'));
 const saved=L.save(w);assert.match(saved.source,/target ~= nil and entity\.exists\(target\)/);assert.match(saved.source,/entity\.get\(target\)\.x/);assert.match(saved.source,/entity\.set\(target, \{x =/);assert.match(saved.source,/return entity\.type\(target\)/);assert.match(saved.source,/entity\.remove\(target\)/);
 const copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),saved.source);
 const objectBlocks=L.toolbox.contents.find(category=>category.name==='Objects').contents.map(item=>item.type);assert.ok(['greg_object_named_value','greg_object_reference_exists','greg_object_reference_type','greg_object_reference_number','greg_object_reference_set','greg_object_reference_remove'].every(type=>objectBlocks.includes(type)));
 w.dispose();copy.dispose();L.setPlacements([]);
});

test('object reference actions require a connected reference and event scope',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),set=w.newBlock('greg_object_reference_set');event.getInput('DO').connection.connect(set.previousConnection);assert.throws(()=>L.generate(w),/Connect an object/);w.dispose();
 const outside=new B.Workspace(),named=outside.newBlock('greg_object_named_value');L.setPlacements([{name:'orb'}]);named.setFieldValue('orb','NAME');assert.throws(()=>L.generate(outside),/inside an event/);outside.dispose();L.setPlacements([]);
});

test('double-jump demo ships as editable beginner blocks and exports its exact runtime Lua',t=>{
 const fs=require('node:fs'),path=require('node:path'),dir=path.join(__dirname,'../maps/double_jump_demo');
 if(!fs.existsSync(path.join(dir,'entities.json'))||!fs.existsSync(path.join(dir,'logic.greggnogg.json'))||!fs.existsSync(path.join(dir,'objects.greggnogg.json')))return t.skip('optional local double-jump authoring fixture is not installed');
 const entities=JSON.parse(fs.readFileSync(path.join(dir,'entities.json'),'utf8'));
 const mapSaved=JSON.parse(fs.readFileSync(path.join(dir,'logic.greggnogg.json'),'utf8'));
 const objectSaved=JSON.parse(fs.readFileSync(path.join(dir,'objects.greggnogg.json'),'utf8'));
 const generated=require('./double-jump-block-fixture')();
 assert.equal(mapSaved.source,generated.map.source);assert.equal(objectSaved.scripts['demo:jump_refill'].update,generated.refill.source);
 L.setObjectTypes(entities.types.map(type=>({key:type.key,regions:type.regions||[]})));L.setPlacements(entities.placements);
 const mapWorkspace=new B.Workspace(),objectWorkspace=new B.Workspace();
 assert.equal(L.restore(mapWorkspace,mapSaved,mapSaved.source),true);assert.equal(L.generate(mapWorkspace),mapSaved.source);
 assert.equal(L.restoreCallback(objectWorkspace,objectSaved.blocks['demo:jump_refill'].update,objectSaved.scripts['demo:jump_refill'].update,'demo:jump_refill','update'),true);
 const doc={format:'eggnogg-map/v2',layout:{order:['center']},entities,mapLua:mapSaved.source,mapBlocks:mapSaved,objectScripts:objectSaved.scripts,objectBlocks:objectSaved.blocks,objectMotion:objectSaved.motion};
 const files=require('./object-tools').exportFiles(doc,{});assert.equal(files['map.lua'],fs.readFileSync(path.join(dir,'map.lua'),'utf8'));
 mapWorkspace.dispose();objectWorkspace.dispose();L.setObjectTypes([]);L.setPlacements([]);
});

test('dropdown release opens after drag cancellation, including newly created blocks',()=>{
 const w=new B.Workspace(),listeners={};let opened=0,cancelled=0;
 w.cancelCurrentGesture=()=>cancelled++;
 const container={addEventListener:(name,fn)=>listeners[name]=fn,removeEventListener:name=>delete listeners[name]};
 const dispose=L.bindDropdownInput(container,w);
 const block=w.newBlock('greg_velocity'),field=block.getField('PLAYER'),target={};
 field.getSvgRoot=()=>({contains:value=>value===target});field.isClickable=()=>true;field.showEditor=()=>opened++;
 const event={target,button:0,pointerId:3,preventDefault(){},stopImmediatePropagation(){}};
 listeners.pointerdown(event);assert.equal(opened,0);listeners.pointerup({...event,pointerId:4});assert.equal(opened,0);
 listeners.pointerup(event);assert.equal(opened,1);assert.equal(cancelled,2);
 listeners.pointerdown(event);listeners.pointercancel(event);listeners.pointerup(event);assert.equal(opened,1);
 dispose();assert.equal(Object.keys(listeners).length,0);delete field.getSvgRoot;delete field.isClickable;delete field.showEditor;w.dispose();
});
test('scoped variables preserve player identity, false values and editable serialization',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),players=w.newBlock('greg_players'),set=w.newBlock('greg_variable_set'),get=w.newBlock('greg_variable_get');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(set.previousConnection);
 set.setFieldValue('current','SCOPE');get.setFieldValue('map','SCOPE');set.getInput('VALUE').connection.connect(get.outputConnection);
 const source=L.generate(w);assert.match(source,/player.player/);assert.match(source,/v:m:variable/);assert.match(source,/== nil and 0 or/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);
 set.setFieldValue('this variable name is too long','NAME');assert.equal(set.getFieldValue('NAME'),'variable');
 w.dispose();copy.dispose();
});

test('per-player variable runtime fixture matches actual blocks',()=>assert.equal(require('./player-block-fixture').variables(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-player-variables.lua','utf8')));

test('legacy terrain sensing and reversal remain loadable but stay outside the toolbox',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),sensor=w.newBlock('greg_terrain_ahead'),reverse=w.newBlock('greg_entity_reverse');
 event.setFieldValue('demo:orb','TYPE');event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(sensor.outputConnection);condition.getInput('DO0').connection.connect(reverse.previousConnection);
 const source=L.generate(w);assert.match(source,/map.solid_box/);assert.match(source,/speed == 0/);assert.match(source,/vx = -entity.get/);
 const game=L.toolbox.contents.find(c=>c.name==='Game').contents.map(item=>item.type);
 assert.equal(game.includes('greg_terrain_ahead'),false);assert.equal(game.includes('greg_terrain_box'),false);assert.equal(game.includes('greg_entity_reverse'),false);
 assert.equal(L.toolbox.contents.some(c=>c.name==='Map values'),false);w.dispose();
});

test('automatic movement is an advanced Lua property, not a beginner object toggle',()=>{
 const w=new B.Workspace(),block=w.newBlock('greg_entity_flag');
 assert.equal(block.getField('PROPERTY').getOptions(false).some(option=>option[1]==='automatic_motion'),false);
 w.dispose();
});

test('contact events expose player state and named object areas without geometry code',()=>{
 L.setObjectTypes([{key:'demo:blade',label:'Blade',regions:[{name:'attack',role:'hitbox'}]},{key:'demo:target',label:'Target',regions:[{name:'damage',role:'hurtbox'}]}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),area=w.newBlock('greg_contact_area'),defeat=w.newBlock('greg_defeat');
 event.setFieldValue('demo:blade','TYPE');event.setFieldValue('player_contact','EVENT');event.getInput('DO').connection.connect(condition.previousConnection);area.setFieldValue('name:attack','ROLE');condition.getInput('IF0').connection.connect(area.outputConnection);condition.getInput('DO0').connection.connect(defeat.previousConnection);defeat.setFieldValue('current','PLAYER');
 const source=L.generate(w);assert.match(source,/entity\.on_player_contact\('demo:blade', function\(handle, contact\)/);assert.match(source,/contact\.region_name == 'attack'/);assert.match(source,/map\.defeat_player\(contact\.player\)/);
 defeat.unplug();defeat.dispose();const inputCheck=w.newBlock('controls_if'),pressed=w.newBlock('greg_player_condition');pressed.setFieldValue('pressed.jump','PROPERTY');condition.getInput('DO0').connection.connect(inputCheck.previousConnection);inputCheck.getInput('IF0').connection.connect(pressed.outputConnection);assert.match(L.generate(w),/contact\.pressed\.jump/);
 w.clear();const objectEvent=w.newBlock('greg_entity_event'),other=w.newBlock('greg_contact_other_is');objectEvent.setFieldValue('demo:blade','TYPE');objectEvent.setFieldValue('contact','EVENT');objectEvent.getInput('DO').connection.connect(w.newBlock('controls_if').previousConnection);const branch=objectEvent.getInputTargetBlock('DO');branch.getInput('IF0').connection.connect(other.outputConnection);other.setFieldValue('demo:target','TYPE');assert.match(L.generate(w),/contact\.other_type == 'demo:target'/);
  w.dispose();L.setObjectTypes([]);
});

test('damage blocks deliver typed object events without imposing a health model',()=>{
 L.setObjectTypes([{key:'demo:blade',label:'Blade'},{key:'demo:target',label:'Target'}]);
 const w=new B.Workspace(),contact=w.newBlock('greg_entity_event'),deal=w.newBlock('greg_entity_damage'),amount=w.newBlock('math_number');
 contact.setFieldValue('demo:blade','TYPE');contact.setFieldValue('contact','EVENT');contact.getInput('DO').connection.connect(deal.previousConnection);
 deal.setFieldValue('other','TARGET');deal.setFieldValue('this','SOURCE');amount.setFieldValue(4,'NUM');deal.getInput('AMOUNT').connection.connect(amount.outputConnection);
 const damaged=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),sourceIs=w.newBlock('greg_damage_source_is'),set=w.newBlock('greg_variable_set'),damageAmount=w.newBlock('greg_damage_amount');
 damaged.setFieldValue('demo:target','TYPE');damaged.setFieldValue('damage','EVENT');damaged.getInput('DO').connection.connect(condition.previousConnection);
 condition.getInput('IF0').connection.connect(sourceIs.outputConnection);sourceIs.setFieldValue('demo:blade','TYPE');condition.getInput('DO0').connection.connect(set.previousConnection);
 set.setFieldValue('object','SCOPE');set.setFieldValue('health','NAME');set.getInput('VALUE').connection.connect(damageAmount.outputConnection);
 const source=L.generate(w);assert.match(source,/entity\.damage\(contact\.other, 4, handle\)/);assert.match(source,/entity\.on_damage\('demo:target', function\(handle, damage\)/);assert.match(source,/damage\.source_type == 'demo:blade'/);assert.match(source,/entity\.set_value\(handle, 'health', damage\.amount\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),source);
 w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('managed object health blocks enable, inspect, set and heal without named variables',()=>{
 L.setObjectTypes([{key:'demo:crate',label:'Crate'}]);
 const w=new B.Workspace(),spawn=w.newBlock('greg_entity_event'),enable=w.newBlock('greg_entity_health_enable'),maximum=w.newBlock('math_number'),current=w.newBlock('math_number'),set=w.newBlock('greg_entity_health_set'),heal=w.newBlock('greg_entity_heal'),get=w.newBlock('greg_entity_health_get');
 spawn.setFieldValue('demo:crate','TYPE');spawn.setFieldValue('spawn','EVENT');spawn.getInput('DO').connection.connect(enable.previousConnection);
 maximum.setFieldValue(10,'NUM');current.setFieldValue(7,'NUM');enable.getInput('MAXIMUM').connection.connect(maximum.outputConnection);enable.getInput('CURRENT').connection.connect(current.outputConnection);enable.nextConnection.connect(set.previousConnection);
 set.setFieldValue('maximum','PROPERTY');set.getInput('VALUE').connection.connect(get.outputConnection);get.setFieldValue('current','PROPERTY');set.nextConnection.connect(heal.previousConnection);
 const source=L.generate(w);assert.match(source,/entity\.enable_health\(handle, 10, 7\)/);assert.match(source,/entity\.set_max_health\(handle, \(select\(1, entity\.health\(handle\)\) or 0\)\)/);assert.match(source,/entity\.heal\(handle, 1\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('object damage and defeated events expose health and drive general reactions',()=>{
 L.setObjectTypes([{key:'demo:crate',label:'Crate',animations:[{name:'hurt'},{name:'broken'}]}]);
 const w=new B.Workspace(),damage=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),defeated=w.newBlock('greg_health_event_defeated'),tint=w.newBlock('greg_entity_tint'),health=w.newBlock('greg_health_event_value'),store=w.newBlock('greg_variable_set');
 damage.setFieldValue('demo:crate','TYPE');damage.setFieldValue('damage','EVENT');damage.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(defeated.outputConnection);condition.getInput('DO0').connection.connect(tint.previousConnection);tint.setFieldValue('#FF4040FF','COLOR');tint.nextConnection.connect(store.previousConnection);store.setFieldValue('map','SCOPE');store.setFieldValue('remaining','NAME');store.getInput('VALUE').connection.connect(health.outputConnection);health.setFieldValue('health','PROPERTY');
 const lost=w.newBlock('greg_entity_event'),animation=w.newBlock('greg_entity_animation'),max=w.newBlock('greg_health_event_value');lost.setFieldValue('demo:crate','TYPE');lost.setFieldValue('defeated','EVENT');lost.getInput('DO').connection.connect(animation.previousConnection);animation.setFieldValue('broken','ANIMATION');animation.nextConnection.connect(w.newBlock('greg_variable_set').previousConnection);const saveMax=animation.getNextBlock();saveMax.setFieldValue('map','SCOPE');saveMax.setFieldValue('capacity','NAME');saveMax.getInput('VALUE').connection.connect(max.outputConnection);max.setFieldValue('max_health','PROPERTY');
 const source=L.generate(w);assert.match(source,/damage\.defeated/);assert.match(source,/damage\.health/);assert.match(source,/entity\.on_defeated\('demo:crate', function\(handle, defeat\)/);assert.match(source,/entity\.play_animation\(handle, 'broken', true\)/);assert.match(source,/defeat\.max_health/);w.dispose();L.setObjectTypes([]);
});

test('player health actions use scoped selectors and health events expose typed values',()=>{
 const w=new B.Workspace(),tick=w.newBlock('greg_tick'),players=w.newBlock('greg_players'),enable=w.newBlock('greg_player_health_enable'),maximum=w.newBlock('math_number'),current=w.newBlock('math_number'),damage=w.newBlock('greg_player_damage');tick.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(enable.previousConnection);enable.setFieldValue('current','PLAYER');maximum.setFieldValue(12,'NUM');current.setFieldValue(9,'NUM');enable.getInput('MAXIMUM').connection.connect(maximum.outputConnection);enable.getInput('CURRENT').connection.connect(current.outputConnection);enable.nextConnection.connect(damage.previousConnection);damage.setFieldValue('current','PLAYER');
 const changed=w.newBlock('greg_player_health_event'),condition=w.newBlock('controls_if'),reason=w.newBlock('greg_health_event_reason'),heal=w.newBlock('greg_player_heal'),delta=w.newBlock('greg_health_event_value');changed.setFieldValue('2','PLAYER');changed.setFieldValue('health_changed','EVENT');changed.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(reason.outputConnection);reason.setFieldValue('damage','REASON');condition.getInput('DO0').connection.connect(heal.previousConnection);heal.setFieldValue('current','PLAYER');heal.getInput('AMOUNT').connection.connect(delta.outputConnection);delta.setFieldValue('delta','PROPERTY');
 const source=L.generate(w);assert.match(source,/map\.enable_player_health\(player\.player, 12, 9\)/);assert.match(source,/map\.damage_player\(player\.player, 1\)/);assert.match(source,/map\.on_player_health_changed\(function\(player, change\)/);assert.match(source,/if player ~= 2 then return end/);assert.match(source,/change\.reason == 'damage'/);assert.match(source,/map\.heal_player\(player, change\.delta\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();
});

test('health change blocks adjust current or maximum health through managed APIs',()=>{
 const w=new B.Workspace(),tick=w.newBlock('greg_tick'),player=w.newBlock('greg_player_health_change'),amount=w.newBlock('math_number');tick.getInput('DO').connection.connect(player.previousConnection);player.setFieldValue('1','PLAYER');player.setFieldValue('maximum','PROPERTY');amount.setFieldValue(3,'NUM');player.getInput('AMOUNT').connection.connect(amount.outputConnection);
 const objectEvent=w.newBlock('greg_entity_event'),object=w.newBlock('greg_entity_health_change');objectEvent.setFieldValue('demo:orb','TYPE');objectEvent.setFieldValue('update','EVENT');objectEvent.getInput('DO').connection.connect(object.previousConnection);object.setFieldValue('current','PROPERTY');
 const source=L.generate(w);assert.match(source,/local current, maximum = map\.player_health\(1\)/);assert.match(source,/map\.set_player_max_health\(1, maximum \+ \(3\)\)/);assert.match(source,/local current, maximum = entity\.health\(handle\)/);assert.match(source,/entity\.set_health\(handle, current \+ \(1\)\)/);w.dispose();
});

test('managed health supports invulnerability, blocked damage, and explicit disable',()=>{
 const w=new B.Workspace(),tick=w.newBlock('greg_tick'),playerSet=w.newBlock('greg_player_invulnerable_set'),yes=w.newBlock('logic_boolean'),playerDisable=w.newBlock('greg_player_health_disable');
 tick.getInput('DO').connection.connect(playerSet.previousConnection);playerSet.setFieldValue('2','PLAYER');yes.setFieldValue('TRUE','BOOL');playerSet.getInput('VALUE').connection.connect(yes.outputConnection);playerSet.nextConnection.connect(playerDisable.previousConnection);playerDisable.setFieldValue('2','PLAYER');
 const update=w.newBlock('greg_entity_event'),objectSet=w.newBlock('greg_entity_invulnerable_set'),objectYes=w.newBlock('logic_boolean'),objectDisable=w.newBlock('greg_entity_health_disable');update.setFieldValue('demo:orb','TYPE');update.setFieldValue('update','EVENT');update.getInput('DO').connection.connect(objectSet.previousConnection);objectYes.setFieldValue('TRUE','BOOL');objectSet.getInput('VALUE').connection.connect(objectYes.outputConnection);objectSet.nextConnection.connect(objectDisable.previousConnection);
 const damage=w.newBlock('greg_entity_event'),branch=w.newBlock('controls_if'),blocked=w.newBlock('greg_damage_blocked'),setHealth=w.newBlock('greg_entity_health_set'),applied=w.newBlock('greg_damage_applied');damage.setFieldValue('demo:orb','TYPE');damage.setFieldValue('damage','EVENT');damage.getInput('DO').connection.connect(branch.previousConnection);branch.getInput('IF0').connection.connect(blocked.outputConnection);branch.getInput('DO0').connection.connect(setHealth.previousConnection);setHealth.getInput('VALUE').connection.connect(applied.outputConnection);
 const source=L.generate(w);assert.match(source,/map\.set_player_invulnerable\(2, true\)/);assert.match(source,/map\.disable_player_health\(2\)/);assert.match(source,/entity\.set_invulnerable\(handle, true\)/);assert.match(source,/entity\.disable_health\(handle\)/);assert.match(source,/damage\.blocked == true/);assert.match(source,/damage\.applied or damage\.amount/);w.dispose();
});

test('incoming object damage can be allowed by source with general logic blocks',()=>{
 L.setObjectTypes([{key:'demo:crate',label:'Crate'},{key:'demo:blade',label:'Blade'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),branch=w.newBlock('controls_if'),source=w.newBlock('greg_damage_source_is'),allow=w.newBlock('greg_damage_filter_set'),yes=w.newBlock('logic_boolean');
 event.setFieldValue('demo:crate','TYPE');event.setFieldValue('damage_filter','EVENT');event.getInput('DO').connection.connect(branch.previousConnection);branch.getInput('IF0').connection.connect(source.outputConnection);source.setFieldValue('demo:blade','TYPE');branch.getInput('DO0').connection.connect(allow.previousConnection);yes.setFieldValue('TRUE','BOOL');allow.getInput('ALLOWED').connection.connect(yes.outputConnection);
 const code=L.generate(w);assert.match(code,/entity\.on_damage_filter\('demo:crate'/);assert.match(code,/local allow_damage = true/);assert.match(code,/damage\.source_type == 'demo:blade'/);assert.match(code,/allow_damage = true/);assert.match(code,/return allow_damage/);
 assert.throws(()=>{event.setFieldValue('damage','EVENT');L.generate(w);},/incoming-damage check/);w.dispose();L.setObjectTypes([]);
});
test('native damage sources and accepted player hits are general conditions',()=>{
 L.setObjectTypes([{key:'demo:target',label:'Target',regions:[{name:'body',role:'body'}]}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),players=w.newBlock('greg_players'),branch=w.newBlock('controls_if'),touching=w.newBlock('greg_player_touching'),hit=w.newBlock('greg_player_hit_object'),and=w.newBlock('logic_operation'),not=w.newBlock('logic_negate'),defeat=w.newBlock('greg_defeat');
 event.setFieldValue('demo:target','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(branch.previousConnection);and.setFieldValue('AND','OP');and.getInput('A').connection.connect(touching.outputConnection);not.getInput('BOOL').connection.connect(hit.outputConnection);and.getInput('B').connection.connect(not.outputConnection);branch.getInput('IF0').connection.connect(and.outputConnection);branch.getInput('DO0').connection.connect(defeat.previousConnection);defeat.setFieldValue('current','PLAYER');
 const source=L.generate(w);assert.match(source,/entity\.was_hit_by_player\(handle, player\.player\)/);assert.match(source,/and not entity\.was_hit_by_player/);w.dispose();
 const f=new B.Workspace(),filter=f.newBlock('greg_entity_event'),allow=f.newBlock('greg_damage_filter_set'),both=f.newBlock('logic_operation'),isSword=f.newBlock('greg_damage_native_source_is'),owned=f.newBlock('logic_compare'),owner=f.newBlock('greg_damage_source_player'),one=f.newBlock('math_number');filter.setFieldValue('demo:target','TYPE');filter.setFieldValue('damage_filter','EVENT');filter.getInput('DO').connection.connect(allow.previousConnection);both.setFieldValue('AND','OP');isSword.setFieldValue('native:sword','SOURCE');both.getInput('A').connection.connect(isSword.outputConnection);owned.setFieldValue('EQ','OP');owned.getInput('A').connection.connect(owner.outputConnection);one.setFieldValue(1,'NUM');owned.getInput('B').connection.connect(one.outputConnection);both.getInput('B').connection.connect(owned.outputConnection);allow.getInput('ALLOWED').connection.connect(both.outputConnection);const filterSource=L.generate(f);assert.match(filterSource,/damage\.source_type == 'native:sword'/);assert.match(filterSource,/\(damage\.source_player or 0\) == 1/);f.dispose();L.setObjectTypes([]);
});

test('signal blocks provide general object messages with value and source metadata',()=>{
 L.setObjectTypes([{key:'demo:switch',label:'Switch'},{key:'demo:door',label:'Door'}]);
 const w=new B.Workspace(),contact=w.newBlock('greg_entity_event'),send=w.newBlock('greg_entity_signal'),amount=w.newBlock('math_number');
 contact.setFieldValue('demo:switch','TYPE');contact.setFieldValue('contact','EVENT');contact.getInput('DO').connection.connect(send.previousConnection);
 send.setFieldValue('open','NAME');send.setFieldValue('other','TARGET');send.setFieldValue('this','SOURCE');amount.setFieldValue(2,'NUM');send.getInput('VALUE').connection.connect(amount.outputConnection);
 const received=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),both=w.newBlock('logic_operation'),name=w.newBlock('greg_signal_name'),expected=w.newBlock('text'),equals=w.newBlock('logic_compare'),sourceIs=w.newBlock('greg_signal_source_is'),set=w.newBlock('greg_variable_set'),signalValue=w.newBlock('greg_signal_value');
 received.setFieldValue('demo:door','TYPE');received.setFieldValue('signal','EVENT');received.getInput('DO').connection.connect(condition.previousConnection);
 condition.getInput('IF0').connection.connect(both.outputConnection);both.setFieldValue('AND','OP');both.getInput('A').connection.connect(equals.outputConnection);both.getInput('B').connection.connect(sourceIs.outputConnection);
 equals.setFieldValue('EQ','OP');equals.getInput('A').connection.connect(name.outputConnection);equals.getInput('B').connection.connect(expected.outputConnection);expected.setFieldValue('open','TEXT');sourceIs.setFieldValue('demo:switch','TYPE');
 condition.getInput('DO0').connection.connect(set.previousConnection);set.setFieldValue('object','SCOPE');set.setFieldValue('opening','NAME');set.getInput('VALUE').connection.connect(signalValue.outputConnection);
 const source=L.generate(w);assert.match(source,/entity\.signal\(contact\.other, 'open', 2, handle\)/);assert.match(source,/entity\.on_signal\('demo:door', function\(handle, signal\)/);assert.match(source,/signal\.name == 'open'/);assert.match(source,/signal\.source_type == 'demo:switch'/);assert.match(source,/entity\.set_value\(handle, 'opening', signal\.value\)/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),source);
 w.dispose();copy.dispose();L.setObjectTypes([]);
});

test('named-object signal action uses the current map placement picker directly',()=>{
 L.setObjectTypes([{key:'demo:switch',label:'Switch'},{key:'demo:door',label:'Door'}]);L.setPlacements([{name:'main_door',type:'demo:door'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),send=w.newBlock('greg_entity_signal_named'),value=w.newBlock('logic_boolean');event.setFieldValue('demo:switch','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(send.previousConnection);send.setFieldValue('main_door','TARGET');send.setFieldValue('this','SOURCE');send.setFieldValue('open','NAME');value.setFieldValue('TRUE','BOOL');send.getInput('VALUE').connection.connect(value.outputConnection);
 const source=L.generate(w);assert.match(source,/local target = entity\.find\('main_door'\)/);assert.match(source,/entity\.signal\(target, 'open', true, handle\)/);assert.ok(send.getField('TARGET').getOptions(false).some(option=>option[1]==='main_door'));
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,saved.source),true);assert.equal(L.generate(copy),source);w.dispose();copy.dispose();L.setPlacements([]);L.setObjectTypes([]);
});

test('configured object creation initializes instance values before spawn logic',()=>{
 L.setObjectTypes([{key:'demo:launcher',label:'Launcher'},{key:'demo:projectile',label:'Projectile'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),spawn=w.newBlock('greg_entity_spawn_value'),owner=w.newBlock('greg_entity_handle');
 event.setFieldValue('demo:launcher','TYPE');event.setFieldValue('update','EVENT');event.getInput('DO').connection.connect(spawn.previousConnection);
 spawn.setFieldValue('demo:projectile','TYPE');spawn.setFieldValue('owner','NAME');spawn.getInput('VALUE').connection.connect(owner.outputConnection);
 const source=L.generate(w);assert.match(source,/entity\.spawn\('demo:projectile', \{x = 0, y = 0, values = \{\['owner'\] = handle\}\}\)/);
 const saved=L.save(w),restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),source);
 const renamed=L.renameReference(saved,saved.source,'demo:projectile','demo:shot');assert.ok(renamed);assert.match(renamed.source,/entity\.spawn\('demo:shot'/);
 w.dispose();restored.dispose();L.setObjectTypes([]);
});

test('patrol runtime fixture matches actual editor blocks',()=>assert.equal(require('./player-block-fixture').patrol(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-patrol.lua','utf8')));

test('variable pickers discover created names and retain them through serialization',()=>{
 const w=new B.Workspace();w.getVariableMap().createVariable('direction');const event=w.newBlock('greg_tick'),set=w.newBlock('greg_variable_set');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('direction','NAME');
 assert.ok(set.getField('NAME').getOptions(false).some(option=>option[1]==='direction'));
 const saved=L.save(w),copy=new B.Workspace();assert.ok(L.restore(copy,saved,saved.source));assert.ok(copy.getVariableMap().getAllVariables().some(v=>v.getName()==='direction'));w.dispose();copy.dispose();
});

test('object variables use the independent handle-owned API and allow the full picker length',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),set=w.newBlock('greg_variable_set');event.setFieldValue('demo:orb','TYPE');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('object','SCOPE');set.setFieldValue('direction','NAME');assert.match(L.generate(w),/entity\.set_value\(handle, 'direction'/);set.setFieldValue('long_variable_name','NAME');assert.match(L.generate(w),/entity\.set_value\(handle, 'long_variable_name'/);w.dispose();
});

test('object point queries compose through OR and retain object rename references',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),condition=w.newBlock('controls_if'),either=w.newBlock('logic_operation'),a=w.newBlock('greg_object_at'),b=w.newBlock('greg_object_at');event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(either.outputConnection);either.setFieldValue('OR','OP');either.getInput('A').connection.connect(a.outputConnection);either.getInput('B').connection.connect(b.outputConnection);a.setFieldValue('demo:wall','TYPE');b.setFieldValue('demo:door','TYPE');
 const saved=L.save(w);assert.match(saved.source,/entity.at/);assert.match(saved.source,/ or /);const renamed=L.renameReference(saved,saved.source,'demo:wall','demo:barrier');assert.ok(renamed);assert.match(renamed.source,/demo:barrier/);w.dispose();
});

test('one coordinate block detects exact authored tiles or current-map objects',()=>{
 L.setTileTypes([{symbol:'@',label:'Automatic terrain'},{symbol:'$',label:'Moss'}]);
 L.setObjectTypes([{key:'demo:wall',label:'Wall'}]);
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),condition=w.newBlock('controls_if'),either=w.newBlock('logic_operation'),tile=w.newBlock('greg_coordinate_at'),object=w.newBlock('greg_coordinate_at');
 event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(either.outputConnection);either.setFieldValue('OR','OP');either.getInput('A').connection.connect(tile.outputConnection);either.getInput('B').connection.connect(object.outputConnection);
 tile.setFieldValue('tile:$','TARGET');object.setFieldValue('object:demo:wall','TARGET');
 assert.ok(tile.getField('TARGET').getOptions(false).some(option=>option[0].includes('Moss')&&option[1]==='tile:$'));
 assert.ok(object.getField('TARGET').getOptions(false).some(option=>option[0].includes('Wall')&&option[1]==='object:demo:wall'));
 const saved=L.save(w);assert.match(saved.source,/map\.tile_at\(0, 0\) == '\$'/);assert.match(saved.source,/entity\.at\(0, 0\)/);assert.match(saved.source,/ or /);
 const renamed=L.renameReference(saved,saved.source,'demo:wall','demo:barrier');assert.ok(renamed);assert.match(renamed.source,/object:demo:barrier|demo:barrier/);
 const game=L.toolbox.contents.find(c=>c.name==='Game').contents.map(item=>item.type);assert.ok(game.includes('greg_coordinate_at'));assert.equal(game.includes('greg_object_at'),false);
 w.dispose();L.setTileTypes([]);L.setObjectTypes([]);
});

test('named timers expose one-shot, repeating, cancel and status blocks with editable round trip',()=>{
 const w=new B.Workspace(),tickEvent=w.newBlock('greg_tick'),timerEvent=w.newBlock('greg_timer_event');
 const start=w.newBlock('greg_timer_start'),delay=w.newBlock('math_number'),repeat=w.newBlock('greg_timer_repeat'),repeatDelay=w.newBlock('math_number'),interval=w.newBlock('math_number'),branch=w.newBlock('controls_if'),active=w.newBlock('greg_timer_active'),cancel=w.newBlock('greg_timer_cancel'),remaining=w.newBlock('greg_timer_remaining'),velocity=w.newBlock('greg_velocity');
 timerEvent.setFieldValue('door_close','NAME');start.setFieldValue('door_close','NAME');repeat.setFieldValue('door_close','NAME');active.setFieldValue('door_close','NAME');cancel.setFieldValue('door_close','NAME');remaining.setFieldValue('door_close','NAME');
 delay.setFieldValue(15,'NUM');repeatDelay.setFieldValue(30,'NUM');interval.setFieldValue(60,'NUM');
 tickEvent.getInput('DO').connection.connect(start.previousConnection);start.getInput('DELAY').connection.connect(delay.outputConnection);start.nextConnection.connect(repeat.previousConnection);repeat.getInput('DELAY').connection.connect(repeatDelay.outputConnection);repeat.getInput('INTERVAL').connection.connect(interval.outputConnection);repeat.nextConnection.connect(branch.previousConnection);branch.getInput('IF0').connection.connect(active.outputConnection);branch.getInput('DO0').connection.connect(cancel.previousConnection);
 timerEvent.getInput('DO').connection.connect(velocity.previousConnection);velocity.setFieldValue('1','PLAYER');velocity.getInput('X').connection.connect(remaining.outputConnection);
 const saved=L.save(w);assert.match(saved.source,/map\.on_timer\('door_close'/);assert.match(saved.source,/map\.timer_start\('door_close', 15\)/);assert.match(saved.source,/map\.timer_start\('door_close', 30, 60\)/);assert.match(saved.source,/map\.timer_cancel\('door_close'\)/);assert.match(saved.source,/map\.timer_remaining\('door_close'\)/);
 const restored=new B.Workspace();assert.equal(L.restore(restored,saved,saved.source),true);assert.equal(L.generate(restored),saved.source);
 const game=L.toolbox.contents.find(category=>category.name==='Game').contents.map(item=>item.type);assert.ok(game.includes('greg_timer_start')&&game.includes('greg_timer_active'));
 w.dispose();restored.dispose();
});

test('timer blocks reject missing, duplicate and oversized timer definitions',()=>{
 const missing=new B.Workspace(),tickEvent=missing.newBlock('greg_tick'),start=missing.newBlock('greg_timer_start');start.setFieldValue('missing','NAME');tickEvent.getInput('DO').connection.connect(start.previousConnection);assert.throws(()=>L.generate(missing),/matching timer-finished event/);missing.dispose();
 const duplicate=new B.Workspace(),first=duplicate.newBlock('greg_timer_event'),second=duplicate.newBlock('greg_timer_event');first.setFieldValue('same','NAME');second.setFieldValue('same','NAME');assert.throws(()=>L.generate(duplicate),/one block/);duplicate.dispose();
 const oversized=new B.Workspace(),event=oversized.newBlock('greg_timer_event');event.setFieldValue('x'.repeat(32),'NAME');assert.throws(()=>L.generate(oversized),/1 to 31 bytes/);oversized.dispose();
});

test('text toolbox exposes deterministic string primitives and round trips them',()=>{
 class TestParser {parseFromString(text){const node=JSON.parse(text);node.getAttribute=function(name){return this.attributes?.[name]??null;};return {documentElement:node,getElementsByTagName(){return [];}};}}
 class TestSerializer {serializeToString(node){return JSON.stringify(node);}}
 B.utils.xml.injectDependencies({document:{createElementNS(namespace,tag){return {tagName:tag,attributes:{},setAttribute(name,value){this.attributes[name]=String(value);},getAttribute(name){return this.attributes[name]??null;}};}},DOMParser:TestParser,XMLSerializer:TestSerializer});
 const w=new B.Workspace(),event=w.newBlock('greg_tick');let tail=null;
 function literal(text){const block=w.newBlock('text');block.setFieldValue(text,'TEXT');return block;}
 function store(name,expression){const block=w.newBlock('greg_variable_set');block.setFieldValue('map','SCOPE');block.setFieldValue(name,'NAME');block.getInput('VALUE').connection.connect(expression.outputConnection);if(tail)tail.nextConnection.connect(block.previousConnection);else event.getInput('DO').connection.connect(block.previousConnection);tail=block;}
 const join=w.newBlock('text_join');join.getInput('ADD0').connection.connect(literal('Bloopa ').outputConnection);join.getInput('ADD1').connection.connect(literal('awake').outputConnection);store('joined',join);
 const length=w.newBlock('text_length');length.getInput('VALUE').connection.connect(literal('sword').outputConnection);store('length',length);
 const empty=w.newBlock('text_isEmpty');empty.getInput('VALUE').connection.connect(literal('').outputConnection);store('empty',empty);
 const index=w.newBlock('text_indexOf');index.setFieldValue('LAST','END');index.getInput('VALUE').connection.connect(literal('egg nogg egg').outputConnection);index.getInput('FIND').connection.connect(literal('egg').outputConnection);store('index',index);
 const character=w.newBlock('text_charAt');character.setFieldValue('FROM_END','WHERE');const characterAt=w.newBlock('math_number');characterAt.setFieldValue(2,'NUM');character.getInput('VALUE').connection.connect(literal('sword').outputConnection);character.getInput('AT').connection.connect(characterAt.outputConnection);store('character',character);
 const substring=w.newBlock('text_getSubstring');substring.setFieldValue('FROM_START','WHERE1');substring.setFieldValue('FROM_END','WHERE2');const start=w.newBlock('math_number'),end=w.newBlock('math_number');start.setFieldValue(2,'NUM');end.setFieldValue(2,'NUM');substring.getInput('STRING').connection.connect(literal('eggnogg').outputConnection);substring.getInput('AT1').connection.connect(start.outputConnection);substring.getInput('AT2').connection.connect(end.outputConnection);store('substring',substring);
 const changed=w.newBlock('text_changeCase');changed.setFieldValue('TITLECASE','CASE');changed.getInput('TEXT').connection.connect(literal('CUSTOM OBJECT').outputConnection);store('case',changed);
 const trim=w.newBlock('text_trim');trim.setFieldValue('BOTH','MODE');trim.getInput('TEXT').connection.connect(literal('  ready  ').outputConnection);store('trim',trim);
 const reverse=w.newBlock('text_reverse');reverse.getInput('TEXT').connection.connect(literal('abc').outputConnection);store('reverse',reverse);
 const source=L.generate(w);assert.match(source,/table\.concat\(\{tostring\('Bloopa '\), tostring\('awake'\)\}\)/);assert.match(source,/#tostring\('sword'\)/);assert.match(source,/, true; if #needle/);assert.match(source,/string\.sub\(tostring\('sword'\), -\(2\), -\(2\)\)/);assert.match(source,/string\.sub\(tostring\('eggnogg'\), 2, -\(2\)\)/);assert.match(source,/string\.byte/);assert.match(source,/string\.reverse/);assert.doesNotMatch(source,/string\.(find|gsub|lower|upper)|math\.random/);
 const saved=L.save(w),copy=new B.Workspace();assert.equal(L.restore(copy,saved,source),true);assert.equal(L.generate(copy),source);
 const text=L.toolbox.contents.find(category=>category.name==='Text').contents.map(item=>item.type);assert.deepEqual(text,['text','text_join','text_length','text_isEmpty','text_indexOf','text_charAt','text_getSubstring','text_changeCase','text_trim','text_reverse']);
 w.dispose();copy.dispose();
});

test('random-character text uses rollback-safe map random and unsafe text actions stay rejected',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),set=w.newBlock('greg_variable_set'),character=w.newBlock('text_charAt'),value=w.newBlock('text');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('map','SCOPE');set.setFieldValue('letter','NAME');character.setFieldValue('RANDOM','WHERE');value.setFieldValue('abc','TEXT');character.getInput('VALUE').connection.connect(value.outputConnection);set.getInput('VALUE').connection.connect(character.outputConnection);
 const source=L.generate(w);assert.match(source,/if #text == 0 then return '' end/);assert.match(source,/map\.random\(1, #text\)/);assert.doesNotMatch(source,/math\.random/);const print=w.newBlock('text_print');set.nextConnection.connect(print.previousConnection);assert.throws(()=>L.generate(w),/Unsupported block/);w.dispose();
});
