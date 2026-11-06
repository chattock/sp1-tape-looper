import {FX,FX_CYCLES} from '../fx-state.mjs?v=610';
const bodies={1:'Move F2 for depth. Tap T2 to switch Chorus / Flanger. Lower F2 to turn it off.',3:'Move F4 for depth. Tap T4 to cycle Gallop / Halves / Slow gear. Slow gear fades into incoming notes. Lower F4 to turn it off.',4:'Move F1 for depth. Tap T1 to cycle Crush / Lo-fi / Radio. Lower F1 to turn it off.',11:'Move F4 for reverb. Tap T4 to freeze the current tail; tap again for Reverb. Lower F4 to turn it off.',14:'Move F3 for noise level. Tap T3 to cycle Cassette / Vinyl / Pink / Minidisc. Lower F3 to turn it off.',15:'Move F4 for depth. Tap T4 to cycle Wow + flutter / Wow / Flutter. Lower F4 to turn it off.',13:'Tap T2 to switch Tone / Compression. Move F2 through its saved value, then adjust. Center Tone for flat; lower Compression fully to turn it off. Both settings stay applied.'};
export const secondaryLessons=[['filter-damping-v4',0,'Filter damping'],['chorus-span-v4',1,'Chorus / flanger span'],['phaser-feedback-v4',8,'Phaser feedback']];
export function updateFxGuide(cards){
 for(let id=0;id<16;id++){
  const page=1+(id>>2),lane=1+(id&3),c=cards.find(c=>c.id===`fx-${page}-${lane}`),f=FX[id];if(!c)continue;
  c.title=f.name;c.gesture=`F${lane} · T${lane} ${f.tap}`;
  if(bodies[id])c.body=bodies[id];
  if(f.secondary)c.note=`Hold T${lane} and move F${lane} for ${f.secondary.toLowerCase()}.`+(f.shape?` Hold T${lane} and move F${lane===4?1:lane+1} for ${f.shape.toLowerCase()}.`:'')+(f.period!==undefined?` Tap a rate with FN + T${lane}, at least twice.`:'');
  if(!f.key)continue;
  const firstTap=c.steps.findIndex(s=>s.tap?.includes('T'+lane));if(firstTap<0)continue;
  const model=c.steps[firstTap];c.steps=c.steps.slice(0,firstTap);
  const choices=f.types.slice(0,FX_CYCLES[page+':'+lane]);
  for(let n=1;n<=choices.length;n++){const type=n%choices.length;c.steps.push({...structuredClone(model),label:`Tap T${lane}: ${choices[type]}`,detail:`Tap T${lane} to select ${choices[type]}. The fader keeps its level.`,hold:[],tap:['T'+lane],faders:{},pattern:'lane-count',count:id===15?type:type+1,lane,amount:.9,page,duration:1.25});}
  c.steps.push({...structuredClone(model),label:`Lower F${lane} to turn it off`,detail:'Keep the selected type; lower the effect level to zero.',hold:[],tap:[],faders:{[lane]:0},pattern:'lane-depth',lane,amount:0,page,duration:1.2});
  if(id===14)for(const step of c.steps)if(step.faders?.[lane]!==undefined)step.amount=step.faders[lane];
 }
 const tone=cards.find(c=>c.id==='fx-4-2');
 tone.note='Both settings stay applied when you switch. Move F2 through its saved value to take control.';
 tone.steps=[
 {label:'Open Page 4',detail:'Hold FN + T4, then release both.',hold:['FN','T4'],tap:[],page:4,duration:2},
 {label:'Tone',detail:'Move F2 through the saved tone value. Center is flat.',hold:[],tap:[],faders:{2:.5},page:4,lane:2,amount:.5,pattern:'lane-depth',duration:2.5},
 {label:'Select Compression',detail:'Tap T2. Two flashes confirm Compression; tone stays set.',hold:[],tap:['T2'],page:4,lane:2,count:2,pattern:'lane-count',duration:2.5},
 {label:'Add compression',detail:'Move F2 to the bottom to pick up, then raise it for more compression.',hold:[],tap:[],faders:{2:.65},page:4,lane:2,amount:.65,pattern:'lane-depth',duration:3},
 {label:'Turn compression off',detail:'Lower F2 fully. Tap T2 again to return to Tone.',hold:[],tap:[],faders:{2:0},page:4,lane:2,amount:0,pattern:'lane-depth',duration:2.5}];
 for(const [id,effect,title] of secondaryLessons){
  const page=1+(effect>>2),lane=1+(effect&3),template=cards.find(c=>c.id==='echo-feedback');const c=structuredClone(template);c.id=id;c.title=title;c.group=`Page ${page} / Secondary controls`;c.gesture=`Page ${page}: hold T${lane} and move F${lane}`;c.body=`Hold T${lane} and move F${lane} to adjust ${title.toLowerCase()} without changing the main level.`;c.note='Release TRACK to return to the primary control; pick up its saved level.';c.secondaryTarget=[page,lane];
  c.steps=template.steps.map((step,index)=>{
   const x=structuredClone(step);
   x.page=page;
   x.lane=lane;
   if(index===0){
    x.label=`Hold FN + T${page}`;
    x.hold=['FN','T'+page];
   }else{
    x.label=x.label.replaceAll('Page 2','Page '+page).replaceAll('T4','T'+lane).replaceAll('F4','F'+lane).replace('Change echo feedback','Adjust '+title.toLowerCase());
    x.hold=x.hold.map(k=>k==='T4'?'T'+lane:k);
   }
   x.detail=x.detail.replaceAll('Page 2','Page '+page).replace('Change the amount fed back into the echo.',`Hold T${lane} and move F${lane} to adjust ${title.toLowerCase()}.`);
   if(x.pattern==='page2')x.pattern='page'+page;
   if(x.faders?.[4]!==undefined)x.faders={[lane]:x.faders[4]};
   return x;
  });cards.push(c);
 }
}

export function finishGuideAudit(cards){
 const removed=cards.findIndex(c=>c.id==='track-trim-reset-v4');if(removed>=0)cards.splice(removed,1);
 const walk=cards.find(c=>c.id==='walk-pages');Object.assign(walk,{title:'Move between pages',gesture:'Page open · VOL− / VOL+',body:'With any page open, press VOL− or VOL+ to move between Pages 1–8. Tap FN to close.',note:'The same controls move between rocker modes when the rocker page is open.'});
 walk.steps=[{label:'Open Page 4',detail:'Hold FN + T4, then release both.',hold:['FN','T4'],tap:[],duration:1.4},{label:'Press VOL+',detail:'Page 5 opens. No FN needed.',hold:[],tap:['VOL+'],page:4,duration:1.4},{label:'Page 5',detail:'Use VOL− / VOL+ to move between pages. Tap FN to close.',hold:[],tap:[],page:5,duration:1.4}];
 for(const card of cards)for(const step of card.steps){
  if(step.label==='Hold FN; tap VOL+'){step.label='Press VOL+';step.hold=[];}
  if(step.detail?.endsWith('Shown slowed down.'))step.detail=step.detail.replace(' Shown slowed down.','');
  if(card.id==='round-grid'&&['snap','no-action'].includes(step.pattern))step.confirmationReplay=true;
 }
 const repeatOctave=cards.find(c=>c.id==='repeat-octave');
 repeatOctave.steps[1].label='Click ROCKER DOWN';
 repeatOctave.steps[1].rockDirection='DOWN';
 const stopPage=cards.find(c=>c.id==='stop-page');
 stopPage.steps[0].detail='With Page 2 open and Track 2 recording the copy, keep PLAY held.';
 const placement=cards.find(c=>c.id==='center-placement');
 placement.note='The all-track shortcut also resets FX.';
 const length=cards.find(c=>c.id==='take-length');
 length.body='On Page 7, F2 selects the take length. F3 selects 3, 4, 5 or 7 beats per bar, from bottom to top. Move through the saved value to take control.';
 length.note='Beats per bar is saved per song; the default is 4. Variable mode counts bars and needs a grid. Fixed mode counts base loops and needs a first take.';
 const preset=cards.find(c=>c.id==='preset');
 preset.steps[4].seconds=1.25;
 const countdown=cards.find(c=>c.id==='countdown');
 countdown.steps[0].seconds=8.75;
 const timing=cards.find(c=>c.id==='reset-timing');Object.assign(timing,{gesture:'Hold FN + PLAY · press VOL−',body:'Reset the window, timing offsets and track trims. Take length returns to Off.',note:'FN + T1 + T4 resets FX; it does not reset the timing page.'});timing.steps=timing.steps.map(step=>step.tap?.includes('T4')?{...step,label:'Reset the timing',detail:'Hold FN + PLAY, then press VOL−.',hold:['FN','PLAY'],tap:['VOL−']}:step);
 const reset=cards.find(c=>c.id==='reset-fx');reset.note='The four TRACK lights briefly turn solid. Effects, EQ, rates, types and routing return to defaults. Monitor mute stays as set.';
 const tape=cards.find(c=>c.id==='restore-tape-v4');tape.note='The four TRACK lights briefly turn solid. Recordings and effect-page settings stay intact.';
 const grid=cards.find(c=>c.id==='clear-grid');grid.note='The four TRACK lights briefly turn solid. Your recorded audio stays in place.';

 const secondary=cards.find(c=>c.id==='secondaries');secondary.body='Hold a lane’s TRACK button and move its fader: P1 filter damping / chorus span; P2 wah damping / echo feedback; P3 phaser feedback / tremolo shape / reverb damping. For tremolo stereo spread, hold T3 and move F4.';
 const overdub=cards.find(c=>c.id==='replace');overdub.note='The earlier take stays saved in this track’s lane. TRACK + ROCKER DOWN finds it after the new recording is saved. Tap FN to cancel preparation.';
 for(const step of overdub.steps)if(/replaces the selected take/.test(step.detail))step.detail='The new result is saved as another take; the earlier audio remains available.';
 const wind=cards.find(c=>c.id==='wind-v4');wind.note='With the WIND page open, hold TRACK + ROCKER UP/DOWN: T1 0.5×, T2 1.5×, T3 3×, T4 4×. Release to return to playback speed. Double-tap TRACK to latch that winding speed for bare-rocker use, including after leaving the page.';
}
