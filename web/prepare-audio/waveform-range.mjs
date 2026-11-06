export const MIN_SELECTION=.001,MAX_SELECTION=480;
const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
export function editSelection(range,kind,time,duration,anchor){
 if(![range.start,range.end,time,duration].every(Number.isFinite)||duration<=0)throw Error('Invalid waveform selection');
 const min=Math.min(MIN_SELECTION,duration),length=Math.min(MAX_SELECTION,Math.max(min,range.end-range.start),duration);
 if(kind==='start')return {start:clamp(time,Math.max(0,range.end-MAX_SELECTION),range.end-min),end:range.end};
 if(kind==='end')return {start:range.start,end:clamp(time,range.start+min,Math.min(duration,range.start+MAX_SELECTION))};
 if(kind==='move'){const start=clamp(time,0,duration-length);return {start,end:start+length}}
 if(kind==='select'){const a=clamp(anchor,0,duration),b=clamp(time,Math.max(0,a-MAX_SELECTION),Math.min(duration,a+MAX_SELECTION));const start=Math.min(a,b),end=Math.max(a,b);return end-start>=min?{start,end}:{start:clamp(start,0,duration-min),end:clamp(start,0,duration-min)+min}}
 throw Error('Invalid waveform action');
}
