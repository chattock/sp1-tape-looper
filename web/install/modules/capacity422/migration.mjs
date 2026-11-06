import {catalogFingerprint} from './preservation.mjs';
import {decodeIndex} from './paged/binary.mjs';
// Build a complete, offline capacity migration sector plan. No device writes.
// Activation remains gated on matching runtime, recovery and host adapters.
import {inspect,read,u32,put,lba} from './fat.mjs';
import {sha256} from '../shared396/site/backup/core.mjs';
import {convertLegacy} from './paged/catalog.mjs';
import {INDEX_SECTORS} from './filesystem.mjs';
const E=new TextEncoder(),enc=x=>E.encode(JSON.stringify(x));
function entry(name,cluster,bytes){const b=new Uint8Array(32),v=new DataView(b.buffer);b.set(E.encode(name));v.setUint16(20,cluster>>>16,true);v.setUint16(26,cluster&65535,true);put(b,28,bytes);return b;}
export async function prepareCapacity(d,sink,{reserveBytes=128*1024*1024,workBytes=256*1024*1024,grow=true}={},progress=()=>{}){
 const id=await d.identify();d.capacity=id.capacity;if(!/^[0-9a-f]{16}$/.test(id.identity)||!Number.isSafeInteger(id.capacity))throw Error('Device identity');
 if(!Number.isSafeInteger(reserveBytes)||reserveBytes<32*1024*1024)throw Error('Shared-space reserve too small');
 if(!Number.isSafeInteger(workBytes)||workBytes<64*1024*1024||workBytes>256*1024*1024||workBytes%32768)throw Error('Invalid maintenance workspace size');
 const s=await inspect(d,progress);if(!s.native)throw Error('Recognized existing library required');
 const {g}=s,fat=s.fat.slice(),n=s.native,own=s.root.entries.find(e=>e.name===(n.path==='/SP1'?'SP1        ':'TAPELOOP   '));
 if(n.path==='/SP1'&&s.root.entries.some(e=>e.name==='TAPELOOP   '))throw Error('Namespace collision');
 const dir=s.dirs.find(x=>x.c===own.cluster),directory=dir.b.slice(),changes=new Map();
 for(const name of['INDEX0  BIN','INDEX1  BIN','OLD0    BIN','OLD1    BIN','WORK    BIN'])if(dir.entries.some(e=>e.name===name))throw Error('Migration file collision');
 let free=0;for(let c=2;c<g.end;c++)if(!(u32(fat,c*4)&0xfffffff))free++;
 const indexClusters=INDEX_SECTORS/g.spc,workClusters=workBytes/(g.spc*512);if(!Number.isInteger(indexClusters)||free<2*indexClusters+workClusters+Math.ceil(reserveBytes/(g.spc*512)))throw Error('Insufficient free space for index and shared reserve');
 function allocateHigh(count){let run=0;for(let c=g.end-1;c>=2;c--){if(!(u32(fat,c*4)&0xfffffff)){if(++run===count){for(let i=0;i<count;i++)put(fat,(c+i)*4,i===count-1?0xfffffff:c+i+1);free-=count;return c;}}else run=0;}throw Error('Index needs a contiguous free extent');}
 const work=allocateHigh(workClusters),banks=[allocateHigh(indexClusters),allocateHigh(indexClusters)],audio=n.extents[2],audioEntry=dir.entries.find(e=>e.name==='AUDIO   BIN');
 const firstCluster=(audio.lba-g.data)/g.spc+2,oldClusters=audio.blocks/g.spc;if(!Number.isInteger(firstCluster)||!Number.isInteger(oldClusters))throw Error('Existing pool alignment');
 let added=0,end=firstCluster+oldClusters,available=Math.max(0,free-Math.ceil(reserveBytes/(g.spc*512)));
 if(grow)while(end+added<g.end&&added<available&&!(u32(fat,(end+added)*4)&0xfffffff)&&(oldClusters+added+1)*g.spc*512<=0xffffffff)added++;
 if(added){for(let c=end-1;c<end+added;c++)put(fat,c*4,c===end+added-1?0xfffffff:c+1);free-=added;}
 const poolBlocks=audio.blocks+added*g.spc;put(directory,audioEntry.offset+28,poolBlocks*512);
 function removeAlias(b,at){for(let p=at-32;p>=0&&b[p+11]===15;p-=32)b[p]=229;}
 for(let i=0;i<2;i++){const old=dir.entries.find(e=>e.name===`CAT${i}    BIN`);removeAlias(directory,old.offset);directory.set(E.encode(`OLD${i}    BIN`),old.offset);}
 const slots=[];for(let at=0;at<directory.length&&slots.length<3;at+=32)if(!directory[at]||directory[at]===229)slots.push(at);
 if(slots.length<3)throw Error('Owned directory needs expansion before migration');
 directory.set(entry("WORK    BIN",work,workBytes),slots[2]);
 for(let i=0;i<2;i++)directory.set(entry(`INDEX${i}  BIN`,banks[i],INDEX_SECTORS*512),slots[i]);
 const source=n.catalogs.reduce((a,b)=>BigInt(a.generation)>=BigInt(b.generation)?a:b),index=convertLegacy(source,poolBlocks,{sectors:INDEX_SECTORS});
 function set(at,b){for(let i=0;i<b.length;i+=512){if(b.length%512)throw Error('Unaligned plan');changes.set(at+i/512,b.slice(i,i+512));}}
 set(lba(g,work),new Uint8Array(1024));
 for(const bank of banks)set(lba(g,bank),index.image.subarray(0,index.used*512));
 for(let i=0;i<dir.cs.length;i++)set(lba(g,dir.cs[i]),directory.subarray(i*g.spc*512,(i+1)*g.spc*512));
 const root=s.root.b.slice();removeAlias(root,own.offset);root.set(E.encode('TAPELOOP   '),own.offset);for(let i=0;i<s.root.cs.length;i++)set(lba(g,s.root.cs[i]),root.subarray(i*g.spc*512,(i+1)*g.spc*512));
 set(g.first+g.reserved,fat);set(g.first+g.reserved+g.fat,fat);
 const info=s.info.slice();put(info,488,free);put(info,492,0xffffffff);set(g.first+1,info);set(g.first+7,info);
 const p={schema:'sp1-capacity422-v1',identity:id.identity,capacity:id.capacity,sourcePath:n.path,path:'/TAPELOOP',sourceGeneration:source.generation,catalogHash:await catalogFingerprint(decodeIndex(index.image)),poolFirst:audio.lba,oldPoolBlocks:audio.blocks,poolBlocks,indexSectors:INDEX_SECTORS,indexFirst:banks.map(c=>lba(g,c)),reserveBytes,workFirst:lba(g,work),workBlocks:workBytes/512,guards:[],extents:[],activationAllowed:false};
 for(const g of s.guards)p.guards.push({lba:g.lba,blocks:g.b.length/512,hash:await sha256(g.b)});
 // Pin both old catalog contents as well as FAT/directory geometry. Their files
 // remain allocated under OLD0/OLD1; no old recording payload is overwritten.
 for(const e of n.extents.slice(0,2)){const b=await read(d,e.lba,e.blocks);p.guards.push({lba:e.lba,blocks:e.blocks,hash:await sha256(b)});}
 let planned=0,pending=[];
 async function flush(){if(!pending.length)return;if(sink.saveBatch)await sink.saveBatch(pending);else for(const x of pending)await sink.save(x.key,x.bytes);for(const x of pending)if(await sha256(await sink.load(x.key))!==await sha256(x.bytes))throw Error('Recovery file readback');pending=[];}
 for(const [at,target]of changes){progress({phase:'prepare-recovery',done:planned++*512,total:changes.size*512});const before=await read(d,at,1);if(before.every((x,i)=>x===target[i]))continue;const key=String(p.extents.length).padStart(6,'0');pending.push({key,bytes:target},{key:'before-'+key,bytes:before});p.extents.push({key,lba:at,blocks:1,original:await sha256(before),target:await sha256(target)});if(pending.length>=128)await flush();}
 await flush();
 progress({phase:'prepare-recovery',done:changes.size*512,total:changes.size*512});
 p.digest=await sha256(enc(p));await sink.seal(p);return p;
}
