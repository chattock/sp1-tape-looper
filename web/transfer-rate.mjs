// Actual payload bytes, including protocol wait time. Idle time after completion is excluded.
export class TransferRate {
 constructor(){this.reset();}
 reset(){this.samples=[];this.bytes=0;this.first=null;this.last=null;}
 record(bytes,start,end){if(!bytes)return;this.first??=start;this.last=end;this.bytes+=bytes;this.samples.push({bytes,start,end});this.samples=this.samples.filter(s=>s.end>=end-2000);}
 get current(){if(!this.samples.length)return 0;const start=Math.max(this.samples[0].start,this.last-2000),duration=this.last-start;let bytes=0;for(const s of this.samples){const span=s.end-s.start;bytes+=span>0?s.bytes*Math.min(1,Math.max(0,(s.end-start)/span)):s.bytes;}return duration>0?bytes/(duration/1000):0;}
 get average(){return this.first!==null&&this.last>this.first?this.bytes/((this.last-this.first)/1000):0;}
}
