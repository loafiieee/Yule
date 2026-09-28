(function(root,factory){
  if(typeof module==='object'&&module.exports){const B=require('./vendor/blockly/blockly_compressed.js');require('./vendor/blockly/blocks_compressed.js');B.setLocale(require('./vendor/blockly/msg/en.js'));module.exports=factory(B,require('./vendor/blockly/lua_compressed.js').luaGenerator);}
  else root.GregLogicBlocks=factory(root.Blockly,root.lua.luaGenerator);
}(typeof globalThis!=='undefined'?globalThis:this,function(B,G){
  'use strict';
  let objectTypes=[],tileTypes=[],placements=[],roomConnections=[];
  const validType=key=>typeof key==='string'&&key.length<=96&&/^[a-z][a-z0-9_.-]*:[a-z][a-z0-9_.-]*$/.test(key);
  function objectOptions(selected){
    const options=objectTypes.map(type=>[type.label,type.key]);
    if(validType(selected)&&!objectTypes.some(type=>type.key===selected))options.push(['Missing object: '+selected,selected]);
    return options.length?options:[['Create an object in Objects first','__none__']];
  }
  class ObjectField extends B.FieldDropdown {
    constructor(){super(function(){return objectOptions(this.pendingType||this.getValue());});}
    doClassValidation_(value){
      if(value!=='__none__'&&!validType(value))return null;
      this.pendingType=value;this.getOptions(false);
      return super.doClassValidation_(value);
    }
    static fromJson(){return new ObjectField();}
  }
  B.fieldRegistry.register('field_greg_object',ObjectField);
  function setObjectTypes(types){
    objectTypes=(types||[]).filter(type=>validType(type.key)).map(type=>({
      key:type.key,label:type.label||type.key,
      regions:(type.regions||[]).filter(region=>region&&typeof region.name==='string'&&/^[a-z0-9_.-]{1,32}$/.test(region.name)).map(region=>({name:region.name,role:region.role})),
      animations:(type.animations||[]).filter(animation=>animation&&typeof animation.name==='string'&&/^[a-z0-9_.-]{1,32}$/.test(animation.name)&&animation.name!=='default').map(animation=>animation.name)
    }));
  }
  const validPlacement=name=>typeof name==='string'&&name.length<=96&&/^[a-z0-9_.-]+$/.test(name);
  function placementOptions(selected){
    const options=placements.map(placement=>[placement.label,placement.name]);
    if(validPlacement(selected)&&!placements.some(placement=>placement.name===selected))options.push(['Missing object: '+selected,selected]);
    return options.length?options:[['Place an object on the map first','__none__']];
  }
  class PlacementField extends B.FieldDropdown {
    constructor(){super(function(){return placementOptions(this.pendingPlacement||this.getValue());});}
    doClassValidation_(value){if(value!=='__none__'&&!validPlacement(value))return null;this.pendingPlacement=value;this.getOptions(false);return super.doClassValidation_(value);}
    static fromJson(){return new PlacementField();}
  }
  B.fieldRegistry.register('field_greg_placement',PlacementField);
  function setPlacements(values){
    const seen=new Set();placements=[];
    for(const placement of values||[]){
      const name=typeof placement==='string'?placement:placement&&placement.name;
      if(!validPlacement(name)||seen.has(name))continue;
      seen.add(name);placements.push({name,label:(placement&&placement.label)||name});
    }
  }
  const regionRoles=[['any area','any'],['sensor','sensor'],['body','body'],['attack area','hitbox'],['damage receiver','hurtbox'],['solid area','solid']];
  const validRegion=value=>regionRoles.some(option=>option[1]===value)||(typeof value==='string'&&/^name:[a-z0-9_.-]{1,32}$/.test(value));
  function surroundingObjectType(block){
    let current=block;
    while(current){
      if(['greg_player_sprite','greg_player_sprite_is'].includes(current.type))return current.getFieldValue('TYPE');
      if(current.type==='greg_entity_event'||current.type==='greg_entities')return current.getFieldValue('TYPE');
      current=current.getSurroundParent?current.getSurroundParent():null;
    }
    return null;
  }
  function regionOptions(field){
    const selected=field.pendingRegion||field.getValue(),type=objectTypes.find(item=>item.key===surroundingObjectType(field.getSourceBlock&&field.getSourceBlock()));
    const names=[],seen=new Set();
    for(const region of type?type.regions:objectTypes.flatMap(item=>item.regions))if(!seen.has(region.name)){seen.add(region.name);names.push(['named area: '+region.name,'name:'+region.name]);}
    const options=regionRoles.concat(names);
    if(validRegion(selected)&&!options.some(option=>option[1]===selected))options.push(['Missing area: '+selected.slice(5),selected]);
    return options;
  }
  class RegionField extends B.FieldDropdown {
    constructor(){super(function(){return regionOptions(this);});}
    doClassValidation_(value){if(!validRegion(value))return null;this.pendingRegion=value;this.getOptions(false);return super.doClassValidation_(value);}
    static fromJson(){return new RegionField();}
  }
  B.fieldRegistry.register('field_greg_region',RegionField);
  function animationOptions(field){const selected=field.pendingAnimation||field.getValue(),type=objectTypes.find(item=>item.key===surroundingObjectType(field.getSourceBlock&&field.getSourceBlock()));const options=[['default animation','default']].concat((type?type.animations:objectTypes.flatMap(item=>item.animations)).map(name=>[name,name]));if(typeof selected==='string'&&/^[a-z0-9_.-]{1,32}$/.test(selected)&&!options.some(option=>option[1]===selected))options.push(['Missing animation: '+selected,selected]);return options;}
  class AnimationField extends B.FieldDropdown {constructor(){super(function(){return animationOptions(this);});}doClassValidation_(value){if(typeof value!=='string'||!/^[a-z0-9_.-]{1,32}$/.test(value))return null;this.pendingAnimation=value;this.getOptions(false);return super.doClassValidation_(value);}static fromJson(){return new AnimationField();}}
  B.fieldRegistry.register('field_greg_animation',AnimationField);
  const validTile=symbol=>typeof symbol==='string'&&symbol.length===1&&symbol.charCodeAt(0)>=0x20&&symbol.charCodeAt(0)<=0x7e;
  const validTarget=value=>typeof value==='string'&&((value.startsWith('tile:')&&validTile(value.slice(5)))||(value.startsWith('object:')&&validType(value.slice(7))));
  function setTileTypes(types){
    const bySymbol=new Map();
    for(const type of types||[])if(validTile(type.symbol))bySymbol.set(type.symbol,{symbol:type.symbol,label:type.label||type.name||type.symbol});
    tileTypes=Array.from(bySymbol.values());
  }
  function targetOptions(selected){
    const options=tileTypes.map(type=>['Tile '+(type.symbol===' '?'(space)':type.symbol)+' — '+type.label,'tile:'+type.symbol])
      .concat(objectTypes.map(type=>['Object — '+type.label,'object:'+type.key]));
    if(validTarget(selected)&&!options.some(option=>option[1]===selected))options.push(['Missing target: '+selected,selected]);
    return options.length?options:[['Add a tile or object first','__none__']];
  }
  class TargetField extends B.FieldDropdown {
    constructor(){super(function(){return targetOptions(this.pendingTarget||this.getValue());});}
    doClassValidation_(value){
      if(value!=='__none__'&&!validTarget(value))return null;
      this.pendingTarget=value;this.getOptions(false);
      return super.doClassValidation_(value);
    }
    static fromJson(){return new TargetField();}
  }
  B.fieldRegistry.register('field_greg_target',TargetField);
  const validTimerName=name=>typeof name==='string'&&name.length>0&&new TextEncoder().encode(name).length<=31&&!name.includes('\0');
  function timerOptions(field){
    const selected=field.pendingTimer||field.getValue(),block=field.getSourceBlock&&field.getSourceBlock(),workspace=block&&block.workspace,seen=new Set(),options=[];
    for(const candidate of workspace?workspace.getTopBlocks(false):[])if(candidate.type==='greg_timer_event'){
      const name=String(candidate.getFieldValue('NAME')||'');if(validTimerName(name)&&!seen.has(name)){seen.add(name);options.push([name,name]);}
    }
    if(validTimerName(selected)&&!seen.has(selected))options.push(['Missing timer: '+selected,selected]);
    return options.length?options:[['Add a timer-finished event first','__none__']];
  }
  class TimerField extends B.FieldDropdown {
    constructor(){super(function(){return timerOptions(this);});}
    doClassValidation_(value){if(value!=='__none__'&&!validTimerName(value))return null;this.pendingTimer=value;this.getOptions(false);return super.doClassValidation_(value);}
    static fromJson(){return new TimerField();}
  }
  B.fieldRegistry.register('field_greg_timer',TimerField);
  const validRoomConnection=value=>typeof value==='string'&&/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(value);
  function roomConnectionValue(value){
    const exact=roomConnections.find(connection=>connection.value===value);
    if(exact)return exact.value;
    if(/^[1-9]\d*$/.test(String(value))){const legacy=roomConnections.find(connection=>connection.index===Number(value));if(legacy)return legacy.value;}
    return value;
  }
  function roomConnectionNumber(value){const connection=roomConnections.find(item=>item.value===value);if(!connection)throw Error('Choose a room doorway that still exists.');return connection.index;}
  function roomConnectionOptions(selected){
    const options=roomConnections.map(connection=>[connection.label,connection.value]);
    if(validRoomConnection(selected)&&!roomConnections.some(connection=>connection.value===selected))options.push(['Missing door: '+selected,selected]);
    return options.length?options:[['Add a doorway in Rooms first','__none__']];
  }
  class RoomConnectionField extends B.FieldDropdown {
    constructor(){super(function(){return roomConnectionOptions(this.pendingConnection||this.getValue());});}
    doClassValidation_(value){value=roomConnectionValue(value);if(value!=='__none__'&&!validRoomConnection(value))return null;this.pendingConnection=value;this.getOptions(false);return super.doClassValidation_(value);}
    static fromJson(){return new RoomConnectionField();}
  }
  B.fieldRegistry.register('field_greg_room_connection',RoomConnectionField);
  function setRoomConnections(values){
    const seen=new Set();roomConnections=[];
    for(const connection of values||[]){
      const index=Number(connection&&connection.index),value=String(connection&&connection.id!==undefined?connection.id:connection);
      if(!Number.isInteger(index)||index<1||index>256||!validRoomConnection(value)||seen.has(value))continue;
      seen.add(value);roomConnections.push({value,index,label:String(connection&&connection.label||('Door '+index))});
    }
  }
  const definitions=[
    {type:'greg_tick',message0:'every game tick %1 %2',args0:[{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_timer_event',message0:'when timer %1 finishes %2 %3',args0:[{type:'field_input',name:'NAME',text:'timer'},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_player_action_event',message0:'when %1 %2 %3 %4 %5',args0:[{type:'field_dropdown',name:'PLAYER',options:[['any player','any'],['player 1','1'],['player 2','2']]},{type:'field_dropdown',name:'EDGE',options:[['presses','pressed'],['releases','released'],['holds','input']]},{type:'field_dropdown',name:'ACTION',options:[['jump','jump'],['attack','attack'],['left','left'],['right','right'],['up','up'],['down','down'],['menu','menu']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_player_sprite_event',message0:'when %1 custom picture animation finishes %2 %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['any player','any'],['player 1','1'],['player 2','2']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_player_lifecycle_event',message0:'when %1 %2 %3 %4',args0:[{type:'field_dropdown',name:'PLAYER',options:[['any player','any'],['player 1','1'],['player 2','2']]},{type:'field_dropdown',name:'EVENT',options:[['spawns','spawned'],['respawns','respawned'],['enters a room','room_changed'],['leaves a room','room_left']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_player_health_event',message0:'when %1 %2 %3 %4',args0:[{type:'field_dropdown',name:'PLAYER',options:[['any player','any'],['player 1','1'],['player 2','2']]},{type:'field_dropdown',name:'EVENT',options:[['takes damage','damage'],['health changes','health_changed'],['is defeated','defeated']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:0},
    {type:'greg_entity_event',message0:'when object %1 %2 %3 %4',args0:[{type:'field_greg_object',name:'TYPE'},{type:'field_dropdown',name:'EVENT',options:[['updates','update'],['is created','spawn'],['finishes its animation','animation_finish'],['touches another object','contact'],['touches a player','player_contact'],['checks incoming damage','damage_filter'],['takes damage','damage'],['is defeated','defeated'],['receives a signal','signal'],['is removed','remove']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_players',message0:'for each active player %1 %2',args0:[{type:'input_dummy'},{type:'input_statement',name:'DO'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_value',message0:'player %1 %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]},{type:'field_dropdown',name:'PROPERTY',options:[['number','player'],['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['facing (-1 left, 1 right)','facing'],['health','health'],['maximum health','max_health'],['contact radius','contact_radius'],['native state number','native_state'],['room number','room'],['previous room number','previous_room'],['collision flags','collision_flags'],['previous collision flags','previous_collision_flags'],['skin palette','skin_palette'],['clothing palette','clothing_palette'],['custom picture cell','custom_sprite_cell'],['custom picture animation tick','custom_sprite_tick'],['custom picture frame','custom_sprite_frame'],['custom picture frame count','custom_sprite_frames']]}],output:'Number',colour:210},
    {type:'greg_player_text',message0:'player %1 %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]},{type:'field_dropdown',name:'PROPERTY',options:[['custom picture object type','custom_sprite_type'],['custom picture animation','custom_sprite_animation'],['skin color','skin_tint'],['clothing / armor color','clothing_tint']]}],output:'String',colour:210},
    {type:'greg_player_condition',message0:'player %1 %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]},{type:'field_dropdown',name:'PROPERTY',options:[['just spawned','spawned'],['just respawned','respawned'],['just changed rooms','room_changed'],['has a sword','has_sword'],['is on the ground','grounded'],['was on the ground last tick','previously_grounded'],['native body is visible','body_visible'],['custom picture is visible','custom_sprite_visible'],['custom picture animation finished','custom_sprite_finished'],['custom picture will restore when finished','custom_sprite_restore'],['just pressed jump','pressed.jump'],['is holding jump','input.jump'],['just released jump','released.jump'],['just pressed attack','pressed.attack'],['is holding attack','input.attack'],['is holding left','input.left'],['is holding right','input.right'],['is holding up','input.up'],['is holding down','input.down']]}],output:'Boolean',colour:210},
    {type:'greg_player_action',message0:'player %1 %2 %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]},{type:'field_dropdown',name:'EDGE',options:[['holds','input'],['just pressed','pressed'],['just released','released']]},{type:'field_dropdown',name:'ACTION',options:[['jump','jump'],['attack','attack'],['left','left'],['right','right'],['up','up'],['down','down'],['menu','menu']]}],output:'Boolean',colour:210},
    {type:'greg_player_reference',message0:'player %1',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]}],output:'Player',colour:210},
    {type:'greg_player_reference_exists',message0:'player %1 is active',args0:[{type:'input_value',name:'PLAYER',check:'Player'}],output:'Boolean',colour:120},
    {type:'greg_player_reference_number',message0:'player %1 %2',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'field_dropdown',name:'PROPERTY',options:[['number','player'],['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['facing (-1 left, 1 right)','facing'],['health','health'],['maximum health','max_health'],['contact radius','contact_radius'],['native state number','native_state'],['room number','room'],['previous room number','previous_room'],['collision flags','collision_flags'],['previous collision flags','previous_collision_flags'],['skin palette','skin_palette'],['clothing palette','clothing_palette'],['custom picture cell','custom_sprite_cell'],['custom picture animation tick','custom_sprite_tick'],['custom picture frame','custom_sprite_frame'],['custom picture frame count','custom_sprite_frames']]}],output:'Number',colour:210},
    {type:'greg_player_reference_text',message0:'player %1 %2',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'field_dropdown',name:'PROPERTY',options:[['custom picture object type','custom_sprite_type'],['custom picture animation','custom_sprite_animation'],['skin color','skin_tint'],['clothing / armor color','clothing_tint']]}],output:'String',colour:210},
    {type:'greg_player_reference_condition',message0:'player %1 %2',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'field_dropdown',name:'PROPERTY',options:[['just spawned','spawned'],['just respawned','respawned'],['just changed rooms','room_changed'],['has a sword','has_sword'],['is on the ground','grounded'],['was on the ground last tick','previously_grounded'],['native body is visible','body_visible'],['custom picture is visible','custom_sprite_visible'],['custom picture animation finished','custom_sprite_finished'],['custom picture will restore when finished','custom_sprite_restore'],['just pressed jump','pressed.jump'],['is holding jump','input.jump'],['just released jump','released.jump'],['just pressed attack','pressed.attack'],['is holding attack','input.attack'],['is holding left','input.left'],['is holding right','input.right'],['is holding up','input.up'],['is holding down','input.down']]}],output:'Boolean',colour:210},
    {type:'greg_player_reference_velocity',message0:'set player %1 velocity x %2 y %3',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_reference_position',message0:'move player %1 to x %2 y %3',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_reference_damage',message0:'deal %2 damage to player %1',args0:[{type:'input_value',name:'PLAYER',check:'Player'},{type:'input_value',name:'AMOUNT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_reference_defeat',message0:'defeat player %1',args0:[{type:'input_value',name:'PLAYER',check:'Player'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_touching',message0:'player %1 touches this object %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]},{type:'field_greg_region',name:'ROLE'}],output:'Boolean',colour:120},
    {type:'greg_player_hit_object',message0:'player %1 landed an attack on this object this tick',args0:[{type:'field_dropdown',name:'PLAYER',options:[['this player','current'],['1','1'],['2','2']]}],output:'Boolean',colour:120},
    {type:'greg_velocity',message0:'set player %1 velocity x %2 y %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_position',message0:'move player %1 to x %2 y %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_tint',message0:'set player %1 %2 color to %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'CHANNEL',options:[['skin','skin_tint'],['clothing / armor','clothing_tint']]},{type:'field_input',name:'COLOR',text:'#FFFFFFFF'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_tint_reset',message0:'reset player %1 %2 color',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'CHANNEL',options:[['skin','skin_tint'],['clothing / armor','clothing_tint']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_visibility',message0:'set player %1 body visible to %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'VISIBLE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_presentation_reset',message0:'reset player %1 appearance',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite',message0:'show player %1 as object picture %2 animation %3 %4 %5',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_greg_object',name:'TYPE'},{type:'field_greg_animation',name:'ANIMATION'},{type:'field_dropdown',name:'RESTART',options:[['continue if unchanged','continue'],['restart now','restart']]},{type:'field_dropdown',name:'RESTORE',options:[['keep showing it','keep'],['restore normal when finished','restore']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_is',message0:'player %1 picture is %2 animation %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_greg_object',name:'TYPE'},{type:'field_greg_animation',name:'ANIMATION'}],output:'Boolean',colour:120},
    {type:'greg_player_sprite_finished',message0:'player %1 custom picture animation finished',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],output:'Boolean',colour:120},
    {type:'greg_player_sprite_number',message0:'set player %1 picture %2 to %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'PROPERTY',options:[['width scale','scale_x'],['height scale','scale_y'],['offset x','offset_x'],['offset y','offset_y'],['rotation','rotation'],['animation speed','animation_speed']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_tint',message0:'set player %1 picture color to %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_input',name:'COLOR',text:'#FFFFFFFF'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_layer',message0:'draw player %1 picture %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'LAYER',options:[['object default','authored'],['behind players','behind'],['in front of players','front']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_mirrored',message0:'set player %1 picture flipped horizontally to %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_visibility',message0:'set player %1 custom picture visible to %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_transform_reset',message0:'reset player %1 picture size, position and style',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_sprite_clear',message0:'restore player %1 normal sprite',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_defeat',message0:'defeat player %1',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_health_get',message0:'player %1 %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]}],output:'Number',colour:0},
    {type:'greg_player_health_enabled',message0:'player %1 has health enabled',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],output:'Boolean',colour:120},
    {type:'greg_player_health_enable',message0:'enable health for player %1 maximum %2 current %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'MAXIMUM',check:'Number'},{type:'input_value',name:'CURRENT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_health_disable',message0:'disable health for player %1',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_invulnerable_set',message0:'set player %1 invulnerable to %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_invulnerable',message0:'player %1 is invulnerable',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],output:'Boolean',colour:120},
    {type:'greg_player_health_set',message0:'set player %1 %2 to %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_health_change',message0:'change player %1 %2 by %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]},{type:'input_value',name:'AMOUNT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_heal',message0:'heal player %1 by %2',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'AMOUNT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_player_damage',message0:'deal %1 damage to player %2 from %3',args0:[{type:'input_value',name:'AMOUNT',check:'Number'},{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'field_dropdown',name:'SOURCE',options:[['no source','none'],['this object','this']]}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_timer_start',message0:'start timer %1 after %2 ticks',args0:[{type:'field_greg_timer',name:'NAME'},{type:'input_value',name:'DELAY',check:'Number'}],previousStatement:null,nextStatement:null,colour:35},
    {type:'greg_timer_repeat',message0:'start repeating timer %1 after %2 ticks then every %3 ticks',args0:[{type:'field_greg_timer',name:'NAME'},{type:'input_value',name:'DELAY',check:'Number'},{type:'input_value',name:'INTERVAL',check:'Number'}],previousStatement:null,nextStatement:null,colour:35},
    {type:'greg_timer_cancel',message0:'cancel timer %1',args0:[{type:'field_greg_timer',name:'NAME'}],previousStatement:null,nextStatement:null,colour:35},
    {type:'greg_timer_remaining',message0:'timer %1 ticks remaining',args0:[{type:'field_greg_timer',name:'NAME'}],output:'Number',colour:35},
    {type:'greg_timer_active',message0:'timer %1 is active',args0:[{type:'field_greg_timer',name:'NAME'}],output:'Boolean',colour:35},
    {type:'greg_camera_point',message0:'show camera at x %1 y %2 zoom %3',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'input_value',name:'ZOOM',check:'Number'}],previousStatement:null,nextStatement:null,colour:120},
    {type:'greg_camera_clear',message0:'return camera to game control',previousStatement:null,nextStatement:null,colour:120},
    {type:'greg_camera_number',message0:'camera override %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['zoom','zoom']]}],output:'Number',colour:120},
    {type:'greg_camera_active',message0:'camera override is active',output:'Boolean',colour:120},
    {type:'greg_exit_lock',message0:'set %1 to %2',args0:[{type:'field_greg_room_connection',name:'CONNECTION'},{type:'field_dropdown',name:'LOCKED',options:[['locked','true'],['unlocked','false']]}],previousStatement:null,nextStatement:null,colour:120},
    {type:'greg_exit_locked',message0:'%1 is locked',args0:[{type:'field_greg_room_connection',name:'CONNECTION'}],output:'Boolean',colour:120},
    {type:'greg_room_count',message0:'number of placed rooms',output:'Number',colour:120},
    {type:'greg_room_start',message0:'starting placed room number',output:'Number',colour:120},
    {type:'greg_room_number',message0:'placed room %1 %2',args0:[{type:'input_value',name:'ROOM',check:'Number'},{type:'field_dropdown',name:'PROPERTY',options:[['source room number','source_room'],['world x','x'],['world y','y'],['width','width'],['height','height']]}],output:'Number',colour:120},
    {type:'greg_room_text',message0:'placed room %1 %2',args0:[{type:'input_value',name:'ROOM',check:'Number'},{type:'field_dropdown',name:'PROPERTY',options:[['name','id'],['source room name','source_id']]}],output:'String',colour:120},
    {type:'greg_room_condition',message0:'placed room %1 %2',args0:[{type:'input_value',name:'ROOM',check:'Number'},{type:'field_dropdown',name:'PROPERTY',options:[['is the starting room','start'],['uses placed-room layout','placed'],['is mirrored','mirrored']]}],output:'Boolean',colour:120},
    {type:'greg_player_room_move',message0:'move player %1 to placed room %2 at local x %3 y %4',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'ROOM',check:'Number'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:120},
    {type:'greg_entity_set',message0:'set this object %1 to %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['width scale','scale_x'],['height scale','scale_y'],['picture offset x','visual_offset_x'],['picture offset y','visual_offset_y'],['picture rotation','visual_rotation'],['animation tick','animation_tick'],['animation speed','animation_speed']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_get',message0:'this object %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['width scale','scale_x'],['height scale','scale_y'],['picture offset x','visual_offset_x'],['picture offset y','visual_offset_y'],['picture rotation','visual_rotation'],['animation tick','animation_tick'],['animation speed','animation_speed']]}],output:'Number',colour:210},
    {type:'greg_entity_layer',message0:'set this object draw order to %1',args0:[{type:'field_dropdown',name:'LAYER',options:[['object default','authored'],['behind players','behind'],['in front of players','front']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_tint',message0:'set this object color to %1',args0:[{type:'field_input',name:'COLOR',text:'#FFFFFFFF'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_tint_reset',message0:'reset this object color',previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_flag',message0:'set this object %1 to %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['animation paused','animation_paused'],['flipped horizontally','mirrored'],['visible','visible']]},{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_animation',message0:'play this object animation %1 %2',args0:[{type:'field_greg_animation',name:'ANIMATION'},{type:'field_dropdown',name:'RESTART',options:[['from the beginning','restart'],['continue if already playing','continue']]}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_animation_is',message0:'this object animation is %1',args0:[{type:'field_greg_animation',name:'ANIMATION'}],output:'Boolean',colour:120},
    {type:'greg_entity_animation_finished',message0:'this object animation finished',output:'Boolean',colour:120},
    {type:'greg_entities',message0:'for each object %1 %2 %3',args0:[{type:'field_greg_object',name:'TYPE'},{type:'input_dummy'},{type:'input_statement',name:'DO'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_named',message0:'with named object %1 %2 %3',args0:[{type:'field_greg_placement',name:'NAME'},{type:'input_dummy'},{type:'input_statement',name:'DO'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_spawn',message0:'create object %1 at x %2 y %3',args0:[{type:'field_greg_object',name:'TYPE'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_remove',message0:'remove this object',previousStatement:null,colour:210},
    {type:'greg_entity_health_get',message0:'this object %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]}],output:'Number',colour:0},
    {type:'greg_entity_health_enabled',message0:'this object has health enabled',output:'Boolean',colour:120},
    {type:'greg_entity_health_enable',message0:'enable this object health maximum %1 current %2',args0:[{type:'input_value',name:'MAXIMUM',check:'Number'},{type:'input_value',name:'CURRENT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_health_disable',message0:'disable health for this object',previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_invulnerable_set',message0:'set this object invulnerable to %1',args0:[{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_invulnerable',message0:'this object is invulnerable',output:'Boolean',colour:120},
    {type:'greg_entity_health_set',message0:'set this object %1 to %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_health_change',message0:'change this object %1 by %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['current health','current'],['maximum health','maximum']]},{type:'input_value',name:'AMOUNT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_heal',message0:'heal this object by %1',args0:[{type:'input_value',name:'AMOUNT',check:'Number'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_damage',message0:'deal %1 damage to %2 from %3',args0:[{type:'input_value',name:'AMOUNT',check:'Number'},{type:'field_dropdown',name:'TARGET',options:[['this object','this'],['other touching object','other']]},{type:'field_dropdown',name:'SOURCE',options:[['no source','none'],['this object','this'],['other touching object','other']]}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_damage_amount',message0:'damage amount',output:'Number',colour:0},
    {type:'greg_damage_applied',message0:'damage actually applied',output:'Number',colour:0},
    {type:'greg_damage_blocked',message0:'damage was blocked by invulnerability',output:'Boolean',colour:120},
    {type:'greg_health_event_value',message0:'health event %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['previous health','old_health'],['current health','health'],['maximum health','max_health'],['change amount','delta']]}],output:'Number',colour:0},
    {type:'greg_health_event_reason',message0:'health changed because of %1',args0:[{type:'field_dropdown',name:'REASON',options:[['damage','damage'],['healing','heal'],['setting current health','set'],['changing maximum health','max_changed'],['player respawn','respawn']]}],output:'Boolean',colour:120},
    {type:'greg_health_event_defeated',message0:'this damage defeated the target',output:'Boolean',colour:120},
    {type:'greg_damage_has_source',message0:'health event has a source object',output:'Boolean',colour:120},
    {type:'greg_damage_source_is',message0:'health event source is %1',args0:[{type:'field_greg_object',name:'TYPE'}],output:'Boolean',colour:120},
    {type:'greg_damage_native_source_is',message0:'damage came from native %1',args0:[{type:'field_dropdown',name:'SOURCE',options:[['punch','native:punch'],['kick','native:kick'],['held sword','native:sword'],['thrown sword','native:thrown_sword'],['spike ball','native:spike_ball'],['mine blast','native:mine']]}],output:'Boolean',colour:120},
    {type:'greg_damage_source_player',message0:'native damage player number (0 if none)',output:'Number',colour:0},
    {type:'greg_damage_filter_set',message0:'set this incoming damage allowed to %1',args0:[{type:'input_value',name:'ALLOWED',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_signal',message0:'send signal %1 value %2 to %3 from %4',args0:[{type:'field_input',name:'NAME',text:'activate'},{type:'input_value',name:'VALUE'},{type:'field_dropdown',name:'TARGET',options:[['this object','this'],['other touching object','other']]},{type:'field_dropdown',name:'SOURCE',options:[['no source','none'],['this object','this'],['other touching object','other']]}],previousStatement:null,nextStatement:null,colour:45},
    {type:'greg_entity_signal_named',message0:'send signal %1 value %2 to named object %3 from %4',args0:[{type:'field_input',name:'NAME',text:'activate'},{type:'input_value',name:'VALUE'},{type:'field_greg_placement',name:'TARGET'},{type:'field_dropdown',name:'SOURCE',options:[['no source','none'],['this object','this']]}],previousStatement:null,nextStatement:null,colour:45},
    {type:'greg_signal_name',message0:'signal name',output:'String',colour:45},
    {type:'greg_signal_value',message0:'signal value',output:null,colour:45},
    {type:'greg_signal_has_source',message0:'signal has a source object',output:'Boolean',colour:120},
    {type:'greg_signal_source_is',message0:'signal source is %1',args0:[{type:'field_greg_object',name:'TYPE'}],output:'Boolean',colour:120},
    {type:'greg_contact_other_is',message0:'other object is %1',args0:[{type:'field_greg_object',name:'TYPE'}],output:'Boolean',colour:120},
    {type:'greg_contact_area',message0:'contact uses this object area %1',args0:[{type:'field_greg_region',name:'ROLE'}],output:'Boolean',colour:120},
    {type:'greg_contact_other_area',message0:'contact uses other object %1',args0:[{type:'field_dropdown',name:'ROLE',options:regionRoles}],output:'Boolean',colour:120},
    {type:'greg_state_set',message0:'set map value %1 to %2',args0:[{type:'field_input',name:'KEY',text:'counter'},{type:'input_value',name:'VALUE'}],previousStatement:null,nextStatement:null,colour:280},
    {type:'greg_state_get',message0:'map value %1',args0:[{type:'field_input',name:'KEY',text:'counter'}],output:null,colour:280},
    {type:'greg_math',message0:'%1 %2 %3',args0:[{type:'input_value',name:'A',check:'Number'},{type:'field_dropdown',name:'OP',options:[['+','+'],['subtract','-'],['multiply','*'],['divide','/'],['remainder','%']]},{type:'input_value',name:'B',check:'Number'}],output:'Number',colour:230},
    {type:'greg_every',message0:'every %1 ticks',args0:[{type:'input_value',name:'INTERVAL',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_random',message0:'random integer from %1 to %2',args0:[{type:'input_value',name:'LOW',check:'Number'},{type:'input_value',name:'HIGH',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_function',message0:'%1 of %2',args0:[{type:'field_dropdown',name:'FUNCTION',options:[['absolute value','abs'],['round down','floor'],['round up','ceil'],['round to nearest','round'],['square root','sqrt'],['sine (degrees)','sin_deg'],['cosine (degrees)','cos_deg'],['tangent (degrees)','tan_deg'],['sign (-1, 0, or 1)','sign']]},{type:'input_value',name:'VALUE',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_bound',message0:'%1 of %2 and %3',args0:[{type:'field_dropdown',name:'FUNCTION',options:[['minimum','min'],['maximum','max']]},{type:'input_value',name:'A',check:'Number'},{type:'input_value',name:'B',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_clamp',message0:'clamp %1 between %2 and %3',args0:[{type:'input_value',name:'VALUE',check:'Number'},{type:'input_value',name:'LOW',check:'Number'},{type:'input_value',name:'HIGH',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_lerp',message0:'move from %1 toward %2 by %3',args0:[{type:'input_value',name:'A',check:'Number'},{type:'input_value',name:'B',check:'Number'},{type:'input_value',name:'AMOUNT',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_map_range',message0:'map %1 from %2 to %3 into %4 to %5',args0:[{type:'input_value',name:'VALUE',check:'Number'},{type:'input_value',name:'IN_LOW',check:'Number'},{type:'input_value',name:'IN_HIGH',check:'Number'},{type:'input_value',name:'OUT_LOW',check:'Number'},{type:'input_value',name:'OUT_HIGH',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_distance',message0:'distance from x %1 y %2 to x %3 y %4',args0:[{type:'input_value',name:'X1',check:'Number'},{type:'input_value',name:'Y1',check:'Number'},{type:'input_value',name:'X2',check:'Number'},{type:'input_value',name:'Y2',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_direction',message0:'direction from x %1 y %2 to x %3 y %4',args0:[{type:'input_value',name:'X1',check:'Number'},{type:'input_value',name:'Y1',check:'Number'},{type:'input_value',name:'X2',check:'Number'},{type:'input_value',name:'Y2',check:'Number'}],output:'Number',colour:230},
    {type:'greg_time',message0:'game tick',output:'Number',colour:120},
    {type:'greg_lua',message0:'Lua code (edit in Advanced)',previousStatement:null,nextStatement:null,colour:290}
  ];
  const variableScope={type:'field_dropdown',name:'SCOPE',options:[['map','map'],['player 1','1'],['player 2','2'],['this player','current'],['this object','object']]};
  class VariableField extends B.FieldDropdown {
    constructor(){super(function(){const workspace=this.getSourceBlock()?.workspace;const names=(workspace?.getVariableMap().getAllVariables()||[]).map(v=>v.getName());const selected=this.pendingName||this.getValue()||'variable';if(!names.includes(selected))names.push(selected);return names.sort().map(name=>[name,name]);});}
    doClassValidation_(name){if(typeof name!=='string'||!/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name))return null;this.pendingName=name;this.getOptions(false);return super.doClassValidation_(name);}
    static fromJson(){return new VariableField();}
  }
  B.fieldRegistry.register('field_greg_variable',VariableField);
  const variableName={type:'field_greg_variable',name:'NAME'};
  definitions.push(
    {type:'greg_variable_get',message0:'%1 variable %2',args0:[variableScope,variableName],output:null,colour:280},
    {type:'greg_variable_set',message0:'set %1 variable %2 to %3',args0:[variableScope,variableName,{type:'input_value',name:'VALUE'}],previousStatement:null,nextStatement:null,colour:280},
    {type:'greg_variable_change',message0:'change %1 variable %2 by %3',args0:[variableScope,variableName,{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:280},
    {type:'greg_variable_clear',message0:'clear %1 variable %2',args0:[variableScope,variableName],previousStatement:null,nextStatement:null,colour:280},
    {type:'greg_variable_exists',message0:'%1 variable %2 exists',args0:[variableScope,variableName],output:'Boolean',colour:280}
  );
  definitions.push(
    {type:'greg_entity_spawn_value',message0:'create object %1 at x %2 y %3 with its variable %4 set to %5',args0:[{type:'field_greg_object',name:'TYPE'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},variableName,{type:'input_value',name:'VALUE'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_handle',message0:'this object',output:'Object',colour:210},
    {type:'greg_object_named_value',message0:'named object %1',args0:[{type:'field_greg_placement',name:'NAME'}],output:'Object',colour:210},
    {type:'greg_object_reference_exists',message0:'object %1 still exists',args0:[{type:'input_value',name:'OBJECT',check:'Object'}],output:'Boolean',colour:120},
    {type:'greg_object_reference_type',message0:'object %1 type',args0:[{type:'input_value',name:'OBJECT',check:'Object'}],output:'String',colour:210},
    {type:'greg_object_reference_number',message0:'object %1 %2',args0:[{type:'input_value',name:'OBJECT',check:'Object'},{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['width scale','scale_x'],['height scale','scale_y'],['picture offset x','visual_offset_x'],['picture offset y','visual_offset_y'],['picture rotation','visual_rotation'],['animation tick','animation_tick'],['animation speed','animation_speed']]}],output:'Number',colour:210},
    {type:'greg_object_reference_set',message0:'set object %1 %2 to %3',args0:[{type:'input_value',name:'OBJECT',check:'Object'},{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['width scale','scale_x'],['height scale','scale_y'],['picture offset x','visual_offset_x'],['picture offset y','visual_offset_y'],['picture rotation','visual_rotation'],['animation tick','animation_tick'],['animation speed','animation_speed']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_object_reference_remove',message0:'remove object %1',args0:[{type:'input_value',name:'OBJECT',check:'Object'}],previousStatement:null,nextStatement:null,colour:210}
  );
  definitions.push(
    {type:'greg_terrain_box',message0:'solid terrain at x %1 y %2 width %3 height %4',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'input_value',name:'WIDTH',check:'Number'},{type:'input_value',name:'HEIGHT',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_terrain_ahead',message0:'solid terrain %1 pixels ahead of this object',args0:[{type:'input_value',name:'DISTANCE',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_entity_reverse',message0:'reverse this object %1',args0:[{type:'field_dropdown',name:'AXIS',options:[['horizontal direction','vx'],['vertical direction','vy'],['both directions','both']]}],previousStatement:null,nextStatement:null,colour:210}
  );
  definitions.push(
    {type:'greg_object_at',message0:'object at x %1 y %2 is %3',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'field_greg_object',name:'TYPE'}],output:'Boolean',colour:120},
    {type:'greg_coordinate_at',message0:'at x %1 y %2 is %3',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'field_greg_target',name:'TARGET'}],output:'Boolean',colour:120},
    {type:'greg_break',message0:'stop this loop',previousStatement:null,colour:120},
    {type:'greg_stop_script',message0:'stop this event or function',previousStatement:null,colour:120}
  );
  definitions.push(
    {type:'greg_vector_create',message0:'vector x %1 y %2',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],output:'Vector',colour:65},
    {type:'greg_vector_component',message0:'vector %1 %2',args0:[{type:'input_value',name:'VECTOR',check:'Vector'},{type:'field_dropdown',name:'COMPONENT',options:[['x','x'],['y','y']]}],output:'Number',colour:65},
    {type:'greg_vector_math',message0:'%1 vector %2 and %3',args0:[{type:'field_dropdown',name:'OP',options:[['add','add'],['subtract','subtract']]},{type:'input_value',name:'A',check:'Vector'},{type:'input_value',name:'B',check:'Vector'}],output:'Vector',colour:65},
    {type:'greg_vector_scale',message0:'vector %1 scaled by %2',args0:[{type:'input_value',name:'VECTOR',check:'Vector'},{type:'input_value',name:'SCALE',check:'Number'}],output:'Vector',colour:65},
    {type:'greg_vector_length',message0:'length of vector %1',args0:[{type:'input_value',name:'VECTOR',check:'Vector'}],output:'Number',colour:65},
    {type:'greg_vector_normalize',message0:'direction of vector %1',args0:[{type:'input_value',name:'VECTOR',check:'Vector'}],output:'Vector',colour:65},
    {type:'greg_to_text',message0:'%1 as text',args0:[{type:'input_value',name:'VALUE'}],output:'String',colour:65},
    {type:'greg_to_boolean',message0:'%1 as true / false',args0:[{type:'input_value',name:'VALUE'}],output:'Boolean',colour:65},
    {type:'greg_to_number',message0:'%1 as number, or %2 if invalid',args0:[{type:'input_value',name:'VALUE'},{type:'input_value',name:'FALLBACK',check:'Number'}],output:'Number',colour:65}
  );
  B.defineBlocksWithJsonArray(definitions);
  for(const type of ['procedures_defnoreturn','procedures_defreturn']){
    const standardProcedureGenerator=G.forBlock[type];
    G.forBlock[type]=(block,generator)=>{
      const result=standardProcedureGenerator(block,generator),name=G.getProcedureName(block.getFieldValue('NAME')),key='%'+name,source=G.definitions_[key];
      if(typeof source==='string'&&source.startsWith('function '))G.definitions_[key]='local '+source;
      return result;
    };
  }
  const value=(b,name,fallback='0')=>G.valueToCode(b,name,G.ORDER_NONE)||fallback;
  const quote=text=>G.quote_(String(text));
  function eventParent(block){let p=block.getSurroundParent();while(p&&!['greg_tick','greg_timer_event','greg_player_action_event','greg_player_sprite_event','greg_player_lifecycle_event','greg_player_health_event','greg_entity_event'].includes(p.type))p=p.getSurroundParent();return p;}
  function timerName(block){const name=String(block.getFieldValue('NAME')||'');if(!validTimerName(name))throw Error('Timer names must contain 1 to 31 bytes.');return name;}
  function contactEvent(block,playerOnly=false){const event=eventParent(block),kind=event&&event.type==='greg_entity_event'&&event.getFieldValue('EVENT');if(!event||!(kind==='contact'||kind==='player_contact')||(playerOnly&&kind!=='player_contact'))throw Error(playerOnly?'This block belongs inside a player-contact event.':'This block belongs inside an object-contact event.');return kind;}
  function healthEvent(block,allowed){const event=eventParent(block),kind=event&&event.getFieldValue('EVENT');if(!event||!((event.type==='greg_entity_event'&&['damage','defeated'].includes(kind))||(event.type==='greg_player_health_event'&&['damage','health_changed','defeated'].includes(kind)))||allowed&&!allowed.includes(kind))throw Error('This block belongs inside a matching health or damage event.');return {event,kind,data:kind==='damage'?'damage':kind==='health_changed'?'change':'defeat'};}
  function damagePayloadEvent(block){const event=eventParent(block),kind=event&&event.getFieldValue('EVENT');if(!event||event.type!=='greg_entity_event'||!['damage_filter','damage'].includes(kind))throw Error('This block belongs inside an incoming-damage check or damage event.');return {event,kind,data:'damage'};}
  function damageSourceEvent(block){const event=eventParent(block),kind=event&&event.getFieldValue('EVENT');return event&&event.type==='greg_entity_event'&&kind==='damage_filter'?{event,kind,data:'damage'}:healthEvent(block);}
  function damageEvent(block){return healthEvent(block,['damage']);}
  function signalEvent(block){const event=eventParent(block);if(!event||event.type!=='greg_entity_event'||event.getFieldValue('EVENT')!=='signal')throw Error('This block belongs inside a signal event.');}
  let callbackProcedureContext=false;
  function entityContext(block){let p=block.getSurroundParent();while(p){if(p.type==='greg_entities'||p.type==='greg_entity_named'||(callbackProcedureContext&&['procedures_defnoreturn','procedures_defreturn'].includes(p.type)))return;if(['greg_tick','greg_entity_event'].includes(p.type))break;p=p.getSurroundParent();}const event=eventParent(block);if(!event||event.type!=='greg_entity_event'||event.getFieldValue('EVENT')==='remove')throw Error('This object block belongs inside an object update or creation event.');}
  function playerContext(block){const event=eventParent(block),kind=event&&event.type==='greg_entity_event'&&event.getFieldValue('EVENT');if(!event||(event.type==='greg_entity_event'&&!['update','player_contact'].includes(kind)))throw Error('Player actions belong inside a game tick, object update, or player-contact event.');}
  function currentPlayer(block){let p=block.getSurroundParent();while(p){if(p.type==='greg_players'||p.type==='greg_player_action_event'||p.type==='greg_player_sprite_event'||p.type==='greg_player_lifecycle_event')return {direct:'player'};if(p.type==='greg_entity_event'){const event=p.getFieldValue('EVENT');if(event==='player_contact')return {direct:'contact'};if(event==='damage_filter'||event==='damage')return {slot:'damage.source_player'};}if(p.type==='greg_player_health_event')return {slot:'player',trusted:true};p=p.getSurroundParent();}throw Error('This player reporter belongs inside a player event, for-each-player loop, player-contact event, or object damage event.');}
  function selectedPlayer(block){const selected=block.getFieldValue('PLAYER');if(selected==='1'||selected==='2')return {slot:selected};if(selected==='current')return currentPlayer(block);throw Error('Choose player 1, player 2, or this player.');}
  function playerObservationValue(block,path,fallback,directFallback=true){
    const selection=selectedPlayer(block),parts=path.split('.');
    if(selection.direct){const expression=selection.direct+'.'+parts.join('.');return directFallback?'('+expression+' or '+fallback+')':expression;}
    const access=parts.map(part=>'['+quote(part)+']').join('');
    return '(function() local selected_player = '+selection.slot+' if selected_player == nil then return '+fallback+' end for _, candidate in ipairs(map.players()) do if candidate.player == selected_player then return candidate'+access+' or '+fallback+' end end return '+fallback+' end)()';
  }
  function currentPlayerNumber(block){const selection=currentPlayer(block);return selection.direct?selection.direct+'.player':selection.trusted?selection.slot:'('+selection.slot+' or 0)';}
  function playerNumber(block){return block.getFieldValue('PLAYER')==='current'?currentPlayerNumber(block):block.getFieldValue('PLAYER');}
  function playerReference(block){
    if(!eventParent(block))throw Error('Player reference blocks belong inside an event.');
    const reference=G.valueToCode(block,'PLAYER',G.ORDER_NONE);
    if(!reference)throw Error('Connect a player to this block.');
    return reference;
  }
  function playerReferenceObservation(block,path,fallback,directFallback=true){
    const reference=playerReference(block),access=path.split('.').map(part=>'['+quote(part)+']').join('');
    return '(function() local selected_player = '+reference+' if selected_player == nil then return '+fallback+' end for _, candidate in ipairs(map.players()) do if candidate.player == selected_player then return candidate'+access+(directFallback?' or '+fallback:'')+' end end return '+fallback+' end)()';
  }
  function objectReference(block){
    if(!eventParent(block))throw Error('Object reference blocks belong inside an event.');
    const reference=G.valueToCode(block,'OBJECT',G.ORDER_NONE);
    if(!reference)throw Error('Connect an object to this block.');
    return reference;
  }
  G.forBlock.greg_players=b=>{if(!eventParent(b))throw Error('Player queries belong inside an event.');return 'for _, player in ipairs(map.players()) do\n'+G.statementToCode(b,'DO')+'end\n';};
  G.forBlock.greg_player_value=b=>{const property=b.getFieldValue('PROPERTY');if(!['player','x','y','vx','vy','facing','health','max_health','contact_radius','native_state','room','previous_room','collision_flags','previous_collision_flags','skin_palette','clothing_palette','custom_sprite_cell','custom_sprite_tick','custom_sprite_frame','custom_sprite_frames'].includes(property))throw Error('Choose a valid player number value.');const optional=['health','max_health','custom_sprite_cell','custom_sprite_tick','custom_sprite_frame','custom_sprite_frames'].includes(property);return [playerObservationValue(b,property,'0',optional),G.ORDER_HIGH];};
  G.forBlock.greg_player_text=b=>{const property=b.getFieldValue('PROPERTY');if(!['custom_sprite_type','custom_sprite_animation','skin_tint','clothing_tint'].includes(property))throw Error('Choose a valid player text value.');return [playerObservationValue(b,property,"''"),G.ORDER_HIGH];};
  G.forBlock.greg_player_condition=b=>{const property=b.getFieldValue('PROPERTY');if(!['spawned','respawned','room_changed','has_sword','grounded','previously_grounded','body_visible','custom_sprite_visible','custom_sprite_finished','custom_sprite_restore','pressed.jump','input.jump','released.jump','pressed.attack','input.attack','input.left','input.right','input.up','input.down'].includes(property))throw Error('Choose a valid player condition.');return [playerObservationValue(b,property,'false',false),G.ORDER_HIGH];};
  G.forBlock.greg_player_action=b=>{const edge=b.getFieldValue('EDGE'),action=b.getFieldValue('ACTION');if(!['pressed','released','input'].includes(edge)||!['jump','attack','left','right','up','down','menu'].includes(action))throw Error('Choose a valid player action.');return [playerObservationValue(b,edge+'.'+action,'false',false),G.ORDER_HIGH];};
  G.forBlock.greg_player_reference=b=>{if(!eventParent(b))throw Error('Player references belong inside an event.');const selected=b.getFieldValue('PLAYER');if(selected==='1'||selected==='2')return [selected,G.ORDER_ATOMIC];const current=currentPlayer(b);return [current.direct?current.direct+'.player':current.slot,G.ORDER_HIGH];};
  G.forBlock.greg_player_reference_exists=b=>[playerReferenceObservation(b,'player','false',false)+' ~= false',G.ORDER_RELATIONAL];
  G.forBlock.greg_player_reference_number=b=>{const property=b.getFieldValue('PROPERTY');if(!['player','x','y','vx','vy','facing','health','max_health','contact_radius','native_state','room','previous_room','collision_flags','previous_collision_flags','skin_palette','clothing_palette','custom_sprite_cell','custom_sprite_tick','custom_sprite_frame','custom_sprite_frames'].includes(property))throw Error('Choose a valid player number value.');return [playerReferenceObservation(b,property,'0'),G.ORDER_HIGH];};
  G.forBlock.greg_player_reference_text=b=>{const property=b.getFieldValue('PROPERTY');if(!['custom_sprite_type','custom_sprite_animation','skin_tint','clothing_tint'].includes(property))throw Error('Choose a valid player text value.');return [playerReferenceObservation(b,property,"''"),G.ORDER_HIGH];};
  G.forBlock.greg_player_reference_condition=b=>{const property=b.getFieldValue('PROPERTY');if(!['spawned','respawned','room_changed','has_sword','grounded','previously_grounded','body_visible','custom_sprite_visible','custom_sprite_finished','custom_sprite_restore','pressed.jump','input.jump','released.jump','pressed.attack','input.attack','input.left','input.right','input.up','input.down'].includes(property))throw Error('Choose a valid player condition.');return [playerReferenceObservation(b,property,'false',false),G.ORDER_HIGH];};
  G.forBlock.greg_player_reference_velocity=b=>{playerContext(b);const player=playerReference(b);return 'do\n  local selected_player = '+player+'\n  if selected_player ~= nil then map.set_player_velocity(selected_player, '+value(b,'X')+', '+value(b,'Y')+') end\nend\n';};
  G.forBlock.greg_player_reference_position=b=>{playerContext(b);const player=playerReference(b);return 'do\n  local selected_player = '+player+'\n  if selected_player ~= nil then map.set_player_position(selected_player, '+value(b,'X')+', '+value(b,'Y')+') end\nend\n';};
  G.forBlock.greg_player_reference_damage=b=>{playerContext(b);const player=playerReference(b);return 'do\n  local selected_player = '+player+'\n  if selected_player ~= nil then map.damage_player(selected_player, '+value(b,'AMOUNT','1')+') end\nend\n';};
  G.forBlock.greg_player_reference_defeat=b=>{playerContext(b);const player=playerReference(b);return 'do\n  local selected_player = '+player+'\n  if selected_player ~= nil then map.defeat_player(selected_player) end\nend\n';};
  G.forBlock.greg_player_touching=b=>{
    entityContext(b);const player=playerObservationValue(b,'player','0',false),selection=b.getFieldValue('ROLE');
    if(!validRegion(selection))throw Error('Choose a valid detection area.');
    return ['(function() for _, contact in ipairs(entity.player_contacts(handle, '+quote(selection)+')) do if contact.player == '+player+' then return true end end return false end)()',G.ORDER_HIGH];
  };
  G.forBlock.greg_player_hit_object=b=>{entityContext(b);return ['entity.was_hit_by_player(handle, '+playerObservationValue(b,'player','0',false)+')',G.ORDER_HIGH];};
  const tickBody=b=>G.statementToCode(b,'DO');
  const playerActionBody=b=>{const player=b.getFieldValue('PLAYER'),edge=b.getFieldValue('EDGE'),action=b.getFieldValue('ACTION');if(!['any','1','2'].includes(player)||!['pressed','released','input'].includes(edge)||!['jump','attack','left','right','up','down','menu'].includes(action))throw Error('Choose a valid player action event.');const slot=player==='any'?'':'player.player == '+player+' and ';return '  for _, player in ipairs(map.players()) do\n    if '+slot+'player.'+edge+'.'+action+' then\n'+G.statementToCode(b,'DO')+'    end\n  end\n';};
  const playerSpriteBody=b=>{const selected=b.getFieldValue('PLAYER');if(!['any','1','2'].includes(selected))throw Error('Choose a valid player picture event.');const slot=selected==='any'?'':'player.player == '+selected+' and ';return '  for _, player in ipairs(map.players()) do\n    if '+slot+'player.custom_sprite_just_finished then\n'+G.statementToCode(b,'DO')+'    end\n  end\n';};
  const playerLifecycleBody=b=>{const selected=b.getFieldValue('PLAYER'),event=b.getFieldValue('EVENT');if(!['any','1','2'].includes(selected)||!['spawned','respawned','room_changed','room_left'].includes(event))throw Error('Choose a valid player lifecycle event.');const slot=selected==='any'?'':'player.player == '+selected+' and ',condition=event==='room_left'?'room_changed':event;return '  for _, player in ipairs(map.players()) do\n    if '+slot+'player.'+condition+' then\n'+G.statementToCode(b,'DO')+'    end\n  end\n';};
  G.forBlock.greg_tick=b=>'map.on_tick(function()\n'+tickBody(b)+'end)\n';
  G.forBlock.greg_timer_event=b=>'map.on_timer('+quote(timerName(b))+', function()\n'+G.statementToCode(b,'DO')+'end)\n';
  G.forBlock.greg_player_action_event=b=>'map.on_tick(function()\n'+playerActionBody(b)+'end)\n';
  G.forBlock.greg_player_sprite_event=b=>'map.on_tick(function()\n'+playerSpriteBody(b)+'end)\n';
  G.forBlock.greg_player_lifecycle_event=b=>'map.on_tick(function()\n'+playerLifecycleBody(b)+'end)\n';
  G.forBlock.greg_player_health_event=b=>{const player=b.getFieldValue('PLAYER'),event=b.getFieldValue('EVENT');if(!['any','1','2'].includes(player)||!['damage','health_changed','defeated'].includes(event))throw Error('Choose a valid player health event.');const data=event==='damage'?'damage':event==='health_changed'?'change':'defeat',slot=player==='any'?'':'  if player ~= '+player+' then return end\n';return 'map.on_player_'+event+'(function(player, '+data+')\n'+slot+G.statementToCode(b,'DO')+'end)\n';};
  G.forBlock.greg_entity_event=b=>{const key=b.getFieldValue('TYPE'),event=b.getFieldValue('EVENT');if(!/^[a-z][a-z0-9_.-]*:[a-z][a-z0-9_.-]*$/.test(key)||key.length>96)throw Error('Choose a valid object type.');const argument=['contact','player_contact'].includes(event)?'contact':['damage_filter','damage'].includes(event)?'damage':event==='defeated'?'defeat':event==='signal'?'signal':event==='animation_finish'?'animation':'value';if(event==='damage_filter')return 'entity.on_damage_filter('+quote(key)+', function(handle, damage)\n  local allow_damage = true\n'+G.statementToCode(b,'DO')+'  return allow_damage\nend)\n';return 'entity.on_'+event+'('+quote(key)+', function(handle, '+argument+')\n'+G.statementToCode(b,'DO')+'end)\n';};
  G.forBlock.greg_velocity=b=>{playerContext(b);return 'map.set_player_velocity('+playerNumber(b)+', '+value(b,'X')+', '+value(b,'Y')+')\n';};
  G.forBlock.greg_player_position=b=>{playerContext(b);return 'map.set_player_position('+playerNumber(b)+', '+value(b,'X')+', '+value(b,'Y')+')\n';};
  G.forBlock.greg_player_tint=b=>{playerContext(b);const color=String(b.getFieldValue('COLOR')||'').trim(),channel=b.getFieldValue('CHANNEL');if(!['skin_tint','clothing_tint'].includes(channel))throw Error('Choose skin or clothing color.');if(!/^#[0-9a-f]{8}$/i.test(color))throw Error('Player color must be #RRGGBBAA.');return 'map.set_player_presentation('+playerNumber(b)+', {'+channel+' = '+quote(color.toUpperCase())+'})\n';};
  G.forBlock.greg_player_tint_reset=b=>{playerContext(b);const channel=b.getFieldValue('CHANNEL');if(!['skin_tint','clothing_tint'].includes(channel))throw Error('Choose skin or clothing color.');return "map.set_player_presentation("+playerNumber(b)+", {"+channel+" = 'default'})\n";};
  G.forBlock.greg_player_visibility=b=>{playerContext(b);return 'map.set_player_presentation('+playerNumber(b)+', {body_visible = '+value(b,'VISIBLE','true')+'})\n';};
  G.forBlock.greg_player_presentation_reset=b=>{playerContext(b);return 'map.reset_player_presentation('+playerNumber(b)+')\n';};
  G.forBlock.greg_player_sprite=b=>{playerContext(b);const key=b.getFieldValue('TYPE'),animation=String(b.getFieldValue('ANIMATION')||'').trim();if(!validType(key))throw Error('Choose an object picture for the player.');if(!/^(?:default|[a-z][a-z0-9_.-]{0,31})$/.test(animation))throw Error("Animation names use up to 32 lowercase letters, numbers, '.', '_' or '-'.");return 'map.set_player_sprite('+playerNumber(b)+', '+quote(key)+', '+quote(animation)+', '+(b.getFieldValue('RESTART')==='restart'?'true':'false')+', '+(b.getFieldValue('RESTORE')==='restore'?'true':'false')+')\n';};
  G.forBlock.greg_player_sprite_is=b=>{if(!eventParent(b))throw Error('Player picture reporters belong inside an event.');const key=b.getFieldValue('TYPE'),animation=String(b.getFieldValue('ANIMATION')||'').trim();if(!validType(key))throw Error('Choose an object picture for the player.');if(!/^(?:default|[a-z][a-z0-9_.-]{0,31})$/.test(animation))throw Error("Animation names use up to 32 lowercase letters, numbers, '.', '_' or '-'.");const sprite='map.player_sprite('+playerNumber(b)+')';return ['(function() local picture = '+sprite+' return picture ~= nil and picture.type == '+quote(key)+' and picture.animation == '+quote(animation)+' end)()',G.ORDER_HIGH];};
  G.forBlock.greg_player_sprite_finished=b=>{if(!eventParent(b))throw Error('Player picture reporters belong inside an event.');const sprite='map.player_sprite('+playerNumber(b)+')';return ['(function() local picture = '+sprite+' return picture ~= nil and picture.finished end)()',G.ORDER_HIGH];};
  G.forBlock.greg_player_sprite_number=b=>{playerContext(b);const property=b.getFieldValue('PROPERTY');if(!['scale_x','scale_y','offset_x','offset_y','rotation','animation_speed'].includes(property))throw Error('Choose a valid player picture property.');return 'map.set_player_sprite_transform('+playerNumber(b)+', {'+property+' = '+value(b,'VALUE')+'})\n';};
  G.forBlock.greg_player_sprite_tint=b=>{playerContext(b);const color=String(b.getFieldValue('COLOR')||'').trim();if(!/^#[0-9a-f]{8}$/i.test(color))throw Error('Player picture color must be #RRGGBBAA.');return 'map.set_player_sprite_transform('+playerNumber(b)+', {tint = '+quote(color.toUpperCase())+'})\n';};
  G.forBlock.greg_player_sprite_layer=b=>{playerContext(b);const layer=b.getFieldValue('LAYER');if(!['authored','behind','front'].includes(layer))throw Error('Choose a valid player picture draw order.');return 'map.set_player_sprite_transform('+playerNumber(b)+', {draw_layer = '+quote(layer)+'})\n';};
  G.forBlock.greg_player_sprite_mirrored=b=>{playerContext(b);return 'map.set_player_sprite_transform('+playerNumber(b)+', {mirrored = '+value(b,'VALUE','false')+'})\n';};
  G.forBlock.greg_player_sprite_visibility=b=>{playerContext(b);return 'map.set_player_sprite_transform('+playerNumber(b)+', {visible = '+value(b,'VALUE','true')+'})\n';};
  G.forBlock.greg_player_sprite_transform_reset=b=>{playerContext(b);return 'map.reset_player_sprite_transform('+playerNumber(b)+')\n';};
  G.forBlock.greg_player_sprite_clear=b=>{playerContext(b);return 'map.clear_player_sprite('+playerNumber(b)+')\n';};
  G.forBlock.greg_defeat=b=>{playerContext(b);return 'map.defeat_player('+playerNumber(b)+')\n';};
  G.forBlock.greg_player_health_get=b=>{if(!eventParent(b))throw Error('Player health reporters belong inside an event.');const call='map.player_health('+playerNumber(b)+')',index=b.getFieldValue('PROPERTY')==='maximum'?2:1;return ['(select('+index+', '+call+') or 0)',G.ORDER_HIGH];};
  G.forBlock.greg_player_health_enabled=b=>{if(!eventParent(b))throw Error('Player health reporters belong inside an event.');return ['map.player_health('+playerNumber(b)+') ~= nil',G.ORDER_RELATIONAL];};
  G.forBlock.greg_player_health_enable=b=>{playerContext(b);const current=G.valueToCode(b,'CURRENT',G.ORDER_NONE),suffix=current?', '+current:'';return 'map.enable_player_health('+playerNumber(b)+', '+value(b,'MAXIMUM','1')+suffix+')\n';};
  G.forBlock.greg_player_health_disable=b=>{playerContext(b);return 'map.disable_player_health('+playerNumber(b)+')\n';};
  G.forBlock.greg_player_invulnerable_set=b=>{playerContext(b);return 'map.set_player_invulnerable('+playerNumber(b)+', '+value(b,'VALUE','true')+')\n';};
  G.forBlock.greg_player_invulnerable=b=>{if(!eventParent(b))throw Error('Player health reporters belong inside an event.');return ['map.player_invulnerable('+playerNumber(b)+') == true',G.ORDER_RELATIONAL];};
  G.forBlock.greg_player_health_set=b=>{playerContext(b);const fn=b.getFieldValue('PROPERTY')==='maximum'?'set_player_max_health':'set_player_health';return 'map.'+fn+'('+playerNumber(b)+', '+value(b,'VALUE')+')\n';};
  G.forBlock.greg_player_health_change=b=>{playerContext(b);const slot=playerNumber(b),maximum=b.getFieldValue('PROPERTY')==='maximum',fn=maximum?'set_player_max_health':'set_player_health',name=maximum?'maximum':'current';return 'do\n  local current, maximum = map.player_health('+slot+')\n  map.'+fn+'('+slot+', '+name+' + ('+value(b,'AMOUNT','1')+'))\nend\n';};
  G.forBlock.greg_player_heal=b=>{playerContext(b);return 'map.heal_player('+playerNumber(b)+', '+value(b,'AMOUNT','1')+')\n';};
  G.forBlock.greg_player_damage=b=>{playerContext(b);const source=b.getFieldValue('SOURCE');if(source==='this')entityContext(b);return 'map.damage_player('+playerNumber(b)+', '+value(b,'AMOUNT','1')+(source==='this'?', handle':'')+')\n';};
  G.forBlock.greg_timer_start=b=>{if(!eventParent(b))throw Error('Timer actions belong inside an event.');return 'map.timer_start('+quote(timerName(b))+', '+value(b,'DELAY','1')+')\n';};
  G.forBlock.greg_timer_repeat=b=>{if(!eventParent(b))throw Error('Timer actions belong inside an event.');return 'map.timer_start('+quote(timerName(b))+', '+value(b,'DELAY','1')+', '+value(b,'INTERVAL','1')+')\n';};
  G.forBlock.greg_timer_cancel=b=>{if(!eventParent(b))throw Error('Timer actions belong inside an event.');return 'map.timer_cancel('+quote(timerName(b))+')\n';};
  G.forBlock.greg_timer_remaining=b=>{if(!eventParent(b))throw Error('Timer reporters belong inside an event.');return ['map.timer_remaining('+quote(timerName(b))+')',G.ORDER_HIGH];};
  G.forBlock.greg_timer_active=b=>{if(!eventParent(b))throw Error('Timer reporters belong inside an event.');return ['map.timer_remaining('+quote(timerName(b))+') > 0',G.ORDER_RELATIONAL];};
  G.forBlock.greg_camera_point=b=>{if(!eventParent(b))throw Error('Camera actions belong inside an event.');return 'map.set_camera_point('+value(b,'X','0')+', '+value(b,'Y','0')+', '+value(b,'ZOOM','1')+')\n';};
  G.forBlock.greg_camera_clear=b=>{if(!eventParent(b))throw Error('Camera actions belong inside an event.');return 'map.clear_camera()\n';};
  G.forBlock.greg_camera_number=b=>{if(!eventParent(b))throw Error('Camera reporters belong inside an event.');const property=b.getFieldValue('PROPERTY');if(!['x','y','zoom'].includes(property))throw Error('Choose x, y, or zoom.');return ['((map.camera_override() or {}).'+property+' or '+(property==='zoom'?'1':'0')+')',G.ORDER_HIGH];};
  G.forBlock.greg_camera_active=b=>{if(!eventParent(b))throw Error('Camera reporters belong inside an event.');return ['map.camera_override() ~= nil',G.ORDER_RELATIONAL];};
  G.forBlock.greg_exit_lock=b=>{if(!eventParent(b))throw Error('Door actions belong inside an event.');return 'map.set_exit_locked('+roomConnectionNumber(b.getFieldValue('CONNECTION'))+', '+b.getFieldValue('LOCKED')+')\n';};
  G.forBlock.greg_exit_locked=b=>{if(!eventParent(b))throw Error('Door conditions belong inside an event.');return ['map.exit_locked('+roomConnectionNumber(b.getFieldValue('CONNECTION'))+')',G.ORDER_HIGH];};
  G.forBlock.greg_room_count=()=>['map.room_count()',G.ORDER_HIGH];
  G.forBlock.greg_room_start=()=>['(map.start_room() or 0)',G.ORDER_HIGH];
  G.forBlock.greg_room_number=b=>['map.room_info('+value(b,'ROOM')+').'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];
  G.forBlock.greg_room_text=b=>['map.room_info('+value(b,'ROOM')+').'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];
  G.forBlock.greg_room_condition=b=>['map.room_info('+value(b,'ROOM')+').'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];
  G.forBlock.greg_player_room_move=b=>{playerContext(b);return 'map.move_player_to_room('+playerNumber(b)+', '+value(b,'ROOM')+', '+value(b,'X')+', '+value(b,'Y')+')\n';};
  G.forBlock.greg_entity_set=b=>{entityContext(b);return 'entity.set(handle, {'+b.getFieldValue('PROPERTY')+' = '+value(b,'VALUE')+'})\n';};
  G.forBlock.greg_entity_flag=b=>{entityContext(b);return 'entity.set(handle, {'+b.getFieldValue('PROPERTY')+' = '+value(b,'VALUE','false')+'})\n';};
  G.forBlock.greg_entities=b=>{if(!eventParent(b))throw Error('Object queries belong inside an event.');const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return 'for _, handle in ipairs(entity.list()) do\n  if entity.exists(handle) and entity.type(handle) == '+quote(key)+' then\n'+G.statementToCode(b,'DO')+'  end\nend\n';};
  G.forBlock.greg_entity_named=b=>{if(!eventParent(b))throw Error('Named object queries belong inside an event.');const name=b.getFieldValue('NAME');if(!validPlacement(name))throw Error('Choose a named object placement.');return 'do\n  local handle = entity.find('+quote(name)+')\n  if handle then\n'+G.statementToCode(b,'DO')+'  end\nend\n';};
  G.forBlock.greg_entity_spawn=b=>{if(!eventParent(b))throw Error('Create objects inside an event block.');const key=b.getFieldValue('TYPE');if(!/^[a-z][a-z0-9_.-]*:[a-z][a-z0-9_.-]*$/.test(key)||key.length>96)throw Error('Choose a valid object type.');return 'entity.spawn('+quote(key)+', {x = '+value(b,'X')+', y = '+value(b,'Y')+'})\n';};
  G.forBlock.greg_entity_spawn_value=b=>{if(!eventParent(b))throw Error('Create objects inside an event block.');const key=b.getFieldValue('TYPE'),name=b.getFieldValue('NAME');if(!validType(key))throw Error('Choose a valid object type.');if(!/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name))throw Error('Variable names need 1–20 letters, digits or underscores, starting with a letter or underscore.');return 'entity.spawn('+quote(key)+', {x = '+value(b,'X')+', y = '+value(b,'Y')+', values = {['+quote(name)+'] = '+value(b,'VALUE')+'}})\n';};
  G.forBlock.greg_entity_handle=b=>{entityContext(b);return ['handle',G.ORDER_HIGH];};
  G.forBlock.greg_object_named_value=b=>{if(!eventParent(b))throw Error('Named object references belong inside an event.');const name=b.getFieldValue('NAME');if(!validPlacement(name))throw Error('Choose a named object placement.');return ['entity.find('+quote(name)+')',G.ORDER_HIGH];};
  G.forBlock.greg_object_reference_exists=b=>{const reference=objectReference(b);return ['(function() local target = '+reference+' return target ~= nil and entity.exists(target) end)()',G.ORDER_HIGH];};
  G.forBlock.greg_object_reference_type=b=>{const reference=objectReference(b);return ["(function() local target = "+reference+" if target ~= nil and entity.exists(target) then return entity.type(target) end return '' end)()",G.ORDER_HIGH];};
  G.forBlock.greg_object_reference_number=b=>{const reference=objectReference(b),property=b.getFieldValue('PROPERTY');return ['(function() local target = '+reference+' if target ~= nil and entity.exists(target) then return entity.get(target).'+property+' end return 0 end)()',G.ORDER_HIGH];};
  G.forBlock.greg_object_reference_set=b=>{const reference=objectReference(b),property=b.getFieldValue('PROPERTY');return 'do\n  local target = '+reference+'\n  if target ~= nil and entity.exists(target) then entity.set(target, {'+property+' = '+value(b,'VALUE')+'}) end\nend\n';};
  G.forBlock.greg_object_reference_remove=b=>{const reference=objectReference(b);return 'do\n  local target = '+reference+'\n  if target ~= nil and entity.exists(target) then entity.remove(target) end\nend\n';};
  G.forBlock.greg_entity_remove=b=>{entityContext(b);return 'entity.remove(handle)\n';};
  G.forBlock.greg_entity_health_get=b=>{entityContext(b);const index=b.getFieldValue('PROPERTY')==='maximum'?2:1;return ['(select('+index+', entity.health(handle)) or 0)',G.ORDER_HIGH];};
  G.forBlock.greg_entity_health_enabled=b=>{entityContext(b);return ['entity.health(handle) ~= nil',G.ORDER_RELATIONAL];};
  G.forBlock.greg_entity_health_enable=b=>{entityContext(b);const current=G.valueToCode(b,'CURRENT',G.ORDER_NONE),suffix=current?', '+current:'';return 'entity.enable_health(handle, '+value(b,'MAXIMUM','1')+suffix+')\n';};
  G.forBlock.greg_entity_health_disable=b=>{entityContext(b);return 'entity.disable_health(handle)\n';};
  G.forBlock.greg_entity_invulnerable_set=b=>{entityContext(b);return 'entity.set_invulnerable(handle, '+value(b,'VALUE','true')+')\n';};
  G.forBlock.greg_entity_invulnerable=b=>{entityContext(b);return ['entity.invulnerable(handle) == true',G.ORDER_RELATIONAL];};
  G.forBlock.greg_entity_health_set=b=>{entityContext(b);const fn=b.getFieldValue('PROPERTY')==='maximum'?'set_max_health':'set_health';return 'entity.'+fn+'(handle, '+value(b,'VALUE')+')\n';};
  G.forBlock.greg_entity_health_change=b=>{entityContext(b);const maximum=b.getFieldValue('PROPERTY')==='maximum',fn=maximum?'set_max_health':'set_health',name=maximum?'maximum':'current';return 'do\n  local current, maximum = entity.health(handle)\n  entity.'+fn+'(handle, '+name+' + ('+value(b,'AMOUNT','1')+'))\nend\n';};
  G.forBlock.greg_entity_heal=b=>{entityContext(b);return 'entity.heal(handle, '+value(b,'AMOUNT','1')+')\n';};
  G.forBlock.greg_entity_damage=b=>{entityContext(b);const target=b.getFieldValue('TARGET'),source=b.getFieldValue('SOURCE');if(target==='other'||source==='other')contactEvent(b);const targetCode=target==='other'?'contact.other':'handle',sourceCode=source==='other'?'contact.other':source==='this'?'handle':'nil';return 'entity.damage('+targetCode+', '+value(b,'AMOUNT','1')+', '+sourceCode+')\n';};
  G.forBlock.greg_damage_amount=b=>{damagePayloadEvent(b);return ['damage.amount',G.ORDER_HIGH];};
  G.forBlock.greg_damage_applied=b=>{damageEvent(b);return ['damage.applied or damage.amount',G.ORDER_HIGH];};
  G.forBlock.greg_damage_blocked=b=>{damageEvent(b);return ['damage.blocked == true',G.ORDER_RELATIONAL];};
  G.forBlock.greg_health_event_value=b=>{const context=healthEvent(b),property=b.getFieldValue('PROPERTY');if(property==='delta'&&context.kind!=='health_changed')throw Error('Health change amount belongs inside a health-changed event.');if(property==='old_health'&&context.kind==='defeated')throw Error('Previous health is unavailable in a defeated event.');return [context.data+'.'+property,G.ORDER_HIGH];};
  G.forBlock.greg_health_event_reason=b=>{const context=healthEvent(b,['health_changed']);return [context.data+'.reason == '+quote(b.getFieldValue('REASON')),G.ORDER_RELATIONAL];};
  G.forBlock.greg_health_event_defeated=b=>{const context=damageEvent(b);return [context.data+'.defeated',G.ORDER_HIGH];};
  G.forBlock.greg_damage_has_source=b=>{const context=damageSourceEvent(b);return [context.data+'.source ~= nil',G.ORDER_RELATIONAL];};
  G.forBlock.greg_damage_source_is=b=>{const context=damageSourceEvent(b),key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return [context.data+'.source_type == '+quote(key),G.ORDER_RELATIONAL];};
  G.forBlock.greg_damage_native_source_is=b=>{const context=damageSourceEvent(b),source=b.getFieldValue('SOURCE');if(!['native:punch','native:kick','native:sword','native:thrown_sword','native:spike_ball','native:mine'].includes(source))throw Error('Choose a native damage source.');return [context.data+'.source_type == '+quote(source),G.ORDER_RELATIONAL];};
  G.forBlock.greg_damage_source_player=b=>{const context=damagePayloadEvent(b);return ['('+context.data+'.source_player or 0)',G.ORDER_HIGH];};
  G.forBlock.greg_damage_filter_set=b=>{const event=damagePayloadEvent(b);if(event.kind!=='damage_filter')throw Error('Damage permission belongs inside an incoming-damage check.');return 'allow_damage = '+value(b,'ALLOWED','true')+'\n';};
  G.forBlock.greg_entity_signal=b=>{entityContext(b);const name=b.getFieldValue('NAME'),target=b.getFieldValue('TARGET'),source=b.getFieldValue('SOURCE');if(!/^[a-z][a-z0-9_.-]{0,31}$/.test(name))throw Error("Signal names start with a lowercase letter and use up to 32 lowercase letters, numbers, '.', '_' or '-'.");if(target==='other'||source==='other')contactEvent(b);const targetCode=target==='other'?'contact.other':'handle',sourceCode=source==='other'?'contact.other':source==='this'?'handle':'nil';return 'entity.signal('+targetCode+', '+quote(name)+', '+value(b,'VALUE','nil')+', '+sourceCode+')\n';};
  G.forBlock.greg_entity_signal_named=b=>{if(!eventParent(b))throw Error('Send signals inside an event block.');const name=b.getFieldValue('NAME'),target=b.getFieldValue('TARGET'),source=b.getFieldValue('SOURCE');if(!/^[a-z][a-z0-9_.-]{0,31}$/.test(name))throw Error("Signal names start with a lowercase letter and use up to 32 lowercase letters, numbers, '.', '_' or '-'.");if(!validPlacement(target))throw Error('Choose a named object placement.');if(source==='this')entityContext(b);return 'do\n  local target = entity.find('+quote(target)+')\n  if target then entity.signal(target, '+quote(name)+', '+value(b,'VALUE','nil')+', '+(source==='this'?'handle':'nil')+') end\nend\n';};
  G.forBlock.greg_signal_name=b=>{signalEvent(b);return ['signal.name',G.ORDER_HIGH];};
  G.forBlock.greg_signal_value=b=>{signalEvent(b);return ['signal.value',G.ORDER_HIGH];};
  G.forBlock.greg_signal_has_source=b=>{signalEvent(b);return ['signal.source ~= nil',G.ORDER_RELATIONAL];};
  G.forBlock.greg_signal_source_is=b=>{signalEvent(b);const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return ['signal.source_type == '+quote(key),G.ORDER_RELATIONAL];};
  G.forBlock.greg_contact_other_is=b=>{contactEvent(b);const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return ['contact.other_type == '+quote(key),G.ORDER_RELATIONAL];};
  G.forBlock.greg_contact_area=b=>{const kind=contactEvent(b),selection=b.getFieldValue('ROLE');if(!validRegion(selection))throw Error('Choose a valid contact area.');const prefix=kind==='player_contact'?'contact.':'contact.self_';return [selection==='any'?'true':selection.startsWith('name:')?prefix+'region_name == '+quote(selection.slice(5)):prefix+'role == '+quote(selection),G.ORDER_RELATIONAL];};
  G.forBlock.greg_contact_other_area=b=>{contactEvent(b);const selection=b.getFieldValue('ROLE');if(!validRegion(selection)||selection.startsWith('name:'))throw Error('Choose a valid other-object area role.');return [selection==='any'?'true':'contact.other_role == '+quote(selection),G.ORDER_RELATIONAL];};
  G.forBlock.greg_entity_get=b=>{entityContext(b);return ['entity.get(handle).'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];};
  G.forBlock.greg_entity_animation=b=>{entityContext(b);return 'entity.play_animation(handle, '+quote(b.getFieldValue('ANIMATION'))+', '+(b.getFieldValue('RESTART')==='restart'?'true':'false')+')\n';};
  G.forBlock.greg_entity_animation_is=b=>{entityContext(b);return ['entity.animation(handle) == '+quote(b.getFieldValue('ANIMATION')),G.ORDER_RELATIONAL];};
  G.forBlock.greg_entity_animation_finished=b=>{entityContext(b);return ['entity.animation_status(handle).finished',G.ORDER_ATOMIC];};
  G.forBlock.greg_entity_layer=b=>{entityContext(b);return 'entity.set(handle, {draw_layer = '+quote(b.getFieldValue('LAYER'))+'})\n';};
  G.forBlock.greg_entity_tint=b=>{entityContext(b);const color=String(b.getFieldValue('COLOR')||'').trim();if(!/^#[0-9a-f]{8}$/i.test(color))throw Error('Object color must be #RRGGBBAA.');return 'entity.set(handle, {visual_tint = '+quote(color.toUpperCase())+'})\n';};
  G.forBlock.greg_entity_tint_reset=b=>{entityContext(b);return "entity.set(handle, {visual_tint = 'default'})\n";};
  function variableKey(b){
    const name=b.getFieldValue('NAME'),scope=b.getFieldValue('SCOPE');
    if(!/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name))throw Error('Variable names need 1?20 letters, digits or underscores, starting with a letter or underscore.');
    if(scope==='object'){entityContext(b);return null;}
    const prefix=scope==='map'?quote('v:m:'+name):scope==='current'?quote('v:p:')+' .. '+currentPlayerNumber(b)+' .. '+quote(':'+name):quote('v:p:'+scope+':'+name);
    return 'map.state['+prefix+']';
  }
  function objectVariable(b){variableKey(b);return 'entity.value(handle, '+quote(b.getFieldValue('NAME'))+')';}
  G.forBlock.greg_variable_get=b=>{const k=b.getFieldValue('SCOPE')==='object'?objectVariable(b):variableKey(b);return ['('+k+' == nil and 0 or '+k+')',G.ORDER_HIGH];};
  G.forBlock.greg_variable_set=b=>b.getFieldValue('SCOPE')==='object'?'entity.set_value(handle, '+quote(b.getFieldValue('NAME'))+', '+value(b,'VALUE')+')\n':variableKey(b)+' = '+value(b,'VALUE')+'\n';
  G.forBlock.greg_variable_change=b=>{if(b.getFieldValue('SCOPE')==='object'){objectVariable(b);return 'entity.change_value(handle, '+quote(b.getFieldValue('NAME'))+', '+value(b,'VALUE')+')\n';}const k=variableKey(b);return k+' = ('+k+' or 0) + ('+value(b,'VALUE')+')\n';};
  G.forBlock.greg_variable_clear=b=>b.getFieldValue('SCOPE')==='object'?(objectVariable(b),'entity.set_value(handle, '+quote(b.getFieldValue('NAME'))+', nil)\n'):variableKey(b)+' = nil\n';
  G.forBlock.greg_variable_exists=b=>b.getFieldValue('SCOPE')==='object'?(objectVariable(b),['entity.has_value(handle, '+quote(b.getFieldValue('NAME'))+')',G.ORDER_HIGH]):['('+variableKey(b)+' ~= nil)',G.ORDER_HIGH];
  G.forBlock.greg_state_set=b=>'map.state['+quote(b.getFieldValue('KEY'))+'] = '+value(b,'VALUE')+'\n';
  G.forBlock.greg_state_get=b=>{const key='map.state['+quote(b.getFieldValue('KEY'))+']';return ['('+key+' == nil and 0 or '+key+')',G.ORDER_HIGH];};
  const deterministicSqrt=code=>'(function() local value = '+code+'; if type(value) ~= '+quote('number')+' or value < 0 or value ~= value or value - value ~= 0 then error('+quote('Square root needs a finite non-negative number.')+') end; if value == 0 then return 0 end; local normalized, scale = value, 1; for _ = 1, 600 do if normalized <= 4 then break end; normalized, scale = normalized / 4, scale * 2 end; for _ = 1, 600 do if normalized >= 1 then break end; normalized, scale = normalized * 4, scale / 2 end; local root = (normalized + 1) * 0.5; for _ = 1, 12 do root = (root + normalized / root) * 0.5 end; return root * scale end)()';
  const deterministicTrig=(code,fn)=>{const setup='local degrees = '+code+'; if type(degrees) ~= '+quote('number')+' or degrees ~= degrees or degrees - degrees ~= 0 then error('+quote('Trigonometry needs a finite number of degrees.')+') end; degrees = degrees % 360; if degrees > 180 then degrees = degrees - 360 end; local radians = degrees * 0.017453292519943295; local squared = radians * radians; ';if(fn==='sin')return '(function() '+setup+'local term, result = radians, radians; for i = 1, 8 do term = -term * squared / ((2 * i) * (2 * i + 1)); result = result + term end; return result end)()';if(fn==='cos')return '(function() '+setup+'local term, result = 1, 1; for i = 1, 8 do term = -term * squared / ((2 * i - 1) * (2 * i)); result = result + term end; return result end)()';return '(function() '+setup+'local sin_term, sine, cos_term, cosine = radians, radians, 1, 1; for i = 1, 8 do sin_term = -sin_term * squared / ((2 * i) * (2 * i + 1)); sine = sine + sin_term; cos_term = -cos_term * squared / ((2 * i - 1) * (2 * i)); cosine = cosine + cos_term end; if math.abs(cosine) < 0.000000000001 then error('+quote('Tangent is undefined at this angle.')+') end; return sine / cosine end)()';};
  const deterministicDirection=(dx,dy)=>'(function() local dx, dy = '+dx+', '+dy+'; if type(dx) ~= '+quote('number')+' or type(dy) ~= '+quote('number')+' or dx ~= dx or dy ~= dy or dx - dx ~= 0 or dy - dy ~= 0 then error('+quote('Point direction needs finite coordinates.')+') end; if dx == 0 then if dy > 0 then return 90 elseif dy < 0 then return -90 else return 0 end end; local ax, ay = math.abs(dx), math.abs(dy); local function atan_degrees(value) local absolute = math.abs(value); return value * (45 + 15.642246457208728 * (1 - absolute)) end; local angle; if ax >= ay then angle = atan_degrees(dy / dx); if dx < 0 then angle = angle + (dy >= 0 and 180 or -180) end elseif dy > 0 then angle = 90 - atan_degrees(dx / dy) else angle = -90 - atan_degrees(dx / dy) end; return angle end)()';
  G.forBlock.greg_math=b=>['('+value(b,'A')+' '+b.getFieldValue('OP')+' '+value(b,'B')+')',G.ORDER_HIGH];
  G.forBlock.greg_every=b=>['map.every('+value(b,'INTERVAL','60')+')',G.ORDER_HIGH];
  G.forBlock.greg_random=b=>{
    if(!eventParent(b))throw Error('Random numbers belong inside an event.');
    const low=value(b,'LOW','1'),high=value(b,'HIGH','10');
    return ['(function() local low, high = '+low+', '+high+'; if low > high then low, high = high, low end; return map.random(low, high) end)()',G.ORDER_HIGH];
  };
  G.forBlock.greg_math_function=b=>{const fn=b.getFieldValue('FUNCTION'),v=value(b,'VALUE','0');if(fn==='round')return ['(function() local n = '+v+'; return n < 0 and math.ceil(n - 0.5) or math.floor(n + 0.5) end)()',G.ORDER_HIGH];if(fn==='sign')return ['(function() local n = '+v+'; return n < 0 and -1 or (n > 0 and 1 or 0) end)()',G.ORDER_HIGH];if(fn==='sqrt')return [deterministicSqrt(v),G.ORDER_HIGH];if(/^(sin|cos|tan)_deg$/.test(fn))return [deterministicTrig(v,fn.slice(0,-4)),G.ORDER_HIGH];return ['math.'+fn+'('+v+')',G.ORDER_HIGH];};
  G.forBlock.greg_math_bound=b=>['math.'+b.getFieldValue('FUNCTION')+'('+value(b,'A')+', '+value(b,'B')+')',G.ORDER_HIGH];
  G.forBlock.greg_math_clamp=b=>['(function() local v, low, high = '+value(b,'VALUE','0')+', '+value(b,'LOW','0')+', '+value(b,'HIGH','1')+'; if low > high then low, high = high, low end; return math.max(low, math.min(high, v)) end)()',G.ORDER_HIGH];
  G.forBlock.greg_math_lerp=b=>['(function() local a, target, amount = '+value(b,'A','0')+', '+value(b,'B','1')+', '+value(b,'AMOUNT','0.5')+'; return a + (target - a) * amount end)()',G.ORDER_HIGH];
  G.forBlock.greg_math_map_range=b=>['(function() local v, in_low, in_high, out_low, out_high = '+value(b,'VALUE','0')+', '+value(b,'IN_LOW','0')+', '+value(b,'IN_HIGH','1')+', '+value(b,'OUT_LOW','0')+', '+value(b,'OUT_HIGH','1')+'; if in_low == in_high then return out_low end; return out_low + (v - in_low) * (out_high - out_low) / (in_high - in_low) end)()',G.ORDER_HIGH];
  G.forBlock.greg_math_distance=b=>['(function() local dx, dy = ('+value(b,'X2','0')+') - ('+value(b,'X1','0')+'), ('+value(b,'Y2','0')+') - ('+value(b,'Y1','0')+'); return '+deterministicSqrt('dx * dx + dy * dy')+' end)()',G.ORDER_HIGH];
  G.forBlock.greg_math_direction=b=>{const dx='('+value(b,'X2','0')+') - ('+value(b,'X1','0')+')',dy='('+value(b,'Y2','0')+') - ('+value(b,'Y1','0')+')';return [deterministicDirection(dx,dy),G.ORDER_HIGH];};
  G.forBlock.greg_terrain_box=b=>{if(!eventParent(b))throw Error('Terrain queries belong inside an event.');return ['map.solid_box('+value(b,'X')+', '+value(b,'Y')+', '+value(b,'WIDTH','1')+', '+value(b,'HEIGHT','1')+')',G.ORDER_HIGH];};
  G.forBlock.greg_terrain_ahead=b=>{entityContext(b);return ['(function() local body = entity.get(handle) local speed = math.max(math.abs(body.vx), math.abs(body.vy)) if speed == 0 then return false end local distance = '+value(b,'DISTANCE','9')+' return map.solid_box(body.x + body.vx / speed * distance, body.y + body.vy / speed * distance, 1, 1) end)()',G.ORDER_HIGH];};
  G.forBlock.greg_entity_reverse=b=>{entityContext(b);const axis=b.getFieldValue('AXIS');return 'entity.set(handle, {'+(axis==='both'?'vx = -entity.get(handle).vx, vy = -entity.get(handle).vy':axis+' = -entity.get(handle).'+axis)+'})\n';};
  G.forBlock.greg_object_at=b=>{if(!eventParent(b))throw Error('Point queries belong inside an event.');const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return ['(function() for _, candidate in ipairs(entity.at('+value(b,'X')+', '+value(b,'Y')+')) do if entity.type(candidate) == '+quote(key)+' then return true end end return false end)()',G.ORDER_HIGH];};
  G.forBlock.greg_coordinate_at=b=>{
    if(!eventParent(b))throw Error('Coordinate queries belong inside an event.');
    const target=b.getFieldValue('TARGET'),x=value(b,'X'),y=value(b,'Y');
    if(typeof target==='string'&&target.startsWith('tile:')&&validTile(target.slice(5)))return ['map.tile_at('+x+', '+y+') == '+quote(target.slice(5)),G.ORDER_HIGH];
    if(typeof target==='string'&&target.startsWith('object:')&&validType(target.slice(7))){const key=target.slice(7);return ['(function() for _, candidate in ipairs(entity.at('+x+', '+y+')) do if entity.type(candidate) == '+quote(key)+' then return true end end return false end)()',G.ORDER_HIGH];}
    throw Error('Choose a tile or object.');
  };
  G.forBlock.greg_time=()=>['map.tick()',G.ORDER_HIGH];
  G.forBlock.greg_break=b=>{let parent=b.getSurroundParent();while(parent){if(['controls_repeat_ext','controls_whileUntil','controls_for','controls_forEach','greg_players','greg_entities'].includes(parent.type))return 'break\n';if(['procedures_defnoreturn','procedures_defreturn'].includes(parent.type))break;parent=parent.getSurroundParent();}throw Error('Stop this loop belongs inside a repeat, while, for, player, or object loop.');};
  G.forBlock.greg_stop_script=b=>{let parent=b.getSurroundParent();while(parent){if(parent.type==='greg_entity_event'&&parent.getFieldValue('EVENT')==='damage_filter')return 'do return allow_damage end\n';if(['greg_tick','greg_timer_event','greg_player_action_event','greg_player_sprite_event','greg_player_lifecycle_event','greg_player_health_event','greg_entity_event','procedures_defnoreturn','procedures_defreturn'].includes(parent.type))return 'do return end\n';parent=parent.getSurroundParent();}throw Error('Stop this event or function belongs inside an event or function.');};
  G.forBlock.lists_create_with=b=>{if(!Number.isInteger(b.itemCount_)||b.itemCount_<0||b.itemCount_>32)throw Error('A list can contain at most 32 items.');const items=[];for(let i=0;i<b.itemCount_;i++){const item=G.valueToCode(b,'ADD'+i,G.ORDER_NONE);if(!item)throw Error('Connect a value to list item '+(i+1)+'.');items.push(item);}if(!items.length)return ['{}',G.ORDER_ATOMIC];return ['(function() local list = {'+items.join(', ')+'}; for i = 1, '+items.length+' do if list[i] == nil then error('+quote('List items cannot be empty.')+') end end; return list end)()',G.ORDER_HIGH];};
  G.forBlock.lists_length=b=>['(function() local list = '+value(b,'VALUE','{}')+'; if type(list) ~= '+quote('table')+' then error('+quote('List length needs a list.')+') end; if #list > 32 then error('+quote('Lists are limited to 32 items.')+') end; return #list end)()',G.ORDER_HIGH];
  G.forBlock.lists_isEmpty=b=>['(function() local list = '+value(b,'VALUE','{}')+'; if type(list) ~= '+quote('table')+' then error('+quote('List empty needs a list.')+') end; if #list > 32 then error('+quote('Lists are limited to 32 items.')+') end; return #list == 0 end)()',G.ORDER_HIGH];
  G.forBlock.lists_indexOf=b=>{const reverse=b.getFieldValue('END')==='LAST'?'true':'false';return ['(function() local list, wanted, reverse = '+value(b,'VALUE','{}')+', '+value(b,'FIND','nil')+', '+reverse+'; if type(list) ~= '+quote('table')+' then error('+quote('List search needs a list.')+') end; if #list > 32 then error('+quote('Lists are limited to 32 items.')+') end; if reverse then for i = #list, 1, -1 do if rawequal(list[i], wanted) then return i end end else for i = 1, #list do if rawequal(list[i], wanted) then return i end end end; return 0 end)()',G.ORDER_HIGH];};
  G.forBlock.lists_getIndex=b=>{const mode=b.getFieldValue('MODE'),where=b.getFieldValue('WHERE');if(mode!=='GET')throw Error('Beginner lists are read-only; choose get instead of remove.');let index;if(where==='FIRST')index='1';else if(where==='LAST')index='#list';else if(where==='FROM_START')index=value(b,'AT','1');else if(where==='FROM_END')index='#list - ('+value(b,'AT','1')+') + 1';else if(where==='RANDOM')index='(#list == 0 and 0 or map.random(1, #list))';else throw Error('Choose a valid list position.');return ['(function() local list = '+value(b,'VALUE','{}')+'; if type(list) ~= '+quote('table')+' then error('+quote('Get list item needs a list.')+') end; if #list > 32 then error('+quote('Lists are limited to 32 items.')+') end; local index = '+index+'; if index < 1 or index > #list then return nil end; return list[index] end)()',G.ORDER_HIGH];};
  G.forBlock.controls_forEach=(b,generator)=>{const variable=generator.getVariableName(b.getFieldValue('VAR')),list=value(b,'LIST','{}'),body=generator.statementToCode(b,'DO');return 'do\n  local list = '+list+'\n  if type(list) ~= '+quote('table')+' then error('+quote('For each item needs a list.')+') end\n  if #list > 32 then error('+quote('Lists are limited to 32 items.')+') end\n  for _, '+variable+' in ipairs(list) do\n'+body+'  end\nend\n';};
  const vectorExpression=(code,result)=>'(function() local vector = '+code+'; if type(vector) ~= '+quote('table')+' or type(vector.x) ~= '+quote('number')+' or type(vector.y) ~= '+quote('number')+' then error('+quote('Vector input needs numeric x and y values.')+') end; '+result+' end)()';
  G.forBlock.greg_vector_create=b=>['(function() local x, y = '+value(b,'X')+', '+value(b,'Y')+'; if type(x) ~= '+quote('number')+' or type(y) ~= '+quote('number')+' then error('+quote('Vector x and y must be numbers.')+') end; return {x = x, y = y} end)()',G.ORDER_HIGH];
  G.forBlock.greg_vector_component=b=>{const component=b.getFieldValue('COMPONENT');if(!['x','y'].includes(component))throw Error('Choose vector x or y.');return [vectorExpression(value(b,'VECTOR','{}'),'return vector.'+component),G.ORDER_HIGH];};
  G.forBlock.greg_vector_math=b=>{const op=b.getFieldValue('OP'),symbol=op==='add'?'+':op==='subtract'?'-':null;if(!symbol)throw Error('Choose add or subtract vectors.');const a=value(b,'A','{}'),c=value(b,'B','{}');return ['(function() local a, b = '+a+', '+c+'; if type(a) ~= '+quote('table')+' or type(b) ~= '+quote('table')+' or type(a.x) ~= '+quote('number')+' or type(a.y) ~= '+quote('number')+' or type(b.x) ~= '+quote('number')+' or type(b.y) ~= '+quote('number')+' then error('+quote('Vector math needs two vectors.')+') end; return {x = a.x '+symbol+' b.x, y = a.y '+symbol+' b.y} end)()',G.ORDER_HIGH];};
  G.forBlock.greg_vector_scale=b=>{const vector=value(b,'VECTOR','{}'),scale=value(b,'SCALE','1');return ['(function() local vector, scale = '+vector+', '+scale+'; if type(vector) ~= '+quote('table')+' or type(vector.x) ~= '+quote('number')+' or type(vector.y) ~= '+quote('number')+' or type(scale) ~= '+quote('number')+' then error('+quote('Scale vector needs a vector and number.')+') end; return {x = vector.x * scale, y = vector.y * scale} end)()',G.ORDER_HIGH];};
  G.forBlock.greg_vector_length=b=>[vectorExpression(value(b,'VECTOR','{}'),'return '+deterministicSqrt('vector.x * vector.x + vector.y * vector.y')),G.ORDER_HIGH];
  G.forBlock.greg_vector_normalize=b=>[vectorExpression(value(b,'VECTOR','{}'),'local length = '+deterministicSqrt('vector.x * vector.x + vector.y * vector.y')+'; if length == 0 then return {x = 0, y = 0} end; return {x = vector.x / length, y = vector.y / length}'),G.ORDER_HIGH];
  G.forBlock.greg_to_text=b=>['tostring('+value(b,'VALUE','nil')+')',G.ORDER_HIGH];
  G.forBlock.greg_to_boolean=b=>['not not ('+value(b,'VALUE','false')+')',G.ORDER_HIGH];
  G.forBlock.greg_to_number=b=>['(function() local input, fallback = '+value(b,'VALUE','nil')+', '+value(b,'FALLBACK','0')+'; if type(fallback) ~= '+quote('number')+' then error('+quote('Number conversion fallback must be a number.')+') end; if type(input) == '+quote('number')+' then return input end; if type(input) == '+quote('boolean')+' then return input and 1 or 0 end; if type(input) ~= '+quote('string')+' or #input == 0 then return fallback end; local first, sign, start, number, divisor, decimal, digits = string.byte(input, 1), 1, 1, 0, 1, false, 0; if first == 45 then sign, start = -1, 2 elseif first == 43 then start = 2 end; for i = start, #input do local byte = string.byte(input, i); if byte == 46 and not decimal then decimal = true elseif byte >= 48 and byte <= 57 then digits = digits + 1; if decimal then divisor = divisor * 10; number = number + (byte - 48) / divisor else number = number * 10 + (byte - 48) end else return fallback end end; number = number * sign; if digits == 0 or number ~= number or number - number ~= 0 then return fallback end; return number end)()',G.ORDER_HIGH];
  G.forBlock.greg_lua=b=>String(b.data||'')+'\n';
  G.forBlock.text_join=b=>{const parts=[];for(let i=0;i<b.itemCount_;i++)parts.push('tostring('+value(b,'ADD'+i,"''")+')');return [parts.length?'table.concat({'+parts.join(', ')+'})':"''",G.ORDER_HIGH];};
  G.forBlock.text_length=b=>['#tostring('+value(b,'VALUE',"''")+')',G.ORDER_HIGH];
  G.forBlock.text_isEmpty=b=>['#tostring('+value(b,'VALUE',"''")+') == 0',G.ORDER_HIGH];
  G.forBlock.text_indexOf=b=>{
    const reverse=b.getFieldValue('END')==='LAST'?'true':'false';
    return ['(function() local text, needle, reverse = tostring('+value(b,'VALUE',"''")+'), tostring('+value(b,'FIND',"''")+'), '+reverse+'; if #needle == 0 then return reverse and #text + 1 or 1 end; if #needle > #text then return 0 end; if reverse then for i = #text - #needle + 1, 1, -1 do if string.sub(text, i, i + #needle - 1) == needle then return i end end else for i = 1, #text - #needle + 1 do if string.sub(text, i, i + #needle - 1) == needle then return i end end end; return 0 end)()',G.ORDER_HIGH];
  };
  G.forBlock.text_charAt=b=>{
    const text='tostring('+value(b,'VALUE',"''")+')',where=b.getFieldValue('WHERE')||'FROM_START';
    if(where==='RANDOM')return ['(function() local text = '+text+'; if #text == 0 then return '+quote('')+' end; local index = map.random(1, #text); return string.sub(text, index, index) end)()',G.ORDER_HIGH];
    if(where==='FIRST')return ['string.sub('+text+', 1, 1)',G.ORDER_HIGH];
    if(where==='LAST')return ['string.sub('+text+', -1, -1)',G.ORDER_HIGH];
    const at=value(b,'AT','1'),index=where==='FROM_END'?'-('+at+')':at;
    return ['string.sub('+text+', '+index+', '+index+')',G.ORDER_HIGH];
  };
  G.forBlock.text_getSubstring=b=>{
    const startWhere=b.getFieldValue('WHERE1'),endWhere=b.getFieldValue('WHERE2');
    const start=startWhere==='FIRST'?'1':startWhere==='FROM_END'?'-('+value(b,'AT1','1')+')':value(b,'AT1','1');
    const end=endWhere==='LAST'?'-1':endWhere==='FROM_END'?'-('+value(b,'AT2','1')+')':value(b,'AT2','1');
    return ['string.sub(tostring('+value(b,'STRING',"''")+'), '+start+', '+end+')',G.ORDER_HIGH];
  };
  G.forBlock.text_changeCase=b=>{
    const mode=quote(b.getFieldValue('CASE')||'UPPERCASE');
    return ['(function() local text, mode, result, in_word = tostring('+value(b,'TEXT',"''")+'), '+mode+', {}, false; for i = 1, #text do local byte = string.byte(text, i); local space = byte == 32 or (byte >= 9 and byte <= 13); if mode == '+quote('UPPERCASE')+' or (mode == '+quote('TITLECASE')+' and not in_word) then if byte >= 97 and byte <= 122 then byte = byte - 32 end elseif mode == '+quote('LOWERCASE')+' or mode == '+quote('TITLECASE')+' then if byte >= 65 and byte <= 90 then byte = byte + 32 end end; result[i] = string.char(byte); in_word = not space end; return table.concat(result) end)()',G.ORDER_HIGH];
  };
  G.forBlock.text_trim=b=>{
    const mode=quote(b.getFieldValue('MODE')||'BOTH');
    return ['(function() local text, mode, first, last = tostring('+value(b,'TEXT',"''")+'), '+mode+', 1, nil; last = #text; local function space(index) local byte = string.byte(text, index); return byte == 32 or (byte >= 9 and byte <= 13) end; if mode ~= '+quote('RIGHT')+' then while first <= last and space(first) do first = first + 1 end end; if mode ~= '+quote('LEFT')+' then while last >= first and space(last) do last = last - 1 end end; return string.sub(text, first, last) end)()',G.ORDER_HIGH];
  };
  G.forBlock.text_reverse=b=>['string.reverse(tostring('+value(b,'TEXT',"''")+'))',G.ORDER_HIGH];
  const category=(name,colour,types)=>({kind:'category',name,colour,contents:types.map(type=>({kind:'block',type}))});
  const textBlocks=['text','text_join','text_length','text_isEmpty','text_indexOf','text_charAt','text_getSubstring','text_changeCase','text_trim','text_reverse'];
  const listBlocks=['lists_create_with','lists_length','lists_isEmpty','lists_getIndex','lists_indexOf','controls_forEach'];
  const dataBlocks=listBlocks.concat(['greg_vector_create','greg_vector_component','greg_vector_math','greg_vector_scale','greg_vector_length','greg_vector_normalize','greg_to_text','greg_to_boolean','greg_to_number']);
  const standardBlocks=['math_number','controls_if','logic_compare','logic_operation','logic_boolean','logic_negate','controls_repeat_ext','controls_whileUntil','controls_for','variables_get','procedures_defnoreturn','procedures_defreturn','procedures_callnoreturn','procedures_callreturn','procedures_ifreturn'].concat(textBlocks,listBlocks);
  const toolbox={kind:'categoryToolbox',contents:[category('Events',35,['greg_tick','greg_timer_event','greg_player_action_event','greg_player_sprite_event','greg_player_lifecycle_event','greg_player_health_event','greg_entity_event']),category('Players',210,['greg_players','greg_player_reference','greg_player_reference_exists','greg_player_reference_number','greg_player_reference_text','greg_player_reference_condition','greg_player_reference_velocity','greg_player_reference_position','greg_player_reference_damage','greg_player_reference_defeat','greg_player_value','greg_player_text','greg_player_condition','greg_player_action','greg_player_touching','greg_player_hit_object','greg_velocity','greg_player_position','greg_player_tint','greg_player_tint_reset','greg_player_visibility','greg_player_presentation_reset','greg_player_sprite','greg_player_sprite_is','greg_player_sprite_finished','greg_player_sprite_number','greg_player_sprite_tint','greg_player_sprite_layer','greg_player_sprite_mirrored','greg_player_sprite_visibility','greg_player_sprite_transform_reset','greg_player_sprite_clear','greg_player_health_enabled','greg_player_health_enable','greg_player_health_disable','greg_player_invulnerable_set','greg_player_invulnerable','greg_player_health_set','greg_player_health_change','greg_player_heal','greg_player_damage','greg_defeat']),category('Objects',210,['greg_entity_handle','greg_object_named_value','greg_object_reference_exists','greg_object_reference_type','greg_object_reference_number','greg_object_reference_set','greg_object_reference_remove','greg_entity_set','greg_entity_get','greg_entity_flag','greg_entity_layer','greg_entity_tint','greg_entity_tint_reset','greg_entity_animation','greg_entity_animation_is','greg_entity_animation_finished','greg_entities','greg_entity_named','greg_entity_spawn','greg_entity_spawn_value','greg_entity_health_get','greg_entity_health_enabled','greg_entity_health_enable','greg_entity_health_disable','greg_entity_invulnerable_set','greg_entity_invulnerable','greg_entity_health_set','greg_entity_health_change','greg_entity_heal','greg_entity_remove','greg_entity_damage','greg_damage_filter_set','greg_damage_amount','greg_damage_applied','greg_damage_blocked','greg_health_event_value','greg_health_event_reason','greg_health_event_defeated','greg_damage_has_source','greg_damage_source_is','greg_damage_native_source_is','greg_damage_source_player','greg_entity_signal','greg_entity_signal_named','greg_signal_name','greg_signal_value','greg_signal_has_source','greg_signal_source_is','greg_contact_other_is','greg_contact_area','greg_contact_other_area']),category('Variables',280,['greg_variable_get','greg_variable_set','greg_variable_change','greg_variable_clear','greg_variable_exists']),category('Game',120,['greg_time','greg_every','greg_timer_start','greg_timer_repeat','greg_timer_cancel','greg_timer_remaining','greg_timer_active','greg_camera_point','greg_camera_clear','greg_camera_number','greg_camera_active','greg_exit_lock','greg_exit_locked','greg_room_count','greg_room_start','greg_room_number','greg_room_text','greg_room_condition','greg_player_room_move','greg_coordinate_at']),category('Logic',120,['controls_if','logic_compare','logic_operation','logic_boolean','logic_negate','controls_repeat_ext','controls_whileUntil','controls_for','greg_break','greg_stop_script']),category('Data',65,dataBlocks),category('Text',160,textBlocks),category('Math',230,['math_number','greg_math','greg_math_function','greg_math_bound','greg_math_clamp','greg_math_lerp','greg_math_map_range','greg_math_distance','greg_math_direction','greg_random']),{kind:'category',name:'Functions',colour:290,custom:'PROCEDURE'}]};
  toolbox.contents.find(c=>c.name==='Variables').contents.unshift({kind:'button',text:'Create variable...',callbackKey:'GREG_CREATE_VARIABLE'});
  function generate(workspace){
    const supported=new Set(definitions.map(d=>d.type).concat(standardBlocks));
    if(workspace.getAllBlocks(false).some(b=>!supported.has(b.type)))throw Error('Unsupported block in this workspace.');
    if(workspace.getAllBlocks(false).length>512)throw Error("Block workspace exceeds 512 blocks.");
    const procedureDefinitions=new Map(),lexicalVariables=new Set();
    for(const block of workspace.getAllBlocks(false)){
      if(['procedures_defnoreturn','procedures_defreturn'].includes(block.type)){
        const name=String(block.getFieldValue('NAME')||'').trim(),normalized=name.toLowerCase();
        if(!name)throw Error('Function names cannot be empty.');
        if(procedureDefinitions.has(normalized))throw Error('Function names must be unique.');
        procedureDefinitions.set(normalized,block.type);
        for(const model of block.getVarModels())lexicalVariables.add(model.getId());
      }
      if(block.type==='controls_for'||block.type==='controls_forEach')lexicalVariables.add(block.getFieldValue('VAR'));
    }
    for(const block of workspace.getAllBlocks(false)){
      if(block.type==='variables_get'&&!lexicalVariables.has(block.getFieldValue('VAR')))throw Error('Function parameter getters and for-loop counters belong to a matching function or loop. Use the Greggnogg map, player, or object variable blocks for saved values.');
      if(block.type==='procedures_callnoreturn'||block.type==='procedures_callreturn'){
        const name=String(block.getFieldValue('NAME')||'').trim(),definition=procedureDefinitions.get(name.toLowerCase()),expected=block.type==='procedures_callreturn'?'procedures_defreturn':'procedures_defnoreturn';
        if(definition!==expected)throw Error('Function calls need a matching function definition in this block workspace.');
      }
    }
    const events=new Set();
    for(const block of workspace.getTopBlocks(true)){
      if(!['greg_tick','greg_timer_event','greg_player_action_event','greg_player_sprite_event','greg_player_lifecycle_event','greg_player_health_event','greg_entity_event','greg_lua','greg_state_set','procedures_defnoreturn','procedures_defreturn'].includes(block.type))throw Error('Connect actions and values inside an event or function block.');
      const key=block.type==='greg_tick'?'tick':block.type==='greg_player_action_event'?'action:'+block.getFieldValue('PLAYER')+':'+block.getFieldValue('EDGE')+':'+block.getFieldValue('ACTION'):block.type==='greg_player_sprite_event'?'picture-finished:'+block.getFieldValue('PLAYER'):block.type==='greg_player_lifecycle_event'?'lifecycle:'+block.getFieldValue('PLAYER')+':'+block.getFieldValue('EVENT'):block.type==='greg_timer_event'?'timer:'+timerName(block):block.type==='greg_player_health_event'?'player:'+block.getFieldValue('EVENT'):block.type==='greg_entity_event'?block.getFieldValue('TYPE')+':'+block.getFieldValue('EVENT'):null;
      if(key&&events.has(key))throw Error('Only one block is allowed for each event.');if(key)events.add(key);
    }
    const registered=new Set(workspace.getTopBlocks(true).filter(block=>block.type==='greg_timer_event').map(timerName));
    if(registered.size>32)throw Error('A map can have at most 32 timers.');
    for(const block of workspace.getAllBlocks(false))if(['greg_timer_start','greg_timer_repeat','greg_timer_cancel','greg_timer_remaining','greg_timer_active'].includes(block.type)&&!registered.has(timerName(block)))throw Error('Add a matching timer-finished event before using timer "'+timerName(block)+'".');
    const top=workspace.getTopBlocks(true),tickEvents=top.filter(block=>block.type==='greg_tick'||block.type==='greg_player_action_event'||block.type==='greg_player_sprite_event'||block.type==='greg_player_lifecycle_event');
    let code;
    if(top.length===1&&top[0].type==='greg_lua'&&!top[0].getNextBlock())code=String(top[0].data||'');
    else if(tickEvents.length<=1)code=G.workspaceToCode(workspace);
    else {
      G.init(workspace);
      let emittedTick=false;code='';
      for(const block of top){
        if(block.type==='greg_tick'||block.type==='greg_player_action_event'||block.type==='greg_player_sprite_event'||block.type==='greg_player_lifecycle_event'){
          if(emittedTick)continue;emittedTick=true;
          code+='map.on_tick(function()\n';
          for(const event of tickEvents)code+=event.type==='greg_tick'?tickBody(event):event.type==='greg_player_action_event'?playerActionBody(event):event.type==='greg_player_sprite_event'?playerSpriteBody(event):playerLifecycleBody(event);
          code+='end)\n';
        } else {const generated=G.blockToCode(block);if(generated)code+=generated;}
      }
      code=G.finish(code);
    }
    if(new TextEncoder().encode(code).length>262144)throw Error('Map logic exceeds 256 KiB.');return code;
  }
  function restore(workspace,saved,source){
    let valid=false;
    if(saved&&saved.source===source){
      const check=new B.Workspace();
      try{
        if(JSON.stringify(saved.workspace).length>1048576)throw Error('Block layout exceeds 1 MiB.');
        B.serialization.workspaces.load(saved.workspace,check);
        const allowed=new Set(definitions.map(d=>d.type).concat(standardBlocks));
        if(check.getAllBlocks(false).length>512||check.getAllBlocks(false).some(b=>!allowed.has(b.type)))throw Error('Unsupported or oversized block layout.');
        valid=generate(check)===source;
      }catch(error){valid=false;}finally{check.dispose();}
    }
    workspace.clear();
    if(valid){B.serialization.workspaces.load(saved.workspace,workspace);return true;}
    if(source){const block=workspace.newBlock('greg_lua');block.data=source;if(block.initSvg){block.initSvg();block.render();}}
    return false;
  }
  function saveCallback(workspace,key,event){
    const tops=workspace.getTopBlocks(true),root=tops.find(block=>block.type==='greg_entity_event'),procedures=tops.filter(block=>['procedures_defnoreturn','procedures_defreturn'].includes(block.type));
    if(!root||tops.length!==procedures.length+1||root.getFieldValue('TYPE')!==key||root.getFieldValue('EVENT')!==event)throw Error('Connect object actions to the supplied event; reusable functions may remain beside it.');
    let code;
    callbackProcedureContext=true;
    try{code=generate(workspace);}finally{callbackProcedureContext=false;}
    const child=root.getInputTargetBlock('DO');let source;
    if(!procedures.length)source=!child?'':child.type==='greg_lua'&&!child.getNextBlock()?String(child.data||''):code.slice(code.indexOf('\n')+1,code.lastIndexOf('end)')).replace(/^  /gm,'').replace(/\n$/,'');
    else {
      callbackProcedureContext=true;
      try{
        G.init(workspace);
        for(const procedure of procedures)G.blockToCode(procedure);
        const body=G.statementToCode(root,'DO').replace(/^  /gm,'').replace(/\n$/,''),definitions=G.finish('').trim();
        source=(definitions+(definitions&&body?'\n\n':'')+body).trim();
      }finally{callbackProcedureContext=false;}
    }
    return {source,workspace:B.serialization.workspaces.save(workspace)};
  }
  function restoreCallback(workspace,saved,source,key,event){
    workspace.clear();let restored=false;
    if(saved&&saved.source===source&&JSON.stringify(saved.workspace).length<=1048576){
      try{B.serialization.workspaces.load(saved.workspace,workspace);restored=saveCallback(workspace,key,event).source===source;}catch(error){restored=false;}
    }
    if(!restored){
      workspace.clear();const root=workspace.newBlock('greg_entity_event');root.setFieldValue(key,'TYPE');root.setFieldValue(event,'EVENT');
      if(source){const raw=workspace.newBlock('greg_lua');raw.data=source;root.getInput('DO').connection.connect(raw.previousConnection);}
    }
    const root=workspace.getTopBlocks().find(block=>block.type==='greg_entity_event');root.setDeletable(false);root.setEditable(false);
    for(const block of workspace.getAllBlocks(false)){if(block.initSvg){block.initSvg();block.render();}}
    return restored;
  }
  function renameReference(saved,source,old,key,callbackKey,event){
    if(!saved||saved.source!==source)return null;
    const workspace=new B.Workspace();
    try{
      if(JSON.stringify(saved.workspace).length>1048576)return null;
      B.serialization.workspaces.load(saved.workspace,workspace);
      const original=callbackKey?saveCallback(workspace,callbackKey,event).source:generate(workspace);
      if(original!==source)return null;
      for(const block of workspace.getAllBlocks(false)){
        if(['greg_entity_event','greg_entity_spawn','greg_entity_spawn_value','greg_entities','greg_object_at'].includes(block.type)&&block.getFieldValue('TYPE')===old)block.setFieldValue(key,'TYPE');
        if(block.type==='greg_coordinate_at'&&block.getFieldValue('TARGET')==='object:'+old)block.setFieldValue('object:'+key,'TARGET');
      }
      return callbackKey?saveCallback(workspace,callbackKey===old?key:callbackKey,event):save(workspace);
    }catch(error){return null;}finally{workspace.dispose();}
  }
  function bindDropdownInput(container,workspace){
    if(workspace.registerButtonCallback)workspace.registerButtonCallback('GREG_CREATE_VARIABLE',()=>{
      B.dialog.prompt('Variable name (1?20 letters, digits or underscores):','variable',name=>{
        if(name===null)return;name=name.trim();
        if(!/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name)){B.dialog.alert('Use 1?20 letters, digits or underscores, starting with a letter or underscore.');return;}
        workspace.getVariableMap().createVariable(name);
      });
    });
    let pending=null;
    function press(event){
      if(event.button!==undefined&&event.button!==0)return;
      for(const block of workspace.getAllBlocks(false))for(const input of block.inputList)for(const field of input.fieldRow){
        if(!(field instanceof B.FieldDropdown)||!field.isClickable())continue;
        const root=field.getSvgRoot();if(!root||!root.contains(event.target))continue;
        event.preventDefault();event.stopImmediatePropagation();
        workspace.cancelCurrentGesture();
        pending={field,pointerId:event.pointerId};return;
      }
    }
    function release(event){
      if(!pending||pending.pointerId!==event.pointerId)return;
      const field=pending.field;pending=null;
      event.preventDefault();event.stopImmediatePropagation();
      workspace.cancelCurrentGesture();
      if(workspace.markFocused)workspace.markFocused();
      if(container.closest&&B.setParentContainer)B.setParentContainer(container.closest('dialog')||container);
      if(B.getFocusManager&&container.closest)B.getFocusManager().setPopoverFocusRoot(container.closest('dialog')||container);
      if(B.Touch&&B.Touch.clearTouchIdentifier)B.Touch.clearTouchIdentifier();
      if(field.getSourceBlock()&&!field.getSourceBlock().isDisposed())field.showEditor(event);
    }
    function cancel(){pending=null;}
    container.addEventListener('pointerdown',press,true);
    container.addEventListener('pointerup',release,true);
    container.addEventListener('pointercancel',cancel,true);
    return ()=>{container.removeEventListener('pointerdown',press,true);container.removeEventListener('pointerup',release,true);container.removeEventListener('pointercancel',cancel,true);pending=null;};
  }

  let theme;
  function getTheme(){return theme||(theme=B.Theme.defineTheme('greggnogg',{base:B.Themes.Classic,componentStyles:{workspaceBackgroundColour:'#21181b',toolboxBackgroundColour:'#181215',toolboxForegroundColour:'#f0e4dc',flyoutBackgroundColour:'#302329',flyoutForegroundColour:'#f0e4dc',flyoutOpacity:1,scrollbarColour:'#91747e',scrollbarOpacity:0.7,insertionMarkerColour:'#ffffff',insertionMarkerOpacity:0.3,cursorColour:'#f4aa75'},fontStyle:{family:'monospace',weight:'normal',size:12}}));}
  function save(workspace){return {source:generate(workspace),workspace:B.serialization.workspaces.save(workspace)};}
  return {toolbox,generate,restore,save,setObjectTypes,setPlacements,setTileTypes,setRoomConnections,saveCallback,restoreCallback,renameReference,getTheme,bindDropdownInput};
}));
