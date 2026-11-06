export const LIMITS={source:256*1048576,frames:23045120,channels:2};
export function compatible(bytes){const b=new Uint8Array(bytes),v=new DataView(b.buffer,b.byteOffset,b.byteLength),name=o=>String.fromCharCode(...b.subarray(o,o+4));if(b.length<12||name(0)!=='RIFF'||name(8)!=='WAVE')return null;if(v.getUint32(4,true)!==b.length-8)throw Error('WAV length is damaged');let at=12,fmt=null,data=null,chunks=0;while(at<b.length){if(++chunks>1024||at+8>b.length)throw Error('Invalid WAV chunks');const n=v.getUint32(at+4,true),tag=name(at);at+=8;if(n>b.length-at)throw Error('Truncated WAV');if(tag==='fmt '){if(fmt)throw Error('Duplicate WAV format');fmt={at,n}}if(tag==='data'){if(data)throw Error('Duplicate WAV audio');data={at,n}}at+=n+(n&1)}if(at!==b.length||!fmt||!data)throw Error('Incomplete WAV');if(fmt.n<16)throw Error('Invalid WAV format');const o=fmt.at,ch=v.getUint16(o+2,true),rate=v.getUint32(o+4,true),bits=v.getUint16(o+14,true);if(v.getUint16(o,true)!==1||![16,18].includes(fmt.n)||(fmt.n===18&&v.getUint16(o+16,true)!==0)||bits!==16||rate!==48000||![1,2].includes(ch))return null;if(v.getUint16(o+12,true)!==ch*2||v.getUint32(o+8,true)!==rate*ch*2||!data.n||data.n%(ch*2))throw Error('Invalid WAV audio alignment');const frames=data.n/(ch*2);if(frames>LIMITS.frames)throw Object.assign(Error('Choose a clip up to 8 minutes.'),{code:'CLIP_REQUIRED',duration:frames/rate});return {frames,channels:ch,rate,bytes:b,dataOffset:data.at,dataBytes:data.n};}
export function encode(channels,rate=48000){
 if(rate!==48000||![1,2].includes(channels.length))throw Error('Unsupported output');
 const frames=channels[0].length;
 if(!frames||frames>LIMITS.frames||channels.some(c=>c.length!==frames))throw Error('Audio length exceeds the preparation limit');
 // Decoding/resampling can exceed unity even for ordinary integer source files.
 // One shared attenuation preserves dynamics and stereo balance; never boost.
 let peak=0,sourceOver=0;
 for(const channel of channels)for(let i=0;i<frames;i++){
  const x=channel[i];if(!Number.isFinite(x))throw Error('Audio contains non-finite samples');
  const a=Math.abs(x);if(a>peak)peak=a;if(a>1)sourceOver++;
 }
 const gain=peak>1?1/peak:1;
 const out=new Uint8Array(44+frames*channels.length*2),v=new DataView(out.buffer),put=(o,s)=>out.set(new TextEncoder().encode(s),o);
 put(0,'RIFF');v.setUint32(4,out.length-8,true);put(8,'WAVEfmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,channels.length,true);v.setUint32(24,rate,true);v.setUint32(28,rate*channels.length*2,true);v.setUint16(32,channels.length*2,true);v.setUint16(34,16,true);put(36,'data');v.setUint32(40,out.length-44,true);
 for(let i=0;i<frames;i++)for(let c=0;c<channels.length;c++){
  const z=Math.max(-1,Math.min(1,channels[c][i]*gain));
  v.setInt16(44+(i*channels.length+c)*2,Math.round(z*(z<0?32768:32767)),true);
 }
 return {bytes:out,frames,channels:channels.length,rate,clipped:0,sourceOver,sourcePeak:peak,gain,gainDb:20*Math.log10(gain)};
}

export function trimAudio(audio,start=0,end=audio.frames/48000){const a=Math.round(start*48000),b=Math.round(end*48000);if(!Number.isFinite(start)||!Number.isFinite(end)||a<0||b<=a||b>audio.frames)throw Error('Choose trim boundaries inside the prepared clip');const x=compatible(audio.bytes),frames=b-a,channels=x.channels,out=new Uint8Array(44+frames*channels*2),v=new DataView(out.buffer),put=(o,s)=>out.set(new TextEncoder().encode(s),o);put(0,'RIFF');v.setUint32(4,out.length-8,true);put(8,'WAVEfmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,channels,true);v.setUint32(24,48000,true);v.setUint32(28,48000*channels*2,true);v.setUint16(32,channels*2,true);v.setUint16(34,16,true);put(36,'data');v.setUint32(40,frames*channels*2,true);out.set(x.bytes.subarray(x.dataOffset+a*channels*2,x.dataOffset+b*channels*2),44);return {...audio,bytes:out,frames,trim:{startFrame:a,endFrame:b}}}
