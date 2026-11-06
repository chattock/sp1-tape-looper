importScripts('./vendor/ffmpeg-core.js?v=audio-4d55a69a','./presentation.js?v=571');
let sharedCore;
const MAX_FRAMES=23045120; // Existing output bound; independent of duration metadata.
function needClip(duration){const e=Error(Number.isFinite(duration)&&duration>0?'Audio is '+Math.floor(duration/60)+':'+String(Math.floor(duration%60)).padStart(2,'0')+'; choose a clip up to 8 minutes.':'Choose a clip up to 8 minutes.');e.code='CLIP_REQUIRED';e.duration=Number.isFinite(duration)&&duration>0?duration:null;return e}
self.onmessage=async e=>{let ff;try{
 const {bytes,stream,channelMode,clip}=e.data;
 if(clip&&(!Number.isFinite(clip.start)||!Number.isFinite(clip.end)||clip.start<0||clip.end<=clip.start||clip.end-clip.start>480+1e-8))throw Error('Choose a valid clip up to 8 minutes');
 const reused=!!sharedCore;postMessage({phase:reused?'Converting audio':'Loading local decoder'});
 ff=sharedCore??=await createFFmpegCore({mainScriptUrlOrBlob:self.location.href+'#'+btoa(JSON.stringify({wasmURL:new URL('./vendor/ffmpeg-core.wasm?v=audio-98ea17c3',self.location).href,workerURL:''}))});ff.reset();ff.setTimeout(90000);
 const logs=[];ff.setLogger(e=>{logs.push(e.message);if(logs.length>8)logs.shift()});ff.FS.writeFile('source',new Uint8Array(bytes));
 ff.ffprobe('-v','error','-show_streams','-of','json','-o','probe.json','source');
 const meta=JSON.parse(new TextDecoder().decode(ff.FS.readFile('probe.json'))),streams=(meta.streams??[]).filter(s=>s.codec_type==='audio');let s;
 if(stream==null){if(streams.length!==1)throw Error(streams.length?'Choose an audio stream index; this file has '+streams.length+' audio streams':'No audio stream found');s=streams[0]}else s=streams.find(s=>s.index===stream);
 if(!s)throw Error('No selected audio stream');
 const duration=Number(s.duration);
 if(!clip&&Number.isFinite(duration)&&duration>MAX_FRAMES/48000)throw needClip(duration);
 if(clip&&Number.isFinite(duration)&&duration>0&&(clip.start>=duration||clip.end>duration+0.05))throw Error('Choose times within the source audio.');
 if(s.channels>2&&channelMode==='auto')throw Error('Choose Stereo downmix or Mono for multichannel audio');
 const channels=channelMode==='mono'?1:channelMode==='stereo'?2:s.channels;if(![1,2].includes(channels))throw Error('Unsupported channel layout');
 postMessage({phase:'Converting with local decoder'});ff.reset();ff.setTimeout(90000);
 const extra=s.codec_name==='aac'&&!clip?Math.ceil(4096*48000/Number(s.sample_rate))+2:0;
 const frameCap=clip?Math.round((clip.end-clip.start)*48000):MAX_FRAMES,cap=(frameCap+extra)*channels*4;
 if(!Number.isSafeInteger(cap)||cap<=0)throw Error('Invalid audio sample-rate metadata');
 // Accurate input seek avoids decoding an hour-long prefix. -t is used only
 // after explicit clip consent. The byte bound detects unintended truncation.
 const seek=clip?['-ss',String(clip.start)]:[],length=clip?['-t',String(clip.end-clip.start)]:[];
 if(ff.exec('-v','error',...seek,'-i','source',...length,'-map','0:'+s.index,'-vn','-ar','48000','-ac',String(channels),'-c:a','pcm_f32le','-f','f32le','-fs',String(cap+4),'output.raw'))throw Error('Decoder failed or timed out');
 const raw=ff.FS.readFile('output.raw');
 if(!raw.length||raw.length%(channels*4))throw Error('Audio could not be decoded completely.');
 if(raw.length>cap){if(!clip)throw needClip(Number.isFinite(duration)?duration:null);throw Error('Decoded clip exceeds its selected length.');}
 const clipped=clip?{raw,presentation:null}:self.clipAACPresentation473(raw,channels,s);
 if(clipped.raw.length>MAX_FRAMES*channels*4)throw needClip(duration);
 postMessage({ok:true,decoderReused:reused,raw:clipped.raw,channels,codec:s.codec_name,stream:s.index,presentation:clipped.presentation,clip:clip?{...clip,sourceDuration:Number.isFinite(duration)?duration:null}:null},[clipped.raw.buffer]);
}catch(e){postMessage({ok:false,error:e.message,code:e.code,duration:e.duration})}finally{if(ff)for(const name of ['source','probe.json','output.raw'])try{ff.FS.unlink(name)}catch{}}};
