// One joint history for workspace tabs, library folders and setup screens.
// Entries contain presentation state only: no handles, commands or authority.
const host=window.top?.location.origin===location.origin?window.top:window;
function controller(){
 if(host.__sp1Navigation)return host.__sp1Navigation;
 const sid=host.crypto.randomUUID(),clients=new Map();let current={sid,index:0,view:host.location.hash.slice(1)||'live',states:{}},bouncing=false;
 const url=view=>{const u=new URL(host.location.href);if(host!==window||host.document.getElementById('tabSetup'))u.hash=view;return u};
 const notify=()=>host.dispatchEvent(new host.CustomEvent('sp1-navigation',{detail:{view:current.view}}));
 const save=replace=>host.history[replace?'replaceState':'pushState']({...host.history.state,sp1Navigation:current},'',url(current.view));
 save(true);
 const api={
  get restoring(){return bouncing},
  register(key,{restore,busy}){clients.set(key,{restore,busy});return()=>clients.delete(key)},
  update(key,data,{view=current.view,replace=false}={}){current={...current,index:current.index+(replace?0:1),view,states:{...current.states,[key]:data}};save(replace);notify()},
  route(view){if([...clients.values()].some(c=>c.busy()))return false;current={...current,index:current.index+1,view};save(false);notify();return true},
  back(){host.history.back()},
  adoptHash(){if(bouncing)return;const view=host.location.hash.slice(1)||'live';if(view===current.view)return;current={...current,index:current.index+1,view};save(true);notify()}
 };
 host.addEventListener('popstate',e=>{
  const target=e.state?.sp1Navigation;
  if(bouncing){bouncing=false;notify();return}
  if([...clients.values()].some(c=>c.busy())){
   const delta=target?.sid===sid?current.index-target.index:1;
   if(delta){bouncing=true;host.history.go(delta)}return;
  }
  if(target?.sid===sid)current=target;
  else{current={...current,index:0,view:host.location.hash.slice(1)||'live'};save(true)}
  notify();
  for(const [key,c]of clients)if((key==='library'?'transfer':key)===current.view)Promise.resolve(c.restore(current.states[key])).catch(()=>{});
 });
 host.__sp1Navigation=api;return api;
}
export const navigation=controller();
export function createNavigation(key,options){navigation.register(key,options);return {save:(data,replace=false)=>navigation.update(key,data,{view:key==='library'?'transfer':key,replace}),back:()=>navigation.back()}}
