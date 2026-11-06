// Byte-exact offline P14S-v1 encoder. Device capability gates transport use.
import {compatible} from './audio.mjs';
export function prepareNativePayload(bytes){
 const info=compatible(bytes);if(!info)throw Error('Prepare PCM16/48k mono or stereo WAV first');
 const input=new DataView(info.bytes.buffer,info.bytes.byteOffset,info.bytes.byteLength);
 const blocks=Math.ceil(info.frames/280),payload=new Uint8Array(blocks*512),stored=new Int16Array(280),q=new Uint32Array(4),error=[0,0];let shift=0;
 const sample=(frame,ch)=>frame>=info.frames?0:input.getInt16(info.dataOffset+(frame*info.channels+(ch<info.channels?ch:0))*2,true);
 for(let block=0;block<blocks;block++){
  let peak=0;const first=block*280;
  for(let i=0;i<140;i++)for(let c=0;c<2;c++){
   const value=Math.floor((sample(first+i*2,c)+sample(first+i*2+1,c)+1)/2);
   stored[i*2+c]=value;peak=Math.max(peak,Math.abs(value));
  }
  let next=0;while(next<3&&peak*2**(next+1)<32000)next++;
  if(next!==shift){error[0]=error[1]=0;shift=next;}
  const at=block*512;payload.set([80,49,52,83],at);payload[at+5]=1;payload[at+11]=0x5b;payload[at+12]=24;payload[at+13]=1;payload[at+14]=shift;
  for(let i=0;i<70;i++){
   for(let k=0;k<4;k++){
    const c=k&1,v=Math.max(-32768,Math.min(32767,stored[i*4+k]*2**shift-error[c]));
    const n=Math.max(-8192,Math.min(8191,Math.floor((v+2)/4)));
    error[c]=Math.max(-16,Math.min(16,n*4-v));q[k]=n&16383;
   }
   const a=((q[0]<<18)|(q[1]<<4)|(q[2]>>>10))>>>0,b=((q[2]<<14)|q[3])>>>0,p=at+16+i*7;
   payload[p]=a>>>24;payload[p+1]=a>>>16;payload[p+2]=a>>>8;payload[p+3]=a;
   payload[p+4]=b>>>16;payload[p+5]=b>>>8;payload[p+6]=b;
  }
 }
 return {frames:info.frames,blocks,payload};
}
