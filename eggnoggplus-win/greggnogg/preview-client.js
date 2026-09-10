(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.GregPreviewClient=factory();}(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  function token(crypto){const bytes=new Uint8Array(16);crypto.getRandomValues(bytes);return Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');}
  async function transfer(id,packet,options){
    const o=options||{},fetcher=o.fetch||fetch,now=o.now||Date.now,sleep=o.sleep||(ms=>new Promise(resolve=>setTimeout(resolve,ms))),status=o.status||(()=>{});
    if(!/^[0-9a-f]{32}$/.test(id))throw Error('Invalid preview session.');
    const url='http://127.0.0.1:31785/preview/'+id,deadline=now()+120000;
    let sent=false,connected=false;
    async function request(method,body){
      const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),method==='POST'?65000:3000);
      try{return await fetcher(url,{method,body,mode:'cors',credentials:'omit',cache:'no-store',signal:controller.signal,headers:body?{'Content-Type':'application/octet-stream'}:undefined});}
      finally{clearTimeout(timer);}
    }
    status('Waiting for EGGNOGG+?');
    while(now()<deadline){
      let response;
      try{response=await request('GET');}catch(error){if(connected)status('Reconnecting to EGGNOGG+?');await sleep(400);continue;}
      if(response.status===403){if(connected)throw Error('Preview session expired or was replaced.');await sleep(400);continue;}
      if(!response.ok)throw Error('EGGNOGG+ rejected the preview connection.');
      connected=true;
      const data=await response.json();
      if(data.state==='done'){status('Preview started.');return;}
      if(data.state==='error')throw Error('EGGNOGG+ could not load this preview. See mods/modframework.log for the map diagnostic.');
      if(data.state==='waiting'&&!sent){
        sent=true;status('Sending map?');
        // Never retry POST: a lost response does not prove the upload failed.
        // GET determines whether the game accepted it.
        try{response=await request('POST',packet);}catch(error){await sleep(400);continue;}
        if(!response.ok)throw Error('EGGNOGG+ rejected the map package.');
      }else if(['receiving','ready','processing'].includes(data.state)){status('Loading preview?');}
      else if(data.state==='waiting'&&sent)throw Error('The map upload did not arrive. Click Preview to start a new session.');
      else throw Error('Unexpected preview response from EGGNOGG+.');
      await sleep(400);
    }
    throw Error('Preview timed out. Open EGGNOGG+ with the updated Yule runtime, allow the browser to connect locally, then try again.');
  }
  return {token,transfer};
}));
