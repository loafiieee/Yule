 'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const L=require('./logic-blocks.js'),B=require('./vendor/blockly/blockly_compressed.js');
function tick(){const workspace=new B.Workspace(),event=workspace.newBlock('greg_tick');return {workspace,event};}
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
 assert.match(L.generate(w),/map.random\(1, 10\)/);w.dispose();
});

test('active-player loop supplies observations and a scoped action target',()=>{
 const {workspace:w,event}=tick(),players=w.newBlock('greg_players'),action=w.newBlock('greg_velocity'),x=w.newBlock('greg_player_value');
 event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(action.previousConnection);action.setFieldValue('current','PLAYER');x.setFieldValue('vx','PROPERTY');action.getInput('X').connection.connect(x.outputConnection);
 const source=L.generate(w);assert.match(source,/ipairs\(map.players\(\)\)/);assert.match(source,/map.set_player_velocity\(player.player, player.vx, 0\)/);
 action.unplug();event.getInput('DO').connection.disconnect();players.dispose();event.getInput('DO').connection.connect(action.previousConnection);assert.throws(()=>L.generate(w),/for-each-player/);w.dispose();
});

test('player-loop native fixture is generated by the real block editor',()=>{assert.equal(require('./player-block-fixture')(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-players.lua','utf8'));});

test('player/object overlap condition matches its native fixture',()=>{assert.equal(require('./player-block-fixture').touching(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-player-touching.lua','utf8'));});

test('object-type loop supplies this-object context in map logic',()=>{const source=require('./logic-runtime-fixture').group();assert.equal(source,require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-object-group.lua','utf8'));assert.match(source,/entity.exists/);assert.match(source,/entity.type/);});

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

test('terrain sensing and reversal compose in an object update',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),sensor=w.newBlock('greg_terrain_ahead'),reverse=w.newBlock('greg_entity_reverse');
 event.setFieldValue('demo:orb','TYPE');event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(sensor.outputConnection);condition.getInput('DO0').connection.connect(reverse.previousConnection);
 const source=L.generate(w);assert.match(source,/map.solid_box/);assert.match(source,/speed == 0/);assert.match(source,/vx = -entity.get/);
 assert.equal(L.toolbox.contents.some(c=>c.name==='Map values'),false);w.dispose();
});

test('patrol runtime fixture matches actual editor blocks',()=>assert.equal(require('./player-block-fixture').patrol(),require('node:fs').readFileSync(__dirname+'/../tests/fixtures/blocks-patrol.lua','utf8')));

test('variable pickers discover created names and retain them through serialization',()=>{
 const w=new B.Workspace();w.getVariableMap().createVariable('direction');const event=w.newBlock('greg_tick'),set=w.newBlock('greg_variable_set');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('direction','NAME');
 assert.ok(set.getField('NAME').getOptions(false).some(option=>option[1]==='direction'));
 const saved=L.save(w),copy=new B.Workspace();assert.ok(L.restore(copy,saved,saved.source));assert.ok(copy.getVariableMap().getAllVariables().some(v=>v.getName()==='direction'));w.dispose();copy.dispose();
});

test('object variable names generate instance-owned keys and reject overly long keys',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),set=w.newBlock('greg_variable_set');event.setFieldValue('demo:orb','TYPE');event.getInput('DO').connection.connect(set.previousConnection);set.setFieldValue('object','SCOPE');set.setFieldValue('direction','NAME');assert.match(L.generate(w),/handle/);set.setFieldValue('long_variable_name','NAME');assert.throws(()=>L.generate(w),/12 characters/);w.dispose();
});

test('object point queries compose through OR and retain object rename references',()=>{
 const w=new B.Workspace(),event=w.newBlock('greg_tick'),condition=w.newBlock('controls_if'),either=w.newBlock('logic_operation'),a=w.newBlock('greg_object_at'),b=w.newBlock('greg_object_at');event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(either.outputConnection);either.setFieldValue('OR','OP');either.getInput('A').connection.connect(a.outputConnection);either.getInput('B').connection.connect(b.outputConnection);a.setFieldValue('demo:wall','TYPE');b.setFieldValue('demo:door','TYPE');
 const saved=L.save(w);assert.match(saved.source,/entity.at/);assert.match(saved.source,/ or /);const renamed=L.renameReference(saved,saved.source,'demo:wall','demo:barrier');assert.ok(renamed);assert.match(renamed.source,/demo:barrier/);w.dispose();
});
