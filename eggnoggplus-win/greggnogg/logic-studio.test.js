const {test}=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
function studio(){
  const elements=new Map();const element=id=>{if(!elements.has(id))elements.set(id,{value:'',hidden:false,textContent:'',setAttribute(name,value){this[name]=value;}});return elements.get(id);};
  const wrapper={},doc={mapLua:'',mapBlocks:{}},state={fail:false,saves:0};let text='';
  const editor={getValue:()=>text,setValue:value=>{text=value;},getWrapperElement:()=>wrapper,on(){},refresh(){},focus(){}};
  const context={window:{},document:{getElementById:element,querySelector:element},CodeMirror:{fromTextArea:()=>editor},Blockly:{Events:{disable(){},enable(){}},inject:()=>({addChangeListener(){}}),svgResize(){}},GregLogicBlocks:{toolbox:{},restore(){},save(){state.saves++;if(state.fail)throw Error('Connect the action to an event.');return {source:'generated',workspace:{}};}}};
  vm.runInNewContext(fs.readFileSync(__dirname+'/logic-studio.js','utf8'),context);
  return {api:context.window.GregLogicStudio,doc,state,element,wrapper,open(){context.window.GregLogicStudio.open(()=>doc,value=>{doc.mapLua=value;element('map-lua-editor').value=value;});}};
}
test('invalid blocks remain visible when switching to Advanced',()=>{const s=studio();s.open();s.state.fail=true;s.element('logic-advanced-button').onclick();assert.equal(s.element('logic-blocks').hidden,false);assert.equal(s.wrapper.hidden,true);assert.match(s.element('logic-status').textContent,/Connect/);assert.equal(s.doc.mapLua,'');s.state.fail=false;s.element('logic-advanced-button').onclick();assert.equal(s.element('logic-blocks').hidden,true);assert.equal(s.doc.mapLua,'generated');});
test('opening another document does not compile the previous block workspace',()=>{const s=studio();s.open();s.state.fail=true;s.doc.mapBlocks=null;s.element('map-lua-editor').value='manual Lua';s.open();assert.equal(s.state.saves,0);assert.equal(s.element('logic-blocks').hidden,true);});
