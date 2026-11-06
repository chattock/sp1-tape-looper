// Preserving offline workspace expansion. No payload relocation or formatting.
import {inspect422,INDEX_SECTORS} from './filesystem.mjs';
import {read,put,u32,lba,equal} from './fat.mjs';
import {encodeIndex,decodeIndex} from './paged/binary.mjs';
import {validateCatalog} from './paged/catalog.mjs';
import {sha256} from '../shared396/site/backup/core.mjs';
import {checkJournal,checkUnattempted,releaseCompleted} from './recovery.mjs';
export const WORKSPACE571_BYTES=256*1048576;
const enc=x=>new TextEncoder().encode(JSON.stringify(x)),fail=m=>{throw Error(m)},integer=(n,a,b)=>Number.isSafeInteger(n)&&n>=a&&n<=b;
function canonical(index){validateCatalog(index);return [...index.records].sort((a,b)=>a[0]-b[0]).map(([id,b])=>{b=b.slice();const type=u32(b,0);if(id===0)put(b,16,0);if(type===3&&!u32(b,20)){const next=u32(b,28);b.fill(0);put(b,0,5);put(b,28,next);}return[id,Array.from(b)];});}
export async function musicFingerprint571(index){return sha256(enc(canonical(index)));}
const foreign=s=>s.files.filter(f=>!f.path.startsWith('/TAPELOOP/')).map(f=>({path:f.path,attr:f.attr,cluster:f.cluster,bytes:f.bytes})).sort((a,b)=>a.path.localeCompare(b.path));
async function bind(d,p){const id=await d.identify();if(id.identity!==p.identity||id.capacity!==p.capacity)fail('Different SP-1');if(!d.largeImports||!d.bootGate?.persistent||d.sharedProfile!=='bounded-fat396')fail('Install the current STANDARD BIN before expanding import space');}
export async function prepareWorkspace571(d,sink,progress=()=>{}){
 const id=await d.identify();d.capacity=id.capacity;if(!d.largeImports)fail('Install the current STANDARD BIN before expanding import space');
 if((await d.bootGate.status()).blocked)fail('Finish the interrupted operation before expanding import space');
 const s=await inspect422(d,progress),n=s.native;if(n?.format!==5||!n.workspace)fail('A verified paged library is required');
 if(n.workspace.blocks*512>=WORKSPACE571_BYTES)return null;
 const {g}=s;if(g.spc!==64)fail('Unsupported workspace cluster size');
 const source=n.banks[n.active].index,records=new Map([...source.records].map(([k,v])=>[k,v.slice()])),fat=s.fat.slice(),audio=n.extents[2],audioStart=(audio.lba-g.data)/g.spc+2,oldCount=audio.blocks/g.spc,oldWorkStart=(n.workspace.lba-g.data)/g.spc+2;
 const live=[...records].filter(([,b])=>u32(b,0)===3&&u32(b,20)>0).map(([id,b])=>({id,first:u32(b,4),blocks:u32(b,8),crc:u32(b,12)}));
 const high=live.reduce((m,o)=>Math.max(m,o.first+o.blocks),0),need=WORKSPACE571_BYTES/(g.spc*512);
 for(let i=0;i<n.workspace.clusters;i++)put(fat,(oldWorkStart+i)*4,0);
 function find(){let run=0;for(let c=g.end-1;c>=2;c--){if(!(u32(fat,c*4)&0xfffffff)){if(++run===need)return c;}else run=0;}return 0;}
 let poolBlocks=audio.blocks,work=find();
 if(!work){const keep=Math.max(Math.ceil(high/g.spc),oldCount-need);if(keep<1||keep>=oldCount)fail('Not enough unused storage for larger imports. Saved recordings were left unchanged.');
  for(let c=audioStart+keep;c<audioStart+oldCount;c++)put(fat,c*4,0);put(fat,(audioStart+keep-1)*4,0x0fffffff);poolBlocks=keep*g.spc;work=find();}
 if(!work)fail('Not enough contiguous unused storage for larger imports. Saved recordings were left unchanged.');
 for(let i=0;i<need;i++)put(fat,(work+i)*4,i===need-1?0x0fffffff:work+i+1);
 let free=0;for(let c=2;c<g.end;c++)if(!(u32(fat,c*4)&0xfffffff))free++;
 // Free-ID descriptors are not recordings. Preserve their free-list links while
 // dropping obsolete free extents that would point past the reduced pool tail.
 for(const [id,b]of records)if(u32(b,0)===3&&!u32(b,20)){const next=u32(b,28);b.fill(0);put(b,0,5);put(b,28,next);}
 put(records.get(0),16,poolBlocks);
 const target=encodeIndex(records,{generation:source.generation+1n,sectors:INDEX_SECTORS}),targetIndex=decodeIndex(target.image);validateCatalog(targetIndex);
 const preserve=await musicFingerprint571(source);if(preserve!==await musicFingerprint571(targetIndex))fail('Recording preservation check failed');
 const changes=new Map();function set(at,b){for(let i=0;i<b.length;i+=512)changes.set(at+i/512,b.slice(i,i+512));}
 const own=s.dirs.find(x=>x.path==='/TAPELOOP');if(!own)fail('Owned directory missing');const directory=own.b.slice(),ae=own.entries.find(e=>e.name==='AUDIO   BIN'),we=own.entries.find(e=>e.name==='WORK    BIN');if(!ae||!we)fail('Owned files missing');put(directory,ae.offset+28,poolBlocks*512);new DataView(directory.buffer).setUint16(we.offset+20,work>>>16,true);new DataView(directory.buffer).setUint16(we.offset+26,work&65535,true);put(directory,we.offset+28,WORKSPACE571_BYTES);
 for(let i=0;i<own.cs.length;i++)set(lba(g,own.cs[i]),directory.subarray(i*g.spc*512,(i+1)*g.spc*512));
 set(lba(g,work),new Uint8Array(1024));for(const e of n.extents.slice(0,2))set(e.lba,target.image.subarray(0,target.used*512));
 set(g.first+g.reserved,fat);set(g.first+g.reserved+g.fat,fat);const info=s.info.slice();put(info,488,free);put(info,492,0xffffffff);set(g.first+1,info);set(g.first+7,info);
 const p={schema:'sp1-workspace571-v1',identity:id.identity,capacity:id.capacity,poolFirst:audio.lba,oldPoolBlocks:audio.blocks,poolBlocks,oldWorkFirst:n.workspace.lba,oldWorkBlocks:n.workspace.blocks,workFirst:lba(g,work),workBlocks:WORKSPACE571_BYTES/512,indexFirst:n.extents.slice(0,2).map(e=>e.lba),sourceGeneration:String(source.generation),targetGeneration:String(targetIndex.generation),sourceHash:await sha256(n.banks[n.active].bytes),preserveHash:preserve,live,foreign:foreign(s),guards:[],extents:[]};
 await sink.save('source-index',n.banks[n.active].bytes);await sink.save('target-index',target.image.subarray(0,target.used*512));
 for(const guard of s.guards)p.guards.push({lba:guard.lba,blocks:guard.b.length/512,hash:await sha256(guard.b)});
 p.guards.push({lba:n.extents[n.active].lba,blocks:n.banks[n.active].bytes.length/512,hash:p.sourceHash});
 let done=0;for(const [at,after]of changes){progress({phase:'prepare-recovery',done:done++*512,total:changes.size*512});if(live.some(o=>at>=audio.lba+o.first&&at<audio.lba+o.first+o.blocks))fail('Workspace plan overlaps a recording');const before=await read(d,at,1);if(equal(before,after))continue;const key=String(p.extents.length).padStart(6,'0');await sink.save(key,after);await sink.save('before-'+key,before);p.extents.push({key,lba:at,blocks:1,original:await sha256(before),target:await sha256(after)});}
 p.digest=await sha256(enc(p));await sink.seal(p);await validateWorkspace571(p,sink);return p;
}
export async function validateWorkspace571(p,sink){
 if(p?.schema!=='sp1-workspace571-v1'||!/^([a-f0-9]{16})$/.test(p.identity)||!integer(p.capacity,1,0xffffffff)||p.workBlocks!==524288||!integer(p.poolFirst,1,p.capacity-1)||!integer(p.oldPoolBlocks,1,p.capacity-p.poolFirst)||!integer(p.poolBlocks,1,p.oldPoolBlocks)||!integer(p.workFirst,1,p.capacity-p.workBlocks)||!Array.isArray(p.extents)||!p.extents.length||!Array.isArray(p.guards)||!p.guards.length)fail('Invalid workspace recovery plan');
 const{digest,...body}=p;if(await sha256(enc(body))!==digest)fail('Workspace recovery checksum');
 const beforeBytes=await sink.load('source-index'),afterBytes=await sink.load('target-index');if(await sha256(beforeBytes)!==p.sourceHash)fail('Source index checksum');const before=decodeIndex(beforeBytes),after=decodeIndex(afterBytes);validateCatalog(before);validateCatalog(after);
 if(String(before.generation)!==p.sourceGeneration||after.generation!==before.generation+1n||String(after.generation)!==p.targetGeneration||u32(before.records.get(0),16)!==p.oldPoolBlocks||u32(after.records.get(0),16)!==p.poolBlocks||await musicFingerprint571(before)!==p.preserveHash||await musicFingerprint571(after)!==p.preserveHash)fail('Workspace recording preservation mismatch');
 const live=[...before.records].filter(([,b])=>u32(b,0)===3&&u32(b,20)>0).map(([id,b])=>({id,first:u32(b,4),blocks:u32(b,8),crc:u32(b,12)}));if(JSON.stringify(live)!==JSON.stringify(p.live))fail('Workspace recording extent mismatch');
 const ranges=[[p.poolFirst,p.poolBlocks],[p.workFirst,p.workBlocks],...(p.indexFirst||[]).map(x=>[x,INDEX_SECTORS])];if(ranges.length!==4)fail('Workspace index geometry');for(let i=0;i<ranges.length;i++){const[a,n]=ranges[i];if(!integer(a,1,p.capacity-n))fail('Workspace range');for(let j=0;j<i;j++){const[b,m]=ranges[j];if(a<b+m&&b<a+n)fail('Workspace file overlap');}}
 const keys=new Set(),sectors=new Set();for(const e of p.extents){if(!/^\d{6}$/.test(e.key)||keys.has(e.key)||!integer(e.lba,0,p.capacity-1)||e.blocks!==1||sectors.has(e.lba)||live.some(o=>e.lba>=p.poolFirst+o.first&&e.lba<p.poolFirst+o.first+o.blocks))fail('Unsafe workspace extent');keys.add(e.key);sectors.add(e.lba);for(const old of[true,false]){const bytes=await sink.load((old?'before-':'')+e.key);if(bytes.length!==512||await sha256(bytes)!==(old?e.original:e.target))fail('Workspace recovery payload checksum');}}
 return p;
}
async function finish(d,p,j,rollback,replay=false){
 for(const e of p.extents)if(await sha256(await read(d,e.lba,1))!==(rollback?e.original:e.target))fail('Workspace verification failed');
 const s=await inspect422(d),n=s.native;if(n?.format!==5||n.extents[2].lba!==p.poolFirst||n.extents[2].blocks!==(rollback?p.oldPoolBlocks:p.poolBlocks)||n.workspace.lba!==(rollback?p.oldWorkFirst:p.workFirst)||n.workspace.blocks!==(rollback?p.oldWorkBlocks:p.workBlocks)||await musicFingerprint571(n.banks[n.active].index)!==p.preserveHash||JSON.stringify(foreign(s))!==JSON.stringify(p.foreign))fail('Workspace filesystem/recording verification failed');
 if(!replay){await j({state:'verified',digest:p.digest});await j({state:'complete',digest:p.digest,restored:rollback});}await releaseCompleted(d,p,replay);return s;
}
async function writePlan(d,sink,p,j,rollback,progress){for(const[index,e]of(rollback?[...p.extents].reverse():p.extents).entries()){const target=await sink.load((rollback?'before-':'')+e.key),current=await read(d,e.lba,1);if(!equal(current,target)){await j({state:rollback?'restore-intent':'write-intent',digest:p.digest,lba:e.lba});await d.writeBounded(e.lba,current,target);await d.sync();if(!equal(await read(d,e.lba,1),target))fail('Workspace write readback failed');}progress({phase:rollback?'restore-workspace':'expand-import-space',done:index+1,total:p.extents.length});}}
export async function applyWorkspace571(d,sink,p,j,progress=()=>{}){await validateWorkspace571(p,sink);await bind(d,p);if((await j.read()).length)fail('Existing workspace attempt; recover it');for(const g of p.guards)if(await sha256(await read(d,g.lba,g.blocks))!==g.hash)fail('Workspace source changed');for(const e of p.extents)if(await sha256(await read(d,e.lba,1))!==e.original)fail('Workspace source changed');await j({state:'admitted',digest:p.digest,identity:p.identity});await d.bootGate.lock(p.digest);await d.begin();await writePlan(d,sink,p,j,false,progress);return finish(d,p,j,false);}
export async function recoverWorkspace571(d,sink,p,j,{rollback=false}={},progress=()=>{}){await validateWorkspace571(p,sink);await bind(d,p);const events=await j.read(),completed=checkJournal(events,p,{intents:['write-intent','restore-intent'],completion:'restored'});if(completed){if(rollback!==completed.restored)fail('Completed workspace upgrade cannot be reversed over later recordings');return finish(d,p,j,rollback,true);}const gate=await d.bootGate.status();if(!gate.blocked&&events.some(e=>e.state==='write-intent'||e.state==='restore-intent'))fail('Workspace recovery was already released');await checkUnattempted(d,sink,p,events,['write-intent','restore-intent']);await d.bootGate.lock(p.digest);await d.begin();await writePlan(d,sink,p,j,rollback,progress);return finish(d,p,j,rollback);}
