const esc=s=>s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export function initGuideRail({root,content,reduced,onSelect,onActive}){
 const entries=[];for(const el of content.querySelectorAll('.guide-category,.tile-section,article[id^="card-"],.rocker-mode-panel')){
  if(el.closest('#whats-new'))continue;
  const title=el.querySelector(':scope > h1,:scope > .category-heading h2,:scope > h2,:scope > .tile-heading h3,:scope > .rocker-heading h2,:scope > .rocker-mode-heading h3,:scope > .rocker-mode-top h3,:scope > h3');
  if(!title)continue;if(!el.id)el.id='guide-group-'+entries.length;
  entries.push({el,label:(el.matches('.guide-category')?String(el.dataset.section).padStart(2,'0')+' ':'')+[...title.childNodes].filter(n=>!n.classList?.contains('section-permalink')).map(n=>n.textContent).join('').trim(),major:el.matches('.guide-category'),section:el.matches('.tile-section'),rocker:el.classList.contains('rocker-mode-panel')});
 }
 root.classList.remove('category-nav');root.classList.add('guide-rail');root.setAttribute('aria-label','Jump to guide section');
 root.innerHTML=entries.map((e,i)=>`<button type="button" class="rail-tick${e.major?' major':e.section?' section':''}" data-rail="${i}" aria-label="${esc(e.label)}" aria-controls="${e.el.id}" tabindex="${i===0?0:-1}"><span class="rail-mark" aria-hidden="true"></span><span class="rail-label" aria-hidden="true">${esc(e.label)}</span></button>`).join('');
 const buttons=[...root.children];let active=0,focus=-1,last=-1;
 const mobile=document.createElement('select');mobile.className='guide-jump';mobile.setAttribute('aria-label','Jump to guide section');mobile.innerHTML='<option value="">Jump to…</option>'+entries.map((e,i)=>`<option value="${i}">${e.major?'':'　'}${esc(e.label)}</option>`).join('');root.after(mobile);
 const select=i=>{const e=entries[i];if(!e)return;const search=document.getElementById('guideSearch');if(search?.value){search.value='';search.dispatchEvent(new Event('input'));}onSelect?.(e.el);if(e.rocker){const tab=document.getElementById(e.el.getAttribute('aria-labelledby'));tab?.click();}e.el.scrollIntoView({behavior:reduced?'instant':'smooth',block:'start'});active=i;updateActive();};
 function updateActive(){buttons.forEach((b,i)=>{b.classList.toggle('current',i===active);if(i===active)b.setAttribute('aria-current','location');else b.removeAttribute('aria-current');});onActive?.(entries[active]?.el);}
 function hill(index){if(last===index)return;last=index;root.classList.toggle('expanded',index>=0);buttons.forEach((b,i)=>{const d=Math.abs(i-index),w=index<0?0:Math.max(0,1-d/3);b.style.setProperty('--near',w.toFixed(3));b.style.setProperty('--label-opacity',i===index?'1':'0');b.classList.toggle('revealed',i===index);});}
 // Reveal only inside the tick rail; never over adjacent page controls.
 function proximity(value){root.style.setProperty('--rail-reach',String(value));root.classList.toggle('nearby',value>0);}
 root.addEventListener('pointerenter',e=>{if(e.pointerType!=='touch')proximity(1);});
 root.addEventListener('pointerleave',()=>proximity(0));
 root.addEventListener('pointermove',e=>{if(e.pointerType==='touch')return;let best=0,d=Infinity;buttons.forEach((b,i)=>{const r=b.getBoundingClientRect(),v=Math.abs(e.clientY-(r.top+r.height/2));if(v<d){d=v;best=i;}});hill(best);});root.addEventListener('pointerleave',()=>hill(focus));
 root.addEventListener('focusin',e=>{focus=buttons.indexOf(e.target);hill(focus);});root.addEventListener('focusout',e=>{if(!root.contains(e.relatedTarget)){focus=-1;hill(-1);}});
 root.addEventListener('keydown',e=>{let i=buttons.indexOf(e.target);if(i<0)return;if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)){e.preventDefault();i=e.key==='Home'?0:e.key==='End'?buttons.length-1:(i+(e.key==='ArrowDown'?1:buttons.length-1))%buttons.length;buttons.forEach((b,n)=>b.tabIndex=n===i?0:-1);buttons[i].focus();}if(e.key==='Escape'){e.target.blur();hill(-1);}});
 buttons.forEach((b,i)=>b.onclick=()=>select(i));mobile.onchange=()=>{if(mobile.value!=='')select(+mobile.value);};
 let queued=false;const sync=()=>{queued=false;let chosen=0;entries.forEach((e,i)=>{if(!e.el.hidden&&e.el.getClientRects().length&&e.el.getBoundingClientRect().top<160)chosen=i;});active=chosen;updateActive();};addEventListener('scroll',()=>{if(!queued){queued=true;requestAnimationFrame(sync)}},{passive:true});sync();
 return{entries,select};
}
