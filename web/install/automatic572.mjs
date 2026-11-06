// Small installation recovery only. Large recording backups still use a chosen folder.
import {save,saveJson,json,journal} from './files.mjs';
const ROOT='sp1-filesystem-recovery572',valid=id=>/^[a-f0-9]{16}$/.test(id);
export async function automaticFolder(identity,storage=navigator.storage){
 if(!valid(identity))throw Error('Invalid recovery identity');
 try{
  if(!storage?.getDirectory)throw Error('Browser storage unavailable');
  const estimate=await storage.estimate?.();if(estimate?.quota&&estimate.quota-(estimate.usage||0)<64*1048576)throw Error('Insufficient browser recovery space');
  try{await storage.persist?.()}catch{}
  const root=await storage.getDirectory(),devices=await root.getDirectoryHandle(ROOT,{create:true}),device=await devices.getDirectoryHandle(identity,{create:true}),parent=await device.getDirectoryHandle('setup-'+crypto.randomUUID(),{create:true});
  await save(parent,'storage-check.bin',new Uint8Array([57,2]));await parent.removeEntry('storage-check.bin');return parent;
 }catch(cause){throw Object.assign(Error('Choose a computer folder to keep installation recovery.'),{code:'FILESYSTEM_STORAGE_UNAVAILABLE',cause})}
}
export async function registerAutomatic(parent,folder,plan){
 const path=await parent.resolve(folder);if(!path||path.some(x=>!x||x==='.'||x==='..'))throw Error('Invalid automatic recovery location');
 await saveJson(parent,'recovery-location.json',{schema:572,identity:plan.identity,digest:plan.digest,path});
}
export async function completeAutomatic(folder,plan){await saveJson(folder,'browser-complete572.json',{identity:plan.identity,digest:plan.digest});}
export async function exportAutomatic(folder,parent){
 const out=await parent.getDirectoryHandle(folder.name,{create:true});
 async function copy(from,to){for await(const entry of from.values()){if(entry.kind==='directory')await copy(entry,await to.getDirectoryHandle(entry.name,{create:true}));else await save(to,entry.name,new Uint8Array(await(await entry.getFile()).arrayBuffer()));}}
 await copy(folder,out);return out;
}
export async function findAutomatic(identity,storage=navigator.storage){
 if(!valid(identity))throw Error('Invalid recovery identity');if(!storage?.getDirectory)return null;
 let device;try{device=await(await(await storage.getDirectory()).getDirectoryHandle(ROOT)).getDirectoryHandle(identity)}catch(e){if(e.name==='NotFoundError')return null;throw e}
 const matches=[];let count=0;
 for await(const dir of device.values()){
  if(dir.kind!=='directory')continue;if(++count>128)throw Error('Too many saved setup sessions; recovery needs review');
  let ref;try{ref=await json(dir,'recovery-location.json')}catch(e){if(e.name==='NotFoundError')continue;throw e}
  if(ref.schema!==572||ref.identity!==identity||!Array.isArray(ref.path)||ref.path.length>6||ref.path.some(x=>typeof x!=='string'||!x||x==='.'||x==='..'||x.includes('/')))throw Error('Invalid saved recovery location');
  let folder=dir;for(const name of ref.path)folder=await folder.getDirectoryHandle(name);
  const plan=await json(folder,'plan.json');if(plan.identity!==identity||plan.digest!==ref.digest)throw Error('Saved recovery identity/checksum mismatch');
  // Reading the journal must not create one on a merely prepared plan.
  try{await folder.getDirectoryHandle('events')}catch(e){if(e.name==='NotFoundError')continue;throw e}
  const events=await(await journal(folder)).read();
  let finished;try{finished=await json(folder,'browser-complete572.json')}catch(e){if(e.name!=='NotFoundError')throw e}
  if(finished){if(finished.identity!==identity||finished.digest!==plan.digest)throw Error('Invalid completion record');continue;}
  // A journal can be complete before the controller gate is released. Retain
  // that session until the verified apply/recover call has actually returned.
  if(events.some(e=>e.state==='recovered'&&e.digest===plan.digest))continue;
  if(events.some(e=>e.state==='admitted'&&e.digest===plan.digest))matches.push({folder,plan});
 }
 if(matches.length>1)throw Error('Multiple interrupted installations need review');return matches[0]||null;
}
