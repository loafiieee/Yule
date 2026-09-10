// Shared authoring fixture: execute this generated Lua with the real map VM.
const L=require('./logic-blocks'),B=require('./vendor/blockly/blockly_compressed');
function build(remove=false){
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event');event.setFieldValue('demo:orb','TYPE');
 const speed=w.newBlock('greg_entity_set');speed.setFieldValue('vx','PROPERTY');
 const number=w.newBlock('math_number');number.setFieldValue(2,'NUM');speed.getInput('VALUE').connection.connect(number.outputConnection);
 event.getInput('DO').connection.connect(speed.previousConnection);
 const paused=w.newBlock('greg_entity_flag');const yes=w.newBlock('logic_boolean');yes.setFieldValue('TRUE','BOOL');paused.getInput('VALUE').connection.connect(yes.outputConnection);speed.nextConnection.connect(paused.previousConnection);
 const remember=w.newBlock('greg_state_set');remember.setFieldValue('tick','KEY');const time=w.newBlock('greg_time');remember.getInput('VALUE').connection.connect(time.outputConnection);paused.nextConnection.connect(remember.previousConnection);
 const condition=w.newBlock('controls_if'),every=w.newBlock('greg_every'),interval=w.newBlock('math_number');interval.setFieldValue(1,'NUM');every.getInput('INTERVAL').connection.connect(interval.outputConnection);condition.getInput('IF0').connection.connect(every.outputConnection);remember.nextConnection.connect(condition.previousConnection);
 const loop=w.newBlock('controls_repeat_ext'),count=w.newBlock('math_number');count.setFieldValue(2,'NUM');loop.getInput('TIMES').connection.connect(count.outputConnection);condition.getInput('DO0').connection.connect(loop.previousConnection);
 const randomValue=w.newBlock('greg_state_set');randomValue.setFieldValue('random','KEY');loop.getInput('DO').connection.connect(randomValue.previousConnection);
 const absolute=w.newBlock('greg_math_function'),random=w.newBlock('greg_random');absolute.getInput('VALUE').connection.connect(random.outputConnection);randomValue.getInput('VALUE').connection.connect(absolute.outputConnection);
 if(remove){condition.nextConnection.connect(w.newBlock('greg_entity_remove').previousConnection);}
 else{const tick=w.newBlock('greg_tick'),spawn=w.newBlock('greg_entity_spawn');spawn.setFieldValue('demo:orb','TYPE');tick.getInput('DO').connection.connect(spawn.previousConnection);}
 const result=L.generate(w);w.dispose();return result;
}
module.exports=build;

module.exports.group=function(){const w=new B.Workspace(),tick=w.newBlock('greg_tick'),objects=w.newBlock('greg_entities'),speed=w.newBlock('greg_entity_set'),two=w.newBlock('math_number'),pause=w.newBlock('greg_entity_flag'),yes=w.newBlock('logic_boolean');objects.setFieldValue('demo:orb','TYPE');tick.getInput('DO').connection.connect(objects.previousConnection);objects.getInput('DO').connection.connect(speed.previousConnection);speed.setFieldValue('vx','PROPERTY');two.setFieldValue(2,'NUM');speed.getInput('VALUE').connection.connect(two.outputConnection);speed.nextConnection.connect(pause.previousConnection);yes.setFieldValue('TRUE','BOOL');pause.getInput('VALUE').connection.connect(yes.outputConnection);const source=L.generate(w);w.dispose();return source;};
