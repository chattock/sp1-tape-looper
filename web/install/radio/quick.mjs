import {check,digest,equal,SIZE,SECTOR,allowed,TARGET} from './plan.mjs?v=565';
import {saveNew,readFile,newTransaction} from './storage.mjs?v=565';
const json=async(d,n,max=32768)=>JSON.parse(new TextDecoder().decode(await readFile(d,n,max)));
export async function compactRecovery(before,manifest){
 check(before.length===SIZE&&await digest(before)===manifest.before_sha256,'Original radio hash mismatch');
 check(manifest.adapter===335&&['migrate','rehearse'].includes(manifest.mode)&&manifest.sectors.length<=28&&new Set(manifest.sectors).size===manifest.sectors.length&&manifest.sectors.every(allowed),'Invalid recovery sectors');
 const data=new Uint8Array(manifest.sectors.length*SECTOR);
 manifest.sectors.forEach((a,i)=>data.set(before.subarray(a,a+SECTOR),i*SECTOR));
 return {data,record:{schema:'sp1-radio-sectors530',identity:manifest.identity,bytes:data.length,sha256:await digest(data),manifest}};
}
export async function markComplete(dir,identity){
 const data=new TextEncoder().encode(JSON.stringify({identity,at:Date.now(),verified:true}));
 const file=await dir.getFileHandle('complete.json',{create:true}),writer=await file.createWritable({keepExistingData:false});
 try{await writer.write(data);await writer.close()}catch(e){try{await writer.abort()}catch{}throw e}
 check(equal(data,await readFile(dir,'complete.json',1024)),'Completion readback failed');
}
export async function markNoWrite(dir,identity){
 check(/^[a-f0-9]{16}$/.test(identity),'Invalid cancellation identity');
 const data=new TextEncoder().encode(JSON.stringify({identity,at:Date.now(),no_write_capable_arm:true,sector_seal_sent:false,recovery_required_at_start:false}));
 const file=await dir.getFileHandle('unarmed.json',{create:true}),writer=await file.createWritable({keepExistingData:false});
 try{await writer.write(data);await writer.close()}catch(e){try{await writer.abort()}catch{}throw e}
 check(equal(data,await readFile(dir,'unarmed.json',1024)),'Unarmed cancellation readback failed');
}
export async function quickRoot(storage=navigator.storage){
 try{
  check(storage?.getDirectory,'Automatic recovery storage is unavailable.');
  // Retention protects against eviction; it is not permission to write OPFS.
  // Test actual disk writes/readback below. Never fall back to volatile RAM.
  let retained=false;try{retained=!!await storage.persisted?.();if(!retained)retained=!!await storage.persist?.()}catch{}
  const root=await storage.getDirectory(),dir=await root.getDirectoryHandle('sp1-radio-recovery530',{create:true});
  // Closed-file readback remains mandatory before any update is authorized.
  const probe='check-'+crypto.randomUUID();await saveNew(dir,probe,new Uint8Array([53,30]));await dir.removeEntry(probe);quickRoot.retentionProtected=retained;return dir;
 }catch(cause){throw Object.assign(Error('This browser cannot keep automatic recovery. Choose a backup folder.'),{code:'RADIO_STORAGE_UNAVAILABLE',cause})}
}
export async function quickTransaction(root,mode,identity){
 check(/^[0-9a-f]{16}$/.test(identity),'Invalid device identity');
 const device=await root.getDirectoryHandle(identity,{create:true}),t=await newTransaction(device,mode);
 return {...t,save:async(name,value)=>{
  // Full readback is verified in memory by the existing protocol. Retain only
  // original changed sectors, their plan and bounded diagnostics on disk.
  if(['radio-backup.bin','radio-after.bin','private-raw.txt','private-partial.txt'].includes(name))return;
  await t.save(name,value);
 },prepareRecovery:async(before,manifest)=>{
  if(!['migrate','rehearse'].includes(mode)||!manifest.sectors.length)return;
  const c=await compactRecovery(before,manifest);
  await t.save('original-sectors.bin',c.data);await t.save('sector-recovery.json',c.record);
  const loaded=await loadCompact(t.dir);check(loaded.record.sha256===c.record.sha256,'Recovery readback mismatch');
  // Persisted before ARM; a failure here leaves the updater unable to write.
  await t.save('pending.json',{identity,mode,created:Date.now()});
 },complete:async()=>markComplete(t.dir,identity)};
}
export async function loadCompact(dir){
 const record=await json(dir,'sector-recovery.json'),data=await readFile(dir,'original-sectors.bin',28*SECTOR);
 check(record.schema==='sp1-radio-sectors530'&&record.identity===record.manifest?.identity&&record.bytes===data.length&&await digest(data)===record.sha256,'Automatic recovery data is damaged');
 return {manifest:record.manifest,compact:data,record,name:dir.name};
}
export async function pendingRecovery(root,identity){
 let device;try{device=await root.getDirectoryHandle(identity)}catch(e){if(e.name==='NotFoundError')return null;throw e}
 const found=[];
 for await(const dir of device.values()){
  if(dir.kind!=='directory')continue;
  let p;try{p=await json(dir,'pending.json')}catch(e){if(e.name!=='NotFoundError'&&!(e instanceof SyntaxError))throw e;try{await dir.getFileHandle('sector-recovery.json')}catch(m){if(m.name==='NotFoundError')continue;throw m}p={identity,created:0}}
  try{const c=await json(dir,'complete.json',1024);if(c.identity===identity&&c.verified===true&&Number.isFinite(c.at))continue}catch(e){if(e.name!=='NotFoundError'&&!(e instanceof SyntaxError))throw e}
  try{const c=await json(dir,'unarmed.json',1024);if(c.identity===identity&&c.no_write_capable_arm===true&&c.sector_seal_sent===false&&c.recovery_required_at_start===false&&Number.isFinite(c.at))continue}catch(e){if(e.name!=='NotFoundError'&&!(e instanceof SyntaxError))throw e}
  const original=await loadCompact(dir);check(p.identity===identity&&original.manifest.identity===identity,'Recovery device mismatch');found.push({original,dir,at:Number.isFinite(p.created)?p.created:0});
 }
 found.sort((a,b)=>b.at-a.at);check(found.length<=1,'Multiple unfinished Bluetooth updates need review. Keep this browser’s data.');return found[0]||null;
}
export async function automaticOriginal(identity){
 // No write or new storage permission is requested while looking for recovery.
 if(!navigator.storage?.getDirectory)return null;
 const root=await navigator.storage.getDirectory();let dir;try{dir=await root.getDirectoryHandle('sp1-radio-recovery530')}catch(e){if(e.name==='NotFoundError')return null;throw e}
 return pendingRecovery(dir,identity);
}
// Read-only, local export for interrupted-browser diagnostics. Never writes the
// radio or changes completion/pending markers; include completed journals too.
export async function exportBrowserRecovery(identity){
 const out={schema:'sp1-radio-browser-export540',identity,origin:location.origin,at:new Date().toISOString(),devices:[],transactions:[]};
 const root=await navigator.storage.getDirectory();let store;
 try{store=await root.getDirectoryHandle('sp1-radio-recovery530')}catch(e){if(e.name==='NotFoundError'){out.missing=true;return out}throw e}
 for await(const d of store.values())if(d.kind==='directory'&&/^[a-f0-9]{16}$/.test(d.name))out.devices.push(d.name);
 if(!/^[a-f0-9]{16}$/.test(identity||''))return out;
 let device;try{device=await store.getDirectoryHandle(identity)}catch(e){if(e.name==='NotFoundError')return out;throw e}
 for await(const d of device.values()){
  if(d.kind!=='directory')continue;check(out.transactions.length<64,'Too many recovery transactions to export');const t={name:d.name,files:[]};out.transactions.push(t);
  for await(const h of d.values()){
   if(h.kind!=='file')continue;const f=await h.getFile(),entry={name:h.name,size:f.size};t.files.push(entry);
   if(['original-sectors.bin','sector-recovery.json','pending.json','complete.json','unarmed.json','recovery.json','write-intent.json','summary.json','failure-summary.json','compatibility.json','browser-session.json'].includes(h.name)){
    check(f.size<=131072,'Recovery export file exceeds bound');const b=new Uint8Array(await f.arrayBuffer());entry.hex=Array.from(b,v=>v.toString(16).padStart(2,'0')).join('');
   }
  }
 }
 return out;
}
export async function completedUpdate(identity,preferredName=null){
 const root=await navigator.storage.getDirectory();let device;
 try{device=await(await root.getDirectoryHandle('sp1-radio-recovery530')).getDirectoryHandle(identity)}catch(e){if(e.name==='NotFoundError')return null;throw e}
 const candidates=[];
 for await(const dir of device.values()){
  if(dir.kind!=='directory'||(preferredName&&dir.name!==preferredName))continue;
  try{
   const complete=await json(dir,'complete.json',1024),summary=await json(dir,'summary.json',4096);
   if(complete.identity!==identity||complete.verified!==true||!Number.isFinite(complete.at)||summary.status!=='BROWSER ADAPTER MIGRATE PASS'||summary.setup!==335)continue;
   let m;
   try{m=(await loadCompact(dir)).manifest}catch(e){
    if(e.name!=='NotFoundError')throw e;
    // Already-current checks have zero changed sectors, so no original-sector
    // payload exists. A sealed full-image result + exact no-change plan suffice.
    m=await json(dir,'recovery.json');
    if(!Array.isArray(m.sectors)||m.sectors.length||m.before_sha256!==m.after_sha256||!/^[a-f0-9]{64}$/.test(m.after_sha256))continue;
   }
   if(m.adapter!==335||m.identity!==identity||m.mode!=='migrate'||m.release_sha256!==TARGET||summary.full_flash_sha256!==m.after_sha256||summary.sectors!==m.sectors.length)continue;
   candidates.push({summary,manifest:m,at:complete.at,name:dir.name,compatibility:await json(dir,'compatibility.json')});
  }catch(e){if(e.name==='NotFoundError')continue;throw e}
 }
 candidates.sort((a,b)=>b.at-a.at);return candidates[0]||null;
}
export async function idleFailure(identity,name){
 // Only use with a newly identified, recovery=0 idle helper or normal runtime.
 // A saved plan/original disqualifies this shortcut; uncertain writes recover.
 if(!/^[a-f0-9]{16}$/.test(identity||'')||!name)return null;
 let dir;
 try{const root=await navigator.storage.getDirectory();dir=await(await(await root.getDirectoryHandle('sp1-radio-recovery530')).getDirectoryHandle(identity)).getDirectoryHandle(name)}catch(e){if(e.name==='NotFoundError')return null;throw e}
 let f;try{f=await json(dir,'failure-summary.json',16384)}catch(e){if(e.name==='NotFoundError')return null;throw e}
 if(f.status!=='STOP'||f.setup!==335||!['migrate','inspect'].includes(f.mode)||f.arm_sent_or_attempted!==false||f.sector_seal_sent_or_attempted!==false||f.recovery_required_at_start===true)return null;
 for(const file of ['recovery.json','sector-recovery.json','write-intent.json']){
  try{await dir.getFileHandle(file);return null}catch(e){if(e.name!=='NotFoundError')throw e}
 }
 return {dir,name};
}
