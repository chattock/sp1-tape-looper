import {decodeGeometry} from './geometry.mjs?v=558';
import {setReading} from './highlight.mjs';
import {pages,initExplorer,updateExplorer,renderMix,configureLayoutEditor,setLayoutEditable} from './explorer.mjs?v=558';
initExplorer();
updateExplorer(null);
renderMix(null);
import {ConnectedDevice} from './model.mjs?v=611';
const model=new ConnectedDevice(document.getElementById('viewport'),{onReady:()=>document.getElementById('modelStatus').textContent='',onError:()=>document.getElementById('modelStatus').textContent='3D unavailable. Live state cards still work.'});
import {BleClient,APPLICATION} from './ble-client.mjs?v=acceptance328';
import {TransferClient} from './transfer-client.mjs?v=usb-only329';
import {UsbClient} from './usb-client.mjs?v=usb278-resume';
import {decode,SERVICE,STREAM,CAPS,Observer,fragments} from './protocol.mjs?v=523';
const $=id=>document.getElementById(id);
const transportPreference='sp1.connectionType';
// USB is the default on every visit; Bluetooth is an explicit Live choice.
$('connectionType').value='usb';
$('connectionType').addEventListener('change',()=>{try{localStorage.setItem(transportPreference,$('connectionType').value);}catch{}});
const stoppedHints=[
 'Effects keep running while the tape is paused.',
 'Tape noise and wobble follow the tape speed, fading out as it stops. Filters, echo and reverb keep running.',
 'All effects fade out as the tape stops and return as it starts.'];
function stoppedHint(){ $('stoppedEffectsHint').textContent=stoppedHints[+$('stoppedEffects').value]||stoppedHints[2]; }
$('stoppedEffects').addEventListener('change',stoppedHint);stoppedHint();
let device,observer,stream,lastAt=0,transfer=0,queue=Promise.resolve(),generation=0,events=[],connected=false;
let pendingSnapshot=null,renderPending=false,basicMode=false;
let diagnostics={};
let usb=null,usbCount=0,storage=null,storageActive=false;const pendingAcks=new Set();
let connectionStage='idle';
function updateSettings(enabled){const button=$('settings');button.hidden=!connected;button.disabled=!enabled;button.classList.toggle('settings-ready',!!enabled&&connected);if(!connected&&$('settingsDialog').open)$('settingsDialog').close();}

function status(title,detail='',busy=false){
 const node=$('status');node.classList.toggle('loading',busy);node.setAttribute('aria-busy',String(busy));
 node.replaceChildren();node.hidden=!title;if(!title)return;const label=document.createElement('strong');label.textContent=title;node.append(label);
 if(detail){const sub=document.createElement('span');sub.textContent=detail;node.append(sub);}
}
function loading(stage,title,detail){connectionStage=stage;status(title,detail,true);}
function snapshotLoading(){if(!observer?.count){const total=observer?.parts;loading('initial-snapshot','Receiving first snapshot',observer?.part&&total?`${observer.part} of ${total} state parts received · values appear when complete`:'Waiting for the SP-1 handshake and complete device state');}}

let omittedEvents=0;const log=(type,detail)=>{events.push({at:new Date().toISOString(),type,detail});if(events.length>4000){events.shift();omittedEvents++;}};
function clear(){ basicMode=false;pendingSnapshot=null;model.setSnapshot(null);updateExplorer(null);lastAt=0;$('state').classList.add('stale');for(const id of ['transport','mode','speed','volume','song'])$(id).textContent='—';renderMix(null);$('identity').textContent='No current device state.';}
function stop(message,detail={}){const failure={message,...detail,gattConnected:!!device?.gatt?.connected,lastSnapshotAgeMs:lastAt?Math.round(performance.now()-lastAt):null,sequence:observer?.seq??null,part:observer?.part??null,revision:observer?.revision??null,...diagnostics,transport:usb?.transport,transportFailure:usb?.lastFailure,storageActive};generation++;connected=false;const oldUsb=usb;usb=null;storage=null;storageActive=false;window.dispatchEvent(new Event('sp1-storage-disconnected'));const closing=Promise.resolve(oldUsb?.close());closing.catch(()=>{});device?.gatt?.disconnect();updateSettings(false);setLayoutEditable(false);notifyCapabilities();clear();$('connect').textContent='Connect SP-1';$('connect').disabled=false;$('connectionType').disabled=false;status(message,detail.intentional?'Reconnect in Live companion after eject.':'Save session evidence before reconnecting.');log('disconnected',failure);if(detail.stage)$('companionDiagnostics').hidden=false;return closing;}
function show(s,n,wire){basicMode=!!s.details?.basic;model.setSnapshot(s);updateExplorer(s);lastAt=performance.now();$('state').classList.remove('stale');if(s.storageFault){connectionStage='storage-fault';status('Storage needs attention.','Stop recording and save diagnostics.');$('companionDiagnostics').hidden=false;}else if(connectionStage!=='live'){connectionStage='live';status('');}setReading($('song'),s.song,$('song').closest('.tile'));setReading($('transport'),s.playing?'Playing':'Stopped',$('transport').closest('.tile'));setReading($('mode'),s.details?.rockerPage?'Rocker page':s.page?`Page ${s.page}`:s.heads?'Four heads':'Tracks',$('mode').closest('.tile'));setReading($('speed'),s.speed.toFixed(2)+'×',$('speed').closest('.tile'));setReading($('volume'),s.volume+' / 15',$('volume').closest('.tile'));$('identity').textContent=`Device ${s.identity} · ${n} complete snapshots`;
 renderMix(s);
}
// Record every accepted snapshot for verification; render only the latest per frame.
// Returning from the protocol callback promptly lets ACK writes queue before layout work.
function receiveSnapshot(s,n,wire){
 log(usb?usb.transport+'-app-snapshot':'snapshot',{...s,wire,session:usb?{transport:usb.transport}:{local:observer.local,remote:observer.remote}});lastAt=performance.now();pendingSnapshot={s,n};
 if(!renderPending){renderPending=true;requestAnimationFrame(()=>{renderPending=false;const next=pendingSnapshot;pendingSnapshot=null;if(next)show(next.s,next.n);});}
}
$('connect').onclick=async()=>{if(connected){stop('Disconnected.');return;}const gen=++generation;$('connect').disabled=true;$('connectionType').disabled=true;diagnostics={};observer=null;clear();loading('chooser','Choose your SP-1','Select the device in the Bluetooth chooser');
 try{await $('setupFrame').contentWindow?.sp1ReleaseSetup?.();if($('connectionType').value==='usb'){
 loading('usb-connect','Connecting over USB','Choose the SP-1 serial port');const client=new UsbClient();usb=client;
 await startApp(client,gen);return;
 }if(!navigator.bluetooth)throw Error('Use a browser with Web Bluetooth, such as desktop Chrome. This page cannot connect in Safari.');
 device=await navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]},{services:['03b80e5a-ede8-4b33-a751-6ce34ec4c700']}],optionalServices:[SERVICE]});device.addEventListener('gattserverdisconnected',()=>{if(gen===generation)stop('Device disconnected. Save session evidence before reconnecting.',{stage:'gatt-disconnect'});},{once:true});
 loading('gatt-connect','Connecting to SP-1','Establishing the Bluetooth connection');
 const server=await device.gatt.connect();loading('service','Initializing companion','Checking services and device capabilities');
 const service=await server.getPrimaryService(SERVICE),caps=new Uint8Array((await (await service.getCharacteristic(CAPS)).readValue()).buffer);
 const expected=[83,80,67,71,1,4,56,20,1,0,0,0];if(caps.length!==12||caps.some((x,i)=>x!==expected[i]))throw Error('Unsupported companion capabilities');
 let application;try{application=await service.getCharacteristic(APPLICATION);}catch(e){if(e.name!=='NotFoundError')throw e;}
 if(application){const link=await service.getCharacteristic('53500007-6014-ce88-a946-12f0f8649172'),limit=await link.readValue();const client=new BleClient(application,limit.getUint16(0,true));usb=client;await startApp(client,gen);return;}
 stream=await service.getCharacteristic(STREAM);let seed=0;while(!seed)seed=crypto.getRandomValues(new Uint32Array(1))[0];observer=new Observer(seed,receiveSnapshot);diagnostics={receivedFragments:0,ackPending:0,ackPeak:0,ackWrites:0};const stats=diagnostics;transfer=0;queue=Promise.resolve();pendingAcks.clear();clear();
 stream.addEventListener('characteristicvaluechanged',e=>{if(gen!==generation)return;try{const v=e.target.value,p=new Uint8Array(v.buffer,v.byteOffset,v.byteLength);stats.receivedFragments++;const replies=observer.receiveFragment(p);snapshotLoading();for(const reply of replies){const ackKey=gen+':'+Array.from(reply).join(',');if(pendingAcks.has(ackKey)){stats.coalescedAcks=(stats.coalescedAcks||0)+1;continue;}pendingAcks.add(ackKey);const fs=fragments(reply,transfer++);stats.ackPending++;stats.ackPeak=Math.max(stats.ackPeak,stats.ackPending);queue=queue.then(async()=>{try{for(const f of fs){if(gen!==generation)return;stats.writeFragment=f[3];stats.writeTransfer=f[2];const writeStart=performance.now();await stream.writeValueWithResponse(f);stats.lastWriteMs=Math.round(performance.now()-writeStart);stats.ackWrites++;}}finally{stats.ackPending--;pendingAcks.delete(ackKey);}}).catch(err=>{if(gen===generation)stop(err.message,{stage:'ack-write',errorName:err.name});});}}catch(err){stop(err.message,{stage:'receive-decode',errorName:err.name});}});
 loading('notifications','Starting live updates','Enabling device notifications');await stream.startNotifications();if(gen!==generation)return;connected=true;$('connect').textContent='Disconnect';$('connect').disabled=false;if(!observer.count)snapshotLoading();notifyCapabilities();log('connected',{name:device.name,capabilities:Array.from(caps),clientNonce:seed});
 }catch(e){if(gen===generation)stop(e.message,{stage:connectionStage,errorName:e.name});}};
setInterval(()=>{if(connected&&!storageActive&&lastAt&&performance.now()-lastAt>3000){$('state').classList.add('stale');updateExplorer(null);renderMix(null);model.setSnapshot(null);for(const id of ['transport','mode','speed','volume','song'])$(id).textContent='—';loading('stale','Waiting for fresh state','Last readings cleared · waiting for the connected device');}},500);
$('export').onclick=()=>{let url=URL.createObjectURL(new Blob([JSON.stringify({schema:1,hardwarePass:false,omittedEvents,events},null,2)],{type:'application/json'}));let a=document.createElement('a');a.href=url;a.download='sp1-companion-session-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};

import './workspace.mjs?v=611';

$('settings').onclick=async()=>{if(!usb||storageActive)return;try{const s=await usb.request(3);$('instantRecord').checked=!!s[0];$('stoppedEffects').value=s[1];stoppedHint();$('settingsResult').textContent='';$('settingsDialog').showModal();}catch(e){status(e.message);}};
$('saveSettings').onclick=async()=>{if(!usb)return;$('saveSettings').disabled=true;$('settingsResult').textContent='Saving…';try{const applied=await usb.request(4,[+$('instantRecord').checked,+$('stoppedEffects').value]);$('instantRecord').checked=!!applied[0];$('stoppedEffects').value=applied[1];stoppedHint();$('settingsResult').textContent='Saved on SP-1.';log('settings-saved',Array.from(applied));}catch(e){$('settingsResult').textContent=e.message;}finally{$('saveSettings').disabled=false;}};
$('closeSettings').onclick=()=>$('settingsDialog').close();
function accessMessage(){return storageActive?'Storage open. Click Done before playback.':!connected?'Loop transfers use USB. Choose USB in Live companion and connect.':usb?.transport!=='usb'?'Loop transfers use USB. Disconnect, choose USB in Live companion, then connect. Bluetooth remains available for Live companion.':'Ready to open storage. Stop playback and wait for saving first.';}
function notifyCapabilities(){ $('storageNotice').hidden=!storageActive;if($('transferAvailability'))$('transferAvailability').textContent=accessMessage();window.dispatchEvent(new Event('sp1-capabilities-changed'));}
window.addEventListener('sp1-enter-transfer',notifyCapabilities);
$('resumeCompanion').onclick=async()=>{
 const button=$('resumeCompanion');button.disabled=true;button.textContent='Closing storage…';$('storageNoticeResult').textContent='Waiting for any active transfer to finish.';
 try{const finish=$('transferFrame').contentWindow?.sp1CloseTransfer;if(!finish)throw Error('Open Library and use Done to close storage.');if(!await finish())throw Error('Storage could not close. Check Library activity.');$('storageNoticeResult').textContent='';loading('initial-snapshot','Receiving live state','Storage closed. Device remains connected.');}
 catch(e){$('storageNoticeResult').textContent=e.message;}
 finally{button.disabled=false;button.textContent='Close storage & resume';}
};

window.sp1Workspace={
 async prepareSetupAccess(){if(document.getElementById("transferFrame").contentWindow?.sp1LibraryActive?.())throw Error("Finish the Library session before opening Setup.");if(connected||usb)await stop("Live connection closed for Setup.",{intentional:true});},
 async prepareDriveAccess(){await $('setupFrame').contentWindow?.sp1ReleaseSetup?.();if(storageActive)throw Error('Finish the older storage session with Done before using SP1 MUSIC.');if(connected||usb)await stop('Live connection closed for USB Drive access.',{intentional:true});},
 async openTransfer(){if(!connected||usb?.transport!=='usb')throw Error(accessMessage());if(storageActive)throw Error('Transfer session already open');storageActive=true;setLayoutEditable(false);updateSettings(false);const session=new TransferClient(usb);try{const layout=await session.enter();storage=session;notifyCapabilities();clear();status('');return {session,layout};}catch(e){log('storage-open-failed',{message:e.message,transport:usb?.transport,detail:e.transportDetail});storageActive=false;setLayoutEditable(connected&&!!usb);updateSettings(connected&&!!usb);notifyCapabilities();if(e.transportDetail&&connected)stop(e.message,{stage:'storage-open',transportDetail:e.transportDetail});throw e;}},
 async closeTransfer(){if(storage)await storage.exit();storage=null;storageActive=false;setLayoutEditable(!!usb);updateSettings(!!usb);notifyCapabilities();},
 get basicMode(){return connected&&basicMode;},
 get connected(){return connected&&usb?.transport==='usb';},get accessMessage(){return accessMessage();}
};
async function startApp(client,gen){
 const caps=await client.connect();if(gen!==generation){await client.close();return;}
 client.onError=e=>{if(gen===generation)stop(e.message,{stage:'application-read'});};
 connected=true;usbCount=0;$('connect').textContent='Disconnect';$('connect').disabled=false;updateSettings(true);setLayoutEditable(true);notifyCapabilities();
 log('application-connected',{transport:client.transport,capabilities:Array.from(caps)});loading('initial-snapshot','Receiving first snapshot','Reading device state');
 let geometry=null,geometryAt=0;const hasGeometry=caps.length>=16&&caps[15]===1;

 const poll=async()=>{if(gen!==generation)return;if(storageActive){geometry=null;setTimeout(poll,250);return;}try{
  const bytes=await client.request(2);if(gen!==generation)return;const value=decode(bytes),now=performance.now();
  if(hasGeometry&&now-geometryAt>(client.transport==='bluetooth'?1200:500)){
   geometryAt=now;try{geometry=decodeGeometry(await client.request(22));}catch(e){if(e.transportDetail)throw e;geometry=null;log('geometry-unavailable',{message:e.message});}
  }
  if(gen!==generation)return;if(geometry?.song===value.song)value.geometry=geometry;
  receiveSnapshot(value,++usbCount,Array.from(bytes));setTimeout(poll,100);
 }catch(e){if(gen===generation)stop(e.message,{stage:'application-snapshot',transportDetail:e.transportDetail,visibility:document.visibilityState,metrics:client.metrics});}};poll();
}


configureLayoutEditor({load:async()=>{if(!usb||storageActive)throw Error('Connect USB or upgraded Bluetooth first.');return usb.request(6);},save:async draft=>{if(!usb||storageActive)throw Error('Connection unavailable');const packed=Array.from({length:8},(_,i)=>draft[2*i]|(draft[2*i+1]<<4));const applied=Array.from(await usb.request(7,packed));log('layout-saved',applied);return applied;}});
