'use strict';
const L=require('./logic-blocks');
const B=require('./vendor/blockly/blockly_compressed');

function value(workspace,type,field,name){
  const block=workspace.newBlock(type);
  if(field)block.setFieldValue(name,field);
  return block;
}
function number(workspace,n){return value(workspace,'math_number','NUM',n);}
function truth(workspace,yes){return value(workspace,'logic_boolean','BOOL',yes?'TRUE':'FALSE');}
function playerValue(workspace,property){return value(workspace,'greg_player_value','PROPERTY',property);}
function playerCondition(workspace,property){return value(workspace,'greg_player_condition','PROPERTY',property);}
function variable(workspace){
  const block=value(workspace,'greg_variable_get','SCOPE','current');
  block.setFieldValue('extra_jump','NAME');return block;
}
function binary(workspace,type,inputA,a,inputB,b){
  const block=workspace.newBlock(type);block.getInput(inputA).connection.connect(a.outputConnection);block.getInput(inputB).connection.connect(b.outputConnection);return block;
}
function both(workspace,a,b){const block=binary(workspace,'logic_operation','A',a,'B',b);block.setFieldValue('AND','OP');return block;}
function either(workspace,a,b){const block=binary(workspace,'logic_operation','A',a,'B',b);block.setFieldValue('OR','OP');return block;}
function negate(workspace,a){const block=workspace.newBlock('logic_negate');block.getInput('BOOL').connection.connect(a.outputConnection);return block;}
function compare(workspace,a,op,b){const block=binary(workspace,'logic_compare','A',a,'B',b);block.setFieldValue(op,'OP');return block;}
function connect(parent,input,child){parent.getInput(input).connection.connect(child.previousConnection);return child;}
function chain(a,b){a.nextConnection.connect(b.previousConnection);return b;}
function setVariable(workspace,yes){
  const block=value(workspace,'greg_variable_set','SCOPE','current');block.setFieldValue('extra_jump','NAME');block.getInput('VALUE').connection.connect(truth(workspace,yes).outputConnection);return block;
}
function setObjectNumber(workspace,property,source){
  const block=value(workspace,'greg_entity_set','PROPERTY',property);block.getInput('VALUE').connection.connect(source.outputConnection);return block;
}

function namedIndicator(workspace,name,playerNumber){
  const named=value(workspace,'greg_entity_named','NAME',name),condition=connect(named,'DO',workspace.newBlock('controls_if'));
  condition.getInput('IF0').connection.connect(compare(workspace,playerValue(workspace,'player'),'EQ',number(workspace,playerNumber)).outputConnection);
  const setX=connect(condition,'DO0',setObjectNumber(workspace,'x',playerValue(workspace,'x')));
  const y=binary(workspace,'greg_math','A',playerValue(workspace,'y'),'B',number(workspace,13));y.setFieldValue('-','OP');
  const setY=chain(setX,setObjectNumber(workspace,'y',y));
  const visible=value(workspace,'greg_entity_flag','PROPERTY','visible');visible.getInput('VALUE').connection.connect(variable(workspace).outputConnection);chain(setY,visible);
  return named;
}

function buildMap(){
  const workspace=new B.Workspace();workspace.getVariableMap().createVariable('extra_jump');
  const tick=workspace.newBlock('greg_tick'),players=connect(tick,'DO',workspace.newBlock('greg_players'));
  const grounded=connect(players,'DO',workspace.newBlock('controls_if'));
  grounded.getInput('IF0').connection.connect(playerCondition(workspace,'grounded').outputConnection);
  connect(grounded,'DO0',setVariable(workspace,true));

  const second=chain(grounded,workspace.newBlock('controls_if'));
  const nativeTakeoff=both(workspace,playerCondition(workspace,'previously_grounded'),compare(workspace,playerValue(workspace,'vy'),'LT',number(workspace,0)));
  const canJump=both(workspace,playerCondition(workspace,'pressed.jump'),both(workspace,negate(workspace,playerCondition(workspace,'grounded')),both(workspace,negate(workspace,nativeTakeoff),variable(workspace))));
  second.getInput('IF0').connection.connect(canJump.outputConnection);
  const velocity=connect(second,'DO0',value(workspace,'greg_velocity','PLAYER','current'));
  velocity.getInput('X').connection.connect(playerValue(workspace,'vx').outputConnection);velocity.getInput('Y').connection.connect(number(workspace,-4).outputConnection);
  chain(velocity,setVariable(workspace,false));

  const indicator1=chain(second,namedIndicator(workspace,'jump_indicator_1',1));
  chain(indicator1,namedIndicator(workspace,'jump_indicator_2',2));
  const saved=L.save(workspace);workspace.dispose();return saved;
}

function buildRefill(){
  const workspace=new B.Workspace();workspace.getVariableMap().createVariable('extra_jump');
  const event=workspace.newBlock('greg_entity_event');event.setFieldValue('demo:jump_refill','TYPE');event.setFieldValue('update','EVENT');
  const players=connect(event,'DO',workspace.newBlock('greg_players')),condition=connect(players,'DO',workspace.newBlock('controls_if'));
  const touching=value(workspace,'greg_player_touching','ROLE','name:pickup');condition.getInput('IF0').connection.connect(touching.outputConnection);
  const recharge=connect(condition,'DO0',setVariable(workspace,true));chain(recharge,workspace.newBlock('greg_entity_remove'));
  const saved=L.saveCallback(workspace,'demo:jump_refill','update');workspace.dispose();return saved;
}

module.exports=function(){
  L.setObjectTypes([{key:'demo:jump_indicator',label:'Jump indicator',regions:[]},{key:'demo:jump_refill',label:'Jump refill',regions:[{name:'pickup',role:'sensor'}]}]);
  L.setPlacements([{name:'jump_indicator_1'},{name:'jump_indicator_2'},{name:'jump_refill'}]);
  try{return {map:buildMap(),refill:buildRefill()};}
  finally{L.setObjectTypes([]);L.setPlacements([]);}
};
