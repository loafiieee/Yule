const {test}=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const L=require('./logic-blocks'),B=require('./vendor/blockly/blockly_compressed');
function panel(){
 const element=tag=>({tag,children:[],hidden:false,isConnected:true,value:'',addEventListener(){},removeEventListener(){},appendChild(child){this.children.push(child);},setAttribute(name,value){this[name]=value;},closest(){return this;}});
 const host=element('section'),workspace=new B.Workspace(),events={};let value='',message='';const wrapper=element('div');
 const editor={getValue:()=>value,setValue:text=>{value=text;if(events.change)events.change();},getWrapperElement:()=>wrapper,on:(name,fn)=>events[name]=fn,refresh(){},toTextArea(){}};
 const doc={entities:{types:[{key:'demo:orb'}]},objectScripts:{}};
 const context={window:{},document:{createElement:element},CodeMirror:{fromTextArea:()=>editor},GregLogicBlocks:L,GregObjects:{label:key=>key},Blockly:{inject:()=>workspace,setParentContainer(){},Events:B.Events,svgResize(){}},requestAnimationFrame:fn=>fn()};
 vm.runInNewContext(fs.readFileSync(__dirname+'/object-logic.js','utf8'),context);
 const api=context.window.GregObjectLogic.mount(host,doc,'demo:orb',text=>message=text);
 return {api,doc,workspace,editor,wrapper,host,get message(){return message;},select:host.children[0],blocks:host.children[1],advanced:host.children[2],canvas:host.children[4]};
}
test('object logic event switching retains independent Lua and blocks',()=>{
 const p=panel();assert.equal(p.wrapper.hidden,true);
 p.advanced.onclick();p.editor.setValue('-- update');p.select.value='spawn';p.select.onchange();
 assert.equal(p.doc.objectScripts['demo:orb'].update,'-- update');
 p.advanced.onclick();p.editor.setValue('-- spawn');p.select.value='update';p.select.onchange();
 p.advanced.onclick();assert.equal(p.editor.getValue(),'-- update');
 assert.equal(p.doc.objectScripts['demo:orb'].spawn,'-- spawn');p.api.dispose();
});
test('disconnected blocks block mode and event switches without losing the workspace',()=>{
 const p=panel(),loose=p.workspace.newBlock('greg_entity_set');
 p.advanced.onclick();assert.equal(p.wrapper.hidden,true);assert.match(p.message,/Connect/);
 p.select.value='spawn';p.select.onchange();assert.equal(p.select.value,'update');assert.equal(p.workspace.getAllBlocks().length,2);
 loose.dispose();p.advanced.onclick();assert.equal(p.wrapper.hidden,false);p.api.dispose();
});
test('created variable names remain available across every object event tab',()=>{
 const p=panel();p.workspace.getVariableMap().createVariable('cooldown');
 p.select.value='spawn';p.select.onchange();
 assert.ok(p.workspace.getVariableMap().getAllVariables().some(variable=>variable.getName()==='cooldown'));
 const set=p.workspace.newBlock('greg_variable_set');set.setFieldValue('cooldown','NAME');
 assert.ok(set.getField('NAME').getOptions(false).some(option=>option[1]==='cooldown'));
 set.dispose();p.workspace.getVariableMap().createVariable('direction');
 p.select.value='remove';p.select.onchange();
 const names=p.workspace.getVariableMap().getAllVariables().map(variable=>variable.getName());
 assert.ok(names.includes('cooldown'));assert.ok(names.includes('direction'));
 p.select.value='update';p.select.onchange();
 assert.ok(p.workspace.getVariableMap().getAllVariables().some(variable=>variable.getName()==='direction'));
 p.api.dispose();
});
test('object logic offers animation, contact, damage filtering and signal lifecycle tabs',()=>{
 const p=panel(),options=p.select.children.map(option=>option.value);
 assert.deepEqual(options,['update','spawn','animation_finish','contact','player_contact','damage_filter','damage','defeated','signal','remove']);
 p.select.value='animation_finish';p.select.onchange();p.advanced.onclick();p.editor.setValue('-- animation finished');p.api.commit();
 p.select.value='player_contact';p.select.onchange();p.advanced.onclick();p.editor.setValue('-- player contact');
 p.select.value='contact';p.select.onchange();p.advanced.onclick();p.editor.setValue('-- object contact');p.api.commit();
 assert.equal(p.doc.objectScripts['demo:orb'].player_contact,'-- player contact');
 p.select.value='damage';p.select.onchange();p.advanced.onclick();p.editor.setValue('-- damage');p.api.commit();
 p.select.value='signal';p.select.onchange();p.advanced.onclick();p.editor.setValue('-- signal');p.api.commit();
 assert.equal(p.doc.objectScripts['demo:orb'].contact,'-- object contact');
 assert.equal(p.doc.objectScripts['demo:orb'].animation_finish,'-- animation finished');
 assert.equal(p.doc.objectScripts['demo:orb'].damage,'-- damage');
 assert.equal(p.doc.objectScripts['demo:orb'].signal,'-- signal');p.api.dispose();
});
