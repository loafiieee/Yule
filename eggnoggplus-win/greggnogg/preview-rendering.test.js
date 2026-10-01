const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const Core=require('./editor-core.js');
const Author=require('./content-workspace/core.js');
const Objects=require('./object-tools.js');
const Queue=require('./preview-render-queue.js');
const flush=()=>new Promise(resolve=>setImmediate(resolve));

function fakeCanvas(){
  const draws=[],stack=[],context={globalAlpha:1,globalCompositeOperation:'source-over',
    save(){stack.push(this.globalAlpha);},restore(){this.globalAlpha=stack.pop();},
    translate(){},rotate(){},scale(){},fillRect(){},clearRect(){},
    drawImage(image,...geometry){draws.push({image,geometry,alpha:this.globalAlpha});}};
  return {width:0,height:0,context,draws,getContext(){return context;}};
}
function installCanvas(){
  const previous={document:global.document,Image:global.Image},surfaces=[];
  global.document={createElement(){const surface=fakeCanvas();surfaces.push(surface);return surface;}};
  global.Image=class{constructor(){this.width=128;this.height=256;}set src(url){this.url=url;queueMicrotask(()=>this.onload());}};
  return {surfaces,restore(){global.document=previous.document;global.Image=previous.Image;}};
}

test('one active render and newest pending render per canvas; separate targets progress',async()=>{
  const target={},other={},calls=[];let release;
  Queue.schedule(target,async()=>{calls.push('first');await new Promise(resolve=>release=resolve);});
  for(let i=0;i<100;i++)Queue.schedule(target,()=>calls.push(i));
  Queue.schedule(other,()=>calls.push('other'));await flush();
  assert.deepEqual(calls,['first','other']);release();await flush();
  assert.deepEqual(calls,['first','other',99]);
  const errors=[];Queue.schedule(target,()=>{throw Error('failed image');},error=>errors.push(error.message));
  Queue.schedule(target,()=>calls.push('recovered'));await flush();
  assert.deepEqual(errors,['failed image']);assert.equal(calls.at(-1),'recovered');
});

test('native objects reuse tinted surfaces without building/parsing map data; authored alpha and ghosts survive',async()=>{
  const fake=installCanvas(),originalBuild=Core.buildDataObject,originalParse=Author.parseAtlas;
  Core.buildDataObject=()=>{throw Error('Full map serialization on a render');};
  Author.parseAtlas=()=>{throw Error('Native sprite should not parse map atlas');};
  try{
    const doc=Core.upgradeToV2(Core.createDefaultDocument()),type={visual:{sheet:'builtin:tiles',sprite:30,tint:'#874521FF'}},output=fakeCanvas();
    for(let i=0;i<100;i++)await Objects.drawPicture(output.context,doc,type,8,8,i,false);
    assert.equal(fake.surfaces.length,1);assert.equal(output.draws.length,100);
    assert.ok(output.draws.every(draw=>draw.image===output.draws[0].image&&draw.alpha===1));
    type.visual.tint='#87452180';await Objects.drawPicture(output.context,doc,type,8,8,0,false,1,'default',null,0.28);
    assert.equal(fake.surfaces.length,1);assert.equal(output.draws.at(-1).alpha,128/255*0.28);
    type.visual.tint='#00FF00FF';await Objects.drawPicture(output.context,doc,type,8,8,0,false);
    assert.equal(fake.surfaces.length,2);assert.notEqual(output.draws.at(-1).image,output.draws[0].image);
  }finally{fake.restore();Core.buildDataObject=originalBuild;Author.parseAtlas=originalParse;}
});

test('external sheet geometry cache invalidates on in-place edits and uses bounded surface storage',async()=>{
  const fake=installCanvas(),originalParse=Author.parseAtlas;let parses=0;
  Author.parseAtlas=(...args)=>{parses++;return originalParse(...args);};
  try{
    const doc={tileset:{sprite_sheet:'art.png',cell_w:16,cell_h:16,tiles:[]},assets:{'art.png':'test:external-geometry'}},
      type={visual:{sheet:'art.png',sprite:1}},out=fakeCanvas();
    await Objects.drawPicture(out.context,doc,type,0,0,0,false);await Objects.drawPicture(out.context,doc,type,0,0,0,false);
    assert.equal(parses,1);assert.equal(fake.surfaces.length,1);assert.equal(out.draws[0].image.width,16);
    doc.tileset.cell_w=8;await Objects.drawPicture(out.context,doc,type,0,0,0,false);
    assert.equal(parses,2);assert.equal(out.draws.at(-1).image.width,8);
    const first=out.draws.at(-1).image;
    for(let i=0;i<140;i++){type.visual.tint='#'+i.toString(16).padStart(6,'0')+'FF';await Objects.drawPicture(out.context,doc,type,0,0,0,false);}
    delete type.visual.tint;await Objects.drawPicture(out.context,doc,type,0,0,0,false);
    assert.notEqual(first,out.draws.at(-1).image,'LRU eviction should discard the oldest surface');
    assert.equal(parses,2);
  }finally{fake.restore();Author.parseAtlas=originalParse;}
});

function extracted(name){
  const source=fs.readFileSync(__dirname+'/editor.js','utf8'),start=source.indexOf('  function '+name+'('),brace=source.indexOf('{',start);let depth=0;
  for(let i=brace;i<source.length;i++){depth+=(source[i]==='{')-(source[i]==='}');if(!depth)return source.slice(start,i+1);}
  throw Error('Missing editor render function');
}
test('actual room render discards stale frames and bounds async canvas allocation',async()=>{
  const canvas=fakeCanvas(),doc=Core.upgradeToV2(Core.createDefaultDocument());let release,atlasCalls=0,created=0,objects=0;
  const sandbox={Core,GregPreviewQueue:Queue,GregObjects:{async drawRoom(){objects++;}},
    state:{document:doc,roomIndex:0,roomRenderRequest:0},els:{'room-render-canvas':canvas},
    activeRoom:()=>doc.rooms[0],rooms:()=>doc.rooms,activePlacedRoomNode:()=>null,
    resolvedAppearance:()=>({bg1:'#000000'}),assetSources:()=>({}),previewTicks:()=>0,finalRoomWorldOffset:()=>0,
    Atlas:{renderRoom(){atlasCalls++;return atlasCalls===1?new Promise(resolve=>release=resolve):Promise.resolve();}},
    document:{createElement(){created++;return fakeCanvas();},body:{classList:{add(){},remove(){}}}},console};
  vm.createContext(sandbox);vm.runInContext(extracted('renderRoomCanvas'),sandbox);
  sandbox.renderRoomCanvas();for(let i=0;i<100;i++)sandbox.renderRoomCanvas();
  assert.equal(created,1);assert.equal(atlasCalls,1);release();await flush();
  assert.equal(created,2);assert.equal(atlasCalls,2);assert.equal(objects,2);assert.equal(canvas.draws.length,1);
});
