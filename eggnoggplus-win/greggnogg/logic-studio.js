(function(){
  'use strict';
  let workspace,editor,readDocument,writeSource,mode='advanced',busy=false,removed=false;
  function status(message){document.getElementById('logic-status').textContent=message;}
  function source(){return document.getElementById('map-lua-editor').value;}
  function write(text){writeSource(text);if(editor.getValue()!==text){busy=true;editor.setValue(text);busy=false;}}
  function blocksChanged(event){
    if(busy||event.isUiEvent)return;removed=false;
    try{const saved=GregLogicBlocks.save(workspace);readDocument().mapBlocks=saved;write(saved.source);status('');}
    catch(error){status(error.message);}
  }
  function choose(next){
    if(mode==='blocks'&&next==='advanced'&&!busy&&!removed){
      try{const saved=GregLogicBlocks.save(workspace);readDocument().mapBlocks=saved;write(saved.source);}
      catch(error){status(error.message);return;}
    }
    mode=next;const blocks=next==='blocks';
    document.getElementById('logic-blocks').hidden=!blocks;
    editor.getWrapperElement().hidden=blocks;
    for(const id of ['script-template-button','script-format-button'])document.getElementById(id).hidden=blocks;
    document.querySelector('#script-dialog .script-reference').hidden=blocks;
    for(const name of ['blocks','advanced'])document.getElementById('logic-'+name+'-button').setAttribute('aria-pressed',String(name===next));
    if(blocks){busy=true;Blockly.Events.disable();try{GregLogicBlocks.restore(workspace,readDocument().mapBlocks,source());status(source()&&!readDocument().mapBlocks?'Existing Lua is kept in a Lua code block. Edit that code in Advanced.':'');}catch(error){status(error.message);}finally{Blockly.Events.enable();busy=false;}Blockly.svgResize(workspace);}
    else{editor.refresh();editor.focus();}
  }
  window.GregLogicStudio={
    open(read,writeCallback){
      readDocument=read;writeSource=writeCallback;removed=false;
      if(Blockly.setParentContainer)Blockly.setParentContainer(document.getElementById('script-dialog'));
      if(GregLogicBlocks.setObjectTypes)GregLogicBlocks.setObjectTypes(((readDocument().entities||{}).types||[]).map(type=>({key:type.key,label:window.GregObjects?window.GregObjects.label(type.key):type.key})));
      if(!editor){
        const textarea=document.getElementById('map-lua-editor');
        editor=CodeMirror.fromTextArea(textarea,{mode:'lua',lineNumbers:true,indentUnit:2,tabSize:2,indentWithTabs:false,lineWrapping:false,matchBrackets:true,autoCloseBrackets:true,extraKeys:{'Ctrl-Space':()=>document.getElementById('script-api-search').focus()}});
        editor.on('change',()=>{if(!busy){removed=false;writeSource(editor.getValue());status('');}});
        editor.on('cursorActivity',()=>{textarea.selectionStart=editor.indexFromPos(editor.getCursor('from'));textarea.selectionEnd=editor.indexFromPos(editor.getCursor('to'));});
        workspace=Blockly.inject('logic-blocks',{theme:GregLogicBlocks.getTheme?GregLogicBlocks.getTheme():undefined,toolbox:GregLogicBlocks.toolbox,media:'vendor/blockly/media/',trashcan:true,sounds:false,maxBlocks:512,move:{scrollbars:true,drag:false,wheel:true},zoom:{controls:true,wheel:true},grid:{spacing:20,length:3,snap:true}});
        if(GregLogicBlocks.bindDropdownInput)GregLogicBlocks.bindDropdownInput(document.getElementById('logic-blocks'),workspace);
        workspace.addChangeListener(blocksChanged);
        if(typeof ResizeObserver!=='undefined'){
          new ResizeObserver(()=>{if(mode==='blocks')Blockly.svgResize(workspace);else editor.refresh();}).observe(document.getElementById('logic-blocks').parentElement);
        }
        document.getElementById('logic-blocks-button').onclick=()=>choose('blocks');document.getElementById('logic-advanced-button').onclick=()=>choose('advanced');
      }
      busy=true;editor.setValue(source());mode='advanced';busy=false;choose(readDocument().mapBlocks||!source()?'blocks':'advanced');
    },
    sync(){if(editor&&editor.getValue()!==source()){busy=true;editor.setValue(source());busy=false;}},
    reset(){if(!editor)return;removed=true;busy=true;Blockly.Events.disable();try{workspace.clear();editor.setValue('');delete readDocument().mapBlocks;}finally{Blockly.Events.enable();busy=false;}},
    close(){if(!removed&&mode==='blocks'){const saved=GregLogicBlocks.save(workspace);readDocument().mapBlocks=saved;write(saved.source);}}
  };
}());
