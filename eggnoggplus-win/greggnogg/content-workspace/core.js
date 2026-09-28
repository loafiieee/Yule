(function (root, factory) {
  const api = factory(typeof module === 'object' && module.exports ? require('../editor-core.js') : root.GregCore);
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.EntityAuthor = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function (Greg) {
  'use strict';
  const bound = 1073741823 / 256;
  const roles = ['body', 'sensor', 'hitbox', 'hurtbox', 'solid'];
  const builtins = ['builtin:tiles', 'builtin:sprites', 'builtin:misc', 'builtin:glyphs'];
  const clone = x => JSON.parse(JSON.stringify(x));
  function validate(doc) {
    const errors = [], warnings = [];
    const fail = (path, message) => errors.push({path, message});
    function object(v, path, keys) {
      if (!v || typeof v !== 'object' || Array.isArray(v)) { fail(path, 'Expected an object.'); return false; }
      Object.keys(v).forEach(k => { if (!keys.includes(k)) fail(path + '.' + k, 'Unknown field.'); });
      return true;
    }
    function number(v, path, min, max, integer, optional) {
      if (v === undefined && optional) return;
      if (typeof v !== 'number' || !Number.isFinite(v) || v < min || v > max || (integer && !Number.isInteger(v)))
        fail(path, 'Expected ' + (integer ? 'an integer' : 'a number') + ' from ' + min + ' to ' + max + '.');
    }
    function name(v, path, qualified) {
      const pattern = qualified ? /^[a-z0-9_.-]+:[a-z0-9_.-]+$/ : /^[a-z0-9_.-]+$/;
      if (typeof v !== 'string' || v.length > 96 || !pattern.test(v)) fail(path, qualified ? 'Use owner:name (lowercase letters, numbers, _, - or .).' : 'Use a local name, up to 96 lowercase identifier characters.');
    }
    function array(v, path, max) {
      if (!Array.isArray(v)) { fail(path, 'Expected an array.'); return []; }
      if (v.length > max) fail(path, 'At most ' + max + ' entries are allowed.');
      return v.slice(0, max);
    }
    if (!object(doc, '$', ['schema', 'capacity', 'types', 'placements'])) return {errors, warnings};
    if (doc.schema !== 1 && doc.schema !== 2) fail('schema', 'Expected schema 1 or 2.');
    number(doc.capacity, 'capacity', 1, 4096, true);
    const types = array(doc.types, 'types', 4096), keys = new Set();
    if (!types.length) fail('types', 'Add at least one entity type.');
    types.forEach((t, i) => {
      const p = 'types[' + i + ']';
      if (!object(t, p, ['key', 'regions', 'visual', 'animations'])) return;
      name(t.key, p + '.key', true);
      if (keys.has(t.key)) fail(p + '.key', 'Duplicate type key.');
      keys.add(t.key);
      const ids = new Set(), regionNames = new Set();
      array(t.regions, p + '.regions', 16).forEach((r, j) => {
        const q = p + '.regions[' + j + ']';
        if (!object(r, q, ['id', 'name', 'role', 'layer', 'mask', 'x', 'y', 'width', 'height'])) return;
        number(r.id, q + '.id', 1, 4294967295, true);
        if (ids.has(r.id)) fail(q + '.id', 'Duplicate region ID.');
        ids.add(r.id);
        if (r.name !== undefined) {
          if (typeof r.name !== 'string' || r.name.length < 1 || r.name.length > 32 || !/^[a-z0-9_.-]+$/.test(r.name))
            fail(q + '.name', 'Use 1 to 32 lowercase letters, numbers, _, - or .');
          else if (regionNames.has(r.name)) fail(q + '.name', 'Duplicate detection-area name.');
          else regionNames.add(r.name);
        }
        if (!roles.includes(r.role)) fail(q + '.role', 'Choose body, sensor, hitbox or hurtbox.');
        ['layer', 'mask'].forEach(k => number(r[k], q + '.' + k, 0, 4294967295, true));
        ['x', 'y'].forEach(k => number(r[k], q + '.' + k, -bound, bound, false, true));
        ['width', 'height'].forEach(k => number(r[k], q + '.' + k, 1 / 256, bound, false));
      });
      if (t.visual !== undefined && object(t.visual, p + '.visual', ['sheet', 'sprite', 'frames', 'frame_ticks', 'mode', 'offset_x', 'offset_y', 'scale_x', 'scale_y', 'rotation', 'tint', 'layer'])) {
        const v = t.visual, q = p + '.visual';
        if (!builtins.includes(v.sheet)) {
          if (typeof v.sheet !== 'string' || v.sheet.length > 127 || v.sheet.startsWith('.') || !/^[A-Za-z0-9_.-]+\.png$/.test(v.sheet)) fail(q + '.sheet', 'Choose a built-in sheet or direct .png filename.');
          else warnings.push({path:q + '.sheet', message:'Declare this PNG in the map tileset too; this workspace exports entity content only.'});
        }
        number(v.sprite, q + '.sprite', 0, 2147483647, true);
        number(v.frames, q + '.frames', 1, 65536, true, true);
        number(v.frame_ticks, q + '.frame_ticks', 1, 1000000, true, true);
        if(v.mode!==undefined&&!['loop','once','ping_pong'].includes(v.mode))fail(q+'.mode','Choose loop, once, or ping_pong.');
        if (v.sprite + (v.frames === undefined ? 1 : v.frames) - 1 > 2147483647) fail(q, 'Animation exceeds the sprite index range.');
        ['offset_x', 'offset_y'].forEach(k => number(v[k], q + '.' + k, -bound, bound, false, true));
        ['scale_x', 'scale_y'].forEach(k => {
          number(v[k], q + '.' + k, -256, 256, false, true);
          if (v[k] !== undefined && Math.floor(Math.abs(v[k]) * 256 + 0.5) === 0) fail(q + '.' + k, 'Scale must not round to zero.');
        });
        number(t.visual.rotation,p+'.visual.rotation',-360000,360000,false,true);
        number(v.layer, q + '.layer', 0, 1, true, true);
        if (v.tint !== undefined && (typeof v.tint !== 'string' || !/^#[0-9a-f]{8}$/i.test(v.tint))) fail(q + '.tint', 'Use #RRGGBBAA.');
      }
      const animationNames=new Set();
      array(t.animations===undefined?[]:t.animations,p+'.animations',32).forEach((animation,j)=>{
        const q=p+'.animations['+j+']';if(!object(animation,q,['name','sprite','frames','frame_ticks','mode']))return;
        if(typeof animation.name!=='string'||animation.name.length<1||animation.name.length>32||!/^[a-z0-9_.-]+$/.test(animation.name))
          fail(q+'.name','Use 1 to 32 lowercase letters, numbers, _, - or .');
        if(animation.name==='default')fail(q+'.name','The name default is reserved for the main animation.');
        if(animationNames.has(animation.name))fail(q+'.name','Duplicate animation name.');animationNames.add(animation.name);
        number(animation.sprite,q+'.sprite',0,2147483647,true);
        number(animation.frames,q+'.frames',1,65536,true,true);number(animation.frame_ticks,q+'.frame_ticks',1,1000000,true,true);
        if(animation.mode!==undefined&&!['loop','once','ping_pong'].includes(animation.mode))fail(q+'.mode','Choose loop, once, or ping_pong.');
        if(animation.sprite+(animation.frames===undefined?1:animation.frames)-1>2147483647)fail(q,'Animation exceeds the sprite index range.');
      });
      if(t.animations!==undefined&&!t.visual)fail(p+'.animations','Named animations require an object picture.');
    });
    const names = new Set();
    array(doc.placements, 'placements', 4096).forEach((v, i) => {
      const p = 'placements[' + i + ']';
      if (!object(v, p, ['name', 'type', 'x', 'y', 'vx', 'vy', 'scale_x', 'scale_y', 'visual_offset_x', 'visual_offset_y', 'visual_rotation', 'visual_tint', 'draw_layer', 'animation', 'visible', 'room', 'instance', 'side'])) return;
      name(v.name, p + '.name', false); name(v.type, p + '.type', true);
      if(v.room!==undefined) {
        if(doc.schema!==2) fail(p+'.room','Room placement requires schema 2.');
        if(typeof v.room!=='string' || !v.room.length || v.room.length>96 || v.room.includes('\0')) fail(p+'.room','Expected a source room name.');
        warnings.push({path:p+'.room',message:'Room placements need an attached map to validate expansion.'});
      }
      if(v.instance!==undefined) {
        if(doc.schema!==2) fail(p+'.instance','Placed-room targeting requires schema 2.');
        if(typeof v.instance!=='string' || !v.instance.length || v.instance.length>96 || v.instance.includes('\0')) fail(p+'.instance','Expected a placed room name.');
        warnings.push({path:p+'.instance',message:'Placed-room targets need an attached map to validate.'});
      }
      if(v.instance!==undefined && v.side!==undefined) fail(p+'.side','A placed-room target does not use source/mirrored side expansion.');
      if(v.side!==undefined && (doc.schema!==2 || v.room===undefined || !['source','mirrored','both'].includes(v.side))) fail(p+'.side','Use source, mirrored or both on a schema 2 room placement.');
      if (names.has(v.name)) fail(p + '.name', 'Duplicate placement name.');
      names.add(v.name);
      if (!keys.has(v.type)) fail(p + '.type', 'This type does not exist.');
      if (v.visible !== undefined && typeof v.visible !== 'boolean') fail(p + '.visible', 'Expected true or false.');
      ['x', 'y', 'vx', 'vy'].forEach(k => number(v[k], p + '.' + k, -bound, bound, false, true));
      ['visual_offset_x','visual_offset_y'].forEach(k=>number(v[k],p+'.'+k,-bound,bound,false,true));
      number(v.visual_rotation,p+'.visual_rotation',-360000,360000,false,true);
      ['scale_x','scale_y'].forEach(k=>{number(v[k],p+'.'+k,-256,256,false,true);if(v[k]!==undefined&&Math.floor(Math.abs(v[k])*256+0.5)===0)fail(p+'.'+k,'Scale must not round to zero.');});
      if(v.visual_tint!==undefined&&(typeof v.visual_tint!=='string'||!/^#[0-9a-f]{8}$/i.test(v.visual_tint)))fail(p+'.visual_tint','Use #RRGGBBAA.');
      if(v.draw_layer!==undefined&&!['behind','front'].includes(v.draw_layer))fail(p+'.draw_layer','Choose behind or front.');
      if(v.animation!==undefined){const type=types.find(type=>type.key===v.type),names=new Set(['default',...((type&&type.animations)||[]).map(animation=>animation.name)]);if(typeof v.animation!=='string'||!names.has(v.animation))fail(p+'.animation','Choose an animation from this object design.');}
    });
    if (Array.isArray(doc.placements) && doc.placements.length > doc.capacity) fail('capacity', 'Capacity is smaller than the placement count.');
    return {errors, warnings};
  }
  function parse(text) {
    if (new TextEncoder().encode(text).length > 1048576) throw new Error('entities.json exceeds 1 MiB.');
    const doc = JSON.parse(text);
    const duplicate = Greg.findDuplicateJsonKeys(text)[0];
    if (duplicate) throw new Error('Duplicate JSON key at ' + duplicate.path + '.');
    const result = validate(doc);
    if (result.errors.length) throw new Error(result.errors[0].path + ': ' + result.errors[0].message);
    return doc;
  }
  function serialize(doc) {
    const result = validate(doc);
    if (result.errors.length) throw new Error(result.errors[0].path + ': ' + result.errors[0].message);
    const text = JSON.stringify(doc, null, 2) + '\n';
    if (new TextEncoder().encode(text).length > 1048576) throw new Error('Export exceeds 1 MiB.');
    return text;
  }
  function create() {
    return {schema:1, capacity:128, types:[{key:'demo:orb', regions:[{id:1, name:'body', role:'body', layer:1, mask:1, x:-4, y:-4, width:8, height:8}], visual:{sheet:'builtin:tiles', sprite:4}}], placements:[{name:'first_orb', type:'demo:orb', x:48, y:64, vx:0.5}]};
  }
  function renameType(doc, index, key) {
    const next = clone(doc), old = next.types[index].key;
    next.types[index].key = key;
    next.placements.forEach(p => { if (p.type === old) p.type = key; });
    if (validate(next).errors.length) throw new Error('Choose a valid, unique type key.');
    return next;
  }
  // Read only atlas declarations: importing this does not replace/edit the map.
  function parseAtlas(text) {
    if (new TextEncoder().encode(text).length > 1048576) throw Error('data.json exceeds 1 MiB.');
    const data = JSON.parse(text);
    if (Greg.findDuplicateJsonKeys(text).length) throw Error('Duplicate JSON key in data.json.');
    const t = data && data.tileset;
    if (!t || typeof t !== 'object' || Array.isArray(t)) throw Error('data.json needs a tileset object.');
    const sheets = Object.create(null);
    function geometry(value, fallback) {
      const out = {};
      for (const [key,max] of [['cell_w',512],['cell_h',512],['padding',64],['source_x',8191],['source_y',8191],['source_w',8192],['source_h',8192]]) {
        const n = value[key] === undefined ? fallback[key] : value[key];
        if (!Number.isInteger(n) || n < (key==='cell_w'||key==='cell_h'?1:0) || n > max) throw Error('Invalid tileset ' + key + '.');
        out[key] = n;
      }
      return out;
    }
    const defaults = geometry(t,{cell_w:16,cell_h:16,padding:0,source_x:0,source_y:0,source_w:0,source_h:0});
    function add(sheet,g) {
      if (typeof sheet !== 'string' || !sheet) throw Error('Missing sprite_sheet.');
      if (sheet.startsWith('builtin:')) return;
      if (sheet.length > 127 || sheet.startsWith('.') || !/^[A-Za-z0-9_.-]+\.png$/.test(sheet)) throw Error('Expected a direct PNG filename.');
      if (!sheets[sheet]) sheets[sheet] = g;
      else if (['cell_w','cell_h','padding','source_x','source_y','source_w','source_h'].some(k => sheets[sheet][k] !== g[k])) sheets[sheet].ambiguous = true;
    }
    if (t.sprite_sheet !== undefined) add(t.sprite_sheet,defaults);
    if(t.sheets!==undefined) {
      if(!Array.isArray(t.sheets)||t.sheets.length>16)throw Error('Expected at most 16 picture sheets.');
      t.sheets.forEach(entry=>{if(!entry||typeof entry!=='object'||Array.isArray(entry))throw Error('Invalid picture sheet.');add(entry.sprite_sheet,geometry(entry,{cell_w:16,cell_h:16,padding:0,source_x:0,source_y:0,source_w:0,source_h:0}));});
    }
    if (t.tiles !== undefined && !Array.isArray(t.tiles)) throw Error('tileset.tiles must be an array.');
    (t.tiles || []).forEach(tile => {
      if (!tile || typeof tile !== 'object' || Array.isArray(tile)) throw Error('Invalid tileset tile.');
      add(tile.sprite_sheet === undefined ? t.sprite_sheet : tile.sprite_sheet,geometry(tile,defaults));
    });
    return sheets;
  }
  function spriteRegion(sheet,image,index,sheets) {
    if (!Number.isInteger(index) || index < 0) throw Error('Invalid sprite index.');
    if (sheet === 'builtin:glyphs') {
      const r = {x:1+(index%16)*9,y:1+Math.floor(index/16)*9,w:8,h:8};
      if (index >= 256 || r.x+8 > image.width || r.y+8 > image.height) throw Error('Sprite is outside the glyph sheet.');
      return r;
    }
    const g = builtins.includes(sheet) ? {cell_w:16,cell_h:16,padding:0,source_x:0,source_y:0,source_w:0,source_h:0} : sheets && sheets[sheet];
    if (!g) throw Error('Import data.json to resolve the grid for ' + sheet + '.');
    if (g.ambiguous) throw Error(sheet + ' has multiple grid declarations; entities need one unambiguous sheet.');
    const {cell_w:w,cell_h:h,padding:p}=g,x=g.source_x||0,y=g.source_y||0;
    const regionW=g.source_w||image.width-x,regionH=g.source_h||image.height-y;
    if(x>=image.width||y>=image.height||regionW>image.width-x||regionH>image.height-y)throw Error('PNG source rectangle is outside the image.');
    const columns=(regionW+p)/(w+p), rows=(regionH+p)/(h+p);
    if (!Number.isInteger(columns) || !Number.isInteger(rows) || columns < 1 || rows < 1) throw Error('PNG source rectangle dimensions do not match the declared grid.');
    if (columns*rows > 8192 || index >= columns*rows) throw Error('Sprite is outside the declared grid or sheet limit.');
    return {x:x+(index%columns)*(w+p),y:y+Math.floor(index/columns)*(h+p),w,h};
  }
  function expandPlacements(doc,rooms) {
    const result=[],names=new Set(), q=v=>{v=v===undefined?0:v;return (v<0?-Math.floor(-v*256+0.5):Math.floor(v*256+0.5))/256;};
    const mapDocument=rooms&&!Array.isArray(rooms)?(rooms.document||rooms):null;
    const roomList=mapDocument?(mapDocument.rooms||[]):(rooms||[]);
    const specs=roomList.map(room=>{
      if(typeof room==='string')return {id:room,width:33,height:12};
      const grid=room&&Array.isArray(room.grid)?room.grid:[];
      const first=grid.length?(Array.isArray(grid[0])?grid[0]:String(grid[0]||'')):[];
      return {id:room&&room.id,width:first.length||33,height:grid.length||12};
    });
    const ids=specs.map(room=>room.id);
    const graph=mapDocument&&mapDocument.layout&&mapDocument.layout.kind==='room_graph'?mapDocument.layout:null;
    const graphNodes=graph&&Array.isArray(graph.nodes)?graph.nodes:[];
    const graphBounds=graphNodes.reduce((bounds,node)=>{
      const index=ids.indexOf(node&&node.room),spec=index>=0?specs[index]:null;
      if(!spec||!Number.isInteger(node.x)||!Number.isInteger(node.y))return bounds;
      bounds.minX=Math.min(bounds.minX,node.x);bounds.minY=Math.min(bounds.minY,node.y);
      return bounds;
    },{minX:Infinity,minY:Infinity});
    if(graph&&(!graphNodes.length||!Number.isFinite(graphBounds.minX)||!Number.isFinite(graphBounds.minY)))throw Error('Invalid or empty room_graph layout.');
    const roomStart=(index,mirrored)=>{
      let start=0;
      if(!mirrored){for(let i=index+1;i<specs.length;i++)start+=specs[i].width;}
      else{for(let i=1;i<specs.length;i++)start+=specs[i].width;start+=specs[0].width;for(let i=1;i<index;i++)start+=specs[i].width;}
      return start*16;
    };
    for(const p of doc.placements) {
      const index=p.room===undefined?-1:ids.indexOf(p.room);
      if(p.room!==undefined && index<0) throw Error('Unknown room or no attached map: '+p.room);
      const x=q(p.x),y=q(p.y),side=p.side||'both';
      if(graph) {
        if(p.room!==undefined&&!p.instance)throw Error('Room-graph placements must name a placed room instance.');
        if(p.instance!==undefined) {
          const node=graphNodes.find(entry=>entry&&entry.id===p.instance);
          if(!node)throw Error('Unknown placed room instance: '+p.instance);
          const sourceIndex=ids.indexOf(node.room),spec=sourceIndex>=0?specs[sourceIndex]:null;
          if(!spec)throw Error('Placed room '+p.instance+' references an unknown room design.');
          if(p.room!==undefined&&p.room!==node.room)throw Error('Placement room does not match placed room instance '+p.instance+'.');
          if(p.side!==undefined)throw Error('Placed-room targets do not use source/mirrored side expansion.');
          const roomWidth=spec.width*16,roomHeight=spec.height*16,mirrored=node.mirrorX===true||node.mirror_x===true;
          if(x<0||x>roomWidth||y<0||y>roomHeight)throw Error('Room coordinates must be within '+roomWidth+' by '+roomHeight+' pixels.');
          const name=p.name;
          if(name.length>96||names.has(name))throw Error('Duplicate or too-long expanded placement name: '+name);
          names.add(name);
          result.push({...p,name,authoredName:p.name,mirrored,x:(node.x-graphBounds.minX)*16+(mirrored?roomWidth-x:x),y:(node.y-graphBounds.minY)*16+y,vx:q(p.vx)*(mirrored?-1:1),vy:q(p.vy)});
          if(result.length>doc.capacity)throw Error('Expanded placements exceed capacity.');
          continue;
        }
      }
      const roomWidth=index>=0?specs[index].width*16:528,roomHeight=index>=0?specs[index].height*16:192;
      if(index>=0 && (x<0||x>roomWidth||y<0||y>roomHeight)) throw Error('Room coordinates must be within '+roomWidth+' by '+roomHeight+' pixels.');
      if(index===0 && side==='mirrored') throw Error('The center room has no mirrored copy.');
      for(const mirrored of [false,true]) {
        if(index<0 && mirrored) continue;
        if(index>=0 && ((mirrored && (!index||side==='source')) || (!mirrored && side==='mirrored'))) continue;
        const name=p.name+(mirrored?'.mirror':'');
        if(name.length>96 || names.has(name)) throw Error('Duplicate or too-long expanded placement name: '+name);
        names.add(name);
        result.push({...p,name,authoredName:p.name,mirrored,x:index<0?x:roomStart(index,mirrored)+(mirrored?roomWidth-x:x),y,vx:q(p.vx)*(mirrored?-1:1),vy:q(p.vy)});
        if(result.length>doc.capacity) throw Error('Expanded placements exceed capacity.');
      }
    }
    return result;
  }
  function openMap(files) {
    const names=Object.keys(files), enc=new TextDecoder('utf-8',{fatal:true});
    if (names.length > 4096) throw Error('Map folder exceeds 4096 files.');
    let total=0;
    names.forEach(name => {
      if (name.includes('\\') || name.includes(':') || name.split('/').some(p=>!p || p==='.' || p==='..')) throw Error('Unsafe map path: '+name);
      if (!(files[name] instanceof Uint8Array)) throw Error('Expected file bytes.');
      total+=files[name].length;
    });
    if (total > 128*1024*1024) throw Error('Map folder exceeds 128 MiB.');
    const pairs=names.filter(n=>n.endsWith('data.json') && (n==='data.json'||n.endsWith('/data.json')) && files[n.slice(0,-9)+'data.map']);
    if (pairs.length!==1) throw Error('Select a folder containing exactly one data.json/data.map pair.');
    const prefix=pairs[0].slice(0,-9), direct=Object.create(null), retained=Object.create(null), folded=new Set();
    names.filter(n=>n.startsWith(prefix)).forEach(n=>{
      const local=n.slice(prefix.length), lower=local.toLowerCase();
      if (folded.has(lower)) throw Error('Duplicate Windows filename: '+local);
      if (['data.json','data.map','map.lua','entities.json'].includes(lower) && local!==lower) throw Error('Use lowercase package filename: '+local);
      folded.add(lower);retained[local]=files[n].slice();
      if (!local.includes('/')) direct[local]=retained[local];
    });
    const parsed=Greg.parsePackageFiles(direct);
    if (!parsed.valid) throw Error(parsed.errors[0].message);
    if (parsed.formatVersion!==2) {
      const upgraded=Greg.exportProjectFiles(Greg.upgradeToV2(parsed.document));
      Object.entries(upgraded).forEach(([name,value])=>retained[name]=typeof value==='string'?new TextEncoder().encode(value):value);
      return openMap(retained);
    }
    const data=JSON.parse(enc.decode(direct['data.json']));
    const atlasText=data.tileset===undefined?'':enc.decode(direct['data.json']);
    const sheets=atlasText?parseAtlas(atlasText):Object.create(null);
    // Unchanged Lua retains original bytes, including non-UTF8 comments.
    const script=direct['map.lua']?new TextDecoder().decode(direct['map.lua']):'';
    const doc=direct['entities.json']?parse(enc.decode(direct['entities.json'])):create();
    /* room_graph intentionally has no serialized layout.order. Object design
     * works with source-room designs, so derive the list from parsed rooms for
     * both layout kinds instead of assuming the legacy mirrored layout. */
    const sourceRooms=Array.isArray(parsed.document.rooms)?parsed.document.rooms.map(room=>room.id):[];
    const result={files:retained,folder:Greg.normalizeFolderId(parsed.document.id),script,doc,atlasText,sheets,rooms:sourceRooms,document:parsed.document};
    validateMapEntities(result,doc);
    return result;
  }
  function starter(kind,key) {
    const type={key,regions:[],visual:{sheet:'builtin:tiles',sprite:4}};
    let script='';
    if(kind==='custom') delete type.visual;
    if(kind==='pad') {
      type.regions=[{id:1,name:'trigger',role:'sensor',layer:1,mask:1,x:-16,y:-3,width:32,height:6}];
      Object.assign(type.visual,{scale_x:2,scale_y:0.375,tint:'#80C0FFFF'});
      script='-- Greggnogg object begin '+key+'\nentity.on_update('+JSON.stringify(key)+', function(handle)\n'+
        '    local launch_speed = 4\n    local players = map.players()\n'+
        '    for _, area in ipairs(entity.regions(handle)) do\n        if area.role == "sensor" then\n'+
        '            for _, player in ipairs(players) do\n                local r = player.contact_radius\n                local feet = player.y + r\n'+
        '                if player.vy >= 0 and player.x + r > area.world_x and player.x - r < area.world_x + area.width and feet >= area.world_y and feet < area.world_y + area.height then\n'+
        '                    map.set_player_velocity(player.player, player.vx, -launch_speed)\n                    player.vy = -launch_speed\n'+
        '                end\n            end\n        end\n    end\nend)\n-- Greggnogg object end '+key+'\n';
    }
    return {type,script,vx:kind==='moving'?0.5:0};
  }
  function starterBlock(script,key) {
    const start=script.indexOf('-- Greggnogg object begin '+key+'\n'), marker='-- Greggnogg object end '+key+'\n';
    if(start<0)return null;const end=script.indexOf(marker,start);return end<0?null:{start,end:end+marker.length,text:script.slice(start,end+marker.length)};
  }
  function removeStarter(script,key) {const b=starterBlock(script,key);return b?script.slice(0,b.start)+script.slice(b.end):script;}
  function renameStarter(script,old,key) {const b=starterBlock(script,old);return b?script.slice(0,b.start)+b.text.split(old).join(key)+script.slice(b.end):script;}
  function addPicture(map,name,bytes) {
    if(!map)throw Error('Choose a map first so the picture can be saved with it.');
    if(!(bytes instanceof Uint8Array)||bytes.length<24||bytes.length>64*1024*1024)throw Error('Choose a PNG up to 64 MiB.');
    const signature=[137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82];
    if(signature.some((n,i)=>bytes[i]!==n))throw Error('Choose a PNG image.');
    const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),w=v.getUint32(16),h=v.getUint32(20);
    if(!w||!h||w>8192||h>8192)throw Error('Use a picture at most 8192 by 8192 pixels.');
    let base=String(name).replace(/\.png$/i,'').toLowerCase().replace(/[^a-z0-9_-]+/g,'_').slice(0,100)||'picture',filename=base+'.png',i=2;
    const used=new Set(Object.keys(map.files).map(n=>n.toLowerCase()));while(used.has(filename))filename=base+'_'+i+++'.png';
    const data=JSON.parse(new TextDecoder().decode(map.files['data.json']));data.tileset=data.tileset||{tiles:[]};data.tileset.sheets=data.tileset.sheets||[];
    if(data.tileset.sheets.length>=16)throw Error('This map already has 16 picture sheets. Add pictures to an existing sheet in advanced settings.');
    const cellW=Math.min(w,512),cellH=Math.min(h,512);
    data.tileset.sheets.push({sprite_sheet:filename,cell_w:cellW,cell_h:cellH,padding:0,source_x:0,source_y:0,source_w:cellW,source_h:cellH});
    const files=Object.assign(Object.create(null),map.files);files[filename]=bytes.slice();files['data.json']=new TextEncoder().encode(JSON.stringify(data,null,2)+'\n');
    return {map:openMap(files),filename};
  }
  function fromMapDocument(document) {
    const source=document || Greg.createDefaultDocument();
    const prepared=source.format===Greg.FORMAT_V2?source:Greg.upgradeToV2(source);
    const files=Greg.exportProjectFiles(prepared);
    Object.keys(files).forEach(name=>{if(typeof files[name]==='string')files[name]=new TextEncoder().encode(files[name]);});
    return openMap(files);
  }
  function validateMapEntities(map,doc) {
    serialize(doc);
    expandPlacements(doc,map.document || map.rooms);
    doc.types.forEach(t=>{
      if (!t.visual || builtins.includes(t.visual.sheet)) return;
      const v=t.visual, bytes=map.files[v.sheet];
      if (!bytes || bytes.length<24) throw Error('Missing PNG for '+t.key+': '+v.sheet);
      const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
      spriteRegion(v.sheet,{width:view.getUint32(16),height:view.getUint32(20)},v.sprite+(v.frames||1)-1,map.sheets);
    });
  }
  function mapZip(map,doc,script) {
    if (!map) throw Error('Open a V2 map folder first.');
    validateMapEntities(map,doc);
    if (new TextEncoder().encode(script).length>262144 || script.includes('\0')) throw Error('map.lua exceeds its limit or contains a NUL byte.');
    const files=Object.assign(Object.create(null),map.files);
    files['entities.json']=serialize(doc);
    if (script!==map.script || !files['map.lua']) files['map.lua']=script;
    const entries=Object.create(null);
    Object.keys(files).sort().forEach(name=>entries[map.folder+'/'+name]=files[name]);
    return Greg.buildStoredZip(entries);
  }
  return {validate, parse, serialize, create, clone, renameType, roles, builtins, parseAtlas, spriteRegion, openMap, mapZip, expandPlacements, fromMapDocument, starter, starterBlock, removeStarter, renameStarter, addPicture};
}));
