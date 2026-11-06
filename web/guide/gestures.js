import {GESTURES_ENABLED} from './features.js';
// Ordered actions: repeated clicks are retained for tap-tempo and double/triple taps.
export class GesturePlan {
 constructor(){this.actions=[];}
 add(key,kind){if(!['click','hold'].includes(kind))throw Error('Unknown gesture');if(this.actions.length>=12)return false;this.actions.push({key,kind});return true;}
 clear(){this.actions=[];}
}
export class GestureComposer {
 constructor(device){document.querySelectorAll('[data-gesture-help]').forEach(el=>el.hidden=!GESTURES_ENABLED);this.d=device;this.plan=new GesturePlan();this.latched=new Set();this.running=false;this.token=0;
 this.root=document.createElement('div');this.root.className='gesture-composer';this.root.innerHTML='<div class="latch-arrow" hidden><svg viewBox="0 0 28 16" aria-hidden="true"><path d="M2 8h23M19 2l6 6-6 6"/></svg><span>Freeze</span></div><div class="latch-choices" hidden><button data-kind="click">Click</button><button data-kind="hold">Hold</button></div><div class="combo-tray glass" hidden><div class="combo-chips"></div><button class="combo-submit" aria-label="Apply combination"><svg viewBox="0 0 16 16" aria-hidden="true"><path d="m3 8 3 3 7-7"/></svg><span>Done</span></button><button class="combo-cancel" aria-label="Cancel combination">×</button></div><div class="gesture-feedback" role="status" hidden></div>';this.root.hidden=!GESTURES_ENABLED;document.body.append(this.root);
 this.arrow=this.root.querySelector('.latch-arrow');this.choices=this.root.querySelector('.latch-choices');this.tray=this.root.querySelector('.combo-tray');this.feedback=this.root.querySelector('.gesture-feedback');
 this.root.querySelector('.combo-submit').onclick=()=>this.submit();this.root.querySelector('.combo-cancel').onclick=()=>this.cancel();
 for(const b of this.choices.querySelectorAll('button'))b.onclick=()=>{if(this.pending){this.plan.add(this.pending,b.dataset.kind);this.pending=null;this.choices.hidden=true;this.render();}};
 document.addEventListener('keydown',e=>{if(e.key==='Escape'&&(this.plan.actions.length||this.latched.size)){e.preventDefault();e.stopImmediatePropagation();this.cancel();}},true);
 }
 begin(state,e){if(!GESTURES_ENABLED)return false;if(this.running)return true;if(/^F[1-4]$/.test(state.key))return false;state.snapshot=structuredClone(this.d.sim);state.checkpoint=this.d.audio.checkpoint();state.latchTimer=setTimeout(()=>{if(this.d.pointers.has(e.pointerId)){this.arrowKey=state.key;this.arrow.hidden=false;this.alignArrow();}},270);if(this.plan.actions.length||this.pending){state.composing=true;this.pending=state.simKey;this.showChoices(e.clientX,e.clientY);return true;}return false;}
 move(state,e){if(!state.snapshot)return false;if(!state.composing&&performance.now()-state.start>=270&&Math.hypot(e.clientX-state.x,e.clientY-state.y)>16){Object.assign(this.d.sim,state.snapshot);this.d.audio.rollback(state.checkpoint,this.d.sim);this.d.simLast=null;state.composing=true;this.pending=state.simKey;this.showChoices(e.clientX,e.clientY);this.d.onInteract({type:'compose',control:state.key});}if(!state.composing)return false;this.arrow.hidden=true;const target=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-kind]');this.choice=target?.dataset.kind||null;for(const b of this.choices.querySelectorAll('button'))b.classList.toggle('selected',b.dataset.kind===this.choice);return true;}
 end(state,e){this.d.audio.discard(state.checkpoint);clearTimeout(state.latchTimer);this.arrow.hidden=true;if(e.type==='pointercancel'){this.cancel();return true;}if(!state.composing)return this.running;const target=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-kind]');if(target&&this.choices.contains(target))this.plan.add(state.simKey,target.dataset.kind);this.pending=null;this.choices.hidden=true;this.choice=null;this.render();return true;}
 alignArrow(){
  if(this.arrow.hidden)return;
  const cue=this.d.cues.get(this.arrowKey);if(!cue||cue.hidden)return;
  const r=cue.getBoundingClientRect(),host=this.d.host.getBoundingClientRect();
  const width=this.arrow.offsetWidth,height=this.arrow.offsetHeight,gap=9;
  let right=r.x+r.width/2>=host.x+host.width/2;
  if(right&&r.right+gap+width>innerWidth-12)right=false;
  if(!right&&r.left-gap-width<12)right=true;
  this.arrow.dataset.direction=right?'right':'left';
  this.arrow.style.left=(right?r.right+gap:r.left-gap-width)+'px';
  this.arrow.style.top=(r.top+r.height/2-height/2)+'px';
 }
 showChoices(x,y){this.choice=null;this.choices.hidden=false;this.place(this.choices,x-75,y-22);}
 place(el,x,y){el.style.left=Math.max(12,Math.min(innerWidth-(el.offsetWidth||180)-12,x))+'px';el.style.top=Math.max(75,Math.min(innerHeight-130,y))+'px';}
 get drafting(){return this.plan.actions.length>0||!!this.pending;}
 render(){const chips=this.root.querySelector('.combo-chips');chips.replaceChildren();for(const [i,a]of this.plan.actions.entries()){const b=document.createElement('button');b.innerHTML='<strong>'+a.key+'</strong><span>'+a.kind+'</span><i aria-hidden="true">×</i>';b.setAttribute('aria-label','Remove '+a.key+' '+a.kind);b.onclick=()=>{this.plan.actions.splice(i,1);this.render();};chips.append(b);}for(const key of this.latched){const b=document.createElement('button');b.className='latched';b.innerHTML='<strong>'+key+'</strong><span>held</span><i aria-hidden="true">×</i>';b.setAttribute('aria-label','Release '+key);b.onclick=()=>this.release(key);chips.append(b);}this.tray.hidden=!(this.plan.actions.length||this.latched.size);this.root.querySelector('.combo-submit').hidden=!this.plan.actions.length;this.root.querySelector('.combo-submit').disabled=this.running;}
 async submit(){if(this.running||!this.plan.actions.length)return;this.running=true;const token=++this.token,actions=this.plan.actions.slice();this.plan.clear();this.pending=null;this.choices.hidden=true;this.render();const before=this.d.sim.lastMessage;
 for(const a of actions){if(token!==this.token)break;if(this.latched.has(a.key)){if(a.kind==='hold')continue;this.release(a.key);await new Promise(r=>setTimeout(r,80));if(token!==this.token)break;}this.d.manualPress(a.key,true);if(a.kind==='hold'){this.latched.add(a.key);await new Promise(r=>setTimeout(r,65));}else{await new Promise(r=>setTimeout(r,95));if(token!==this.token)break;this.d.manualPress(a.key,false);await new Promise(r=>setTimeout(r,110));}}
 if(token===this.token){this.running=false;this.render();this.notice(this.d.sim.lastMessage!==before?this.d.sim.lastMessage:this.latched.size?'Buttons held':'No action in this context');}}
 release(key){this.latched.delete(key);this.d.manualPress(key,false);this.render();}
 cancel(){this.token++;this.running=false;this.plan.clear();this.pending=null;this.choices.hidden=true;this.arrow.hidden=true;for(const key of [...this.latched])this.release(key);this.d.sim.cancel();this.d.audio.command('cancel');this.d.held.clear();this.render();}
 notice(text){this.feedback.textContent=text;this.feedback.hidden=false;clearTimeout(this.noticeTimer);this.noticeTimer=setTimeout(()=>this.feedback.hidden=true,2200);}
}
