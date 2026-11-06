import {effectChanged} from './fx-state.mjs?v=552';
// Follow applied controls, never the raw ADC stream. Identical heartbeats leave browsing alone.
export class DeviceActivity {
 reset(){this.previous=null;this.anchors=new Map();}
 constructor(){this.reset();}
 update(s){
  if(!s){this.reset();return null;}
  const p=this.previous;this.previous=s;
  if(!p||p.identity!==s.identity){this.anchors.clear();return {page:s.page,lanes:[]};}
  const d=s.details,old=p.details;
  const pageChanged=p.page!==s.page||p.heads!==s.heads||old?.rockerPage!==d?.rockerPage;
  if(pageChanged)this.anchors.clear();
  const lanes=[];
  for(let i=0;i<4&&!pageChanged;i++){
   const id=d?.layout?.[(s.page-1)*4+i]??((s.page-1)*4+i);
   const key=`${s.song}/${s.page}/${i}/${id}`;
   const current=s.page>=1&&s.page<=4?s.fx[id]:s.page===5?d?.eq[i]:s.page===6?d?.placement[i]:s.levels[i];
   const previous=s.page>=1&&s.page<=4?p.fx[id]:s.page===5?old?.eq[i]:s.page===6?old?.placement[i]:p.levels[i];
   if(!this.anchors.has(key))this.anchors.set(key,previous);
   let changed=s.page!==7&&Math.abs(current-this.anchors.get(key))>=7;
   if(changed)this.anchors.set(key,current);
   if(d&&old){
    if(s.page>=1&&s.page<=4)changed ||= ['secondary','shape'].some(key=>d[key]?.[id]!==old[key]?.[id])||effectChanged(id,d,old);
    if(s.page===8)changed ||= d.tracks[i].nextFormat!==old.tracks[i].nextFormat;
   }
   if(changed)lanes.push(i);
  }
  const changed=['song','playing','volume','muted'].some(key=>s[key]!==p[key])||(d&&old&&[
   'rocker','rockerAction','fnHeld','secondaryLane','shapeHeld','monitorMuted','recordReady','stopFx','isolate','isolateMask',
   'chopDivision','chopOffset','repeat','repeatDivision','repeatStart','repeatLength','windowStart','windowEnd','windowReverse',
   'gridOffset','takePreset','beatsPerBar','songTrimHead','songTrimTail','distortionType','gatePattern','echoDivision','noiseType','wobbleType'
  ].some(key=>d[key]!==old[key]))||(d&&old&&d.routes.some((value,i)=>value!==old.routes[i]));
  return pageChanged||lanes.length||changed?{page:s.page,lanes}:null;
 }
}
