 'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),P=require('./preview-package.js');
const files={'data.json':'{}','data.map':'x','map.lua':'-- preview\n'};
test('browser packet matches the native fixture byte for byte',()=>{assert.deepEqual(Buffer.from(P.encode(files)),fs.readFileSync(__dirname+'/../tests/fixtures/preview-package.bin'));});
test('binary assets survive exactly and editor-only metadata is omitted',()=>{const bytes=new Uint8Array([137,80,0,255]);const packet=P.encode({...files,'sheet.png':bytes,'logic.greggnogg.json':'editor state'});assert.deepEqual(packet.slice(-4),bytes);assert.equal(new DataView(packet.buffer).getUint32(4,true),4);});
test('unsafe paths, Windows devices, duplicates and invalid text reject',()=>{
 for(const name of ['../a.png','CON.png','com1.extra.png','a/b.png','map.LUA','x.png:stream','.png'])assert.throws(()=>P.encode({...files,[name]:'x'}));
 assert.throws(()=>P.encode({...files,'x.png':new Uint8Array(),'X.PNG':new Uint8Array()}));
 assert.throws(()=>P.encode({...files,'map.lua':'bad\0text'}));assert.throws(()=>P.encode({'data.json':'{}'}));
 assert.throws(()=>P.encode({...files,'map.lua':'x'.repeat(262145)}));
});
