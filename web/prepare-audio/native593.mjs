import {prepareNativePayload} from './native593-codec.mjs?v=593';
const table=Uint32Array.from({length:256},(_,x)=>{for(let i=0;i<8;i++)x=(x>>>1)^((x&1)?0xedb88320:0);return x>>>0});
export function crc593(b){let x=0xffffffff;for(const y of b)x=(x>>>8)^table[(x^y)&255];return (~x)>>>0}
export function nativeCapability(state){const generation=/Library generation: (\d+)/.exec(state.text)?.[1];return /(?:^|\n)Native import: p14s-v1\r?\n/.test(state.text)&&generation&&BigInt(generation)>0n?generation:null}
export function encodeNative593(source,{identity,session,generation,song,track,action,target}){
 if(!/^[a-f0-9]{16}$/.test(identity||'')||!/^\d+$/.test(session||'')||!/^\d+$/.test(generation||'')||!Number.isInteger(song)||song<1||song>16||!Number.isInteger(track)||track<1||track>4||!['add','replace'].includes(action))throw Error('Invalid native import binding');
 for(const n of [BigInt(session),BigInt(generation)])if(n<=0n||n>0xffffffffffffffffn)throw Error('Invalid native import generation');
 const ordinal=action==='replace'&&/^T\d{7}\.WAV$/i.test(target||'')?Number(target.slice(1,8)):0;if(action==='replace'&&!ordinal)throw Error('Choose an existing take');
 const r=prepareNativePayload(source);if(r.frames>35840*643)throw Error('Audio exceeds supported loop length');
 const b=new Uint8Array(512+r.payload.length),v=new DataView(b.buffer);b.set(new TextEncoder().encode('SP1N593\0'));v.setUint32(8,1,true);v.setUint32(12,512,true);v.setUint32(16,1,true);v.setUint32(20,r.frames,true);v.setUint32(24,r.payload.length,true);v.setUint32(28,r.blocks,true);v.setUint32(32,(song-1)*4+track-1,true);v.setUint32(36,action==='replace'?2:3,true);v.setUint32(40,ordinal,true);v.setBigUint64(48,BigInt(generation),true);v.setBigUint64(56,BigInt('0x'+identity),true);v.setBigUint64(64,BigInt(session),true);v.setUint32(72,crc593(r.payload),true);v.setUint32(76,crc593(b.subarray(0,512)),true);b.set(r.payload,512);return b;
}
export async function encodeNativeWorker593(source,binding,{signal}={}){
 if(signal?.aborted)throw new DOMException('Cancelled','AbortError');
 const worker=new Worker(new URL('./native593-worker.mjs?v=593',import.meta.url),{type:'module'});
 return new Promise((resolve,reject)=>{
  const finish=(error,result)=>{worker.terminate();signal?.removeEventListener('abort',abort);error?reject(error):resolve(result)};
  const abort=()=>finish(new DOMException('Cancelled','AbortError'));signal?.addEventListener('abort',abort,{once:true});
  worker.onerror=e=>finish(Error(e.message||'Audio preparation failed'));worker.onmessage=e=>e.data.error?finish(Error(e.data.error)):finish(null,new Uint8Array(e.data.bytes));
  const copy=source.slice();worker.postMessage({source:copy.buffer,binding},[copy.buffer]);
 });
}
export async function prepareUploads593(jobs,state,phase=()=>{}){
 const generation=nativeCapability(state);
 for(const j of jobs){
  if(!generation){delete j.upload;continue;}
  const binding={identity:state.identity,session:state.session,generation,song:j.song,track:j.track,action:j.action,target:j.target};
  const key=JSON.stringify(binding);
  if(j.upload?.binding===key)continue;
  phase('Preparing '+j.source+'…');const started=performance.now();
  const bytes=await encodeNativeWorker593(j.audio.bytes,binding);
  const sha256=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
  j.upload={bytes,sha256,binding:key,codec:'P14S-v1',preparationMs:performance.now()-started};
 }
 return jobs;
}
