// Browser counterpart of setup335/plan.py. Python parity fixtures are the oracle.
export const SIZE=524288, SECTOR=4096;
export const SOURCE='df2c412c00ef69e5db37f4f2fca8dc6bfdba10d319eb4c2775ee24c44710a682', PREVIOUS='b8e4b03137e1c88cbaafc106b6822dbcea6450cce4e2790107abd93473fdd983', TARGET='f435db26aecb5e0b94cc9dede57bc0d035e74af25bd6056bd96ee7c65f6b9ca0';
export const FELDD565='085f762cbac4307ef4b2d2065da4f384c2ed75f67367c9e800f7d0e8491cc59b';
export const ABI='93d1a15a4c383bfa3cce62644d6a77be';
export function check(ok,message){if(!ok)throw new Error(message);}
export const hex=b=>Array.from(b,x=>x.toString(16).padStart(2,'0')).join('');
export function bytes(s){check(typeof s==='string'&&/^(?:[0-9a-f]{2})*$/.test(s),'Invalid hex');return Uint8Array.from(s.match(/../g)||[],v=>parseInt(v,16));}
export const equal=(a,b)=>a.length===b.length&&a.every((v,i)=>v===b[i]);
export const digest=async b=>hex(new Uint8Array(await crypto.subtle.digest('SHA-256',b)));
export const filled=(b,v)=>b.every(x=>x===v);
export const allowed=a=>Number.isInteger(a)&&(a===4096||(a>=0x3000&&a<0x1e000&&a%4096===0));
export const STATIC=new Uint8Array(4096).fill(255);STATIC.set(bytes('010800f000006208c05d89fd0400ffffffff4006000804206a7020020a0000300000001000000010'));
export function crc32(b){let c=0xffffffff;for(const x of b){c^=x;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
const sum=b=>b.reduce((a,v)=>a+v,0);
export function decode(b){
 check(b instanceof Uint8Array&&b.length===SIZE,'Full 512 KiB snapshot required');
 const records=new Map(),offsets=new Map();let retired=0,at=4096;
 while(at+4<=8192){
  const [tag,n,checksum,header]=b.slice(at,at+4);
  if(tag===255){check(filled(b.slice(at,8192),255),'Non-erased bytes after terminator');return {records,offsets,retired,terminator:at};}
  const following=at+4+((n+1)&~1);check(n&&following<=8192,'Record bounds');const payload=b.slice(at+4,at+4+n);
  check(checksum===((sum(payload)^0x55)&255),'Record payload checksum');
  if(tag===0)retired++;
  else if((tag&128)&&(tag&127)>=1&&(tag&127)<=0x7b){
   check(header===(((tag+n+checksum)^0x55)&255),'Active header checksum');const slot=tag&127;
   check(!records.has(slot),'Duplicate active ID');records.set(slot,payload);offsets.set(slot,at+4);
  }else throw new Error('Unknown record tag');
  at=following;
 }throw new Error('No erased terminator');
}
export async function validateRelease(app,meta){check(app.length===0x1b000&&await digest(app)===TARGET&&meta.target_sha256===TARGET&&meta.source_sha256===SOURCE&&meta.previous_sha256===PREVIOUS&&meta.abi===ABI,'Release pin mismatch');}
async function legacy435(b){
 const fixed=new Uint8Array(SIZE-4096);fixed.set(b.slice(0,4096));fixed.set(b.slice(8192),4096);
 if(await digest(fixed)!=='69c9a7a6a0bb181ed8bd09161649ef728166b4cb47683965fe414bbaea1c2e93')return false;
 let at=4096,seen=0;
 while(at+4<=8192){const [tag,n,c,h]=b.slice(at,at+4);
  if(tag===255)return filled(b.slice(at,8192),255)&&seen===3&&at-4096+368<4093;
  if(n!==156||at+160>8192)return false;
  const r=b.slice(at+4,at+160),kind=r[5],size=kind?65:138,t=0xf0+kind;
  if(kind>1||(tag!==0&&tag!==t)||hex(r.slice(0,5))!=='424e443101'||r[6]!==size||r[7]>1)return false;
  if(c!==((sum(r)^0x55)&255)||h!==(((t+156+c)^0x55)&255)||!filled(r.slice(8+size,-4),0)||new DataView(r.buffer).getUint32(152,true)!==crc32(r.slice(0,152)))return false;
  if(tag){if(seen&(1<<kind))return false;seen|=1<<kind;}at+=160;
 }return false;
}
export async function validateSource(b){
 check(b instanceof Uint8Array&&b.length===SIZE,'Full 512 KiB snapshot required');
 check(equal(b.slice(0,21),STATIC.slice(0,21))&&equal(b.slice(27,4096),STATIC.slice(27))&&!filled(b.slice(21,27),0)&&!filled(b.slice(21,27),255),'Unknown static layout/identity');
 const hash=await digest(b.slice(0x3000,0x1e000));
 if(hash===FELDD565){
  const inv=decode(b);check(filled(b.slice(0x1e000),255)&&!inv.records.has(0x70)&&!inv.records.has(0x71)&&inv.terminator-4096+736<4093,'Feldd565 protected tail or unclaimed-slot profile refused');
 }else if(![SOURCE,PREVIOUS,TARGET].includes(hash)){
  check(await legacy435(b)||await digest(b)==='e9363d68f36cee3c0676f162a063a1ebb3b9a754dde28dd166944a44f1f0097a','Unknown immutable image or invalid legacy pairing log; inspect only');
  const inv=decode(b);check(await legacy435(b)||!inv.records.has(0x70)&&!inv.records.has(0x71)&&inv.terminator-4096+736<4093,'Unclaimed-slot profile refused');
 }
 check(filled(b.slice(8192,12288),255),'Backup VS is not erased');return decode(b);
}
async function pairingTarget(b){
 const inv=await validateSource(b),out=b.slice(4096,8192),address=b.slice(21,27).reverse(),abi=bytes(ABI);
 let keep=[false,false],valid=[false,false],repair=false;
 for(let kind=0;kind<2;kind++){
  const r=inv.records.get(0x70+kind);if(!r)continue;
  const old=r.length===156&&hex(r.slice(0,5))==='424e443101',current=r.length===180&&hex(r.slice(0,5))==='544c423202',size=kind?65:138;
  check((old||current)&&r[5]===kind&&r[6]===size&&r[7]<=1,'Owned slot record format mismatch');
  check(new DataView(r.buffer,r.byteOffset).getUint32(r.length-4,true)===crc32(r.slice(0,-4)),'Owned record CRC');
  const start=old?8:32;
  check(filled(r.slice(start+size,-4),0)&&(!current||(filled(r.slice(30,32),0)&&!filled(r.slice(8,14),0)&&!filled(r.slice(8,14),255))),'Owned record padding/address');
  valid[kind]=!!r[7];keep[kind]=current&&equal(r.slice(8,14),address)&&equal(r.slice(14,30),abi);repair ||= !keep[kind];
 }
 if(valid[1]&&!valid[0])repair=true;if(repair)keep=[false,false];let at=inv.terminator-4096;
 for(let kind=0;kind<2;kind++){
  if(keep[kind])continue;check(at+184<4093,'Not enough append capacity; no compaction');
  if(inv.offsets.has(0x70+kind))out[inv.offsets.get(0x70+kind)-4-4096]=0;
  const r=new Uint8Array(180);r.set([84,76,66,50,2,kind,kind?65:138,0]);r.set(address,8);r.set(abi,14);new DataView(r.buffer).setUint32(176,crc32(r.slice(0,-4)),true);
  const tag=0xf0+kind,c=(sum(r)^0x55)&255;out.set([tag,180,c,((tag+180+c)^0x55)&255],at);out.set(r,at+4);at+=184;
 }
 const after=b.slice();after.set(out,4096);const final=decode(after);
 for(const [k,v] of inv.records)if(k!==0x70&&k!==0x71)check(final.records.has(k)&&equal(v,final.records.get(k)),'Unrelated records changed');
 check([...final.records.keys()].filter(k=>k!==0x70&&k!==0x71).length===[...inv.records.keys()].filter(k=>k!==0x70&&k!==0x71).length,'Unrelated records added');
 return out;
}
export async function makePlan(before,identity,mode,app,meta){
 check(/^[0-9a-f]{16}$/.test(identity),'Identity format');check(before instanceof Uint8Array&&before.length===SIZE,'Full snapshot required');await validateRelease(app,meta);
 if(mode!=='inspect')await validateSource(before);const after=before.slice();let sectors=[];
 if(mode==='migrate'){after.set(await pairingTarget(before),4096);after.set(app,0x3000);for(let a=0;a<SIZE;a+=4096)if(!equal(before.slice(a,a+4096),after.slice(a,a+4096)))sectors.push(a);}
 else if(mode==='rehearse')sectors=[0x3000];else check(mode==='inspect','Unknown mode');
 check(sectors.length<=28&&sectors.every(allowed),'Write boundary');
 const manifest={schema:1,adapter:335,identity,mode,before_sha256:await digest(before),after_sha256:await digest(after),sectors,sector_targets:await Promise.all(sectors.map(a=>digest(after.slice(a,a+4096)))),release_sha256:TARGET};
 return {after,manifest};
}
export const canonical=x=>JSON.stringify(x,(_,v)=>v&&typeof v==='object'&&!Array.isArray(v)?Object.fromEntries(Object.entries(v).sort(([a],[b])=>a.localeCompare(b))):v);
export async function recoveryPlan(current,identity,original,app,meta){
 const m=original.manifest;let before=original.before;
 if(original.compact){
  check(current.length===SIZE&&m.sectors.length<=28&&new Set(m.sectors).size===m.sectors.length&&m.sectors.every(allowed)&&original.compact.length===m.sectors.length*SECTOR,'Invalid compact recovery');
  before=current.slice();m.sectors.forEach((a,i)=>before.set(original.compact.subarray(i*SECTOR,(i+1)*SECTOR),a));
  check(await digest(before)===m.before_sha256,'Protected radio bytes changed or original recovery data is damaged');
 }
 check(m.identity===identity&&['migrate','rehearse'].includes(m.mode),'Wrong unit or original transaction');
 const expected=await makePlan(before,identity,m.mode,app,meta);check(canonical(expected.manifest)===canonical(m),'Original journal/backup integrity mismatch');
 check(current.length===SIZE,'Incomplete current backup');
 for(let a=0;a<SIZE;a+=4096)if(!m.sectors.includes(a))check(equal(current.slice(a,a+4096),before.slice(a,a+4096)),'Changed protected bytes; refuse stale/different-device recovery');
 const sectors=m.sectors.filter(a=>!equal(current.slice(a,a+4096),before.slice(a,a+4096)));
 return {after:before,manifest:{schema:1,adapter:335,identity,mode:'recover',before_sha256:await digest(current),after_sha256:await digest(before),sectors,sector_targets:await Promise.all(sectors.map(a=>digest(before.slice(a,a+4096)))),release_sha256:TARGET,original_recovery:original.name}};
}
export async function inspectImage(b,identity,app,meta){
 const hash=await digest(b.slice(0x3000,0x1e000));const report={schema:1,adapter:335,flash_bytes:b.length,application_sha256:hash,source_family:({[SOURCE]:'owned289',[PREVIOUS]:'release322',[TARGET]:'release332',[FELDD565]:'feldd-captured565',['ad5423f1127779991b509acf48c4dc0fbd396829485e86f4842add773902fff0']:'legacy-owned-application-reviewed-B405'})[hash]||'unqualified-source',public_qualified:false,automatic_write_authorized:false};
 try{const {manifest:m}=await makePlan(b,identity,'migrate',app,meta);Object.assign(report,{decision:m.sectors.length?'bench-migration-candidate':'already-current',changed_sectors:m.sectors.length,pairing_records_change:m.sectors.includes(4096)});}catch(error){Object.assign(report,{decision:'compatibility-review-required',reason:error.message});}
 return report;
}
