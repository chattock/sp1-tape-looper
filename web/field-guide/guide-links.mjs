// Shareable presentation links. Never start a demonstration or touch hardware.
export function initGuideLinks(content,{reduced=false,onSection}={}){
 const embedded=parent!==window;
 const href=id=>embedded?'#guide/'+id:'#'+id;
 const slug=text=>text.normalize('NFKD').replace(/[\u0300-\u036f]/g,'').toLowerCase().replace(/['’]/g,'').replace(/&/g,' and ').replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');
 const destinations=new Map(),names=new Map();
 const aliases={'card-center-placement':'card-placement','card-reset-timing':'card-reset-window'};
 function reveal(id){
  id=destinations.get(id)||aliases[id]||id;
  if(!/^[a-z0-9-]+$/.test(id))return;
  const target=document.getElementById(id);if(!target||!content.contains(target))return;
  const search=document.getElementById('guideSearch');if(search?.value){search.value='';search.dispatchEvent(new Event('input'));}
  for(let p=target;p&&p!==content;p=p.parentElement){
   if(p.tagName==='DETAILS')p.open=true;
   if(p.getAttribute('role')==='tabpanel')document.getElementById(p.getAttribute('aria-labelledby'))?.click();
  }
  const destination=target.getAttribute('role')==='tabpanel'?(target.closest('.fx-guide,.rocker-guide')||target):target;
  destination.scrollIntoView({behavior:reduced?'instant':'smooth',block:'start'});
  const section=target.closest('[data-section]');if(section)onSection?.(+section.dataset.section);
 }
 function go(id){
  if(embedded)parent.postMessage({type:'sp1-guide-link',target:id},location.origin);
  else{history.pushState(null,'',href(id));reveal(id);}
 }
 for(const section of content.querySelectorAll('section,article[id]')){
  const heading=section.querySelector(':scope > h1,:scope > h2,:scope > .category-heading h2,:scope > .tile-heading h3,:scope > .rocker-heading h2,:scope > .rocker-mode-heading h3,:scope > .rocker-mode-top h3');
  if(!heading)continue;
  if(!section.id)section.id='section-'+heading.textContent.trim().toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/-$/,'');
  const title=heading.textContent.trim();
  // Repeated cards in What's new share the main card's canonical link.
  const identity=section.id.replace(/^new-/,'');
  const base=identity==='section-track-trim'?'track-trim-controls':slug(title)||'guide';
  let name=names.get(identity);
  if(!name){
   name=base;
   if(destinations.has(name)&&destinations.get(name).replace(/^new-/,'')!==identity)
    name=base+'-'+identity.replace(/^(card-|section-)/,'');
   names.set(identity,name);destinations.set(name,section.id);
  }else if(!section.id.startsWith('new-'))destinations.set(name,section.id);
  const link=document.createElement('a');link.className='section-permalink';link.dataset.guideLink=name;
  link.setAttribute('aria-label','Copy link to '+title);link.title='Copy link';
  link.innerHTML='<svg class="link-copy" viewBox="0 0 16 16" aria-hidden="true"><rect x="5" y="5" width="9" height="9" rx="1.5"/><path d="M10 5V2H2v8h3"/></svg><svg class="link-check" viewBox="0 0 16 16" aria-hidden="true"><path d="m3 8 3 3 7-7"/></svg>';
  heading.append(link);
 }
 for(const a of content.querySelectorAll('[data-guide-link]')){a.href=href(a.dataset.guideLink);if(embedded)a.target='_top';}
 const copiedTimers=new WeakMap();
 const status=document.createElement('span');status.className='guide-link-status';status.setAttribute('role','status');content.append(status);
 content.addEventListener('click',async e=>{
  const a=e.target.closest('a[data-guide-link]');if(!a||e.button||e.metaKey||e.ctrlKey||e.shiftKey||e.altKey)return;
  e.preventDefault();go(a.dataset.guideLink);
  const url=new URL(embedded?parent.location.href:location.href);url.hash=href(a.dataset.guideLink);
  try{
   await navigator.clipboard.writeText(url.href);
   clearTimeout(copiedTimers.get(a));a.classList.add('copied');a.title='Link copied';status.textContent='Link copied';
   copiedTimers.set(a,setTimeout(()=>{a.classList.remove('copied');a.title='Copy link';status.textContent='';},1600));
  }catch{status.textContent='Could not copy link. Copy the address from your browser.';}
 });
 addEventListener('message',e=>{if(embedded&&e.source===parent&&e.origin===location.origin&&e.data?.type==='sp1-guide-target')reveal(e.data.target);});
 if(!embedded){const fromHash=()=>reveal(location.hash.slice(1));addEventListener('hashchange',fromHash);addEventListener('popstate',fromHash);requestAnimationFrame(fromHash);}
 return {go,reveal};
}
