(function(root,factory){
  const api=factory(typeof module==='object'&&module.exports?require('./editor-core.js'):root.GregCore,typeof module==='object'&&module.exports?require('./content-workspace/core.js'):root.EntityAuthor,()=>typeof module==='object'&&module.exports?require('./logic-blocks.js'):root.GregLogicBlocks);
  if(typeof module==='object'&&module.exports)module.exports=api;else root.GregObjects=api;
}(typeof globalThis!=='undefined'?globalThis:this,function(G,A,logic){
  'use strict';
  const clone=x=>JSON.parse(JSON.stringify(x));
  const label=key=>key.split(':').pop().replace(/_/g,' ');
  function catalog(doc){return doc.entities||{schema:2,capacity:4096,types:[],placements:[]};}
  function create(doc){
    const next=doc.format===G.FORMAT_V2?clone(doc):G.upgradeToV2(doc),entities=clone(catalog(next));let key='custom:object',i=2;
    while(entities.types.some(t=>t.key===key))key='custom:object_'+i++;
    entities.types.push({key,regions:[],visual:{sheet:'builtin:tiles',sprite:4}});next.entities=entities;(next.objectMotion||={})[key]={automatic:false,enabled:false,gravity:0.15,drag:0,maxFall:6};return {document:next,key};
  }
  function atCell(doc,placement,room,row,col){
    const index=doc.layout.order.indexOf(room);if(index<0)return false;
    if(placement.room!==undefined&&placement.room!==room)return false;
    const x=placement.room===undefined?placement.x-(doc.layout.order.length-1-index)*528:placement.x;
    return Math.floor(x/16)===col&&Math.floor(placement.y/16)===row;
  }
  function paint(doc,key,room,changes,erase){
    const entities=clone(catalog(doc));if(!erase&&!entities.types.some(t=>t.key===key))throw Error('Choose an object from Custom tiles.');
    const before=JSON.stringify(entities.placements);
    for(const c of changes){
      if(!Number.isInteger(c.row)||!Number.isInteger(c.col)||c.row<0||c.row>=12||c.col<0||c.col>=33)continue;
      const x=c.col*16+8,y=c.row*16+8;
      const at=entities.placements.filter(p=>atCell(doc,p,room,c.row,c.col));
      if(!erase&&at.length===1&&at[0].type===key)continue;
      entities.placements=entities.placements.filter(p=>!(atCell(doc,p,room,c.row,c.col)));
      if(!erase){let name='object_'+room.toLowerCase().replace(/[^a-z0-9_.-]/g,'_').slice(0,50)+'_'+c.row+'_'+c.col,i=2,base=name;while(entities.placements.some(p=>p.name===name||p.name===name+'.mirror'))name=base+'_'+i++;entities.placements.push({name,type:key,room,side:'both',x,y});}
    }
    entities.schema=2;if(entities.types.length){A.serialize(entities);A.expandPlacements(entities,doc.layout.order);}
    if(before===JSON.stringify(entities.placements))return false;doc.entities=entities;return true;
  }
  function duplicateRoom(doc,source,target,order){
    const entities=clone(catalog(doc));
    for(const placement of entities.placements.filter(p=>p.room===source)){
      const copy=clone(placement),base=placement.name.slice(0,70)+'_copy';let n=1;
      copy.room=target;copy.name=base;
      while(entities.placements.some(p=>p.name===copy.name||p.name===copy.name+'.mirror'))copy.name=base+'_'+n++;
      entities.placements.push(copy);
    }
    if(entities.types.length){A.serialize(entities);A.expandPlacements(entities,order);doc.entities=entities;}
  }
  function remove(doc,key){const next=clone(doc);next.entities=clone(catalog(next));if(next.entities.placements.some(p=>p.type===key))throw Error('Erase this object from the map before deleting its design.');next.entities.types=next.entities.types.filter(t=>t.key!==key);if(next.objectScripts)delete next.objectScripts[key];if(next.objectBlocks)delete next.objectBlocks[key];if(next.objectMotion)delete next.objectMotion[key];return next;}
  function rename(doc,old,key){
    const next=clone(doc),index=catalog(next).types.findIndex(t=>t.key===old);next.entities=A.renameType(catalog(next),index,key);
    if(next.mapBlocks||next.objectBlocks){
      const L=logic();
      if(L){
        const map=L.renameReference(next.mapBlocks,String(next.mapLua||''),old,key);
        if(map){next.mapBlocks=map;next.mapLua=map.source;}
        for(const owner of Object.keys(next.objectBlocks||{}))for(const event of Object.keys(next.objectBlocks[owner])){
          const saved=L.renameReference(next.objectBlocks[owner][event],((next.objectScripts||{})[owner]||{})[event]||'',old,key,owner,event);
          if(saved){next.objectBlocks[owner][event]=saved;((next.objectScripts||={})[owner]||={})[event]=saved.source;}
        }
      }
    }
    if(next.objectScripts&&next.objectScripts[old]){next.objectScripts[key]=next.objectScripts[old];delete next.objectScripts[old];}
    if(next.objectBlocks&&next.objectBlocks[old]){next.objectBlocks[key]=next.objectBlocks[old];delete next.objectBlocks[old];}
    if(next.objectMotion&&next.objectMotion[old]){next.objectMotion[key]=next.objectMotion[old];delete next.objectMotion[old];}
    return next;
  }
  function collisionSource(settings){
    if(!settings||!settings.collide)return '';
    const width=settings.width===undefined?16:settings.width,height=settings.height===undefined?16:settings.height;
    if(!Number.isFinite(width)||width<1||width>128||!Number.isFinite(height)||height<1||height>128)throw Error('Collision width and height must be 1..128 pixels.');
    return `local body = entity.get(handle)
if math.abs(body.vx) > 64 or math.abs(body.vy) > 64 then error('Terrain collision speed exceeds 64 pixels per tick') end
local function sweep(x, y, speed, horizontal)
  local steps = math.ceil(math.abs(speed))
  if steps == 0 then return 0 end
  local step = speed / steps
  local moved = 0
  for i = 1, steps do
    local function blocked(distance)
      local cx, cy = x + (horizontal and distance or 0), y + (horizontal and 0 or distance)
      return map.solid_box(cx, cy, ${width}, ${height}) or entity.solid_box(cx, cy, ${width}, ${height}, handle)
    end
    if blocked(moved + step) then
      local low, high = 0, 1
      for j = 1, 8 do
        local middle = (low + high) / 2
        if blocked(moved + step * middle) then high = middle else low = middle end
      end
      return moved + step * low
    end
    moved = moved + step
  end
  return moved
end
local dx = sweep(body.x, body.y, body.vx, true)
local dy = sweep(body.x + dx, body.y, body.vy, false)
entity.set(handle, {vx = dx, vy = dy})
`;
  }
  function motionSource(settings){
    if(!settings||!settings.enabled)return '';
    const {gravity=0.15,drag=0,maxFall=6}=settings;
    if(!Number.isFinite(gravity)||Math.abs(gravity)>16||!Number.isFinite(drag)||drag<0||drag>1||!Number.isFinite(maxFall)||maxFall<=0||maxFall>64)throw Error('Motion needs gravity -16..16, drag 0..1 and maximum speed above 0 through 64.');
    return 'local motion = entity.get(handle)\nentity.set(handle, {vx = motion.vx * '+(1-drag)+', vy = math.max(-'+maxFall+', math.min('+maxFall+', motion.vy + '+gravity+'))})\n';
  }
  function compileScript(doc){
    let text=String(doc.mapLua||'');const scripts=doc.objectScripts||{};
    for(const type of catalog(doc).types.slice().sort((a,b)=>(a.key<b.key?-1:a.key>b.key?1:0))){const events=scripts[type.key]||{};for(const event of ['spawn','update','remove']){const body=(event==='spawn'&&(doc.objectMotion||{})[type.key]?.automatic===false?'entity.set(handle, {automatic_motion = false})\n':'')+(event==='update'?motionSource((doc.objectMotion||{})[type.key])+collisionSource((doc.objectMotion||{})[type.key]):'')+(events[event]||'');if(body)text+='\nentity.on_'+event+'('+JSON.stringify(type.key)+', function(handle, value)\n'+body+'\nend)\n';}}
    if(new TextEncoder().encode(text).length>262144||text.includes('\0'))throw Error('Combined object logic exceeds the script limit or contains a NUL byte.');return text;
  }
  function exportFiles(doc,files){
    const out=Object.assign({},files),entities=catalog(doc);
    if(entities.types.length){out['entities.json']=A.serialize(entities);A.expandPlacements(entities,doc.layout.order);out['map.lua']=compileScript(doc);out['objects.greggnogg.json']=JSON.stringify({schema:1,baseLua:String(doc.mapLua||''),scripts:doc.objectScripts||{},blocks:doc.objectBlocks||{},motion:doc.objectMotion||{}},null,2)+'\n';}
    if(doc.mapBlocks&&doc.mapBlocks.source===String(doc.mapLua||''))out['logic.greggnogg.json']=JSON.stringify(doc.mapBlocks)+'\n';
    return out;
  }
  function importLogic(doc,text){
    const data=JSON.parse(text);if(!data||data.schema!==1||typeof data.baseLua!=='string'||!data.scripts||typeof data.scripts!=='object'||Array.isArray(data.scripts))throw Error('Invalid object logic editor metadata.');
    const scripts=Object.create(null);for(const key of Object.keys(data.scripts)){if(!catalog(doc).types.some(t=>t.key===key))throw Error('Object logic references an unknown design.');const events=data.scripts[key];if(!events||typeof events!=='object'||Array.isArray(events))throw Error('Invalid object events.');scripts[key]={};for(const event of Object.keys(events)){if(!['spawn','update','remove'].includes(event)||typeof events[event]!=='string')throw Error('Invalid object callback.');scripts[key][event]=events[event];}}
    const blocks=data.blocks===undefined?{}:data.blocks;
    if(!blocks||typeof blocks!=='object'||Array.isArray(blocks)||JSON.stringify(blocks).length>4194304)throw Error('Invalid or oversized object block layouts.');
    for(const key of Object.keys(blocks)){
      if(!catalog(doc).types.some(t=>t.key===key)||!blocks[key]||typeof blocks[key]!=='object'||Array.isArray(blocks[key]))throw Error('Object blocks reference an unknown design or invalid events.');
      for(const event of Object.keys(blocks[key])){
        const saved=blocks[key][event];
        if(!['spawn','update','remove'].includes(event)||!saved||typeof saved!=='object'||typeof saved.source!=='string'||!saved.workspace||typeof saved.workspace!=='object'||Array.isArray(saved.workspace)||JSON.stringify(saved.workspace).length>1048576)throw Error('Invalid object callback block layout.');
        if(saved.source!==((scripts[key]||{})[event]||''))delete blocks[key][event]; /* Keep newer hand-edited Lua. */
      }
    }
    const next=clone(doc);next.mapLua=data.baseLua;next.objectScripts=scripts;next.objectBlocks=clone(blocks);next.objectMotion=data.motion===undefined?{}:clone(data.motion);if(!next.objectMotion||typeof next.objectMotion!=='object'||Array.isArray(next.objectMotion))throw Error('Invalid object motion settings.');if(compileScript(next)!==String(doc.mapLua||''))throw Error('Object editor metadata does not match map.lua; preserve the edited script before importing.');return next;
  }
  async function prepareForSave(doc,loadImage=imageFor){
    const candidate=clone(doc),entities=catalog(candidate);
    if(!entities.types.length)return candidate;
    A.serialize(entities);A.expandPlacements(entities,candidate.layout.order);compileScript(candidate);
    const sheets=A.parseAtlas(JSON.stringify(G.buildDataObject(candidate)));
    for(const type of entities.types){
      if(!type.visual)continue;
      const v=type.visual,picture=await loadImage(candidate,v.sheet);
      A.spriteRegion(v.sheet,picture,v.sprite+(v.frames||1)-1,sheets);
    }
    return candidate;
  }
  const images=new Map();
  function imageFor(doc,sheet){
    const builtin={'builtin:tiles':'tiles.png','builtin:sprites':'sprites.png','builtin:misc':'misc.png','builtin:glyphs':'font8x8.png'},url=builtin[sheet]?'assets/game/'+builtin[sheet]:(doc.assets||{})[sheet];
    if(!url)return Promise.reject(Error('Missing picture: '+sheet));
    if(!images.has(url))images.set(url,new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=()=>reject(Error('Picture could not load: '+sheet));image.src=url;}));return images.get(url);
  }
  async function drawPicture(ctx,doc,type,x,y,tick,mirrored,scale=1){
    if(!type.visual)return;const v=type.visual,image=await imageFor(doc,v.sheet),data=G.buildDataObject(doc),sheets=data.tileset?A.parseAtlas(JSON.stringify(data)):{};
    const r=A.spriteRegion(v.sheet,image,v.sprite+Math.floor(tick/(v.frame_ticks||1))%(v.frames||1),sheets),surface=document.createElement('canvas');surface.width=r.w;surface.height=r.h;const c=surface.getContext('2d'),tint=v.tint||'#FFFFFFFF';
    c.drawImage(image,r.x,r.y,r.w,r.h,0,0,r.w,r.h);c.globalCompositeOperation='multiply';c.fillStyle=tint.slice(0,7);c.fillRect(0,0,r.w,r.h);c.globalCompositeOperation='destination-in';c.drawImage(image,r.x,r.y,r.w,r.h,0,0,r.w,r.h);
    ctx.save();ctx.imageSmoothingEnabled=false;ctx.translate(x+(v.offset_x||0)*scale*(mirrored?-1:1),y+(v.offset_y||0)*scale);ctx.scale((v.scale_x===undefined?1:v.scale_x)*scale*(mirrored?-1:1),(v.scale_y===undefined?1:v.scale_y)*scale);ctx.globalAlpha=parseInt(tint.slice(7,9),16)/255;ctx.drawImage(surface,-r.w/2,-r.h/2);ctx.restore();
  }
  async function drawRoom(canvas,doc,room,tick,mirrored){
    const entities=catalog(doc);if(!entities.types.length)return;const order=doc.layout.order,index=order.indexOf(room),final=order.length-1+(mirrored?index:-index),x0=final*528,ctx=canvas.getContext('2d');
    for(const p of A.expandPlacements(entities,order)){const type=entities.types.find(t=>t.key===p.type);await drawPicture(ctx,doc,type,p.x-x0,p.y,tick,p.mirrored);}
  }
  return {catalog,create,atCell,paint,duplicateRoom,remove,rename,compileScript,exportFiles,importLogic,prepareForSave,label,imageFor,drawPicture,drawRoom};
}));
