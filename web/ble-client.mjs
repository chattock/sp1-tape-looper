import {requestFrame,ResponseParser,crc16} from './usb-client.mjs';
export const APPLICATION='53500006-6014-ce88-a946-12f0f8649172';
// Retained responses are read, not consumed. Only HELLO may be retried automatically.
export class BleClient {
 constructor(characteristic,maxWrite=20){this.maxWrite=Math.min(244,Math.max(20,maxWrite));this.characteristic=characteristic;this.transport='bluetooth';this.seq=0;this.queue=Promise.resolve();this.closed=false;}
 async connect(){let caps;for(let i=0;i<2;i++){try{caps=await this.request(1);break;}catch(e){if(i)throw e;await new Promise(r=>setTimeout(r,700));}}if(!caps||String.fromCharCode(...caps.slice(0,4))!=='APP1'||caps[4]!==1)throw Error('Unsupported Bluetooth application firmware');this.capabilities=caps;return caps;}
 enqueue(fn){const run=this.queue.then(fn);this.queue=run.catch(()=>{});return run;}
 request(op,data=[]){return this.enqueue(()=>this.exchange(op,data));}
 async operation(stage,op,seq,fn){try{return await fn();}catch(cause){const error=new Error(`Bluetooth ${stage} failed (operation ${op}): ${cause.message}`,{cause});error.transportDetail={transport:this.transport,stage,op,seq,errorName:cause.name};this.lastFailure=error.transportDetail;throw error;}}
 async exchange(op,data=[]){if(this.closed)throw Error('Bluetooth session closed');const seq=(++this.seq)&65535,b=requestFrame(seq,op,data);for(let i=0;i<b.length;i+=20){if(this.closed)throw Error('Bluetooth session closed');await this.operation('command write',op,seq,()=>this.characteristic.writeValueWithResponse(b.slice(i,i+20)));}
 const deadline=performance.now()+12000;while(performance.now()<deadline&&!this.closed){await new Promise(r=>setTimeout(r,5));const v=await this.operation('response read',op,seq,()=>this.characteristic.readValue());let reply;new ResponseParser(f=>{if(f.seq===seq&&f.op===op)reply=f;}).push(new Uint8Array(v.buffer,v.byteOffset,v.byteLength));if(reply){if(reply.status)throw Error(['','No application session','Invalid request','Stop playback/recording and wait for saving first','Storage write/flush failed; inspect before retry','Unsupported operation'][reply.status]||`Device error ${reply.status}`);return reply.data;}}
 throw Error('Bluetooth request timed out; no automatic write retry');}
 bulkWrite(block,data){return this.enqueue(async()=>{if(!data.length||data.length%512||data.length>8192)throw Error('Invalid bulk size');const c=crc16(data);await this.exchange(19,[block&255,(block>>>8)&255,(block>>>16)&255,block>>>24,data.length/512,c&255,c>>8]);
 for(let at=0;at<data.length;at+=this.maxWrite-6){if(this.closed)throw Error('Bluetooth session closed');const part=data.slice(at,at+this.maxWrite-6),b=new Uint8Array(6+part.length);b.set([83,80,66,49,at&255,at>>8]);b.set(part,6);await this.characteristic.writeValueWithResponse(b);}
 const r=await this.exchange(20);if(r.length!==2||(r[0]|r[1]<<8)!==c)throw Error('Bulk write readback failed');});}

 async close(){this.closed=true;}
}
