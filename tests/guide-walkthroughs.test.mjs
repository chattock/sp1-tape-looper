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
