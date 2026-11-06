// Original release effect/routing kernels. Browser transport and storage adapters.
import {TakeCodec} from './audio-codec.js';
const wrap=(x,n)=>(x%n+n)%n,byte=x=>Math.max(0,Math.min(255,Math.round(x*255)));
class FirmwareRack {
 constructor(module){const e=new WebAssembly.Instance(module).exports;this.e=e;this.l=new Int32Array(e.memory.buffer,e.audio_left(),256);this.r=new Int32Array(e.memory.buffer,e.audio_right(),256);this.c=new Int32Array(e.memory.buffer,e.audio_config(),64);this.geometry=new Uint32Array(e.memory.buffer,e.audio_geometry(),4);this.input=new Int16Array(e.memory.buffer,e.audio_input(),512);}
 configure(s){const c=this.c;c.fill(0);c.set(s.fx.slice(0,5).flat().map(byte));c.set(s.secondary.flat().map(byte),20);c[36]=byte(s.spread);c[37]=s.cycles['1:3'];c[38]=s.cycles['1:4'];c[39]=s.cycles['2:4'];for(let i=0;i<3;i++)c[40+i]=s.cycles['3:'+(i+1)];['1:4','2:4','3:1','3:2','3:3','1:2','4:4'].forEach((id,i)=>c[43+i]=s.rates[id]?Math.round(2880000/s.rates[id]):0);c[50]=Math.round(2880000/s.bpm);c[51]=s.grid?1:0;}
 run(l,r,mask){for(let i=0;i<256;i++){this.l[i]=Math.round(l[i]*32768);this.r[i]=Math.round(r[i]*32768);}this.e.audio_run(mask);for(let i=0;i<256;i++){l[i]=this.l[i]/32768;r[i]=this.r[i]/32768;}}
}
export class TapeAudio {
 constructor(sr,stems,module,codecModule){this.codec=codecModule?new TakeCodec(codecModule):null;if(sr!==48000)throw Error('Firmware DSP requires 48 kHz');this.sr=sr;this.stems=stems;this.pos=0;this.inputPos=0;this.live=[new Float32Array(2048),new Float32Array(2048)];this.liveRead=0;this.liveWrite=0;this.banks=new Map();this.rack=new FirmwareRack(module);this.pan=Array.from({length:4},()=>[0,0]);this.speedQ16=0;this.env=256;this.capturePhase=0;this.inputPeak=0;this.ramps=[0,0,0,0];this.gain=0;this.lastRecord=null;this.peak=0;this.vu=0;this.vuHold=0;this.epoch=-1;this.offset=256;this.buffers=Array.from({length:4},()=>new Float32Array(256));}
 prepareExample(sim,reset=false){
  if(reset){this.banks.delete('example');this.lastRecord=null;this.offset=256;}
  if(!this.banks.has('example'))this.banks.set('example',Array(4).fill(null));
  const bank=this.banks.get('example');
  for(let lane=0;lane<4;lane++){
   const tr=sim.tracks[lane];if(tr.state!=='playing'||bank[lane])continue;
   const count=sim.fixtureLengths?Math.round(tr.length*this.sr):this.source?.[0].length||Math.round(tr.length*this.sr),channels=[new Float32Array(count),new Float32Array(count)];
   for(let i=0;i<count;i++){
    if(this.source){const gate=sim.fixture==='split'?Math.floor(i/count*4)===lane:sim.fixture==='duet'?Math.floor(i/count*2)===lane%2:true;for(let c=0;c<2;c++)channels[c][i]=gate?this.source[c][i%this.source[c].length]:0;}
    else {const beat=Math.floor((i%this.stems[0].length)/(this.sr*.5));let v=0;if(sim.fixture==='duet')v=this.stems[lane%2][i%this.stems[0].length]*.5;else if(sim.fixture==='split')v=((lane<2)===(beat<4))?this.stems[lane%2][i%this.stems[0].length]*.5:0;else for(const stem of this.stems)v+=stem[i%stem.length]*.5;channels[0][i]=channels[1][i]=v;}
   }
   bank[lane]=this.codec?this.codec.convert(channels,false):channels;bank[lane].start=0;bank[lane].gain=1;tr.length=bank[lane][0].length/this.sr;
  }
 }
 checkpoint(){return {banks:new Map([...this.banks].map(([k,v])=>[k,v.slice()])),lastRecord:this.lastRecord?{...this.lastRecord,channels:this.lastRecord.channels.map(c=>c.map((part,i)=>i===c.length-1?part.slice():part))}:null,pos:this.pos,inputPos:this.inputPos,capturePhase:this.capturePhase,speedQ16:this.speedQ16,gain:this.gain,env:this.env,ramps:[...this.ramps],pan:this.pan.map(p=>p.slice()),dsp:new Uint8Array(this.rack.e.memory.buffer).slice()};}
 rollback(saved){const {dsp,...state}=saved;Object.assign(this,state);new Uint8Array(this.rack.e.memory.buffer).set(dsp);this.offset=256;}
 setSource(source){this.microphone=source==='microphone';this.source=this.microphone?null:source;this.inputPos=0;this.liveRead=this.liveWrite=0;}
 setState(s){if(this.epoch!==s.epoch){this.pos=s.clock*this.sr;this.epoch=s.epoch;}if(this.s&&Math.abs(s.clock-this.s.clock)>.25&&Math.abs(s.clock-this.pos/this.sr)>.3)this.pos=s.clock*this.sr;this.s=s;this.rack.configure(s);
 if(!this.banks.has(s.example?"example":s.slot))this.banks.set(s.example?"example":s.slot,Array(4).fill(null));const bank=this.banks.get(s.example?"example":s.slot);for(let i=0;i<4;i++)if(s.tracks[i].state==='empty')bank[i]=null;
 const rec=s.recording&&s.tracks[s.recording.i].state==='recording'?s.recording:null,id=rec?`${s.slot}:${rec.i}:${rec.at}`:null;
 if(this.lastRecord&&this.lastRecord.id!==id){const r=this.lastRecord;if((s.example?'example':s.slot)!==r.slot||s.tracks[r.i]?.state!=='empty')this.banks.get(r.slot)[r.i]=this.finishRecording(r);this.lastRecord=null;}
 if(rec&&this.lastRecord)this.lastRecord.kind=rec.kind;
 if(rec&&!this.lastRecord){this.capturePhase=0;this.lastRecord={id,slot:s.example?"example":s.slot,i:rec.i,kind:rec.kind,mono:s.tracks[rec.i].recordedMono,start:this.pos,reference:Math.max(.1875,this.speedQ16/65536),sumL:0,sumR:0,samples:0,length:0,channels:[[],[]]};}}
 finishRecording(r){let channels=r.channels.map(ch=>{const out=new Float32Array(Math.max(1,r.length));let offset=0;for(const part of ch){out.set(part.subarray(0,Math.min(part.length,r.length-offset)),offset);offset+=Math.min(part.length,r.length-offset);}return out;});if(r.kind==='copy'&&r.reference!==1){const length=Math.max(1,Math.round(r.length*r.reference));channels=channels.map(ch=>Float32Array.from({length},(_,i)=>this.sample(ch,i/r.reference)));}const result=this.codec?this.codec.convert(channels,r.mono):channels;result.start=r.start;result.mono=r.mono;result.gain=r.kind==='ordinary'?1:4;return result;}
 sample(a,p){const x=wrap(p,a.length),n=Math.floor(x),frac=Math.floor((x-n)*65536)>>1;const v=Math.round(a[n]*32768),next=Math.round(a[(n+1)%a.length]*32768);return (v+(((next-v)*frac)>>15))/32768;}
 capture(l,r){const rec=this.lastRecord;if(!rec||rec.length>=Math.floor(23045120/(rec.kind==='copy'?rec.reference:1)))return;const at=rec.length%48000;if(!at){rec.channels[0].push(new Float32Array(48000));rec.channels[1].push(new Float32Array(48000));}const part=rec.channels[0].length-1;rec.channels[0][part][at]=l;rec.channels[1][part][at]=r;rec.length++;}
 block(){const s=this.s,[l,r,il,ir]=this.buffers;for(const b of this.buffers)b.fill(0);if(!s)return;const bank=this.banks.get(s.example?'example':s.slot),rack=this.rack,active=s.powered&&s.playing;
 // Original engine speed slew: one update per 256 engine frames.
 const target=active?Math.round(s.speed*65536):0,sd=target-this.speedQ16;
 this.speedQ16=Math.abs(sd)<64?target:this.speedQ16+Math.trunc(sd/50);
 const speed=this.speedQ16/65536;const liveReady=this.microphone&&this.liveWrite-this.liveRead>=256;
 // An example owns playback: use its loaded tracks, or input when teaching capture.
 const listenInput=s.input&&(!s.example||s.fixture==='input'||s.recording?.kind==='ordinary'||!s.tracks.some(t=>t.state==='playing'));
 for(let n=0;n<256;n++){
  let a=0,b=0;if(active&&listenInput){if(this.microphone){if(liveReady){a=this.live[0][(this.liveRead+n)%2048];b=this.live[1][(this.liveRead+n)%2048];}}else if(this.source){a=this.sample(this.source[0],this.inputPos);b=this.sample(this.source[1],this.inputPos);}else{for(const stem of this.stems)a+=this.sample(stem,this.inputPos);a*=.5;b=a;}}
  if(active)this.inputPos++;if(this.inputMuted)a=b=0;rack.input[n*2]=Math.max(-32768,Math.min(32767,Math.round(a*32768)));rack.input[n*2+1]=Math.max(-32768,Math.min(32767,Math.round(b*32768)));
 }
 if(liveReady)this.liveRead+=256;
 rack.e.audio_input_run(s.route===3?0:s.route,this.lastRecord&&this.lastRecord.kind!=='ordinary'?1:0);
 for(let n=0;n<256;n++){this.inputPeak=Math.max(this.inputPeak,Math.abs(rack.input[n*2]),Math.abs(rack.input[n*2+1]));il[n]=rack.input[n*2]/32768;ir[n]=rack.input[n*2+1]/32768;}
 for(let i=0;i<4;i++){
  const tr=s.tracks[i],source=s.heads?s.headSource:i,audible=!tr.muted&&(tr.state==='playing'||s.heads&&s.tracks[source].state==='playing');
  const prev=this.ramps[i],vol=audible?Math.round(tr.level*256):0,delta=vol-prev;this.ramps[i]=vol;
  const pn=byte(tr.placement),pan=this.pan[i],tg=[pn>136?Math.min(255,((pn-136)*551)>>8):0,pn&&pn<120?Math.min(255,((120-pn)*551)>>8):0];for(let c=0;c<2;c++)pan[c]+=Math.max(-4,Math.min(4,tg[c]-pan[c]));
  const len=bank[source]?.[0].length||this.source?.[0].length||this.stems[0].length,w=s.window;
  const region=Math.max(.002,Math.abs(w.end-w.start)),size=Math.max(.001,region*w.size*(s.repeat?.size||1)),start=Math.min(w.start,w.end)+(w.position||0)*Math.max(0,1-region)+(s.repeat?.position||0)*region;
  const spb=bank[source]?.mono?496:280,gb=Math.max(1,Math.ceil(len/spb)),div=w.free?1:Math.max(1,Math.round(1/w.size));
  const center=(w.start+w.end)/2,half=Math.max(2/255,Math.abs(w.end-w.start)*(w.free?w.size:1))/2,ws=w.free?Math.max(0,center-half):w.start,we=w.free?Math.min(1,center+half):w.end;
  rack.e.audio_tile(gb,spb,Math.round((s.loopLength||len/this.sr)*this.sr),s.fixed?1:0,div,Math.floor((w.position||0)*div),w.free||w.start!==0||w.end!==1?1:0,byte(ws),byte(we));
  const [wper,win,wbase,cyc]=rack.geometry;
  const nq=byte(tr.nudge),beat=s.grid?2880000/s.bpm*speed:len/8,nudge=nq>=120&&nq<=136?0:Math.trunc((nq-128)*beat/256),anchor=bank[source]?.start||0;
  for(let n=0;n<256;n++){
   let p;
   if(!s.repeat&&div===1&&!w.free&&w.start===0&&w.end===1&&!tr.reverse&&!s.heads){p=wrap(this.pos+n*speed-anchor-nudge,len);}
   else {const absolute=this.pos+n*speed+(s.heads?s.headPositions[i]*len:0)-nudge,block=Math.floor(absolute/spb),fraction=wrap(absolute,spb);let c=wrap(block-wrap(Math.floor(anchor/spb),gb),cyc);
    if(s.repeat){const q=s.repeat,span=q.free?Math.abs(q.end-q.start)*q.size:q.size,base=q.free?Math.min(q.start,q.end)+(Math.abs(q.end-q.start)-span)/2:q.position||0,width=Math.max(1,Math.floor(cyc*Math.min(.999,span))),offset=Math.floor(base*cyc);c=wrap(offset+wrap(c-offset,width),cyc);}
    if(tr.reverse!==!!w.reverse)c=cyc-1-c;const storage=Math.floor(c/win)*wper+wbase+c%win;p=storage*spb+(tr.reverse!==!!w.reverse?spb-1-fraction:fraction);}
   let a=0,b=0;
   if(active&&bank[source]){a=this.sample(bank[source][0],p);b=this.sample(bank[source][1],p);}else if(active&&s.example){if(this.source){a=this.sample(this.source[0],p);b=this.sample(this.source[1],p);}else {const beat=Math.floor(wrap(p,this.stems[0].length)/(this.sr*.5));if(s.fixture==='duet'){a=b=this.sample(this.stems[source%2],p)*.5;}else if(s.fixture==='split'){const lane=source%2,half=source<2?beat<4:beat>=4;a=b=half?this.sample(this.stems[lane],p)*.5:0;}else {for(const stem of this.stems)a+=this.sample(stem,p)*.5;b=a;}}}
   const vf=Math.abs(delta)>1?prev+((delta*(n+1))>>8):vol,gL=(vf*(256-pan[0]))>>8,gR=(vf*(256-pan[1]))>>8;
   l[n]+=((Math.round(a*32768)*gL)>>8)/32768*(bank[source]?.gain||1);r[n]+=((Math.round(b*32768)*gR)>>8)/32768*(bank[source]?.gain||1);
  }
 }
 this.pos+=256*speed;
 for(let n=0;n<256;n++){rack.l[n]=Math.round((l[n]+il[n])*32768);rack.r[n]=Math.round((r[n]+ir[n])*32768);}
 rack.e.audio_mix_run(s.monitorMuted?1:0);
 const rec=this.lastRecord;
 for(let n=0;n<256;n++){
  if(rec&&active){const a=rec.kind==='ordinary'?Math.round(il[n]*32768):rack.l[n]>>2,b=rec.kind==='ordinary'?Math.round(ir[n]*32768):rack.r[n]>>2;rec.sumL+=a;rec.sumR+=b;rec.samples++;this.capturePhase+=rec.kind==='ordinary'?speed:1;while(this.capturePhase>=1){this.capturePhase--;this.capture((rec.samples?Math.trunc(rec.sumL/rec.samples):a)/32768,(rec.samples?Math.trunc(rec.sumR/rec.samples):b)/32768);rec.samples=0;rec.sumL=rec.sumR=0;}}
 }
 // Firmware master gain interpolation, power envelope, soft limiter and VU.
 this.env=s.powered?Math.min(256,this.env+48):Math.max(0,this.env-16);
 const ge=(Math.round(s.volume*256)*this.env)>>8,g0=this.gain,gd=ge-g0;this.gain=ge;let peak=0;
 for(let n=0;n<256;n++){const gain=gd?g0+((gd*(n+1))>>8):ge;const a=rack.e.audio_limit((rack.l[n]*gain)>>8),b=rack.e.audio_limit((rack.r[n]*gain)>>8);l[n]=a/32768;r[n]=b/32768;this.peak=Math.max(this.peak,Math.abs(l[n]),Math.abs(r[n]));if((n&3)===0)peak=Math.max(peak,Math.abs(a),Math.abs(b));}
 let level=0;if(peak>8){const b=31-Math.clz32(peak),hf=(peak>>(b-1))&1;level=Math.max(0,Math.min(12,b*2+hf-13));}if(level>=this.vu){this.vu=level;this.vuHold=0;}else if(++this.vuHold>=8){this.vuHold=0;if(this.vu)this.vu--;}
 }
 process(left,right,input){if(this.microphone&&input?.[0]){for(let i=0;i<input[0].length;i++){const at=this.liveWrite++%2048;this.live[0][at]=input[0][i];this.live[1][at]=(input[1]||input[0])[i];}if(this.liveWrite-this.liveRead>2048)this.liveRead=this.liveWrite-2048;}for(let n=0;n<left.length;n++){if(this.offset>=256){this.block();this.offset=0;}left[n]=this.buffers[0][this.offset];right[n]=this.buffers[1][this.offset++];}}
}
