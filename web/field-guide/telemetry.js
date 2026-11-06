// Aggregate guide usage only. No audio, input contents, or session recordings.
const production=['sp1-tape-looper.vercel.app','sp1-tape-guide.vercel.app'].includes(location.hostname);
const enabled=production&&navigator.doNotTrack!=='1';
const seen=new Set();let sent=0,catalog=[],sections=[];
export function configureAnalytics(cards,categories){catalog=cards;sections=categories;}

export function track(name,data={}){
 if(!enabled||sent>=200)return;
 sent++;
 const card=catalog.find(c=>c.id===data.guide||c.title===data.guide);
 if(card){const step=card.steps[data.step-1];data={...data,guide:card.title,guide_id:card.id,category:sections.find(c=>c.id===card.category)?.short||String(card.category),...(step?{step_label:step.label}:{})};}
 else if(data.category)data={...data,category:sections.find(c=>c.id===data.category)?.short||String(data.category)};
 try{window.va?.('event',{name,data});}catch{} // Analytics must never interrupt the guide.
}
export function trackOnce(key,name,data={}){if(seen.has(key))return;seen.add(key);track(name,data);}
if(enabled){
 window.va=window.va||function(){(window.vaq=window.vaq||[]).push(arguments)};
 window.si=window.si||function(){(window.siq=window.siq||[]).push(arguments)};
 const clean=event=>{if(event.url){const u=new URL(event.url);u.search='';u.hash='';event.url=u.href}return event};
 window.va('beforeSend',clean);window.si('beforeSend',clean);
 for(const src of ['/_vercel/insights/script.js','/_vercel/speed-insights/script.js']){const s=document.createElement('script');s.defer=true;s.src=src;document.head.appendChild(s)}
 addEventListener('error',e=>{if(e instanceof ErrorEvent)trackOnce('script-error','client_error',{kind:'javascript'})});
 addEventListener('unhandledrejection',()=>trackOnce('promise-error','client_error',{kind:'unhandled_promise'}));
 document.addEventListener('click',e=>{if(e.target.closest('.print-link'))track('print_guide_opened')});
 document.addEventListener('toggle',e=>{if(e.target.open&&e.target.matches('.card-note,.related-guides')){const id=e.target.closest('article').id.slice(5),kind=e.target.matches('.card-note')?'good_to_know_opened':'related_guides_opened';trackOnce(kind+':'+id,kind,{guide:id});}else if(e.target.open&&e.target.matches('.gesture-primer'))trackOnce('gesture-primer','gesture_primer_opened');},true);
}
