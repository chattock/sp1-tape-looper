import {inspectDrive} from './jobs.mjs?v=601';

// Both Finder and this adapter go through the host filesystem. Never open a
// second raw-sector connection to a volume the OS has mounted.
export async function openMountedDrive(pick, phase=()=>{}) {
  if (!pick) throw Error('Use desktop Chrome or Edge to open SP1 MUSIC. You can also edit WAVs in Finder.');
  // Invoke the picker before awaiting other work: Chrome requires the click's
  // transient user activation for this permission prompt.
  let raw;
  try { raw=await pick({mode:'readwrite',id:'sp1-music-library'}); }
  catch(e) { if(e.name==='AbortError')throw Error('No drive selected. Select the SP1 MUSIC drive itself.');throw e; }
  await parent.sp1Workspace?.prepareDriveAccess();
  if(raw.name.toUpperCase()!=='SP1 MUSIC')throw Error('Select the SP1 MUSIC drive itself, not SONGS, a track folder or a backup folder.');
  const info=await inspectDrive(raw);
  const songs=await raw.getDirectoryHandle('SONGS');
  for(let s=1;s<=16;s++)await songs.getDirectoryHandle('SONG'+String(s).padStart(2,'0'));
  if(!navigator.locks)throw Error('This browser cannot coordinate safe library access. Use Finder instead.');
  let release,closed=false;
  await new Promise((resolve,reject)=>{
    navigator.locks.request('sp1-library-session-'+info.identity,{ifAvailable:true},async lock=>{
      if(!lock){reject(Error('This SP-1 library is already open in another browser tab. Finish there first.'));return;}
      await new Promise(done=>{release=done;resolve();});
    }).catch(reject);
  });
  function assertOpen(){if(closed)throw Error('Library closed. After Tape Looper returns, hold T2 + T3 for 3 seconds and select the SP1 MUSIC drive itself.');}
  const metrics={checks:0,checkMs:0};
  async function check(){
    if(closed)throw Error('Library closed. After Tape Looper returns, hold T2 + T3 for 3 seconds and select the SP1 MUSIC drive itself.');
    const began=performance.now();metrics.checks++;
    try {
      const now=await inspectDrive(raw);
      if(now.identity!==info.identity||now.session!==info.session)throw Error('SP1 MUSIC changed or reconnected. Finish this view and connect again.');return now;
    } catch(e){e.uncertain=true;throw e;}finally{metrics.checkMs+=performance.now()-began;}
  }
  function close(){if(closed)return;closed=true;release();removeEventListener('pagehide',close);}
  addEventListener('pagehide',close,{once:true});
  function directory(handle){return {
    name:handle.name,kind:'directory',
    async *entries(){await check();for await(const [name,h]of handle.entries())yield[name,h.kind==='directory'?directory(h):file(h)];},
    async getDirectoryHandle(name,options){assertOpen();if(options?.create)throw Error('SP-1 manages its song and track folders.');return directory(await handle.getDirectoryHandle(name));},
    async getFileHandle(name,options){assertOpen();
      // Looking up an existing handle cannot change media. Validate when reading
      // its payload, creating a file, writing, publishing or deleting instead.
      if(name==='RESULT.TXT'&&handle===raw){if(options?.create)throw Error('Device status is read-only.');const status=await handle.getFileHandle(name);return {name,kind:'file',getFile:async()=>{assertOpen();return status.getFile();}};}
      if(options?.create)await check();return file(await handle.getFileHandle(name,options));},
    async removeEntry(name,options){await check();if(options?.recursive)throw Error('Only individual takes can be removed.');return handle.removeEntry(name,options);},
    async resolve(other){return handle.resolve(other);}
  };}
  function file(handle){return {
    name:handle.name,kind:'file',
    async getFile(){await check();return handle.getFile();},
    async createWritable(){await check();const stream=await handle.createWritable();return {
      async write(bytes){await check();return stream.write(bytes);},
      async truncate(size){await check();return stream.truncate(size);},
      async close(){await check();return stream.close();},
      async abort(){return stream.abort();}
    };}
  };}
  const root=directory(raw);let sourceBudget=null;
  root.admitBatch=async jobs=>{const current=await check();const free=/Source free bytes: (\d+)/.exec(current.text),capacity=/Source capacity bytes: (\d+)/.exec(current.text);if(!free||!capacity)throw Error('Install the current STANDARD BIN and run Setup v4 before importing audio. Your saved recordings are kept.');const needed=32768+jobs.reduce((sum,j)=>sum+Math.ceil((j.upload?.bytes||j.audio.bytes).length/32768)*32768+65536,0);const available=Math.min(Number(free[1]),sourceBudget??Number(free[1]));if(needed>available)throw Error(needed<=Number(capacity[1])?'Eject to save earlier copies, then reopen the drive before adding this batch.':Number(capacity[1])<100*1048576?'Expand import space in Setup v4 first. Your saved recordings are kept.':'This batch exceeds the drive copy space. Add fewer files at a time.');sourceBudget=available-needed;};
  root.mounted=true;root.close=close;root.check=check;root.metrics=metrics;
  phase('Library connected.');
  return {root,info};
}
