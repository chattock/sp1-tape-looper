import {SIZE,STATIC,ABI,bytes,equal,filled,check,digest,crc32,validateRelease,makePlan,TARGET} from './plan.mjs?v=565';
export function staticProfile607(b){
 check(b instanceof Uint8Array&&b.length===SIZE,'Incomplete Bluetooth inspection');
 check(equal(b.subarray(0,21),STATIC.subarray(0,21))&&equal(b.subarray(27,4096),STATIC.subarray(27))&&!filled(b.subarray(21,27),0)&&!filled(b.subarray(21,27),255),'Bluetooth hardware configuration needs review; nothing was changed');
}
export function freshSettings607(b){
 staticProfile607(b);const out=new Uint8Array(4096).fill(255),address=b.slice(21,27).reverse();
 for(let kind=0;kind<2;kind++){
  const r=new Uint8Array(180);r.set([84,76,66,50,2,kind,kind?65:138,0]);r.set(address,8);r.set(bytes(ABI),14);new DataView(r.buffer).setUint32(176,crc32(r.subarray(0,176)),true);
  const tag=0xf0+kind,c=(r.reduce((a,v)=>a+v,0)^0x55)&255,at=kind*184;out.set([tag,180,c,((tag+180+c)^0x55)&255],at);out.set(r,at+4);
 }
 return out;
}
export async function cleanPlan607(before,identity,app,meta){
 check(/^[a-f0-9]{16}$/.test(identity),'Invalid SP-1 identity');staticProfile607(before);await validateRelease(app,meta);
 let after;
 // A verified healthy current installation keeps its pairings and writes nothing.
 try{const current=await makePlan(before,identity,'migrate',app,meta);if(!current.manifest.sectors.length)after=before.slice()}catch{}
 if(!after){after=before.slice();after.set(freshSettings607(before),4096);after.fill(255,8192,12288);after.set(app,0x3000)}
 const sectors=[];for(let a=4096;a<0x1e000;a+=4096)if(!equal(before.subarray(a,a+4096),after.subarray(a,a+4096)))sectors.push(a);
 check(sectors.length<=29,'Bluetooth target bounds');
 return {after,manifest:{schema:1,adapter:607,identity,mode:'clean',profile:'static-v1',before_sha256:await digest(before),after_sha256:await digest(after),sectors,sector_targets:await Promise.all(sectors.map(a=>digest(after.subarray(a,a+4096)))),release_sha256:TARGET},report:{schema:1,adapter:607,profile:'static-v1',decision:sectors.length?'clean-install-candidate':'already-current',changed_sectors:sectors.length,pairingsReset:sectors.includes(4096),source_application_required:false,forwardRecovery:true,public_qualified:false}};
}
