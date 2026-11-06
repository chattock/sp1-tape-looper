import {packedSink,recoverySink} from './packs.mjs';
// Read-only hardware checkpoint: complete fixed-catalog music backup and exact
// proposed migration/recovery files. Never exposes apply/restore operations.
import {inspect,read,equal} from './fat.mjs';
import {prepareCapacity} from './migration.mjs';
import {validateCapacity} from './migration-journal.mjs';
import {save,saveJson,load,json,sink} from '../shared396/site/files.mjs';
import {sha256,crcUpdate} from '../shared396/site/backup/core.mjs';

export async function prepareSource(d,parent,progress=()=>{}){
 const id=await d.identify();if(!/^[a-f0-9]{16}$/.test(id.identity))throw Error('Invalid device identity');d.capacity=id.capacity;
 if((await d.bootGate.status()).blocked)throw Error('An unfinished recovery must be resolved first');
 const state=await inspect(d,progress),n=state.native;if(!n?.catalogs)throw Error('Expected existing fixed-catalog library');
 const folder=await parent.getDirectoryHandle(`SP1-CAPACITY-${id.identity}-${crypto.randomUUID()}`,{create:true});
 const planFolder=await folder.getDirectoryHandle('migration-recovery',{create:true});
 const planSink=await packedSink(planFolder,{create:true});const plan=await prepareCapacity(d,planSink,{},progress);await validateCapacity(plan,planSink);
 const objects=new Map();for(const c of n.catalogs)for(const o of c.objects){const key=`${o.first}-${o.blocks}`;const prior=objects.get(key);if(prior&&prior.crc!==o.crc)throw Error('Conflicting recovery payload');objects.set(key,o);}
 const manifest={schema:'sp1-capacity-source-backup-v1',identity:id.identity,capacity:id.capacity,poolFirst:n.extents[2].lba,poolBlocks:n.extents[2].blocks,planDigest:plan.digest,complete:false,catalogs:[],objects:[]};
 await saveJson(folder,'backup.partial.json',manifest);
 for(let i=0;i<2;i++){const e=n.extents[i],b=await read(d,e.lba,e.blocks),name=`catalog${i}.bin`;await save(folder,name,b);manifest.catalogs.push({name,lba:e.lba,bytes:b.length,sha256:await sha256(b)});}
 let done=0,total=[...objects.values()].reduce((sum,o)=>sum+o.blocks*512,0);
 for(const o of objects.values()){
  const name=`object-${String(manifest.objects.length).padStart(6,'0')}.bin`,handle=await folder.getFileHandle(name,{create:true}),writer=await handle.createWritable(),chunks=[];let crc=0xffffffff;
  try{for(let off=0;off<o.blocks;off+=128){const blocks=Math.min(128,o.blocks-off),b=await read(d,manifest.poolFirst+o.first+off,blocks);crc=crcUpdate(crc,b);await writer.write(b);chunks.push({offset:off*512,bytes:b.length,sha256:await sha256(b)});done+=b.length;progress({phase:'backup-recordings',done,total});}if((~crc>>>0)!==o.crc)throw Error('Source recording checksum failed');await writer.close();}catch(e){await writer.abort().catch(()=>{});throw e;}
  const file=await handle.getFile();if(file.size!==o.blocks*512)throw Error('Saved recording length');for(const c of chunks)if(await sha256(new Uint8Array(await file.slice(c.offset,c.offset+c.bytes).arrayBuffer()))!==c.sha256)throw Error('Saved recording readback failed');
  manifest.objects.push({name,first:o.first,blocks:o.blocks,crc:o.crc,chunks});
 }
 for(const g of state.guards)if(!equal(g.b,await read(d,g.lba,g.b.length/512)))throw Error('Source filesystem changed');
 for(const c of manifest.catalogs)if(await sha256(await read(d,c.lba,c.bytes/512))!==c.sha256)throw Error('Source catalog changed');
 const after=await d.identify();if(after.identity!==id.identity||after.capacity!==id.capacity)throw Error('Device changed');
 manifest.complete=true;await saveJson(folder,'backup.json',manifest);
 await verifySource(folder,progress);return {handle:folder,folder:folder.name,identity:id.identity,objects:manifest.objects.length,bytes:total,oldPoolBytes:plan.oldPoolBlocks*512,newPoolBytes:plan.poolBlocks*512,recoverySectors:plan.extents.length,planDigest:plan.digest,migrationEnabled:false};
}
export async function verifySource(folder,progress=()=>{}){
 const m=await json(folder,'backup.json');if(m.schema!=='sp1-capacity-source-backup-v1'||! /^[a-f0-9]{16}$/.test(m.identity)||!m.complete||!Array.isArray(m.objects)||!Array.isArray(m.catalogs)||m.catalogs.length!==2)throw Error('Incomplete source backup');
 const planFolder=await folder.getDirectoryHandle('migration-recovery'),plan=await json(planFolder,'plan.json');await validateCapacity(plan,await recoverySink(planFolder));if(plan.digest!==m.planDigest||plan.identity!==m.identity||plan.capacity!==m.capacity||plan.poolFirst!==m.poolFirst||plan.oldPoolBlocks!==m.poolBlocks)throw Error('Backup and migration plan differ');
 const {catalog}=await import('../shared396/site/catalog.mjs');const required=new Map();
 for(let i=0;i<2;i++){const c=m.catalogs[i];if(c.name!==`catalog${i}.bin`||c.bytes!==4096||!plan.guards.some(g=>g.lba===c.lba&&g.blocks===8&&g.hash===c.sha256))throw Error('Catalog backup shape');const b=await load(folder,c.name);if(b.length!==c.bytes||await sha256(b)!==c.sha256)throw Error('Catalog backup integrity');const decoded=catalog(b,m.poolBlocks);for(const o of decoded.objects){const key=`${o.first}-${o.blocks}`,old=required.get(key);if(old&&old.crc!==o.crc)throw Error('Conflicting catalog objects');required.set(key,o);}}
 if(required.size!==m.objects.length)throw Error('Missing recording backup');
 for(let i=0;i<m.objects.length;i++){progress({phase:'verify-saved-backup',done:i,total:m.objects.length});const o=m.objects[i],key=`${o.first}-${o.blocks}`,needed=required.get(key);if(!needed||needed.crc!==o.crc||o.name!==`object-${String(i).padStart(6,'0')}.bin`||!Array.isArray(o.chunks))throw Error('Recording manifest mismatch');required.delete(key);const file=await(await folder.getFileHandle(o.name)).getFile();if(file.size!==o.blocks*512)throw Error('Recording length');let offset=0,crc=0xffffffff;for(const c of o.chunks){if(c.offset!==offset||!Number.isInteger(c.bytes)||c.bytes<512||c.bytes>65536||c.bytes%512||offset+c.bytes>file.size)throw Error('Recording chunk bounds');const b=new Uint8Array(await file.slice(offset,offset+c.bytes).arrayBuffer());if(await sha256(b)!==c.sha256)throw Error('Recording chunk checksum');crc=crcUpdate(crc,b);offset+=c.bytes;}if(offset!==file.size||(~crc>>>0)!==o.crc)throw Error('Recording CRC');}
 progress({phase:'verify-saved-backup',done:m.objects.length,total:m.objects.length});return m;
}
