import test from 'node:test';
import assert from 'node:assert/strict';
import {cards} from '../web/field-guide/content.js';
import {Scenario,prepareScenario,stepSeconds,gestureSeconds,stepEvents,gestureCues} from '../web/field-guide/scenarios.js';
const focused=['takes-v4','replace','rocker-v4','wind-v4','bender-v4','battery-v4','sandpaper-v4','magnet-v4','manual-head-v4','restore-tape-v4','track-trim-v4','track-slide-v4'];
const card=id=>{const c=cards.find(c=>c.id===id);assert(c,id);return c;};
const loaded=c=>prepareScenario(c).tracks.flatMap((t,i)=>t.state==='playing'?[i+1]:[]);
test('every walkthrough caption lasts seven seconds, independently of physical input time',()=>{
 for(const c of cards)for(let i=0;i<c.steps.length;i++){
  assert.equal(stepSeconds(c,i),7,c.id);assert(gestureSeconds(c,i)>0,c.id);
  const events=stepEvents(c,i),snapshot=JSON.stringify(events);gestureCues(c,i,1,events);assert.equal(JSON.stringify(events),snapshot);
  assert.equal(gestureCues(c,i,-.01).size,0);
 }
});
test('rocker clicks remain 120ms, with a longer visual tap trail',()=>{
 const c=card('takes-v4');for(const i of [0,2]){
  const e=stepEvents(c,i).filter(e=>e[1]==='button');assert(Math.abs(e[1][0]-e[0][0]-.12)<1e-9);
  assert.equal(gestureCues(c,i,.34).has('ROCKER'),false);
  for(const t of [.35,.46,.8,1.5,1.66])assert.equal(gestureCues(c,i,t).get('ROCKER'),'tap');
  assert.equal(gestureCues(c,i,1.68).has('ROCKER'),false);
 }
});
test('single-track lessons seed only the track being taught',()=>{
 for(const id of focused)assert.deepEqual(loaded(card(id)),[1],id);
 for(const [id,tracks]of [['levels',[1,2,3,4]],['isolate',[1,2,3,4]],['head-source',[1,2]],['scissors386',[1,2]],['nudge',[1,2]]])assert.deepEqual(loaded(card(id)),tracks,id);
});
test('Unlimited takes has no unrelated T2/T3/T4 activity throughout its scenario',()=>{
 const c=card('takes-v4'),s=new Scenario(c);
 for(let i=0;i<c.steps.length;i++){
  s.begin(i);const end=gestureSeconds(c,i);
  for(let t=0;t<=end;t+=.016){s.advance(t);const lights=s.sim.lights();for(let n=2;n<=4;n++)assert.equal(lights['LED.Track'+n]||0,0,`step ${i+1}, T${n}, ${t}s`);}
  s.advance(end);
 }
});
test('genuine all-track pairing and reset indications remain intact',()=>{
 const s=new Scenario(card('bluetooth-v4'));s.begin(0);s.advance(2);const leds=s.sim.lights();assert(leds['LED.Track1']>0);for(let n=2;n<=4;n++)assert.equal(leds['LED.Track'+n],leds['LED.Track1']);
 const reset=new Scenario(card('escape-hatch-v4'));reset.begin(0);reset.advance(2.1);const result=reset.sim.lights();for(let n=1;n<=4;n++)assert.equal(result['LED.Track'+n],1);
});
test('countdown keeps its full four-bar timing in two seven-second captions',()=>{
 const c=card('countdown');assert.equal(c.steps.length,2);assert.equal(c.steps.reduce((n,_,i)=>n+gestureSeconds(c,i),0),8.75);
 const s=new Scenario(c);s.begin(0);s.advance(gestureSeconds(c,0));assert(s.sim.recording);s.begin(1);s.advance(gestureSeconds(c,1));assert.equal(s.sim.recording,null);
});

import {Simulator} from '../web/field-guide/simulator.js';
test('FX tempo is tapped with FN; FN + PLAY clears only that lane',()=>{
 for(const [page,lane]of [[1,2],[1,4],[2,4],[3,1],[3,2],[3,3],[4,4]]){
  const s=new Simulator(),key='T'+lane,id=page+':'+lane;s.page=page;
  s.button('FN',true);s.button(key,true);s.tick(80);s.button(key,false);
  s.tick(500);s.button(key,true);s.tick(580);s.button(key,false);s.button('FN',false);
  assert.equal(s.rates[id],120,id);s.rates['other']=90;
  s.button('FN',true);s.button('PLAY',true);s.button(key,true);
  assert.equal(s.rates[id],undefined,id);assert.equal(s.rates.other,90);
 }
 const c=card('tap-rate');assert.match(c.steps[2].label,/Clear tapped tempo/);assert.deepEqual(c.steps[2].hold,['FN','PLAY']);assert.deepEqual(c.steps[2].tap,['T4']);
});
test('plain gate and chorus taps keep a tapped rate; timed division and wobble taps clear it',()=>{
 for(const [page,lane,clears]of [[1,2,false],[1,4,false],[2,4,true],[3,1,true],[3,2,true],[3,3,true],[4,4,true]]){
  const s=new Simulator(),id=page+':'+lane;s.page=page;s.rates[id]=120;s.pageTap(lane-1);assert.equal(s.rates[id],clears?undefined:120,id);
 }
});
test('input mute refuses new input takes and cancels a waiting arm, while bounces remain available',()=>{
 const s=new Simulator();s.startTake(0);assert.equal(s.tracks[0].state,'armed');
 s.button('PLAY',true);s.button('VOL−',true);s.button('VOL+',true);
 assert(s.monitorMuted);assert.equal(s.recording,null);assert.equal(s.tracks[0].state,'empty');
 s.startTake(0);assert.equal(s.recording,null);s.startTake(1,'copy');assert.equal(s.recording.kind,'copy');
 assert.doesNotMatch(card('monitor').steps[1].detail,/still captures/);
});
test('window reset clears trims, offsets, take length and bar length without deleting audio',()=>{
 const s=new Simulator();s.tracks[0].state='playing';s.tracks[0].trim={head:1,tail:2};s.tracks[0].nudge=.7;s.current.offset=.2;s.current.preset=3;s.current.beatsPerBar=7;
 s.button('FN',true);s.button('PLAY',true);s.button('VOL−',true);
 assert.equal(s.current.offset,0);assert.equal(s.current.preset,0);assert.equal(s.current.beatsPerBar,4);assert.equal(s.tracks[0].trim,undefined);assert.equal(s.tracks[0].nudge,.5);assert.equal(s.tracks[0].state,'playing');
});
test('a bounce onto a loaded track preserves its earlier take',()=>{
 const s=new Simulator();s.tracks[0].state='playing';s.tracks[0].audioId=123;s.startTake(0,'copy');s.tick(1000);s.finishTake(true);
 assert.equal(s.tracks[0].takes.length,2);assert.equal(s.tracks[0].takes[0].audioId,123);
 for(const id of ['tape-copy','print-repeat','sampler'])assert.doesNotMatch(card(id).note,/loaded.*replaced/);
});
test('every FX type/division caption and flash count matches the executed track tap',()=>{
 const types={'1:2':['Chorus','Flanger'],'1:3':['Soft','Hard','Fold'],'1:4':['Gallop','Halves','Slow gear'],'2:1':['Crush','Lo-fi','Radio'],'2:4':['1/16','dotted 1/16','1/8'],'3:1':['beat','half','quarter'],'3:2':['beat','half','quarter'],'3:3':['beat','half','quarter'],'3:4':['Reverb','Freeze'],'4:3':['Cassette','Vinyl','Pink','Minidisc'],'4:4':['Wow + flutter','Wow','Flutter']};
 for(const [id,names]of Object.entries(types)){
  const c=card('fx-'+id.replace(':','-')),s=new Scenario(c);for(let i=0;i<c.steps.length;i++){
   s.begin(i);s.advance(gestureSeconds(c,i));const step=c.steps[i];if(!step.label.startsWith('Tap T')||!step.label.includes(':'))continue;
   const selected=s.sim.cycles[id];assert.equal(step.label.split(': ')[1].toLowerCase(),names[selected].toLowerCase(),c.id+' step '+(i+1));assert.equal(step.count,id==='4:4'?selected:selected+1,c.id+' flashes');
  }
 }
});
test('monitor chord works on every page and rocker mode without changing routing or mode',()=>{
 for(const page of [0,1,2,3,4,5,6,7,8])for(const rockerOpen of [false,true])for(const mode of [0,1,2,3,4,5,6,7])for(const route of [1,2,3]){
  const s=new Simulator();s.page=page;s.rockerOpen=rockerOpen;s.rockerMode=mode;s.route=route;
  s.button('PLAY',true);s.button('VOL−',true);s.tick(50);s.button('VOL+',true);s.tick(150);
  assert(s.monitorMuted);assert.equal(s.route,route);assert.equal(s.page,page);assert.equal(s.rockerMode,mode);
 }
});
test('single and double volume taps under PLAY choose routes after the pair grace window',()=>{
 const s=new Simulator();s.button('PLAY',true);s.button('VOL−',true);s.tick(80);assert.equal(s.route,3);s.button('VOL−',false);s.tick(101);assert.equal(s.route,1);
 s.tick(200);s.button('VOL−',true);s.tick(280);s.button('VOL−',false);s.tick(301);assert.equal(s.route,3);
 s.tick(400);s.button('VOL+',true);s.tick(480);s.button('VOL+',false);s.tick(501);assert.equal(s.route,2);
});
test('Page 7 reset skips clear settings rather than requiring three taps',()=>{
 for(const trim of [false,true])for(const nudge of [false,true]){
  const s=new Simulator();s.page=7;s.current.offset=.2;s.tracks[0].nudge=nudge?.7:.5;if(trim)s.tracks[0].trim={head:1,tail:0};
  const expected=1+Number(trim)+Number(nudge);let taps=0;while(s.current.offset&&taps<4){s.pageTap(0);taps++;}assert.equal(taps,expected);
 }
 assert.match(card('reset-timing').steps[0].detail,/already clear are skipped/);
});
test('releasing PLAY ends the routing double-tap chain',()=>{
 const s=new Simulator();s.button('PLAY',true);s.button('VOL−',true);s.tick(50);s.button('VOL−',false);s.button('PLAY',false);assert.equal(s.route,1);
 s.tick(150);s.button('PLAY',true);s.button('VOL+',true);s.tick(251);assert.equal(s.route,2);
});

test('guide teaches simple adjustment and keeps one canonical loop reset',async()=>{
 const {related,hiddenIds}=await import('../web/field-guide/guide-layout.js');
 assert(hiddenIds.has('center-placement'));assert(hiddenIds.has('reset-timing'));
 assert(!Object.values(related).flat().includes('center-placement'));assert(!Object.values(related).flat().includes('reset-timing'));
 assert.match(card('placement').body,/Return it to center/);assert.match(card('placement').body,/TRACK button for exact center/);assert(!/T1 \+ T4/.test(card('placement').body));
 assert.equal(card('reset-window').title,'Reset loop controls');assert.equal(card('reset-window').steps.length,3);
 assert(!/trim|nudge/.test(card('beats-per-bar-v4').note));assert.match(card('nudge').note,/Nudge is relative/);
});
test('simplified placement walkthrough really centers its track',()=>{
 const c=card('placement'),s=new Scenario(c);for(let i=0;i<c.steps.length;i++){s.begin(i);s.advance(gestureSeconds(c,i));}assert.equal(s.sim.tracks[0].placement,.5);
});
test('canonical loop reset restores loops and timing while preserving audio and FX',()=>{
 const c=card('reset-window'),s=new Scenario(c);s.begin(0);s.sim.fx[1][3]=.7;s.sim.route=1;s.sim.current.beatsPerBar=7;s.sim.current.preset=3;s.sim.current.offset=.2;s.sim.tracks[0].trim={head:1,tail:0};s.sim.tracks[0].nudge=.7;const audio=JSON.stringify(s.sim.tracks[0].takes);
 for(let i=0;i<c.steps.length;i++){if(i)s.begin(i);s.advance(gestureSeconds(c,i));}
 assert.equal(s.sim.current.beatsPerBar,4);assert.equal(s.sim.current.preset,0);assert.equal(s.sim.current.offset,0);assert.equal(s.sim.tracks[0].trim,undefined);assert.equal(s.sim.tracks[0].nudge,.5);assert.equal(s.sim.fx[1][3],.7);assert.equal(s.sim.route,1);assert.equal(JSON.stringify(s.sim.tracks[0].takes),audio);
});
