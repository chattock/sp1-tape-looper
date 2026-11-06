import {cleanPlan607} from './clean607.mjs?v=607';
import {SIZE,check,hex,bytes,equal,makePlan,recoveryPlan,inspectImage} from './plan.mjs?v=565';
export const BANNER='BTID335 READY base=guarded-helper probe sdk-reusable-plan-bench-b405';
export class Flow {
 constructor({mode,app,meta,save,expectedIdentity=null,original=null,onProgress=()=>{},prepareRecovery=null,returnDirect=false,cleanPreferred=false,exportOriginal=false,onPolicy=async()=>{}}){
  Object.assign(this,{mode,app,meta,save,expectedIdentity,original,onProgress,prepareRecovery,returnDirect,cleanPreferred,exportOriginal,onPolicy,identity:null,sent:false,nonce:null,seq:0,waitAck:null,queue:[],armed:false,started:false,verified:0,ready:0,ended:false,planVerified:false,manifest:null,expected:null,report:null});
  this.data=[new Uint8Array(SIZE),new Uint8Array(SIZE)];this.length=[0,0];this.count=[0,0];this.lines=[];this.source565=false;this.transport=null;this.recoveryRequired=null;this.preambleBanners=0;
 }
 next(){check(this.waitAck===null,'More than one command in flight');if(!this.queue.length)return null;const [op,args]=this.queue.shift();this.waitAck=++this.seq;if(op==='ARM335')this.armed=true;if(op==='SEAL335')this.started=true;return `${op} ${this.nonce} ${this.seq.toString(16).padStart(8,'0')}${args?' '+args:''}\n`;}
 async line(line){
  check(!this.ended,'Data after final report');check(this.lines.length<14000,'Report bound');this.lines.push(line);let m;
  if(line.startsWith('BTID607 CLEAN ')){
   m=/^BTID607 CLEAN profile=static-v1 forward=1 pending=([01])$/.exec(line);check(m&&!this.sent&&!this.cleanCapability,'Invalid clean Bluetooth capability');this.cleanCapability=true;this.cleanPending=m[1]==='1';this.cleanChosen=this.cleanPreferred&&this.mode==='migrate'&&!this.original;
  }else if(line.startsWith('BTID565 SOURCE ')){
   check(!this.sent&&!this.source565&&line==='BTID565 SOURCE feldd565-v1 protected=1 vs=append-only','Invalid source migration capability');this.source565=true;
  }else if(line.startsWith('BTID545 SPEED ')){
   check(this.identity&&!this.sent&&!this.transport&&line==='BTID545 SPEED autobaud=1000000 export=batched pipeline=1 rx_wakeup=1 read_only=0','Invalid radio link capability');
   this.transport={internal_uart_baud:1000000,autobaud:true,batched_export:true,read_pipeline:true,rx_wakeup:true,reset_ack:false};
  }else if(line.startsWith('BTID545 AUTOBAUD ')){
   check(this.transport&&this.sent&&!this.transport.reset_ack&&this.length[0]===0&&line==='BTID545 AUTOBAUD baud=1000000 reset_ack=1','Invalid radio autobaud acknowledgement');this.transport.reset_ack=true;
  }else if(/^BTID54[34] SPEED .* read_only=1$/.test(line)&&this.mode!=='inspect'){
   check(!this.sent&&!this.armed,'Unexpected read-only capability');throw Object.assign(Error('This BIN is a read-only radio test. Use the standard Tape Looper BIN for Bluetooth setup.'),{code:'RADIO_READONLY_BENCH'});
  }else if(line.startsWith('BTID524 SETUP ')){
   m=/^BTID524 SETUP recovery=([01]) return_gesture=t2\+t3 hold_ms=3000$/.exec(line);check(m,'Invalid setup recovery status');
   this.recoveryRequired=m[1]==='1';
   if(m[1]==='1'&&!this.original&&!(this.cleanChosen&&this.cleanPending))throw Object.assign(Error('Bluetooth recovery is required. Choose the original radio recovery folder.'),{code:'RADIO_RECOVERY_REQUIRED'});
  }else if(line.startsWith('BTID335 SETUP ')){
   m=/^BTID335 SETUP identity=([0-9a-f]{16}) radio_writes=bounded-host-plan audio=disabled storage=disabled$/.exec(line);check(m&&!this.identity,'Wrong adapter identity or device restarted');this.identity=m[1];check(this.expectedIdentity===this.identity,"Setup device differs from verified336 handoff");
  }else if(line===BANNER){
   // Closing/reopening CDC can leave one old READY in the host input queue.
   // It grants no authority: still require a fresh identity and another READY.
   if(!this.identity){check(!this.sent&&this.preambleBanners++===0,'Repeated adapter banner before identity');return null;}
   check(!this.sent,'Repeated adapter banner after startup');
   if(this.returnDirect)check(this.recoveryRequired!==null&&this.lines.includes('BTID535 SETUP direct_return=1'),'Missing guarded return/recovery capability');
   this.sent=true;return 'SP1PLAN335\n';}
  else if(line.startsWith('BTID335 FLASH ')){
   m=/^BTID335 FLASH ([01]) ([0-9a-f]{5}) ([0-9a-f]+)$/.exec(line);check(this.sent&&m,'Unexpected flash block');const stage=+m[1],at=parseInt(m[2],16),b=bytes(m[3]);
   check((!stage||this.planVerified)&&at===this.length[stage]&&b.length&&b.length===Math.min(240,SIZE-at)&&(!this.nonce||stage),'Missing/repeated/oversize/unexpected flash block');
   this.data[stage].set(b,at);this.length[stage]+=b.length;this.count[stage]++;
   if(this.count[stage]%128===1||this.length[stage]===SIZE)this.onProgress(stage?'verify':'backup',this.length[stage]/SIZE);
  }else if(line.startsWith('BTID335 READY_PLAN ')){
   m=/^BTID335 READY_PLAN ([0-9a-f]{16})$/.exec(line);check(this.sent&&!this.nonce&&m&&m[1]!=='0000000000000000'&&this.count[0]===2185&&this.length[0]===SIZE,'Invalid plan challenge/incomplete backup');this.nonce=m[1];
   let plan;
   if(this.cleanChosen){
    try{plan=await cleanPlan607(this.data[0],this.identity,this.app,this.meta)}catch(cause){throw Object.assign(Error(cause.message),{code:'RADIO_SOURCE_REVIEW',report:{adapter:607,decision:'compatibility-review-required',reason:cause.message,forwardRecovery:true,public_qualified:false},cleanRecoveryPending:!!this.cleanPending})}this.report=plan.report;
    await this.onPolicy({clean:true});if(this.exportOriginal)await this.save('radio-backup.bin',this.data[0]);await this.save('compatibility.json',this.report);
   }else{
    await this.onPolicy({clean:false});
   // Await closed-file readback for every original/journal before returning PLAN/ARM.
   await this.save('radio-backup.bin',this.data[0]);this.report=await inspectImage(this.data[0],this.identity,this.app,this.meta);if(this.report.source_family==='feldd-captured565'&&this.report.decision!=='compatibility-review-required'&&!this.source565){this.report.decision='compatibility-review-required';this.report.controllerUpdateRequired=true;this.report.reason='This Feldd radio is recognized. Flash the Feldd-compatible controller test BIN before updating.';}this.report.controller_source_profile=this.source565?'feldd565-v1':null;await this.save('compatibility.json',this.report);
   if(this.mode==='migrate'&&!this.original&&this.report.decision==='compatibility-review-required')throw Object.assign(Error('This Bluetooth firmware needs review; no writes were sent.'),{code:'RADIO_SOURCE_REVIEW',report:this.report,radioSnapshotVerified:true});
    plan=this.original?await recoveryPlan(this.data[0],this.identity,this.original,this.app,this.meta):await makePlan(this.data[0],this.identity,this.mode,this.app,this.meta);

   }
   this.expected=plan.after;this.manifest=plan.manifest;
   await this.save('recovery.json',this.manifest);await this.save('write-intent.json',{session:this.nonce,mode:this.mode,original_recovery:this.original?.name||null});
   if(this.prepareRecovery)await this.prepareRecovery(this.data[0],this.manifest);
   const number=this.original?1:this.cleanChosen?3:this.mode==='migrate'?0:2;
   this.queue=[['PLAN335',`${this.identity} ${number.toString(16).padStart(2,'0')} ${this.manifest.sectors.length.toString(16).padStart(2,'0')}`],...this.manifest.sectors.map((a,i)=>['HASH335',`${a.toString(16).padStart(5,'0')} ${this.manifest.sector_targets[i]}`]),['ARM335','']];
   this.onProgress('plan',0,this.manifest);return this.next();
  }else if(line.startsWith('BTID335 ACK ')){
   m=/^BTID335 ACK ([0-9a-f]{8})$/.exec(line);check(m&&this.waitAck!==null&&parseInt(m[1],16)===this.waitAck,'Unexpected/stale/out-of-order ACK');this.waitAck=null;return this.next();
  }else if(line.startsWith('BTID335 SECTOR_READY ')){
   m=/^BTID335 SECTOR_READY index=(\d+) address=([0-9a-f]{5})$/.exec(line);const sectors=this.manifest?.sectors||[];
   check(m&&this.armed&&this.waitAck===null&&!this.queue.length&&this.ready===this.verified&&+m[1]===this.ready+1&&this.ready<sectors.length&&parseInt(m[2],16)===sectors[this.ready],'Unexpected sector request');
   const a=sectors[this.ready++],job=this.ready.toString(16).padStart(2,'0');
   for(let at=0;at<4096;at+=240)this.queue.push(['DATA335',`${job} ${at.toString(16).padStart(4,'0')} ${hex(this.expected.slice(a+at,a+Math.min(at+240,4096)))}`]);
   this.queue.push(['SEAL335',job]);return this.next();
  }else if(line.startsWith('BTID335 SECTOR_VERIFIED ')){
   m=/^BTID335 SECTOR_VERIFIED index=(\d+) address=([0-9a-f]{5})$/.exec(line);
   check(m&&this.started&&this.waitAck===null&&this.ready===this.verified+1&&+m[1]===this.ready&&parseInt(m[2],16)===this.manifest.sectors[this.verified],'Unexpected sector verification');this.verified++;this.onProgress('write',this.verified/this.manifest.sectors.length);
  }else if(line.startsWith('BTID335 PLAN_VERIFIED ')){
   check(this.armed&&this.waitAck===null&&!this.planVerified&&this.verified===this.manifest.sectors.length&&line===(this.verified?`BTID335 PLAN_VERIFIED sectors=${this.verified} begin_full_readback=1`:'BTID335 PLAN_VERIFIED sectors=0 no_write=1'),'Incomplete plan verification');this.planVerified=true;
  }else if(line==='BTID335 END')this.ended=true;
  else if(line.startsWith('BTID335 READY '))throw new Error('Wrong setup firmware banner');
  return null;
 }
 validate(){
  check(this.ended&&this.planVerified&&this.count[0]===2185&&this.count[1]===(this.verified?2185:0)&&equal(this.data[this.verified?1:0],this.expected),'Incomplete transaction or full-image mismatch');
  const one=(prefix,pattern)=>{const rows=this.lines.filter(s=>s.startsWith('BTID335 '+prefix));check(rows.length===1&&pattern.test(rows[0].slice(8)),'Invalid final report: '+prefix);};
  one('result=',/^result=1 tx_accepted=\d+ rx=\d+ dropped=0 errors=0 cts=[01]$/);
  one('RAM ',/^RAM written=39 verified=39 bytes=8966 phase=5 error=0 entry_silent=[01] launch=1$/);
  one('HANDOFF ',/^HANDOFF high_before_launch=1 high_window_ms=500 ready_restored=1 low_seen=1$/);
  one('LAUNCH ',/^LAUNCH sent=1 ack=1 elapsed_ms=\d+ reset_held_after=1$/);
  one('STRAP ',/^STRAP started=1 low_seen=1 restored=1 release_ms=\d+$/);
  one('FAST ',/^FAST status=1 bytes=524288 blocks=2185 duplicate_reads=2 error=0$/);
  one('WRITE ',new RegExp(`^WRITE started=${+!!this.manifest.sectors.length} verified=1 full_verified=1 mode=${this.original?1:this.cleanChosen?3:this.mode==='migrate'?0:2} sectors=${this.verified}$`));
  if(this.transport){check(this.transport.reset_ack,'Missing radio autobaud acknowledgement');one('UART ',/^UART .* baud=10000000 .*$/);}
  const ms=+this.lines.find(s=>s.startsWith('BTID335 STRAP ')).split('release_ms=')[1];check(ms>=50&&ms<1000,'Strap timing outside bounds');
 }
}
