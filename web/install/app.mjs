import {radioTransaction607} from './radio/transaction607.mjs?v=607';
import {automaticFolder,registerAutomatic,findAutomatic,completeAutomatic,exportAutomatic} from './automatic572.mjs?v=572';
import {controllerRelease} from './controller571.mjs?v=612';
import {prepareWorkspace571,validateWorkspace571,applyWorkspace571,recoverWorkspace571,WORKSPACE571_BYTES} from './modules/capacity422/workspace571.mjs?v=571';
import {filesystemReady,runtimeRoute,testAdmission} from './flow574.mjs?v=607';
import {discoverHandoff} from './handoff551.mjs';
import {navigation} from '../navigation.mjs?v=536';
import {prepareSource} from './modules/capacity422/preflight.mjs';
import {recoverySink} from './modules/capacity422/packs.mjs';
import {applyCapacity,recoverCapacity,validateCapacity} from './modules/capacity422/migration-journal.mjs';
import {BackupSerial} from './serial.mjs?v=607.5';
import {Runtime,modeOf,FILTERS,radioMode} from './device.mjs?v=619';
import {inspect422 as inspect} from './modules/capacity422/filesystem.mjs';
import {prepare,apply,recover,consent,validate,finishVerification} from './plan.mjs?v=563';
import {sink,journal,json} from './files.mjs';
import {backupMusic,planLegacy} from './backup/core.mjs';
import {ProgressMeter} from './session.mjs';
import {KEY,readReminder,bindIdentity,libraryRoute,radioRoute,canApply,writeGuard} from './model.mjs?v=607';
import {transact} from './radio/serial.mjs?v=608';
import {newTransaction,originalJournal} from './radio/storage.mjs?v=565';
import {quickRoot,quickTransaction,automaticOriginal,pendingRecovery,markComplete,markNoWrite,exportBrowserRecovery,completedUpdate,idleFailure} from './radio/quick.mjs?v=565';
import {digest,validateRelease} from './radio/plan.mjs?v=565';
const $=x=>document.getElementById(x);
let filesystemRecoveryFolder=null;let importSpaceVerified=false;let automaticParent=null,pendingPreparation=null;
let reminder;try{reminder=readReminder(localStorage.getItem(KEY))}catch{}
// Only unfinished operations bind a future connection to a previous unit.
// Idle/completed reminders are navigation hints, not recovery authority.
if(reminder&&!reminder.busy&&reminder.phase!=='radio-verified'){reminder=null;try{localStorage.removeItem(KEY)}catch{}}
let identityBound=false;
let identity=reminder?.identity,device,port,mode,layout,stage='connect',busy=false,libraryVerified=false,radioChecked=false,radioSkipped=false,verifiedRuntime=false,radioIntent=false,radioUsed=true,radioReenter=false,verifiedMaintenance469=false;
let basicRuntimeVerified=false,upgradeRequested=false,hardwareSetupExit=false;
let radioOneStep=false,radioDirectReturn=false,quickStorageUnavailable=false;
let radioClean=false,radioFullExport=false;
let radioQuick=false,quickStore=null,quickOriginalDir=null;
let connectionState='disconnected',finalCheckRequested=false,unexpectedDisconnect=false;
let recoveryAction='finish';let activeRadioOperation=null;let activeLibraryInstall=false;
let plan,folder,parent,radioOriginal,radioResult,release,firmware,app,meta,ready=false,reviewed=false,phrase='',clean=false,error=null;
let radioPending=!!(reminder?.busy&&['radio-migrate','radio-recover'].includes(reminder.phase));
// Only protect an active operation; setup steps do not enter browser history.
navigation.register('setup',{busy:()=>busy,restore:()=>{}});
const evidence={speedBundle:456,phaseTimings:[],wizard:615,firmwareBuild:null,configuredFirmwareBuild:controllerRelease.build,maintenanceBuild:608,runtime:444,events:[],bluetoothScope:"audio-input",outgoingAudioDeferred:true,publicQualified:false},meter=new ProgressMeter();let rateBase=0;let phaseStarted=0;let phaseName=null;
const phaseLabel=p=>({'prepare-recovery':'Saving recovery files','verify-original':'Checking originals','install':'Installing','verify-written':'Verifying','inspect-filesystem':'Checking library','backup-recordings':'Backing up recordings','verify-saved-backup':'Checking backup','verify-source-guards':'Checking source','compare-original-metadata':'Checking library','migrate-capacity':'Upgrading library','verify-written-metadata':'Verifying library','verify-filesystem-and-takes':'Checking takes','inspect-paged-index':'Checking library'}[p]||String(p).replaceAll('-',' '));
const esc=x=>String(x??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function save(phase=stage,active=busy){if(reminder?.busy&&!identityBound)return;if(radioPending){phase=radioOriginal?'radio-recover':'radio-migrate';active=true}try{if(identity)localStorage.setItem(KEY,JSON.stringify({schema:1,identity,phase,busy:active,forward:radioClean,folder:folder?.name||reminder?.folder||''}))}catch{}}
function finishPhase(){if(phaseName)evidence.phaseTimings.push({phase:phaseName,ms:Math.round(performance.now()-phaseStarted),bytes:meter.last});phaseName=null}
function clearActivity(){finishPhase();meter.reset();rateBase=0;$('activity').hidden=true;$('status').textContent='';$('progress').hidden=true;$('progress').removeAttribute('value');$('progress').removeAttribute('max')}
function note(message){$('progress').hidden=true;evidence.events.push({at:new Date().toISOString(),message});$('activity').hidden=false;$('status').textContent=message;$('log').textContent=evidence.events.slice(-160).map(x=>x.at+' '+x.message).join('\n')}
function progress(p){if(p.phase!==meter.phase||(p.done??0)<meter.last){finishPhase();phaseName=p.phase;phaseStarted=performance.now();rateBase=p.done??0;}const v=meter.update(p,performance.now());v.rate=v.elapsed>=1?(v.done-rateBase)/v.elapsed:null;$('activity').hidden=false;$('status').textContent=phaseLabel(v.phase)+(v.remaining!=null?' · ~'+Math.ceil(v.remaining)+'s remaining':'');$('progress').hidden=false;if(v.total){$('progress').max=v.total;$('progress').value=v.done}else $('progress').removeAttribute('value')}
function fsReady(){return filesystemReady({libraryVerified,basicRuntimeVerified,layout,libraryMode:evidence.libraryMode})}
function connection(state){connectionState=state;const el=$('connectionStatus');if(el){el.dataset.state=state;el.textContent=({connected:'Connected',connecting:'Connecting…',restarting:'Restarting…',waiting:'Waiting for SP-1',disconnected:'Not connected'})[state];if(state==='connected'&&identity)el.title='SP-1 '+identity;else el.removeAttribute('title');}}
function next(s){if(['listen','input-pair','input-listen','input-reconnect','complete'].includes(s)&&!testAdmission({filesystem:fsReady(),verifiedRuntime}))throw Error('Verify Tape Looper startup first');if(s==='complete'&&!radioChecked)throw Error('Verify Bluetooth firmware first');clearActivity();stage=s;error=null;delete evidence.error;delete evidence.stoppedAt;save(s,false);render()}
async function close(){if(device?.metrics)evidence.lastUSBMetrics={...device.metrics};await device?.close().catch(()=>{});device=null;connection('disconnected')}
function bind(id){identity=bindIdentity(identity,id.identity);identityBound=true;connection('connected');evidence.identity=identity;$('device').textContent='SP-1: '+identity;save()}
async function run(fn,phase=({'workspace-review':'apply',review:'apply','radio-install':'radio-migrate','radio-folder':'radio-inspect','radio-recovery':'radio-recover','recover-review':'recover'})[stage]||stage){if(busy)return;busy=true;error=null;clearActivity();save(phase,true);render();let wake;try{
 await navigator.locks.request('sp1-onboarding404',{ifAvailable:true},async lock=>{if(!lock)throw Error('Another setup tab is active. Finish or close it first.');try{wake=await navigator.wakeLock?.request('screen')}catch{}await fn()});
 }catch(e){if(e.radioDiagnostic)evidence.radioFailure=e.radioDiagnostic;phase=e.setupWritePhase||phase;if(e.code==='FILESYSTEM_STORAGE_UNAVAILABLE'||(automaticParent&&pendingPreparation&&!e.setupWritePhase&&['prepare','workspace-upgrade','recovery-location'].includes(stage)&&['QuotaExceededError','SecurityError','NotAllowedError','InvalidStateError'].includes(e.name))){automaticParent=null;parent=null;folder=null;plan=null;evidence.automaticFilesystemRecovery=false;stage='recovery-location';error=null;return;}if(e.radioUnarmed||e.code==='RADIO_READONLY_BENCH'){radioPending=false;phase='radio-unarmed';}if(e.radioVerified){phase='radio-verified';radioPending=false;}if(e.code==='RADIO_STORAGE_UNAVAILABLE'&&!radioPending&&!activeRadioOperation){quickStorageUnavailable=true;error=null;evidence.browserRecovery={available:false,reason:e.cause?.message||e.message};return;}if(e.code==='RADIO_SOURCE_REVIEW'&&e.radioUnarmed&&e.radioSnapshotVerified){
  radioPending=false;radioResult={compatibility:e.report};evidence.radioReview=e.report;
  evidence.radioReadbackVerified=true;evidence.radioBackupSaved=!radioQuick&&!!evidence.radioFolder;
  evidence.reviews=[...(evidence.reviews||[]),{kind:'radio-compatibility',at:new Date().toISOString(),noWrites:true,backupSaved:evidence.radioBackupSaved,folder:evidence.radioFolder}];
  error=null;delete evidence.error;delete evidence.stoppedAt;activeRadioOperation=null;stage='radio-review';clearActivity();
  note(evidence.radioBackupSaved?'Bluetooth backup saved. No radio changes were made.':'Bluetooth check complete. No radio changes were made.');render();await close();return;
 }if(e.code==='RADIO_SOURCE_REVIEW'){radioPending=!!e.cleanRecoveryPending;radioResult={compatibility:e.report};evidence.radioReview=e.report;}if(e.code==='RADIO_RECOVERY_REQUIRED')radioPending=true;error=e.message;evidence.error=e.message;if(device?.statusRows?.length)evidence.runtimeReport={identity,at:new Date().toISOString(),rows:[...device.statusRows]};evidence.stoppedAt={operation:phase,phase:meter.phase||null,bytes:meter.last,at:new Date().toISOString()};evidence.failures=[...(evidence.failures||[]),{...evidence.stoppedAt,message:e.message,command:e.command||device?.lastRequest||null,transportPhase:e.transportPhase||device?.phase||null,code:e.code||null,deviceReply:e.deviceReply||null,radioFolder:evidence.radioFolder||null}].slice(-12);
 if(e.code==='SETUP_BUSY'&&mode==='runtime'&&device&&!unexpectedDisconnect){
  note(e.message);
  return;
 }
 finishPhase();note('Stopped: '+e.message+(radioQuick||evidence.automaticFilesystemRecovery?'. Keep this browser’s data.':'. Keep the original recovery folder.')+' No automatic retry.');stage=e.code==='RADIO_SOURCE_REVIEW'?'radio-review':e.code==='FIRMWARE_REQUIRED'?'firmware-needed':e.code==='RADIO_RECOVERY_REQUIRED'?'radio-recovery-folder':'error';render();await close();}
 finally{try{await wake?.release()}catch{}busy=false;activeRadioOperation=null;activeLibraryInstall=false;if(unexpectedDisconnect){unexpectedDisconnect=false;if(!error&&stage!=='radio-review'){device=null;if(!['listen','input-pair','input-listen','input-issue','complete'].includes(stage)){verifiedRuntime=false;stage='connect';}connection('waiting');}}save(error?phase:stage,!!error&&['apply','radio-migrate','radio-recover','recover','finish'].includes(phase));render()}}
async function require469(){
 try{const row=await device.simple('S470CAPS?','SETUP470 CAPS converter=469 verified=1 flow=postflash');verifiedMaintenance469=true;evidence.setupCapability=row}
 catch(e){if(radioPending)throw e;throw Object.assign(Error('Setup compatibility could not be verified. Download the compatible BIN below, flash through Solderless, then reconnect. Existing recovery folders must be kept.'),{code:'FIRMWARE_REQUIRED'})}
}
async function classify(){
 await require469();libraryVerified=false;verifiedRuntime=false;plan=null;
 // These status commands are explicitly available while the OS owns the
 // volume. Physical identity/read/boot-gate commands are intentionally not.
 const drive=await device.driveState();evidence.drive=drive;
 if(drive.host){connection('connected');next('drive-review');return;}
 const id=await device.identify();bind(id);device.capacity=id.capacity;await device.capabilities();if(!device.largeImports)throw Object.assign(Error('Install the current v4 BIN for larger imports and drive recovery. Your recordings are kept.'),{code:'FIRMWARE_REQUIRED'});const gate=await device.bootGate.status();
 if([1,2,3,4].includes(drive.arena)){next('drive-review');return;}
 if(gate.blocked){layout='interrupted';try{const found=await findAutomatic(identity);if(found){folder=found.folder;filesystemRecoveryFolder=folder;plan=found.plan;evidence.automaticFilesystemRecovery=true;await validateRecoveryPlan();next('recover-review');}else next('recovery');}catch(e){evidence.automaticRecoveryError=e.message;next('recovery');note('Saved recovery needs review. Choose a verified recovery folder to continue.');}return}
 const bytes=await device.read(0,6);
 try{planLegacy(bytes,id.capacity);layout='legacy'}catch{
  try{const x=await inspect(device,progress);layout=x.native?(x.native.format===5?'native':'fixed'):'shared';evidence.workspaceBytes=(x.native?.workspace?.blocks||0)*512;evidence.libraryFormat=x.native?.format??(x.native?2:null)}catch(e){if(device.failed)throw e;layout='unknown';evidence.storageReview=e.message}
 }
 if(layout==='fixed')clean=false;if(layout==='native'){basicRuntimeVerified=false;const workspaceBytes=evidence.workspaceBytes||0;if(!device.largeImports)throw Object.assign(Error('Install the current v4 BIN to enable larger imports. Your recordings are kept.'),{code:'FIRMWARE_REQUIRED'});if(workspaceBytes<WORKSPACE571_BYTES){next('workspace-upgrade');return;}importSpaceVerified=true;}libraryVerified=layout==='native';evidence.layout=layout;
 if(layout==='legacy'&&basicRuntimeVerified&&upgradeRequested){next('prepare');return}
 if(libraryVerified){if(!activeLibraryInstall)await returnLooper();return;}
 next(libraryRoute(layout));
}
async function connect(selectedPort=null){await window.parent.sp1Workspace?.prepareSetupAccess?.();const known=(await navigator.serial.getPorts?.()??[]).filter(p=>{if(p.connected===false)return false;const i=p.getInfo();return FILTERS.some(f=>f.usbVendorId===i.usbVendorId&&f.usbProductId===i.usbProductId)});let selected;if(selectedPort)selected=selectedPort;else if(known.length===1)selected=known[0];else{try{selected=await navigator.serial.requestPort({filters:FILTERS})}catch(e){if(['AbortError','NotFoundError'].includes(e.name))return;throw e}}await close();unexpectedDisconnect=false;connection('connecting');mode=modeOf(selected);port=selected;
 if(mode==='storage'){device=await BackupSerial.connect(selected);device.onTransportFailure=()=>note('USB transfer stopped. Checking the connection…');device.onReadRecovery=info=>note(info.state==='reconnecting'?'Reconnecting USB… Keep it plugged in.':'USB reconnected. Continuing with smaller reads.');await classify();return}
 if(mode==='runtime'){
  device=await Runtime.connect(selected);bind(await device.identify());const result=await device.status();if(device.statusRows?.length)evidence.runtimeReport={identity,at:new Date().toISOString(),rows:[...device.statusRows]};evidence.runtime={identity,result,at:new Date().toISOString()};libraryVerified=device.libraryBackend==='paged';verifiedRuntime=libraryVerified;
  let savedRecovery;try{savedRecovery=await automaticOriginal(identity)}catch(e){if(!['SecurityError','NotAllowedError','InvalidStateError','QuotaExceededError','UnknownError'].includes(e.name))throw e;quickStorageUnavailable=true;evidence.browserRecovery={available:false,reason:e.message};}
  if(savedRecovery){radioOriginal=savedRecovery.original;quickOriginalDir=savedRecovery.dir;radioQuick=true;radioPending=true;}
  if((radioPending||reminder?.phase==='radio-verified')&&!savedRecovery&&!quickStorageUnavailable&&!reminder?.forward){const completed=await completedUpdate(identity,reminder?.folder||null);if(!completed){const idle=await idleFailure(identity,reminder?.folder);if(idle){await markNoWrite(idle.dir,identity);radioPending=false;evidence.unarmedFailureArchived=idle.name;}}if(completed){radioPending=false;radioChecked=true;radioSkipped=false;radioResult={summary:completed.summary,compatibility:completed.compatibility};evidence.completedRadioAfterCrash=completed;evidence.radio={summary:completed.summary,compatibility:completed.compatibility};}}
  if(reminder?.forward&&!savedRecovery){radioPending=false;radioClean=true;}
  evidence.radioRecoveryLookup={pending:radioPending,browserRecoveryFound:!!savedRecovery,browserStorageUnavailable:quickStorageUnavailable,reminderPhase:reminder?.phase||null,forwardReminder:!!reminder?.forward};
  if(radioPending){const saved=savedRecovery;if(saved){radioOriginal=saved.original;quickOriginalDir=saved.dir;radioQuick=true;next('radio-resume');}else if(device.directRadio){evidence.radioRecoveryLookup.action='check-device-status';radioOneStep=false;radioReenter=false;await device.enterRadio();device=null;next('connect');connection('restarting');await reattach535('radio');}else next('radio-recovery-folder');return;}
  if(device.libraryBackend==='legacy'){
   basicRuntimeVerified=true;libraryVerified=false;verifiedRuntime=true;layout='legacy';evidence.libraryMode='basic';evidence.runtimeCapabilityBuild=523;
   next('basic-ready');return;
  }
  basicRuntimeVerified=false;evidence.libraryMode=device.libraryBackend;
  if(libraryVerified&&!importSpaceVerified){await device.enter();device=null;next('connect');connection('restarting');await reattach535('storage');return;}
  if(!libraryVerified){layout='fixed';next('upgrade-enter');return;}
  layout='native';
  const target=runtimeRoute({filesystem:fsReady(),radioChecked,radioNeedsReview:radioResult?.compatibility?.decision==='compatibility-review-required',pairingConfirmed:evidence.inputPairingConfirmed,audioConfirmed:evidence.inputAudioConfirmed,finalCheckRequested});
  if(target==='complete'){finalCheckRequested=false;await close();next('complete');return;}
  if(target==='bluetooth'){await radioChoice();return;}
  await close();next(target);return;
 }
 // A handoff from a just-verified runtime needs one USB open, not an
 // identify/close/reopen cycle. Flow verifies fresh identity, recovery state,
 // banner and direct-return capability before issuing its first command.
 if(mode==='radio'&&radioOneStep&&radioReenter==='migrate'&&!radioUsed&&identity&&verifiedRuntime&&libraryVerified&&!radioOriginal){
  radioDirectReturn=true;hardwareSetupExit=true;radioReenter=false;await runRadio('migrate');return;
 }
 const id=await radioMode(selected,identity);evidence.radioConnection={...id,at:new Date().toISOString()};bind(id);hardwareSetupExit=!!id.offlineExit;radioDirectReturn=!!id.directReturn;
 if(id.cleanForward&&(!id.recoveryRequired||id.cleanPending)&&!radioOriginal){if(radioReenter==='inspect'){radioDirectReturn=!!id.directReturn;radioUsed=false;radioReenter=false;await runRadio('inspect');return;}radioClean=true;radioQuick=true;radioDirectReturn=!!id.directReturn;radioPending=!!id.cleanPending;radioOneStep=true;if(radioReenter==='migrate'&&!radioUsed){radioReenter=false;await runRadio('migrate');return;}radioUsed=true;next('radio-folder');return;}
 const automatic=await automaticOriginal(identity);evidence.radioRecoveryLookup={pending:radioPending,browserRecoveryFound:!!automatic,deviceRecoveryRequired:id.recoveryRequired,cleanForward:id.cleanForward,cleanPending:id.cleanPending};if(automatic){radioOriginal=automatic.original;quickOriginalDir=automatic.dir;radioQuick=true;radioPending=true;if(radioReenter!=='recover')radioReenter=false;}
 if(!id.recoveryRequired&&radioPending&&!automatic){
  const completed=await completedUpdate(identity,reminder?.folder||null);
  if(completed){radioPending=false;radioChecked=true;radioSkipped=false;radioResult={summary:completed.summary,compatibility:completed.compatibility};evidence.completedRadioAfterCrash=completed;evidence.radio={summary:completed.summary,compatibility:completed.compatibility};next('radio-done');return;}
  const idle=await idleFailure(identity,reminder?.folder);if(idle){await markNoWrite(idle.dir,identity);radioPending=false;evidence.unarmedFailureArchived=idle.name;}
  if(radioPending&&id.recoveryStatusKnown){radioPending=false;evidence.staleRadioReminder={cleared:true,reason:'Device explicitly reports no pending radio recovery',at:new Date().toISOString()};}
 }
 if(id.recoveryRequired){radioPending=true;if(!radioOriginal){radioReenter=false;next('radio-recovery-folder');return;}}
 if(!verifiedMaintenance469&&!radioPending&&!radioOneStep){next('radio-check-library');return;}
 // A newly loaded page cannot inherit transaction authority from local storage.
 if(radioReenter&&!radioUsed){const operation=radioReenter;radioReenter=false;await runRadio(operation);return}
 if(radioPending){next(radioOriginal?'radio-recovery':'radio-recovery-folder');return}
 next(radioUsed?'radio-restart':'radio-folder');
}
async function chooseFolder(options){if(typeof showDirectoryPicker!=='function')throw Error('Use desktop Chrome or Edge to save files to a folder');try{return await showDirectoryPicker(options)}catch(e){if(e.name==='AbortError')return null;throw e}}
async function upgradeLibrary(){upgradeRequested=true;clean=false;if(mode==='runtime'){await device.enter();device=null;connection('restarting');next('connect');await reattach535('storage')}else next('prepare')}
async function backupAndUpgrade(){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;automaticParent=null;evidence.automaticFilesystemRecovery=false;await upgradeLibrary();if(device&&mode==='storage'&&stage==='prepare'&&['legacy','fixed'].includes(layout))await prepareLibrary({reuseParent:true,installAfterPrepare:true});}
async function prepareEmpty(){clean=true;if(mode==='runtime'){await device.enter();device=null;connection('restarting');next('connect');await reattach535('storage');if(!device||mode!=='storage'||!['prepare','unknown'].includes(stage))return;clean=true;}next('prepare');await prepareLibrary();}
async function prepareLibrary({reuseParent=false,installAfterPrepare=false}={}){if(!device||mode!=='storage')throw Error('Reconnect the SP-1 first');if(layout==='unknown'&&!clean)throw Error('Unknown storage requires explicit empty-library selection');
 if(!reuseParent||!parent){automaticParent=null;if(clean||layout==='shared'){pendingPreparation=()=>prepareLibrary({reuseParent:true,installAfterPrepare});parent=automaticParent=await automaticFolder(identity);evidence.automaticFilesystemRecovery=true;}else{const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;evidence.automaticFilesystemRecovery=false;}}plan=null;folder=null;
 evidence.installationChoice=clean?'empty':'preserve';evidence.fullRecordingBackup=!clean&&['legacy','fixed'].includes(layout);
 if(layout==='fixed'&&!clean){const source=await prepareSource(device,parent,progress);evidence.musicBackup=source.folder;folder=await source.handle.getDirectoryHandle('migration-recovery');plan=await json(folder,'plan.json');if(automaticParent){await registerAutomatic(automaticParent,folder,plan);filesystemRecoveryFolder=folder;}evidence.plan={identity,folder:folder.name,digest:plan.digest,mode:'capacity'};reviewed=false;next('review');note('Backup verified.');if(installAfterPrepare)await installPreparedPreserving();return;}
 const operation=clean?'clean':layout==='legacy'?'legacy':'preserve';let backup;
 if(operation==='legacy'){note('Backing up recordings.');const x=await backupMusic({identify:async()=>({...await device.identify(),readOnly:true}),read:(l,n)=>device.read(l,n)},parent,progress);backup=x.folder;evidence.musicBackup=backup.name}
 folder=await parent.getDirectoryHandle('SP1-INSTALL-'+identity+'-'+crypto.randomUUID(),{create:true});save('prepare',true);
 try{plan=await prepare(device,sink(folder),{mode:operation,backup},progress)}catch(e){
  if(e.code!=='FILESYSTEM_TARGET_PRESENT'||operation!=='clean'||e.identity!==identity||e.capacity!==device.capacity)throw e;
  // Every proposed base byte was already equal: no erase/write plan exists.
  // Reclassify before preparing the remaining preserving paged upgrade.
  evidence.baseFilesystemAlreadyPresent=true;await classify();
  if(layout==='fixed'){clean=false;return prepareLibrary({reuseParent:true});}
  if(fsReady()){await returnLooper();return;}
  throw Error('The filesystem changed during preparation. Reconnect to check it.');
 }
 if(automaticParent){await registerAutomatic(automaticParent,folder,plan);filesystemRecoveryFolder=folder;}
 evidence.plan={identity,folder:folder.name,digest:plan.digest,mode:operation};reviewed=false;phrase='';next('review');note('Recovery files verified. Ready to review.');if(installAfterPrepare)await installPreparedPreserving();
}
async function installPreparedPreserving(){if(unexpectedDisconnect||connectionState!=='connected'||device?.failed)throw Error('USB disconnected during preparation. Reconnect to check before installing.');if(!plan||plan.mode==='clean')throw Error('Erasing requires a separate confirmation');reviewed=true;try{await installLibrary();}catch(e){e.setupWritePhase='apply';throw e;}}
async function prepareImportSpace({reuseParent=false}={}){if(!reuseParent){pendingPreparation=()=>prepareImportSpace({reuseParent:true});automaticParent=null;parent=automaticParent=await automaticFolder(identity);evidence.automaticFilesystemRecovery=true;}folder=await parent.getDirectoryHandle('SP1-IMPORT-SPACE-'+identity+'-'+crypto.randomUUID(),{create:true});plan=await prepareWorkspace571(device,sink(folder),progress);if(!plan){importSpaceVerified=true;await classify();return;}if(automaticParent){await registerAutomatic(automaticParent,folder,plan);filesystemRecoveryFolder=folder;}evidence.plan={identity,folder:folder.name,digest:plan.digest,mode:'workspace'};next('workspace-review');await expandImportSpace();}
async function finishAutomatic(){if(evidence.automaticFilesystemRecovery&&folder&&plan)await completeAutomatic(folder,plan);}
async function expandImportSpace(){if(unexpectedDisconnect||connectionState!=='connected'||device?.failed)throw Error('USB disconnected during preparation. Reconnect to check before expanding.');if(!plan||plan.identity!==identity||plan.schema!=='sp1-workspace571-v1')throw Error('Check this device’s import-space plan first');save('apply',true);try{await applyWorkspace571(device,sink(folder),plan,await journal(folder),progress);await finishAutomatic();importSpaceVerified=true;await classify();}catch(e){e.setupWritePhase='apply';throw e;}}
async function installLibrary(){if(!canApply({device,identity,plan,folder,reviewed,phrase}))throw Error('Review the prepared operation first');save('apply',true);activeLibraryInstall=true;render();libraryVerified=false;verifiedRuntime=false;
 if(plan.schema==='sp1-capacity422-v1')await applyCapacity(device,await recoverySink(folder),plan,await journal(folder),progress);else await apply(device,sink(folder),plan,await journal(folder),phrase,progress);await finishAutomatic();await classify();if(layout==='fixed'){note('Finishing the library upgrade.');await prepareLibrary({reuseParent:true});if(plan?.schema!=='sp1-capacity422-v1'||plan.identity!==identity)throw Error('Final upgrade plan does not match this device');save('apply',true);await applyCapacity(device,await recoverySink(folder),plan,await journal(folder),progress);await finishAutomatic();await classify();if(!libraryVerified)throw Error('Final library verification failed');}if(!fsReady())throw Error('Post-install filesystem verification did not pass');note('Filesystem verified.');await returnLooper();}
async function reattach535(expected){
 connection('restarting');note('Restarting. Keep USB connected.');
 // Discovery/identification only. An uncertain update is never retried here.
 await new Promise(resolve=>setTimeout(resolve,1800));
 const pid=expected==='radio'?0x5211:expected==='storage'?0x5217:0x5210;
 const found=await discoverHandoff(navigator.serial,pid);
 evidence.usbHandoffs=[...(evidence.usbHandoffs||[]),{expected,products:found.products,discovered:!!found.port,at:new Date().toISOString()}].slice(-12);
 if(found.port){await connect(found.port);return;}connection('waiting');
 note(expected==='radio'?'Reconnect SP-1 to continue Bluetooth setup. No update was started.':expected==='storage'?'Reconnect SP-1 to continue filesystem setup.':'Wake with FN if needed, then reconnect SP-1 to verify startup.');
}
async function beginRadio535(fullBackup=false,exportOnly=false){
 if(!device?.directRadio||!identity||!verifiedRuntime||!fsReady())throw Error('Connect and verify this SP-1 first');
 radioFullExport=fullBackup;
 if(fullBackup){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;radioQuick=false;}
 else{radioQuick=true;}
 radioOneStep=!exportOnly;radioReenter=exportOnly?'inspect':'migrate';radioUsed=false;radioIntent=true;
 evidence.radioRecoveryStorage=radioQuick?'browser-sectors':'folder-full';
 activeRadioOperation='handoff';note('Starting Bluetooth setup. Keep USB connected.');render();await device.enterRadio();device=null;connection('restarting');next('connect');await reattach535('radio');
}
async function radioChoice(){radioIntent=true;radioSkipped=false;if(!fsReady())throw Error('Finish filesystem setup first');if(mode==='runtime'){if(device.directRadio){next('radio-direct-choice');return}await device.enter();device=null;next('connect');connection('restarting');await reattach535('storage');return}
 if(mode!=='storage'||!device)throw Error('Reconnect the SP-1 first');await device.radio();device=null;radioUsed=false;next('connect');connection('restarting');await reattach535('radio');}
async function returnLooper(){if(!device||mode!=='storage'||!libraryVerified)throw Error('Verify the library before returning');await device.onboard();await device.returnToLooper();device=null;connection('restarting');next('verify');await reattach535('runtime');}
async function runRadio(operation){writeGuard({identity,libraryVerified,explicitUpdate:radioOneStep,radio:radioOneStep&&operation==='migrate'?(radioClean?'clean-update':'check-update'):operation,radioDecision:radioResult?.compatibility?.decision,original:radioOriginal});
 if((!radioQuick&&!parent)||!port||mode!=='radio')throw Error('Choose a backup folder and reconnect Bluetooth setup first');
 if(radioUsed)throw Error('Reopen Bluetooth setup before another transaction');radioUsed=true;activeRadioOperation=operation;render();
 const transaction=radioTransaction607(async()=>{
  if(radioQuick){quickStore=quickStore||await quickRoot();const pending=await pendingRecovery(quickStore,identity);if(pending&&operation!=='recover')throw Error('Finish the saved Bluetooth recovery first');return quickTransaction(quickStore,operation,identity);}
  return newTransaction(parent,operation);
 },{exportOriginal:radioFullExport});
 if(operation!=='inspect')radioPending=true;save('radio-'+operation,true);
 const returnDirect=radioDirectReturn&&((radioOneStep&&operation==='migrate')||operation==='recover');
 let radioWriteBytes=0;try{await navigator.locks.request('sp1-radio-setup335',{ifAvailable:true},async lock=>{if(!lock)throw Error('Another Bluetooth installer is active');radioResult=await transact(port,{mode:operation,app,meta,expectedIdentity:identity,original:operation==='recover'?radioOriginal:null,cleanPreferred:!radioOriginal,exportOriginal:radioFullExport,onPolicy:async policy=>{await transaction.policy(policy);radioClean=policy.clean;folder=transaction.dir;evidence.radioFolder=transaction.name;evidence.radioRecoveryStorage=policy.clean?'controller-forward':'original-sectors';},save:transaction.save,prepareRecovery:transaction.prepareRecovery,returnDirect,onVerified:async(summary,compatibility)=>{
  await transaction.complete();if(!transaction.clean&&radioQuick&&operation==='recover'&&quickOriginalDir)await markComplete(quickOriginalDir,identity);
  radioPending=false;radioChecked=operation==='migrate'||operation==='inspect'&&radioRoute(compatibility)==='radio-done';radioSkipped=operation==='recover';evidence.radio={summary,compatibility};save('radio-verified',false);
 },onProgress:(phase,fraction,manifest)=>{
  if(manifest)radioWriteBytes=manifest.sectors.length*4096;const total=phase==='write'?radioWriteBytes:524288;
  if(phase==='plan')note(transaction.clean?(manifest.sectors.length?'Updating Bluetooth…':'Bluetooth is current. Verifying…'):'Recovery verified. '+manifest.sectors.length+' changed sectors.');else progress({phase:phase==='backup'&&radioQuick?'Bluetooth check':'Bluetooth '+phase,done:fraction*total,total});
 }});});}catch(e){if(e.radioUnarmed&&!e.radioVerified&&radioQuick&&!transaction.clean&&transaction.dir){try{await markNoWrite(transaction.dir,identity)}catch(seal){e.radioUnarmed=false;e.message+=' Cancellation journal could not be sealed: '+seal.message;}}throw e;}
 evidence.radio={summary:radioResult.summary,compatibility:radioResult.compatibility};
 if(operation==='inspect'&&radioFullExport){next('radio-exported');return;}
 if(operation==='inspect'){let target=radioRoute(radioResult.compatibility);radioChecked=target==='radio-done';if(target==='radio-install'&&!libraryVerified)target='radio-check-library';next(target)}else{radioChecked=operation==='migrate';radioSkipped=operation==='recover';next('radio-done')}
 if(radioResult.returnRequested){radioIntent=false;next('verify');await reattach535('runtime');return;}
 if(returnDirect){await exitRadio();return;}
 note(operation==='inspect'?'Radio backup verified. '+(radioResult.compatibility.decision==='compatibility-review-required'?'This source needs review; no radio write is authorized.':'Compatibility check complete.'):'All radio bytes verified.');
}
async function exitRadio(){radioIntent=stage==='radio-check-library';if(radioPending)throw Error('Recover the interrupted radio operation first');await radioMode(port,identity,true);if(radioDirectReturn){next('verify');await reattach535('runtime');}else{next('connect');await reattach535('storage');}}
async function enterRadioRecovery554(){
 if(!radioOriginal)throw Error('Choose this SP-1’s original Bluetooth recovery first');
 if(!radioQuick&&!parent){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;}
 radioReenter='recover';radioUsed=false;radioIntent=true;radioOneStep=false;
 if(mode==='runtime'){if(!device?.directRadio)throw Error('Reconnect in Bluetooth recovery mode');await device.enterRadio();}
 else if(mode==='storage')await device.radio();
 else{await reopenRadioRecovery535();return;}
 device=null;connection('restarting');next('connect');await reattach535('radio');
}
async function reopenRadioRecovery535(){
 radioReenter='recover';radioIntent=true;
 if(radioDirectReturn){await radioMode(port,identity,true,true);radioUsed=false;next('connect');await reattach535('radio');}
 else{radioUsed=false;next('connect');note('Restart the SP-1, then reconnect to recover Bluetooth.');}
}
async function validateRecoveryPlan(){if(plan.schema==='sp1-workspace571-v1')await validateWorkspace571(plan,sink(folder));else if(plan.schema==='sp1-capacity422-v1')await validateCapacity(plan,await recoverySink(folder));else await validate(plan,sink(folder));}
async function loadRecovery(){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;folder=selected;automaticParent=null;evidence.automaticFilesystemRecovery=false;plan=await json(folder,'plan.json');if(plan.schema==='sp1-workspace571-v1')await validateWorkspace571(plan,sink(folder));else if(plan.schema==='sp1-capacity422-v1')await validateCapacity(plan,await recoverySink(folder));else await validate(plan,sink(folder));bind({identity:plan.identity});next('recover-review')}
function action(label,fn,secondaryLabel=null,secondary=null){$('next').textContent=label;$('next').onclick=()=>run(fn);$('next').disabled=busy||!ready;$('secondary').hidden=!secondaryLabel;$('secondary').textContent=secondaryLabel||'';$('secondary').onclick=secondary?()=>run(secondary):null;$('secondary').disabled=busy}
function card(title,description,label,fn,hint='',fields='',secondaryLabel=null,secondary=null){const primary=$('next'),actions=$('secondary').parentElement;if(primary.parentElement!==actions)actions.prepend(primary);$('title').textContent=title;$('description').textContent=description;$('description').hidden=!description;$('hint').textContent=hint;$('hint').hidden=!hint;$('fields').innerHTML=fields;action(label,fn,secondaryLabel,secondary)}
async function startRadio554(direct,fullBackup=false,exportOnly=false){
 if(!direct&&mode==='radio'&&radioUsed){
  if(fullBackup&&!parent){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;}
  radioFullExport=fullBackup;radioQuick=!fullBackup;radioReenter=exportOnly?'inspect':'migrate';radioOneStep=!exportOnly;radioUsed=false;
  await radioMode(port,identity,true,true);connection('restarting');next('connect');await reattach535('radio');return;
 }
 if(direct){await beginRadio535(fullBackup,exportOnly);return;}
 if(fullBackup){const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;radioQuick=false;radioOriginal=null;quickOriginalDir=null;evidence.radioRecoveryStorage='folder-full';}
 else{radioQuick=true;}
 radioFullExport=fullBackup;
 radioOneStep=radioDirectReturn;await runRadio(exportOnly?'inspect':radioOneStep?'migrate':'inspect');
}
function reconnectInstruction607(){
 const expected=evidence.usbHandoffs?.at(-1)?.expected;
 if(expected==='storage')return 'SP-1 is in filesystem setup mode. Click Reconnect SP-1 and select its SP-1 USB port. Keep USB connected.';
 if(expected==='radio')return 'SP-1 is in Bluetooth setup mode. Click Reconnect SP-1 and select the SP-1 Bluetooth Setup USB port. Keep USB connected.';
 return 'Click Reconnect SP-1 and select its USB port.';
}
function renderRadioStart(direct){
 card(radioClean&&radioPending?'Finish Bluetooth setup.':'Set up Bluetooth.','Checks your Bluetooth firmware and updates it only if needed. An update resets Bluetooth pairings.',radioClean&&radioPending?'Finish setup':'Set up Bluetooth',()=>startRadio554(direct),'Keep USB connected. Your SP-1 will restart automatically.');
}
function renderChecklist(){
 const list=$('setupChecklist');if(!list)return;
 list.hidden=!identity||stage!=='complete';
 if(list.hidden)return;
 const filesystemReady=libraryVerified&&(layout==='native'||evidence.libraryMode==='paged');
 const filesystem=filesystemReady?'Done':layout==='interrupted'?'Recovery needed':basicRuntimeVerified||layout==='legacy'?'Upgrade available':layout==='unknown'?'Setup needed':layout==='shared'?'Setup available':'Unverified';
 const bluetooth=radioPending?'Recovery needed':radioChecked?'Done':radioResult?.compatibility?.decision==='compatibility-review-required'?'Needs review':radioResult?.compatibility?.decision==='bench-migration-candidate'?'Update available':'Unverified';
 const audio=evidence.inputAudioConfirmed&&evidence.inputReconnectConfirmed;
 list.innerHTML=[['Filesystem',filesystem,filesystemReady],['Bluetooth',audio&&radioChecked?'Done':bluetooth,radioChecked]].map(([name,status,done])=>'<li><span class="check-state '+(done?'done':'')+'" aria-hidden="true">'+(done?'✓':'○')+'</span><span>'+name+'</span><span class="check-status">'+status+'</span></li>').join('');
}
function render(){$('reviewReturn').hidden=stage!=='radio-review'||mode==='runtime';$('reviewReturn').disabled=busy||radioPending;$('filesystemRecoveryExport').hidden=!filesystemRecoveryFolder;$('filesystemRecoveryExport').disabled=busy;
 connection(connectionState);renderChecklist();const backup=$('radioFullBackup');if(backup){backup.hidden=busy||radioPending||!['radio-direct-choice','radio-folder'].includes(stage);backup.disabled=busy;backup.onclick=()=>run(()=>startRadio554(stage==='radio-direct-choice',true,true));$('radioBackupInfo').hidden=backup.hidden;}
 document.body.classList.remove('setup-tasks');
 if(!busy)$('activity').hidden=!(error&&device&&mode==='runtime'&&evidence.failures?.at(-1)?.code==='SETUP_BUSY');
 $('step').hidden=!busy;$('step').textContent=busy?'KEEP USB CONNECTED':({flash:'START HERE',connect:'CONNECT YOUR SP-1',review:'REVIEW YOUR INSTALL',listen:'MAKE SOME MUSIC',complete:'READY'})[stage]||'ONE STEP AT A TIME';
 $('diagnostics').disabled=false;
 if(busy&&stage==='prepare'){card(clean?'Preparing empty filesystem.':['legacy','fixed'].includes(layout)?'Backing up recordings.':'Preparing filesystem.','Keep USB connected.','Working…',async()=>{});return;}
 if(busy&&activeLibraryInstall){card('Finishing installation.','Installing and verifying your library.','Working…',async()=>{},'Keep this tab open and USB connected.');return}
 if(busy&&activeRadioOperation){card(activeRadioOperation==='handoff'?'Starting Bluetooth setup.':activeRadioOperation==='inspect'?'Checking Bluetooth compatibility.':activeRadioOperation==='recover'?'Recovering Bluetooth.':'Setting up Bluetooth.','Keep USB connected until this finishes.','Working…',async()=>{},'');return}
 switch(stage){
 case 'flash':case 'flash-instructions':case 'firmware-needed':{
 const firmware=release?.variants?.keepRecordings;
 const links=firmware?'<p><a class="firmware-download button" download href="'+esc(firmware.url)+'">Download v4</a></p><p>Flashing won’t erase your recordings. Reconnect afterward to finish setup.</p><p>Flash with <a href="https://solderless.engineering/" target="_blank" rel="noopener noreferrer">Solderless</a>, then reconnect here.</p>':'<p>The firmware download is unavailable.</p>';
 card('Get compatible firmware.','Download and flash v4.','Reconnect & check',async()=>{verifiedMaintenance469=false;await connect()},'Keep any existing recovery folders.',links);break;}
 case 'connect':case 'verify':{
  const reconnect=stage==='verify'||connectionState==='waiting';
  const audioHint='If your computer switches audio output to SP-1, switch back to your usual speakers or headphones in sound settings. Keep playback stopped during setup.';
  card(reconnect?'Reconnect your SP-1.':'Connect your SP-1.',
   reconnect?reconnectInstruction607():'Connect USB. Keep SP-1 awake and stopped. Close other SP-1 connections.',
   reconnect?'Reconnect SP-1':'Connect SP-1',connect,
   reminder?'Reconnect to check this SP-1. '+audioHint:audioHint);
  break;
 }
 case 'drive-review':{
 if(device?.pendingWipe){card('An erase installation is pending.','Flash the current v4 before recovering to keep saved recordings. An older erase BIN would erase them after recovery.','Get v4',async()=>next('firmware-needed'),'Keep your original recovery files.');break;}
 const discardable=!evidence.drive?.host&&[1,2].includes(evidence.drive?.arena);
 if(discardable){card('Cancel the unfinished import.','Return to your saved library. Files copied during this unfinished drive session will not be imported.','Cancel import & verify',async()=>{
  const current=await device.identify();if(current.identity!==identity)throw Error('Different SP-1');
  const currentDrive=await device.driveState();if(currentDrive.host||![1,2].includes(currentDrive.arena))throw Error('Drive session changed. Reconnect to inspect it.');
  evidence.driveCancellation=await device.discardDrive();await close();next('connect');note('Saved library verified. Wake with FN if needed, then reconnect.');
 },'Saved recordings are verified before returning.','', 'Save diagnostics',async()=>downloadEvidence());break;}
 const recoverable=!evidence.drive?.host&&evidence.drive?.arena===3;
 card(recoverable?'Recover the pending drive edit.':'Finish the USB Drive session first.',recoverable?'Recover the interrupted drive edit before continuing.':'Eject SP1 MUSIC in Finder and wait for Tape Looper. If it is missing from Finder, check Disk Utility for SP1 MUSIC and eject it there. Then reconnect.',recoverable?'Recover & verify':'Reconnect & check',recoverable?async()=>{evidence.driveRecovery=await device.recoverDrive();await close();next('connect');note('Recovery finished. Reconnect to verify.');}:connect,'Keep the original recovery files.','', 'Save diagnostics',async()=>downloadEvidence());break;}
 case 'unknown':card('Set up filesystem.','This storage is not recognized. An empty filesystem will erase its music, settings and other firmware files.','Use an empty library',prepareEmpty,'You’ll confirm erasing before installation.');break;
 case 'basic-ready':case 'basic-maint-ready':card('Upgrade filesystem.','Back up your recordings, then upgrade for takes and USB Library access.','Back up & upgrade',backupAndUpgrade,'Your existing recordings and settings will be preserved.','','Start empty',async()=>{clean=true;next('clean-confirm')});break;

 case 'upgrade-enter':card('Upgrade filesystem.','Back up your recordings, then upgrade for takes and USB Library access.','Back up & upgrade',backupAndUpgrade,'Audio pauses during setup.','','Start empty',async()=>{clean=true;next('clean-confirm')});break;
 case 'prepare':card(clean?'Prepare an empty filesystem.':['legacy','fixed'].includes(layout)?'Keep your recordings.':'Set up Tape Looper storage.',clean?'Installation recovery is saved automatically. Existing recordings will be erased only after you confirm.':['legacy','fixed'].includes(layout)?'Choose a folder to back up your recordings and settings before upgrading the filesystem.':'Your existing files stay on the SP-1; Tape Looper gets its own storage folder. Installation recovery is saved automatically.',clean?'Continue':['legacy','fixed'].includes(layout)?'Back up & upgrade':'Set up filesystem',()=>prepareLibrary({installAfterPrepare:!clean}),'Keep USB connected until Tape Looper returns.','',clean?null:'Start empty',clean?null:async()=>next('clean-confirm'));break;
 case 'clean-confirm':card('Start empty?','This erases this SP-1’s music, settings and other firmware files. Existing recordings will not be backed up.','Use an empty library',prepareEmpty,'You’ll confirm before installation.');break;
 case 'review':{
 const destructive=plan?.mode==='clean',reviewIdentity=identity,reviewPlan=plan;if(!destructive)reviewed=true;
 card(destructive?'Set up an empty filesystem.':'Upgrade filesystem.',destructive?'Recovery files are verified. Existing music, settings and other firmware files will be erased.':['legacy','fixed'].includes(layout)?'Backup verified. Your recordings will be kept. This enables takes and USB Library access.':'Installation recovery verified. Existing files stay on the SP-1. This enables takes and USB Library access.',destructive?'Erase & install':'Install filesystem',async()=>{if(destructive){if(identity!==reviewIdentity||plan!==reviewPlan)throw Error('Device or prepared installation changed. Reconnect to check.');reviewed=true;phrase=consent(reviewIdentity);}await installLibrary();},'Keep USB connected until Tape Looper returns. Your SP-1 will restart automatically.');
 $('next').disabled=busy||!canApply({device,identity,plan,folder,reviewed:destructive?true:reviewed,phrase:destructive?consent(identity):phrase});break;}

 case 'recovery-location':card('Choose a recovery folder.','This browser cannot save installation recovery automatically. Choose a folder on your computer.','Choose folder & continue',async()=>{const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;automaticParent=null;evidence.automaticFilesystemRecovery=false;await pendingPreparation();},'Nothing has been installed.');break;
 case 'workspace-upgrade':card('Expand import space.','Keep your recordings and make room for full-length audio imports. Recovery is saved automatically.','Expand & verify',prepareImportSpace,'Uses unused device space; saved recordings stay in place.');break;
 case 'workspace-review':card('Expanding import space.','Keep USB connected until Tape Looper returns.','Working…',async()=>{});$('next').disabled=true;break;
 case 'radio-direct-choice':renderRadioStart(true);break;
 case 'radio-restart':card('Reopen Bluetooth setup.','Reconnect to start a new Bluetooth check.','Continue',async()=>{await exitRadio();radioIntent=true});break;
 case 'radio-folder':renderRadioStart(false);break;
 case 'radio-check-library':card('Check filesystem first.','Verify the filesystem before updating Bluetooth.','Check filesystem',exitRadio);break;
 case 'radio-install':card('Bluetooth update available.',radioQuick?'Compatible radio. Recovery will be saved automatically before updating.':'Compatible radio. We’ll back it up, update it and verify the result.','Install Bluetooth',async()=>{radioReenter='migrate';await exitRadio();radioIntent=true;note(radioQuick?'Reconnect to update. Automatic recovery enabled.':'Reconnect to update. Backup folder retained.');},radioResult?.compatibility?.pairing_records_change?'Pair your phone again after updating.':'Saved pairings will be kept.');break;
 case 'radio-review':{
 const saved=evidence.radioBackupSaved,update=radioResult?.compatibility?.controllerUpdateRequired;
 if(radioResult?.compatibility?.forwardRecovery){card('Bluetooth configuration needs review.',radioResult.compatibility.reason||'The hardware configuration is outside this installer’s supported profile. No flash writes were sent in this attempt.','Save diagnostics',async()=>downloadEvidence(),'Keep USB connected. Save diagnostics before retrying.','', 'Reconnect & check',async()=>{radioResult=null;await connect()});break;}
 const files=update?'<p><a class="firmware-download button" download href="'+esc(release.variants.keepRecordings.url)+'">Download v4</a></p><p>Keeps your recordings. Flash with <a href="https://solderless.engineering/" target="_blank" rel="noopener noreferrer">Solderless</a>, then reconnect Setup.</p>':saved?'<p>In <strong>'+esc(evidence.radioFolder)+'</strong>, send <code>radio-backup.bin</code> and <code>compatibility.json</code> privately for review.</p>':'';
 card(update?'Update the controller BIN.':saved?'Bluetooth backup saved.':'Bluetooth needs review.',update?'This Feldd radio is recognized. Updating it needs the newer controller BIN; no radio changes were made.':saved?'Backup complete. This radio firmware needs compatibility review; no radio changes were made.':'Compatibility check complete. This radio firmware needs review; no radio changes were made.','Save diagnostics',async()=>downloadEvidence(),saved?'Bluetooth backup saved. Keep it private.':update?'Reconnect after flashing the current firmware.':'Save diagnostics for review.',files, 'Reconnect & check',async()=>{radioResult=null;delete evidence.radioReview;await connect()});break;}
 case 'radio-done':card('Bluetooth check complete.',radioChecked?'Radio verified. Return to Tape Looper to test audio.':'Original radio restored and verified.','Continue',exitRadio,'');break;
 case 'listen':case 'input-pair':case 'input-listen':case 'input-reconnect':case 'input-issue':next(radioChecked&&verifiedRuntime&&fsReady()?'complete':'connect');break;
 case 'radio-exported':card('Bluetooth export saved.','The original Bluetooth firmware is saved in your selected folder.','Continue setup',async()=>{radioResult=null;radioChecked=false;radioFullExport=false;await exitRadio();});break;
 case 'complete':card('Your Tape Looper is ready.','Filesystem and Bluetooth firmware verified.','Open companion',async()=>{window.top.location.href=new URL('../#live',location.href).href},radioResult?.compatibility?.pairingsReset?'Pair again: forget the old SP-1 entry on your phone, then tap FN twice and hold the third press for 1 second. The four TRACK LEDs slowly fade while pairing.':radioChecked?'Enable Bluetooth: tap FN twice and hold the third press for 1 second. The four TRACK LEDs slowly fade while pairing.':'','', 'Save setup summary',async()=>downloadEvidence());
 // Navigation is not a setup transaction: marking it busy makes the shared
 // history guard bounce the hash change back to Setup.
 $('next').onclick=()=>{if(busy||stage!=='complete')return;if(window.top!==window)navigation.route('live');else location.assign(new URL('../#live',location.href).href)};
 break;
 case 'recovery':card('Finish an interrupted installation.','Choose this SP-1’s original installation recovery folder.','Choose original recovery folder',loadRecovery,'Use the original folder for this device—not an empty folder.');break;
 case 'recover-review':card('Recover this installation.','Recovery files match. Finish the installation or restore its original affected sectors.','Continue recovery',async()=>{const op=recoveryAction;if(plan.schema==='sp1-workspace571-v1')await recoverWorkspace571(device,sink(folder),plan,await journal(folder),{rollback:op!=='finish'},progress);else if(plan.schema==='sp1-capacity422-v1')await recoverCapacity(device,await recoverySink(folder),plan,await journal(folder),{rollback:op!=='finish'},progress);else if(op==='finish')await finishVerification(device,sink(folder),plan,await journal(folder),progress);else await recover(device,sink(folder),plan,await journal(folder),progress);await finishAutomatic();await classify()},evidence.automaticFilesystemRecovery?'Recovery was found in this browser. Keep USB connected.':'Keep the recovery folder and USB connected.','<label>Recovery action<select id="recoveryChoice"><option value="finish">Finish and verify installation</option><option value="rollback">Restore original affected sectors</option></select></label>');$('recoveryChoice').value=recoveryAction;$('recoveryChoice').onchange=e=>{recoveryAction=e.target.value};break;
 case 'radio-resume':card('Recover Bluetooth.','Saved recovery was found for this SP-1. Restore and verify it before continuing.','Restore & verify',enterRadioRecovery554,'Keep USB connected and keep this browser’s data.');break;
 case 'radio-recovery-folder':card('Recover Bluetooth.','Reconnect to check automatic recovery. If you saved a backup folder earlier, select that existing folder—not a new empty one.','Reconnect & check',connect,'Keep USB connected and use the same browser and site as before.','', 'Choose existing recovery folder',async()=>{const selected=await chooseFolder({mode:'read'});if(!selected)return;let original;try{original=await originalJournal(selected)}catch(e){evidence.radioRecoverySelection={folder:selected.name,error:e.message,at:new Date().toISOString()};note(e.name==='NotFoundError'?'This folder is missing recovery.json or radio-backup.bin. Choose the original update subfolder, or reconnect to check automatic recovery.':'Recovery files could not be verified: '+e.message);return;}bind({identity:original.manifest.identity});radioOriginal=original;next(mode==='runtime'||mode==='storage'?'radio-resume':'radio-recovery')});break;
 case 'radio-recovery':if(radioQuick&&radioOriginal){card('Recover Bluetooth.','Saved recovery found for this SP-1. Keep USB connected.','Restore & verify',async()=>{quickStore=await quickRoot();await reopenRadioRecovery535()},'Use this browser until recovery completes.');break;}card('Restore the original radio image.','Choose a new computer folder for the recovery log. We’ll restore and verify the original radio.','Choose folder & reopen recovery',async()=>{const selected=await chooseFolder({mode:'readwrite'});if(!selected)return;parent=selected;await reopenRadioRecovery535();});break;
 case 'error':card('Let’s check before continuing.',error||'The previous operation stopped.','Reconnect & check',connect,radioClean?'Keep USB connected. Reconnect to finish Bluetooth setup.':radioQuick||evidence.automaticFilesystemRecovery?'Keep this browser’s data. Reconnect to check without retrying.':folder?'Keep the recovery folder. Reconnecting checks without retrying installation.':'Reconnect to check this SP-1. Installation will not be retried automatically.','', 'Save diagnostics',async()=>downloadEvidence());break;
 }
 if(busy){$('next').disabled=true;$('secondary').disabled=true;for(const el of $('fields').querySelectorAll('input,select'))el.disabled=true}
 if(!ready)$('next').disabled=$('secondary').disabled=true;
}
function downloadEvidence(){const a=document.createElement('a'),url=URL.createObjectURL(new Blob([JSON.stringify({...evidence,stage,identity,layout,libraryVerified,verifiedRuntime,radioChecked,radioSkipped,recoveryFolder:folder?.name},null,2)]));a.href=url;a.download='sp1-onboarding444-summary.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),10000)}
$('diagnostics').onclick=downloadEvidence;
$('radioRecoveryExport').onclick=async()=>{const button=$('radioRecoveryExport');button.disabled=true;try{const data=await exportBrowserRecovery(identity),a=document.createElement('a'),url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)]));a.href=url;a.download='sp1-radio-recovery540-'+(identity||'unidentified')+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),10000)}catch(e){note('Could not export browser recovery: '+e.message)}finally{button.disabled=false}};
navigator.serial?.addEventListener?.('disconnect',event=>{if((event.port||event.target)!==port)return;unexpectedDisconnect=!['restarting','waiting'].includes(connectionState);connection('waiting');if(busy)return;unexpectedDisconnect=false;device=null;if(!['listen','input-pair','input-listen','input-issue','complete'].includes(stage)){verifiedRuntime=false;stage='connect';}render();});
// Release an idle normal-runtime connection before another workspace uses USB.
window.sp1ReleaseSetup=async()=>{if(busy||radioPending)throw Error('Finish setup or recovery before connecting another view.');if(device&&mode!=='runtime')throw Error('Return to Tape Looper in Setup first.');await close();if(!['listen','input-pair','input-listen','input-issue','complete'].includes(stage)){stage='connect';verifiedRuntime=false;}render();};
window.addEventListener('beforeunload',e=>{if(busy){e.preventDefault();e.returnValue=''}});
render();
try{
 if(!isSecureContext||!navigator.serial||!navigator.locks)throw Error('Use desktop Chrome or Edge on HTTPS or localhost with USB and local-folder access.');
 const load=async file=>{const r=await fetch(file,{cache:'no-store'});if(!r.ok)throw Error('Missing release file: '+file);return new Uint8Array(await r.arrayBuffer())};
 release=JSON.parse(new TextDecoder().decode(await load('release.json')));if(release.version!==444||release.firmwareBuild!==controllerRelease.build||release.runtime!==444||release.file!==controllerRelease.file)throw Error('Wrong release manifest');
 if(release.sha256!==controllerRelease.standardSHA256||release.variants?.keepRecordings?.sha256!==controllerRelease.standardSHA256)throw Error('Firmware manifest mismatch');
 app=await load('radio/assets/release-application.bin');meta=JSON.parse(new TextDecoder().decode(await load('radio/assets/release.json')));await validateRelease(app,meta);ready=true;render();
}catch(e){error=e.message;stage='error';render();$('next').disabled=$('secondary').disabled=true;note(e.message)}

$('filesystemRecoveryExport').onclick=async()=>{if(busy||!filesystemRecoveryFolder)return;try{const destination=await chooseFolder({mode:'readwrite'});if(!destination)return;await exportAutomatic(filesystemRecoveryFolder,destination);note('Installation recovery saved.');}catch(e){note('Could not export installation recovery: '+e.message)}};

$('reviewReturn').onclick=()=>run(async()=>{if(stage!=='radio-review'||radioPending||mode==='runtime')return;await exitRadio();});
