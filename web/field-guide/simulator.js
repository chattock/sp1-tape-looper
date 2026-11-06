import {FX,FX_CYCLES,FX_DEFAULT_CYCLES} from '../fx-state.mjs?v=610';
// Source-derived control adapter for v4 guide scenes; not a hardware emulator.
import {ledPattern,pageLights,musicVu} from './led-patterns.js?v=610';
import {firmwareSemitone,firmwareBreath,firmwarePreview,firmwareBluetooth,firmwareAnimation} from './firmware-core.js?v=610';
const rateLanes=new Set(['1:2','1:4','2:4','3:1','3:2','3:3','4:4']);
const clamp=(n,a=0,b=1)=>Math.max(a,Math.min(b,n));
const freshTrack=()=>({state:'empty',muted:false,length:4,level:1,reverse:false,mono:false,placement:.5,nudge:.5,takes:[],selected:-1});
const secondaryDefaults=()=>[[128,128,128,128],[128,128,128,166],[128,128,128,179],[128,0,128,128]].map(row=>row.map(v=>v/255));
export function snapBeatFrames(bf){if(!bf)return bf;const bpm100=Math.floor((288000000+Math.floor(bf/2))/bf),whole=Math.floor((bpm100+50)/100);if(whole<50||whole>200||Math.abs(bpm100-whole*100)>40)return bf;return Math.floor((2880000+Math.floor(whole/2))/whole)||bf;}
const song=()=>({tracks:Array.from({length:4},freshTrack),grid:false,bpm:120,offset:0,preset:0});
export class Simulator{
 constructor(){this.reset();}
 reset(){this.now=0;this.clock=0;this.powered=true;this.playing=false;this.page=0;this.pageAt=0;this.slot=0;this.songs=Array.from({length:16},song);this.keys=new Map();this.faders=[1,1,1,1];this.pickup=[null,null,null,null];this.fx=Array.from({length:6},(_,p)=>Array.from({length:4},(_,i)=>p===4||p===5||p===3&&i===1||p===0&&i===0?.5:0));this.secondary=secondaryDefaults();this.spread=0;this.cycles={...FX_DEFAULT_CYCLES};this.takeSequence=0;this.windLatch=3;this.windLastTap=null;this.speed=1;this.octaveFrom=0;this.volume=45/256;this.volumeIndex=10;this.fixed=false;this.route=3;this.monitorMuted=false;this.bright=false;this.input=false;this.charging=false;this.heads=false;this.headSource=0;this.headPositions=[0,.25,.5,.75];this.window={start:0,end:1,position:0,size:1};this.repeat=null;this.recording=null;this.tapTimes=[];this.playTaps=[];this.rateTaps={};this.rates={};this.lastTaps={};this.successUntil=0;this.flash=null;this.powerGesture=null;this.lastMessage='Ready';this.isolate=null;this.recordReady=null;this.rockerOpen=false;this.rockerMode=0;this.cutMark=null;this.cuts=[];this.btTaps=[];this.btHold=null;this.bluetooth=null;this.rockerAt=0;this.manualContact=false;this.wear=0;}
 get current(){return this.songs[this.slot];}get tracks(){return this.current.tracks;}
 has(key){return this.keys.has(key);}say(text){this.lastMessage=text;}
 consume(...keys){for(const key of keys){const k=this.keys.get(key);if(k)k.used=true;}}
 confirm(){this.successUntil=this.now+480;}
 openPage(page,walk=false){this.page=((page-1+8)%8)+1;this.pageAt=this.now;this.rearm();this.flash={pattern:'page-entry',at:this.now,page:this.page,walk};this.consume('FN',...['T1','T2','T3','T4']);this.say('Page '+this.page);}
 controlValue(i){return this.page===4&&i===1&&this.cycles['4:2']===1?this.secondary[3][1]:this.fx[this.page-1][i];}
 rearm(){this.pickup=this.faders.map((_,i)=>this.page<=6&&this.page?this.controlValue(i):this.page===8?null:this.page===7?(i===0?.5+this.current.offset:i===1?this.current.preset/4:null):this.tracks[i].level);}
 startTake(i,kind='ordinary'){
  if(this.recording!==null)return;
  if(kind==='ordinary'&&this.monitorMuted){this.flash={pattern:'no-action',at:this.now};this.say('Unmute input before recording');return;}
  this.resetCuts();const tr=this.tracks[i];if(tr.state!=='empty'&&tr.selected<0){tr.selected=tr.takes.length;tr.takes.push({...tr,takes:[]});}this.recordBackup={...tr,takes:tr.takes.map(t=>({...t}))};tr.state=kind==='ordinary'?'armed':'recording';tr.muted=false;
  tr.audioId=++this.takeSequence;this.audioRecordedFrames=undefined;tr.recordedMono=tr.mono;this.recording={i,kind,at:this.now,clock:this.clock,armedAt:this.clock};this.playing=true;
  this.say(kind==='ordinary'?'Track '+(i+1)+' armed':kind==='sampler'?'Sampler print · keep destination held':'Tape copy recording');
 }
 finishTake(force=false){if(!this.recording)return;if(!force&&this.current.grid&&this.recording.kind==='ordinary'&&this.tracks[this.recording.i].state==='recording'){this.recording.stopAt=(Math.floor(this.clock/(60/this.current.bpm))+1)*(60/this.current.bpm);this.say('Finishing the beat');return;}const {i,at}=this.recording;if(this.tracks[i].state==='armed'){this.tracks[i]=this.recordBackup||freshTrack();this.recording=null;this.say('Arm cancelled');return;}this.tracks[i].state='playing';this.tracks[i].length=Math.max(1/48000,this.audioRecordedFrames===undefined?(this.now-at)/1000:this.audioRecordedFrames/48000);const tr=this.tracks[i];tr.selected=tr.takes.length;tr.takes.push({...tr,takes:[]});this.recording=null;this.recordBackup=null;this.say('Take saved');}
 button(key,down){
  if(down){if(this.has(key))return;this.keys.set(key,{at:this.now,used:false,moved:false});
   if(!this.powered)return;
   if(key==='FN'&&this.recordReady){this.recordReady=null;this.say('Overdub preparation cancelled');}
   if(key==='FN'&&!this.recording&&!this.page&&!this.rockerOpen&&this.keys.size===1&&this.btTaps.length===2&&this.now-this.btTaps[1]<=350){this.btHold={at:this.now,action:0};this.tapTimes=[];}
   const m=/^T([1-4])$/.exec(key),i=m?+m[1]-1:-1;
   const trackKeys=[...this.keys.keys()].filter(k=>/^T[1-4]$/.test(k));
   if(this.has('FN')&&this.has('PLAY')&&trackKeys.length){
    if(trackKeys.length>1){this.endIsolate();this.consume('FN','PLAY',...trackKeys);this.say('PLAY supports one track button at a time');return;}
    const trackKey=trackKeys[0],n=+trackKey[1]-1;
    if(this.recording?.i===n&&this.recording.kind!=='ordinary'){this.consume('FN','PLAY',trackKey);if(key==='UP'||key==='DOWN')this.rock(key==='UP'?1:-1);return;}
    if(this.page){delete this.rates[this.page+':'+(n+1)];this.say('Tapped rate cleared');}
    else if(this.tracks[n].state==='empty'&&!this.heads)this.say('No track here');
    else if(key==='PLAY'){this.tracks[n].reverse=!this.tracks[n].reverse;this.say('Track '+(n+1)+(this.tracks[n].reverse?' reversed':' forward'));}
    else {this.endIsolate();this.isolate=this.tracks.map(t=>t.muted);this.isolateKey=trackKey;this.tracks.forEach((t,j)=>{if(j===n)t.muted=false;else if(t.state==='playing'||this.heads)t.muted=true;});this.say('Isolating track '+(n+1));}
    this.consume('FN','PLAY',trackKey);return;
   }
   if(i>=0&&this.has('FN')&&this.has('T1')&&this.has('T4')){
    for(let p=0;p<5;p++)this.fx[p]=Array.from({length:4},(_,n)=>p===4||p===3&&n===1||p===0&&n===0?.5:0);
    this.secondary=secondaryDefaults();this.spread=0;for(const id in this.cycles)this.cycles[id]=id==='3:3'?2:0;this.rates={};this.route=3;
    if(this.page===6){this.tracks.forEach(t=>t.placement=.5);this.fx[5].fill(.5);}
    this.confirm();this.say('Effects reset');this.rearm();this.consume('FN','T1','T4');return;
   }
   if(i>=0&&this.has('PLAY')&&!this.has('FN')){
    if(this.recording?.i===i)this.finishTake();else if(!this.page&&this.playing)this.startTake(i,'copy');
    this.consume('PLAY',key);return;
   }
   if(i>=0&&this.rockerOpen&&!this.has('FN')&&!this.has('PLAY')){this.consume(key);return;}
   if(i>=0&&!this.page&&!this.has('FN')&&!this.has('PLAY')&&this.recording?.i===i){this.finishTake();this.consume(key);return;}
   if(key.startsWith('VOL'))this.volumeAction(key);
   if(key==='UP'||key==='DOWN')this.rock(key==='UP'?1:-1);
   return;
  }
  const press=this.keys.get(key);if(!press)return;const held=this.now-press.at;
  this.keys.delete(key);
  if((key==='UP'||key==='DOWN')&&this.rockerMode===7){this.heads=false;this.manualContact=false;this.say('Tape contact released');}
  if((key==='UP'||key==='DOWN')&&this.rockerMode===1&&this.windBackup){this.speed=this.windBackup.speed;this.tracks.forEach((t,i)=>t.reverse=this.windBackup.reverse[i]);this.windBackup=null;}
  if((key==='UP'||key==='DOWN')&&this.rockerMode===2&&this.bendBackup!==undefined){this.speed=this.bendBackup;delete this.bendBackup;}
  if(key==='DOWN'&&this.magnetMark){const {at,targets}=this.magnetMark;for(const n of targets){const tr=this.tracks[n];tr.gaps??=[];tr.gaps.push([at,this.clock]);}this.magnetMark=null;}
  if(key==='FN'&&this.powerGesture?.waking){if(held>=1500){this.powered=true;this.flash={pattern:'power-on',at:this.now};this.say('Power on');}this.powerGesture=null;return;}
  if(!this.powered)return;
  const m=/^T([1-4])$/.exec(key),i=m?+m[1]-1:-1;
  if((key===this.isolateKey||key==='PLAY'||key==='FN')&&this.isolate){this.endIsolate();this.say('Mix restored');return;}
  if(i>=0&&this.recording?.i===i&&this.recording.kind==='sampler'){this.finishTake();return;}
  if(key==='PLAY'&&this.repeat){if(this.current.grid&&!this.recording&&!this.has('FN')){const bar=240/this.current.bpm;this.clock-=Math.round((this.clock-(this.repeat.at??this.clock))/bar)*bar;}this.repeat=null;this.say('Repeat released');return;}
  if(press.moved){this.secondaryLedSpread=false;this.rearm();return;}
  if(this.rockerOpen&&this.rockerMode===1&&/^T[1-4]$/.test(key)&&!press.windChord&&!press.moved&&held<350){const i=+key[1]-1;if(this.windLastTap?.i===i&&this.now-this.windLastTap.at<=300){this.windLatch=[.5,1.5,3,4][i];this.windLastTap=null;this.say('Wind speed '+this.windLatch+'×');}else this.windLastTap={i,at:this.now};return;}
  if(key==='FN'&&this.btHold){this.btHold=null;this.btTaps=[];return;}
  if(press.used)return;
  if(key==='FN'||key==='PLAY'){
   const other=key==='FN'?'PLAY':'FN';
   if(this.has(other)&&held>=350&&held<5000){this.fixed=!this.fixed;this.flash={pattern:this.fixed?'fixed':'variable',at:this.now};this.consume(other);this.say(this.fixed?'Fixed loops':'Variable loops');return;}
  }
  if(key==='FN'){
   if(held>=25&&held<300){if(this.btTaps.length&&this.now-this.btTaps.at(-1)>350)this.btTaps=[];this.btTaps.push(this.now);if(this.btTaps.length>2)this.btTaps=this.btTaps.slice(-2);}else this.btTaps=[];
   if(this.rockerOpen&&held<600){this.rockerOpen=false;this.flash={pattern:'page-exit',at:this.now,page:(this.rockerMode%4)+1};this.say('Rocker page closed');return;}
   if(this.recordReady){this.recordReady=null;this.say('Overdub preparation cancelled');return;}
   if(this.page&&held<600){this.flash={pattern:'page-exit',at:this.now,page:this.page};this.page=0;this.rearm();this.say('Track mode');}
   else if(!this.page&&!this.rockerOpen&&held<400){this.tapTimes=this.tapTimes.filter(t=>this.now-t<4500);if(this.tapTimes.length&&this.now-this.tapTimes.at(-1)>1500)this.tapTimes=[];this.tapTimes.push(this.now);if(this.tapTimes.length>=4){const gap=(this.tapTimes.at(-1)-this.tapTimes[0])/(this.tapTimes.length-1);this.current.bpm=clamp(60000/gap,40,300);this.current.grid=true;this.clock=0;this.say('Beat grid: '+Math.round(this.current.bpm)+' BPM');}}
   this.powerGesture=null;return;
  }
  if(key==='PLAY'){
   if(this.repeat){this.repeat=null;this.say('Repeat released');return;}
   if(this.has('FN')){this.consume('FN');this.playTaps.push(this.now);return;}
   if(this.recording){if(held<400&&this.recording.kind==='ordinary'&&this.tracks[this.recording.i].state==='armed'){this.finishTake();this.playing=false;this.say('Arm cancelled · stopped');return;}if(held<400)return;}
   if(held>=400){this.clock=0;this.playing=true;this.say('Restarted');}else{this.playing=!this.playing;this.say(this.playing?'Playing':'Stopped');}return;
  }
  if(i<0)return;
  if(this.has('FN')){
   this.consume('FN');
   if(this.page){const id=this.page+':'+(i+1);if(rateLanes.has(id)){let run=this.rateLane===id?this.rateTaps[id]||[]:[];this.rateLane=id;const gap=this.now-run.at(-1);if(gap<200||gap>1500)run=[];run.push(this.now);this.rateTaps[id]=run;if(run.length>1){this.rates[id]=60000*(run.length-1)/(run.at(-1)-run[0]);this.say('Effect rate tapped');}}}
   else {if(this.recording){this.say('Finish the take before changing songs');return;}this.resetCuts();const bank=i*4;this.slot=Math.floor(this.slot/4)===i?bank+(this.slot+1)%4:bank;this.rearm();this.say('Bank '+(i+1)+' · song '+(this.slot%4+1));}return;
  }
  if(this.page){this.pageTap(i);return;}
  if(this.recording?.i===i){this.finishTake();return;}
  if(this.recordReady?.i===i&&this.now<this.recordReady.until){this.recordReady=null;this.startTake(i);}
  else if(this.tracks[i].state==='empty'){if(held>=40&&held<=350)this.startTake(i);}
  else {this.tracks[i].muted=!this.tracks[i].muted;this.say(this.tracks[i].muted?'Track muted':'Track restored');}
  this.lastTaps[key]=this.now;
 }
 pageTap(i){
  if(this.recording&&this.page>=6)return;
  if(this.page===4&&i===3)delete this.rates['4:4'];
  if(this.page<=4){const id=this.page+':'+(i+1);if(id in this.cycles){this.cycles[id]=(this.cycles[id]+1)%FX_CYCLES[id];this.flash={pattern:'lane-count',at:this.now,lane:i+1,count:id==='4:4'?this.cycles[id]:this.cycles[id]+1};if(['1:4','2:4','3:1','3:2','3:3','4:4'].includes(id))delete this.rates[id];}else {this.fx[this.page-1][i]=this.page===1&&i===0||this.page===4&&i===1?.5:0;this.secondary[this.page-1][i]=secondaryDefaults()[this.page-1][i];}}
  if(this.page===5)this.fx[4][i]=.5;
  if(this.page===6){this.tracks[i].placement=.5;this.fx[5][i]=.5;}
  if(this.page===7){if(this.tracks[i].trim){delete this.tracks[i].trim;}else if(this.tracks[i].nudge!==.5)this.tracks[i].nudge=.5;else if(i===0)this.current.offset=0;else if(i===1)this.current.preset=0;}
  if(this.page===8)this.tracks[i].mono=!this.tracks[i].mono;
  this.rearm();this.say('Page '+this.page+' · track '+(i+1));
 }
 volumeAction(key){
  const up=key==='VOL+',d=up?1:-1;for(const k of this.keys.keys())if(/^T[1-4]$/.test(k))this.consume(k);
  if(this.has('FN')&&this.has('VOL+')&&this.has('VOL−')){this.page=0;this.rockerOpen=true;this.rockerAt=this.now;this.flash={pattern:'page-entry',at:this.now,page:(this.rockerMode%4)+1};this.consume('FN','VOL+','VOL−');this.say('Rocker '+(this.rockerMode+1));return;}
  if(this.rockerOpen&&!this.has('FN')){this.rockerMode=Math.max(0,Math.min(7,this.rockerMode+d));this.rockerAt=this.now;this.consume(key);this.say('Rocker '+(this.rockerMode+1));return;}

  if(this.has('PLAY')&&this.has('VOL+')&&this.has('VOL−')){this.monitorMuted=!this.monitorMuted;this.flash={pattern:this.monitorMuted?'sweep-down':'sweep-up',at:this.now};this.consume('PLAY','VOL+','VOL−');this.say(this.monitorMuted?'Input monitor muted':'Input monitor on');return;}
  if(this.has('FN')&&this.has('PLAY')){if(up)this.window.position=0;else {this.window={start:0,end:1,position:0,size:1};this.current.offset=0;this.current.preset=0;this.current.beatsPerBar=4;this.tracks.forEach(t=>{t.nudge=.5;delete t.trim;});}this.consume('FN','PLAY',key);this.confirm();this.say('Window reset');return;}
  if(this.page&&!this.has('FN')&&!this.has('PLAY')){this.openPage(this.page+d,true);this.consume(key);return;}
  if(this.has('FN')){if(this.page)this.openPage(this.page+d,true);else {this.window.position=(this.window.position+d*this.window.size+1)%1;this.say('Loop window moved');}this.consume('FN',key);return;}
  if(this.has('PLAY')){if(this.repeat){this.repeat.position=(this.repeat.position+d*this.repeat.size+1)%1;this.say('Repeat moved');}else{this.route=this.lastVolume&&this.lastVolume.key===key&&this.now-this.lastVolume.at<350?3:up?2:1;this.lastVolume={key,at:this.now};this.flash={pattern:['','one','two','three'][this.route],at:this.now};this.say(['','Input FX','Track FX','Input + track FX'][this.route]);}this.consume('PLAY',key);return;}
  this.volumeIndex=Math.max(0,Math.min(15,this.volumeIndex+d));this.volume=[0,2,3,4,6,8,11,16,23,32,45,64,90,128,181,256][this.volumeIndex]/256;this.say('Volume '+Math.round(this.volume*100)+'%');
 }
 rock(dir,repeating=false){
  if(!this.powered)return;
  const held=[...this.keys.keys()].find(k=>/^T[1-4]$/.test(k));
  if(held&&!this.rockerOpen&&!this.page&&!this.has('FN')&&!this.has('PLAY')&&!this.heads&&!this.recording){
   this.resetCuts();const i=+held[1]-1,tr=this.tracks[i];
   if(tr.state!=='empty'&&tr.selected<0){tr.takes.push({...tr,takes:[]});tr.selected=tr.takes.length-1;}
   if(dir>0){this.tracks[i]={...freshTrack(),takes:tr.takes};this.say('New take');}
   else if(tr.takes.length){const at=tr.selected<0?tr.takes.length-1:(tr.selected+tr.takes.length-1)%tr.takes.length;this.tracks[i]={...tr.takes[at],takes:tr.takes,selected:at};this.say('Saved take '+(at+1));}
   this.consume(held);this.recordReady=null;return;
  }
  if(!this.has('FN')&&!this.has('PLAY')&&this.rockerMode){
   const targets=held&&this.rockerOpen?[+held[1]-1]:this.tracks.map((t,i)=>t.state!=='empty'?i:-1).filter(i=>i>=0);if(held)this.consume(held);
   if(this.rockerMode===6){
    if(repeating)return;
    const blocked=this.heads||this.recording||this.repeat||this.window.free||this.window.size!==1||this.window.reverse||this.page||this.tracks.some(t=>t.reverse||t.trim?.head||t.trim?.tail);
    const signature=targets.join(',');
    if(blocked){this.flash={pattern:'no-action',at:this.now};this.say('Cut unavailable in this state');return;}
    if(dir>0){if(this.cutMark)this.cutMark=null;else{const at=this.cuts.findLastIndex(c=>c.targets.some(i=>targets.includes(i)));if(at>=0){const cut=this.cuts[at];cut.previous.forEach(([i,length,offset])=>{this.tracks[i].playLength=length;this.tracks[i].phaseOffset=offset;});this.cuts.splice(at,1);}}this.say('Cut undone');return;}
    if(!this.playing||!targets.length||this.cuts.length>=8){this.flash={pattern:'no-action',at:this.now};this.say('Cut unavailable');return;}
    const source=this.tracks[targets[0]],spb=source.mono?496:280,period=source.playLength||source.length,blocks=Math.max(1,Math.ceil(period*48000/spb)),phase=Math.floor(((this.clock+(source.phaseOffset||0))%period+period)%period*48000/spb)%blocks,q=Math.min(65535,Math.ceil(phase*65536/blocks));
    if(!this.cutMark){this.cutMark={at:this.clock,signature,q,period};this.say('Cut start marked');return;}
    if(this.cutMark.signature!==signature){this.say('Keep the same track held');return;}
    if(this.clock-this.cutMark.at>=this.cutMark.period){this.cutMark=null;this.flash={pattern:'no-action',at:this.now};this.say('Mark a section shorter than one loop');return;}
    const qa=this.cutMark.q,previous=[];
    for(const i of targets){const tr=this.tracks[i],spb=tr.mono?496:280,old=tr.playLength||tr.trim?.length||tr.length,n=Math.ceil(old*48000/spb),a=Math.floor(qa*n/65536),b=Math.floor(q*n/65536),keep=b>a?n-(b-a):a-b;if(a===b||keep<2){this.cutMark=null;this.flash={pattern:'no-action',at:this.now};this.say('Section too short');return;}previous.push([i,old,tr.phaseOffset||0,keep*spb/48000,(b>a?a%keep:0)*spb/48000-this.clock]);}
    for(const [i,old,offset,length,phaseOffset] of previous){this.tracks[i].playLength=length;this.tracks[i].phaseOffset=phaseOffset;}
    this.cuts.push({targets:[...targets],previous:previous.map(x=>x.slice(0,3))});this.cutMark=null;this.say('Section cut');return;
   }
   if(this.rockerMode===7){this.heads=dir<0;this.manualContact=true;this.say(dir<0?'Manual heads':'Tape contact');return;}
   if(this.rockerMode===5){if(dir<0&&!this.magnetMark)this.magnetMark={at:this.clock,targets};else if(dir>0&&!repeating){for(const n of targets)this.tracks[n].gaps?.pop();this.say('Latest erasure undone');}return;}
   if(this.rockerMode===1){this.windBackup??={speed:this.speed,reverse:this.tracks.map(t=>t.reverse)};if(held&&this.rockerOpen)this.keys.get(held).windChord=true;this.speed=held&&this.rockerOpen?[.5,1.5,3,4][+held[1]-1]:this.windLatch;this.tracks.forEach(t=>t.reverse=dir<0);this.say(dir<0?'Rewinding':'Fast forwarding');return;}
   if(this.rockerMode===2){this.bendBackup??=this.speed;this.speed=clamp(this.speed*(dir<0?.9:1.1),.05,3);this.say(dir<0?'Tape dragged':'Tape pulled');return;}
   if(this.rockerMode===3||this.rockerMode===4){this.wear=clamp(this.wear+(dir<0?.08:-.08));this.say(this.rockerMode===3?'Battery wear':'Tape wear');return;}
  }
  if(this.recording?.kind==='ordinary'){this.say('Speed is locked during this take');return;}
  if(this.has('FN')&&this.has('PLAY')){if(!repeating){const bpm=Math.round(this.speed*80),next=dir>0?(this.octaveFrom&&bpm===Math.floor(this.octaveFrom/2)?this.octaveFrom:bpm*2):Math.floor(bpm/2);if(next>=20&&next<=160){if(dir<0)this.octaveFrom=bpm;else this.octaveFrom=0;this.speed=next/80;}}this.consume('FN','PLAY');}
  else if(this.has('PLAY')&&this.playing&&!this.page){if(!this.repeat){const length=this.tracks.find(t=>t.state==='playing')?.length||4,cycle=length*this.window.size;let size=this.current.grid?(60/this.current.bpm)*(dir>0?1:2)/cycle:dir>0?.125:.25;if(size>=1)size=dir>0?.25:.5;const unit=cycle*size,position=((Math.floor(this.clock/unit)-1)*size%1+1)%1;this.repeat={size:clamp(size,1/256,1),position,at:this.clock};}else this.repeat.size=clamp(this.repeat.size*(dir>0?.5:2),1/256,.5);this.consume('PLAY');}
  else if(this.has('FN')&&!this.page){this.window.size=clamp(this.window.size*(dir>0?.5:2),1/256,1);this.consume('FN');}
  else {const twice=!repeating&&this.lastRock&&this.lastRock.dir===dir&&this.now-this.lastRock.at<=350,base=twice?this.lastRock.base:this.speed;const k=12*Math.log2(base);this.speed=clamp(twice?firmwareSemitone(base,dir):(Math.round(this.speed*80)+dir)/80,.25,2);if(!repeating)this.lastRock={dir,at:this.now,base:twice?this.speed:base};else this.lastRock=null;}
  this.say(this.has('FN')&&this.has('PLAY')?'Tape speed '+this.speed.toFixed(2)+'×':this.repeat?'Repeat '+Math.round(this.repeat.size*1000)/10+'%':this.has('FN')&&!this.page?'Window '+Math.round(this.window.size*1000)/10+'%':'Tape speed '+this.speed.toFixed(2)+'×');
 }
 trimFader(i,v){
  const tr=this.tracks[i],cut=this.has('FN'),key=this.keys.get('PLAY');
  if(!key||this.heads||this.repeat||this.recording||tr.state!=='playing'||(!cut&&!tr.trim))return false;
  const spb=tr.mono?496:280,full=Math.ceil(tr.length*48000/spb),q=Math.round(v*255);
  key.trim??=[];let hand=key.trim[i];
  if(!hand||hand.cut!==cut){key.trim[i]={cut,q0:q};return true;}
  let head=tr.trim?.head||0,tail=tr.trim?.tail||0,total=head+tail;
  const unit=this.current.grid?Math.max(spb,Math.floor(2880000/this.current.bpm/(cut?16:4))):spb;
  const soft=(a,b,x,max)=>a>=b?(b>=255?x:Math.floor((x*(255-a)+(255-b)/2)/(255-b))):(b<=0?x:x+Math.floor(((max-x)*(b-a)+b/2)/b));
  const lg=x=>{const m=Math.floor(Math.log2(Math.max(1,x))),f=m>=8?x>>m-8:x<<8-m;return m*256+(f&255);};
  const llen=p=>{if(!p)return full;if(p>=255)return 2;const l=lg(full)-Math.floor((p*(lg(full)-256)+127)/255),m=l>>8,f=256+(l&255);return Math.max(2,Math.min(full,m>=8?f*2**(m-8):Math.floor(f/2**(8-m))));};
  const choices=[Math.floor(full*spb/unit)];if(cut&&this.current.grid){const minimum=Math.ceil(2*spb/unit);for(let e=15;e>=0&&choices.length<30;e--)for(const n of [e?3*2**(e-1):0,2**e])if(n&&n<choices[0]&&n>=minimum&&(choices.length===1||n<choices.at(-1)))choices.push(n);}
  if(!hand.engaged){if(Math.abs(q-hand.q0)<7)return true;hand.engaged=true;hand.q0=q;
   if(cut){if(this.current.grid){const kept=Math.round((full-total)*spb/unit);hand.max=choices.length-1;hand.base=choices.reduce((best,n,k)=>Math.abs(n-kept)<Math.abs(choices[best]-kept)?k:best,0);}else{hand.max=255;hand.base=Math.max(0,Math.min(255,Math.round((lg(full)-lg(Math.max(2,full-total)))*255/(lg(full)-256))));}}
   else{hand.max=Math.floor(total*spb/unit);hand.base=Math.min(hand.max,Math.round(head*spb/unit));}
  }
  const pos=soft(q,hand.q0,hand.base,hand.max);
  if(cut){const kept=this.current.grid?Math.max(2,Math.min(full,Math.round(choices[pos]*unit/spb))):llen(pos);total=full-kept;head=Math.min(head,total);tail=total-head;}
  else{head=Math.min(total,Math.round(pos*unit/spb));tail=total-head;}
  tr.trim={head,tail,length:(full-head-tail)*spb/48000};
  this.consume('FN','PLAY');key.moved=true;if(this.has('FN'))this.keys.get('FN').moved=true;
  this.say(cut?'Track '+(i+1)+' length trimmed':'Track '+(i+1)+' section moved');return true;
 }
 fader(i,v){
  v=clamp(v);const old=this.faders[i];this.faders[i]=v;if(!this.powered)return;
  if(this.trimFader(i,v))return;
  const heldTrack=[...this.keys.keys()].find(k=>/^T[1-4]$/.test(k));
  if(this.page&&heldTrack){const lane=+heldTrack[1]-1;
   if(this.page===4&&lane===1&&i===1){if(this.cycles['4:2']===1)this.secondary[3][1]=clamp(this.secondary[3][1]+v-old);else this.fx[3][1]=clamp(this.fx[3][1]+v-old);this.keys.get(heldTrack).moved=true;return;}
   if(this.page===7&&lane===i){this.tracks[i].nudge=clamp(this.tracks[i].nudge+v-old);this.keys.get(heldTrack).moved=true;this.say('Track nudge '+Math.round((this.tracks[i].nudge-.5)*200)+'%');return;}
   if(this.page>=1&&this.page<=4&&((lane===i&&FX[(this.page-1)*4+lane].secondary)||(i===(lane+1)%4&&FX[(this.page-1)*4+lane].shape))){if(lane!==i)this.spread=clamp(this.spread+v-old);else this.secondary[this.page-1][lane]=clamp(this.secondary[this.page-1][lane]+v-old);this.secondaryLedSpread=lane!==i;this.keys.get(heldTrack).moved=true;this.say('Secondary control adjusted');return;}
  }
  if(this.page<=4&&this.page&&heldTrack&&+heldTrack[1]-1===i){this.keys.get(heldTrack).moved=true;return;}
  if(this.has('FN')&&!this.page){this.consume('FN');if(this.heads)this.headPositions[i]=v;else {const w=this.repeat||this.window;if(w.start===undefined){w.start=w.position||0;w.end=Math.min(1,w.start+w.size);w.size=1;}if(!w.free){if(w===this.window&&w.size<1){w.start=w.position;w.end=Math.min(1,w.start+w.size);w.size=1;}w.free=true;}if(i<2)w[i===0?'start':'end']=v;else if(i===2){const width=Math.abs(w.end-w.start),base=v*(1-width),reversed=w.start>w.end;w.start=base+(reversed?width:0);w.end=base+(reversed?0:width);w.position=0;}else w.size=Math.max(1/256,v);if(!this.repeat)w.reverse=w.start>w.end;}this.keys.get('FN').moved=true;this.say(this.heads?'Head position adjusted':'Loop window adjusted');return;}
  if(this.rockerMode===7&&this.has('DOWN')){this.headPositions[i]=v;this.say('Head position adjusted');return;}
  const target=this.pickup[i];if(target!==null){if(Math.abs(v-target)>.025&&(old-target)*(v-target)>0){this.say('Cross the stored value to pick up');return;}this.pickup[i]=null;}
  if(!this.page)this.tracks[i].level=v;
  else if(this.page<=6){if(this.page===4&&i===1&&this.cycles['4:2']===1)this.secondary[3][1]=v;else this.fx[this.page-1][i]=v;if(this.page===6)this.tracks[i].placement=v;}
  else if(this.page===7){if(i===0)this.current.offset=v-.5;if(i===1){this.current.preset=Math.min(4,Math.floor(v*5));this.flash={pattern:'length',at:this.now,count:this.current.preset};}}
  this.say(this.page?'Page '+this.page+' · fader '+(i+1):'Track '+(i+1)+' level');
 }
 tick(ms){
  const dt=Math.max(0,Math.min(ms-this.now,100));this.now=ms;if(this.powered&&(this.playing||this.current.grid&&this.tracks.every(t=>t.state==='empty')))this.clock+=dt/1000*this.speed;
  if(this.cutMark&&!this.playing)this.cutMark=null;
  if(this.recordReady&&ms>=this.recordReady.until)this.recordReady=null;
  if(this.btHold){const age=ms-this.btHold.at;if(age>=1000&&!this.btHold.action){this.btHold.action=1;this.bluetooth={state:1,at:ms};this.consume('FN');this.say('Bluetooth input discovery');}if(age>=3000&&this.btHold.action===1){this.btHold.action=2;this.bluetooth={state:5,at:ms};this.say('Bluetooth off');}}
  if(this.playTaps.length&&ms-this.playTaps.at(-1)>400){if(this.playTaps.length>=3){this.heads=!this.heads;this.say(this.heads?'Four heads':'Track mode');}else if(this.playTaps.length===2){this.speed=1;this.window={start:0,end:1,position:0,size:1};this.resetCuts();this.windLatch=3;this.windLastTap=null;this.tracks.forEach(t=>t.gaps=[]);this.wear=0;this.confirm();this.say('Tape restored');}this.playTaps=[];}
  for(const [key,k] of this.keys){const age=ms-k.at;if(k.used)continue;
   if(key==='FN'&&this.keys.size===1&&!this.page&&!this.btHold){
    if(!this.powered){this.powerGesture={waking:true,at:k.at};continue;}
    if(this.tapTimes.length&&ms-this.tapTimes.at(-1)<3000&&age>=1000){if(this.tapTimes.length>=4){const bf=Math.round(2880000/this.current.bpm),nb=snapBeatFrames(bf);if(nb!==bf){if(this.tracks.some(t=>t.state!=='empty'))this.speed=clamp(Math.floor((Math.round(this.speed*65536)*bf+Math.floor(nb/2))/nb),16384,131072)/65536;this.current.bpm=2880000/nb;this.flash={pattern:'snap',at:this.now};this.say('Tempo rounded');}else{this.flash={pattern:'no-action',at:this.now};this.say('Tempo unchanged');}}else{this.current.grid=false;this.current.offset=0;this.confirm();this.say('Grid cleared');}this.tapTimes=[];this.consume(key);continue;}
    if((!this.playing||this.tracks.every(t=>t.state==='empty'))&&age>=2500){this.powered=false;this.flash={pattern:'power-off',at:ms};this.consume(key);this.say('Power off');}
   }
   if(this.has('FN')&&this.has('PLAY')&&!this.keys.get('FN').used&&!this.keys.get('PLAY').used&&ms-Math.max(this.keys.get('FN').at,this.keys.get('PLAY').at)>=5000){this.bright=!this.bright;this.consume('FN','PLAY');this.say('LED brightness changed');}
   const m=/^T([1-4])$/.exec(key);if(m){const i=+m[1]-1;
    if(this.has('FN')&&!this.has('PLAY')&&age>=400){this.rockerOpen=false;if(this.page===i+1){this.flash={pattern:'page-exit',at:this.now,page:this.page};this.page=0;this.rearm();this.consume('FN',key);this.say('Track mode');}else this.openPage(i+1);continue;}
    if(!this.rockerOpen&&!this.page&&!this.has('FN')&&!this.has('PLAY')){
     if(this.lastTaps[key]!==undefined&&k.at-this.lastTaps[key]<=600&&age>=400){const tr=this.tracks[i],takes=tr.takes.filter((_,n)=>n!==tr.selected);this.tracks[i]={...freshTrack(),takes};if(this.recording?.i===i)this.recording=null;this.consume(key);this.say('Track deleted');}
     else if(this.tracks[i].state!=='empty'&&age>=600&&(this.lastTaps[key]===undefined||k.at-this.lastTaps[key]>600)){if(this.heads&&this.tracks[i].state!=='empty'){if(age>=400){this.headSource=i;this.consume(key);this.say('Head source changed');}}else if(this.recording===null){if(this.tracks[i].state!=='empty'){this.recordReady={i,until:ms+10000};this.say('Release, then tap to arm overdub');}else this.startTake(i);this.consume(key);}}
    }
   }
   const windowStep=(this.has('FN')&&!this.has('PLAY'))||(this.has('PLAY')&&!this.has('FN'));if((key==='UP'||key==='DOWN')&&age>=(windowStep?450:600)&&ms-(k.repeatAt||k.at)>=(windowStep?375:80)){this.rock(key==='UP'?1:-1,true);k.repeatAt=ms;}
  }
  if(this.recording){const r=this.recording,tr=this.tracks[r.i];
   if(r.kind==='copy'&&this.has('T'+(r.i+1))&&ms-this.keys.get('T'+(r.i+1)).at>=180){r.kind='sampler';this.say('Sampler print');}
   if(tr.state==='armed'&&(this.current.grid||this.input&&(this.audioInputPeak===undefined?ms-r.at>=600:this.audioInputPeak>=1000))&&(!this.current.grid||this.clock>=(Math.floor(r.armedAt/(60/this.current.bpm))+1)*(60/this.current.bpm))){tr.state='recording';r.at=ms;this.say('Recording');}
   if(r.stopAt&&this.clock>=r.stopAt){this.finishTake(true);return;}
   if(tr.state==='recording'&&r.kind==='ordinary'&&this.current.preset){const units=[0,1,2,4,8][this.current.preset],unit=this.fixed?this.tracks.find(t=>t.state==='playing')?.length||0:this.current.grid?240/this.current.bpm:0;if(unit&&ms-r.at>=units*unit*1000)this.finishTake(true);}
  }
 }
 // Same track-row policy in normal view and TRACK-held rocker preview.
 // Block quantization and the 280-block cap follow show_tracks468().
 trackLights(){
  const out={},beat=60/this.current.bpm,mod=(n,d)=>((n%d)+d)%d;
  const gridPhase=mod(this.clock-this.current.offset*beat,beat);
  for(let i=0;i<4;i++){
   const tr=this.tracks[i];let value=0;
   if(tr.state==='recording')value=this.recording?.stopAt?+(mod(gridPhase,beat/2)<beat/8):1;
   else if(tr.state==='armed')value=+(mod(gridPhase,this.current.grid?beat/2:beat)<beat/8);
   else if(tr.state!=='empty'||this.heads){
    if(tr.muted)value=.125;
    else if(!this.playing)value=1;
    else if(this.current.grid&&!this.heads)value=+(gridPhase<beat/8);
    else{
     const source=this.heads?this.tracks[this.headSource]:tr,spb=source.mono?496:280;
     const length=source.playLength||source.trim?.length||source.length;
     const blocks=Math.max(1,Math.ceil(length*this.window.size*48000/spb));
     const phase=this.clock+(source.phaseOffset||0)+(this.heads?this.headPositions[i]*length:0)+tr.nudge-.5;
     let pos=Math.floor(mod(phase,blocks*spb/48000)*48000/spb);
     if(tr.reverse!==!!this.window.reverse)pos=blocks-1-pos;
     const on=pos<Math.max(1,Math.min(280,Math.floor(blocks/8)));
     value=on?(tr.state==='empty'?.125:1):0;
    }
   }
   out['LED.Track'+(i+1)]=value;
  }
  if(this.current.grid&&this.tracks.every(tr=>tr.state==='empty'&&!tr.takes.length)){const count=this.current.beatsPerBar||4,index=Math.floor(mod(this.clock-this.current.offset*beat,beat*count)/beat)%4;for(let i=0;i<4;i++)out['LED.Track'+(i+1)]=+(i===index&&gridPhase<beat/8);}
  return out;
 }
 lights(){
  const t=this.now/1000,fn=this.keys.get('FN'),beat=60/this.current.bpm;
  if(!this.powered){if(this.powerGesture?.waking)return ledPattern('wake-hold',(this.now-this.powerGesture.at)/1000);if(this.flash?.pattern==='power-off')return ledPattern('power-off',(this.now-this.flash.at)/1000);return this.charging?ledPattern('charging',t):{};}
  if(fn&&!fn.used&&!this.btHold&&this.keys.size===1&&!this.page&&(!this.playing||this.tracks.every(tr=>tr.state==='empty'))&&this.now-fn.at>400)return ledPattern('shutdown-hold',(this.now-fn.at)/1000,{bank:Math.floor(this.slot/4)+1,song:this.slot%4+1});
  let o=this.page?pageLights(this.page,(this.now-this.pageAt)/1000,this.monitorMuted):ledPattern(this.playing&&!this.has('FN')&&(this.input||this.tracks.some(tr=>tr.state!=='empty'))?'vu':'song',this.playing?this.clock:t,{bank:Math.floor(this.slot/4)+1,song:this.slot%4+1,vu:this.audioVu??musicVu(this.clock,beat)});
  if(!this.page)Object.assign(o,this.trackLights());
  else if(this.page<=6){for(let i=0;i<4;i++){let value=this.controlValue(i);const id=this.page+':'+(i+1);let amount=this.page>=5||this.page===1&&i===0||this.page===4&&i===1&&this.cycles['4:2']!==1?Math.abs(value-.5)*2:value;const held=this.keys.get('T'+(i+1));if(held&&this.page<=4&&!(this.page===4&&i===1))amount=this.page===3&&i===2&&this.secondaryLedSpread?this.spread:this.secondary[this.page-1][i];Object.assign(o,ledPattern('lane-depth',t,{lane:i+1,amount}));}}
  else if(this.page===7){const held=[...this.keys.keys()].find(k=>/^T[1-4]$/.test(k));if(held){const i=+held[1]-1;Object.assign(o,ledPattern('lane-depth',t,{lane:i+1,amount:Math.abs(this.tracks[i].nudge-.5)*2}));}else if(this.recording&&this.current.preset){const units=[0,1,2,4,8][this.current.preset],unit=this.fixed?(this.tracks.find(tr=>tr.state==='playing')?.length||4):4*beat,left=Math.min(4,Math.max(0,Math.ceil(units-(this.now-this.recording.at)/1000/unit)));for(let i=1;i<=left;i++)o['LED.Track'+i]=left===1?Math.floor(t/.128)%2:1;}else if(this.current.grid){Object.assign(o,ledPattern('beat',this.clock-this.current.offset*beat,{beatSeconds:beat}));for(let i=0;i<4;i++)if(this.tracks[i].nudge!==.5&&!o['LED.Track'+(i+1)])o['LED.Track'+(i+1)]=(64+40*191/255)/255;}}
  else Object.assign(o,ledPattern('format',t,{mono:this.tracks.flatMap((tr,i)=>tr.mono?[i+1]:[])}));
  if(this.flash){const f=this.flash,age=(this.now-f.at)/1000;const limit=['lane-count','length'].includes(f.pattern)?(f.count===0?.344:f.count*.288):f.pattern==='success'?.48:f.pattern==='no-action'?.16:f.pattern==='page-entry'||f.pattern==='page-exit'?(firmwareAnimation(f.pattern==='page-exit'?2:f.walk?3:1,f.page||1,age*1000)&256?10:0):f.pattern==='power-on'?.52:f.pattern==='fixed'?.54:f.pattern==='variable'?.71:f.pattern==='sweep-up'||f.pattern==='sweep-down'?.384:.6;if(age<limit){const row=['page-entry','page-exit','lane-count','length','snap','no-action','success'].includes(f.pattern)?'Track':'Side';if(['lane-count','length'].includes(f.pattern))delete o['LED.Track'+(f.pattern==='length'?2:f.lane)];else for(let i=1;i<=4;i++)delete o['LED.'+row+i];Object.assign(o,ledPattern(f.pattern,age,f));}}
  // Source side-row priority: FN previews the song even on either page.
  const sideConfirmation=this.flash&&['one','two','three','sweep-up','sweep-down'].includes(this.flash.pattern)&&this.now-this.flash.at<({one:128,two:256,three:384,'sweep-up':384,'sweep-down':384}[this.flash.pattern]);
  const songRow=!!fn||!this.page&&!(this.playing&&(this.input||this.tracks.some(tr=>tr.state!=='empty')));
  if(songRow&&!sideConfirmation){const bank=Math.floor(this.slot/4),position=this.slot%4,blink=this.current.grid&&this.playing&&this.speed>=.1875?((this.clock%(beat)+beat)%beat)<beat/2:Math.floor(this.now/250)%2===0;for(let i=0;i<4;i++)o['LED.Side'+(i+1)]=i===position?(blink?1:0):i===bank?1:0;}
  const row=mask=>{for(let i=0;i<4;i++)o['LED.Track'+(i+1)]=mask&(1<<i)?1:0;};
  const anim=this.flash&&['page-entry','page-exit'].includes(this.flash.pattern)&&!!(firmwareAnimation(this.flash.pattern==='page-exit'?2:this.flash.walk?3:1,this.flash.page||1,this.now-this.flash.at)&256);
  const heldTrack=[...this.keys.keys()].some(k=>/^T[1-4]$/.test(k));
  if(this.rockerOpen&&heldTrack&&!anim)Object.assign(o,this.trackLights());
  if(this.rockerOpen&&!heldTrack&&!anim){row(0);Object.assign(o,ledPattern('rocker',(this.now-this.rockerAt)/1000,{slot:this.rockerMode+1,monitorMuted:this.monitorMuted}));}
  if((!this.page&&!this.rockerOpen||this.rockerOpen&&heldTrack)&&!anim&&!this.heads){
   for(let i=0;i<4;i++){const tr=this.tracks[i];if(tr.state==='empty'&&tr.takes.length&&this.recordReady?.i!==i)o['LED.Track'+(i+1)]=firmwareBreath(this.now)/255;if(this.recordReady?.i===i){const p=this.now%1000;o['LED.Track'+(i+1)]=p<80||p>=180&&p<260?1:0;}}
  }
  if(!this.page&&!this.rockerOpen&&!this.heads&&!this.recording&&!this.recordReady&&this.playing&&fn&&!fn.used&&!anim){
   const counts=this.tracks.map(tr=>tr.takes.length+(tr.state!=='empty'&&tr.selected<0?1:0));
   const signature=counts.join(',');if(!this.preview||this.preview.signature!==signature||this.preview.hold!==fn.at)this.preview={signature,hold:fn.at,at:this.now};
   const mask=firmwarePreview(this.now-this.preview.at,counts);if(mask>=0)row(mask);
  }else this.preview=null;
  if(this.btHold&&this.btHold.action===0){row((1<<Math.min(4,1+Math.floor((this.now-this.btHold.at)/250)))-1);}
  else if(!(this.flash?.pattern==='success'&&this.now-this.flash.at<480)&&this.bluetooth&&!this.page&&!this.rockerOpen&&!this.recording&&(!fn||this.btHold?.action)){const mask=firmwareBluetooth(this.bluetooth.state,this.now-this.bluetooth.at,this.now);if(mask&256){for(let i=0;i<4;i++){o['LED.Side'+(i+1)]=mask&(1<<i)?1:0;o['LED.Track'+(i+1)]=mask&(16<<i)?mask&512?(mask>>>16&255)/255:1:0;}}}
  if(this.now<this.successUntil)for(let i=1;i<=4;i++)o['LED.Track'+i]=1;
  return o;
 }
 seed(card,step){this.reset();const index=card.steps.indexOf(step);const c=step.context||{};this.playing=c.playing??true;this.current.grid=!!c.grid;const loaded=c.loaded||[1];for(const i of loaded)this.tracks[i-1].state='playing';for(const i of c.muted||[])this.tracks[i-1].muted=true;this.page=step.page||(/^page[1-8]$/.test(step.pattern||'')?+step.pattern.slice(4):0);this.heads=!!c.heads||card.id==='head-source';this.faders=[...(card.initialFaders||[.15,.55,.8,.6])];
  if(card.id==='take-length'||card.id==='countdown')this.page=7;
  if(card.id==='countdown'){this.current.grid=true;this.current.preset=3;this.startTake(1);this.tracks[1].state='recording';}
  if(card.id==='power'){this.powered=['power-on','shutdown-hold'].includes(step.pattern);this.playing=false;this.tracks.forEach(tr=>tr.state='empty');this.charging=step.pattern==='charging';}
  if(card.id==='first-loop'){this.tracks.forEach(tr=>tr.state='empty');if(index>0&&index<4){this.startTake(0);if(index===3){this.tracks[0].state='recording';this.recording.at=-1000;}}if(index===4){this.tracks[0].state='playing';this.playing=true;}}
  if(card.id==='preset'){this.current.preset=1;this.current.grid=true;if(index===0)this.page=7;if(index===3)this.startTake(0);}
  if(card.id==='grid-record-lights'){this.startTake(1);if(step.pattern==='grid-finish'){this.tracks[1].state='recording';this.recording.stopAt=.5;}}
  if(card.id.startsWith('repeat')||['resize-repeat','move-repeat','release-repeat'].includes(card.id))this.repeat={size:.125,position:0,at:0};
  if(c.recording?.length){this.startTake(c.recording[0]-1,card.id==='held-print'?'sampler':'copy');if(card.id==='held-print')this.keys.set('T'+c.recording[0],{at:0,used:true});}
  if(this.repeat)this.keys.set('PLAY',{at:0,used:true});
  if(card.id==='fx-4-2'&&index>=2)this.cycles['4:2']=1;
  if(step.lane&&step.amount!==undefined&&this.page<=6&&this.page){if(this.page===4&&step.lane===2&&this.cycles['4:2']===1)this.secondary[3][1]=step.amount;else this.fx[this.page-1][step.lane-1]=step.amount;}
  if(step.pattern==='lane-count'&&step.lane&&this.page<=4){const id=this.page+':'+step.lane;if(id in this.cycles)this.cycles[id]=id==='4:4'?step.count:Math.max(0,(step.count||1)-1);}
  const secondaryCards={'secondaries':[2,4],'wah-resonance':[2,3],'echo-feedback':[2,4],'tremolo-shape':[3,3],'tremolo-spread':[3,3],'reverb-damping':[3,4]};
  if(card.secondaryTarget||secondaryCards[card.id]){const [p,l]=card.secondaryTarget||secondaryCards[card.id];this.page=p;this.fx[p-1][l-1]=.6;if(step.amount!==undefined){if(card.id==='tremolo-spread')this.spread=step.amount;else this.secondary[p-1][l-1]=step.amount;}}
  const moved={};for(const previous of card.steps.slice(0,index+1))Object.assign(moved,previous.faders||{});
  if(this.page===6)for(const [lane,value]of Object.entries(moved))this.tracks[+lane-1].placement=value;
  if(card.id==='reverse'&&index>0)this.tracks[0].reverse=true;
  this.rearm();this.say(this.repeat?'PLAY held · tap PLAY to release':card.id==='held-print'&&this.recording?'Destination held · tap it to release':'Ready');
 }
 resetCuts(){this.cuts=[];this.cutMark=null;this.tracks.forEach(t=>{delete t.playLength;delete t.phaseOffset;});}
 endIsolate(){if(this.isolate){this.tracks.forEach((t,i)=>t.muted=this.isolate[i]);this.isolate=null;this.isolateKey=null;}}
 cancel(){this.keys.clear();this.powerGesture=null;if(this.isolate){this.tracks.forEach((t,i)=>t.muted=this.isolate[i]);this.isolate=null;}}
}
