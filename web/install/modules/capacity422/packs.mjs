// Versioned local recovery packs. Each bounded pack is closed and read back
// before its index is published. Existing per-segment folders remain supported.
import {save,saveJson,load,json} from '../shared396/site/files.mjs';
import {sha256} from '../shared396/site/backup/core.mjs';
const LIMIT=1048576;
export async function packedSink(folder,{create=false}={}){
 let index;
 if(create){try{await folder.getFileHandle('packs.json');throw Error('Existing packed recovery folder');}catch(e){if(e.name!=='NotFoundError')throw e;}index={schema:'sp1-recovery-packs-v1',packs:[],entries:[]};}
 else index=await json(folder,'packs.json');
 if(index.schema!=='sp1-recovery-packs-v1'||!Array.isArray(index.packs)||!Array.isArray(index.entries))throw Error('Recovery pack index');
 const map=new Map();
 for(const e of index.entries){const p=index.packs[e.pack];if(!/^(before-)?\d{6}$/.test(e.key)||map.has(e.key)||!p||p.name!==`pack-${String(e.pack).padStart(6,'0')}.bin`||!Number.isInteger(p.bytes)||p.bytes<512||p.bytes>LIMIT||!Number.isInteger(e.offset)||!Number.isInteger(e.bytes)||e.offset<0||e.offset%512||e.bytes<512||e.bytes%512||e.offset+e.bytes>p.bytes||!/^[a-f0-9]{64}$/.test(e.hash))throw Error('Recovery pack entry');map.set(e.key,e);}
 let sealed=!create;
 async function saveBatch(records){
  if(sealed||!records.length)throw Error('Packed recovery is sealed or empty');const keys=new Set();let size=0;
  for(const {key,bytes}of records){if(!/^(before-)?\d{6}$/.test(key)||map.has(key)||keys.has(key)||!(bytes instanceof Uint8Array)||!bytes.length||bytes.length%512)throw Error('Recovery pack record');keys.add(key);size+=bytes.length;}
  if(size>LIMIT)throw Error('Recovery pack too large');const payload=new Uint8Array(size),pack=index.packs.length,name=`pack-${String(pack).padStart(6,'0')}.bin`;let offset=0;const entries=[];
  for(const {key,bytes}of records){payload.set(bytes,offset);entries.push({key,pack,offset,bytes:bytes.length,hash:await sha256(bytes)});offset+=bytes.length;}
  // Refuse overwriting interrupted evidence. Use a new folder after a failed
  // preparation; no device write has been admitted at this stage.
  try{await folder.getFileHandle(name);throw Error('Existing interrupted recovery pack');}catch(e){if(e.name!=='NotFoundError')throw e;}
  await save(folder,name,payload);index.packs.push({name,bytes:size,hash:await sha256(payload)});index.entries.push(...entries);
  await saveJson(folder,'packs.json',index);for(const e of entries)map.set(e.key,e);
 }
 return {
  saveBatch,save:(key,bytes)=>saveBatch([{key,bytes}]),
  load:async key=>{const e=map.get(key);if(!e)throw Error('Missing recovery segment');const p=index.packs[e.pack],file=await(await folder.getFileHandle(p.name)).getFile();if(file.size!==p.bytes)throw Error('Recovery pack length');const b=new Uint8Array(await file.slice(e.offset,e.offset+e.bytes).arrayBuffer());if(await sha256(b)!==e.hash)throw Error('Recovery segment checksum');return b;},
  seal:async plan=>{if(sealed)throw Error('Recovery already sealed');await saveJson(folder,'plan.json',plan);sealed=true;}
 };
}

export async function recoverySink(folder){
 try{await folder.getFileHandle('packs.json');}catch(e){if(e.name!=='NotFoundError')throw e;const {sink}=await import('../shared396/site/files.mjs');return sink(folder);}
 return packedSink(folder);
}
