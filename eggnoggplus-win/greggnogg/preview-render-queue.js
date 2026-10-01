(function(root,factory){
  const api=factory();
  if(typeof module==='object'&&module.exports)module.exports=api;
  else root.GregPreviewQueue=api;
}(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const targets=new WeakMap();
  function schedule(target,draw,onError){
    let slot=targets.get(target);
    if(!slot){slot={running:false,next:null};targets.set(target,slot);}
    /* Keep one running render and the newest pending request. Animation and
     * edits must not accumulate async canvases while image loading is slow. */
    slot.next={draw,onError};
    if(slot.running)return;
    slot.running=true;
    (async function(){
      while(slot.next){
        const task=slot.next;slot.next=null;
        try{await task.draw();}
        catch(error){if(task.onError)task.onError(error);else console.warn('Preview render failed.',error);}
      }
      slot.running=false;
    }());
  }
  return {schedule};
}));
