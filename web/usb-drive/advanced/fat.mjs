import {catalog} from './catalog.mjs?v=396.2';
import {crc32} from './backup/core.mjs?v=396.2';
// Standard MBR/FAT32 inspection. Read-only until an explicit sector plan is built.
export const u16=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint16(o,true);
export const u32=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
export const put=(b,o,v)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(o,v,true);
export const text=b=>new TextDecoder().decode(b);
export const equal=(a,b)=>a.length===b.length&&a.every((v,i)=>v===b[i]);
const fail=s=>{throw Error(s)};
export async function read(d,lba,n){if(!Number.isSafeInteger(lba)||!Number.isSafeInteger(n)||lba<0||n<1||lba+n>d.capacity)fail('Read outside device');const b=new Uint8Array(n*512);for(let at=0;at<n;at+=16){const c=Math.min(16,n-at),p=await d.read(lba+at,c);if(p.length!==c*512)fail('Short read');b.set(p,at*512)}return b;}
export function geometry(mbr,boot,capacity){
 if(u16(mbr,510)!==0xaa55)fail('No MBR');
 const parts=[];for(let i=0;i<4;i++){const p=446+i*16;if(mbr[p+4])parts.push({type:mbr[p+4],first:u32(mbr,p+8),blocks:u32(mbr,p+12)})}
 if(parts.length!==1||![11,12].includes(parts[0].type))fail('Expected one FAT32 partition');
 const {first,blocks}=parts[0],spc=boot[13],reserved=u16(boot,14),fat=u32(boot,36),root=u32(boot,44),info=u16(boot,48),backup=u16(boot,50);
 if(first<1||!blocks||first+blocks>capacity||u16(boot,510)!==0xaa55||u16(boot,11)!==512||!spc||spc>128||(spc&(spc-1))||reserved<8||boot[16]!==2||u16(boot,17)||u16(boot,19)||u16(boot,22)||u16(boot,42)||u16(boot,40)||u32(boot,32)!==blocks||u32(boot,28)!==first||!fat||fat>16384||info!==1||backup!==6)fail('Unsupported FAT32 geometry');
 const data=first+reserved+fat*2,clusters=Math.floor((first+blocks-data)/spc),end=clusters+2;
 if(clusters<65525||end>0x0ffffff0||end*4>fat*512||root<2||root>=end)fail('Invalid FAT32 capacity');
 return {first,blocks,spc,reserved,fat,root,info,backup,data,clusters,end,capacity};
}
export function chain(g,fat,start){const out=[],seen=new Set();let c=start;while(true){if(c<2||c>=g.end||seen.has(c))fail('Invalid/cyclic FAT chain');seen.add(c);out.push(c);const n=u32(fat,c*4)&0xfffffff;if(n>=0xffffff8)return out;c=n;}}
export const lba=(g,c)=>g.data+(c-2)*g.spc;
export function contiguous(g,fat,c,bytes){if(!bytes||bytes%512)fail('Invalid file length');const list=chain(g,fat,c);if(list.length!==Math.ceil(bytes/(g.spc*512))||list.some((x,i)=>x!==c+i))fail('Native file needs a contiguous reservation');return {lba:lba(g,c),blocks:bytes/512,clusters:list.length};}
export function directoryEntry(b,o){return {name:text(b.slice(o,o+11)),attr:b[o+11],cluster:u16(b,o+26)+u16(b,o+20)*65536,bytes:u32(b,o+28),offset:o};}
export const osNames=new Set(['.Spotlight-V100','.fseventsd','.Trashes','.TemporaryItems']);
function longName(b,o){let sum=0;for(let i=0;i<11;i++)sum=(((sum&1)?128:0)+(sum>>1)+b[o+i])&255;let name='',n=1;
 for(let p=o-32;p>=0&&n<=2;p-=32,n++){const e=b.slice(p,p+32);if(e[11]!==15||e[12]||u16(e,26)||e[13]!==sum||(e[0]&31)!==n||(e[0]&0xa0))return null;
 for(const at of [1,3,5,7,9,14,16,18,20,22,24,28,30]){const c=u16(e,at);if(c!==65535)name+=String.fromCharCode(c);}if(e[0]&64)return name.split('\0')[0];}return null;}
export async function inspect(d,progress=()=>{}){
 const mbr=await read(d,0,1),first=u32(mbr,454);if(first<1||first>=d.capacity)fail('No supported partition');
 const boot=await read(d,first,1),g=geometry(mbr,boot,d.capacity),fat=await read(d,first+g.reserved,g.fat),mirror=await read(d,first+g.reserved+g.fat,g.fat);
 if(!equal(fat,mirror))fail('FAT mirrors differ');
 if((u32(fat,0)&0xfffffff)<0xffffff8||(u32(fat,4)&0xfffffff)<0xffffff8)fail('Invalid reserved FAT entries');
 const bootCopy=await read(d,first+6,1);if(!equal(boot,bootCopy))fail('Backup boot sector differs');
 const info=await read(d,first+1,1),infoCopy=await read(d,first+7,1);
 for(const b of [info,infoCopy])if(u32(b,0)!==0x41615252||u32(b,484)!==0x61417272||u32(b,508)!==0xaa550000)fail('Invalid FSInfo');
 const used=new Set(),dirs=[],files=[],guards=[{lba:0,b:mbr},{lba:first,b:boot},{lba:first+6,b:bootCopy},{lba:first+1,b:info},{lba:first+7,b:infoCopy},{lba:first+g.reserved,b:fat},{lba:first+g.reserved+g.fat,b:mirror}];
 async function walk(c,path,depth){if(depth>32||dirs.length>4096)fail('Directory bounds exceeded');const cs=chain(g,fat,c);for(const cl of cs){if(used.has(cl))fail('Cross-linked directory');used.add(cl)}
 if(cs.length*g.spc*512>8*1024*1024)fail('Directory too large');const b=new Uint8Array(cs.length*g.spc*512);for(let i=0;i<cs.length;i++){const x=await read(d,lba(g,cs[i]),g.spc);b.set(x,i*g.spc*512);guards.push({lba:lba(g,cs[i]),b:x})}
 const dir={path,c,cs,b,entries:[]};dirs.push(dir);
 for(let o=0;o<b.length;o+=32){if(!b[o])break;if(b[o]===229||b[o+11]===15)continue;const e=directoryEntry(b,o);e.longName=longName(b,o);if(e.name==='..         '||e.name==='.          '||e.attr&8)continue;
 if(e.attr&0xc0)fail('Invalid directory attributes');if(dir.entries.some(x=>x.name===e.name))fail('Duplicate directory name');dir.entries.push(e);
 if(e.attr&16){if(e.bytes)fail('Directory length invalid');await walk(e.cluster,path+'/'+e.name.trim(),depth+1)}else{
 const cs=e.cluster?chain(g,fat,e.cluster):[];if(cs.length!==Math.ceil(e.bytes/(g.spc*512)))fail('File allocation/length mismatch');for(const cl of cs){if(used.has(cl))fail('Cross-linked file');used.add(cl)}files.push({...e,path:path+'/'+e.name,cs});}}
 progress({phase:'inspect-filesystem',done:used.size,total:g.clusters});return dir;}
 const root=await walk(g.root,'',0);
 // Reject orphan/reserved chains rather than guessing which allocations are disposable.
 for(let c=2;c<g.end;c++){const n=u32(fat,c*4)&0xfffffff;if(n&&n!==0xffffff7&&!used.has(c))fail('Orphan allocation requires filesystem repair');}
 const own=root.entries.find(e=>e.name==='TAPELOOP   '),legacy=root.entries.find(e=>e.name==='SP1        ');
 let native=null;
 if(own||legacy){const e=own||legacy;if(!(e.attr&16))fail('Owned name is not a directory');const dir=dirs.find(x=>x.c===e.cluster);const names=['CAT0    BIN','CAT1    BIN','AUDIO   BIN'];const entries=names.map(n=>dir.entries.find(e=>e.name===n));
 if(entries.every(Boolean)){const extents=entries.map(e=>{if(e.attr&16)fail('Native file is a directory');return contiguous(g,fat,e.cluster,e.bytes)});if(extents[0].blocks!==8||extents[1].blocks!==8||extents[2].blocks<32||extents[2].blocks%g.spc)fail('Invalid native files');const cats=[];for(const e of extents.slice(0,2))cats.push(await read(d,e.lba,8));const decoded=cats.map(b=>catalog(b,extents[2].blocks));if(decoded[0].generation===decoded[1].generation&&!equal(cats[0],cats[1]))fail('Conflicting catalogs');for(const c of decoded){const o=c.objects.find(o=>o.id===c.control-1);if(!o||o.blocks!==6||o.tag!==0x314c5443)fail('Missing native settings');const b=await read(d,extents[2].lba+o.first,o.blocks);if(crc32(b)!==o.crc||u32(b,0)!==0x53453341||u32(b,4)>=16)fail('Native settings integrity failed');}native={path:own?'/TAPELOOP':'/SP1',extents,catalogs:decoded};}
 else if(own)fail('TAPELOOP exists without a complete library');}
 const osRoots=root.entries.filter(e=>(e.attr&16)&&osNames.has(e.longName)).map(e=>'/'+e.name.trim());
 return {g,mbr,boot,fat,info,infoCopy,root,dirs,files,guards,native,osRoots};
}
