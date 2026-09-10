(function(){
  'use strict';
  const O=window.GregObjects,A=window.EntityAuthor,G=window.GregCore;
  let dialog,working,selected,applyChange,tick=0,request=0,saveRequest=0,callbackStudio=null;
  const element=(tag,text,parent)=>{const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(parent)parent.appendChild(n);return n;};
  function button(text,parent,fn){const n=element('button',text,parent);n.type='button';n.onclick=()=>{try{if(callbackStudio)callbackStudio.commit();fn();}catch(error){status(error.message);}};return n;}
  function field(text,value,parent,change,kind){const label=element('label',text,parent),input=element('input',undefined,label);input.type=kind||(typeof value==='number'?'number':'text');input.value=value;input.onchange=()=>{try{if(callbackStudio)callbackStudio.commit();change(input.type==='number'?Number(input.value):input.value);status('');preview();}catch(error){status(error.message);}};return input;}
  function status(text){dialog.querySelector('[data-object-status]').textContent=text;}
  function current(){return O.catalog(working).types.find(t=>t.key===selected);}
  function changeName(name){const old=selected,key=old.split(':')[0]+':'+name.toLowerCase().trim().replace(/[^a-z0-9_.-]+/g,'_');working=O.rename(working,old,key);selected=key;render();}
  async function preview(){
    const type=current(),canvas=dialog.querySelector('[data-object-preview]'),id=++request;if(!canvas)return;
    const scratch=document.createElement('canvas');scratch.width=320;scratch.height=240;const c=scratch.getContext('2d');c.imageSmoothingEnabled=false;
    try{if(type)await O.drawPicture(c,working,type,160,120,tick,false,4);if(type){for(const area of type.regions){c.strokeStyle=area.role==='sensor'?'#56d6f5':'#f5b85a';c.lineWidth=1;c.strokeRect(160+area.x*4,120+area.y*4,area.width*4,area.height*4);}}if(id===request){const ctx=canvas.getContext('2d');ctx.imageSmoothingEnabled=false;ctx.clearRect(0,0,320,240);ctx.drawImage(scratch,0,0);}}catch(error){if(id===request)status(error.message);}
  }
  function render(){
    if(callbackStudio){try{callbackStudio.commit();}catch(error){status(error.message);return;}callbackStudio.dispose();callbackStudio=null;}
    const list=dialog.querySelector('[data-object-list]'),properties=dialog.querySelector('[data-object-properties]');list.replaceChildren();properties.replaceChildren();dialog.querySelector('[data-object-areas]').replaceChildren();
    O.catalog(working).types.forEach(t=>{const b=button(O.label(t.key),list,()=>{selected=t.key;render();});b.className='tile-button'+(selected===t.key?' is-active':'');});
    const type=current();if(!type){element('p','No custom objects.',properties);preview();return;}
    field('Name',O.label(type.key),properties,changeName);
    element('h3','Appearance',properties);
    if(!type.visual)button('Add picture',properties,()=>{type.visual={sheet:'builtin:tiles',sprite:4};render();});
    if(type.visual){
      const v=type.visual,picker=element('select',undefined,element('label','Sheet',properties));
      const builtins={'builtin:tiles':'Game tiles','builtin:sprites':'Characters and items','builtin:misc':'Scenery','builtin:glyphs':'Letters'};
      [...A.builtins,...Object.keys(working.assets||{})].forEach(key=>{const option=element('option',builtins[key]||key,picker);option.value=key;});picker.value=v.sheet;picker.onchange=()=>{v.sheet=picker.value;v.sprite=0;render();};
      const sheet=element('canvas',undefined,properties);sheet.className='object-sheet-picker';sheet.setAttribute('aria-label','Picture sheet; use the Picture number field as a keyboard alternative');
      const typeKey=type.key;
      O.imageFor(working,v.sheet).then(image=>{if(selected!==typeKey||!sheet.isConnected)return;sheet.width=image.width;sheet.height=image.height;sheet.getContext('2d').drawImage(image,0,0);sheet.onclick=e=>{const rect=sheet.getBoundingClientRect(),x=(e.clientX-rect.left)*image.width/rect.width,y=(e.clientY-rect.top)*image.height/rect.height,data=G.buildDataObject(working),sheets=data.tileset?A.parseAtlas(JSON.stringify(data)):{};for(let i=0;i<8192;i++){let r;try{r=A.spriteRegion(v.sheet,image,i,sheets);}catch(error){break;}if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h){v.sprite=i;render();break;}}};}).catch(error=>status(error.message));
      field('Picture number',v.sprite,properties,value=>v.sprite=value);
      const file=element('input',undefined,properties);file.type='file';file.accept='image/png';file.setAttribute('aria-label','Import picture');file.onchange=async()=>{const picture=file.files[0],targetDocument=working;if(!picture)return;try{if(picture.size>67108864)throw Error('Picture exceeds 64 MiB.');const bytes=new Uint8Array(await picture.arrayBuffer()),added=A.addPicture(A.fromMapDocument(working),picture.name,bytes);const url=await new Promise((resolve,reject)=>{const reader=new FileReader();reader.onload=()=>resolve(reader.result);reader.onerror=reject;reader.readAsDataURL(picture);});await new Promise((resolve,reject)=>{const image=new Image();image.onload=resolve;image.onerror=()=>reject(Error('Picture is not a readable PNG.'));image.src=url;});if(!dialog.open||working!==targetDocument||current()!==type)return;working.assets=working.assets||{};working.assets[added.filename]=url;working.tileset=JSON.parse(new TextDecoder().decode(added.map.files['data.json'])).tileset;type.visual={sheet:added.filename,sprite:0};render();}catch(error){status(error.message);}};
      const numbers=element('div',undefined,properties);numbers.className='object-fields';
      for(const [key,label,fallback] of [['frames','Animation frames',1],['frame_ticks','Ticks per frame',1],['scale_x','Width scale',1],['scale_y','Height scale',1]])field(label,v[key]===undefined?fallback:v[key],numbers,value=>v[key]=value);
      field('Color',(v.tint||'#FFFFFFFF').slice(0,7),properties,value=>v.tint=value+(v.tint||'#FFFFFFFF').slice(7,9),'color');
      const drawOrder=element('select',undefined,element('label','Draw order',properties));
      for(const [value,text] of [['0','Behind players (default)'],['1','In front of players']]){const option=element('option',text,drawOrder);option.value=value;}
      drawOrder.value=String(v.layer||0);drawOrder.onchange=()=>{v.layer=Number(drawOrder.value);preview();};
      const extra=element('details',undefined,properties);element('summary','Offsets',extra);for(const key of ['offset_x','offset_y'])field(key,v[key]||0,extra,value=>v[key]=value);
    }
    const physics=element('fieldset',undefined,properties);element('legend','Movement',physics);
    working.objectMotion=working.objectMotion||{};const motion=working.objectMotion[type.key]||{enabled:false,gravity:0.15,drag:0,maxFall:6};working.objectMotion[type.key]=motion;
    const enabled=element('input',undefined,element('label','Apply gravity and drag',physics));enabled.type='checkbox';enabled.checked=motion.enabled;enabled.onchange=()=>{motion.enabled=enabled.checked;if(enabled.checked){motion.automatic=true;}};
    for(const [key,label,min,max,step] of [['gravity','Gravity',-2,2,0.01],['drag','Horizontal drag',0,1,0.01],['maxFall','Maximum vertical speed',0.25,16,0.25]]){
      const row=element('label',label,physics),slider=element('input',undefined,row),number=element('input',undefined,row);slider.type='range';number.type='number';
      for(const input of [slider,number]){input.min=min;input.max=max;input.step=step;input.value=motion[key];input.oninput=()=>{const value=Number(input.value);if(Number.isFinite(value)){motion[key]=value;slider.value=value;number.value=value;}};}
    }
    const collide=element('input',undefined,element('label','Collide with solid map blocks and objects',physics));collide.type='checkbox';collide.checked=!!motion.collide;collide.onchange=()=>{motion.collide=collide.checked;if(collide.checked){motion.automatic=true;}};
    field('Collision width',motion.width===undefined?16:motion.width,physics,value=>motion.width=value);
    field('Collision height',motion.height===undefined?16:motion.height,physics,value=>motion.height=value);
    element('p','Movement and collision run before your update logic. The collision box is centered on the object; detection areas remain separate.',physics);
    element('h3','Logic',properties);working.objectScripts=working.objectScripts||{};
    callbackStudio=GregObjectLogic.mount(element('div',undefined,properties),working,type.key,status);
    const regions=element('details',undefined,dialog.querySelector('[data-object-areas]'));regions.open=true;element('summary','Detection areas',regions);element('p','Enable Solid to block players, swords, corpses and native hazards. Other purposes detect contacts for logic.',regions);
    function validateAreas(){A.serialize(O.catalog(working));preview();}
    type.regions.forEach((area,index)=>{
      const row=element('fieldset',undefined,regions);element('legend','Area '+area.id,row);
      const role=element('select',undefined,element('label','Purpose',row));
      for(const [value,text] of [['body','Body'],['sensor','Sensor'],['hitbox','Attack'],['hurtbox','Damage receiver'],['solid','Solid obstacle']]){const option=element('option',text,role);option.value=value;}role.value=area.role;role.onchange=()=>{area.role=role.value;solid.checked=area.role==='solid';validateAreas();};
      const solid=element('input',undefined,element('label','Solid ? blocks players and native physics objects',row));solid.type='checkbox';solid.checked=area.role==='solid';solid.onchange=()=>{area.role=solid.checked?'solid':'body';role.value=area.role;validateAreas();};
      const dimensions=element('div',undefined,row);dimensions.className='object-fields';
      for(const [key,text] of [['x','Left'],['y','Top'],['width','Width'],['height','Height']])field(text,area[key],dimensions,value=>{const old=area[key];area[key]=value;try{validateAreas();}catch(error){area[key]=old;throw error;}});
      const filters=element('details',undefined,row);element('summary','Contact filters',filters);
      for(const key of ['id','layer','mask'])field(key,area[key],filters,value=>{const old=area[key];area[key]=value;try{validateAreas();}catch(error){area[key]=old;throw error;}});
      button('Remove area',row,()=>{type.regions.splice(index,1);render();});
    });
    if(type.regions.length<16)button('Add area',regions,()=>{let id=1;while(type.regions.some(r=>r.id===id))id++;type.regions.push({id,role:'sensor',layer:1,mask:1,x:-8,y:-8,width:16,height:16});render();});
    preview();
  }
  window.openGregObjectDesigner=function(doc,onApply,key){
    if(callbackStudio){callbackStudio.dispose();callbackStudio=null;}
    working=JSON.parse(JSON.stringify(doc));applyChange=onApply;selected=key||O.catalog(working).types[0]?.key;
    if(!dialog){dialog=element('dialog',undefined,document.body);dialog.className='object-designer-dialog';dialog.innerHTML='<header><h2>Object designer</h2><button type="button" data-object-cancel aria-label="Cancel object edits">×</button></header><div class="object-designer-body"><aside><div data-object-list></div><div data-object-actions></div></aside><section><div class="object-geometry"><canvas data-object-preview width="320" height="240"></canvas><div data-object-areas></div></div><div data-object-properties></div></section></div><footer><span data-object-status role="status"></span><button type="button" data-object-done>Done</button></footer>';
      const actions=dialog.querySelector('[data-object-actions]');
      button('New',actions,()=>{const result=O.create(working);working=result.document;selected=result.key;render();});
      button('Delete',actions,()=>{try{working=O.remove(working,selected);selected=O.catalog(working).types[0]?.key;render();}catch(error){status(error.message);}});
      dialog.querySelector('[data-object-cancel]').onclick=()=>dialog.close();
      dialog.querySelector('[data-object-done]').onclick=async()=>{
        const done=dialog.querySelector('[data-object-done]'),body=dialog.querySelector('.object-designer-body'),source=working,operation=++saveRequest;
        done.disabled=true;body.inert=true;
        try{
          if(callbackStudio)callbackStudio.commit();
          const candidate=await O.prepareForSave(source);
          if(!dialog.open||working!==source||operation!==saveRequest)return;
          applyChange(candidate);dialog.close();
        }catch(error){if(operation===saveRequest&&dialog.open)status(error.message);}
        finally{if(operation===saveRequest){done.disabled=false;body.inert=false;}}
      };
      function animate(){requestAnimationFrame(animate);if(dialog.open&&!document.hidden){tick++;preview();}}requestAnimationFrame(animate);
    }
    saveRequest++;dialog.querySelector('[data-object-done]').disabled=false;dialog.querySelector('.object-designer-body').inert=false;status('');render();dialog.showModal();
  };
}());
