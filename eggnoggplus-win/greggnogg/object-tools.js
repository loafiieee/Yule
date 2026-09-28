(function(root,factory){
  const api=factory(typeof module==='object'&&module.exports?require('./editor-core.js'):root.GregCore,typeof module==='object'&&module.exports?require('./content-workspace/core.js'):root.EntityAuthor,()=>typeof module==='object'&&module.exports?require('./logic-blocks.js'):root.GregLogicBlocks);
  if(typeof module==='object'&&module.exports)module.exports=api;else root.GregObjects=api;
}(typeof globalThis!=='undefined'?globalThis:this,function(G,A,logic){
  'use strict';
  const clone=x=>JSON.parse(JSON.stringify(x));
  const label=key=>key.split(':').pop().replace(/_/g,' ');
  function catalog(doc){return doc.entities||{schema:2,capacity:4096,types:[],placements:[]};}
  function roomWidth(room){if(!room||!Array.isArray(room.grid)||!room.grid.length)return 33;return (Array.isArray(room.grid[0])?room.grid[0]:String(room.grid[0]||'')).length||33;}
  function roomHeight(room){return room&&Array.isArray(room.grid)&&room.grid.length?room.grid.length:12;}
  function sourceRoomOffset(doc,index){let cells=0;for(let i=index+1;i<doc.rooms.length;i++)cells+=roomWidth(doc.rooms[i]);return cells*16;}
  function finalRoomOffset(doc,index,mirrored){if(!mirrored)return sourceRoomOffset(doc,index);let cells=roomWidth(doc.rooms[0]);for(let i=1;i<doc.rooms.length;i++)cells+=roomWidth(doc.rooms[i]);for(let i=1;i<index;i++)cells+=roomWidth(doc.rooms[i]);return cells*16;}
  function create(doc){
    const next=doc.format===G.FORMAT_V2?clone(doc):G.upgradeToV2(doc),entities=clone(catalog(next));let key='custom:object',i=2;
    while(entities.types.some(t=>t.key===key))key='custom:object_'+i++;
    entities.types.push({key,regions:[],visual:{sheet:'builtin:tiles',sprite:4}});next.entities=entities;(next.objectMotion||={})[key]={automatic:false,enabled:false,gravity:0.15,drag:0,maxFall:6};return {document:next,key};
  }
  function atCell(doc,placement,room,row,col,instanceId){
    const order=Array.isArray(doc.layout.order)?doc.layout.order:(doc.rooms||[]).map(entry=>entry.id),index=order.indexOf(room);if(index<0)return false;
    if(doc.layout.kind==='room_graph'&&placement.instance!==instanceId)return false;
    if(placement.room!==undefined&&placement.room!==room)return false;
    const x=placement.room===undefined?placement.x-sourceRoomOffset(doc,index):placement.x;
    return Math.floor(x/16)===col&&Math.floor(placement.y/16)===row;
  }
  function transformAreaPlacements(placements,operation,width,height){
    if(!Array.isArray(placements)||!Number.isInteger(width)||width<1||!Number.isInteger(height)||height<1)throw Error('Invalid object selection.');
    return placements.map(source=>{
      const copy=clone(source);let scale,angle;
      if(!Number.isFinite(copy.x)||!Number.isFinite(copy.y))throw Error('Selected object has an invalid position.');
      if(operation==='flip_x'){
        copy.x=Math.max(0,Math.min(width*16-1/256,width*16-copy.x));scale=-(copy.scale_x===undefined?1:copy.scale_x);if(scale===1)delete copy.scale_x;else copy.scale_x=scale;
      }else if(operation==='flip_y'){
        copy.y=Math.max(0,Math.min(height*16-1/256,height*16-copy.y));scale=-(copy.scale_y===undefined?1:copy.scale_y);if(scale===1)delete copy.scale_y;else copy.scale_y=scale;
      }else if(operation==='rotate_cw'){
        const oldX=copy.x;copy.x=Math.max(0,Math.min(height*16-1/256,height*16-copy.y));copy.y=Math.max(0,Math.min(width*16-1/256,oldX));angle=(copy.visual_rotation||0)+90;while(angle>180)angle-=360;copy.visual_rotation=angle;if(!angle)delete copy.visual_rotation;
      }else throw Error('Unknown object selection transform.');
      return copy;
    });
  }
  function paint(doc,key,room,changes,erase,instanceId){
    const entities=clone(catalog(doc));if(!erase&&!entities.types.some(t=>t.key===key))throw Error('Choose an object from Custom tiles.');
    if(doc.layout.kind==='room_graph'){
      const node=(doc.layout.nodes||[]).find(entry=>entry&&entry.id===instanceId);
      if(!node||node.room!==room)throw Error('Choose a placed room copy from Rooms before painting custom objects.');
    }
    const before=JSON.stringify(entities.placements);
    for(const c of changes){
      const sourceRoom=(doc.rooms||[]).find(entry=>entry.id===room),rows=roomHeight(sourceRoom),cols=roomWidth(sourceRoom);
      if(!Number.isInteger(c.row)||!Number.isInteger(c.col)||c.row<0||c.row>=rows||c.col<0||c.col>=cols)continue;
      const x=c.col*16+8,y=c.row*16+8;
      const at=entities.placements.filter(p=>atCell(doc,p,room,c.row,c.col,instanceId));
      if(!erase&&at.length===1&&at[0].type===key)continue;
      entities.placements=entities.placements.filter(p=>!(atCell(doc,p,room,c.row,c.col,instanceId)));
      if(!erase){let name='object_'+room.toLowerCase().replace(/[^a-z0-9_.-]/g,'_').slice(0,50)+'_'+c.row+'_'+c.col,i=2,base=name;while(entities.placements.some(p=>p.name===name||p.name===name+'.mirror'))name=base+'_'+i++;const placed={name,type:key,room,side:'both',x,y};if(doc.layout.kind==='room_graph'){placed.instance=instanceId;delete placed.side;}entities.placements.push(placed);}
    }
    entities.schema=2;if(entities.types.length){A.serialize(entities);A.expandPlacements(entities,doc);}
    if(before===JSON.stringify(entities.placements))return false;doc.entities=entities;return true;
  }
  function duplicateRoom(doc,source,target,order){
    const entities=clone(catalog(doc));
    for(const placement of (doc.layout.kind==='room_graph'?[]:entities.placements.filter(p=>p.room===source))){
      const copy=clone(placement),base=placement.name.slice(0,70)+'_copy';let n=1;
      copy.room=target;copy.name=base;
      while(entities.placements.some(p=>p.name===copy.name||p.name===copy.name+'.mirror'))copy.name=base+'_'+n++;
      entities.placements.push(copy);
    }
    if(entities.types.length){const sourceRoom=(doc.rooms||[]).find(room=>room.id===source),roomSpecs=order.map(id=>(doc.rooms||[]).find(room=>room.id===id)||(id===target&&sourceRoom?Object.assign({},sourceRoom,{id:target}):id));A.serialize(entities);A.expandPlacements(entities,doc.layout.kind==='room_graph'?doc:roomSpecs);doc.entities=entities;}
  }
  function remove(doc,key){const next=clone(doc);next.entities=clone(catalog(next));next.entities.placements=next.entities.placements.filter(p=>p.type!==key);next.entities.types=next.entities.types.filter(t=>t.key!==key);if(next.objectScripts)delete next.objectScripts[key];if(next.objectBlocks)delete next.objectBlocks[key];if(next.objectMotion)delete next.objectMotion[key];return next;}
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
    if(settings.customSolids!==undefined&&typeof settings.customSolids!=='boolean')throw Error('Custom solid collision setting must be boolean.');
    const blocked=settings.customSolids?`local cx, cy = x + (horizontal and distance or 0), y + (horizontal and 0 or distance)
      return map.solid_box(cx, cy, ${width}, ${height}) or entity.solid_box(cx, cy, ${width}, ${height}, handle)`:`return map.solid_box(x + (horizontal and distance or 0), y + (horizontal and 0 or distance), ${width}, ${height})`;
    return `local body = entity.get(handle)
if math.abs(body.vx) > 64 or math.abs(body.vy) > 64 then error('Terrain collision speed exceeds 64 pixels per tick') end
local function sweep(x, y, speed, horizontal)
  local steps = math.ceil(math.abs(speed))
  if steps == 0 then return 0 end
  local step = speed / steps
  local moved = 0
  for i = 1, steps do
    local function blocked(distance)
      ${blocked}
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
  function nativeInteractionSource(settings){
    if(settings&&settings.nativeTriggers!==undefined&&typeof settings.nativeTriggers!=='boolean')throw Error('Vanilla physics trigger interaction must be enabled or disabled.');
    if(!settings||!settings.nativeTriggers)return '';
    return 'local native_interaction = entity.get(handle)\nmap.trigger_mine_at(native_interaction.x, native_interaction.y)\n';
  }
  function healthSpawnSource(settings){
    if(settings&&settings.healthEnabled!==undefined&&typeof settings.healthEnabled!=='boolean')throw Error('Object health must be enabled or disabled.');
    if(!settings||!settings.healthEnabled)return '';
    const maximum=settings.healthMaximum===undefined?1:settings.healthMaximum,current=settings.healthCurrent===undefined?maximum:settings.healthCurrent;
    if(!Number.isFinite(maximum)||maximum<=0||maximum>1000000000||!Number.isFinite(current)||current<0||current>maximum)throw Error('Health needs a maximum above 0 and starting health from 0 through the maximum.');
    return 'entity.enable_health(handle, '+maximum+', '+current+')\n';
  }
  function mineKnockbackSpawnSource(settings){
    if(settings&&settings.mineKnockback!==undefined&&typeof settings.mineKnockback!=='boolean')throw Error('Mine knockback must be enabled or disabled.');
    return settings&&settings.mineKnockback?'entity.set_mine_knockback(handle, true)\n':'';
  }
  function nativeDamageFilterSource(settings,hasCustom){
    if(!settings||!settings.healthEnabled)return '';
    const sources=[['nativePunch','native:punch'],['nativeKick','native:kick'],['nativeSword','native:sword'],['nativeThrownSword','native:thrown_sword'],['nativeSpikeBall','native:spike_ball'],['nativeMine','native:mine']];
    let source='';
    for(const [key,name] of sources){if(settings[key]!==undefined&&typeof settings[key]!=='boolean')throw Error('Native damage source settings must be enabled or disabled.');if(settings[key]===false)source+='if damage.source_type == '+JSON.stringify(name)+' then return false end\n';}
    return source&& !hasCustom ? source+'return true\n' : source;
  }
  function compileScript(doc){
    let text=String(doc.mapLua||'');const scripts=doc.objectScripts||{};
    for(const type of catalog(doc).types.slice().sort((a,b)=>(a.key<b.key?-1:a.key>b.key?1:0))){const events=scripts[type.key]||{};for(const event of ['spawn','update','animation_finish','contact','player_contact','damage_filter','damage','defeated','signal','remove']){const settings=(doc.objectMotion||{})[type.key],custom=events[event]||'',body=(event==='spawn'?(settings?.automatic===false?'entity.set(handle, {automatic_motion = false})\n':'')+healthSpawnSource(settings)+mineKnockbackSpawnSource(settings):'')+(event==='update'?motionSource(settings)+collisionSource(settings)+nativeInteractionSource(settings):'')+(event==='damage_filter'?nativeDamageFilterSource(settings,!!custom):'')+custom;const argument=['contact','player_contact'].includes(event)?'contact':['damage_filter','damage'].includes(event)?'damage':event==='defeated'?'defeat':event==='signal'?'signal':event==='animation_finish'?'animation':'value';if(body)text+='\nentity.on_'+event+'('+JSON.stringify(type.key)+', function(handle, '+argument+')\n'+body+'\nend)\n';}}
    if(new TextEncoder().encode(text).length>262144||text.includes('\0'))throw Error('Combined object logic exceeds the script limit or contains a NUL byte.');return text;
  }
  function exportFiles(doc,files){
    const out=Object.assign({},files),entities=catalog(doc);
    if(entities.types.length){out['entities.json']=A.serialize(entities);A.expandPlacements(entities,doc);out['map.lua']=compileScript(doc);out['objects.greggnogg.json']=JSON.stringify({schema:1,baseLua:String(doc.mapLua||''),scripts:doc.objectScripts||{},blocks:doc.objectBlocks||{},motion:doc.objectMotion||{}},null,2)+'\n';}
    if(doc.mapBlocks&&doc.mapBlocks.source===String(doc.mapLua||''))out['logic.greggnogg.json']=JSON.stringify(doc.mapBlocks)+'\n';
    return out;
  }
  function importLogic(doc,text){
    const data=JSON.parse(text);if(!data||data.schema!==1||typeof data.baseLua!=='string'||!data.scripts||typeof data.scripts!=='object'||Array.isArray(data.scripts))throw Error('Invalid object logic editor metadata.');
    const scripts=Object.create(null);for(const key of Object.keys(data.scripts)){if(!catalog(doc).types.some(t=>t.key===key))throw Error('Object logic references an unknown design.');const events=data.scripts[key];if(!events||typeof events!=='object'||Array.isArray(events))throw Error('Invalid object events.');scripts[key]={};for(const event of Object.keys(events)){if(!['spawn','update','animation_finish','contact','player_contact','damage_filter','damage','defeated','signal','remove'].includes(event)||typeof events[event]!=='string')throw Error('Invalid object callback.');scripts[key][event]=events[event];}}
    const blocks=data.blocks===undefined?{}:data.blocks;
    if(!blocks||typeof blocks!=='object'||Array.isArray(blocks)||JSON.stringify(blocks).length>4194304)throw Error('Invalid or oversized object block layouts.');
    for(const key of Object.keys(blocks)){
      if(!catalog(doc).types.some(t=>t.key===key)||!blocks[key]||typeof blocks[key]!=='object'||Array.isArray(blocks[key]))throw Error('Object blocks reference an unknown design or invalid events.');
      for(const event of Object.keys(blocks[key])){
        const saved=blocks[key][event];
        if(!['spawn','update','animation_finish','contact','player_contact','damage_filter','damage','defeated','signal','remove'].includes(event)||!saved||typeof saved!=='object'||typeof saved.source!=='string'||!saved.workspace||typeof saved.workspace!=='object'||Array.isArray(saved.workspace)||JSON.stringify(saved.workspace).length>1048576)throw Error('Invalid object callback block layout.');
        if(saved.source!==((scripts[key]||{})[event]||''))delete blocks[key][event]; /* Keep newer hand-edited Lua. */
      }
    }
    const next=clone(doc);next.mapLua=data.baseLua;next.objectScripts=scripts;next.objectBlocks=clone(blocks);next.objectMotion=data.motion===undefined?{}:clone(data.motion);if(!next.objectMotion||typeof next.objectMotion!=='object'||Array.isArray(next.objectMotion))throw Error('Invalid object motion settings.');if(compileScript(next)!==String(doc.mapLua||''))throw Error('Object editor metadata does not match map.lua; preserve the edited script before importing.');return next;
  }
  async function prepareForSave(doc,loadImage=imageFor){
    const candidate=clone(doc),entities=catalog(candidate);
    if(!entities.types.length)return candidate;
    A.serialize(entities);A.expandPlacements(entities,candidate);compileScript(candidate);
    const sheets=A.parseAtlas(JSON.stringify(G.buildDataObject(candidate)));
    for(const type of entities.types){
      if(!type.visual)continue;
      const v=type.visual,picture=await loadImage(candidate,v.sheet);
      A.spriteRegion(v.sheet,picture,v.sprite+(v.frames||1)-1,sheets);
      for(const animation of type.animations||[])A.spriteRegion(v.sheet,picture,animation.sprite+(animation.frames||1)-1,sheets);
    }
    return candidate;
  }
  const images=new Map();
  function imageFor(doc,sheet){
    const builtin={'builtin:tiles':'tiles.png','builtin:sprites':'sprites.png','builtin:misc':'misc.png','builtin:glyphs':'font8x8.png'},url=builtin[sheet]?'assets/game/'+builtin[sheet]:(doc.assets||{})[sheet];
    if(!url)return Promise.reject(Error('Missing picture: '+sheet));
    if(!images.has(url))images.set(url,new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=()=>reject(Error('Picture could not load: '+sheet));image.src=url;}));return images.get(url);
  }
  function multiplyTint(first,second){
    const a=/^#[0-9a-f]{8}$/i.test(first||'')?first:'#FFFFFFFF',b=/^#[0-9a-f]{8}$/i.test(second||'')?second:'#FFFFFFFF';let out='#';
    for(let i=1;i<9;i+=2)out+=Math.round(parseInt(a.slice(i,i+2),16)*parseInt(b.slice(i,i+2),16)/255).toString(16).padStart(2,'0');return out.toUpperCase();
  }
  async function drawPicture(ctx,doc,type,x,y,tick,mirrored,scale=1,animationName='default',placement,editorOpacity=1){
    if(!type.visual)return;const v=type.visual,image=await imageFor(doc,v.sheet),data=G.buildDataObject(doc),sheets=data.tileset?A.parseAtlas(JSON.stringify(data)):{};
    const clip=animationName==='default'?v:(type.animations||[]).find(animation=>animation.name===animationName)||v;
    const frames=clip.frames||1,raw=Math.floor(tick/(clip.frame_ticks||1)),mode=clip.mode||'loop',period=Math.max(1,frames*2-2);
    const frame=mode==='once'?Math.min(frames-1,raw):mode==='ping_pong'&&frames>1?(raw%period<frames?raw%period:period-raw%period):raw%frames;
    const r=A.spriteRegion(v.sheet,image,clip.sprite+frame,sheets),surface=document.createElement('canvas');surface.width=r.w;surface.height=r.h;const c=surface.getContext('2d'),tint=multiplyTint(v.tint,placement&&placement.visual_tint);
    c.drawImage(image,r.x,r.y,r.w,r.h,0,0,r.w,r.h);c.globalCompositeOperation='multiply';c.fillStyle=tint.slice(0,7);c.fillRect(0,0,r.w,r.h);c.globalCompositeOperation='destination-in';c.drawImage(image,r.x,r.y,r.w,r.h,0,0,r.w,r.h);
    const instanceX=placement&&placement.scale_x!==undefined?placement.scale_x:1,instanceY=placement&&placement.scale_y!==undefined?placement.scale_y:1;
    ctx.save();ctx.imageSmoothingEnabled=false;ctx.translate(x+((v.offset_x||0)*(mirrored?-1:1)+(placement&&placement.visual_offset_x||0))*scale,y+((v.offset_y||0)+(placement&&placement.visual_offset_y||0))*scale);ctx.rotate(((v.rotation||0)+(placement&&placement.visual_rotation||0))*(mirrored?-1:1)*Math.PI/180);ctx.scale((v.scale_x===undefined?1:v.scale_x)*instanceX*scale*(mirrored?-1:1),(v.scale_y===undefined?1:v.scale_y)*instanceY*scale);ctx.globalAlpha=parseInt(tint.slice(7,9),16)/255*Math.max(0,Math.min(1,editorOpacity));ctx.drawImage(surface,-r.w/2,-r.h/2);ctx.restore();
  }
  async function drawRoom(canvas,doc,room,tick,mirrored,instanceId){
    const entities=catalog(doc);if(!entities.types.length)return;const order=Array.isArray(doc.layout.order)?doc.layout.order:(doc.rooms||[]).map(entry=>entry.id),index=order.indexOf(room),source=doc.rooms.find(entry=>entry.id===room),roomPixels=roomWidth(source)*16,ctx=canvas.getContext('2d');let x0=finalRoomOffset(doc,index,mirrored),y0=0;
    if(doc.layout.kind==='room_graph'){
      const nodes=doc.layout.nodes||[],node=nodes.find(entry=>entry&&entry.id===instanceId&&entry.room===room)||nodes.find(entry=>entry&&entry.room===room);
      if(!node)return;const minX=Math.min(...nodes.map(entry=>entry.x)),minY=Math.min(...nodes.map(entry=>entry.y));x0=(node.x-minX)*16;y0=(node.y-minY)*16;
    }
    /* Invisible placements still run logic and collision in game. Keep a faint
     * editor-only ghost visible so authors can select, move, or erase them. */
    for(const p of A.expandPlacements(entities,doc)){if(p.x<x0||p.x>x0+roomPixels||p.y<y0||p.y>y0+roomHeight(source)*16)continue;const type=entities.types.find(t=>t.key===p.type);await drawPicture(ctx,doc,type,p.x-x0,p.y-y0,tick,p.mirrored,1,p.animation||'default',p,p.visible===false?0.28:1);}
  }
  return {catalog,create,atCell,transformAreaPlacements,paint,duplicateRoom,remove,rename,compileScript,exportFiles,importLogic,prepareForSave,label,imageFor,drawPicture,drawRoom};
}));
