import {renderTimeline,rockerName,rockerRows} from './geometry.mjs?v=558';
import {effectSettings} from './fx-state.mjs?v=552';
import {rockerPosition} from './panel-state.mjs?v=rocker279';
import {formatValue} from './value-format.mjs';
import {highlight,setReading} from './highlight.mjs';
// Values are device observations. No extrapolated playhead or remote writes.
const $=id=>document.getElementById(id),pct=formatValue,names=['Pitch','Wind','Bender','Battery','Sandpaper','Manual tape head','Magnet','Stash','Scissors/glue'];
const routes=['Input + tracks','Input','Tracks'];
const rockerLabels=[['Click size','Hold repeat','Pitch interval','Inertia'],['Forward speed','Wind-up','Reverse speed','Spin-down'],['Depth','Pull speed','Flutter','Recovery'],['Sag','Drain','Wow','Stalls'],['Dulling','Grit','Patches','Wear rate']];
const sec=['Filter damping','Chorus span',null,null,null,null,'Wah damping','Echo feedback','Phaser feedback',null,'Tremolo shape','Reverb damping',null,'Compression',null,null];
const shp=Array(16).fill(null);shp[10]='Stereo spread';
function tile(label,value){const n=document.createElement('div');n.className='state-value';const l=document.createElement('small');l.textContent=label;const v=document.createElement('strong');v.textContent=value;n.append(l,v);return n;}
function group(root,items){
 if(root.children.length!==items.length||items.some(([label],i)=>root.children[i]?.firstChild.textContent!==label)){root.replaceChildren(...items.map(([k,v])=>tile(k,v)));return;}
 let changed=false;
 items.forEach(([,value],i)=>{const node=root.children[i],text=String(value);if(node.lastChild.textContent!==text){changed=true;setReading(node.lastChild,text,node);}});
 if(changed)highlight(root.closest('.state-group')||root.closest('.value-card'));
}
export function playbackRegion(d){
 // Firmware Q8 endpoints are inclusive and relative to the whole audible loop.
 if(d.repeat && d.repeatLength)return [d.repeatStart/256,(d.repeatStart+d.repeatLength)/256];
 const div=Math.max(1,d.chopDivision), start=d.freeWindow?Math.min(d.windowStart,d.windowEnd)/256:0, width=d.freeWindow?(Math.abs(d.windowEnd-d.windowStart)+1)/256:1;
 return [start+width*d.chopOffset/div,start+width*(d.chopOffset+1)/div];
}
export function rockerAction(d){
 // Wear/Pitch do not populate the Bender/Wind action enum. Show their sampled
 // physical pull, not an inferred DSP action (wear can stay latched on release).
 if([0,3,4,6].includes(d.rocker)){
  const position=rockerPosition(d.panel);
  if(position)return position>0?'Pull down':'Pull up';
 }
 return ['Idle','Pull down','Pull up','Recover','Wind forward','Rewind','Forward cue','Reverse cue','Reverse held','Heads held','First cut marked','Cut joined'][d.rockerAction]??'Active';
}
export const effectiveReverse=(d,t)=>!!d.windowReverse!==!!t.reverse;
export const takeFormat=value=>value?'Mono':'Stereo';
export function renderDetails(s){
 let root=$('completeState');if(!root){root=document.createElement('section');root.id='completeState';root.innerHTML='<div id="performanceState" class="state-values"></div><div id="windowView" class="window-view" role="img"></div><div id="currentRocker" class="state-values"></div><div id="stateMore" class="state-groups"></div><p id="stateCoverage" class="value-foot"></p>';document.getElementById('state').append(root);}
 const d=s?.details;
 let fault=$('storageFault');if(!fault){fault=document.createElement('p');fault.id='storageFault';fault.setAttribute('role','alert');root.prepend(fault);}fault.hidden=!s?.storageFault;fault.textContent=s?.storageFault?'Saving stopped because of a storage error. Tracks shown empty are unavailable, not confirmed deleted. Keep the SP-1 powered and preserve diagnostics before recording again.':'';
 if(!d){
  group($('performanceState'),['Direction','Chop','Window','Repeat','Rocker','Recording','Speed','Loop'].map(label=>[label,'—']));
  $('windowView').replaceChildren();delete $('windowView').dataset.region;delete $('windowView').dataset.geometry;
  group($('currentRocker'),[]);
  renderStateGroups([
   ['Timing & trim',['Base speed','Beat grid','Beats per bar','Take preset','Grid offset','Trim start','Trim end','Repeat region']],
   ['Input & routing',['Input','USB activity','Audio input','Isolation','Stopped FX','FX destination','Recording FX']],
   ['Rocker',['Action']]
  ].map(([title,labels])=>[title,labels.map(label=>[label,'—'])]));
  $('stateCoverage').textContent=s?'Extended state unavailable on this firmware.':'Connect to read device state.';
  return;
 }
 // Fixed rocker defaults remain in telemetry for possible future editing.
 group($('currentRocker'),[]);
 const reversed=d.tracks.filter(t=>t.reverse).length;
 group($('performanceState'),[['Direction',`${d.windowReverse?'Window reverse':'Window forward'} · ${reversed} reversed ${s.heads?'heads':'tracks'}`],['Chop',d.chopDivision>1?`1/${d.chopDivision} · slice ${d.chopOffset+1}`:'Full loop'],['Window',d.freeWindow?`${pct(d.windowStart)} → ${pct(d.windowEnd)}`:'Full chop slice'],['Repeat',d.repeat?`1/${d.repeatDivision} · ${d.repeatLive?'active':'engaging'}`:'Off'],['Rocker',rockerName(d.rocker)+(d.rockerPage?' · page open':'')],['Recording',d.recordTrack>=0?`${d.bounce?'Bounce':'Record'} T${d.recordTrack+1}`:d.saving?'Saving':d.recordReady>=0?`Ready T${d.recordReady+1} · tap to record`:'Idle'],['Speed',`${d.effectiveSpeed.toFixed(2)}× now · ${d.targetSpeed.toFixed(2)}× target`],['Loop',`${d.fixed?'Fixed':'Variable'} · ${(d.loopSamples/48000).toFixed(2)} s`]]);
 const geometry=s.geometry?.song===s.song&&s.geometry.loopSamples===d.loopSamples?s.geometry:null;
 renderTimeline($('windowView'),d,geometry,playbackRegion(d));

 const sections=[
  ['Timing & trim',[
   ['Base speed',d.baseSpeed.toFixed(2)+'×'],['Beat grid',d.gridActive?`${d.gridBeats} beats · ${d.bpm.toFixed(1)} BPM`:'No grid'],
   ['Beats per bar',String(d.beatsPerBar)],['Take preset',d.takePreset?`${d.takePreset} bars`:'Off'],['Grid offset',`${d.gridOffset}/256 beat`],
   ['Trim start',`${(d.songTrimHead/48).toFixed(1)} ms`],['Trim end',`${(d.songTrimTail/48).toFixed(1)} ms`],
   ['Repeat region',d.repeat?`${(d.repeatStart/256*100).toFixed(1)}–${((d.repeatStart+d.repeatLength)/256*100).toFixed(1)}%`:'Off']]],
  ['Input & routing',[
   [d.version>=4?'Input':'Input monitor',d.monitorMuted?'Muted':'Audible'],['USB activity',d.usbAudio?'Audio':d.transfer?'Transfer':'Idle'],
   ...(d.version>=5?[['Audio input',d.inputSource===3?'USB + Bluetooth':d.inputSource===1?'Bluetooth':d.inputSource===2?'USB · Bluetooth waits for take to finish':d.usbAudio?'USB':'No Bluetooth audio selected']]:[]),
   ['Isolation',d.isolate?d.tracks.map((_,i)=>d.isolateMask&(1<<i)?`T${i+1}`:'').filter(Boolean).join(' '):'Off'],
   ['Stopped FX',['Keep running','Natural','All'][d.stopFx]??'Unknown'],
   ['FX destination',d.routes.every(route=>route===d.routes[0])?(routes[d.routes[0]]??'Unknown'):'Mixed · unexpected page routes'],
   ...(d.version>=4?[["Recording FX",d.routes.every(route=>route===1)?'Input FX print into takes':d.routes.every(route=>route===2)?'Input takes dry · track FX on playback':'Shared FX print through bounce']]:[])]],
  ['Rocker',[['Action',rockerAction(d)],...rockerRows(d,geometry)]]
 ];
 renderStateGroups(sections,rockerName(d.rocker));
 $('stateCoverage').textContent='';
}
function renderStateGroups(sections,name=''){
 const more=$('stateMore');
 if(!more.children.length){
  for(const [title] of sections){const section=document.createElement('section');section.className='state-group';const heading=document.createElement('h3');heading.textContent=title;const values=document.createElement('div');values.className='group-values';if(title==='Rocker'){const header=document.createElement('div');header.className='rocker-card-heading';const selected=document.createElement('span');selected.className='rocker-selected';header.append(heading,selected);section.append(header,values);}else section.append(heading,values);more.append(section);}

 }
 const selected=more.querySelector('.rocker-selected');
 selected.hidden=!name;
 if(selected.textContent!==name){selected.textContent=name;if(name)selected.setAttribute('aria-label','Selected rocker mode: '+name);else selected.removeAttribute('aria-label');}
 sections.forEach(([,items],i)=>group(more.children[i].lastElementChild,items));
}
export function pageExtras(s,page){
 let box=document.getElementById('pageExtras');if(!box){box=document.createElement('div');box.id='pageExtras';box.className='state-values';document.getElementById('pageLanes').after(box);}
 const lanes=[...document.querySelectorAll('#pageLanes .value-card')].map(card=>{let node=card.querySelector('.lane-settings');if(!node){node=document.createElement('div');node.className='lane-settings';card.append(node);}return node;});
 const d=s?.details;
 if(!d){lanes.forEach(node=>group(node,[]));group(box,[]);return;}
 if(page===3&&d.comb){
  const c=d.comb;
  lanes.forEach((node,i)=>{const sem=c.snapped?Math.round(c.tuning[i]*48/255):c.tuning[i]*48/255;const hz=55*2**(sem/12);group(node,[[c.snapped?'Snapped tuning':'Continuous tuning',hz.toFixed(1)+' Hz'],['Decay',pct(c.decay[i])],['Damping',pct(c.damping[i])]]);});
  group(box,[['Engine','Four-voice comb · reverb memory shared'],['Wet mix',pct(c.mix)],['Controls','Hold TRACK: own fader = tune; next = decay; next = damping; next = wet. Wrap after F4.'],['Type','T4: snapped → continuous → reverb → freeze']]);return;
 }
 if(page>4)lanes.forEach(node=>group(node,[]));
 let items=[];
 if(page<=4){
  const settings=Array.from({length:4},()=>[]);
  for(let i=0;i<4;i++){
   const k=d.layout?.[(page-1)*4+i]??((page-1)*4+i);
   if(k===13&&d.toneMode!=null)settings[i].push(['Tone',pct(s.fx[13])]);
   settings[i].push(...effectSettings(k,d,i));
  }
  lanes.forEach((node,i)=>group(node,settings[i]));
  // Routing is global and shown once in Input & routing.
 }else if(page===7){d.nudge.forEach((v,i)=>items.push([`T${i+1} nudge`,v?String(v-128):'Centered']));d.tracks.forEach((t,i)=>items.push([`T${i+1} own trim H / T`,`${t.trimHead} / ${t.trimTail} blocks`]));}
 else if(page===8)d.tracks.forEach((t,i)=>items.push([`T${i+1} recorded / next`,`${t.state===0?'—':takeFormat(t.format)} / ${takeFormat(t.nextFormat)}`]));
 group(box,items);
}

// One shared fold for all four tracks; preserve its state across live updates.
export function renderTrackDetails(s){
 let extra=document.getElementById('trackDetails');
 if(!extra){extra=document.createElement('details');extra.id='trackDetails';extra.className='track-extra';extra.innerHTML='<summary>Track details</summary><div class="track-detail-grid"></div>';document.getElementById('tracks').after(extra);
  for(let i=0;i<4;i++){const column=document.createElement('div');column.innerHTML=`<b class="track-detail-label">T${i+1}</b><div></div>`;extra.lastElementChild.append(column);}}
 extra.hidden=!s?.details;
 for(let i=0;i<4;i++){
  const root=document.getElementById('trackLevel'+i).closest('.mix-channel');
  if(!root.querySelector('.track-reading')){const n=document.createElement('div');n.className='track-reading';root.append(n);}
  const quick=root.querySelector('.track-reading'),body=extra.lastElementChild.children[i].lastElementChild,d=s?.details,t=d?.tracks[i];
  if(!t){quick.textContent='';body.replaceChildren();delete body.dataset.values;continue;}
  const duration=t.lengthSamples||t.lengthBlocks*t.samplesPerBlock;
  quick.textContent=t.state===0?'':`${effectiveReverse(d,t)?'Reverse':'Forward'} · ${(duration/48000).toFixed(2)} s`;
  group(body,[['Kept region cuts H / T',`${t.effectiveTrimHead} / ${t.effectiveTrimTail} blocks`],['Track cuts H / T',`${t.trimHead} / ${t.trimTail} blocks`],['Repeat',t.repeatBlocks?`${t.repeatBlocks} blocks @ ${t.repeatOffset}`:'Off'],['Head',(d.headMask&(1<<i))?`${pct(t.headPosition)} · source T${d.headSource+1}`:'Off'],['Recorded / next',`${t.state===0?'—':takeFormat(t.format)} / ${takeFormat(t.nextFormat)}`]]);
 }
}
