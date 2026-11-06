// Current v4 source kernels; build/source hashes live in tools/release610.
let core=null;
export function useFirmwareCore(exports){core=exports;}
export async function loadFirmwareCore(){try{const r=await fetch(new URL('./assets/firmware-core483.wasm?v=610',import.meta.url));if(!r.ok)throw Error('LED core missing');const {instance}=await WebAssembly.instantiate(await r.arrayBuffer());useFirmwareCore(instance.exports);return true;}catch{return false;}}
const ease=[0,2,8,18,30,44,61,78,96,114,131,148,162,174,184,190,192];
export function firmwareBreath(ms){if(core)return core.firmware_breath(Math.max(0,Math.floor(ms)));let p=Math.floor(ms)%4800;if(p<3200)return 0;p-=3200;if(p>800)p=1600-p;const i=Math.floor(p/50),f=p%50;return i>=16?192:ease[i]+Math.floor((ease[i+1]-ease[i])*f/50);}
export function firmwarePreview(ms,counts){if(core)return core.firmware_preview(Math.floor(ms),...counts);if(counts.some(n=>n>=131071))return-1;const max=Math.max(...counts),phase=Math.floor(ms)%(max*288+900);if(phase>=max*288||phase%288>=144)return 0;const n=Math.floor(phase/288);return counts.reduce((mask,c,i)=>mask|(n<c?1<<i:0),0);}
export function firmwareBluetooth(state,age,now=age){if(core)return core.firmware_bt(6,state,Math.floor(age),Math.floor(now));if(state===4)return age<1000?0x100|((Math.floor(age/125)&1)?0xf0:0):0;if(state===3)return age<1000?0x100|1<<(4+Math.floor(age/250)):0;if(state===5)return age<1000?0x100|1<<(7-Math.floor(age/250)):0;if(state!==1&&state!==2)return 0;let x=Math.floor(age)%2400;if(x>1200)x=2400-x;const p=Math.floor(x*2/3),i=Math.floor(p/50),f=p%50,y=i>=16?192:ease[i]+Math.floor((ease[i+1]-ease[i])*f/50);const level=Math.floor((y*255+96)/192);return 0x100|0xf0|0x200|(level<<16);}
export function firmwareAnimation(kind,page,elapsed){if(core)return core.firmware_animation(kind,page,Math.max(0,Math.floor(elapsed)));const walk=kind===3;let left=walk?2*Math.min(page,4)+(page>4?2+2*(page-4):0):24,last=0,mask=0;const target=page<=4?page-1:0,position=s=>s<=3?s:s>=6?0:6-s;for(let ms=0;ms<=elapsed;ms+=8){if(!left)return 0;let pos=-1;if(kind===2){const f=24-left,land=6-target;pos=f<8?target:Math.floor((f-8)/2)>land?-1:position(land-Math.floor((f-8)/2));}else if(walk){const first=Math.min(4,page),second=Math.max(0,page-4),total=2*first+(second?2+2*second:0),f=total-left;pos=f<2*first?Math.floor(f/2):second&&f>=2*first+2?Math.floor((f-2*first-2)/2):-1;}else if(left<=8)pos=target;else{const s=Math.floor((24-left)/2);pos=position(s);if(s>=3&&pos===target)left=9;}mask=pos>=0?1<<pos:0;if(ms-last>=25){last=ms;left--;}if(kind===2&&pos<0)left=0;}return 0x100|mask;}
// Convert actual PWM duty to renderer-linear output. The exposure factor is a
// screen presentation setting, not a claim of calibrated physical luminance.
export function firmwareLEDDisplay(v,bright=false,row='Track'){if(!(v>0))return 0;const ghost=v===.125,b=ghost?255:Math.max(1,Math.min(255,Math.round(v*255)));let duty;if(core)duty=row==='Track'?core.firmware_track_duty(b,+ghost,+bright)/16000:core.firmware_status_duty(b,+ghost,+bright)/1000;else{const win=row==='Track'?(bright?15984:832):66,cc=Math.max(1,Math.floor(win*b*b/65025));duty=(ghost?(bright?Math.floor((row==='Track'?cc:1000)/5):cc>>3):bright&&(row==='Side'||b===255)?(row==='Track'?16000:1000):cc)/(row==='Track'?16000:1000);}return Math.min(1,duty*10);}
export function firmwarePattern(p,t,s={}){if(!core)return null;const frame=Math.floor((Math.max(0,t)+1e-9)/.008);let kind,arg,extra=0,row='Track',lane=s.lane||1;
 if(/^page[1-8]$/.test(p)){kind=0;arg=+p.slice(4);extra=+!!s.monitorMuted;row='Side';}
 else if(p==='page-entry'||p==='page-exit'){const packed=firmwareAnimation(p==='page-exit'?2:s.walk?3:1,s.page||1,t*1000),out={};for(let i=0;i<4;i++)if(packed&(1<<i))out['LED.Track'+(i+1)]=1;return out;}
 else if(p==='lane-depth'){kind=7;arg=Math.round((s.amount??.6)*255);}
 else if(p==='lane-count'||p==='length'){kind=4;arg=s.count??2;extra=Math.round((s.amount??0)*255);lane=p==='length'?2:lane;}
 else if(p==='song'){kind=5;arg=((s.bank||1)-1)*4+(s.song||2)-1;row='Side';}
 else if(p==='vu'&&s.vu!==undefined){kind=6;arg=s.vu;row='Side';}
 else if(p==='rocker'){kind=9;arg=s.slot||1;extra=+!!s.monitorMuted;}
 else return null;
 const result={};for(let i=0;i<4;i++){const v=core.firmware_led(kind,arg,frame,extra,i);if(v)result['LED.'+row+((kind===4||kind===7)?lane:i+1)]=v;}return result;
}
export function firmwareSemitone(speed,dir){const sp=Math.round(speed*65536);if(core)return core.firmware_semitone(sp,dir)/65536;const table=[16384,17358,18390,19484,20643,21870,23170,24548,26008,27554,29193,30929,32768,34716,36781,38968,41285,43740,46341,49097,52016,55109,58386,61858,65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072];return(dir>0?table.find(v=>v>sp+Math.floor(sp/250))??table.at(-1):table.findLast(v=>v<sp-Math.floor(sp/250))??table[0])/65536;}

// Modest face-only screen lift; exact off stays off and the glow stays disabled.
export function ledFaceBrightness(value){return Math.max(0,Math.min(1,value*1.18));}
