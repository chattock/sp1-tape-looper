import {exportTakeName} from './legacy-names.mjs?v=568';
// Music export only. Sequential chunks and readback; never write to SP1 MUSIC.
const table=Uint32Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0});
function crc(c,bytes){for(const b of bytes)c=table[(c^b)&255]^(c>>>8);return c>>>0;}
async function scan(root){const files=[],directories=[];let total=0;const songs=await root.getDirectoryHandle('SONGS');
 async function walk(dir,path){directories.push(path);for await(const [name,h]of dir.entries()){if(name.startsWith('.'))continue;if(h.kind==='directory'){const valid=path.length===1?/^SONG(0[1-9]|1[0-6])$/.test(name):path.length===2?/^TRACK[1-4]$/.test(name):path.length===3&&name==='TAKES';if(valid)await walk(h,[...path,name]);}else if(path.length===4&&/\.wav$/i.test(name)){const file=await h.getFile();files.push({path:[...path,name],handle:h,size:file.size,lastModified:file.lastModified});total+=file.size;}}}
 await walk(songs,['SONGS']);files.sort((a,b)=>a.path.join('/').localeCompare(b.path.join('/'),undefined,{numeric:true}));const counts=new Map();for(const item of files){const key=item.path.slice(0,-1).join('/'),take=(counts.get(key)||0)+1;counts.set(key,take);item.exportName=exportTakeName(item.path,take);}return {files,directories,total};}
async function directory(root,path){let d=root;for(const name of path)d=await d.getDirectoryHandle(name,{create:true});return d;}
async function checksum(file,signal){const r=file.stream().getReader();let c=0xffffffff,n=0;try{while(true){signal?.throwIfAborted();const {done,value}=await r.read();if(done)break;c=crc(c,value);n+=value.length;}}finally{await r.cancel().catch(()=>{});r.releaseLock();}return {crc:(c^0xffffffff)>>>0,bytes:n};}
export async function downloadAll(root,destination,{signal,progress=()=>{},folderName='SP1-takes-'+new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomUUID().slice(0,8)}={}){
 signal?.throwIfAborted();if(!destination||await root.resolve(destination)!==null)throw Error('Choose a computer folder outside the SP1 MUSIC drive.');await root.check?.();
 progress({phase:'Scanning takes',done:0,total:0,bytes:0,ms:0});const plan=await scan(root);if(!plan.files.length)return {files:0,bytes:0,folder:null};signal?.throwIfAborted();
 try{await destination.getDirectoryHandle(folderName);throw Error('Download folder already exists.');}catch(e){if(e.name!=='NotFoundError')throw e;}
 const output=await destination.getDirectoryHandle(folderName,{create:true});for(const path of plan.directories){signal?.throwIfAborted();await directory(output,path);}
 const started=performance.now();let bytes=0,count=0;
 for(const item of plan.files){signal?.throwIfAborted();await root.check?.();const file=await item.handle.getFile();if(file.size!==item.size||file.lastModified!==item.lastModified)throw Error('A take changed during download. Keep the partial folder.');const dest=await directory(output,item.path.slice(0,-1)),handle=await dest.getFileHandle(item.exportName,{create:true}),w=await handle.createWritable(),r=file.stream().getReader();let c=0xffffffff,size=0;
 try{while(true){signal?.throwIfAborted();const {done,value}=await r.read();if(done)break;await w.write(value);c=crc(c,value);size+=value.length;progress({phase:'Downloading',done:count,total:plan.files.length,bytes:bytes+size,totalBytes:plan.total,ms:performance.now()-started});}if(size!==item.size)throw Error('Take length changed during download.');await w.close();}
 catch(e){await w.abort().catch(()=>{});throw e;}finally{await r.cancel().catch(()=>{});r.releaseLock();}
 progress({phase:'Checking',done:count,total:plan.files.length,bytes:bytes+size,totalBytes:plan.total,ms:performance.now()-started});const got=await checksum(await handle.getFile(),signal);if(got.bytes!==size||got.crc!==((c^0xffffffff)>>>0))throw Error('Downloaded file check failed. Keep the partial folder.');bytes+=size;count++;progress({phase:'Downloading',done:count,total:plan.files.length,bytes,totalBytes:plan.total,ms:performance.now()-started});
 }
 await root.check?.();return {files:count,bytes,folder:folderName};
}
