import {check,digest} from './plan.mjs?v=565';
import {Flow} from './flow.mjs?v=607';
const encoder=new TextEncoder(),decoder=new TextDecoder('ascii',{fatal:true});
export const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
export function deadline(promise,ms,message){let timer;return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(message)),ms);})]).finally(()=>clearTimeout(timer));}
export async function transact(port,options){
 check(!options.returnDirect||typeof options.onVerified==='function','Durable completion required before return');
 const info=port.getInfo();check(info.usbVendorId===0x2fe3&&info.usbProductId===0x5211,'Choose the SP-1 setup USB port');
 const flow=new Flow(options),start=performance.now();let reader,writer,pending='',opened=false,commandAt=0,partialAt=0,completionSealed=false;
 try{
  await port.open({baudRate:115200,bufferSize:65536});opened=true;
  await port.setSignals({dataTerminalReady:false});await delay(200);await port.setSignals({dataTerminalReady:true});
  reader=port.readable.getReader();writer=port.writable.getWriter();
  while(!flow.ended){
   const now=performance.now(),remaining=Math.min(780000-(now-start),!flow.sent?10000-(now-start):Infinity,flow.waitAck!==null?10000-(now-commandAt):Infinity,partialAt?5000-(now-partialAt):Infinity);
   check(remaining>0,'Setup deadline exceeded; no retry');
   const {value,done}=await deadline(reader.read(),remaining,'USB response timed out; no retry');check(!done,'USB disconnected');
   check(value.every(v=>v<128),'Non-ASCII setup response');pending+=decoder.decode(value);check(pending.length<=65536,'Serial line bound');
   let at;
   while((at=pending.indexOf('\n'))>=0){
    const line=pending.slice(0,at).replace(/\r$/,'');pending=pending.slice(at+1);partialAt=0;
    const command=await deadline(flow.line(line),60000,'Backup or plan could not be saved within the device deadline');
    if(command){commandAt=performance.now();await deadline(writer.write(encoder.encode(command)),2000,'USB request write timed out; no retry');}
    if(flow.ended){check(!pending.trim(),'Data after final report');break;}
   }
   partialAt=pending?(partialAt||performance.now()):0;
  }
  flow.validate();await options.save('private-raw.txt',encoder.encode(flow.lines.join('\n')+'\n'));await options.save('radio-after.bin',flow.expected);
  const summary={status:'BROWSER ADAPTER '+options.mode.toUpperCase()+' PASS',setup:335,sectors:flow.verified,full_flash_sha256:await digest(flow.expected),elapsed_seconds:Math.round((performance.now()-start)/10)/100,radio_transport:flow.transport||null,public_qualified:false,clean_forward:!!flow.cleanChosen};
  await options.save('summary.json',summary);
  // Seal recovery and persist host state BEFORE asking the device to leave.
  // A browser crash during return must never look like an unfinished write.
  if(options.onVerified)await deadline(options.onVerified(summary,flow.report),60000,'Bluetooth verified; recovery completion could not be sealed. Reconnect to check.');
  completionSealed=true;
  let returnRequested=false;
  if(options.returnDirect){
   check(flow.lines.some(line=>line==='BTID535 SETUP direct_return=1'),'Direct return capability missing');
   await deadline(reader.cancel(),1000,'Could not stop verified USB reads');reader.releaseLock();reader=null;
   const serial=globalThis.navigator?.serial;let timer,listener;
   const disconnected=new Promise(resolve=>{timer=setTimeout(resolve,2500);listener=e=>{if(e.port===port||e.target===port)resolve()};serial?.addEventListener('disconnect',listener)});
   try{await deadline(writer.write(encoder.encode('A336EXIT?')),2000,'Bluetooth verified; return uncertain. Reconnect to check.');returnRequested=true;await disconnected;}
   finally{clearTimeout(timer);serial?.removeEventListener('disconnect',listener)}
  }
  return {summary,compatibility:flow.report,returnRequested,cleanForward:!!flow.cleanChosen};
 }catch(error){
  // Known idle source + no write-capable ARM (or a zero-sector plan) cannot
  // require restoration. Unknown startup/recovery/write uncertainty stays held.
  error.radioUnarmed=flow.recoveryRequired===false&&!options.original&&!flow.started&&(!flow.armed||flow.manifest?.sectors.length===0);
  error.radioDiagnostic={identity:flow.identity,cleanForward:!!flow.cleanChosen,plannedSectors:flow.manifest?.sectors?.length??null,armSent:!!flow.armed,sectorSealSent:!!flow.started,verifiedSectors:flow.verified,waitAck:flow.waitAck,rows:flow.lines.filter(row=>/^BTID335 (?:ACK |SECTOR_READY |SECTOR_VERIFIED |PLAN_VERIFIED |WRITE |BOOT |result=|END|ERROR )/.test(row)).slice(-32)};
  if(completionSealed){error.radioVerified=true;error.message='Bluetooth update verified; returning to Tape Looper was interrupted. Reconnect to check. '+error.message;}
  try{await deadline(options.save('failure-summary.json',{status:completionSealed?'VERIFIED; RETURN INCOMPLETE':error.code==='RADIO_SOURCE_REVIEW'&&error.radioUnarmed?'COMPATIBILITY REVIEW; NO WRITES':'STOP',setup:335,mode:options.mode,arm_sent_or_attempted:flow.armed,sector_seal_sent_or_attempted:flow.started,recovery_required_at_start:flow.recoveryRequired??null,known_unarmed:!!error.radioUnarmed,verified_sectors:flow.verified,startup:{identity_seen:!!flow.identity,start_sent:!!flow.sent,bytes:flow.length?.[0]??0,rows:flow.lines.filter(row=>['BTID335 SETUP ','BTID335 BOOT ','BTID335 READY ','BTID524 SETUP ','BTID535 SETUP ','BTID545 SPEED ','BTID545 AUTOBAUD '].some(prefix=>row.startsWith(prefix))).slice(0,24)},error:error.message,public_qualified:false}),5000,'Diagnostic save timeout');await deadline(options.save('private-partial.txt',encoder.encode(flow.lines.join('\n')+'\n')),5000,'Diagnostic save timeout');}catch{}
  throw error;
 }finally{
  // Never reconnect, reset, boot the radio or send a recovery command on failure.
  if(reader){try{await deadline(reader.cancel(),1000,'Reader cleanup timeout');}catch{}try{reader.releaseLock();}catch{}}
  if(writer){try{writer.releaseLock();}catch{}}
  if(opened){try{await deadline(port.close(),2000,'Port cleanup timeout');}catch{}}
 }
}
