import {sha256} from './backup/core.mjs?v=396.2';
const enc=new TextEncoder(),dec=new TextDecoder();
export async function load(folder,name){return new Uint8Array(await(await(await folder.getFileHandle(name)).getFile()).arrayBuffer())}
export async function save(folder,name,b){const f=await folder.getFileHandle(name,{create:true}),w=await f.createWritable();try{await w.write(b);await w.close()}catch(e){await w.abort().catch(()=>{});throw e}if(await sha256(await load(folder,name))!==await sha256(b))throw Error('Saved file verification failed: '+name)}
export const json=async(f,n)=>JSON.parse(dec.decode(await load(f,n)));
export const saveJson=(f,n,v)=>save(f,n,enc.encode(JSON.stringify(v)));
export function sink(folder){return {save:(id,b)=>save(folder,`segment-${id}.bin`,b),load:id=>load(folder,`segment-${id}.bin`),seal:p=>saveJson(folder,'plan.json',p)}}
export async function journal(folder){
 const events=await folder.getDirectoryHandle('events',{create:true});
 const read=async()=>{const names=[];for await(const [name] of events.entries()){if(!/^event-[0-9]{8}\.json$/.test(name))throw Error('Unexpected journal entry');names.push(name)}names.sort();const records=[];for(let i=0;i<names.length;i++){if(names[i]!==`event-${String(i+1).padStart(8,'0')}.json`)throw Error('Journal sequence gap');try{const r=await json(events,names[i]);if(r.index!==i+1)throw Error('Journal index mismatch');records.push(r)}catch(e){records.push({state:'incomplete_journal_entry',index:i+1})}}return records};
 let index=(await read()).length;
 const append=async r=>{const name=`event-${String(index+1).padStart(8,'0')}.json`;try{await events.getFileHandle(name);throw Error('Journal entry already exists')}catch(e){if(e.name!=='NotFoundError')throw e}await saveJson(events,name,{...r,index:index+1,at:new Date().toISOString()});index++};
 append.read=read;return append;
}
