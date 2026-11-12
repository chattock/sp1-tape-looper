// Shareable presentation links. Never start a demonstration or touch hardware.
export function initGuideLinks(content,{reduced=false,onSection}={}){
 const embedded=parent!==window;
 const href=id=>embedded?'#guide/'+id:'#'+id;
 const aliases={'card-center-placement':'card-placement','card-reset-timing':'card-reset-window'};
 function reveal(id){
  id=aliases[id]||id;
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
  const link=document.createElement('a');link.className='section-permalink';link.dataset.guideLink=section.id;link.textContent='#';link.setAttribute('aria-label','Link to '+heading.textContent.trim());heading.append(link);
 }
 for(const a of content.querySelectorAll('[data-guide-link]')){a.href=href(a.dataset.guideLink);if(embedded)a.target='_top';}
 content.addEventListener('click',e=>{const a=e.target.closest('a[data-guide-link]');if(!a||e.button||e.metaKey||e.ctrlKey||e.shiftKey||e.altKey)return;e.preventDefault();go(a.dataset.guideLink);});
 addEventListener('message',e=>{if(embedded&&e.source===parent&&e.origin===location.origin&&e.data?.type==='sp1-guide-target')reveal(e.data.target);});
 if(!embedded){const fromHash=()=>reveal(location.hash.slice(1));addEventListener('hashchange',fromHash);addEventListener('popstate',fromHash);requestAnimationFrame(fromHash);}
 return {go,reveal};
}
