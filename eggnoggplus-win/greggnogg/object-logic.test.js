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
