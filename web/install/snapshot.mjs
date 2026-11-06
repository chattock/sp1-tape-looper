import {inspect,read,equal} from './fat.mjs?v=396.2';
import {sha256,crc32} from './backup/core.mjs?v=396.2';
import {save,saveJson,load,json} from './files.mjs?v=396.2';
// Snapshot ALL allocated filesystem sectors, including foreign folders/files,
// and both catalogs' reservations. No format-specific fixed prefix assumptions.
export async function snapshot(d,parent,progress=()=>{}){
 const id=await d.identify();d.capacity=id.capacity;const s=await inspect(d),folder=await parent.getDirectoryHandle(`SP1-SHARED-${id.identity}-${crypto.randomUUID()}`,{create:true});
 const ranges=[{lba:0,blocks:1},{lba:s.g.first,blocks:s.g.data-s.g.first}],pool=s.native?.extents[2];
 // Save occupied native objects, not hundreds of MiB of unused pool space.
 // Foreign files retain every allocated cluster, including directory slack.
 for(const dir of s.dirs)for(const c of dir.cs)ranges.push({lba:s.g.data+(c-2)*s.g.spc,blocks:s.g.spc});
 for(const f of s.files){const at=s.g.data+(f.cluster-2)*s.g.spc;if(pool&&at===pool.lba){
  const intervals=[];for(const cat of s.native.catalogs)for(const o of cat.objects)intervals.push({lba:pool.lba+o.first,blocks:o.blocks,crc:o.crc});
  // CRC is checked from the closed saved snapshot below. Read each device
  // extent once, preserving all references from both catalogs.
  for(const o of intervals)ranges.push({lba:o.lba,blocks:o.blocks});
 }else for(const c of f.cs)ranges.push({lba:s.g.data+(c-2)*s.g.spc,blocks:s.g.spc});}
 ranges.sort((a,b)=>a.lba-b.lba);const merged=[];for(const r of ranges){const last=merged.at(-1);if(last&&r.lba<=last.lba+last.blocks)last.blocks=Math.max(last.lba+last.blocks,r.lba+r.blocks)-last.lba;else merged.push({...r});}
 const manifest={schema:'sp1-shared-snapshot396-v1',identity:id.identity,capacity:id.capacity,complete:false,files:[],scope:'Allocated foreign filesystem data and referenced native objects; unused audio-pool bytes omitted'};
 let done=0,total=merged.reduce((n,x)=>n+x.blocks*512,0);
 for(const r of merged)for(let off=0;off<r.blocks;off+=128){const blocks=Math.min(128,r.blocks-off),at=r.lba+off,b=await read(d,at,blocks),name=`extent-${String(manifest.files.length).padStart(6,'0')}.bin`;await save(folder,name,b);manifest.files.push({name,lba:at,blocks,sha256:await sha256(b)});done+=b.length;progress({phase:'backup',done,total});}
 // Metadata must remain unchanged throughout; runtime and host remain excluded.
 for(const x of s.guards)if(!equal(await read(d,x.lba,x.b.length/512),x.b))throw Error('Filesystem changed during backup');
 manifest.complete=true;await saveJson(folder,'manifest.json',manifest);await verify(folder);return {folder,manifest};
}
export async function verify(folder){const m=await json(folder,'manifest.json');if(m.schema!=='sp1-shared-snapshot396-v1'||!m.complete||!Array.isArray(m.files))throw Error('Incomplete shared backup');let end=0;for(const f of m.files){if(!/^extent-\d{6}\.bin$/.test(f.name)||!Number.isInteger(f.lba)||!Number.isInteger(f.blocks)||f.blocks<1||f.blocks>128||f.lba<end||f.lba+f.blocks>m.capacity)throw Error('Invalid snapshot bounds');const b=await load(folder,f.name);if(b.length!==f.blocks*512||await sha256(b)!==f.sha256)throw Error('Snapshot file checksum failed');end=f.lba+f.blocks;}
 if(!/^[a-f0-9]{16}$/.test(m.identity)||m.capacity!==7733248||!m.files.length)throw Error('Invalid snapshot identity');
 // Resolve every filesystem/reference read through saved extents. Missing data
 // must fail, never be silently synthesized as zeros during verification.
 const cache=new Map();
 const virtual={capacity:m.capacity,read:async(at,n)=>{const out=new Uint8Array(n*512);for(let i=0;i<n;i++){const f=m.files.find(f=>at+i>=f.lba&&at+i<f.lba+f.blocks);if(!f)throw Error('Snapshot omits required sectors');let b=cache.get(f.name);if(!b){b=await load(folder,f.name);if(cache.size>=16)cache.delete(cache.keys().next().value);cache.set(f.name,b)}out.set(b.subarray((at+i-f.lba)*512,(at+i-f.lba+1)*512),i*512)}return out;}};
 const state=await inspect(virtual),pool=state.native?.extents[2];
 const covered=(at,n)=>{let pos=at;for(const f of m.files){if(f.lba>pos)break;if(f.lba+f.blocks>pos)pos=f.lba+f.blocks;if(pos>=at+n)return true;}return false;};
 for(const f of state.files){const at=state.g.data+(f.cluster-2)*state.g.spc;if(pool&&at===pool.lba)continue;for(const c of f.cs)if(!covered(state.g.data+(c-2)*state.g.spc,state.g.spc))throw Error('Snapshot omits allocated file data');}
 if(pool)for(const cat of state.native.catalogs)for(const o of cat.objects){if(!covered(pool.lba+o.first,o.blocks)||crc32(await read(virtual,pool.lba+o.first,o.blocks))!==o.crc)throw Error('Snapshot object integrity failed');}
 return m;}
