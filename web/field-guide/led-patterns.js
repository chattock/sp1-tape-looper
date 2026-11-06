import {firmwarePattern,firmwareAnimation} from './firmware-core.js?v=610';
// Current v4 source kernels; browser fallbacks match the same timing constants. Times in seconds; LED tick = 8 ms.
// Beat-dependent examples use 120 BPM. Display intensity is normalized, not calibrated PWM.
export const patternNames = ['arm','format','muted','beat','nudged','song','countdown','length','brightness','one','two','three','sweep-up','sweep-down','fixed','variable','power-on','power-off','wake-hold','shutdown-hold','page-entry','page-exit','lane-count','lane-depth','heads','wraps','grid-playing','grid-wait','grid-finish','vu','no-action','charging','snap',...Array.from({length:8},(_,i)=>'page'+(i+1))];
export function pageLights(n,t,muted=false){
 const compiled=firmwarePattern('page'+n,t,{monitorMuted:muted});if(compiled)return compiled;
 const out={};let count=n,level=1;
 if(n<=4){const f=Math.floor((t+1e-9)/.008)%275;level=muted?(Math.floor((t+1e-9)/.248)%2?1:40/255):(150+Math.floor(105*Math.abs(f-137)/137))/255;}
 else{const f=Math.floor((t+1e-9)/.008)%295;count=f<70?4:n-4;level=f<62?1:f<66?.125:f<70?0:f<195?1:(255-Math.floor(255*(f-195)/100))/255;}
 for(let i=1;i<=count;i++)out['LED.Side'+i]=level;return out;
}
// Deterministic example mix: kick, backbeat, offbeat hats and a moving bass/pad bed.
// Fast attacks and overlapping release tails imitate musical energy, not a test oscillator.
export function musicVu(t,beat=.5){
 const time=Math.max(0,t),b=Math.max(.15,beat),bar=Math.floor(time/(4*b));
 const pulse=(age,attack,release)=>age<0?0:(1-Math.exp(-age/attack))*Math.exp(-age/release);
 let energy=2.1+.45*Math.sin(time*.73)+.3*Math.sin(time*1.37+.8);
 const kicks=[1,.72,.88,.65],snares=[.86,1,.78,.94];
 // Two bars of lookback retain tails through phrase boundaries.
 for(let measure=Math.max(0,bar-1);measure<=bar;measure++){
  const start=measure*4*b,phrase=measure%8,breakdown=phrase===7?.68:1;
  for(const beatIndex of [0,2,...(phrase%2?[2.75]:[])]){
   const age=time-start-beatIndex*b;
   energy+=breakdown*7.7*kicks[measure%4]*pulse(age,.006,.17);
  }
  for(const beatIndex of [1,3])energy+=breakdown*7.2*snares[measure%4]*pulse(time-start-beatIndex*b,.004,.135);
  for(let eighth=0;eighth<8;eighth++)energy+=(eighth%2?1.55:.75)*pulse(time-start-eighth*b/2,.002,.045);
  for(const beatIndex of [0,.75,1.5,2.5,3.25])energy+=1.3*pulse(time-start-beatIndex*b,.025,.23);
  if(phrase===0)energy+=2.1*pulse(time-start,.003,.65); // occasional phrase accent
 }
 return Math.max(0,Math.min(12,Math.round(energy)));
}
export function ledPattern(p,t,s={}){
 const compiled=firmwarePattern(p,t,s);if(compiled)return compiled;
 const o={};const row=(name,n=4,v=1)=>{for(let i=1;i<=n;i++)o['LED.'+name+i]=v};const one=(name,i,v=1)=>{if(i>=1&&i<=4)o['LED.'+name+i]=v};
 const lane=s.lane||1,beat=s.beatSeconds||.5;
 if(p==='success'){if(t>=0&&t<.48)row('Track');return o;}
 if(/^page[1-8]$/.test(p))return pageLights(+p.slice(4),t,s.monitorMuted);
 if(p==='rocker'){const slot=s.slot||1,frame=Math.floor((t+1e-9)/.008);if(slot<=4){const f=frame%275,level=s.monitorMuted?(Math.floor(frame/31)&1?255:40):105+Math.floor(150*Math.abs(f-137)/137);one('Track',slot,level/255);}else if(Math.floor(t*1000/128)&1)one('Track',(slot-1)%4+1);}
 if(p==='fixed'){if(t<.54&&t%.27<.15)row('Side');}
 if(p==='variable'){const i=t<.44?Math.floor(t/.11)+1:t<.71?3-Math.floor((t-.44)/.09):0;one('Side',i);}
 if(['one','two','three'].includes(p)){const n={one:1,two:2,three:3}[p];if(t<n*.128&&t%.128<.064)row('Side');}
 if(p==='sweep-up'||p==='sweep-down'){const j=Math.floor(t/.096);if(j<4)one('Side',p==='sweep-down'?4-j:j+1);}
 if(p==='power-on'&&t<.52)row('Side',Math.min(4,Math.floor(t/.09)+1));
 if(p==='wake-hold')row('Side',Math.min(4,Math.floor(t*4/1.5)+1));
 if(p==='shutdown-hold'){
  if(t<=.4)return ledPattern('song',t,s);
  if(t<2.5)row('Side',Math.min(4,Math.floor(t*4/2.5)+1));
  else return ledPattern('power-off',t-2.5,s);
 }
 // power_off() only clears existing pins. The preceding FN countdown clears the track row.
 if(p==='power-off'&&t<.32){const remaining=3-Math.floor((t+1e-9)/.08);for(const [name,value] of Object.entries(s.initialLights||{'LED.Side1':1,'LED.Side2':1,'LED.Side3':1,'LED.Side4':1}))if(+name.slice(-1)<=remaining)o[name]=value;}
 if(p==='song'){one('Side',s.bank||1);one('Side',s.song||2,Math.floor(t/.25)%2===0?1:0);}
 if(p==='page-entry'||p==='page-exit'){const packed=firmwareAnimation(p==='page-exit'?2:s.walk?3:1,s.page||1,t*1000);for(let i=0;i<4;i++)if(packed&(1<<i))one('Track',i+1);}
 if(p==='muted')one('Track',lane,.125);
 if(p==='arm')one('Track',lane,t%beat<beat/8?1:0);
 if(p==='grid-wait'||p==='grid-finish')one('Track',lane,t%(beat/2)<beat/8?1:0);
 if(p==='grid-playing')row('Track',4,t%beat<beat/8?1:0);
 if(p==='beat'||p==='nudged'){if(p==='nudged')one('Track',lane,(64+40*191/255)/255);if(t%beat<beat/8)one('Track',Math.floor(t/beat)%4+1);}
 if(p==='heads'){for(let i=1;i<=4;i++)one('Track',i,((t/2+(i-1)/4)%1)<.125?(i===1?1:.125):0);}
 if(p==='wraps'){[1,1.5,2,2.5].forEach((period,i)=>one('Track',i,t%period<period/8?1:0));}
 if(p==='format'){for(let i=1;i<=4;i++)one('Track',i,(s.mono||[]).includes(i)?1:(Math.floor(t/.128)%2));}
 if(p==='countdown'){const left=Math.max(0,4-Math.floor(t/2));row('Track',left,left===1?(Math.floor(t/.128)%2):1);}
 if(p==='lane-depth'){const amt=s.amount??.6;one('Track',lane,amt>0?(64+Math.min(1,amt)*191)/255:0);}
 if(p==='length'||p==='lane-count'){const n=s.count??2,l=p==='length'?2:lane;const end=n===0?.344:n*.288;const on=n===0?t<.2:t<end&&t%.288<.144;one('Track',l,t<end?(on?1:0):(s.amount??0));}
 if(p==='brightness'){
  Object.assign(o,ledPattern('song',t,s));
  for(const i of s.loaded||[1])one('Track',i);
  for(const key of Object.keys(o))o[key]*=t<5?.45:1;
 }
 if(p==='vu'){const v=s.vu??musicVu(t,beat);for(let i=0;i<4;i++){const a=v-i*3;one('Side',4-i,a<=0?0:a===1?.125:1);}}
 if(p==='charging'){const n=s.level||3;row('Side',n-1);one('Side',n,s.full?1:(Math.floor(t/.48)%2===0?1:0));}
 if(p==='snap'){let catchFrame=48;for(let f=12;f<48;f++){const j=Math.floor(f/2)%6,pos=j<=3?j:6-j;if(pos===Math.floor(f*.008/beat)%4){catchFrame=f;break;}}const f=Math.floor(t/.008);if(f<=catchFrame){const j=Math.floor(f/2)%6;one('Track',(j<=3?j:6-j)+1);}else if(t%beat<beat/8)one('Track',Math.floor(t/beat)%4+1);}
 if(p==='no-action'){const f=Math.floor(t/.008);if(f<20&&((20-f)>>2)&1)row('Track');}
 return o;
}


// Row ownership mirrors led_service: page over song/VU; confirmations over ambient rows.
export function sceneLights(s,t,held=[]){
 let o={};const c=s.context,p=s.pattern||'';
 const clear=row=>{for(let i=1;i<=4;i++)delete o['LED.'+row+i]};
 if(c){
  Object.assign(o,ledPattern(c.playing&&(c.loaded.length||c.input)&&!held.includes('FN')?'vu':'song',t,s));
  if(c.heads)Object.assign(o,ledPattern('heads',t,s));
  else for(const i of c.loaded){
   if((c.muted||[]).includes(i))o['LED.Track'+i]=.125;
   else if(!c.playing)o['LED.Track'+i]=1;
   else o['LED.Track'+i]=t%(c.grid?.5:2)<(c.grid?.5:2)/8?1:0;
  }
  if(c.grid&&!c.loaded.length)Object.assign(o,ledPattern('beat',t,s));
 }
 const page=s.page||(/^page[1-8]$/.test(p)?+p.slice(4):0);
 if(page&&p!=='page-exit'){
  clear('Side');clear('Track');Object.assign(o,pageLights(page,t,s.monitorMuted));
  if(page===7&&c?.grid)Object.assign(o,ledPattern('beat',t,s));
  if(page===8)Object.assign(o,ledPattern('format',t,{...s,mono:s.mono||[]}));
 }
 if(c?.recording&&!page)for(const i of c.recording)o['LED.Track'+i]=1;
 for(const k of s.lights||[])o['LED.Track'+k.slice(1)]=1;
 const side=['fixed','variable','one','two','three','sweep-up','sweep-down','song','vu','charging'];
 const tracks=['page-entry','page-exit','arm','muted','beat','nudged','heads','wraps','grid-playing','grid-wait','grid-finish','format','countdown','lane-depth','lane-count','length','snap','no-action'];
 const power=['power-on','power-off','wake-hold','shutdown-hold','brightness'];
 const transient=['fixed','variable','one','two','three','sweep-up','sweep-down','power-on','power-off','page-entry','page-exit','lane-count','length','no-action','snap'].includes(p);
 if(power.includes(p)){clear('Side');clear('Track');}
 const confirmationLength={fixed:.54,variable:.71,one:.128,two:.256,three:.384,'sweep-up':.384,'sweep-down':.384}[p];
 const confirmationActive=confirmationLength===undefined||t>=.9&&t<.9+confirmationLength;
 if(side.includes(p)&&confirmationActive)clear('Side');
 if(tracks.includes(p))clear('Track');
 if((!transient||t>=.9)&&confirmationActive)Object.assign(o,ledPattern(p,(transient?t-.9:t)+(s.patternTimeOffset||0),s));
 return o;
}

// Confirmations remain at runtime speed; no teaching-only slow replay.
export function confirmationCloseup(){return null;}
