(function(){
 'use strict';
 window.GregObjectLogic={mount(host,doc,key,report){
  const callbacks=(doc.objectScripts[key]||={}),metadata=((doc.objectBlocks||={})[key]||={});
  let event='update',mode='advanced',busy=false;
  const make=(tag,text)=>{const n=document.createElement(tag);if(text)n.textContent=text;host.appendChild(n);return n;};
  const select=make('select');select.setAttribute('aria-label','Object event');for(const [value,label] of [['update','Every game tick'],['spawn','When created'],['remove','When removed']]){const option=document.createElement('option');option.value=value;option.textContent=label;select.appendChild(option);}
  const blocks=make('button','Blocks'),advanced=make('button','Advanced Lua');blocks.type=advanced.type='button';
  const area=make('textarea');area.setAttribute('aria-label','Object callback Lua');
  const editor=CodeMirror.fromTextArea(area,{mode:'lua',lineNumbers:true,indentUnit:2,tabSize:2,matchBrackets:true,autoCloseBrackets:true});
  const canvas=make('div');canvas.className='object-logic-blocks';
  GregLogicBlocks.setObjectTypes((doc.entities.types||[]).map(t=>({key:t.key,label:GregObjects.label(t.key)})));
  Blockly.setParentContainer(host.closest('dialog')||host);
  const workspace=Blockly.inject(canvas,{theme:GregLogicBlocks.getTheme(),toolbox:{kind:'categoryToolbox',contents:GregLogicBlocks.toolbox.contents.filter(c=>c.name!=='Events')},media:'vendor/blockly/media/',sounds:false,trashcan:true,maxBlocks:512,zoom:{controls:true,wheel:true},move:{scrollbars:true,drag:false,wheel:true}});
  const unbindDropdown=GregLogicBlocks.bindDropdownInput(canvas,workspace);
  function commit(){if(mode==='blocks'){const saved=GregLogicBlocks.saveCallback(workspace,key,event);metadata[event]=saved;callbacks[event]=saved.source;}else callbacks[event]=editor.getValue();}
  function choose(next,opening=false){
    if(!opening){try{commit();}catch(error){report(error.message);return false;}}
    mode=next;busy=true;Blockly.Events.disable();
    try{if(mode==='blocks')GregLogicBlocks.restoreCallback(workspace,metadata[event],callbacks[event]||'',key,event);else editor.setValue(callbacks[event]||'');}
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
