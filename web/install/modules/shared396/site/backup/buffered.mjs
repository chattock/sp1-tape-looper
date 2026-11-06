// Bounded host-side buffering only. A successful flush is not a completed backup;
// caller still closes, reads back and authenticates the entire saved file.
export class BufferedWriter {
 constructor(writer,capacity=262144,clock=()=>performance.now()){
  if(!Number.isSafeInteger(capacity)||capacity<512)throw Error('Invalid save buffer');
  this.writer=writer;this.buffer=new Uint8Array(capacity);this.used=0;this.clock=clock;this.ms=0;this.bytes=0;this.calls=0;
 }
 async write(bytes){for(let at=0;at<bytes.length;){const n=Math.min(bytes.length-at,this.buffer.length-this.used);this.buffer.set(bytes.subarray(at,at+n),this.used);at+=n;this.used+=n;if(this.used===this.buffer.length)await this.flush()}}
 async flush(){if(!this.used)return;const n=this.used,t=this.clock();await this.writer.write(this.buffer.subarray(0,n));this.ms+=this.clock()-t;this.bytes+=n;this.calls++;this.used=0}
}
