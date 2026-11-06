import {effectInfo} from './fx-state.mjs?v=552';
import {renderTakeLanes} from './take-lanes.mjs?v=523';
import {formatValue} from './value-format.mjs';
import {highlight,clearHighlights,setReading} from './highlight.mjs';
import {renderDetails,pageExtras,renderTrackDetails} from './details.mjs?v=558';
import {DeviceActivity} from './activity.mjs?v=552';
const $=id=>document.getElementById(id);
function meter(id,label,max){
 const node=document.createElement('div');node.id=id;node.className='level-meter';
 node.setAttribute('role','meter');node.setAttribute('aria-label',label);
 node.setAttribute('aria-valuemin','0');node.setAttribute('aria-valuemax','100');
 node.dataset.max=max;node.append(document.createElement('span'));const marker=document.createElement('i');marker.className='pickup-position';marker.hidden=true;marker.setAttribute('aria-hidden','true');node.append(marker);return node;
}
function setMeter(node,value){
 if(node._reading===value)return;node._reading=value;
 node.hidden=value===null;
 if(value===null){node.removeAttribute('aria-valuenow');return;}
 node.setAttribute('aria-valuenow',String((value/Number(node.dataset.max)*100).toFixed(1)));
 node.firstChild.style.transform=`scaleY(${Math.max(0,Math.min(1,value/Number(node.dataset.max)))})`;
}
// Match the controller's ADC → Q8 conversion, including endpoint clamping.
function showPickup(node,s,i,waiting,value){
 const key=`${!!waiting}/${s?.faderValid}/${s?.faders[i]}/${value}`;if(node._pickup===key)return;node._pickup=key;
 const marker=node.querySelector('.pickup-position');
 const valid=waiting&&s&&(s.faderValid&(1<<i))&&value!==null;
 marker.hidden=!valid;node.classList.toggle('pickup-pending',!!valid);
 if(!valid){node.removeAttribute('title');node.removeAttribute('aria-valuetext');return;}
 const max=Number(node.dataset.max),physical=Math.min(max,Math.floor(s.faders[i]*256/3700));
 marker.style.bottom=`${physical/max*100}%`;
 const direction=physical<value?'up':physical>value?'down':'through the saved level';
 const format=v=>max===256?Math.round(v/max*100)+'%':formatValue(v,max);
 const hint=`Pickup: move fader ${direction}. Physical ${format(physical)}; saved ${format(value)}.`;
 node.title=hint;node.setAttribute('aria-valuetext',hint);
}
export const pages=[
 ['Filter · modulation',['Filter','Chorus','Distortion','Gate'],['T1 · bypass','T2 · dry','T3 · type','T4 · pattern']],
 ['Texture · delay',['Bitcrush','Ring mod','Auto-wah','Echo'],['T1 · off','T2 · off','T3 · off','T4 · division']],
 ['Motion · space',['Phaser','Sweep','Tremolo','Reverb'],['T1 · division','T2 · division','T3 · division','T4 · dry']],
 ['Tape character',['Tape drive','Tone','Noise','Wobble'],['T1 · off','T2 · flat','T3 · noise source','T4 · wobble type']],
 ['Master EQ',['100 Hz','350 Hz','1.6 kHz','4 kHz'],['T1 · flat','T2 · flat','T3 · flat','T4 · flat']],
 ['Stereo placement',['Track 1','Track 2','Track 3','Track 4'],['T1 · center','T2 · center','T3 · center','T4 · center']],
 ['Timing · trim',['Grid offset','Take length','Beats per bar','Song trim'],['F1 · earlier / later','F2 · off / 1 / 2 / 4 / 8','F3 · 3 / 4 / 5 / 7','F4 · trim']],
 ['Next take format',['Track 1 level','Track 2 level','Track 3 level','Track 4 level'],['T1 · mono / stereo','T2 · mono / stereo','T3 · mono / stereo','T4 · mono / stereo']]
];
// Deferred to the next release; retain saved-layout display support.
const LAYOUT_CUSTOMIZATION_ENABLED=false;
let snapshot=null,selected=1,showingTracks=true,following=true,layoutDraft=null,layoutAdapter=null,layoutEditable=false,layoutSaving=false;
export function configureLayoutEditor(adapter){layoutAdapter=adapter;}
export function setLayoutEditable(value){layoutEditable=LAYOUT_CUSTOMIZATION_ENABLED&&value;if(!layoutEditable)layoutDraft=null;if($("editLayout"))renderPage();}
const activity=new DeviceActivity();
function clearFocus(){clearHighlights();}
export function initExplorer(){
 $('explorer').innerHTML=`<section id="page-map" class="value-pages"><div class="page-tools"><span id="pageContext" class="live-badge">Not connected</span><label class="follow"><input id="followPage" type="checkbox" checked> Follow device</label></div><div id="pageTabs" class="page-tabs" role="group" aria-label="Browse control page"></div><div class="page-title"><div><small id="pageNumber"></small><h3 id="pageTitle"></h3></div><button id="editLayout" class="quiet-action" hidden disabled>Customize layout</button></div><div id="layoutTools" hidden><p>Choose an effect to swap positions. Changes stay here until you save.</p><div class="layout-actions"><button id="layoutApply">Save layout</button><button id="layoutCancel" class="quiet-action">Cancel</button><button id="layoutDefaults" class="quiet-action">Default layout</button></div></div><p id="layoutResult" role="status"></p><p id="pageEntry" class="gesture"></p><div id="pageLanes" class="value-grid"></div><p class="pickup-legend">Amber mark: fader position waiting to meet the saved level.</p><p id="pageFoot" class="value-foot"></p></section>`;
 $('controlNavigation').append($('page-map').querySelector('.page-tools'),$('pageTabs'));
 const tracksButton=document.createElement('button');tracksButton.id='browseTracks';tracksButton.className='page-tab';tracksButton.textContent='Tracks';tracksButton.onclick=()=>browse(0);$('pageTabs').append(tracksButton);
 pages.forEach((p,i)=>{const b=document.createElement('button');b.className='page-tab';b.textContent=i+1;b.setAttribute('aria-label',`Page ${i+1}: ${p[0]}`);b.dataset.page=i+1;b.onclick=()=>browse(i+1);$('pageTabs').append(b);});
 $('editLayout').onclick=async()=>{if(!layoutEditable)return;try{layoutDraft=Array.from(await layoutAdapter.load());$('layoutResult').textContent='';renderPage();}catch(e){$('layoutResult').textContent=e.message;}};
 $('layoutCancel').onclick=()=>{layoutDraft=null;$('layoutResult').textContent='';renderPage();};
 $('layoutApply').onclick=async()=>{if(!layoutEditable||!layoutDraft||layoutSaving)return;layoutSaving=true;renderPage();try{const applied=await layoutAdapter.save(layoutDraft);if(snapshot?.details)snapshot.details.layout=applied;layoutDraft=null;$('layoutResult').textContent='Layout saved on SP-1.';}catch(e){$('layoutResult').textContent=e.message;}finally{layoutSaving=false;renderPage();}};
 $('layoutDefaults').onclick=()=>{if(!layoutEditable)return;layoutDraft=Array.from({length:16},(_,i)=>i);renderPage();};
 $('followPage').onchange=()=>{following=$('followPage').checked;clearFocus();if(following){showingTracks=!snapshot?.page;if(snapshot?.page)selected=snapshot.page;}renderPage();};renderPage();
}
function browse(page){showingTracks=page===0;if(page)selected=page;clearFocus();renderPage();}
function syncControlView(){
 $('trackView').hidden=!showingTracks;$('page-map').hidden=showingTracks;
 $('browseTracks').setAttribute('aria-pressed',String(showingTracks));$('browseTracks').disabled=!!layoutDraft;
 $('browseTracks').classList.toggle('device-page',!!snapshot&&!snapshot.page);
}
function renderPage(){const p=pages[selected-1];
 $('editLayout').hidden=!LAYOUT_CUSTOMIZATION_ENABLED||selected>4||!!layoutDraft;$('editLayout').disabled=!layoutEditable;$('editLayout').title=layoutEditable?'':'Connect USB or an upgraded Bluetooth radio to edit';
 $('layoutTools').hidden=!layoutDraft;$('layoutApply').disabled=layoutSaving;$('layoutApply').textContent=layoutSaving?'Saving…':'Save layout';$('layoutCancel').disabled=layoutSaving;$('layoutDefaults').disabled=layoutSaving;
 $('followPage').disabled=!!layoutDraft;$('pageNumber').textContent='PAGE '+selected;$('pageTitle').textContent=p[0];
 $('pageEntry').textContent=selected<=4?`Hold FN + T${selected} to open · tap FN to close`:`Page 4 → FN + VOL+ × ${selected-4} · tap FN to close`;
 $('pageLanes').replaceChildren(...p[1].map((name,i)=>{const card=document.createElement('article');card.className='value-card';const label=document.createElement('h4');label.textContent=name;const f=document.createElement('span');f.className='value-control';f.textContent='F'+(i+1);const val=document.createElement('strong');val.id='laneValue'+i;val.className='parameter-value';const gauge=meter('laneMeter'+i,name+' value',selected===8?256:255);const cue=document.createElement('p');cue.className='gesture';cue.textContent=p[2][i];const pickup=document.createElement('p');pickup.id='lanePickup'+i;pickup.className='pickup-hint';pickup.hidden=true;card.append(f,label,val,gauge,cue,pickup);return card;}));
 if(layoutDraft){$('pageLanes').replaceChildren(...Array.from({length:4},(_,i)=>{const slot=(selected-1)*4+i,card=document.createElement('article');card.className='value-card layout-lane';const label=document.createElement('label');label.textContent='F'+(i+1);const select=document.createElement('select');select.setAttribute('aria-label',`Page ${selected} fader ${i+1} effect`);pages.slice(0,4).flatMap(p=>p[1]).forEach((name,id)=>{const option=document.createElement('option');option.value=id;option.textContent=name;select.append(option);});select.value=layoutDraft[slot];select.disabled=layoutSaving;select.onchange=()=>{const other=layoutDraft.indexOf(+select.value);[layoutDraft[slot],layoutDraft[other]]=[layoutDraft[other],layoutDraft[slot]];renderPage();};label.append(select);card.append(label);return card;}));}
 $('pageFoot').textContent=selected<=4?'Values · 0–100. Pattern, bypass and secondary settings are not reported.':selected===8?'Fader levels are live. Next-take mono/stereo selections are not reported.':selected===7?'Timing values are not reported. Hold any T + its fader to nudge that track.':'These values are not reported by the current firmware.';updateValues();
}
function updateValues(){
 syncControlView();
 [...$('pageTabs').querySelectorAll('[data-page]')].forEach((b,i)=>{b.setAttribute('aria-pressed',String(!showingTracks&&selected===i+1));b.disabled=!!layoutDraft&&(i>3||layoutSaving);});
 if(layoutDraft){$('pageEntry').textContent='Stop playback and close device control pages before saving. Gestures move with each effect.';$('pageFoot').textContent='Sound processing order stays the same.';return;}
 pageExtras(snapshot,selected);
 const d=snapshot?.details;
 $('pageFoot').textContent=d?'':'Extended settings unavailable on this firmware.';
 $('pageContext').textContent=snapshot?(snapshot.page?`Device · Page ${snapshot.page}`:snapshot.heads?'Device · four heads':'Device · tracks'):'Not connected';
 $('pageEntry').textContent=(selected<=4?`Hold FN + T${selected} to open`:`Page 4 → FN + VOL+ × ${selected-4}`)+(snapshot?.page===selected?' · tap FN to close':'');
 [...$('pageTabs').querySelectorAll('[data-page]')].forEach((b,i)=>{b.setAttribute('aria-pressed',String(!showingTracks&&selected===i+1));b.classList.toggle('device-page',snapshot?.page===i+1);});
 if(selected<=4){for(let i=0;i<4;i++){const id=d?.layout?.[(selected-1)*4+i]??((selected-1)*4+i),card=$('laneValue'+i).parentElement;const info=effectInfo(id,d);card.querySelector('h4').textContent=info.name;card.querySelector('.gesture').textContent='T'+(i+1)+' · '+info.tap;card.querySelector('[role=meter]').setAttribute('aria-label',info.name+' value');}const custom=d?.layout?.some((id,i)=>id!==i);$('pageTitle').textContent=custom?'Custom FX page '+selected:pages[selected-1][0];}
 if(selected===3&&d?.comb){$('pageTitle').textContent='Four-voice resonator';for(let i=0;i<4;i++){const card=$('laneValue'+i).parentElement;card.querySelector('h4').textContent='Voice '+(i+1);card.querySelector('.gesture').textContent=i===3?'T4 · change engine':'T'+(i+1)+' · mute voice';}$('pageFoot').textContent='Reverb, phaser, sweep and tremolo resume when you switch back. Controls are session settings.';}
 for(let i=0;i<4;i++){const value=(selected===3&&d?.comb)?d.comb.levels[i]:snapshot?(selected<=4?((d?.layout?.[(selected-1)*4+i]??((selected-1)*4+i))===13&&d?.toneMode===1?d.secondary[13]:snapshot.fx[d?.layout?.[(selected-1)*4+i]??((selected-1)*4+i)]):selected===8?snapshot.levels[i]:d?(selected===5?d.eq[i]:selected===6?d.placement[i]:null):null):null;const n=$('laneValue'+i),m=$('laneMeter'+i);let label=value===null?'—':selected===8?Math.round(value/256*100)+'%':formatValue(value);if(d&&selected===7)label=[`${d.gridOffset}/256 beat`,d.takePreset?`${d.takePreset} bars`:'Off',String(d.beatsPerBar),`${(d.songTrimHead/48).toFixed(1)} / ${(d.songTrimTail/48).toFixed(1)} ms`][i];setReading(n,label,n.parentElement,selected===7?0:7/256*100);setMeter(m,value);showPickup(m,snapshot,i,snapshot?.page===selected&&(selected<=7?!!(snapshot.pagePickup&(1<<i)):selected===8&&!!(snapshot.trackPickup&(1<<i))),value);const pickup=$('lanePickup'+i);pickup.hidden=!(snapshot?.page===selected&&selected<=7&&(snapshot.pagePickup&(1<<i)));pickup.textContent=pickup.hidden?'':value===null?'Pickup pending':`Pick up ${formatValue(value)}`;n.parentElement.classList.toggle('waiting',value===null);}
}
export function updateExplorer(s){
 const change=activity.update(s);snapshot=s;renderDetails(s);
 if(!s)clearFocus();
 // Browsing lasts until meaningful device activity, not the next polling snapshot.
 const followChange=!layoutDraft&&following&&change;
 if(followChange)showingTracks=!change.page;
 const next=followChange&&change.page?change.page:selected;
 if(next!==selected){selected=next;renderPage();}else updateValues();
 if(layoutDraft||!following||!change)return;
 const nodes=change.lanes.map(i=>change.page?$('laneValue'+i)?.parentElement:$('trackLevel'+i)?.closest('.mix-channel'));
 nodes.forEach(highlight);
}
export function renderMix(s){
 if(!$('tracks').children.length)for(let i=0;i<4;i++){const row=document.createElement('div');row.className='mix-channel';row.innerHTML=`<div class="mix-top"><b>T${i+1}</b><strong id="trackLevel${i}">—</strong></div><span id="trackState${i}" class="track-state">—</span>`;row.insertBefore(meter('trackMeter'+i,`Track ${i+1} level`,256),row.lastChild);$('tracks').append(row);}
 for(let i=0;i<4;i++){setReading($('trackLevel'+i),s?Math.round(s.levels[i]/256*100)+'%':'—',$('trackLevel'+i).closest('.mix-channel'),7/256*100);const m=$('trackMeter'+i);setMeter(m,s?s.levels[i]:null);showPickup(m,s,i,!!(s?.trackPickup&(1<<i)),s?s.levels[i]:null);setReading($('trackState'+i),s?(s.details?['Empty','Armed','Recording','Saving','Loaded'][s.details.tracks[i].state]+((s.muted&(1<<i))?' · muted':''):!(s.loaded&(1<<i))?'Empty':s.muted&(1<<i)?'Muted':'Loaded')+((s.trackPickup&(1<<i))?' · pickup':''):'—',$('trackState'+i).closest('.mix-channel'));}
 renderTrackDetails(s);
 renderTakeLanes(s);
}
