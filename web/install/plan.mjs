import {inspect,read,u32,u16,put,equal,lba,geometry,contiguous} from './fat.mjs?v=396.2';
import {sha256,crc32,crcUpdate,verifyBackup,META_BLOCKS} from './backup/core.mjs?v=396.2';
import {takeHeader} from './backup/image.mjs?v=396.2';
const E=new TextEncoder(),fail=s=>{throw Error(s)};
const enc=o=>E.encode(JSON.stringify(o));
export const consent=id=>`ERASE MUSIC AND SETTINGS ON ${id}`;
function entry(name,attr,cl,bytes=0){const b=new Uint8Array(32);b.set(E.encode(name));b[11]=attr;new DataView(b.buffer).setUint16(20,cl>>>16,true);new DataView(b.buffer).setUint16(26,cl&65535,true);put(b,28,bytes);return b;}
export function freshGeometry(capacity){const first=8192,blocks=capacity-first,spc=64,reserved=32;let fat=Math.ceil((Math.floor(blocks/spc)+2)*4/512);for(let i=0;i<32;i++){const next=Math.ceil((Math.floor((blocks-reserved-fat*2)/spc)+2)*4/512);if(next===fat)break;fat=next;}
 const mbr=new Uint8Array(512);mbr[450]=12;put(mbr,454,first);put(mbr,458,blocks);mbr[510]=85;mbr[511]=170;
 const boot=new Uint8Array(512),v=new DataView(boot.buffer);boot.set([235,88,144]);boot.set(E.encode('MSDOS5.0'),3);v.setUint16(11,512,true);boot[13]=spc;v.setUint16(14,reserved,true);boot[16]=2;boot[21]=248;v.setUint16(24,63,true);v.setUint16(26,255,true);put(boot,28,first);put(boot,32,blocks);put(boot,36,fat);put(boot,44,2);v.setUint16(48,1,true);v.setUint16(50,6,true);boot[64]=128;boot[66]=41;put(boot,67,0x53503196);boot.set(E.encode('SP-1       '),71);boot.set(E.encode('FAT32   '),82);boot[510]=85;boot[511]=170;
 return {g:geometry(mbr,boot,capacity),mbr,boot};}
async function library(backup,id){
 const meta=new Uint8Array(META_BLOCKS*512);put(meta,0,0x53453341);for(let s=0;s<16;s++)put(meta,8+s*44,65536);
 if(!backup)return {meta,tracks:[]};const m=await verifyBackup(backup);if(m.identity!==id.identity||m.capacity!==id.capacity)fail('Backup belongs to another device');meta.set(new Uint8Array(await(await(await backup.getFileHandle('metadata.bin')).getFile()).arrayBuffer()));return {meta,tracks:m.files.slice(1)};
}
// All target sectors and originals live in a local, read-back-verified plan folder.
// No device write capability is used by preparation.
export async function prepare(d,sink,{mode='preserve',backup=null,poolMiB=256}={},progress=()=>{}){
 const id=await d.identify();d.capacity=id.capacity;
 if(!/^[a-f0-9]{16}$/.test(id.identity)||id.capacity!==7733248)fail('Unsupported identity/capacity');
 if(!['preserve','clean','legacy'].includes(mode)||!Number.isInteger(poolMiB)||poolMiB<16||poolMiB>1024)fail('Invalid preparation options');
 let s,g,mbr,boot,fat,root,guards=[];
 if(mode==='preserve'){s=await inspect(d,progress);if(s.native)fail('Library already installed; no initialization needed');if(s.root.entries.some(e=>e.name==='TAPELOOP   '))fail('TAPELOOP name collision');({g,mbr,boot}=s);fat=s.fat.slice();root=s.root;guards=s.guards;}
 else {if(mode==='legacy'&&!backup)fail('Verified legacy backup required');({g,mbr,boot}=freshGeometry(id.capacity));fat=new Uint8Array(g.fat*512);put(fat,0,0xffffff8);put(fat,4,0xfffffff);put(fat,8,0xfffffff);root={cs:[2],b:new Uint8Array(g.spc*512),entries:[]};}
 const lib=await library(backup,id),changes=new Map();
 function set(at,b){if(b.length%512||at<0||at+b.length/512>id.capacity)fail('Target bounds');for(let n=0;n<b.length/512;n++)changes.set(at+n,b.slice(n*512,(n+1)*512));}
 function alloc(count){let run=0,start=2;for(let c=2;c<g.end;c++){if((u32(fat,c*4)&0xfffffff)===0){if(!run)start=c;run++;if(run===count){for(let k=0;k<count;k++)put(fat,(start+k)*4,k+1===count?0xfffffff:start+k+1);return start;}}else run=0;}fail('Insufficient contiguous free space; nothing formatted or written');}
 const dirCl=alloc(1),cat0=alloc(Math.ceil(4096/(g.spc*512))),cat1=alloc(Math.ceil(4096/(g.spc*512))),imports=alloc(1),exports=alloc(1);
 let needed=16;for(const f of lib.tracks)needed+=Math.ceil((f.blocks+16)/16)*16;
 const poolBlocks=Math.ceil(Math.max(poolMiB*2048,needed+32)/g.spc)*g.spc;
 const poolCl=alloc(poolBlocks/g.spc),poolFirst=lba(g,poolCl);
 if(poolBlocks*512>0xffffffff)fail('Pool exceeds FAT32 file limit');
 const own=new Uint8Array(g.spc*512);own.set(entry('.          ',16,dirCl));own.set(entry('..         ',16,0),32);own.set(entry('CAT0    BIN',0,cat0,4096),64);own.set(entry('CAT1    BIN',0,cat1,4096),96);own.set(entry('AUDIO   BIN',0,poolCl,poolBlocks*512),128);own.set(entry('IMPORTS    ',16,imports),160);own.set(entry('EXPORTS    ',16,exports),192);set(lba(g,dirCl),own);
 for(const c of [imports,exports]){const b=new Uint8Array(g.spc*512);b.set(entry('.          ',16,c));b.set(entry('..         ',16,dirCl),32);set(lba(g,c),b);}
 let slot=-1;for(let o=0;o<root.b.length;o+=32)if(!root.b[o]||root.b[o]===229){slot=o;break;}
 if(slot<0){const c=alloc(1);put(fat,root.cs.at(-1)*4,c);root={...root,cs:[...root.cs,c],b:new Uint8Array([...root.b,...new Uint8Array(g.spc*512)])};slot=root.b.length-g.spc*512;}
 const rootBytes=root.b.slice();if(!rootBytes[slot]&&slot+32<rootBytes.length)rootBytes[slot+32]=0;rootBytes.set(entry('TAPELOOP   ',16,dirCl),slot);for(let i=0;i<root.cs.length;i++)set(lba(g,root.cs[i]),rootBytes.slice(i*g.spc*512,(i+1)*g.spc*512));
 const cat=new Uint8Array(4096);cat.set(E.encode('SP1POOL2'));put(cat,8,2);put(cat,12,1);cat.fill(255,2080,2336);let cursor=0,object=0;
 // Stream each imported take into plan segments below; never hold the entire library in RAM.
 const importsList=[];
 for(const f of lib.tracks){cursor=Math.ceil(cursor/16)*16;const header=takeHeader(lib.meta,f);importsList.push({f,at:poolFirst+cursor,header});put(cat,32+object*16,cursor);put(cat,36+object*16,f.blocks+16);put(cat,44+object*16,0x314b4154);new DataView(cat.buffer).setUint16(2080+((f.song-1)*4+f.track-1)*2,object,true);cursor+=f.blocks+16;object++;}
 cursor=Math.ceil(cursor/16)*16;put(cat,20,object+1);put(cat,32+object*16,cursor);put(cat,36+object*16,6);put(cat,40+object*16,crc32(lib.meta.slice(0,3072)));put(cat,44+object*16,0x314c5443);set(poolFirst+cursor,lib.meta.slice(0,3072));
 const p={schema:'sp1-shared-fat396-v1',identity:id.identity,capacity:id.capacity,mode,path:'/TAPELOOP',geometry:g,pool:{first:poolFirst,blocks:poolBlocks},tracks:lib.tracks.length,guards:[],extents:[],hardwareQualified:false};
 let bytes=0;
 async function saveTarget(at,b){const key=String(p.extents.length).padStart(6,'0'),before=await read(d,at,b.length/512);if(equal(before,b))return;await sink.save(key,b);await sink.save('before-'+key,before);if(!equal(await sink.load(key),b)||!equal(await sink.load('before-'+key),before))fail('Recovery file readback failed');p.extents.push({key,lba:at,blocks:b.length/512,target:await sha256(b),original:await sha256(before)});bytes+=b.length;progress({phase:'prepare-recovery',done:bytes,total:0});}
 for(let i=0;i<importsList.length;i++){const {f,at,header}=importsList[i],file=await(await backup.getFileHandle(f.name)).getFile();let sum=0xffffffff;const update=b=>{sum=crcUpdate(sum,b)};update(header);await saveTarget(at,header);for(let off=0;off<file.size;off+=65536){const b=new Uint8Array(await file.slice(off,off+65536).arrayBuffer());update(b);await saveTarget(at+16+off/512,b);}put(cat,40+i*16,(~sum)>>>0);}
 put(cat,4092,crc32(cat.subarray(0,4092)));set(lba(g,cat0),cat);set(lba(g,cat1),cat);
 // Update both FAT copies and advisory FSInfo; unrelated FAT entries remain exact.
 let free=0,next=0xffffffff;for(let c=2;c<g.end;c++)if(!(u32(fat,c*4)&0xfffffff)){free++;if(next===0xffffffff)next=c;}
 const info=s?s.info.slice():new Uint8Array(512);put(info,0,0x41615252);put(info,484,0x61417272);put(info,488,free);put(info,492,next);put(info,508,0xaa550000);
 set(g.first+g.reserved,fat);set(g.first+g.reserved+g.fat,fat);set(g.first+1,info);set(g.first+7,info);
 if(mode!=='preserve'){set(g.first,boot);set(g.first+6,boot);}
 // Data and directories first, FAT/boot metadata last; persistent boot lock spans all writes.
 const sorted=[...changes.keys()].sort((a,b)=>a-b);for(let i=0;i<sorted.length;){const at=sorted[i],chunk=[];do{chunk.push(...changes.get(sorted[i++]));}while(i<sorted.length&&sorted[i]===at+chunk.length/512&&chunk.length<65536);await saveTarget(at,Uint8Array.from(chunk));}
 if(mode!=='preserve')await saveTarget(0,mbr);
 for(const x of guards)p.guards.push({lba:x.lba,blocks:x.b.length/512,hash:await sha256(x.b)});
 // Legacy conversion must prove source still matches the verified backup before writes.
 if(mode==='legacy'){const m=await verifyBackup(backup);for(const f of m.files)p.guards.push({lba:f.lba,blocks:f.blocks,hash:f.sha256});}
 if(!p.extents.length)throw Object.assign(Error('This empty base filesystem is already installed'),{code:'FILESYSTEM_TARGET_PRESENT',identity:id.identity,capacity:id.capacity});
 p.digest=await sha256(enc(p));await sink.seal(p);return p;
}
export async function validate(p,sink){if(p?.schema!=='sp1-shared-fat396-v1'||p.path!=='/TAPELOOP'||p.capacity!==7733248||!/^[a-f0-9]{16}$/.test(p.identity)||!['clean','preserve','legacy','restore'].includes(p.mode)||!Array.isArray(p.extents)||!p.extents.length)fail('Invalid shared plan');const {digest,...base}=p;if(digest!==await sha256(enc(base)))fail('Plan checksum mismatch');const intervals=[];for(const e of p.extents){if(!/^\d{6}$/.test(e.key)||!Number.isInteger(e.lba)||!Number.isInteger(e.blocks)||e.blocks<1||e.blocks>128||e.lba<0||e.lba+e.blocks>p.capacity||intervals.some(x=>e.lba<x[1]&&x[0]<e.lba+e.blocks))fail('Overlapping/invalid plan bounds');intervals.push([e.lba,e.lba+e.blocks]);for(const original of [false,true]){const b=await sink.load((original?'before-':'')+e.key);if(b.length!==e.blocks*512||await sha256(b)!==(original?e.original:e.target))fail('Recovery payload mismatch');}}return p;}
async function bind(d,p){const id=await d.identify();if(id.identity!==p.identity||id.capacity!==p.capacity)fail('Different device');d.capacity=id.capacity;if(d.sharedProfile!=='bounded-fat396'||!d.bootGate?.persistent)fail('Shared396 maintenance required');}
async function compare(d,p,sink,original,progress){let done=0,total=p.extents.reduce((n,e)=>n+e.blocks*512,0);for(const e of p.extents){if(await sha256(await read(d,e.lba,e.blocks))!==(original?e.original:e.target))fail(original?'Original media changed':'Final verification failed');done+=e.blocks*512;progress({phase:original?'verify-original':'verify-written',done,total});}}
async function write(d,e,b,j,p,original){for(let at=0;at<e.blocks;at+=16){const n=Math.min(16,e.blocks-at),target=b.slice(at*512,(at+n)*512),current=await read(d,e.lba+at,n);if(equal(current,target))continue;await j({state:original?'restore-intent':'write-intent',digest:p.digest,key:e.key,lba:e.lba+at});await d.writeBounded(e.lba+at,current,target);await d.sync();if(!equal(await read(d,e.lba+at,n),target))fail('Write readback mismatch');}}
export async function apply(d,sink,p,j,confirmation,progress=()=>{}){await validate(p,sink);await bind(d,p);if((p.mode==='clean'||p.mode==='restore')&&confirmation!==(p.mode==='restore'?'RESTORE FILESYSTEM ON '+p.identity:consent(p.identity)))fail('Explicit erase confirmation required');if((await j.read()).length)fail('Existing attempt; use recovery');for(const g of p.guards)if(await sha256(await read(d,g.lba,g.blocks))!==g.hash)fail('Source filesystem changed');await compare(d,p,sink,true,progress);await j({state:'admitted',digest:p.digest,identity:p.identity});await d.bootGate.lock(p.digest);await d.begin();let done=0,total=p.extents.reduce((n,e)=>n+e.blocks*512,0);for(const e of p.extents){await write(d,e,await sink.load(e.key),j,p,false);done+=e.blocks*512;progress({phase:'install',done,total});}await compare(d,p,sink,false,progress);const s=await inspect(d);if(p.mode!=='restore'&&(!s.native||s.native.path!=='/TAPELOOP'))fail('Installed filesystem admission failed');await j({state:'verified',digest:p.digest});await d.bootGate.unlock(p.digest);await j({state:'complete',digest:p.digest});return s;}
export async function recover(d,sink,p,j,progress=()=>{}){await validate(p,sink);await bind(d,p);const events=await j.read();if(!events.some(e=>e.state==='admitted'&&e.digest===p.digest&&e.identity===p.identity))fail('Original admission journal required');if(!(await d.bootGate.status()).blocked)fail('Transaction released; refuse rollback over later recordings');await d.bootGate.lock(p.digest);for(const e of p.extents)if(!events.some(x=>x.state==='write-intent'&&x.digest===p.digest&&x.key===e.key)&&await sha256(await read(d,e.lba,e.blocks))!==e.original)fail('Unattempted extent changed');await d.begin();for(const e of [...p.extents].reverse())await write(d,e,await sink.load('before-'+e.key),j,p,true);await compare(d,p,sink,true,progress);await j({state:'restored',digest:p.digest});await d.bootGate.unlock(p.digest);await j({state:'recovered',digest:p.digest});}

// Explicit later snapshot restore captures a NEW rollback plan, never reuses an
// installation journal after users have made recordings.
export async function prepareRestore(d,sink,source,progress=()=>{}){
 const {verify}=await import('./snapshot.mjs?v=396.2');const m=await verify(source),id=await d.identify();d.capacity=id.capacity;
 if(id.identity!==m.identity||id.capacity!==m.capacity)fail('Snapshot belongs to another device');
 const p={schema:'sp1-shared-fat396-v1',identity:id.identity,capacity:id.capacity,mode:'restore',path:'/TAPELOOP',guards:[],extents:[],hardwareQualified:false};
 let done=0,total=m.files.reduce((n,f)=>n+f.blocks*512,0);
 // Publish partition metadata last. Gate excludes runtime throughout.
 for(const f of [...m.files].sort((a,b)=>(a.lba===0)-(b.lba===0))){const b=new Uint8Array(await(await(await source.getFileHandle(f.name)).getFile()).arrayBuffer()),original=await read(d,f.lba,f.blocks);if(!equal(b,original)){const key=String(p.extents.length).padStart(6,'0');await sink.save(key,b);await sink.save('before-'+key,original);p.extents.push({key,lba:f.lba,blocks:f.blocks,target:f.sha256,original:await sha256(original)});}done+=b.length;progress({phase:'prepare-restore',done,total});}
 if(!p.extents.length)fail('Snapshot already matches; no restore needed');p.digest=await sha256(enc(p));await validate(p,sink);await sink.seal(p);return p;
}

// Finish only an already-written, still-locked transaction. Never writes media.
export async function finishVerification(d,sink,p,j,progress=()=>{}){
 await validate(p,sink);await bind(d,p);const rows=await j.read();
 if(!rows.some(e=>e.state==='admitted'&&e.digest===p.digest&&e.identity===p.identity)||rows.some(e=>['restore-intent','restored','recovered','complete'].includes(e.state)))fail('Unfinished original installation journal required');
 if(!(await d.bootGate.status()).blocked)fail('Transaction is not locked');
 await d.bootGate.lock(p.digest);await compare(d,p,sink,false,progress);
 const result=await inspect(d);if(p.mode!=='restore'&&(!result.native||result.native.path!=='/TAPELOOP'))fail('Installed filesystem admission failed');
 await j({state:'verified',digest:p.digest});await d.bootGate.unlock(p.digest);await j({state:'complete',digest:p.digest});return result;
}
