import {check,equal} from './plan.mjs?v=565';
export const encode=value=>value instanceof Uint8Array?value:new TextEncoder().encode(JSON.stringify(value,null,2)+'\n');
export async function readFile(dir,name,max){const h=await dir.getFileHandle(name),f=await h.getFile();check(f.size<=max,'File size exceeds bound: '+name);return new Uint8Array(await f.arrayBuffer());}
export async function saveNew(dir,name,value){
 const data=encode(value);let exists=false;
 try{await dir.getFileHandle(name);exists=true;}catch(e){if(e.name!=='NotFoundError')throw e;}
 check(!exists,'Refusing to overwrite original file: '+name);
 const handle=await dir.getFileHandle(name,{create:true}),writer=await handle.createWritable({keepExistingData:false});
 try{await writer.write(data);await writer.close();}catch(error){try{await writer.abort();}catch{}throw error;}
 const actual=await readFile(dir,name,data.length);check(equal(data,actual),'Saved file readback failed: '+name);
}
export async function newTransaction(root,mode){
 const name=new Date().toISOString().replace(/[:.]/g,'-')+'-setup335-'+mode+'-'+crypto.randomUUID();
 const dir=await root.getDirectoryHandle(name,{create:true});
 await saveNew(dir,'browser-session.json',{schema:1,adapter:335,mode,created:new Date().toISOString(),public_qualified:false});
 return {dir,name,save:(name,value)=>saveNew(dir,name,value)};
}
export async function originalJournal(dir){
 const manifest=JSON.parse(new TextDecoder().decode(await readFile(dir,'recovery.json',16384))),before=await readFile(dir,'radio-backup.bin',524288);
 check(before.length===524288&&manifest.adapter===335&&['migrate','rehearse'].includes(manifest.mode),'Choose an original setup335 installation/rehearsal folder');
 return {manifest,before,name:dir.name};
}
