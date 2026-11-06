// Host producer/reader for the C index, with one explicit activation root.
// Builds an offline immutable image; caller journals FAT file publication.
import {crc32} from '../../shared396/site/backup/core.mjs';
const MAGIC=new TextEncoder().encode('PG422IDX');
const get=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
const put=(b,o,v)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(o,v,true);
const LIMIT=123*123*15;
function page(type){const b=new Uint8Array(512);b.set(MAGIC);put(b,8,type);return b;}
function seal(b){put(b,508,crc32(b.subarray(0,508)));return b;}
function valid(b,type){return b?.length===512&&MAGIC.every((x,i)=>b[i]===x)&&get(b,8)===type&&get(b,508)===crc32(b.subarray(0,508));}
export function encodeIndex(records,{generation=1n,sectors=null}={}){
 generation=BigInt(generation);if(generation<1n||generation>0xffffffffffffffffn)throw Error('Generation range');
 const leaves=new Map();
 for(const [id,b]of records){if(!Number.isInteger(id)||id<0||id>=LIMIT||b.length!==32)throw Error('Record bounds');const n=Math.floor(id/15);if(!leaves.has(n))leaves.set(n,page(3));leaves.get(n).set(b,28+id%15*32);}
 const output=[null,null],root=page(2),branches=new Map();
 for(const [n,leaf]of[...leaves].sort((a,b)=>a[0]-b[0])){const a=Math.floor(n/123),b=n%123;if(!branches.has(a))branches.set(a,page(1));put(branches.get(a),16+b*4,output.length);output.push(seal(leaf));}
 for(const [a,branch]of branches){put(root,16+a*4,output.length);output.push(seal(branch));}
 const rootAt=records.size?output.length:0;if(rootAt)output.push(seal(root));
 const superblock=page(0);put(superblock,12,Number(generation&0xffffffffn));put(superblock,16,Number(generation>>32n));put(superblock,20,rootAt);put(superblock,24,output.length);seal(superblock);output[0]=superblock;output[1]=superblock.slice();
 const used=output.length;sectors??=Math.max(5,used);if(!Number.isInteger(sectors)||sectors<Math.max(5,used))throw Error('Index extent too small');
 const image=new Uint8Array(sectors*512);output.forEach((b,i)=>image.set(b,i*512));return {image,used,generation};
}
export function decodeIndex(image){
 if(image.length%512||image.length<5*512)throw Error('Index geometry');
 const at=n=>image.subarray(n*512,(n+1)*512),candidates=[];
 for(let slot=0;slot<2;slot++){const b=at(slot);if(!valid(b,0))continue;const generation=BigInt(get(b,12))|(BigInt(get(b,16))<<32n),root=get(b,20),tail=get(b,24);if(tail<2||tail>image.length/512||root&&(root<2||root>=tail))continue;candidates.push({slot,generation,root,tail});}
 candidates.sort((a,b)=>a.generation>b.generation?-1:a.generation<b.generation?1:0);const chosen=candidates[0];if(!chosen)throw Error('No index checkpoint');
 if(candidates[1]?.generation===chosen.generation&&(candidates[1].root!==chosen.root||candidates[1].tail!==chosen.tail))throw Error('Conflicting checkpoints');
 const seen=new Set();function load(n,type){if(n<2||n>=chosen.tail||seen.has(n)||!valid(at(n),type))throw Error('Invalid index page');seen.add(n);return at(n);}
 const records=new Map();if(chosen.root){const root=load(chosen.root,2);for(let a=0;a<123;a++){const n=get(root,16+a*4);if(!n)continue;const branch=load(n,1);for(let b=0;b<123;b++){const n=get(branch,16+b*4);if(!n)continue;const leaf=load(n,3);for(let r=0;r<15;r++){const record=leaf.slice(28+r*32,60+r*32);if(record.some(x=>x))records.set((a*123+b)*15+r,record);}}}}
 return {...chosen,records,pages:seen.size};
}
