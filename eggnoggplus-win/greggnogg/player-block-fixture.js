const L=require('./logic-blocks'),B=require('./vendor/blockly/blockly_compressed');
module.exports=function(){const w=new B.Workspace(),tick=w.newBlock('greg_tick'),players=w.newBlock('greg_players'),action=w.newBlock('greg_velocity'),speed=w.newBlock('greg_player_value');tick.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(action.previousConnection);action.setFieldValue('current','PLAYER');speed.setFieldValue('vx','PROPERTY');action.getInput('X').connection.connect(speed.outputConnection);const source=L.generate(w);w.dispose();return source;};

module.exports.touching=function(){const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),players=w.newBlock('greg_players'),condition=w.newBlock('controls_if'),touching=w.newBlock('greg_player_touching'),action=w.newBlock('greg_velocity');event.setFieldValue('demo:launch_pad','TYPE');event.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(touching.outputConnection);touching.setFieldValue('sensor','ROLE');condition.getInput('DO0').connection.connect(action.previousConnection);action.setFieldValue('current','PLAYER');const jump=w.newBlock('math_number');jump.setFieldValue(-4,'NUM');action.getInput('Y').connection.connect(jump.outputConnection);const source=L.generate(w);w.dispose();return source;};

module.exports.variables=function(){
 const w=new B.Workspace(),tick=w.newBlock('greg_tick'),players=w.newBlock('greg_players'),change=w.newBlock('greg_variable_change'),amount=w.newBlock('greg_player_value'),action=w.newBlock('greg_velocity'),get=w.newBlock('greg_variable_get');
 tick.getInput('DO').connection.connect(players.previousConnection);players.getInput('DO').connection.connect(change.previousConnection);change.nextConnection.connect(action.previousConnection);
 change.setFieldValue('current','SCOPE');change.setFieldValue('charges','NAME');amount.setFieldValue('player','PROPERTY');change.getInput('VALUE').connection.connect(amount.outputConnection);
 action.setFieldValue('current','PLAYER');get.setFieldValue('current','SCOPE');get.setFieldValue('charges','NAME');action.getInput('Y').connection.connect(get.outputConnection);
 const source=L.generate(w);w.dispose();return source;
};

module.exports.patrol=function(){
 const w=new B.Workspace(),event=w.newBlock('greg_entity_event'),condition=w.newBlock('controls_if'),sensor=w.newBlock('greg_terrain_ahead'),reverse=w.newBlock('greg_entity_reverse');
 event.setFieldValue('demo:orb','TYPE');event.getInput('DO').connection.connect(condition.previousConnection);condition.getInput('IF0').connection.connect(sensor.outputConnection);condition.getInput('DO0').connection.connect(reverse.previousConnection);
 const source=L.generate(w);w.dispose();return source;
};
