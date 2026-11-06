// Executable teaching scenarios. Prerequisites are set once; controls then drive
// the same adapter used by direct interaction. No effects inferred from LED labels.
import {Simulator} from './simulator.js?v=620';
export function stepSeconds(card,index){
 const step=card.steps[index];
 // The preset demo separates the armed LED from the grid punch-in.
 if(card.id==='preset'&&(index===3||index===4))return step.seconds??step.duration*2;
 return Math.max(2.5,step.seconds??step.duration*2);
}
const secondary={'secondaries':[2,4],'wah-resonance':[2,3],'echo-feedback':[2,4],'tremolo-shape':[3,3],'tremolo-spread':[3,3],'reverb-damping':[3,4]};
export function prepareScenario(card){
 const s=new Simulator(),first=card.steps[0],c=first.context||{};s.playing=c.playing??true;s.current.grid=!!c.grid;s.input=false;
 const loaded=c.loaded??[1];for(const n of loaded)s.tracks[n-1].state='playing';
 s.faders=[...(card.initialFaders||[.8,.8,.8,.8])];s.tracks.forEach((t,i)=>t.level=s.faders[i]);s.fixture=loaded.length>1?'split':'mix';
 s.page=first.hold?.includes('FN')&&first.hold.some(k=>/^T[1-4]$/.test(k))?0:first.page||0;s.heads=!!c.heads;
 if(card.secondaryTarget||secondary[card.id]){const [p,l]=card.secondaryTarget||secondary[card.id];s.fx[p-1][l-1]=.6;s.secondary[p-1][l-1]=.5;s.spread=.5;}
 if(card.id==='connect'){s.playing=false;s.tracks.forEach(t=>t.state='empty');}
 if(card.id==='power'){s.powered=false;s.playing=false;s.tracks.forEach(t=>t.state='empty');}
 if(card.id==='first-loop'){s.playing=false;s.input=false;s.faders[0]=0;s.tracks[0].level=0;}
 if(card.id==='replace')s.input=true;
 const modes={'wind-v4':1,'bender-v4':2,'battery-v4':3,'sandpaper-v4':4,'magnet-v4':5,'scissors386':5,'manual-head-v4':7};
 if(card.id in modes){s.rockerMode=modes[card.id];s.rockerOpen=card.id!=='scissors386';}
 if(card.id==='takes-v4')s.current.tracks[0].takes=[{...s.tracks[0],takes:[]}];
 if(['track-slide-v4','track-trim-reset-v4'].includes(card.id)){const spb=280,full=Math.ceil(s.tracks[0].length*48000/spb),kept=Math.floor(full/2);s.tracks[0].trim={head:0,tail:full-kept,length:kept*spb/48000};if(card.id==='track-trim-reset-v4')s.tracks[1].trim={...s.tracks[0].trim};}
 if(card.id==='escape-hatch-v4'){s.speed=.5;s.window.size=.25;s.tracks[0].muted=true;s.fx[1][3]=.5;}
 if(card.id==='normal-speed')s.speed=.5;
 if(card.id==='move-window'||card.id==='reset-window'||card.id==='repeat-chop')s.window={start:0,end:1,position:.25,size:.25};
 if(card.id==='reset-window')s.window.reverse=true;
 if(card.id==='head-source')s.heads=true;
 if(card.id==='grid'){s.current.grid=false;s.playing=false;}
 if(['clear-grid','round-grid'].includes(card.id)){s.current.grid=true;s.current.bpm=119.8;}
 if(card.id==='pickup'){s.faders[0]=.9;}
 if(card.id==='tap-rate'){s.fx[1][3]=.6;}
 if(card.id==='reset-fx'){s.fx[1][3]=.55;s.fx[4][1]=.8;s.route=2;}
 if(card.id==='monitor'){s.tracks.forEach(t=>t.state='empty');s.input=true;}
 if(card.id==='routing'){s.fx[1][3]=.65;}
 if(card.id==='center-placement'){s.tracks.forEach((t,i)=>{t.placement=i%2?.9:.1;s.fx[5][i]=t.placement;});}
 if(card.id==='preset'){s.page=7;s.current.preset=1;s.current.grid=true;s.input=true;}
 if(card.id==='stop-page')s.page=2;
 if(card.id==='countdown'){s.page=7;s.current.preset=3;s.current.grid=true;s.input=true;s.startTake(1);s.tracks[1].state='recording';}
 if(['nudge','nudge-lights','reset-timing'].includes(card.id)){s.tracks[1].state='playing';s.fixture='split';if(card.id!=='nudge')s.tracks[0].nudge=.75;}
 if(card.id==='reset-timing'){s.current.offset=.25;s.current.preset=3;s.tracks[1].nudge=.2;}
 if(card.id==='format')s.tracks.forEach(t=>t.mono=true);
 if(card.id==='repeat-chop')s.window.size=1/16;
 if(['release-repeat','move-repeat','repeat-octave','repeat-faders','flow-back'].includes(card.id)){s.repeat={size:.125,position:0,at:0};s.keys.set('PLAY',{at:0,used:true});}
 if(['stop-page','flow-back','print-speed','held-print'].includes(card.id)){s.startTake(1,card.id==='held-print'?'sampler':'copy');if(card.id==='held-print')s.keys.set('T2',{at:0,used:true});}
 if(card.id==='status-lights'){s.slot=1;s.tracks[0].state='playing';}
 if(card.id.startsWith('fx-')){const [,p,l]=card.id.split('-').map(Number);s.faders[l-1]=s.fx[p-1][l-1];if(p+':'+l in s.cycles)s.cycles[p+':'+l]=0;}
 if(['nudge','nudge-lights'].includes(card.id))s.faders[0]=.5;
 if(['head-source','nudge','reset-timing','nudge-lights'].includes(card.id))s.fixture='duet';
 s.rearm();return s;
}
// Each list represents physical press/release order; timing is in seconds within
// the teaching step, independent of display frame rate.
export function stepEvents(card,index){const s=card.steps[index],events=[],tap=(k,t=.55,len=.12)=>{events.push([t,'button',k,true],[t+len,'button',k,false]);};
 if(card.id==='bluetooth-v4'){
  if(index===0){tap('FN',.2,.08);tap('FN',.45,.08);tap('FN',.7,1.2);}
  if(index===1)events.push([2.5,'bluetooth-connected']);
  if(index===2)events.push([1,'input-start']);
 }
 for(const k of s.tap||[])tap(k);
 if(card.id==='takes-v4'&&index===1)tap('T1',2.8,.1);
 if(s.confirmationReplay)events.push([.7,'confirmation',s]);
 const p=s.pattern;
 if(p==='rockTap')tap(s.rockDirection||'UP',.55,.12);
 if(p==='rockDouble'){tap('UP',.5);tap('UP',.75);}
 if(p==='playDouble'||p==='playTriple')[.5,.75,...(p==='playTriple'?[1]:[])].forEach(t=>tap('PLAY',t,.08));
 if(p==='gridGesture')[.3,.8,1.3,1.8].forEach(t=>tap('FN',t,.10));
 if(p==='volDouble')[.5,.75].forEach(t=>tap('VOL+',t,.08));
 if(p==='rateT4')[.3,.8,1.3,1.8].forEach(t=>tap('T4',t,.1));
 if(p==='deleteGesture'){tap('T1',.25);tap('T1',.65,.45);}
 if(p==='clearGrid'){tap('FN',.2);tap('FN',.55,1.05);}
 if(p==='reverseGesture'){tap('FN',.4,.35);tap('T1',.4,.35);tap('PLAY',.54,.1);}
 // Rocker examples marked UP/DOWN are clicks; a speed sweep is a real hold.
 if(s.rock){const key=s.rock<0?'UP':'DOWN';tap(key,card.id==='scissors386'?.95:.35,card.id!=='scissors386'&&/hold|sweep/i.test(s.label)?1.25:.12);}
 return events.sort((a,b)=>a[0]-b[0]);
}
export class Scenario {
 constructor(card){this.card=card;this.sim=prepareScenario(card);this.index=-1;this.elapsed=0;}
 begin(index){
  if(index!==this.index+1){this.sim=prepareScenario(this.card);this.index=-1;for(let i=0;i<index;i++){this.begin(i);this.advance(stepSeconds(this.card,i));}}
  else if(this.index>=0)this.advance(stepSeconds(this.card,this.index));
  const s=this.sim,step=this.card.steps[index];this.index=index;this.elapsed=0;this.start=s.now;this.cursor=0;this.u=undefined;this.events=stepEvents(this.card,index);this.from=[...s.faders];
  this.fixture(index);
  const held=new Set(step.hold||[]);
  // Release the most recently pressed first: PLAY before FN, and track before modifiers.
  for(const k of [...s.keys.keys()].reverse())if(!held.has(k))s.button(k,false);
  for(const k of held)if(!s.has(k))s.button(k,true);
 }
 fixture(i){const s=this.sim,id=this.card.id;
  if(id==='first-loop')s.input=i>=2;
  if(id==='track-trim-reset-v4'&&i===1){s.page=7;s.rearm();}
  if(id==='power'&&i===4)s.charging=true;
  if(id==='reset-window'&&i===3)s.window={start:0,end:1,position:.5,size:.25};
  if(id==='round-grid'&&i===1){s.tapTimes=[s.now-2001,s.now-1500,s.now-999,s.now-498];s.current.bpm=119.8;}
  // These cards compare distinct operating states; they are explicitly fixtures,
  // not gestures that secretly create recordings.
  if(id==='track-lights'){
   s.fixtureLengths=true;
   s.cancel();s.recording=null;s.tracks.forEach(t=>{t.state='empty';t.muted=false;});s.input=false;s.current.grid=i===5;s.playing=i!==6;
   if(i===1){s.tracks[0].state='playing';s.tracks[0].muted=true;}
   if(i===2||i===3){s.startTake(0);s.tracks[0].state=i===2?'armed':'recording';s.audioInputPeak=0;}
   if(i>=4)s.tracks.forEach((t,n)=>{t.state='playing';t.length=i===4?[2,3,4,6][n]:4;});
  }
  if(id==='grid-record-lights'){s.current.grid=true;s.clock=i===0?.05:.1;s.startTake(1);s.tracks[1].state=i===0?'armed':'recording';s.recording.stopAt=i===1?.5:undefined;s.recording.armedAt=s.clock;s.input=false;}
 }
 advance(target){target=Math.max(this.elapsed,target);const s=this.sim,step=this.card.steps[this.index];
  while(this.elapsed+1e-7<target){this.elapsed=Math.min(target,this.elapsed+.008);s.tick(this.start+this.elapsed*1000);
   while(this.cursor<this.events.length&&this.events[this.cursor][0]<=this.elapsed+1e-7){const [,method,...args]=this.events[this.cursor++];if(method==='bluetooth-connected'){s.bluetooth={state:3,at:s.now};}else if(method==='input-start'){s.input=true;}else if(method==='confirmation'){const v=args[0];s.flash={...v,pattern:v.pattern,at:s.now};}else s[method](...args);}
   const u=Math.max(0,Math.min(1,(this.elapsed-.35)/1.2));if(u!==this.u){for(const [n,v]of Object.entries(step.faders||{})){const k=+n-1;s.fader(k,this.from[k]+(v-this.from[k])*u);}this.u=u;}
  }
  return s;
 }
}
