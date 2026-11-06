import {BackupSerial} from './serial.mjs?v=607.5';
import {deadline,delay} from './radio/serial.mjs';
import {BANNER} from './radio/flow.mjs';
export const FILTERS=[0x5210,0x5217,0x5211].map(usbProductId=>({usbVendorId:0x2fe3,usbProductId}));
export function modeOf(port){const i=port.getInfo();if(i.usbVendorId!==0x2fe3)throw Error('Choose the SP-1 USB port');const mode=({0x5210:'runtime',0x5217:'storage',0x5211:'radio'})[i.usbProductId];if(!mode)throw Error('Flash the supplied firmware first');return mode}
export class Runtime extends BackupSerial{
 static async connect(port){await port.open({baudRate:115200,bufferSize:65536});const d=new Runtime(port);try{d.reader=port.readable.getReader();d.writer=port.writable.getWriter();await port.setSignals({dataTerminalReady:true});await d.identify();return d}catch(e){await d.close();throw e}}
 async response(command,pattern){await this.send(command);const seen=[];for(let n=0;n<256;n++){const s=await this.line();if(s.startsWith('LIBRARY462'))throw Error('A USB Library session is open. Finish it in Library first. If it needs recovery, power-cycle the SP-1 and reconnect Setup.');if(pattern.test(s))return s;if(s.includes('SETUP REFUSED')){
 const message=command==='A373SETUP?'
  ? 'Switch your computer’s audio output away from SP-1. On the SP-1, stop playback and recording, close any control page, return to Tracks and wait for saving to finish. Then retry the upgrade.'
  : 'Stop playback and recording, finish saving and close any control page, then retry.';
 throw Object.assign(Error(message),{code:'SETUP_BUSY',deviceReply:s});
}if(s.startsWith('BTID')&&s.includes('RUNTIME')&&!/^BTID(?:444|422) /.test(s))throw Object.assign(Error('Get the current Tape Looper BIN from GitHub, flash through Solderless, then reconnect.'),{code:'FIRMWARE_REQUIRED'});seen.push(s);if(seen.length>12)seen.shift()}throw Error('Runtime response missing; flash the supplied firmware')}
 async identify(){return this.transact(async()=>{
 let legacy;try{legacy=await this.response('A403HELLO?',/^BTID(?:444|422) RUNTIME identity=[a-f0-9]{16} takes=[01]$/)}catch(e){if(e.code==='USB_READ_TIMEOUT'||/USB disconnected|USB Library session|Stop recording|Different|identity changed/.test(e.message))throw e;throw Object.assign(Error('Compatible firmware could not be verified. Get the current Tape Looper BIN from GitHub, flash through Solderless, then reconnect.'),{code:'FIRMWARE_REQUIRED'})}
 let exact;try{exact=await this.response('A468HELLO?',/^ROCKER468 HELLO mode=runtime identity=[a-f0-9]{16}$/)}catch(e){
 // Older Tape Looper builds answer A403 but silently ignore newer queries.
 // Do not retry on a timed-out reader or enter maintenance with unverified firmware.
 if(e.code==='USB_READ_TIMEOUT')throw Object.assign(Error('Tape Looper responded, but the required setup capability could not be verified. Flash the current preserving v4 BIN, then reconnect. No installation changes were made.'),{code:'FIRMWARE_REQUIRED',deviceReply:legacy,command:'A468HELLO?',transportPhase:'runtime capability check',cause:e});
 if(/USB disconnected|USB Library session|Stop recording|Different|identity changed/.test(e.message))throw e;
 throw Object.assign(Error('Compatible firmware could not be verified. Get the current Tape Looper BIN from GitHub, flash through Solderless, then reconnect.'),{code:'FIRMWARE_REQUIRED',deviceReply:legacy})}

 this.basicAllowed=legacy.endsWith('takes=0');if(this.basicAllowed)await this.response('A523MODE?',/^BASIC523 CAPS legacy=1 takes=0 drive=0$/);
 const identity=/identity=([a-f0-9]{16})/.exec(legacy)[1];if(!exact.endsWith('identity='+identity))throw Error('Runtime identity changed during verification');
 return {identity,firmware:444,build:468};
 })}
 async status(){return this.transact(async()=>{
 //500: collect the complete bounded cached report before refusing setup.
 // No eMMC access, acknowledgement, recovery, reset or state-clearing command.
 this.directRadio=false;const rows=this.statusRows=[];let storage,backend,writerStatus,rejection;
 const refuse=message=>{rejection ||= message};
 await this.send('A373CHECK?');
 for(let n=0;n<128;n++){const row=await this.line();rows.push(row);
 if(/^FAULT420 class=/.test(row)&&!/^FAULT420 class=none preflight=[01] metadata_pending=0$/.test(row))refuse('Finish recording/saving or resolve the reported storage fault before setup');
 if(/^FAULT400 /.test(row)){writerStatus=row;if(!/code=[0-7][a-f0-9]{7}\b/.test(row)||/\b(?:writer|failed)=[1-9]/.test(row))refuse('Storage has an unfinished write or fault; preserve diagnostics');}
 if(/^STORAGE373 RESULT /.test(row))storage=row;
 if(row==='SETUP539 CAPS radio_direct=1')this.directRadio=true;
 if(/^CAPACITY422 backend=/.test(row))backend=row.split('=')[1];
 if(/^READY427 prepared=[01] audition=[01]$/.test(row)){
  if(!storage||!(['paged','fixed'].includes(backend)?/format=2\b/.test(storage):this.basicAllowed&&backend==='legacy'&&/format=1\b/.test(storage)))refuse('Incomplete library admission report');
  if(writerStatus&&/\bactive=[1-9]/.test(writerStatus)&&!(backend==='paged'&&row==='READY427 prepared=1 audition=0'&&['owner=1','writer=0','phase=3','written=16','active=1','failed=0'].every(field=>new RegExp('\\b'+field+'\\b').test(writerStatus))))refuse('Storage has an unfinished write; preserve diagnostics');
  if(row.endsWith('audition=1'))refuse('Wait for saving to finish');
  if(rejection)throw Error(rejection);
  this.libraryBackend=backend;return storage+' backend='+backend;
 }
 }throw Error(rejection||'Incomplete runtime verification');
 })}
 async enterRadio(){if(!this.directRadio)throw Error('Direct Bluetooth setup is unavailable');return this.transact(async()=>{this.phase='Bluetooth setup handoff';await this.response('A539BTUP?',/^BTID373 SETUP RESTART$/);await this.close()})}
 async enter(){return this.transact(async()=>{await this.response('A373SETUP?',/^BTID373 SETUP RESTART$/);await this.close()})}
}
export async function radioMode(port,expected=null,exit=false,restart=false){
 if(exit&&!expected)throw Error('Identify the SP-1 before leaving Bluetooth setup');
 let reader,writer,opened=false;
 try{
  await port.open({baudRate:115200,bufferSize:65536});opened=true;
  await port.setSignals({dataTerminalReady:false});await delay(200);await port.setSignals({dataTerminalReady:true});
  reader=port.readable.getReader();writer=port.writable.getWriter();let text='',identity;const until=performance.now()+10000;
  while(!identity){const {value,done}=await deadline(reader.read(),Math.max(1,until-performance.now()),'Bluetooth setup identification timed out');if(done)throw Error('USB disconnected');text+=new TextDecoder().decode(value);if(text.length>65536)throw Error('Unexpected setup response');const m=/(?:^|\n)BTID335 SETUP identity=([a-f0-9]{16}) radio_writes=bounded-host-plan audio=disabled storage=disabled\r?\n/.exec(text);if(m&&text.includes(BANNER))identity=m[1];if(performance.now()>until)throw Error('Wrong Bluetooth adapter or missing identity')}
  const recoveryRequired=/BTID524 SETUP recovery=1 return_gesture=t2\+t3 hold_ms=3000/.test(text);
  if(exit&&recoveryRequired&&!restart)throw Object.assign(Error('Bluetooth recovery is required. Choose this SP-1’s original radio recovery folder.'),{code:'RADIO_RECOVERY_REQUIRED'});
  if(expected&&expected!==identity)throw Error('Different SP-1 selected; reconnect the original device');
  if(restart&&!text.includes('BTID535 SETUP direct_return=1'))throw Error('Restart the SP-1, then reconnect to recover Bluetooth.');
  if(exit){await deadline(reader.cancel(),1000,'Could not stop USB reads');reader.releaseLock();reader=null;// Keep DTR/USB open until the device consumes the command and disconnects.
   // Closing immediately can drop host readiness before firmware services RX.
   let timer;const serial=globalThis.navigator?.serial;let listener;
   const disconnected=new Promise(resolve=>{timer=setTimeout(()=>resolve(false),2500);listener=e=>{if(e.port===port||e.target===port)resolve(true)};serial?.addEventListener('disconnect',listener)});
   try{await deadline(writer.write(new TextEncoder().encode(restart?'A535NEW?':'A336EXIT?')),2000,'Return request uncertain; reconnect to identify the current mode');await disconnected;}
   finally{clearTimeout(timer);serial?.removeEventListener('disconnect',listener)}}
  return {identity,recoveryRequired,cleanForward:/BTID607 CLEAN profile=static-v1 forward=1 pending=[01]/.test(text),cleanPending:text.includes('BTID607 CLEAN profile=static-v1 forward=1 pending=1'),directReturn:text.includes('BTID535 SETUP direct_return=1'),offlineExit:text.includes('BTID524 SETUP recovery=')};
 }finally{try{await deadline(reader?.cancel(),1000,'cleanup')}catch{}try{reader?.releaseLock()}catch{}try{writer?.releaseLock()}catch{}if(opened)try{await deadline(port.close(),2000,'cleanup')}catch{}}
}
