// Stable fingerprint of every logical record (including all archive links,
// current selections, metadata/control references, extents and CRCs). Physical
// COW page positions and file slack are intentionally excluded.
import {sha256} from '../shared396/site/backup/core.mjs';
export async function catalogFingerprint(index){
 const entries=[...index.records].sort((a,b)=>a[0]-b[0]);
 const b=new Uint8Array(16+entries.length*36),v=new DataView(b.buffer);
 v.setBigUint64(0,BigInt(index.generation),true);v.setUint32(8,entries.length,true);v.setUint32(12,1,true);
 for(let i=0;i<entries.length;i++){const [id,record]=entries[i];if(!Number.isInteger(id)||id<0||record.length!==32)throw Error('Invalid preservation record');v.setUint32(16+i*36,id,true);b.set(record,20+i*36);}
 return sha256(b);
}
