(function(root,factory){if(typeof module==='object'&&module.exports)module.exports=factory();else root.GregPreviewPackage=factory();}(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const MAX=128*1024*1024,encoder=new TextEncoder();
  function validName(name){if(['data.json','data.map','map.lua','entities.json'].includes(name))return true;return name.length<128&&/^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.png$/i.test(name)&&!/^(con|prn|aux|nul|com[1-9]|lpt[1-9])\./i.test(name);}
  function encode(files){
    const names=Object.keys(files).filter(name=>!['objects.greggnogg.json','logic.greggnogg.json'].includes(name)).sort();
    if(!names.includes('data.json')||!names.includes('data.map')||names.length>64)throw Error('Preview requires data.json and data.map, with at most 64 files.');
    let size=8;const seen=new Set(),entries=[];
    for(const name of names){
      if(!validName(name)||seen.has(name.toLowerCase()))throw Error('Unsafe or duplicate preview filename: '+name);seen.add(name.toLowerCase());
      const bytes=typeof files[name]==='string'?encoder.encode(files[name]):files[name];
      if(!(bytes instanceof Uint8Array))throw Error('Invalid preview bytes: '+name);
      const limit=({'data.json':4194304,'data.map':4194304,'map.lua':262144,'entities.json':1048576})[name]||67108864;
      if(bytes.length>limit||(limit!==67108864&&bytes.includes(0))||(['data.json','data.map'].includes(name)&&!bytes.length))throw Error('Invalid or oversized preview file: '+name);
      const key=encoder.encode(name);size+=8+key.length+bytes.length;if(size>MAX)throw Error('Preview package exceeds 128 MiB.');entries.push({key,bytes});
    }
    const result=new Uint8Array(size),view=new DataView(result.buffer);result.set(encoder.encode('GGP2'));view.setUint32(4,entries.length,true);let offset=8;
    for(const {key,bytes} of entries){view.setUint16(offset,key.length,true);view.setUint32(offset+4,bytes.length,true);offset+=8;result.set(key,offset);offset+=key.length;result.set(bytes,offset);offset+=bytes.length;}
    return result;
  }
  return {encode,validName,MAX_BYTES:MAX};
}));
