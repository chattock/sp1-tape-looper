import {compatible} from './audio.mjs';
// Lossless only: differing channels, even by one PCM bit, remain stereo.
// Applies to prepared imports, never changes stored takes or their frame count.
export function compactExactStereo(audio){
 const began=performance.now(),info=compatible(audio.bytes);if(!info)throw Error('Prepare PCM16/48k WAV before compacting');
 if(info.channels!==2||info.dataBytes<1048576)return audio;
 const b=info.bytes,first=info.dataOffset,end=first+info.dataBytes;
 for(let at=first;at<end;at+=4)if(b[at]!==b[at+2]||b[at+1]!==b[at+3])return audio;
 const out=new Uint8Array(44+info.frames*2),v=new DataView(out.buffer),put=(at,s)=>out.set(new TextEncoder().encode(s),at);
 put(0,'RIFF');v.setUint32(4,out.length-8,true);put(8,'WAVEfmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);v.setUint32(24,48000,true);v.setUint32(28,96000,true);v.setUint16(32,2,true);v.setUint16(34,16,true);put(36,'data');v.setUint32(40,out.length-44,true);
 for(let f=0;f<info.frames;f++){out[44+f*2]=b[first+f*4];out[45+f*2]=b[first+f*4+1];}
 return {...audio,bytes:out,frames:info.frames,rate:48000,channels:1,preparationMs:(audio.preparationMs||0)+performance.now()-began,losslessChannelCompaction:{from:2,to:1,sourceBytes:b.length,preparedBytes:out.length}};
}
