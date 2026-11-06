// Read-only endpoint for the project-authored SL/SF + snapshot-v4 protocols.
export const SERVICE='53500003-6014-ce88-a946-12f0f8649172';
export const STREAM='53500004-6014-ce88-a946-12f0f8649172';
export const CAPS='53500005-6014-ce88-a946-12f0f8649172';
export function crc(p,n=p.length){let c=65535;for(let i=0;i<n;i++){c^=p[i]<<8;for(let k=0;k<8;k++)c=((c<<1)^((c&32768)?0x1021:0))&65535;}return c;}
const u32=(p,i)=>new DataView(p.buffer,p.byteOffset,p.byteLength).getUint32(i,true);
export function frame(type,local,remote,seq=0){let p=new Uint8Array(32),v=new DataView(p.buffer);p.set([83,76,1,type]);v.setUint32(4,local,true);v.setUint32(8,remote,true);v.setUint32(12,seq,true);return seal(p);}
export function seal(p){let c=crc(p,30);p[30]=c&255;p[31]=c>>8;return p;}
export function fragments(p,id){return [0,1].map(i=>{let f=new Uint8Array(20);f.set([83,70,id&255,i]);f.set(p.slice(i*16,i*16+16),4);return f;});}
export function decode(p){
 const word=i=>p[i]|p[i+1]<<8;
 if(p.length===480){
  if(![14,15].includes(p[59]))throw Error('Invalid take lane schema');
  if(p[59]===15&&p[170]>1)throw Error('Invalid storage backend');
  const core=p.slice(0,448);core[59]=13;const base=decode(core);
  const lanes=Array.from({length:4},(_,i)=>({selected:u32(p,448+8*i),total:u32(p,452+8*i)}));
  const unavailable=lanes.every(x=>x.selected===0xffffffff&&x.total===0xffffffff);
  if(!unavailable&&lanes.some(x=>x.total>=131071||x.selected>x.total))throw Error('Invalid take lane bounds');
  base.details.version=p[59];base.details.takeLanes=unavailable?null:lanes;base.details.basic=p[59]>=15&&!!p[170];if(base.details.basic){if(!unavailable)throw Error('Basic mode cannot publish take lanes');base.details.stash.takes=false;}return base;
 }
 if(p.length===448){if(p[56]!==86||p[57]!==65||p[58]!==76||![1,2,3,4,5,6,7,8,9,10,11,12,13].includes(p[59]))throw Error('Invalid extended schema');const raw=p.slice(0,56);if(p[59]>=13){if(raw[47]>1)throw Error('Invalid storage fault flag');raw[47]=0;}const base=decode(raw);return {...base,storageFault:p[59]>=13&&!!p[47],details:decodeDetails(p)};}
 if(p.length!==56||p[0]!==17||p[1]!==1||(p[2]&~3)||p[5]>15||p[10]>8||p[11]||p[44]>15||p[45]>15||p[46]>15||p[47]||p.slice(48).every(x=>x===0)||p.slice(48).every(x=>x===255))throw Error('Invalid snapshot');
 for(let i=0;i<4;i++)if(word(12+i*2)>256||word(20+i*2)>4095)throw Error('Invalid fader bounds');
 return {playing:!!(p[2]&1),heads:!!(p[2]&2),loaded:p[3]&15,muted:p[3]>>4,song:p[4]+1,volume:p[5],speed:u32(p,6)/65536,page:p[10],levels:Array.from({length:4},(_,i)=>word(12+2*i)),faders:Array.from({length:4},(_,i)=>word(20+2*i)),fx:Array.from(p.slice(28,44)),pagePickup:p[44],faderValid:p[45],trackPickup:p[46],identity:Array.from(p.slice(48),x=>x.toString(16).padStart(2,'0')).join('')};
}
export class Observer{
 constructor(nonce,onSnapshot=()=>{}){if(!nonce)throw Error('Nonzero nonce required');this.local=nonce>>>0;this.remote=0;this.seq=0;this.part=0;this.revision=0;this.count=0;this.onSnapshot=onSnapshot;this.partial=null;this.snapshot=new Uint8Array(56);this.ready=false;this.lastFrame=null;this.mask=0n;this.payloads=[];this.parts=7;this.encoding=null;}
 receiveFragment(f){if(f.length!==20||f[0]!==83||f[1]!==70||f[3]>1)throw Error('Invalid fragment');if(f[3]===0){this.partial=f.slice();return [];}
 if(!this.partial||this.partial[2]!==f[2])throw Error('Missing or mismatched fragment');let p=new Uint8Array(32);p.set(this.partial.slice(4));p.set(f.slice(4),16);this.partial=null;return this.receive(p);}
 receive(p){
 if(p.length!==32||p[0]!==83||p[1]!==76||p[2]!==1||crc(p,30)!==(p[30]|p[31]<<8))throw Error('Invalid frame');
 const type=p[3],sender=u32(p,4),target=u32(p,8);
 if(!sender||(this.remote&&sender!==this.remote))throw Error('Stale session');
 if(type===3||type===4){
  if(p.slice(12,30).some(x=>x)||(type===3?target!==0:target!==this.local))throw Error('Invalid handshake');
  this.remote=sender;if(type===4)this.ready=true;
  return [frame(4,this.local,this.remote)];
 }
 if(!this.ready||type!==1||target!==this.local||p[16]!==12||p[17])throw Error('Unexpected message');
 const seq=u32(p,12),d=p.slice(18,30);
 if(seq===this.seq){if(!this.lastFrame||p.some((x,i)=>x!==this.lastFrame[i]))throw Error('Changed duplicate');return [frame(2,this.local,this.remote,seq)];}
 const extended=d[0]===20&&d[1]===6,delta=extended||(d[0]===19&&d[1]===5),index=d[2]&127,parts=extended?56:7,final=delta?!!(d[2]&128):index===6;
 if(this.encoding===null){this.encoding=extended;this.parts=parts;this.snapshot=new Uint8Array(parts*8);}else if(this.encoding!==extended)throw Error('Snapshot encoding changed within session');
 if(seq!==this.seq+1||(!delta&&(d[0]!==18||d[1]!==4))||index>=parts||(!delta&&d[2]>=7)||(delta?index<this.part:index!==this.part)||d[3]!==this.revision)throw Error('Out of order snapshot');
 this.snapshot.set(d.slice(4),index*8);this.part=index+1;this.mask|=1n<<BigInt(index);this.seq=seq;this.lastFrame=p.slice();this.payloads.push(Array.from(d,x=>x.toString(16).padStart(2,'0')).join(''));
 if(final){if(!this.count&&this.mask!==((1n<<BigInt(parts))-1n))throw Error('Incomplete initial snapshot');const value=decode(this.snapshot);this.part=0;this.mask=0n;this.revision=(this.revision+1)&255;this.count++;const payloads=this.payloads;this.payloads=[];this.onSnapshot(value,this.count,payloads);}
 return [frame(2,this.local,this.remote,seq)];
 }
}

function decodeDetails(p){
 const v=new DataView(p.buffer,p.byteOffset,p.byteLength),u=i=>v.getUint32(i,true),w=i=>v.getUint16(i,true),a=(i,n)=>Array.from(p.slice(i,i+n));
 if(p[60]>(p[59]>=10?8:p[59]>=7?7:p[59]>=3?6:4)||p[63]>4||p[68]>3||p[146]&&!([3,4,5,7].includes(p[146]))||p[364]>(p[59]>=6?3:p[59]>=5?2:0)||p[365]>1||(![0,1,3].includes(p[382])||p[381]>1||(!p[382]&&p.slice(440).some(x=>x))))throw Error('Extended bounds');
 if(p[59]>=2&&(p[332]>1||p[333]>1||(p[59]===2&&p.slice(334,340).some(Boolean))))throw Error('Panel telemetry bounds');
 if(p[59]>=3&&(p[334]>1||p[335]>2||p[336]>(p[59]>=11?3:1)||p[337]>1||p[338]>3||p[339]>8))throw Error('FX and magnet bounds');
 if(p[59]>=7&&((p[59]<12&&p[375]>64)||(p[59]>=9?(p[376]&0xcf)>6:p[376]>6)||(p[376]&3)===3))throw Error('Stash bounds');
 if((p[382]&2)&&p[125]>1)throw Error('Invalid tone selector');
 if(p[59]>=4&&p[383]>4)throw Error('Record ready bounds');
 const tracks=Array.from({length:4},(_,i)=>{const o=160+i*40;if(p[o]>4)throw Error('Track state');return {state:p[o],format:p[o+1],nextFormat:p[o+2],reverse:!!p[o+3],headPosition:p[o+4],printGainShift:p[o+5],effectiveTrimHead:w(o+6),effectiveTrimTail:w(o+8),lengthBlocks:u(o+12),lengthSamples:u(o+16),startSamples:u(o+20),repeatBlocks:u(o+24),repeatOffset:u(o+28),trimHead:u(o+32),trimTail:u(o+36),samplesPerBlock:w(432+i*2)};});
 const layout=(p[59]>=11&&p[336]>=2)?Array.from({length:16},(_,i)=>i):(p[382]&1)===1?Array.from({length:16},(_,i)=>(p[440+(i>>1)]>>((i&1)*4))&15):Array.from({length:16},(_,i)=>i);if(new Set(layout).size!==16)throw Error('Invalid FX layout');

 return {toneMode:(p[382]&2)?p[125]:null,comb:p[59]>=11&&p[336]>=2?{levels:a(384,4),tuning:a(388,4),decay:a(392,4),damping:a(396,4),mix:p[400],snapped:p[336]===2,layer:p[154]}:null,stash:p[59]>=7?{takes:p[59]>=12,slot:p[375],...(p[59]>=9?{laneTrack:1+((p[376]>>4)&3),lanes:true}:{}),live:p[59]>=8,busy:!!(p[376]&4),result:p[376]&3}:null,inputSource:p[59]>=5?p[364]:null,recordReady:p[59]>=4?p[383]-1:-1,chorusType:p[59]>=3?p[334]:null,crushType:p[59]>=3?p[335]:null,reverbType:p[59]>=3?p[336]:null,reverbFrozen:p[59]>=3?!!p[337]:null,magnet:p[59]>=3?{active:!!(p[338]&1),undo:p[339],held:!!(p[338]&2)}:null,layout,instantRecord:(p[382]&1)===1?!!p[381]:null,version:p[59],panel:p[59]>=2?{side:a(320,4),tracks:a(324,4),trackRaw:w(328),volumeRaw:w(330),valid:!!p[332],fn:!!p[333]}:null,rocker:p[60],rockerPage:!!p[61],fixed:!!p[62],recordTrack:p[63]-1,freeWindow:!!p[64],windowStart:p[65],windowEnd:p[66],windowReverse:!!p[67],headSource:p[68],repeat:!!p[69],repeatLive:!!p[70],bounce:!!p[71],chopDivision:u(72),chopOffset:u(76),repeatDivision:u(80),loopSamples:u(84),targetSpeed:u(88)/65536,effectiveSpeed:u(92)/65536,secondary:a(96,16),shape:a(112,16),routes:a(128,4),eq:a(132,4),placement:a(136,4),nudge:a(140,4),gridOffset:v.getInt8(144),takePreset:p[145],beatsPerBar:p[146],gridActive:!!p[147],distortionType:p[148],gatePattern:p[149],echoDivision:p[150],noiseType:p[151],stopFx:p[152],secondaryLane:p[153],shapeHeld:!!p[154],gridBeats:p[155],beatFrames:u(156),tracks,rockerParameters:p[59]===1?a(320,20):null,songTrimHead:u(340),songTrimTail:u(344),repeatStart:u(348),repeatLength:u(352),saving:!!p[356],usbAudio:!!p[357],transfer:!!p[358],wearActive:!!p[359],wear:a(360,4),midi:null,songValid:!!p[365],lfoDivision:a(366,3),isolate:!!p[369],isolateMask:p[370],rockerAction:p[371],rockerOwnsSpeed:!!p[372],rewindMask:p[373],fnHeld:!!p[374],wobbleType:p[377],monitorMuted:!!p[378],headMask:p[379],rockerPickup:p[380],tapFrames:p[59]>=11&&p[336]>=2?Array(7).fill(0):Array.from({length:7},(_,i)=>u(384+4*i)),position:u(412),batteryWear:u(416)/65536,sandWear:u(420)/65536,baseSpeed:u(424)/65536,bpm:u(428)/256};
}
