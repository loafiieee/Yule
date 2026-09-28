'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const A = require('./core.js'), G = require('../editor-core.js');
test('defaults round-trip and checked-in native examples validate', () => {
  assert.deepEqual(A.parse(A.serialize(A.create())), A.create());
  for (const name of ['entity_package.json','entity_visual_package.json','entity_launch_pad.json','entity_double_jump.json','entity_damage.json','entity_projectile.json','entity_signal.json']) {
    assert.equal(A.validate(A.parse(fs.readFileSync(path.join(__dirname,'../../docs/examples',name),'utf8'))).errors.length,0);
  }
});
test('rejects duplicate and escaped duplicate keys without silently normalizing', () => {
  assert.throws(()=>A.parse('{"schema":1,"sch\\u0065ma":1,"capacity":1,"types":[],"placements":[]}'),/Duplicate/);
});
test('schema, references, geometry, visual ranges and limits fail closed', () => {
  const mutations = [
    d=>d.capacity=0, d=>d.capacity=1.2, d=>d.schema=3, d=>d.unknown=true,
    d=>d.types[0].key='bad', d=>d.types.push(A.clone(d.types[0])),
    d=>d.types[0].regions[0].width=0, d=>d.types[0].regions[0].mask=-1,
    d=>d.types[0].regions[0].role='danger', d=>d.types[0].regions[0].name='Bad Name',
    d=>{const r=A.clone(d.types[0].regions[0]);r.id=2;d.types[0].regions.push(r);},
    d=>d.placements[0].type='other:missing', d=>d.placements[0].x=NaN,
    d=>d.placements.push(A.clone(d.placements[0])), d=>d.types[0].visual.scale_x=0.0001,
    d=>d.types[0].visual.sheet='../escape.png', d=>d.types[0].visual.tint='#ffff',
    d=>d.types[0].visual.layer=2, d=>d.types[0].visual.rotation=360001,
    d=>d.placements[0].visual_rotation=-360001,
    d=>{d.types[0].visual.sprite=2147483647;d.types[0].visual.frames=2;},d=>d.placements[0].visible='no',
    d=>d.types[0].visual=null, d=>d.placements[0].x=null
  ];
  mutations.forEach(mutate=>{const d=A.create();mutate(d);assert.ok(A.validate(d).errors.length);assert.throws(()=>A.serialize(d));});
});
test('named animations validate as bounded author-defined clips with general playback modes',()=>{
 const d=A.create();d.types[0].visual.mode='ping_pong';d.types[0].visual.rotation=-15;d.types[0].animations=[{name:'idle',sprite:4,frames:2,frame_ticks:6},{name:'open',sprite:8,frames:3,frame_ticks:2,mode:'once'}];Object.assign(d.placements[0],{animation:'open',scale_x:1.5,scale_y:-1,visual_offset_x:2,visual_offset_y:-3,visual_rotation:45,visual_tint:'#12AB34FF',draw_layer:'behind'});assert.equal(A.validate(d).errors.length,0);assert.deepEqual(A.parse(A.serialize(d)).types[0].animations,d.types[0].animations);
 for(const mutate of [x=>x.types[0].animations[1].name='idle',x=>x.types[0].animations[0].name='default',x=>x.types[0].animations[0].name='Bad name',x=>x.types[0].animations[0].frames=0,x=>x.types[0].animations[0].mode='bounce',x=>x.placements[0].animation='missing',x=>x.placements[0].draw_layer='middle',x=>x.placements[0].visual_tint='#bad',x=>x.placements[0].scale_x=0,x=>x.types[0].animations=Array.from({length:33},(_,i)=>({name:'a'+i,sprite:0}))]){const invalid=A.clone(d);mutate(invalid);assert.ok(A.validate(invalid).errors.length);}
});
test('rename preserves references and failure preserves the input', () => {
  const d=A.create(), renamed=A.renameType(d,0,'custom:orb');
  assert.equal(renamed.placements[0].type,'custom:orb');assert.equal(d.types[0].key,'demo:orb');
  assert.throws(()=>A.renameType(d,0,'bad key'));assert.equal(d.types[0].key,'demo:orb');
});
test('export ZIP keeps the exact entity and Lua bytes', () => {
  const json=A.serialize(A.create()), script='-- deliberately not executed\nentity.on_update("demo:orb", function(h) end)\n';
  const zip=G.buildStoredZip({'entities.json':json,'map.lua':script});
  const files=G.parseStoredZip(zip);
  assert.equal(new TextDecoder().decode(files['entities.json']),json);
  assert.equal(new TextDecoder().decode(files['map.lua']),script);
  assert.deepEqual(G.buildStoredZip({'entities.json':json,'map.lua':script}),zip);
});

test('export matches the shared native-loader fixture byte for byte',()=>{
  const d=A.create();Object.assign(d.types[0].visual,{frames:3,frame_ticks:6,offset_y:-1.25,scale_x:-0.5,tint:'#80C0FFFF'});
  assert.equal(A.serialize(d),fs.readFileSync(path.join(__dirname,'../../tests/fixtures/workshop-entities.json'),'utf8'));
});

test('atlas inheritance, rectangular crops and padding match map grid rules',()=>{
  const sheets=A.parseAtlas(JSON.stringify({tileset:{sprite_sheet:'orb.png',cell_w:12,cell_h:8,padding:2,tiles:[{sprite_sheet:'other.png',cell_w:6}]}}));
  assert.deepEqual(A.spriteRegion('orb.png',{width:26,height:18},3,sheets),{x:14,y:10,w:12,h:8});
  assert.deepEqual(A.spriteRegion('other.png',{width:14,height:18},2,sheets),{x:0,y:10,w:6,h:8});
  assert.throws(()=>A.spriteRegion('orb.png',{width:25,height:18},0,sheets),/dimensions/);
  assert.throws(()=>A.spriteRegion('orb.png',{width:26,height:18},4,sheets),/outside/);
  assert.throws(()=>A.spriteRegion('absent.png',{width:16,height:16},0,sheets),/Import/);
  assert.throws(()=>A.parseAtlas('{"tileset":{"cell_w":0}}'),/cell_w/);
  assert.throws(()=>A.parseAtlas('{"tileset":{"padding":null}}'),/padding/);
  const cropped=A.parseAtlas(JSON.stringify({tileset:{sheets:[{sprite_sheet:'large.png',cell_w:16,cell_h:8,padding:0,source_x:37,source_y:19,source_w:32,source_h:16}]}}));
  assert.deepEqual(A.spriteRegion('large.png',{width:1000,height:700},3,cropped),{x:53,y:27,w:16,h:8});
  assert.throws(()=>A.spriteRegion('large.png',{width:60,height:40},0,cropped),/outside/);
});
test('entity PNG grids reject ambiguity, while identical declarations agree',()=>{
  const t={sprite_sheet:'orb.png',tiles:[{sprite_sheet:'orb.png',cell_w:16}]};
  assert.equal(A.spriteRegion('orb.png',{width:32,height:16},1,A.parseAtlas(JSON.stringify({tileset:t}))).x,16);
  t.tiles[0].cell_w=8;
  assert.throws(()=>A.spriteRegion('orb.png',{width:32,height:16},0,A.parseAtlas(JSON.stringify({tileset:t}))),/multiple grid/);
});
test('glyph preview includes the native one-pixel origin and nine-pixel stride',()=>{
  assert.deepEqual(A.spriteRegion('builtin:glyphs',{width:145,height:145},17,{}),{x:10,y:10,w:8,h:8});
  assert.deepEqual(A.spriteRegion('builtin:glyphs',{width:145,height:145},255,{}),{x:136,y:136,w:8,h:8});
  assert.throws(()=>A.spriteRegion('builtin:glyphs',{width:145,height:145},256,{}),/outside/);
});

function mapFixture() {
  const files=G.exportProjectFiles(G.upgradeToV2(G.createDefaultDocument()));
  return Object.fromEntries(Object.entries(files).map(([k,v])=>[k,typeof v==='string'?new TextEncoder().encode(v):v]));
}
test('complete map export retains exact map, assets and unchanged Lua through nested import',()=>{
  const files=mapFixture();files['map.lua']=new Uint8Array([45,45,32,128,10]);files['notes/readme.txt']=new TextEncoder().encode('Keep this file.');
  const map=A.openMap(Object.fromEntries(Object.entries(files).map(([k,v])=>['wrapper/archive/map/'+k,v])));
  const result=G.parseStoredZip(A.mapZip(map,map.doc,map.script)), prefix=map.folder+'/';
  for(const [name,bytes] of Object.entries(files))assert.deepEqual(result[prefix+name],bytes,name);
  assert.deepEqual(A.parse(new TextDecoder().decode(result[prefix+'entities.json'])),map.doc);
  const reopened=A.openMap(result);assert.equal(reopened.folder,map.folder);
  const edited=G.parseStoredZip(A.mapZip(map,map.doc,'-- revised Lua\n'));
  assert.equal(new TextDecoder().decode(edited[prefix+'map.lua']),'-- revised Lua\n');
  assert.deepEqual(map.files['map.lua'],files['map.lua']);
});
test('object workspace opens free-layout maps without a legacy layout order',()=>{
  const graph=G.convertMirroredToRoomGraph(G.upgradeToV2(G.createDefaultDocument()));
  const exported=G.exportProjectFiles(graph);
  const files=Object.fromEntries(Object.entries(exported).map(([name,value])=>[name,typeof value==='string'?new TextEncoder().encode(value):value]));
  const raw=JSON.parse(new TextDecoder().decode(files['data.json']));
  assert.equal(raw.layout.kind,G.ROOM_GRAPH_KIND);assert.equal(raw.layout.order,undefined);
  const opened=A.openMap(files);
  assert.deepEqual(opened.rooms,graph.rooms.map(room=>room.id));
});
test('map import rejects ambiguous roots, Windows collisions, traversal and invalid entities',()=>{
  const files=mapFixture();
  assert.throws(()=>A.openMap({...files,'../escape.txt':new Uint8Array()}),/Unsafe/);
  assert.throws(()=>A.openMap({...files,'other/data.json':files['data.json'],'other/data.map':files['data.map']}),/exactly one/);
  assert.throws(()=>A.openMap({...files,'Readme.txt':new Uint8Array(),'readme.txt':new Uint8Array()}),/Duplicate/);
  assert.throws(()=>A.openMap({...files,'Map.lua':new Uint8Array()}),/lowercase/);
  assert.throws(()=>A.openMap({...files,'entities.json':new TextEncoder().encode('{}')}),/schema/);
  const map=A.openMap(files), d=A.create();d.types[0].visual.sheet='missing.png';
  assert.throws(()=>A.mapZip(map,d,''),/Missing PNG/);
  assert.throws(()=>A.mapZip(map,map.doc,'bad\0script'),/NUL/);
});

test('room expansion matches native source/mirror coordinates and orientation',()=>{
  const d=A.create();d.schema=2;d.placements=[{name:'pad',type:'demo:orb',room:'side',x:8,y:12,vx:2}];
  const expanded=A.expandPlacements(d,['center','side','end']);
  assert.deepEqual(expanded.map(p=>[p.name,p.x,p.vx,p.mirrored]),[['pad',536,2,false],['pad.mirror',2104,-2,true]]);
  d.placements[0].room='center';assert.equal(A.expandPlacements(d,['center','side','end']).length,1);
  d.placements[0].side='mirrored';assert.throws(()=>A.expandPlacements(d,['center','side']),/center room/);
  d.placements[0].room='side';d.placements[0].side='both';d.capacity=1;
  assert.throws(()=>A.expandPlacements(d,['center','side']),/capacity/);
  assert.throws(()=>A.expandPlacements(d),/Unknown room/);
});
test('room expansion uses authored per-room dimensions',()=>{
  const d=A.create();d.schema=2;d.capacity=8;d.placements=[{name:'far',type:'demo:orb',room:'side',x:700,y:250,vx:2}];
  const rooms=[{id:'center',grid:Array.from({length:6},()=>Array(20).fill(' '))},{id:'side',grid:Array.from({length:18},()=>Array(47).fill(' '))}];
  const expanded=A.expandPlacements(d,rooms);
  assert.deepEqual(expanded.map(p=>[p.name,p.x,p.y,p.vx,p.mirrored]),[['far',700,250,2,false],['far.mirror',1124,250,-2,true]]);
  d.placements[0].x=753;assert.throws(()=>A.expandPlacements(d,rooms),/752 by 288/);
});
test('room graph expansion targets one exact placed room copy',()=>{
  const d=A.create();d.schema=2;d.capacity=8;
  d.placements=[{name:'upper_pad',type:'demo:orb',room:'side',instance:'upper_side',x:8,y:12,vx:2}];
  const map={rooms:[
    {id:'center',grid:Array.from({length:12},()=>Array(33).fill(' '))},
    {id:'side',grid:Array.from({length:18},()=>Array(47).fill(' '))}
  ],layout:{kind:'room_graph',nodes:[
    {id:'start',room:'center',x:-10,y:0},
    {id:'upper_side',room:'side',x:40,y:20,mirrorX:true}
  ]}};
  const expanded=A.expandPlacements(d,map);
  assert.deepEqual(expanded.map(p=>[p.name,p.x,p.y,p.vx,p.mirrored]),[['upper_pad',1544,332,-2,true]]);
  delete d.placements[0].instance;assert.throws(()=>A.expandPlacements(d,map),/must name a placed room instance/);
  d.placements[0].instance='missing';assert.throws(()=>A.expandPlacements(d,map),/Unknown placed room instance/);
  d.placements[0].instance='upper_side';d.placements[0].room='center';assert.throws(()=>A.expandPlacements(d,map),/does not match/);
  d.placements[0].room='side';d.placements[0].side='both';assert.throws(()=>A.expandPlacements(d,map),/do not use/);
});
test('ordinary maps become ready to use without asking for a format selection',()=>{
  const source=G.createDefaultDocument(),before=JSON.stringify(source),map=A.fromMapDocument(source);
  assert.equal(JSON.stringify(source),before);assert.ok(map.rooms.length);
  assert.equal(JSON.parse(new TextDecoder().decode(map.files['data.json'])).format,G.FORMAT_V2);
  assert.ok(A.mapZip(map,map.doc,map.script).length);
});

test('launch-pad starter exports the exact fixture executed by the real Lua runtime',()=>{
  const s=A.starter('pad','custom:launch_pad'),d={schema:2,capacity:16,types:[s.type],placements:[{name:'launch_pad',type:s.type.key,room:'center',x:48,y:64}]};
  assert.equal(A.serialize(d),fs.readFileSync(path.join(__dirname,'../../tests/fixtures/workshop-launch-pad.json'),'utf8'));
  assert.equal(s.script,fs.readFileSync(path.join(__dirname,'../../tests/fixtures/workshop-launch-pad.lua'),'utf8'));
  const script='-- user code\n'+s.script+'-- more user code\n',renamed=A.renameStarter(script,s.type.key,'custom:renamed');
  assert.ok(A.starterBlock(renamed,'custom:renamed'));assert.equal(A.starterBlock(renamed,s.type.key),null);
  assert.equal(A.removeStarter(script,s.type.key),'-- user code\n-- more user code\n');
});

test('picture import declares and packages the asset without changing existing tiles or bytes',()=>{
  const map=A.fromMapDocument(),before=map.files['data.json'].slice();
  const png=new Uint8Array([137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,32,0,0,0,16]);
  const added=A.addPicture(map,'My Picture.PNG',png);assert.equal(added.filename,'my_picture.png');
  assert.deepEqual(map.files['data.json'],before);assert.deepEqual(added.map.files[added.filename],png);
  assert.deepEqual(added.map.sheets[added.filename],{cell_w:32,cell_h:16,padding:0,source_x:0,source_y:0,source_w:32,source_h:16});
  const d=A.clone(added.map.doc);d.types[0].visual={sheet:added.filename,sprite:0};
  const exported=G.parseStoredZip(A.mapZip(added.map,d,''));assert.deepEqual(exported[added.map.folder+'/'+added.filename],png);
  assert.equal(A.addPicture(added.map,'My Picture.PNG',png).filename,'my_picture_2.png');
  assert.throws(()=>A.addPicture(map,'bad.png',new Uint8Array(24)),/PNG/);
  const large=png.slice();large[18]=33;assert.throws(()=>A.addPicture(map,'large.png',large),/8192/);
});

test('solid region purpose survives validated map serialization',()=>{
 const doc=A.create();doc.types[0].regions[0].role='solid';assert.equal(A.parse(A.serialize(doc)).types[0].regions[0].role,'solid');
});
test('detection area names round trip and are unique within an object type',()=>{
 const doc=A.create(),region=doc.types[0].regions[0];
 assert.equal(A.parse(A.serialize(doc)).types[0].regions[0].name,'body');
 const second=A.clone(region);second.id=2;second.name='feet';doc.types[0].regions.push(second);assert.equal(A.validate(doc).errors.length,0);
 second.name='body';assert.match(A.validate(doc).errors.find(error=>error.path.endsWith('.name')).message,/Duplicate/);
});
