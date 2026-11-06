import {TransferRate} from './transfer-rate.mjs';
const addr=n=>{if(!Number.isInteger(n)||n<0||n>0xffffffff)throw Error('Invalid block');return [n&255,(n>>>8)&255,(n>>>16)&255,n>>>24];};
export class TransferClient {
 constructor(client){this.client=client;this.active=false;this.cache=null;this.bytes=0;this.started=performance.now();this.rate=new TransferRate();}
 async enter(){if(this.client.transport!=='usb')throw Error('Loop transfers use USB. Disconnect Bluetooth, choose USB and connect in the header.');const b=await this.client.request(10);if(b.length!==24)throw Error('Invalid storage layout');const v=new DataView(b.buffer,b.byteOffset,b.byteLength);this.layoutTotal=v.getUint32(12,true)+v.getUint32(4,true)*v.getUint32(8,true)*v.getUint32(16,true);this.active=true;return b;}
 async readBlock(block,remaining=Infinity){if(this.client.transport==='usb'&&this.active){if(this.cache&&block>=this.cache.block&&block<this.cache.block+this.cache.data.length/512)return this.cache.data.slice((block-this.cache.block)*512,(block-this.cache.block+1)*512);
 const total=this.layoutTotal,count=Math.min(this.client.capabilities?.[12]===31?31:16,total-block,remaining);if(count<=0)throw Error('Read beyond storage');const start=performance.now(),data=await this.client.request(18,[...addr(block),count]);if(data.length!==count*512)throw Error('Incomplete bulk read');this.cache={block,data};this.bytes+=data.length;this.rate.record(data.length,start,performance.now());return data.slice(0,512);}
 const start=performance.now(),out=new Uint8Array(512);for(let half=0;half<2;half++){const b=await this.client.request(11,[...addr(block),half]);if(b.length!==256)throw Error('Incomplete block');out.set(b,half*256);}this.bytes+=512;this.rate.record(512,start,performance.now());return out;}
 async writeBlock(block,data){return this.writeBlocks(block,data);}
 async writeBlocks(block,data){this.cache=null;const start=performance.now();await this.client.bulkWrite(block,data);this.bytes+=data.length;this.rate.record(data.length,start,performance.now());}
 async commit(){this.cache=null;await this.client.request(15);}
 async ping(){await this.client.request(3);}
 async exit(){if(this.active){await this.client.request(16);this.active=false;}}
}
