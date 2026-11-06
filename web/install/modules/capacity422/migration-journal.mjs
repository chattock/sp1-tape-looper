import {catalogFingerprint} from './preservation.mjs';
import {checkJournal,checkUnattempted,releaseCompleted} from './recovery.mjs';
// Offline-tested coordinator. Production must supply a matching paged runtime
// and a persistent boot gate; this module never discovers or opens a device.
import {read,equal,inspect} from './fat.mjs';
import {inspect422,INDEX_SECTORS} from './filesystem.mjs';
import {sha256} from '../shared396/site/backup/core.mjs';
const enc=x=>new TextEncoder().encode(JSON.stringify(x));
const fail=m=>{throw Error(m)};
const integer=(x,min,max)=>Number.isSafeInteger(x)&&x>=min&&x<=max;
export async function validateCapacity(p,sink){
 if(p?.schema!=='sp1-capacity422-v1'||!/^([0-9a-f]{16})$/.test(p.identity)||!integer(p.capacity,1,0xffffffff)||!['/SP1','/TAPELOOP'].includes(p.sourcePath)||p.path!=='/TAPELOOP'||!Array.isArray(p.extents)||!p.extents.length||!Array.isArray(p.guards)||!p.guards.length)fail('Invalid capacity plan');
 if(!/^[0-9a-f]{64}$/.test(p.catalogHash))fail('Missing complete-library preservation fingerprint');
 const {digest,...rest}=p;if(await sha256(enc(rest))!==digest)fail('Capacity plan checksum');
 if(!integer(p.poolFirst,1,p.capacity-1)||!integer(p.oldPoolBlocks,1,p.capacity-p.poolFirst)||!integer(p.poolBlocks,p.oldPoolBlocks,p.capacity-p.poolFirst)||p.indexSectors!==INDEX_SECTORS||!Array.isArray(p.indexFirst)||p.indexFirst.length!==2)fail('Invalid capacity geometry');
 if(!integer(p.workFirst,1,p.capacity-1)||!integer(p.workBlocks,131072,524288)||p.workBlocks%64)fail('Invalid maintenance workspace geometry');
 const ranges=[[p.workFirst,p.workBlocks],[p.poolFirst,p.poolBlocks],...p.indexFirst.map(x=>[x,p.indexSectors])];
 for(let i=0;i<ranges.length;i++){const [a,n]=ranges[i];if(!integer(a,1,p.capacity-n))fail('Invalid index extent');for(let j=0;j<i;j++){const [b,m]=ranges[j];if(a<b+m&&b<a+n)fail('Overlapping owned files');}}
 const keys=new Set(),sectors=new Set();for(const e of p.extents){
  if(!/^\d{6}$/.test(e.key)||keys.has(e.key)||!integer(e.lba,0,p.capacity-1)||e.blocks!==1||sectors.has(e.lba)||(e.lba>=p.poolFirst&&e.lba<p.poolFirst+p.oldPoolBlocks))fail('Unsafe migration extent');keys.add(e.key);sectors.add(e.lba);
  for(const original of[false,true]){const b=await sink.load((original?'before-':'')+e.key);if(b.length!==512||await sha256(b)!==(original?e.original:e.target))fail('Recovery file checksum');}
 }
 for(const g of p.guards)if(!integer(g.lba,0,p.capacity-1)||!integer(g.blocks,1,p.capacity-g.lba)||!/^([0-9a-f]{64})$/.test(g.hash))fail('Invalid source guard');
 return p;
}
async function bind(d,p){
 const id=await d.identify();if(id.identity!==p.identity||id.capacity!==p.capacity)fail('Different device');d.capacity=id.capacity;
 if(d.sharedProfile!=='bounded-fat396'||!d.bootGate?.persistent||typeof d.bootGate.status!=='function'||d.capacityProfile!=='paged422-v1')fail('Matching runtime and persistent recovery gate required');

}
async function compare(d,p,original,progress=()=>{}){let done=0;const phase=original?'compare-original-metadata':'verify-written-metadata';progress({phase,done,total:p.extents.length*512});for(const e of p.extents){if(await sha256(await read(d,e.lba,1))!==(original?e.original:e.target))fail('Migration comparison failed');progress({phase,done:++done*512,total:p.extents.length*512});}}
async function writes(d,sink,p,j,original,progress){
 const order=original?[...p.extents].reverse():p.extents;let done=0;
 for(const e of order){const b=await sink.load((original?'before-':'')+e.key),current=await read(d,e.lba,1);
  if(!equal(current,b)){await j({state:original?'restore-intent':'write-intent',digest:p.digest,lba:e.lba});await d.writeBounded(e.lba,current,b);await d.sync();if(!equal(await read(d,e.lba,1),b))fail('Migration write readback failed');}
  progress({phase:original?'restore-capacity':'migrate-capacity',done:++done*512,total:order.length*512});
 }
}
async function finish(d,p,j,original,replay=false,progress=()=>{}){
 await compare(d,p,original,progress);progress({phase:'verify-filesystem-and-takes',done:0,total:0});const s=await(original?inspect:inspect422)(d),n=s.native;
 if(!n||n.path!==(original?p.sourcePath:p.path)||n.extents[2].lba!==p.poolFirst||n.extents[2].blocks!==(original?p.oldPoolBlocks:p.poolBlocks))fail('Restored filesystem admission failed');
 if(!original&&(!n.workspace||n.workspace.lba!==p.workFirst||n.workspace.blocks!==p.workBlocks))fail('Workspace admission failed');
 if(!original&&(n.format!==5||n.extents[0].lba!==p.indexFirst[0]||n.extents[1].lba!==p.indexFirst[1]))fail('Paged index admission failed');
 if(!original&&await catalogFingerprint(n.banks[n.active].index)!==p.catalogHash)fail('Migrated take/control preservation mismatch');
 if(!replay){await j({state:original?'restored':'verified',digest:p.digest});await j({state:'complete',digest:p.digest,restored:original});}await releaseCompleted(d,p,replay);return s;
}
export async function applyCapacity(d,sink,p,j,progress=()=>{}){
 await validateCapacity(p,sink);await bind(d,p);if((await j.read()).length)fail('Existing attempt; recover it');
 let checked=0;const guardBytes=p.guards.reduce((n,g)=>n+g.blocks*512,0);progress({phase:'verify-source-guards',done:0,total:guardBytes});for(const g of p.guards){if(await sha256(await read(d,g.lba,g.blocks))!==g.hash)fail('Source changed since preparation');checked+=g.blocks*512;progress({phase:'verify-source-guards',done:checked,total:guardBytes});}
 await compare(d,p,true,progress);await j({state:'admitted',digest:p.digest,identity:p.identity});await d.bootGate.lock(p.digest);await d.begin();await writes(d,sink,p,j,false,progress);return finish(d,p,j,false,false,progress);
}
export async function recoverCapacity(d,sink,p,j,{rollback=false}={},progress=()=>{}){
 await validateCapacity(p,sink);await bind(d,p);const events=await j.read();
 const completed=checkJournal(events,p,{intents:['write-intent','restore-intent'],completion:'restored'});
 if(completed){if(rollback!==completed.restored)fail('Completed migration cannot be rolled back over later recordings');return finish(d,p,j,completed.restored,true,progress);}
 const gate=await d.bootGate.status();
 if(!gate.blocked&&events.some(e=>e.state==='write-intent'||e.state==='restore-intent'))fail('Transaction released; refuse recovery over later recordings');
 await checkUnattempted(d,sink,p,events,['write-intent','restore-intent']);
 await d.bootGate.lock(p.digest);await d.begin();await writes(d,sink,p,j,rollback,progress);return finish(d,p,j,rollback,false,progress);
}
