import {filterFx} from './fx-rack610.mjs?v=610';
import {filterRockers} from './rocker-guide.js?v=610';
import {cards} from './content.js?v=620';
function init(){
 if(window!==parent)document.body.classList.add('embedded-workspace');
 const content=document.getElementById('content'),wrap=document.createElement('div');wrap.className='guide-search';wrap.innerHTML=`<span class="eyebrow guide-version">TAPE LOOPER 4.0</span><button type="button" id="guideSearchToggle" aria-label="Search Field guide" aria-expanded="false" aria-controls="guideSearchField"><svg viewBox="0 0 24 24" width="19" height="19" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5"/><path d="m15.5 15.5 5 5"/></svg></button><div id="guideSearchField" class="guide-search-field" inert><input type="search" id="guideSearch" aria-label="Search Field guide" placeholder="Takes, Slow gear, bounce…" tabindex="-1"></div><span id="guideSearchCount" role="status"></span>`;content.prepend(wrap);
 const toggle=wrap.querySelector('button'),field=wrap.querySelector('.guide-search-field');
 const expand=on=>{wrap.classList.toggle('is-open',on);toggle.setAttribute('aria-expanded',String(on));field.inert=!on;const search=field.querySelector('input');search.tabIndex=on?0:-1;if(on)search.focus();};toggle.onclick=()=>{const on=!wrap.classList.contains('is-open');if(!on){input.value='';filter();}expand(on);};
 const input=wrap.querySelector('input'),count=wrap.querySelector('#guideSearchCount'),catalog=new Map(cards.map(c=>['card-'+c.id,[c.title,c.gesture,c.body,c.note,c.group].join(' ').toLowerCase()]));let saved=null;
 const filter=()=>{const q=input.value.trim().toLowerCase();if(q&&!saved)saved=new Map([...content.querySelectorAll('details')].map(d=>[d,d.open]));let hits=0;
 for(const card of content.querySelectorAll("article[id*='card-']")){const own=!q||catalog.get(card.id.replace(/^new-/,''))?.includes(q),children=[...card.querySelectorAll("article[id*='card-']")];const childHit=children.some(c=>catalog.get(c.id.replace(/^new-/,''))?.includes(q));card.hidden=!(own||childHit);if(q&&own)hits++;}
 const fxHits=filterFx(content,q);if(q)hits+=fxHits;const rockerHits=filterRockers(content,q);if(q)hits+=rockerHits.length;
 for(const group of content.querySelectorAll('.tile-section:not(.rocker-guide):not(.fx-guide)'))group.hidden=!!q&&![...group.querySelectorAll('.tile')].some(c=>!c.hidden);
 for(const cat of content.querySelectorAll('.guide-category'))cat.hidden=!!q&&![...cat.querySelectorAll('.tile-section')].some(c=>!c.hidden);
 for(const id of ['start-here','whats-new']){const intro=document.getElementById(id);if(intro)intro.hidden=!!q;}
 if(q){for(const d of content.querySelectorAll('details.related-guides'))d.open=[...d.querySelectorAll('article')].some(c=>!c.hidden);count.textContent=hits?`${hits} ${hits===1?'match':'matches'}`:'No matches';}else{count.textContent='';if(saved){for(const [d,open] of saved)d.open=open;saved=null;}}
 };
 input.oninput=filter;input.onkeydown=e=>{if(e.key==='Escape'){input.value='';filter();expand(false);toggle.focus();}};
 document.getElementById('categories').addEventListener('click',()=>{if(input.value){input.value='';filter();}},true);
}
if(document.readyState==='loading')addEventListener('DOMContentLoaded',init,{once:true});else init();
function alignBrowseStage(){
 if(window===parent)return;
 const frame=window.frameElement;
 if(!frame)return;
 const header=parent.document.querySelector('.topbar');
 if(!header)return;
 const offset=header.getBoundingClientRect().bottom-frame.getBoundingClientRect().top;
 document.body.style.setProperty('--model-stage-offset',offset+'px');
}
addEventListener('resize',alignBrowseStage);
addEventListener('message',e=>{if(e.origin!==location.origin||e.source!==parent||e.data?.type!=='sp1-guide-visibility')return;
 if(!e.data.visible){const close=document.getElementById('closeDemo');close?.click();}
 document.body.hidden=!e.data.visible;
 if(e.data.visible)alignBrowseStage();
});
