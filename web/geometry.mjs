// Read-only companion geometry. Coordinates are snapshots, never a predicted clock.
export function decodeGeometry(bytes){
 if(bytes.length!==384||String.fromCharCode(...bytes.slice(0,4))!=='GEO1')throw Error('Invalid geometry reply');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=o=>v.getUint32(o,true),w=o=>v.getUint16(o,true);
 if(bytes[4]>=16||bytes[5]>8||bytes[6]>15||bytes[7]>1||bytes[24]>15)throw Error('Invalid geometry bounds');
 const tracks=Array.from({length:4},(_,i)=>{const o=32+88*i,original=u(o),kept=u(o+4),depth=bytes[o+8],count=bytes[o+9];if(depth>8||count>9||kept>original||bytes[o+10]>1||bytes[o+11]>4)throw Error('Invalid track geometry');const spans=Array.from({length:count},(_,j)=>{const a=w(o+48+4*j),b=w(o+50+4*j);if(b<a)throw Error('Invalid retained span');return[a/65535,b/65535]});return{original,kept,depth,active:!!bytes[o+10],state:bytes[o+11],samplesPerBlock:w(o+12),gaps:bytes.slice(o+16,o+48),spans};});
 return{song:bytes[4]+1,cuts:bytes[5],pendingMask:bytes[6],grid:!!bytes[7],loopSamples:u(8),position:u(12),gridOrigin:u(16),mark:u(20),magnetHeldMask:bytes[24],magnetStart:bytes[25],magnetCells:bytes[26]+1,gridBeats:bytes[27],windSpeed:u(28)/65536,tracks};
}
const modeNames={0:'Pitch',1:'Wind',2:'Bender',3:'Battery',4:'Sandpaper',5:'Manual tape head',6:'Magnet',8:'Scissors/glue'};
export const rockerName=id=>modeNames[id]||'Unavailable';
export function rockerRows(d,g){
 const rows=[],tracks=mask=>[0,1,2,3].filter(i=>mask&(1<<i)).map(i=>'T'+(i+1)).join(' ')||'None';
 switch(d.rocker){
 case 0:rows.push(['Target speed',d.targetSpeed.toFixed(2)+'×'],['Speed control',d.rockerOwnsSpeed?'Active':'Idle']);break;
 case 1:rows.push(['Direction',d.rewindMask?'Rewind':'Forward'],['Release speed',d.baseSpeed.toFixed(2)+'×']);break;
 case 2:rows.push(['Speed bend',((d.effectiveSpeed-d.baseSpeed)>=0?'+':'')+(d.effectiveSpeed-d.baseSpeed).toFixed(2)+'×'],['Recovery',d.rockerAction===3?'Returning':'Idle']);break;
 case 3:case 4:rows.push(['Speed control',d.rockerOwnsSpeed?'Active':'Idle']);break;
 case 5:rows.push(...d.tracks.map((t,i)=>['T'+(i+1)+' head',(t.headPosition/255*100).toFixed(1)+'%']));break;
 case 6:rows.push(['Held tracks',g?tracks(g.magnetHeldMask):'Not reported']);break;
 }
 // These observations remain relevant after closing the page or selecting a
 // different slot. Never infer "clear" from the currently selected mode.
 rows.push(['WIND speed',g?g.windSpeed+'×':'Not reported'],
  ['Magnet',d.magnet?.held?'Erasing':d.magnet?.active?'Gaps active':d.magnet?'Clear':'Not reported'],
  ['Magnet undo',String(d.magnet?.undo??'—')],['Scissors cuts',g?String(g.cuts):'Not reported'],
  ['First cut mark',g?(g.pendingMask?tracks(g.pendingMask):'None'):'Not reported'],
  ['Battery wear',(d.batteryWear*100).toFixed(1)+'%'],['Sandpaper wear',(d.sandWear*100).toFixed(1)+'%']);
 if(d.headMask||d.rocker===5)rows.push(['Heads',tracks(d.headMask)],['Head source','T'+(d.headSource+1)]);
 if(g)g.tracks.forEach((t,i)=>{if(t.depth)rows.push(['T'+(i+1)+' cuts',`${t.depth} · ${(t.kept/Math.max(1,t.original)*100).toFixed(1)}% kept${t.active?'':' · suspended'}`]);});
 return rows;
}

export function gridTicks(d,g){
 if(!d.gridActive||!d.loopSamples)return[];const n=g?.gridBeats||d.gridBeats;if(!n)return[];const origin=g?g.gridOrigin/d.loopSamples:0;const stride=Math.max(1,Math.ceil(n/128)),bpb=d.beatsPerBar||4;
 return Array.from({length:Math.ceil(n/stride)},(_,i)=>{const beat=i*stride;return{at:((origin+beat/n)%1+1)%1,downbeat:beat%bpb===0,beat:beat+1}});
}
const svgNS='http://www.w3.org/2000/svg';
function el(tag,attrs={},text){const n=document.createElementNS(svgNS,tag);for(const[k,v]of Object.entries(attrs))n.setAttribute(k,String(v));if(text!==undefined)n.textContent=text;return n;}
function gaps(mask,cells){const out=[];let begin=-1;for(let i=0;i<=cells;i++){const on=i<cells&&!!(mask[i>>3]&(1<<(i&7)));if(on&&begin<0)begin=i;if(!on&&begin>=0){out.push([begin/cells,i/cells]);begin=-1}}return out;}
// All rows share a sample scale. Retained spans are joined in playback order;
// Magnet cells are intersected in original tape coordinates before joining.
export function trackTimeline(t,loopSamples,cells){
 const original=t.original*t.samplesPerBlock, kept=(t.depth&&t.active?t.kept:t.original)*t.samplesPerBlock;
 const spans=t.depth&&t.active?t.spans:[[0,1]],ranges=[],joins=[];let cursor=0;
 const total=spans.reduce((n,[a,b])=>n+b-a,0),factor=total?kept/total:0;
 const period=t.samplesPerBlock?Math.floor((loopSamples+t.samplesPerBlock/2)/t.samplesPerBlock)*t.samplesPerBlock:loopSamples;
 const muted=gaps(t.gaps,cells).map(([a,b])=>[Math.ceil(a*period/t.samplesPerBlock)*t.samplesPerBlock,Math.ceil(b*period/t.samplesPerBlock)*t.samplesPerBlock]);
 for(const[a,b]of spans){const from=a*original,to=b*original,len=(b-a)*factor;
  if(cursor>0)joins.push(cursor);
  for(const[x,y]of muted){const lo=Math.max(from,x),hi=Math.min(to,y);if(hi>lo&&to>from)ranges.push([cursor+(lo-from)/(to-from)*len,cursor+(hi-from)/(to-from)*len]);}
  cursor+=len;
 }
 return{original,kept,joins,ranges};
}
export function renderTimeline(root,d,g,region){
 const signature=JSON.stringify([d.gridActive,d.gridBeats,d.gridOffset,d.beatsPerBar,d.loopSamples,d.position,d.headMask,d.headSource,d.tracks.map(t=>[t.headPosition,t.state]),d.magnet,d.rocker,d.rockerAction,d.windowReverse,region,g]);if(root.dataset.geometry===signature)return;root.dataset.geometry=signature;root.className='window-view timeline';root.replaceChildren();
 const svg=el('svg',{viewBox:'0 0 1000 42',preserveAspectRatio:'none','aria-hidden':true});
 svg.append(el('rect',{x:0,y:13,width:1000,height:16,rx:4,class:'timeline-base'}));
 const[a,b]=region.map(x=>Math.max(0,Math.min(1,x)));svg.append(el('rect',{x:Math.min(a,b)*1000,y:13,width:Math.max(.2,Math.abs(b-a)*1000),height:16,rx:4,class:d.windowReverse?'timeline-window reverse':'timeline-window'}));
 for(const t of gridTicks(d,g))svg.append(el('line',{x1:t.at*1000,x2:t.at*1000,y1:t.downbeat?3:9,y2:32,class:t.downbeat?'timeline-beat downbeat':'timeline-beat'}));
 if(d.loopSamples){const pos=(d.position%d.loopSamples)/d.loopSamples;svg.append(el('line',{x1:pos*1000,x2:pos*1000,y1:7,y2:35,class:'timeline-position'}));}
 if(g?.pendingMask&&d.loopSamples){const x=(g.mark%d.loopSamples)/d.loopSamples*1000;svg.append(el('line',{x1:x,x2:x,y1:1,y2:40,class:'timeline-mark'}));}
 if(d.headMask)for(let i=0;i<4;i++)if(d.headMask&(1<<i)){const x=d.tracks[i].headPosition/255*1000;const marker=el('path',{d:`M ${x} 4 l -4 -4 h 8 z`,class:'timeline-head'});marker.append(el('title',{},'Head '+(i+1)+' · source T'+(d.headSource+1)));svg.append(marker);}
 root.append(svg);const caption=document.createElement('div');caption.className='timeline-caption';caption.textContent=[d.repeat?'Beat repeat':d.chopDivision>1?'1/'+d.chopDivision+' chop':'Full length',d.gridActive?`${g?.gridBeats||d.gridBeats} beats · ${d.bpm.toFixed(1)} BPM${g?'':' · spacing only'}`:''].filter(Boolean).join(' · ');root.append(caption);
 const showMaps=g&&(g.cuts||d.magnet?.active||g.magnetHeldMask||d.rocker===6||d.rocker===8);
 const rawLoop=d.loopSamples+(d.songTrimHead||0)+(d.songTrimTail||0);
 const layouts=showMaps?g.tracks.map(t=>trackTimeline(t,rawLoop,g.magnetCells)):[];
 const scale=Math.max(d.loopSamples,...layouts.map(t=>t.original),1);
 // The transport and track bars use the same start and seconds-per-pixel.
 svg.setAttribute('viewBox',`0 0 ${1000*scale/Math.max(1,d.loopSamples)} 42`);
 if(showMaps)g.tracks.forEach((t,i)=>{if(!t.original&&!t.depth)return;const data=layouts[i],row=document.createElement('div');row.className='timeline-track';const label=document.createElement('small');label.textContent='T'+(i+1);row.append(label);
 const seconds=n=>(n/48000).toFixed(2)+' s';
 const map=el('svg',{viewBox:'0 0 1000 18',preserveAspectRatio:'none','aria-label':`Track ${i+1}: ${seconds(data.kept)}${t.depth?`, originally ${seconds(data.original)}, ${t.depth} cut${t.depth===1?'':'s'}${t.active?'':', suspended'}`:''}`,role:'img'});
 map.append(el('rect',{x:0,y:2,width:data.original/scale*1000,height:14,rx:3,class:'timeline-base'}));
 map.append(el('rect',{x:0,y:2,width:data.kept/scale*1000,height:14,rx:3,class:'timeline-kept'}));
 for(const[a,b]of data.ranges)map.append(el('rect',{x:a/scale*1000,y:2,width:(b-a)/scale*1000,height:14,class:'timeline-gap'}));
 for(const x of data.joins)map.append(el('line',{x1:x/scale*1000,x2:x/scale*1000,y1:0,y2:18,class:'timeline-join'}));
 row.append(map);const value=document.createElement('small');value.textContent=seconds(data.kept);value.title=t.depth?`${t.depth} cut${t.depth===1?'':'s'} · ${seconds(data.original)} before cuts${t.active?'':' · cuts paused'}`:'Take length';row.append(value);root.append(row);});
 if(showMaps){const note=document.createElement('small');note.className='timeline-unavailable';note.textContent='Pink: cut joins · Dark: muted · Same time scale';root.append(note);}

 if((d.rocker===6||d.rocker===8)&&!g){const note=document.createElement('small');note.className='timeline-unavailable';note.textContent='This firmware does not report cut/gap positions.';root.append(note);}
 root.setAttribute('aria-label',caption.textContent+(g?.pendingMask?' · first cut marked':'')+(showMaps?' · track lengths and edits below':''));
}
