(function(){
 'use strict';
 window.GregObjectLogic={mount(host,doc,key,report){
  const callbacks=(doc.objectScripts[key]||={}),metadata=((doc.objectBlocks||={})[key]||={});
  let event='update',mode='advanced',busy=false;
  const validVariable=name=>typeof name==='string'&&/^[A-Za-z_][A-Za-z0-9_]{0,19}$/.test(name);
  const sharedVariables=new Set();
  function rememberSerializedVariables(saved){
    const variables=saved&&saved.workspace&&saved.workspace.variables;
    if(!Array.isArray(variables))return;
    for(const variable of variables)if(variable&&validVariable(variable.name))sharedVariables.add(variable.name);
  }
  for(const saved of Object.values(metadata))rememberSerializedVariables(saved);
  const make=(tag,text)=>{const n=document.createElement(tag);if(text)n.textContent=text;host.appendChild(n);return n;};
  const select=make('select');select.setAttribute('aria-label','Object event');for(const [value,label] of [['update','Every game tick'],['spawn','When created'],['animation_finish','When animation finishes'],['contact','When touching another object'],['player_contact','When touching a player'],['damage_filter','Before taking damage'],['damage','When taking damage'],['defeated','When defeated'],['signal','When receiving a signal'],['remove','When removed']]){const option=document.createElement('option');option.value=value;option.textContent=label;select.appendChild(option);}
  const blocks=make('button','Blocks'),advanced=make('button','Advanced Lua');blocks.type=advanced.type='button';
  const area=make('textarea');area.setAttribute('aria-label','Object callback Lua');
  const editor=CodeMirror.fromTextArea(area,{mode:'lua',lineNumbers:true,indentUnit:2,tabSize:2,matchBrackets:true,autoCloseBrackets:true});
  const canvas=make('div');canvas.className='object-logic-blocks';
  GregLogicBlocks.setObjectTypes((doc.entities.types||[]).map(t=>({key:t.key,label:GregObjects.label(t.key),regions:(t.regions||[]).map(r=>({name:r.name,role:r.role})),animations:(t.animations||[]).map(a=>({name:a.name}))})));
  if(GregLogicBlocks.setPlacements)GregLogicBlocks.setPlacements((doc.entities.placements||[]).map(p=>({name:p.name,label:p.name})));
  if(GregLogicBlocks.setTileTypes){const native=(window.GregCore&&GregCore.TILE_METADATA||[]).map(tile=>({symbol:tile.glyph,label:tile.label||tile.name||tile.glyph})),custom=((doc.tileset||{}).tiles||[]).map(tile=>({symbol:tile.symbol,label:tile.name||tile.id||tile.symbol}));GregLogicBlocks.setTileTypes(native.concat(custom));}
  if(GregLogicBlocks.setRoomConnections){const graph=window.GregCore&&GregCore.validateRoomGraph&&doc.layout&&doc.layout.kind==='room_graph'?GregCore.validateRoomGraph(doc):null,connections=graph&&graph.valid?graph.connections:[];GregLogicBlocks.setRoomConnections(connections.map((connection,index)=>({id:connection.id,index:index+1,label:'Door '+(index+1)+' · '+connection.from+' '+connection.fromSide+' → '+connection.to+' '+connection.toSide})));}
  Blockly.setParentContainer(host.closest('dialog')||host);
  const timerBlocks=new Set(['greg_timer_start','greg_timer_repeat','greg_timer_cancel','greg_timer_remaining','greg_timer_active']);
  const objectToolbox=GregLogicBlocks.toolbox.contents.filter(c=>c.name!=='Events').map(c=>c.name==='Game'?Object.assign({},c,{contents:c.contents.filter(item=>!timerBlocks.has(item.type))}):c);
  const workspace=Blockly.inject(canvas,{theme:GregLogicBlocks.getTheme(),toolbox:{kind:'categoryToolbox',contents:objectToolbox},media:'vendor/blockly/media/',sounds:false,trashcan:true,maxBlocks:512,zoom:{controls:true,wheel:true},move:{scrollbars:true,drag:false,wheel:true}});
  const unbindDropdown=GregLogicBlocks.bindDropdownInput(canvas,workspace);
  function rememberWorkspaceVariables(){for(const variable of workspace.getVariableMap().getAllVariables()){const name=variable.getName();if(validVariable(name))sharedVariables.add(name);}}
  function restoreSharedVariables(){for(const name of sharedVariables)workspace.getVariableMap().createVariable(name);}
  function commit(){if(mode==='blocks'){rememberWorkspaceVariables();const saved=GregLogicBlocks.saveCallback(workspace,key,event);metadata[event]=saved;callbacks[event]=saved.source;}else callbacks[event]=editor.getValue();}
  function choose(next,opening=false){
    if(!opening){try{commit();}catch(error){report(error.message);return false;}}
    mode=next;busy=true;Blockly.Events.disable();
    try{if(mode==='blocks'){GregLogicBlocks.restoreCallback(workspace,metadata[event],callbacks[event]||'',key,event);restoreSharedVariables();}else editor.setValue(callbacks[event]||'');}
    finally{Blockly.Events.enable();busy=false;}
    canvas.hidden=mode!=='blocks';editor.getWrapperElement().hidden=mode==='blocks';
    blocks.setAttribute('aria-pressed',String(mode==='blocks'));advanced.setAttribute('aria-pressed',String(mode==='advanced'));
    requestAnimationFrame(()=>{if(canvas.isConnected){Blockly.svgResize(workspace);editor.refresh();}});report('');return true;
  }
  editor.on('change',()=>{if(!busy)callbacks[event]=editor.getValue();});
  workspace.addChangeListener(e=>{if(busy||e.isUiEvent)return;try{commit();report('');}catch(error){report(error.message);}});
  blocks.onclick=()=>choose('blocks');advanced.onclick=()=>choose('advanced');
  select.onchange=()=>{const next=select.value;try{commit();}catch(error){select.value=event;report(error.message);return;}event=next;choose(metadata[event]||!callbacks[event]?'blocks':'advanced',true);};
  choose(metadata[event]||!callbacks[event]?'blocks':'advanced',true);
  return {commit,dispose(){unbindDropdown();workspace.dispose();editor.toTextArea();}};
 }};
}());
