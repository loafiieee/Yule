 'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
const O=require('../object-tools.js'),G=require('../editor-core.js');
function project(){return O.create(G.createDefaultDocument());}
test('object design is owned by the current document and upgrades without modifying its source',()=>{
 const source=G.createDefaultDocument(),before=JSON.stringify(source),result=O.create(source);
 assert.equal(JSON.stringify(source),before);assert.equal(result.document.format,G.FORMAT_V2);
 assert.equal(O.catalog(result.document).types.length,1);
 assert.notEqual(O.create(result.document).key,result.key);
});
test('painting objects preserves terrain, deduplicates cells and erases through the same tool',()=>{
 const {document:doc,key}=project(),room=doc.layout.order[0],before=JSON.stringify(doc.rooms),cell=[{row:3,col:4}];
 assert.equal(O.paint(doc,key,room,cell,false),true);assert.equal(O.paint(doc,key,room,cell,false),false);
 assert.equal(doc.entities.placements.length,1);assert.equal(doc.entities.placements[0].x,72);assert.equal(doc.entities.placements[0].y,56);
 assert.equal(JSON.stringify(doc.rooms),before);assert.equal(O.paint(doc,null,room,cell,true),true);assert.equal(doc.entities.placements.length,0);
});
test('capacity failure leaves the existing map placements intact',()=>{
 const {document:doc,key}=project(),room=doc.layout.order[0];doc.entities.capacity=1;
 O.paint(doc,key,room,[{row:1,col:1}],false);const before=JSON.stringify(doc);
 assert.throws(()=>O.paint(doc,key,room,[{row:1,col:2}],false));assert.equal(JSON.stringify(doc),before);
});
test('object logic round trips without repeated generated callbacks or losing manual edits',()=>{
 const {document:doc,key}=project();doc.mapLua='-- map logic\n';doc.objectScripts={[key]:{spawn:'entity.set(handle, {vx=1})'}};
 const files=O.exportFiles(doc,{}),loaded=JSON.parse(JSON.stringify(doc));loaded.mapLua=files['map.lua'];delete loaded.objectScripts;
 const restored=O.importLogic(loaded,files['objects.greggnogg.json']);assert.equal(restored.mapLua,doc.mapLua);
 assert.equal(O.exportFiles(restored,{})['map.lua'],files['map.lua']);loaded.mapLua+='-- manual edit';
 assert.throws(()=>O.importLogic(loaded,files['objects.greggnogg.json']),/does not match/);
});
test('rename preserves placements and callbacks; deleting a design removes every placed copy',()=>{
 const {document:doc,key}=project();doc.objectScripts={[key]:{update:'-- update'}};O.paint(doc,key,doc.layout.order[0],[{row:0,col:0},{row:0,col:1}],false);
 const renamed=O.rename(doc,key,'custom:renamed');assert.equal(renamed.entities.placements[0].type,'custom:renamed');
 assert.equal(renamed.objectScripts['custom:renamed'].update,'-- update');const removed=O.remove(renamed,'custom:renamed');
 assert.equal(renamed.entities.placements.length,2);assert.equal(removed.entities.types.some(type=>type.key==='custom:renamed'),false);assert.equal(removed.entities.placements.length,0);assert.equal(removed.objectScripts['custom:renamed'],undefined);
 assert.ok(G.parsePackageFiles(G.exportProjectFiles(removed)).valid,'map still exports and imports after cascading deletion');
});
test('object designer opens inside Greggnogg and the old entry redirects there',()=>{
 const html=fs.readFileSync(__dirname+'/../index.html','utf8'),old=fs.readFileSync(__dirname+'/index.html','utf8'),ui=fs.readFileSync(__dirname+'/../object-designer.js','utf8');
 assert.match(html,/id="object-designer-button"/);assert.match(html,/object-designer.js/);assert.match(old,/index.html\?objects=1/);
 assert.doesNotMatch(ui,/Start with a map|Use my Greggnogg map|starter-custom|add-placement/);
 assert.match(ui,/Can interact with vanilla physics-triggered objects/);assert.match(ui,/Health and damage/);assert.match(ui,/This object has health/);assert.match(ui,/Knocked away by mine explosions/);
});
test('invisible object copies remain discoverable as editor ghosts',()=>{
 const html=fs.readFileSync(__dirname+'/../index.html','utf8'),tools=fs.readFileSync(__dirname+'/../object-tools.js','utf8');
 assert.match(html,/This only hides the picture\. Logic, hitboxes, damage, and physics triggers stay active\./);
 assert.match(tools,/p\.visible===false\?0\.28:1/);
 assert.doesNotMatch(tools,/if\(p\.visible===false\)continue/);
});
test('repeated notices count in place and distinct notices have a visible limit',()=>{
 const vm=require('node:vm'),source=fs.readFileSync(__dirname+'/../editor.js','utf8');let nextTimer=0;const canceled=[];
 function node(){return {children:[],parentNode:null,hidden:false,append(...items){items.forEach(item=>{item.parentNode=this;this.children.push(item);});},appendChild(item){this.append(item);},setAttribute(){},remove(){if(!this.parentNode)return;const siblings=this.parentNode.children,index=siblings.indexOf(this);if(index>=0)siblings.splice(index,1);this.parentNode=null;},get firstElementChild(){return this.children[0]||null;}};}
 const region=node(),context=vm.createContext({document:{createElement:node},els:{'toast-region':region},window:{setTimeout:()=>++nextTimer,clearTimeout:id=>canceled.push(id)},announce(){}});
 vm.runInContext(source.slice(source.indexOf('  var activeToasts = Object.create(null);'),source.indexOf('  function openDialog(')),context);
 context.toast('Cannot switch','Room A conflicts','warning');context.toast('Cannot switch','Room A conflicts','warning');
 assert.equal(region.children.length,1);assert.equal(region.children[0].children[2].textContent,'×2');assert.ok(canceled.includes(1));
 for(let i=0;i<4;i++)context.toast('Other '+i,'Different details','warning');
 assert.equal(region.children.length,4);assert.equal(region.children.some(item=>item.children[1].children[0].textContent==='Cannot switch'),false);
 context.toast('Cannot switch','Room A conflicts','warning');assert.equal(region.children.length,4);
 assert.equal(region.children[3].children[2].hidden,true,'a newly displayed notice starts at one');
});
test('spawn markers are the final tile-catalog section',()=>{
 const editor=fs.readFileSync(__dirname+'/../editor.js','utf8');
 assert.match(editor,/CATEGORY_ORDER = \[[^\]]*"Custom tiles", "Spawn markers"\]/);
});

test('room duplication copies instances with distinct names and checks expanded capacity atomically',()=>{
 const {document:doc,key}=project(),source=doc.layout.order[0];O.paint(doc,key,source,[{row:2,col:3}],false);
 Object.assign(doc.entities.placements[0],{animation:'default',scale_x:1.5,scale_y:-1,visual_offset_x:2,visual_offset_y:-3,visual_rotation:45,visual_tint:'#40FF8000',draw_layer:'behind',visible:false});
 const order=[source,'copy'];O.duplicateRoom(doc,source,'copy',order);
 assert.equal(doc.entities.placements.length,2);assert.equal(doc.entities.placements[1].room,'copy');
 assert.notEqual(doc.entities.placements[0].name,doc.entities.placements[1].name);
 for(const key of ['animation','scale_x','scale_y','visual_offset_x','visual_offset_y','visual_rotation','visual_tint','draw_layer','visible'])assert.equal(doc.entities.placements[1][key],doc.entities.placements[0][key]);
 doc.entities.capacity=3;const before=JSON.stringify(doc);
 assert.throws(()=>O.duplicateRoom(doc,source,'copy2',[...order,'copy2']));assert.equal(JSON.stringify(doc),before);
});

test('normal editor owns per-copy animation, appearance, movement and draw-order controls',()=>{
 const html=fs.readFileSync(__dirname+'/../index.html','utf8'),editor=fs.readFileSync(__dirname+'/../editor.js','utf8');
 for(const id of ['object-inspector','object-placement-picker','object-placement-empty','object-placement-fields','object-placement-animation','object-placement-layer','object-placement-side','object-placement-visible','object-placement-x','object-placement-y','object-placement-vx','object-placement-vy','object-placement-scale-x','object-placement-scale-y','object-placement-offset-x','object-placement-offset-y','object-placement-rotation','object-placement-tint','object-placement-edit-design'])assert.match(html,new RegExp('id="'+id+'"'));
 assert.doesNotMatch(html,/id="object-tab"[^>]*disabled/);
 assert.match(editor,/function selectPlacement/);assert.match(editor,/picked&&picked\.type===state\.selectedObject/);assert.match(editor,/state\.pointer\.tool === "select"[\s\S]*?if\(picked\)\{state\.selection=null;selectPlacement\(picked,true\);\}/);assert.match(editor,/object-placement-picker/);assert.match(editor,/function bindPlacementControls/);
});

test('area transforms move and orient custom object copies with their tiles',()=>{
 const source=[{name:'orb',type:'custom:orb',x:8,y:24,scale_x:2,visual_rotation:30,_sourceName:'orb'}];
 const horizontal=O.transformAreaPlacements(source,'flip_x',3,2);
 assert.equal(horizontal[0].x,40);assert.equal(horizontal[0].y,24);assert.equal(horizontal[0].scale_x,-2);
 const vertical=O.transformAreaPlacements(source,'flip_y',3,2);
 assert.equal(vertical[0].x,8);assert.equal(vertical[0].y,8);assert.equal(vertical[0].scale_y,-1);
 const rotated=O.transformAreaPlacements(source,'rotate_cw',3,2);
 assert.equal(rotated[0].x,8);assert.equal(rotated[0].y,8);assert.equal(rotated[0].visual_rotation,120);
 assert.deepEqual(source[0],{name:'orb',type:'custom:orb',x:8,y:24,scale_x:2,visual_rotation:30,_sourceName:'orb'});
 assert.throws(()=>O.transformAreaPlacements(source,'diagonal',3,2),/Unknown/);
});

test('normal map package parsing retains generated Lua for editor metadata restoration',()=>{
 const {document:doc,key}=project();doc.mapLua='-- base';doc.objectScripts={[key]:{update:'-- callback'}};
 const files=O.exportFiles(doc,G.exportProjectFiles(doc)),parsed=G.parsePackageFiles(files);
 assert.equal(parsed.valid,true,JSON.stringify(parsed.errors));const loaded=parsed.document;
 loaded.entities=JSON.parse(files['entities.json']);loaded.mapLua=loaded._preserved.mapLua;delete loaded._preserved;
 const restored=O.importLogic(loaded,files['objects.greggnogg.json']);
 assert.equal(O.compileScript(restored),files['map.lua']);assert.deepEqual(restored.entities,doc.entities);
});

function importRuntime(){
 const vm=require('node:vm'),source=fs.readFileSync(__dirname+'/../editor.js','utf8');
 const state={importRequest:0,document:G.createDefaultDocument()},messages=[];
 const context=vm.createContext({state,Core:G,GregObjects:O,EntityAuthor:require('./core.js'),TextDecoder,Uint8Array,
  els:{'confirm-import-button':{}},showImportResult:(...args)=>messages.push(args),
  filesToEntries:async files=>Object.entries(files).map(([name,bytes])=>({name,bytes:typeof bytes==='string'?new TextEncoder().encode(bytes):bytes})),
  basename:name=>name.split('/').pop(),dirname:name=>name.split('/').slice(0,-1).join('/'),
  decodeText:entry=>new TextDecoder().decode(entry.bytes),ensureDocumentShape:doc=>doc,normalizeId:s=>s,
  bytesToPngDataUrl:async bytes=>'data:image/png;base64,'+Buffer.from(bytes).toString('base64')});
 vm.runInContext(source.slice(source.indexOf('  async function stageImport('),source.indexOf('  function confirmImport(')),context);
 return {state,messages,load:files=>context.stageImport(files)};
}
test('actual map import accepts nested custom-object exports and restores the current-map callback model',async()=>{
 const {document:doc,key}=project();doc.objectScripts={[key]:{spawn:'-- created'}};
 const files=O.exportFiles(doc,G.exportProjectFiles(doc)),nested=Object.fromEntries(Object.entries(files).map(([k,v])=>['zip/map/'+k,v]));
 const runtime=importRuntime();await runtime.load(nested);
 assert.ok(runtime.state.pendingImport,JSON.stringify(runtime.messages));
 assert.equal(runtime.state.pendingImport.entities.types[0].key,key);
 assert.equal(runtime.state.pendingImport.objectScripts[key].spawn,'-- created');
});
test('actual map import rejects conflicting callback metadata without replacing the current map',async()=>{
 const {document:doc}=project(),files=O.exportFiles(doc,G.exportProjectFiles(doc));files['map.lua']+='-- changed';
 const runtime=importRuntime(),before=JSON.stringify(runtime.state.document);await runtime.load(files);
 assert.equal(runtime.state.pendingImport,null);assert.equal(JSON.stringify(runtime.state.document),before);
 assert.match(runtime.messages.at(-1)[1],/does not match/);
});

test('save validates an immutable snapshot while picture loading is pending',async()=>{
 const {document:doc}=project();let resolvePicture;
 const pending=O.prepareForSave(doc,()=>new Promise(resolve=>{resolvePicture=resolve;}));
 doc.entities.types[0].visual.sprite=999999;
 resolvePicture({width:256,height:256});const candidate=await pending;
 assert.equal(candidate.entities.types[0].visual.sprite,4);
 assert.equal(doc.entities.types[0].visual.sprite,999999);
});
test('save rejects animation frames that extend beyond the picture sheet',async()=>{
 const {document:doc}=project();doc.entities.types[0].visual.sprite=255;doc.entities.types[0].visual.frames=2;
 await assert.rejects(O.prepareForSave(doc,async()=>({width:256,height:256})),/outside|range/i);
});

test('map tools find and erase unsnapped room-local and legacy world-coordinate objects',()=>{
 const {document:doc,key}=project(),room=doc.layout.order[0],origin=(doc.layout.order.length-1)*528;
 doc.entities.placements=[{name:'local',type:key,room,x:19,y:35},{name:'world',type:key,x:origin+21,y:36}];
 for(const placement of doc.entities.placements)assert.equal(O.atCell(doc,placement,room,2,1),true);
 O.paint(doc,key,room,[{row:2,col:1}],true);assert.equal(doc.entities.placements.length,0);
});
test('custom objects can be painted across the full authored room size',()=>{
 const {document:doc,key}=project(),room=doc.rooms[0];G.resizeRoom(room,47,18);doc.layout.roomFormat=G.VARIABLE_ROOM_FORMAT;
 assert.equal(O.paint(doc,key,room.id,[{row:17,col:46}],false),true);
 assert.equal(doc.entities.placements[0].x,46*16+8);assert.equal(doc.entities.placements[0].y,17*16+8);
 assert.equal(O.paint(doc,key,room.id,[{row:17,col:47}],false),false);
});
test('actual painting ignores a previously selected blank terrain tile and supports explicit keyboard erasure',()=>{
 const vm=require('node:vm'),source=fs.readFileSync(__dirname+'/../editor.js','utf8'),{document:doc,key}=project();
 const state={document:doc,selectedObject:key,selectedGlyph:' ',tool:'pencil',pointer:null};
 const context=vm.createContext({state,GregObjects:O,ROWS:12,COLS:33,activeRoom:()=>({id:doc.layout.order[0]}),roomRows:()=>12,roomCols:()=>33,renderAfterMapEdit(){},toast:(...args)=>{throw Error(args.join(' '));}});
 vm.runInContext(source.slice(source.indexOf('  function applyChanges('),source.indexOf('  function renderAfterMapEdit(')),context);
 assert.equal(context.applyChanges([{row:1,col:1,glyph:' '}],true),true);assert.equal(doc.entities.placements.length,1);
 assert.equal(context.applyChanges([{row:1,col:1,glyph:' '}],true,true),true);assert.equal(doc.entities.placements.length,0);
});

test('object block layouts survive package import and design rename without losing editable actions',()=>{
 const L=require('../logic-blocks'),B=require('../vendor/blockly/blockly_compressed'),{document:doc,key}=project(),w=new B.Workspace();
 L.restoreCallback(w,null,'',key,'update');const action=w.newBlock('greg_entity_flag');w.getTopBlocks()[0].getInput('DO').connection.connect(action.previousConnection);
 const saved=L.saveCallback(w,key,'update');doc.objectScripts={[key]:{update:saved.source}};doc.objectBlocks={[key]:{update:saved}};
 const files=O.exportFiles(doc,{}),loaded=JSON.parse(JSON.stringify(doc));loaded.mapLua=files['map.lua'];delete loaded.objectBlocks;
 const imported=O.importLogic(loaded,files['objects.greggnogg.json']);assert.deepEqual(imported.objectBlocks,JSON.parse(JSON.stringify(doc.objectBlocks)));
 const renamed=O.rename(imported,key,'custom:renamed');assert.equal(L.restoreCallback(w,renamed.objectBlocks['custom:renamed'].update,renamed.objectScripts['custom:renamed'].update,'custom:renamed','update'),true);
 assert.ok(w.getAllBlocks().some(b=>b.type==='greg_entity_flag'));w.dispose();
 const removed=O.remove(renamed,'custom:renamed');assert.equal(removed.objectBlocks['custom:renamed'],undefined);
});
test('malformed object block metadata rejects atomically and stale layouts preserve newer Lua',()=>{
 const {document:doc,key}=project();doc.objectScripts={[key]:{update:'-- current'}};const files=O.exportFiles(doc,{}),loaded=JSON.parse(JSON.stringify(doc));loaded.mapLua=files['map.lua'];const before=JSON.stringify(loaded),data=JSON.parse(files['objects.greggnogg.json']);
 data.blocks=[];assert.throws(()=>O.importLogic(loaded,JSON.stringify(data)),/block layouts/);assert.equal(JSON.stringify(loaded),before);
 data.blocks={[key]:{update:{source:'-- old',workspace:{}}}};const imported=O.importLogic(loaded,JSON.stringify(data));assert.equal(imported.objectScripts[key].update,'-- current');assert.equal(imported.objectBlocks[key].update,undefined);
});

test('renaming updates map blocks and creation blocks in other objects atomically',()=>{
 const L=require('../logic-blocks'),B=require('../vendor/blockly/blockly_compressed'),first=project(),second=O.create(first.document),doc=second.document,w=new B.Workspace();
 const tick=w.newBlock('greg_tick'),spawn=w.newBlock('greg_entity_spawn');spawn.setFieldValue(first.key,'TYPE');tick.getInput('DO').connection.connect(spawn.previousConnection);doc.mapBlocks=L.save(w);doc.mapLua=doc.mapBlocks.source;
 L.restoreCallback(w,null,'',second.key,'update');const create=w.newBlock('greg_entity_spawn');create.setFieldValue(first.key,'TYPE');w.getTopBlocks()[0].getInput('DO').connection.connect(create.previousConnection);const saved=L.saveCallback(w,second.key,'update');doc.objectBlocks={[second.key]:{update:saved}};doc.objectScripts={[second.key]:{update:saved.source}};
 const before=JSON.stringify(doc),renamed=O.rename(doc,first.key,'custom:projectile');assert.equal(JSON.stringify(doc),before);
 assert.match(renamed.mapLua,/custom:projectile/);assert.match(renamed.objectScripts[second.key].update,/custom:projectile/);
 assert.equal(L.restoreCallback(w,renamed.objectBlocks[second.key].update,renamed.objectScripts[second.key].update,second.key,'update'),true);w.dispose();
});

test('motion controls compile before custom logic and survive export/import',()=>{
 const {document:doc,key}=project();doc.objectMotion={[key]:{enabled:true,gravity:0.25,drag:0.5,maxFall:1}};doc.objectScripts={[key]:{update:'-- custom'}};
 const files=O.exportFiles(doc,{});assert.ok(files['map.lua'].indexOf('local motion')<files['map.lua'].indexOf('-- custom'));
 const imported=O.importLogic({...doc,mapLua:files['map.lua']},files['objects.greggnogg.json']);assert.deepEqual(imported.objectMotion,doc.objectMotion);
 assert.deepEqual(O.rename(doc,key,'custom:renamed').objectMotion['custom:renamed'],doc.objectMotion[key]);
 const source=O.compileScript({entities:{types:[{key:'demo:orb'}]},objectMotion:{'demo:orb':{enabled:true,gravity:0.25,drag:0.5,maxFall:1}}});assert.equal(source,fs.readFileSync(__dirname+'/../../tests/fixtures/object-motion-controls.lua','utf8'));
 doc.objectMotion[key].drag=2;assert.throws(()=>O.compileScript(doc),/Motion needs/);
});

test('vanilla physics interaction is an object checkbox setting, not a logic block',()=>{
 const {document:doc,key}=project();doc.objectMotion={[key]:{automatic:true,enabled:false,gravity:0.15,drag:0,maxFall:6,nativeTriggers:true}};
 const source=O.compileScript(doc);assert.match(source,/local native_interaction = entity\.get\(handle\)/);assert.match(source,/map\.trigger_mine_at\(native_interaction\.x, native_interaction\.y\)/);assert.equal(require('../logic-blocks').toolbox.contents.flatMap(category=>category.contents||[]).some(item=>item.type==='greg_trigger_mine'),false);
 doc.objectMotion[key].nativeTriggers='yes';assert.throws(()=>O.compileScript(doc),/enabled or disabled/);
});

test('object health controls generate managed health',()=>{
 const {document:doc,key}=project();doc.objectMotion={[key]:{healthEnabled:true,healthMaximum:5,healthCurrent:4}};
 const source=O.compileScript(doc);assert.match(source,/entity\.enable_health\(handle, 5, 4\)/);
 doc.objectMotion[key].healthCurrent=6;assert.throws(()=>O.compileScript(doc),/starting health/);
 doc.objectMotion[key].healthCurrent=4;doc.objectMotion[key].healthEnabled='yes';assert.throws(()=>O.compileScript(doc),/enabled or disabled/);
});

test('object health uses verified native damage sources and real damage amounts',()=>{
 const {document:doc,key}=project();doc.objectMotion={[key]:{healthEnabled:true,healthMaximum:200,healthCurrent:200,nativeKick:false,nativeMine:false}};
 const source=O.compileScript(doc);assert.match(source,/damage\.source_type == "native:kick" then return false/);assert.match(source,/damage\.source_type == "native:mine" then return false/);assert.match(source,/return true/);
 assert.match(fs.readFileSync(__dirname+'/../object-designer.js','utf8'),/Punch'.*12.*Kick'.*25.*Held sword'.*100/s);
 doc.objectMotion[key].nativeKick='no';assert.throws(()=>O.compileScript(doc),/enabled or disabled/);
});
test('object mine knockback is independent from managed health',()=>{
 const {document:doc,key}=project();doc.objectMotion={[key]:{mineKnockback:true}};
 assert.match(O.compileScript(doc),/entity\.set_mine_knockback\(handle, true\)/);
 doc.objectMotion[key].mineKnockback='yes';assert.throws(()=>O.compileScript(doc),/Mine knockback must be enabled or disabled/);
});

test('Bloopa treats an accepted native hit differently from ordinary lethal touch',t=>{
 const outer=__dirname+'/../../maps/untitled_map/',root=fs.existsSync(outer+'entities.json')?outer:outer+'untitled_map/';
 if(!fs.existsSync(root+'entities.json')||!fs.existsSync(root+'objects.greggnogg.json')||!fs.existsSync(root+'map.lua'))return t.skip('optional local Bloopa authoring fixture is not installed');
 const entities=JSON.parse(fs.readFileSync(root+'entities.json','utf8')),metadata=JSON.parse(fs.readFileSync(root+'objects.greggnogg.json','utf8'));
 const doc={entities,mapLua:metadata.baseLua,objectScripts:metadata.scripts,objectBlocks:metadata.blocks,objectMotion:metadata.motion};
 const source=O.compileScript(doc);assert.equal(source,fs.readFileSync(root+'map.lua','utf8'));
 assert.match(source,/selected_player = damage\.source_player[\s\S]*candidate\['input'\]\['attack'\]/);
 assert.match(source,/if not player\.input\.attack then\s+map\.defeat_player\(player\.player\)/);
});

test('terrain collision controls generate the native movement fixture',()=>{
 const source=O.compileScript({entities:{types:[{key:'demo:orb'}]},objectMotion:{'demo:orb':{enabled:true,gravity:0.25,drag:0,maxFall:6,collide:true,customSolids:true,width:16,height:16}}});
 assert.equal(source,fs.readFileSync(__dirname+'/../../tests/fixtures/object-terrain-motion.lua','utf8'));
 assert.match(source,/map.solid_box/);
});

test('old terrain-only collision source is preserved until the designer upgrades it',()=>{
 const doc={entities:{types:[{key:'demo:orb'}]},objectMotion:{'demo:orb':{collide:true}}};
 const legacy=O.compileScript(doc);assert.doesNotMatch(legacy,/entity.solid_box/);assert.match(legacy,/return map.solid_box\(x/);
 doc.objectMotion['demo:orb'].customSolids=true;assert.match(O.compileScript(doc),/entity.solid_box/);
});

test('contact, damage filtering and signal callbacks compile and survive editor metadata import',()=>{
 const {document:doc,key}=project();doc.objectScripts={[key]:{contact:'-- object hit',player_contact:'-- player hit',damage_filter:'return damage.source_type == "demo:blade"',damage:'-- damage received',signal:'-- signal received'}};
 const files=O.exportFiles(doc,{});assert.match(files['map.lua'],/entity\.on_contact/);assert.match(files['map.lua'],/function\(handle, contact\)/);assert.match(files['map.lua'],/entity\.on_player_contact/);
 assert.match(files['map.lua'],/entity\.on_damage_filter/);assert.match(files['map.lua'],/return damage\.source_type == "demo:blade"/);
 assert.match(files['map.lua'],/entity\.on_damage/);assert.match(files['map.lua'],/function\(handle, damage\)/);
 assert.match(files['map.lua'],/entity\.on_signal/);assert.match(files['map.lua'],/function\(handle, signal\)/);
 const loaded=JSON.parse(JSON.stringify(doc));loaded.mapLua=files['map.lua'];delete loaded.objectScripts;
 const imported=O.importLogic(loaded,files['objects.greggnogg.json']);assert.equal(imported.objectScripts[key].contact,'-- object hit');assert.equal(imported.objectScripts[key].player_contact,'-- player hit');assert.equal(imported.objectScripts[key].damage_filter,'return damage.source_type == "demo:blade"');assert.equal(imported.objectScripts[key].damage,'-- damage received');assert.equal(imported.objectScripts[key].signal,'-- signal received');
});
