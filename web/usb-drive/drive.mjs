import {inspect,read,lba,equal} from './fat.mjs';
import {sha256} from './backup/core.mjs';
import {save,saveJson,load,json} from './files.mjs';
const enc=x=>new TextEncoder().encode(JSON.stringify(x));
const fail=x=>{throw Error(x)};
export const exchangeRoot=own=>own==='/SP1'?'':own;
export const mutablePath=(p,own)=>{const root=exchangeRoot(own);return p===root+'/IMPORTS'||p.startsWith(root+'/IMPORTS/')||p===root+'/EXPORTS'||p.startsWith(root+'/EXPORTS/')};
export const osPath=(p,roots=[])=>roots.some(r=>p===r||p.startsWith(r+'/'));
export function descriptors(s){return s.files.filter(f=>!mutablePath(f.path,s.native.path)&&!osPath(f.path,s.osRoots)).map(f=>({path:f.path,bytes:f.bytes,cs:f.cs}));}
function ranges(s){
 const r=[{lba:0,blocks:1},{lba:s.g.first,blocks:s.g.data-s.g.first}];
 for(const d of s.dirs)for(const c of d.cs)r.push({lba:lba(s.g,c),blocks:s.g.spc});
 for(const f of s.files.filter(f=>mutablePath(f.path,s.native.path)||osPath(f.path,s.osRoots)))for(const c of f.cs)r.push({lba:lba(s.g,c),blocks:s.g.spc});
 r.sort((a,b)=>a.lba-b.lba);const out=[];for(const x of r){const last=out.at(-1);if(last&&x.lba<=last.lba+last.blocks)last.blocks=Math.max(last.lba+last.blocks,x.lba+x.blocks)-last.lba;else out.push({...x})}return out;
}
export async function capability(d){await d.simple('S409CAPS?','STORAGE409 DRIVE protected-fat-v1');}
export async function bind(d,m){const id=await d.identify();d.capacity=id.capacity;if(m&&(m.identity!==id.identity||m.capacity!==id.capacity))fail('Choose the original SP-1');await capability(d);return id;}
export async function prepare(d,parent,progress=()=>{}){
 const id=await bind(d);if((await d.bootGate.status()).blocked)fail('Recover the previous drive session first');
 // Qualify 16-sector reads on this connection before enabling burst transfers.
 const slow=await read(d,0,16);d.readSectors=16;const fast=await read(d,0,16);if(!equal(slow,fast))fail('USB burst read comparison failed');
 const s=await inspect(d);if(!s.native||s.g.spc<16||s.g.end>524288)fail('Drive geometry not supported by this candidate');
 const own=s.native.path,root=exchangeRoot(own);if(!s.dirs.some(x=>x.path===root+'/IMPORTS')||!s.dirs.some(x=>x.path===root+'/EXPORTS'))fail('Imports/Exports directories missing');
 const folder=await parent.getDirectoryHandle('SP1-DRIVE-'+id.identity+'-'+crypto.randomUUID(),{create:true});
 const m={schema:'sp1-drive409-v1',identity:id.identity,capacity:id.capacity,geometry:s.g,own,osRoots:s.osRoots,protected:descriptors(s),catalogs:[],files:[],complete:false};
 for(const e of s.native.extents.slice(0,2))m.catalogs.push({lba:e.lba,sha256:await sha256(await read(d,e.lba,8))});
 const rs=ranges(s),total=rs.reduce((a,x)=>a+x.blocks*512,0);let done=0;
 for(const r of rs)for(let off=0;off<r.blocks;off+=128){const n=Math.min(128,r.blocks-off),b=await read(d,r.lba+off,n),name=`extent-${String(m.files.length).padStart(6,'0')}.bin`;await save(folder,name,b);m.files.push({name,lba:r.lba+off,blocks:n,sha256:await sha256(b)});done+=b.length;progress({phase:'Saving drive recovery',done,total});}
 for(const x of s.guards)if(!equal(await read(d,x.lba,x.b.length/512),x.b))fail('Filesystem changed during preparation');
 m.complete=true; m.digest=await sha256(enc(m));await saveJson(folder,'drive.json',m);await verify(folder);return {folder,manifest:m};
}
export async function verify(folder){const m=await json(folder,'drive.json'),{digest,...body}=m;
 if(m.schema!=='sp1-drive409-v1'||!m.complete||!/^[a-f0-9]{16}$/.test(m.identity)||m.capacity!==7733248||!Array.isArray(m.files)||!m.files.length||await sha256(enc(body))!==digest)fail('Invalid drive recovery manifest');
 let end=0;for(const f of m.files){if(!/^extent-\d{6}\.bin$/.test(f.name)||!Number.isInteger(f.lba)||!Number.isInteger(f.blocks)||f.blocks<1||f.blocks>128||f.lba<end||f.lba+f.blocks>m.capacity)fail('Invalid recovery extent');const b=await load(folder,f.name);if(b.length!==f.blocks*512||await sha256(b)!==f.sha256)fail('Recovery file damaged');end=f.lba+f.blocks;}return m;
}
export async function open(d,folder){const m=await verify(folder);await bind(d,m);if((await d.bootGate.status()).blocked)fail('A previous transaction is locked');
 // Recheck saved preimages in case preparation and mounting were separated.
 for(const f of m.files)if(await sha256(await read(d,f.lba,f.blocks))!==f.sha256)fail('Filesystem changed; prepare a fresh drive session');
 await d.bootGate.lock(m.digest);await d.begin();await d.hostStart();return m;
}
async function protectedCheck(d,m){const s=await inspect(d);if(JSON.stringify(s.g)!==JSON.stringify(m.geometry)||s.native?.path!==m.own)fail('Geometry or native library changed');const now=descriptors(s);for(const f of m.protected){const found=now.find(x=>x.path===f.path);if(JSON.stringify(found)!==JSON.stringify(f))fail('Protected file allocation changed: '+f.path)}for(const c of m.catalogs)if(await sha256(await read(d,c.lba,8))!==c.sha256)fail('Native catalog changed');return s;}
export async function finish(d,folder){const m=await verify(folder);await bind(d,m);const h=await d.hostStatus();if(h.active||h.failed||!h.ejected)fail('Eject in Finder/Explorer first; interrupted sessions use recovery');await d.sync();await protectedCheck(d,m);await saveJson(folder,'result.json',{state:'verified',identity:m.identity,at:new Date().toISOString(),metrics:await d.hostMetrics()});await d.bootGate.unlock(m.digest);return m;}
export async function recover(d,folder,progress=()=>{}){const m=await verify(folder);await bind(d,m);const h=await d.hostStatus();if(h.active)fail('Eject the mounted drive before recovery');const gate=await d.bootGate.status();if(!gate.blocked||!gate.valid)fail('No recoverable drive lock');
 // Firmware compares the digest when unlocking; match it BEFORE any writes too.
 await d.bootGate.lock(m.digest);await d.begin();const total=m.files.reduce((n,f)=>n+f.blocks*512,0);let done=0;
 for(const f of [...m.files].reverse()){const b=await load(folder,f.name);for(let off=0;off<f.blocks;off+=16){const n=Math.min(16,f.blocks-off),target=b.slice(off*512,(off+n)*512),before=await read(d,f.lba+off,n);if(!equal(before,target))await d.writeBounded(f.lba+off,before,target);if(!equal(await read(d,f.lba+off,n),target))fail('Recovery readback failed');done+=n*512;progress({phase:'Restoring exchange session',done,total});}}
 await d.sync();await protectedCheck(d,m);await saveJson(folder,'result.json',{state:'recovered',identity:m.identity,at:new Date().toISOString()});await d.bootGate.unlock(m.digest);return m;
}
