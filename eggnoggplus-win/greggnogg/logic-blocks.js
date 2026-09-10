(function(root,factory){
  if(typeof module==='object'&&module.exports){const B=require('./vendor/blockly/blockly_compressed.js');require('./vendor/blockly/blocks_compressed.js');B.setLocale(require('./vendor/blockly/msg/en.js'));module.exports=factory(B,require('./vendor/blockly/lua_compressed.js').luaGenerator);}
  else root.GregLogicBlocks=factory(root.Blockly,root.lua.luaGenerator);
}(typeof globalThis!=='undefined'?globalThis:this,function(B,G){
  'use strict';
  let objectTypes=[];
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
    objectTypes=(types||[]).filter(type=>validType(type.key)).map(type=>({key:type.key,label:type.label||type.key}));
  }
  const definitions=[
    {type:'greg_tick',message0:'every game tick %1 %2',args0:[{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_entity_event',message0:'when object %1 %2 %3 %4',args0:[{type:'field_greg_object',name:'TYPE'},{type:'field_dropdown',name:'EVENT',options:[['updates','update'],['is created','spawn'],['is removed','remove']]},{type:'input_dummy'},{type:'input_statement',name:'DO'}],colour:35},
    {type:'greg_players',message0:'for each active player %1 %2',args0:[{type:'input_dummy'},{type:'input_statement',name:'DO'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_player_value',message0:'this player %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['number','player'],['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['contact radius','contact_radius']]}],output:'Number',colour:210},
    {type:'greg_player_touching',message0:'this player touches this object %1',args0:[{type:'field_dropdown',name:'ROLE',options:[['any area','any'],['sensor','sensor'],['body','body'],['attack area','hitbox'],['damage receiver','hurtbox']]}],output:'Boolean',colour:120},
    {type:'greg_velocity',message0:'set player %1 velocity x %2 y %3',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_defeat',message0:'defeat player %1',args0:[{type:'field_dropdown',name:'PLAYER',options:[['1','1'],['2','2'],['this player','current']]}],previousStatement:null,nextStatement:null,colour:0},
    {type:'greg_entity_set',message0:'set this object %1 to %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['animation tick','animation_tick']]},{type:'input_value',name:'VALUE',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_get',message0:'this object %1',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['x','x'],['y','y'],['horizontal speed','vx'],['vertical speed','vy'],['animation tick','animation_tick']]}],output:'Number',colour:210},
    {type:'greg_entity_flag',message0:'set this object %1 to %2',args0:[{type:'field_dropdown',name:'PROPERTY',options:[['animation paused','animation_paused'],['flipped horizontally','mirrored']]},{type:'input_value',name:'VALUE',check:'Boolean'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entities',message0:'for each object %1 %2 %3',args0:[{type:'field_greg_object',name:'TYPE'},{type:'input_dummy'},{type:'input_statement',name:'DO'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_spawn',message0:'create object %1 at x %2 y %3',args0:[{type:'field_greg_object',name:'TYPE'},{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'}],previousStatement:null,nextStatement:null,colour:210},
    {type:'greg_entity_remove',message0:'remove this object',previousStatement:null,colour:210},
    {type:'greg_state_set',message0:'set map value %1 to %2',args0:[{type:'field_input',name:'KEY',text:'counter'},{type:'input_value',name:'VALUE'}],previousStatement:null,nextStatement:null,colour:280},
    {type:'greg_state_get',message0:'map value %1',args0:[{type:'field_input',name:'KEY',text:'counter'}],output:null,colour:280},
    {type:'greg_math',message0:'%1 %2 %3',args0:[{type:'input_value',name:'A',check:'Number'},{type:'field_dropdown',name:'OP',options:[['+','+'],['subtract','-'],['multiply','*'],['divide','/'],['remainder','%']]},{type:'input_value',name:'B',check:'Number'}],output:'Number',colour:230},
    {type:'greg_every',message0:'every %1 ticks',args0:[{type:'input_value',name:'INTERVAL',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_random',message0:'random integer from %1 to %2',args0:[{type:'input_value',name:'LOW',check:'Number'},{type:'input_value',name:'HIGH',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_function',message0:'%1 of %2',args0:[{type:'field_dropdown',name:'FUNCTION',options:[['absolute value','abs'],['round down','floor'],['round up','ceil']]},{type:'input_value',name:'VALUE',check:'Number'}],output:'Number',colour:230},
    {type:'greg_math_bound',message0:'%1 of %2 and %3',args0:[{type:'field_dropdown',name:'FUNCTION',options:[['minimum','min'],['maximum','max']]},{type:'input_value',name:'A',check:'Number'},{type:'input_value',name:'B',check:'Number'}],output:'Number',colour:230},
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
    {type:'greg_terrain_box',message0:'solid terrain at x %1 y %2 width %3 height %4',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'input_value',name:'WIDTH',check:'Number'},{type:'input_value',name:'HEIGHT',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_terrain_ahead',message0:'solid terrain %1 pixels ahead of this object',args0:[{type:'input_value',name:'DISTANCE',check:'Number'}],output:'Boolean',colour:120},
    {type:'greg_entity_reverse',message0:'reverse this object %1',args0:[{type:'field_dropdown',name:'AXIS',options:[['horizontal direction','vx'],['vertical direction','vy'],['both directions','both']]}],previousStatement:null,nextStatement:null,colour:210}
  );
  definitions.push({type:'greg_object_at',message0:'object at x %1 y %2 is %3',args0:[{type:'input_value',name:'X',check:'Number'},{type:'input_value',name:'Y',check:'Number'},{type:'field_greg_object',name:'TYPE'}],output:'Boolean',colour:120});
  B.defineBlocksWithJsonArray(definitions);
  const value=(b,name,fallback='0')=>G.valueToCode(b,name,G.ORDER_NONE)||fallback;
  const quote=text=>G.quote_(String(text));
  function eventParent(block){let p=block.getSurroundParent();while(p&&!['greg_tick','greg_entity_event'].includes(p.type))p=p.getSurroundParent();return p;}
  function entityContext(block){let p=block.getSurroundParent();while(p){if(p.type==='greg_entities')return;if(['greg_tick','greg_entity_event'].includes(p.type))break;p=p.getSurroundParent();}const event=eventParent(block);if(!event||event.type!=='greg_entity_event'||event.getFieldValue('EVENT')==='remove')throw Error('This object block belongs inside an object update or creation event.');}
  function playerContext(block){const event=eventParent(block);if(!event||(event.type==='greg_entity_event'&&event.getFieldValue('EVENT')!=='update'))throw Error('Player actions belong inside a game tick or object update event.');}
  function currentPlayer(block){let p=block.getSurroundParent();while(p){if(p.type==='greg_players')return 'player';p=p.getSurroundParent();}throw Error('This player belongs inside a for-each-player block.');}
  function playerNumber(block){return block.getFieldValue('PLAYER')==='current'?currentPlayer(block)+'.player':block.getFieldValue('PLAYER');}
  G.forBlock.greg_players=b=>{if(!eventParent(b))throw Error('Player queries belong inside an event.');return 'for _, player in ipairs(map.players()) do\n'+G.statementToCode(b,'DO')+'end\n';};
  G.forBlock.greg_player_value=b=>[currentPlayer(b)+'.'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];
  G.forBlock.greg_player_touching=b=>{
    entityContext(b);const player=currentPlayer(b),role=b.getFieldValue('ROLE');
    return ['(function() for _, region in ipairs(entity.regions(handle)) do if '+(role==='any'?'true':'region.role == '+quote(role))+' then local dx = math.max(region.world_x - '+player+'.x, 0, '+player+'.x - region.world_x - region.width) local dy = math.max(region.world_y - '+player+'.y, 0, '+player+'.y - region.world_y - region.height) if dx * dx + dy * dy <= '+player+'.contact_radius * '+player+'.contact_radius then return true end end end return false end)()',G.ORDER_HIGH];
  };
  G.forBlock.greg_tick=b=>'map.on_tick(function()\n'+G.statementToCode(b,'DO')+'end)\n';
  G.forBlock.greg_entity_event=b=>{const key=b.getFieldValue('TYPE');if(!/^[a-z][a-z0-9_.-]*:[a-z][a-z0-9_.-]*$/.test(key)||key.length>96)throw Error('Choose a valid object type.');return 'entity.on_'+b.getFieldValue('EVENT')+'('+quote(key)+', function(handle, value)\n'+G.statementToCode(b,'DO')+'end)\n';};
  G.forBlock.greg_velocity=b=>{playerContext(b);return 'map.set_player_velocity('+playerNumber(b)+', '+value(b,'X')+', '+value(b,'Y')+')\n';};
  G.forBlock.greg_defeat=b=>{playerContext(b);return 'map.defeat_player('+playerNumber(b)+')\n';};
  G.forBlock.greg_entity_set=b=>{entityContext(b);return 'entity.set(handle, {'+b.getFieldValue('PROPERTY')+' = '+value(b,'VALUE')+'})\n';};
  G.forBlock.greg_entity_flag=b=>{entityContext(b);return 'entity.set(handle, {'+b.getFieldValue('PROPERTY')+' = '+value(b,'VALUE','false')+'})\n';};
  G.forBlock.greg_entities=b=>{if(!eventParent(b))throw Error('Object queries belong inside an event.');const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return 'for _, handle in ipairs(entity.list()) do\n  if entity.exists(handle) and entity.type(handle) == '+quote(key)+' then\n'+G.statementToCode(b,'DO')+'  end\nend\n';};
  G.forBlock.greg_entity_spawn=b=>{if(!eventParent(b))throw Error('Create objects inside an event block.');const key=b.getFieldValue('TYPE');if(!/^[a-z][a-z0-9_.-]*:[a-z][a-z0-9_.-]*$/.test(key)||key.length>96)throw Error('Choose a valid object type.');return 'entity.spawn('+quote(key)+', {x = '+value(b,'X')+', y = '+value(b,'Y')+'})\n';};
  G.forBlock.greg_entity_remove=b=>{entityContext(b);return 'entity.remove(handle)\n';};
  G.forBlock.greg_entity_get=b=>{entityContext(b);return ['entity.get(handle).'+b.getFieldValue('PROPERTY'),G.ORDER_HIGH];};
  function variableKey(b){
    const name=b.getFieldValue('NAME'),scope=b.getFieldValue('SCOPE');
    if(!/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name))throw Error('Variable names need 1?20 letters, digits or underscores, starting with a letter or underscore.');
    if(scope==='object'){entityContext(b);if(name.length>12)throw Error('Object variable names are limited to 12 characters.');return 'map.state['+quote('o:')+' .. handle .. '+quote(':'+name)+']';}
    const prefix=scope==='map'?quote('v:m:'+name):scope==='current'?quote('v:p:')+' .. '+currentPlayer(b)+'.player .. '+quote(':'+name):quote('v:p:'+scope+':'+name);
    return 'map.state['+prefix+']';
  }
  G.forBlock.greg_variable_get=b=>{const k=variableKey(b);return ['('+k+' == nil and 0 or '+k+')',G.ORDER_HIGH];};
  G.forBlock.greg_variable_set=b=>variableKey(b)+' = '+value(b,'VALUE')+'\n';
  G.forBlock.greg_variable_change=b=>{const k=variableKey(b);return k+' = ('+k+' or 0) + ('+value(b,'VALUE')+')\n';};
  G.forBlock.greg_variable_clear=b=>variableKey(b)+' = nil\n';
  G.forBlock.greg_variable_exists=b=>['('+variableKey(b)+' ~= nil)',G.ORDER_HIGH];
  G.forBlock.greg_state_set=b=>'map.state['+quote(b.getFieldValue('KEY'))+'] = '+value(b,'VALUE')+'\n';
  G.forBlock.greg_state_get=b=>{const key='map.state['+quote(b.getFieldValue('KEY'))+']';return ['('+key+' == nil and 0 or '+key+')',G.ORDER_HIGH];};
  G.forBlock.greg_math=b=>['('+value(b,'A')+' '+b.getFieldValue('OP')+' '+value(b,'B')+')',G.ORDER_HIGH];
  G.forBlock.greg_every=b=>['map.every('+value(b,'INTERVAL','60')+')',G.ORDER_HIGH];
  G.forBlock.greg_random=b=>{if(!eventParent(b))throw Error('Random numbers belong inside an event.');return ['map.random('+value(b,'LOW','1')+', '+value(b,'HIGH','10')+')',G.ORDER_HIGH];};
  G.forBlock.greg_math_function=b=>['math.'+b.getFieldValue('FUNCTION')+'('+value(b,'VALUE')+')',G.ORDER_HIGH];
  G.forBlock.greg_math_bound=b=>['math.'+b.getFieldValue('FUNCTION')+'('+value(b,'A')+', '+value(b,'B')+')',G.ORDER_HIGH];
  G.forBlock.greg_terrain_box=b=>{if(!eventParent(b))throw Error('Terrain queries belong inside an event.');return ['map.solid_box('+value(b,'X')+', '+value(b,'Y')+', '+value(b,'WIDTH','1')+', '+value(b,'HEIGHT','1')+')',G.ORDER_HIGH];};
  G.forBlock.greg_terrain_ahead=b=>{entityContext(b);return ['(function() local body = entity.get(handle) local speed = math.max(math.abs(body.vx), math.abs(body.vy)) if speed == 0 then return false end local distance = '+value(b,'DISTANCE','9')+' return map.solid_box(body.x + body.vx / speed * distance, body.y + body.vy / speed * distance, 1, 1) end)()',G.ORDER_HIGH];};
  G.forBlock.greg_entity_reverse=b=>{entityContext(b);const axis=b.getFieldValue('AXIS');return 'entity.set(handle, {'+(axis==='both'?'vx = -entity.get(handle).vx, vy = -entity.get(handle).vy':axis+' = -entity.get(handle).'+axis)+'})\n';};
  G.forBlock.greg_object_at=b=>{if(!eventParent(b))throw Error('Point queries belong inside an event.');const key=b.getFieldValue('TYPE');if(!validType(key))throw Error('Choose an object type.');return ['(function() for _, candidate in ipairs(entity.at('+value(b,'X')+', '+value(b,'Y')+')) do if entity.type(candidate) == '+quote(key)+' then return true end end return false end)()',G.ORDER_HIGH];};
  G.forBlock.greg_time=()=>['map.tick()',G.ORDER_HIGH];
  G.forBlock.greg_lua=b=>String(b.data||'')+'\n';
  const category=(name,colour,types)=>({kind:'category',name,colour,contents:types.map(type=>({kind:'block',type}))});
  const toolbox={kind:'categoryToolbox',contents:[category('Events',35,['greg_tick','greg_entity_event']),category('Players',210,['greg_players','greg_player_value','greg_player_touching','greg_velocity','greg_defeat']),category('Objects',210,['greg_entity_set','greg_entity_get','greg_entity_flag','greg_entities','greg_entity_spawn','greg_entity_remove']),category('Variables',280,['greg_variable_get','greg_variable_set','greg_variable_change','greg_variable_clear','greg_variable_exists']),category('Game',120,['greg_time','greg_every','greg_object_at','greg_terrain_box','greg_terrain_ahead']),category('Logic',120,['controls_if','logic_compare','logic_operation','logic_boolean','logic_negate','controls_repeat_ext']),category('Math',230,['math_number','greg_math','greg_math_function','greg_math_bound','greg_random'])]};
  toolbox.contents.find(c=>c.name==='Variables').contents.unshift({kind:'button',text:'Create variable...',callbackKey:'GREG_CREATE_VARIABLE'});
  function generate(workspace){
    const supported=new Set(definitions.map(d=>d.type).concat(['math_number','controls_if','logic_compare','logic_operation','logic_boolean','logic_negate','controls_repeat_ext']));
    if(workspace.getAllBlocks(false).some(b=>!supported.has(b.type)))throw Error('Unsupported block in this workspace.');
    if(workspace.getAllBlocks(false).length>512)throw Error("Block workspace exceeds 512 blocks.");
    const events=new Set();
    for(const block of workspace.getTopBlocks(true)){
      if(!['greg_tick','greg_entity_event','greg_lua','greg_state_set'].includes(block.type))throw Error('Connect actions and values inside an event block.');
      const key=block.type==='greg_tick'?'tick':block.type==='greg_entity_event'?block.getFieldValue('TYPE')+':'+block.getFieldValue('EVENT'):null;
      if(key&&events.has(key))throw Error('Only one block is allowed for each event.');if(key)events.add(key);
    }
    const top=workspace.getTopBlocks(true);const code=top.length===1&&top[0].type==='greg_lua'&&!top[0].getNextBlock()?String(top[0].data||''):G.workspaceToCode(workspace);if(new TextEncoder().encode(code).length>262144)throw Error('Map logic exceeds 256 KiB.');return code;
  }
  function restore(workspace,saved,source){
    let valid=false;
    if(saved&&saved.source===source){
      const check=new B.Workspace();
      try{
        if(JSON.stringify(saved.workspace).length>1048576)throw Error('Block layout exceeds 1 MiB.');
        B.serialization.workspaces.load(saved.workspace,check);
        const allowed=new Set(definitions.map(d=>d.type).concat(['math_number','controls_if','logic_compare','logic_operation','logic_boolean','logic_negate','controls_repeat_ext']));
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
    const tops=workspace.getTopBlocks(true);
    if(tops.length!==1||tops[0].type!=='greg_entity_event'||tops[0].getFieldValue('TYPE')!==key||tops[0].getFieldValue('EVENT')!==event)throw Error('Connect object actions to the supplied event.');
    const code=generate(workspace),child=tops[0].getInputTargetBlock('DO');
    const source=child&&child.type==='greg_lua'&&!child.getNextBlock()?String(child.data||''):code.slice(code.indexOf('\n')+1,code.lastIndexOf('end)')).replace(/^  /gm,'').replace(/\n$/,'');
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
    const root=workspace.getTopBlocks()[0];root.setDeletable(false);root.setEditable(false);
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
      for(const block of workspace.getAllBlocks(false))if(['greg_entity_event','greg_entity_spawn','greg_entities','greg_object_at'].includes(block.type)&&block.getFieldValue('TYPE')===old)block.setFieldValue(key,'TYPE');
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
  return {toolbox,generate,restore,save,setObjectTypes,saveCallback,restoreCallback,renameReference,getTheme,bindDropdownInput};
}));
