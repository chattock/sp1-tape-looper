import {Scenario} from './scenarios.js?v=620';
import {makeDrums} from './drums.js';
import {TapeAudio} from './audio-dsp.js?v=610';
import {useFirmwareCore} from './firmware-core.js?v=610';
import {Simulator} from './simulator.js?v=gesture-audit-20261112';
const audioState=(s,epoch,example)=>({epoch,example,clock:s.clock,powered:s.powered,playing:s.playing,slot:s.slot,grid:s.current.grid,fixed:s.fixed,loopLength:s.tracks.find(t=>t.state==='playing')?.length,tracks:s.tracks,fx:s.fx,secondary:s.secondary,spread:s.spread,cycles:s.cycles,rates:s.rates,bpm:s.current.bpm,speed:s.speed,volume:s.volume,route:s.route,input:s.input,monitorMuted:s.monitorMuted,heads:s.heads,headSource:s.headSource,headPositions:s.headPositions,window:s.window,repeat:s.repeat,recording:s.recording,fixture:s.fixture});
class SP1Audio extends AudioWorkletProcessor{
 constructor(options){super();const o=this.options=options.processorOptions;useFirmwareCore(new WebAssembly.Instance(o.coreModule).exports);this.engine=new TapeAudio(sampleRate,makeDrums(sampleRate),o.module,o.codecModule);this.engine.setSource(o.defaultSource||null);this.engine.inputMuted=!!o.inputMuted;this.sim=new Simulator();Object.assign(this.sim,o.state);this.sim.audioInputPeak=0;this.frames=0;this.epoch=0;this.seq=0;this.example=false;this.checkpoints=new Map();this.port.onmessage=({data:m})=>{
  if(m.type==='scenario'){const reset=!this.scenario||this.scenario.card.id!==m.card.id||m.index!==this.scenario.index+1;if(!this.scenario||this.scenario.card.id!==m.card.id)this.scenario=new Scenario(m.card);this.scenario.begin(m.index);this.sim=this.scenario.sim;this.engine.prepareExample(this.sim,reset||m.card.id==='track-lights');this.example=true;this.seq=m.seq;this.epoch++;}
  if(m.type==='audio-paused')this.paused=m.value;
  if(m.type==='reset-example'){
   this.scenario=null;
   this.checkpoints.clear();const old=this.engine,rec=old.lastRecord;
   if(rec&&rec.slot!=='example')old.banks.get(rec.slot)[rec.i]=old.finishRecording(rec);
   this.engine=new TapeAudio(sampleRate,old.stems,this.options.module,this.options.codecModule);
   this.engine.banks=new Map([...old.banks].filter(([key])=>key!=='example'));
   this.engine.takeAudio=new Map([...old.takeAudio].filter(([key])=>!key.startsWith('example/')));
   this.engine.setSource(old.microphone?'microphone':old.source);this.engine.inputMuted=old.inputMuted;
  }
  if(m.type==='input-mute'){this.engine.inputMuted=m.muted;this.engine.inputPeak=0;this.sim.audioInputPeak=0;}
  if(m.type==='source')this.engine.setSource(m.source??this.options.defaultSource??null);
  if(m.type==='command'){this.seq=m.seq;this.sim[m.method](...m.args);}
  if(m.type==='replace'){this.scenario=null;this.seq=m.seq;Object.assign(this.sim,m.state);this.example=!!m.example;this.epoch++;this.engine.setState(audioState(this.sim,this.epoch,this.example));}
  if(m.type==='checkpoint')this.checkpoints.set(m.id,this.engine.checkpoint());
  if(m.type==='discard')this.checkpoints.delete(m.id);
  if(m.type==='rollback'){this.seq=m.seq;const saved=this.checkpoints.get(m.id);if(saved)this.engine.rollback(saved);Object.assign(this.sim,m.state);this.example=!!m.example;this.checkpoints.delete(m.id);}
  if(m.type==='draft')this.drafting=m.value;
 };}
 process(inputs,outputs){const out=outputs[0];if(out.length<2)return true;if(this.drafting||this.paused){out[0].fill(0);out[1].fill(0);return true;}
  // Controls and audio share the worklet clock; display frames never advance transport.
  if(this.scenario)this.scenario.advance(this.scenario.elapsed+out[0].length/sampleRate);else if(!this.drafting)this.sim.tick(this.sim.now+out[0].length*1000/sampleRate);
  this.engine.setState(audioState(this.sim,this.epoch,this.example));this.engine.process(out[0],out[1],inputs[0]);
  this.sim.audioVu=this.engine.vu;this.sim.audioInputPeak=this.engine.inputPeak;
  const rec=this.engine.lastRecord;this.sim.audioRecordedFrames=rec?Math.round(rec.length*(rec.kind==='copy'?rec.reference:1)):undefined;
  if(rec&&this.sim.audioRecordedFrames>=23045120&&this.sim.recording)this.sim.finishTake(true);
  this.frames+=out[0].length;if(this.frames>=sampleRate/50){this.port.postMessage({seq:this.seq,state:{...this.sim},outputPeak:this.engine.peak});this.engine.peak=0;this.engine.inputPeak=0;this.frames=0;}return true;
 }
}
registerProcessor('sp1-audio',SP1Audio);
