// Read-only active-song lanes. Telemetry contains cached ordinals, never file names.
export function visibleTakes(total,selected){
 if(total<=7)return Array.from({length:total},(_,i)=>i+1);
 const start=Math.max(1,Math.min(total-6,(selected||total)-3));
 return Array.from({length:7},(_,i)=>start+i);
}
export function renderTakeLanes(snapshot){
 const d=snapshot?.details,lanes=d?.takeLanes;
 for(let i=0;i<4;i++){
  const card=document.getElementById('trackLevel'+i)?.closest('.mix-channel');if(!card)continue;
  let view=card.querySelector('.take-lane');
  if(!view){view=document.createElement('div');view.className='take-lane';card.append(view);}
  const lane=lanes?.[i],busy=!!(d?.stash?.busy&&d.stash.laneTrack===i+1);
  const key=JSON.stringify([snapshot?.song,lane?.selected,lane?.total,busy,d?.tracks?.[i]?.state,!!snapshot,d?.version,d?.basic]);
  if(view.dataset.key===key)continue;view.dataset.key=key;view.replaceChildren();
  const title=document.createElement('div');title.className='take-lane-label';
  if(d?.basic){title.textContent='Single recording';view.append(title);continue;}
  title.textContent=lane?`${lane.total} ${lane.total===1?'take':'takes'}`:snapshot?'Takes':'—';view.append(title);
  if(!lane){const hint=document.createElement('span');hint.className='take-lane-wait';
   hint.textContent=!snapshot?'':busy?'Switching…':d?.tracks?.[i]?.state===2?'Recording…':d?.saving||d?.tracks?.[i]?.state===3?'Saving…':'Take details unavailable';view.append(hint);continue;}
  const strip=document.createElement('div');strip.className='take-strip';strip.setAttribute('role','list');strip.setAttribute('aria-label',`Song ${snapshot.song}, Track ${i+1} takes`);
  const numbers=visibleTakes(lane.total,lane.selected);
  function chip(number){const item=document.createElement('span');item.className='take-chip';item.setAttribute('role','listitem');
   item.textContent=number===0?'New':String(number);const active=lane.selected===number;item.classList.toggle('current',active);
   if(active)item.setAttribute('aria-current','true');item.setAttribute('aria-label',`${number===0?'New lane':'Take '+number}${active?', selected':''}`);strip.append(item);}
  function gap(){const el=document.createElement('span');el.className='take-gap';el.textContent='…';el.setAttribute('aria-hidden','true');strip.append(el);}
  if(numbers[0]>1)gap();numbers.forEach(chip);if(numbers.at(-1)<lane.total)gap();chip(0);view.append(strip);
 }
}
