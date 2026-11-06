/* Optional preview only. Decode in bounded frames; retain compressed input and
 * a small peak envelope, never a full-length PCM buffer or an audio export. */
importScripts('./vendor/ffmpeg-core.js?v=audio-4d55a69a');
let core,loaded=false,info;
self.onmessage=async ({data})=>{try{
 const {id,file,start,end,stream}=data;
 if(!Number.isFinite(start)||!Number.isFinite(end)||start<0||end<=start)throw Error('Choose a valid waveform range.');
 const bins=2048;
 if(!core){postMessage({id,phase:'Loading waveform…'});core=await createFFmpegCore({mainScriptUrlOrBlob:self.location.href+'#'+btoa(JSON.stringify({wasmURL:new URL('./vendor/ffmpeg-core.wasm?v=audio-98ea17c3',self.location).href,workerURL:''}))});core.setLogger(()=>{});}
 if(!loaded){if(!file||file.size<=0||file.size>256*1048576)throw Error('Source exceeds waveform bounds.');const bytes=await file.arrayBuffer();core.FS.writeFile('source',new Uint8Array(bytes));core.reset();core.setTimeout(30000);core.ffprobe('-v','error','-show_streams','-of','json','-o','probe.json','source');const meta=JSON.parse(new TextDecoder().decode(core.FS.readFile('probe.json'))),audio=meta.streams.filter(s=>s.codec_type==='audio');info=stream==null?(audio.length===1?audio[0]:null):audio.find(s=>s.index===stream);if(!info)throw Error('Choose an audio stream before loading its waveform.');loaded=true;}
 const rate=Number(info.sample_rate),windowSize=Math.max(1,Math.ceil((end-start)*rate/bins));
 if(!Number.isSafeInteger(rate)||rate<=0||!Number.isSafeInteger(windowSize)||windowSize>262144)throw Error('Zoom to a shorter range to load its waveform.');
 postMessage({id,phase:'Reading waveform…'});core.reset();core.setTimeout(30000);
 // Overall peak is the maximum across channels: opposite-phase stereo cannot
 // disappear through a mono downmix. No resampling or source edits.
 const filter=`asetnsamples=n=${windowSize}:p=0,astats=metadata=1:reset=1:measure_perchannel=none:measure_overall=Peak_level,ametadata=print:key=lavfi.astats.Overall.Peak_level:file=peaks.txt`;
 if(core.exec('-v','error','-ss',String(start),'-i','source','-t',String(end-start),'-map','0:'+info.index,'-vn','-af',filter,'-f','null','-'))throw Error('Waveform could not be read.');
 const text=new TextDecoder().decode(core.FS.readFile('peaks.txt')),values=[];
 for(const match of text.matchAll(/lavfi\.astats\.Overall\.Peak_level=([^\r\n]+)/g)){const value=match[1].trim().toLowerCase(),db=Number(value),peak=value==='-inf'||db===-Infinity?0:10**(db/20);if(!Number.isFinite(peak)||peak<0)throw Error('Invalid waveform data.');values.push(peak);if(values.length>bins+2)throw Error('Waveform exceeded its preview bounds.');}
 if(!values.length)throw Error('No waveform could be read.');const peaks=Float32Array.from(values);
 postMessage({id,ok:true,start,end,step:windowSize/rate,peaks},[peaks.buffer]);
}catch(error){postMessage({id:data.id,ok:false,error:error.message})}finally{if(core)try{core.FS.unlink('peaks.txt')}catch{}}};
