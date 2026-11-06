// Browser ring adapter; codec kernels and bitstream are unchanged firmware C.
export class TakeCodec{
 constructor(module){this.e=new WebAssembly.Instance(module).exports;this.input=new Int16Array(this.e.memory.buffer,this.e.codec_input(),512);this.output=new Int16Array(this.e.memory.buffer,this.e.codec_output(),992);}
 convert(channels,mono){const count=channels[0].length,spb=mono?496:280,result=[new Float32Array(count),new Float32Array(count)];this.e.codec_reset();
  for(let base=0;base<count;base+=spb){this.input.fill(0);for(let n=0;n<spb/2;n++)for(let c=0;c<2;c++){const a=Math.max(-32768,Math.min(32767,Math.round((channels[c][base+2*n]||0)*32768))),b=Math.max(-32768,Math.min(32767,Math.round((channels[c][base+2*n+1]||0)*32768)));this.input[2*n+c]=(a+b+1)>>1;}
   this.e.codec_run(mono?1:0);for(let n=0;n<spb&&base+n<count;n++)for(let c=0;c<2;c++)result[c][base+n]=this.output[n*2+c]/32768;
   // Sequential decoder heals the preceding block's final interpolated frame.
   if(base>0)for(let c=0;c<2;c++){const a=Math.round(result[c][base-2]*32768),b=this.output[c];result[c][base-1]=((a+b+(mono?1:0))>>1)/32768;}
  }return result;
 }
}
