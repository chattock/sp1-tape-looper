// Shared recovery invariants. No device discovery, retries or implicit unlock.
import {read} from './fat.mjs';
import {sha256} from '../shared396/site/backup/core.mjs';
export function checkJournal(events,p,{intents,completion}){
 if(!Array.isArray(events)||!events.length||events[0].state!=='admitted'||events[0].identity!==p.identity||events.some(e=>e.digest!==p.digest))throw Error('Original recovery journal required');
 const allowed=new Set(['admitted','verified','restored','complete',...intents]);
 let complete=null;
 for(let i=0;i<events.length;i++){
  const e=events[i];if(!allowed.has(e.state)||(i>0&&e.state==='admitted')||complete)throw Error('Invalid recovery journal ordering');
  if(e.state==='complete'){if(typeof e[completion]!=='boolean')throw Error('Invalid completion direction');complete=e;}
  if(intents.includes(e.state)){
   const extent=p.extents.find(x=>Number.isSafeInteger(e.lba)&&e.lba>=x.lba&&e.lba<x.lba+x.blocks);
   if(!extent||(e.key!==undefined&&e.key!==extent.key)||!Number.isSafeInteger(e.blocks??1)||(e.blocks??1)<1||(e.blocks??1)>16||e.lba+(e.blocks??1)>extent.lba+extent.blocks)throw Error('Intent outside recovery plan');
  }
 }
 return complete;
}
// Compare every never-attempted sector, including untouched parts of a chunked
// restore extent. An intent for one chunk must not authorize its whole file.
export async function checkUnattempted(d,sink,p,events,intents){
 const attempted=new Set();for(const e of events)if(intents.includes(e.state))for(let i=0;i<(e.blocks??1);i++)attempted.add(e.lba+i);
 for(const e of p.extents){const original=await sink.load('before-'+e.key);for(let off=0;off<e.blocks;off+=16){const n=Math.min(16,e.blocks-off),now=await read(d,e.lba+off,n);for(let i=0;i<n;i++)if(!attempted.has(e.lba+off+i)){
  const at=(off+i)*512;if(now.subarray(i*512,(i+1)*512).some((v,j)=>v!==original[at+j]))throw Error('Unattempted extent changed');
 }}}
 // Guards outside the write plan protect foreign directories and source roots.
 // A partially edited FAT guard cannot be compared wholesale; check each
 // unchanged sector against a saved guard only when the entire range is intact.
 for(const g of p.guards)if(!p.extents.some(e=>e.lba<g.lba+g.blocks&&g.lba<e.lba+e.blocks))if(await sha256(await read(d,g.lba,g.blocks))!==g.hash)throw Error('Unchanged source guard changed');
}
export async function releaseCompleted(d,p,replay=false){
 const gate=await d.bootGate.status();
 // Completed transactions may be opened again only for verification. Never
 // acquire a fresh gate or rewrite music on replay. A still-locked gate must
 // match this digest (the device checks it) before it can be released.
 if(!gate.blocked&&!replay)throw Error('Recovery gate unexpectedly released');
 if(gate.blocked)await d.bootGate.unlock(p.digest);
}
