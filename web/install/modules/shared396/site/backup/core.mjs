import {BufferedWriter} from './buffered.mjs?v=396.1';
export const SECTOR=512, META_BLOCKS=4096, TRACK_BLOCKS=86016, TRACKS=64;
const fail=message=>{throw Error(message)};
const CRC_TABLE=Uint32Array.from({length:256},(_,c)=>{for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);return c>>>0});
export function crcUpdate(c,bytes){for(const b of bytes)c=(c>>>8)^CRC_TABLE[(c^b)&255];return c>>>0}
const u32=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
export function crc32(bytes){return (~crcUpdate(0xffffffff,bytes))>>>0}
export async function sha256(bytes){return [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(x=>x.toString(16).padStart(2,'0')).join('')}
export function planLegacy(metadata,capacity){
 if(!(metadata instanceof Uint8Array)||metadata.length<3072||!Number.isSafeInteger(capacity)||capacity<META_BLOCKS+TRACKS*TRACK_BLOCKS||capacity>0xffffffff)fail('Invalid card geometry');
 if(u32(metadata,0)!==0x53453341||u32(metadata,4)>=16)fail('Unsupported legacy music index; nothing changed');
 const x=1536;let sum=0;for(let i=x+16;i<x+1040;i++)sum=(sum+metadata[i])&65535;
 const dv=new DataView(metadata.buffer,metadata.byteOffset,metadata.byteLength);
 const xvalid=u32(metadata,x)===0x53453358&&dv.getUint16(x+4,true)===1&&dv.getUint16(x+6,true)===sum;
 const files=[{name:'metadata.bin',lba:0,blocks:META_BLOCKS,kind:'metadata'}];
 for(let song=0;song<16;song++)for(let track=0;track<4;track++){
  const row=8+song*44,present=metadata[row+8+track];if(present>1)fail('Invalid track presence flag');if(!present)continue;
  if(!xvalid)fail('Track codec metadata needs review before a recoverable backup');
  const xe=x+16+(song*4+track)*16,codec=metadata[xe+12];if(![4,5,6].includes(codec))fail(`Unsupported codec in song ${song+1}, track ${track+1}`);
  const fpb=codec===6?496:280,loop=u32(metadata,row+4);
  const length=u32(metadata,row+12+track*4)||Math.floor((loop+fpb/2)/fpb);
  const content=u32(metadata,716+(song*4+track)*4),extended=u32(metadata,xe+8);
  const needed=Math.max(length,content,extended);
  if(!length||needed>TRACK_BLOCKS)fail(`Invalid length in song ${song+1}, track ${track+1}`);
  const blocks=Math.min(TRACK_BLOCKS,Math.ceil((needed+1)/16)*16);
  files.push({name:`song${String(song+1).padStart(2,'0')}-track${track+1}.bin`,lba:META_BLOCKS+(song*4+track)*TRACK_BLOCKS,blocks,kind:'track',song:song+1,track:track+1,codec});
 }
 return files;
}
export function validateManifest(m){
 if(!m||m.schema!=='sp1-legacy-music-backup-v1'||m.complete!==true||!/^[a-f0-9]{16}$/.test(m.identity)||!Number.isInteger(m.capacity)||!Array.isArray(m.files)||m.files.length<1||m.files.length>65)fail('Incomplete or invalid backup manifest');
 const names=new Set();for(const f of m.files){
  if(!/^(metadata|song(0[1-9]|1[0-6])-track[1-4])\.bin$/.test(f.name)||names.has(f.name)||!Number.isSafeInteger(f.lba)||!Number.isSafeInteger(f.blocks)||f.lba<0||f.blocks<1||f.blocks>TRACK_BLOCKS||f.lba+f.blocks>m.capacity||!Number.isSafeInteger(f.bytes)||f.bytes!==f.blocks*512||!/^[a-f0-9]{64}$/.test(f.sha256))fail('Invalid backup extent or checksum');names.add(f.name);
 }
 if(m.files[0].name!=='metadata.bin'||m.files[0].lba!==0||m.files[0].blocks!==META_BLOCKS)fail('Missing complete reserved metadata area');
 return m;
}
async function writeJSON(folder,name,value){const h=await folder.getFileHandle(name,{create:true}),w=await h.createWritable();try{await w.write(JSON.stringify(value,null,2)+'\n');await w.close()}catch(e){await w.abort().catch(()=>{});throw e}}
export async function verifyBackup(folder){
 const mf=await (await folder.getFileHandle('manifest.json')).getFile();if(mf.size>131072)fail('Oversized manifest');
 const m=validateManifest(JSON.parse(await mf.text()));let metadata;
 for(const f of m.files){const file=await (await folder.getFileHandle(f.name)).getFile();if(file.size!==f.bytes)fail(`Wrong size: ${f.name}`);const b=new Uint8Array(await file.arrayBuffer());if(await sha256(b)!==f.sha256)fail(`Checksum mismatch: ${f.name}`);if(f.kind==='metadata')metadata=b;}
 const expected=planLegacy(metadata,m.capacity);if(expected.length!==m.files.length)fail('Backup omits or adds tracks');
 for(let i=0;i<expected.length;i++)for(const key of Object.keys(expected[i]))if(expected[i][key]!==m.files[i][key])fail(`Manifest differs from saved music index: ${key}`);
 return m;
}
export async function backupMusic(device,parent,progress=()=>{}){
 const info=await device.identify();if(!/^[a-f0-9]{16}$/.test(info.identity)||info.readOnly!==true)fail('Expected read-only backup firmware');
 const start=await device.read(0,6),plan=planLegacy(start,info.capacity);
 const name=`SP1-${info.identity}-${new Date().toISOString().replaceAll(':','-')}-${crypto.randomUUID()}`;
 const folder=await parent.getDirectoryHandle(name,{create:true});
 const manifest={schema:'sp1-legacy-music-backup-v1',identity:info.identity,capacity:info.capacity,complete:false,created:new Date().toISOString(),scope:'Logical Tape Looper music backup, not a stock or whole-card image',files:[]};
 await writeJSON(folder,'manifest.partial.json',manifest);
 const metrics={fileWriteMs:0,fileWrites:0,fileVerifyMs:0};
 let done=0,total=plan.reduce((n,f)=>n+f.blocks*512,0);
 try{
  for(const f of plan){
   const file=await folder.getFileHandle(f.name,{create:true}),writer=await file.createWritable();let rolling=0xffffffff;const buffered=new BufferedWriter(writer);
   try{for(let offset=0;offset<f.blocks;){const n=Math.min(16,f.blocks-offset),bytes=await device.read(f.lba+offset,n);if(bytes.length!==n*512)fail('Short device read');
    // Incremental transport-independent checksum verifies the saved file again.
    rolling=crcUpdate(rolling,bytes);
    await buffered.write(bytes);offset+=n;done+=bytes.length;progress({name:f.name,done,total});}
    await buffered.flush();await writer.close();metrics.fileWriteMs+=buffered.ms;metrics.fileWrites+=buffered.calls;
   }catch(e){await writer.abort().catch(()=>{});throw e}
   const verifyStart=performance.now();const saved=await file.getFile();if(saved.size!==f.blocks*512)fail('Backup file size mismatch');const bytes=new Uint8Array(await saved.arrayBuffer());
   if(f.kind==='metadata'&&start.some((v,i)=>v!==bytes[i]))fail('Saved metadata differs from initial snapshot');
   if(crc32(bytes)!==((~rolling)>>>0))fail('Saved file differs from device bytes');
   manifest.files.push({...f,bytes:saved.size,sha256:await sha256(bytes)});await writeJSON(folder,'manifest.partial.json',manifest);metrics.fileVerifyMs+=performance.now()-verifyStart;
  }
  const end=await device.read(0,6);if(end.length!==start.length||end.some((v,i)=>v!==start[i]))fail('Device metadata changed during backup');
  const again=await device.identify();if(again.identity!==info.identity||again.capacity!==info.capacity)fail('Device changed during backup');
  manifest.complete=true;await writeJSON(folder,'manifest.json',manifest);await verifyBackup(folder);
  return {folder,name,manifest,metrics};
 }catch(e){manifest.complete=false;manifest.error=String(e);await writeJSON(folder,'failure.json',manifest).catch(()=>{});throw e}
}
export async function restoreBackup(device,folder,journal,progress=()=>{}){
 // This adapter is tested against simulated media only. Firmware351 has no
 // write capability and is rejected here. No UI exposes physical restore yet.
 if(device.writes!==true)fail('No qualified write adapter connected');
 const m=await verifyBackup(folder),id=await device.identify();
 if(id.identity!==m.identity||id.capacity!==m.capacity)fail('Restore requires the original device and capacity');
 await journal({state:'verified',identity:m.identity,manifest:m});
 const ordered=m.files.slice(1).map(f=>({f,start:0,count:f.blocks}));
 ordered.push({f:m.files[0],start:2,count:META_BLOCKS-2},{f:m.files[0],start:1,count:1},{f:m.files[0],start:0,count:1});
 for(const {f,start,count} of ordered){const file=await (await folder.getFileHandle(f.name)).getFile();
  for(let offset=start;offset<start+count;){const n=Math.min(16,start+count-offset),lba=f.lba+offset,bytes=new Uint8Array(await file.slice(offset*512,(offset+n)*512).arrayBuffer());
   await journal({state:'write_intent',lba,blocks:n,sha256:await sha256(bytes)});
   await device.write(lba,bytes);await device.sync();const returned=await device.read(lba,n);
   if(returned.length!==bytes.length||returned.some((v,i)=>v!==bytes[i]))fail('Restore readback mismatch; remain in maintenance');
   await journal({state:'verified_extent',lba,blocks:n});progress({lba,blocks:n});offset+=n;
  }
 }
 // Re-read the complete restored logical image after ALL writes: a later
 // misdirected write must not be hidden by an earlier successful readback.
 for(const f of m.files){const all=new Uint8Array(f.bytes);
  for(let off=0;off<f.blocks;){const n=Math.min(16,f.blocks-off),b=await device.read(f.lba+off,n);if(b.length!==n*512)fail('Short final restore verification');all.set(b,off*512);off+=n;}
  if(await sha256(all)!==f.sha256)fail('Final restore verification failed; remain in maintenance');
 }
 await journal({state:'complete',identity:m.identity});return m;
}
