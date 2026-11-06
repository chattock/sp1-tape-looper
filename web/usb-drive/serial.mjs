// Qualification migration/recovery adapter. No automatic retries.
import {crc32} from './backup/core.mjs?v=396.2';
const enc=new TextEncoder(),dec=new TextDecoder();
export class BackupSerial {
 constructor(port){this.port=port;this.rx=new Uint8Array(32768);this.rxStart=0;this.rxEnd=0;this.readSectors=1;this.metrics={readCalls:0,readBytes:0,readMs:0};this.byob=false;this.seq=0;this.sharedProfile="bounded-fat396";this.readOnly=false;this.writes=true;this.started=false;this.failed=false;this.busy=false;this.bootGate={persistent:true,lock:h=>this.gateCommand(true,h),unlock:h=>this.gateCommand(false,h),status:()=>this.gateStatus()};}
 static async connect(selected=null){const port=selected??await navigator.serial.requestPort({filters:[{usbVendorId:0x2fe3,usbProductId:0x5217}]});await port.open({baudRate:115200,bufferSize:65536,flowControl:'none'});const x=new BackupSerial(port);try{x.reader=port.readable.getReader({mode:'byob'});x.byob=true;x.readBuffer=new ArrayBuffer(16384)}catch{x.reader=port.readable.getReader()}x.writer=port.writable.getWriter();try{await port.setSignals({dataTerminalReady:false});await new Promise(resolve=>setTimeout(resolve,350));await port.setSignals({dataTerminalReady:true});await x.greeting();return x}catch(e){await x.close();throw e}}
 async greeting(){
 const seen=[];for(let i=0;i<8;i++){const line=await this.line();if(line==='STORAGE396 READY read_only=0'){this.discardedGreetingLines=seen;return}
 seen.push(line.slice(0,160));if(line!==''&&!/^STORAGE396 (ERROR |ID )/.test(line))throw Error('Expected backup396 greeting; received '+JSON.stringify(line.slice(0,160)));}
 throw Error('No fresh backup396 greeting; received '+JSON.stringify(seen));
 }
 get pending(){return this.rx.subarray(this.rxStart,this.rxEnd)}
 async fill(){let timer;try{
 const request=this.byob?this.reader.read(new Uint8Array(this.readBuffer)):this.reader.read();
 const r=await Promise.race([request,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(`USB response timed out while waiting for ${this.lastRequest||'firmware greeting'}; phase=${this.phase||'greeting'}, buffered=${this.pending.length} bytes. No automatic retry.`)),15000)})]);
 if(r.done)throw Error('USB disconnected');
 const count=this.pending.length;if(count+r.value.length>this.rx.length)throw Error('Unexpected USB response size');
 if(this.rxEnd+r.value.length>this.rx.length){this.rx.copyWithin(0,this.rxStart,this.rxEnd);this.rxStart=0;this.rxEnd=count}
 this.rx.set(r.value,this.rxEnd);this.rxEnd+=r.value.length;
 if(this.byob)this.readBuffer=r.value.buffer;
 }finally{clearTimeout(timer)}}
 async bytes(n){while(this.pending.length<n)await this.fill();const result=this.rx.slice(this.rxStart,this.rxStart+n);this.rxStart+=n;if(this.rxStart===this.rxEnd)this.rxStart=this.rxEnd=0;return result}
 async line(){let pos;while((pos=this.pending.indexOf(10))<0){if(this.pending.length>512)throw Error('Malformed USB response');await this.fill()}if(pos>512)throw Error('Malformed USB response');return dec.decode(await this.bytes(pos+1)).trim()}
 async transact(action){if(this.failed||this.busy)throw Error('USB session unavailable');this.busy=true;try{return await action()}catch(e){this.failed=true;await this.close();throw e}finally{this.busy=false}}
 async send(s){this.lastRequest=s;await this.writer.write(enc.encode(s+'\n'))}
 async identify(){return this.transact(async()=>{await this.send('S396HELLO?');for(let i=0;i<4;i++){const s=await this.line();if(s==='STORAGE396 READY read_only=0'||s==='')continue;const m=/^STORAGE396 ID identity=([0-9a-f]{16}) blocks=(\d+) read_only=0$/.exec(s);if(!m)throw Error(s||'Wrong firmware');if(Number(m[2])!==7733248)throw Error('Unsupported physical capacity');if(this.boundIdentity&&this.boundIdentity!==m[1])throw Error('Device changed');this.boundIdentity=m[1];return {identity:m[1],capacity:Number(m[2]),readOnly:false,firmwareWriteCapable:true}}throw Error('No firmware identity')})}
 async read(lba,n){
 if(!Number.isInteger(lba)||lba<0||!Number.isInteger(n)||n<1||n>16||lba+n>0x100000000)throw Error('Invalid read request');
 const result=new Uint8Array(n*512);
 for(let at=0;at<n;){const count=Math.min(this.readSectors,n-at);result.set(await this.readChunk(lba+at,count),at*512);at+=count}
 return result;
 }

 async readChunk(lba,n){return this.transact(async()=>{const began=performance.now();if(!Number.isInteger(lba)||lba<0||lba>0xffffffff||!Number.isInteger(n)||n<1||n>16||lba+n>0x100000000)throw Error('Invalid read request');const seq=++this.seq;const hex=x=>x.toString(16).padStart(8,'0');await this.send(`R396 ${hex(seq)} ${hex(lba)} ${hex(n)}`);this.phase='read header';const line=await this.line(),m=/^STORAGE396 DATA seq=([0-9a-f]{8}) lba=(\d+) blocks=(\d+) crc=([0-9a-f]{8})$/.exec(line);if(!m||parseInt(m[1],16)!==seq||Number(m[2])!==lba||Number(m[3])!==n)throw Error('Unexpected read response: '+line);this.phase=`payload (${n*512} bytes expected)`;const bytes=await this.bytes(n*512);const actual=crc32(bytes);if(actual!==parseInt(m[4],16)){this.transferFailure={seq,lba,blocks:n,expected:m[4],actual:hex(actual),payloadHex:Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join(''),bufferedHex:Array.from(this.pending,b=>b.toString(16).padStart(2,'0')).join('')};throw Error(`USB payload checksum failed: seq=${seq} lba=${lba} blocks=${n} expected=${m[4]} actual=${hex(actual)}; no retry`)};this.phase='payload boundary';if(await this.line()!=='')throw Error('Missing data boundary');this.phase='completion marker';if(await this.line()!=='STORAGE396 DONE')throw Error('Missing completion marker');this.phase='idle';this.metrics.readCalls++;this.metrics.readBytes+=bytes.length;this.metrics.readMs+=performance.now()-began;return bytes})}
 async simple(command,expected){return this.transact(async()=>{await this.send(command);if(await this.line()!==expected)throw Error('Maintenance command refused')})}
 async transferMetrics(){return this.transact(async()=>{await this.send('S396PERF?');const s=await this.line();if(!/^STORAGE396 PERF (?:[a-z_]+=\d+ ?)+$/.test(s))throw Error('Invalid transfer metrics');return Object.fromEntries(s.slice(16).trim().split(' ').map(x=>{const [k,v]=x.split('=');return [k,Number(v)]}))})}
 async onboard(){await this.simple('S396ONBOARD!','STORAGE396 ONBOARD accepted')}
 async radio(){await this.simple('S396RADIO!','STORAGE396 RADIO accepted');await this.close()}
 configureClean(original){this.cleanInstall={profile:'sp1-tail-clean-v1',capacity:7733248};this.cleanCRC=crc32(original).toString(16).padStart(8,'0')}
 async begin(recover=false){return this.transact(async()=>{
  if(this.started)return;
  if(this.cleanCRC){await this.send('S396CLEAN! '+this.cleanCRC);if(await this.line()!=='STORAGE396 CLEAN admitted')throw Error('Clean recovery admission refused')}
  if(recover){await this.send('S396RECOVER!');if(await this.line()!=='STORAGE396 RECOVERY only_sector0')throw Error('Recovery mode refused')}
  await this.send('S396CACHEON!');if(await this.line()!=='STORAGE396 CACHE enabled')throw Error('Cache enable refused');this.started=true;
 })}
 async writeBounded(lba,before,bytes){return this.transact(async()=>{
  const n=bytes.length/512;if(!this.started||!Number.isInteger(lba)||!Number.isInteger(n)||n<1||n>16||lba<0||lba+n>7733248||before.length!==bytes.length)throw Error('Outside bounded write');
  const seq=++this.seq,hex=x=>x.toString(16).padStart(8,'0');
  await this.send(`L396 ${hex(lba)} ${hex(n)} ${hex(crc32(before))} ${hex(crc32(bytes))}`);
  if(await this.line()!=='STORAGE396 LEASE admitted')throw Error('Original bytes changed or lease refused');
  await this.send(`P396 ${hex(seq)} ${hex(lba)} ${hex(crc32(bytes))} ${hex(n)}`);
  if(await this.line()!==`STORAGE396 UPLOAD seq=${hex(seq)}`)throw Error('Upload refused');
  await this.writer.write(bytes);if(await this.line()!==`STORAGE396 PAYLOAD seq=${hex(seq)}`)throw Error('Payload incomplete');
  await this.send(`C396 ${hex(seq)}`);const result=await this.line();
  if(!result.startsWith(`STORAGE396 WRITTEN seq=${hex(seq)} lba=${lba} blocks=${n} us=`))throw Error('Write verification failed: '+result);
 })}
 async sync(){return this.transact(async()=>{await this.send('S396FLUSH!');const s=await this.line();if(!/^STORAGE396 FLUSH ok=1 us=\d+$/.test(s))throw Error('Durability barrier failed: '+s)})}
 async status(){return this.transact(async()=>{this.phase='device status';await this.send('S396STATUS?');const s=await this.line();if(!/^STORAGE396 STATUS (?:[a-z_]+=\d+ ?)+$/.test(s))throw Error('Unexpected status: '+s);return Object.fromEntries(s.slice(18).trim().split(' ').map(x=>{const [k,v]=x.split('=');return [k,Number(v)]}))})}
 async hostMetrics(){return this.transact(async()=>{await this.send('S396METR?');const s=await this.line();if(!/^STORAGE396 METR (?:[a-z_]+=\d+ ?)+$/.test(s))throw Error('Unexpected drive metrics: '+s);return Object.fromEntries(s.slice(16).trim().split(' ').map(x=>{const [k,v]=x.split('=');return [k,Number(v)]}))})}
 async hostStart(){return this.transact(async()=>{await this.send('S396HOST!');if(await this.line()!=='STORAGE396 HOST active')throw Error('USB drive admission refused')})}
 async hostStatus(){return this.transact(async()=>{await this.send('S396HOST?');const s=await this.line(),m=/^STORAGE396 HOST active=([01]) ejected=([01]) failed=([01])$/.exec(s);if(!m)throw Error('Unexpected USB drive status');return {active:m[1]==='1',ejected:m[2]==='1',failed:m[3]==='1'}})}
 async gateStatus(){return this.transact(async()=>{await this.send('S396GATE?');const s=await this.line(),m=/^STORAGE396 GATE blocked=([01]) valid=([01])$/.exec(s);if(!m)throw Error('Unexpected gate status: '+s);return {blocked:m[1]==='1',valid:m[2]==='1'}})}
 async gateCommand(lock,hash){if(!/^[a-f0-9]{64}$/.test(hash))throw Error('Invalid target hash');return this.transact(async()=>{await this.send(`S396${lock?'LOCK':'OPEN'}! ${hash}`);const expected='STORAGE396 GATE '+(lock?'locked':'open');if(await this.line()!==expected)throw Error('Persistent gate operation refused')})}
 async returnToLooper(){return this.transact(async()=>{await this.send('S396NORMAL!');if(await this.line()!=='STORAGE396 NORMAL accepted')throw Error('Return was not acknowledged');await this.close();return true})}
 async enterBootloader(){return this.transact(async()=>{await this.send('S396BOOT!');if(await this.line()!=='STORAGE396 BOOT accepted')throw Error('Recovery command was not acknowledged');await this.close();return true})}
 async close(){this.failed=true;try{await this.reader?.cancel()}catch{}try{this.reader?.releaseLock()}catch{}try{this.writer?.releaseLock()}catch{}try{await this.port.close()}catch{}}
}
