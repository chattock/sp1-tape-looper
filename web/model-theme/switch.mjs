const preferenceKey='sp1-model-style';

export function mountModelSwitch(device){
 const button=document.createElement('button');
 button.type='button';
 button.className='model-theme-switch';
 button.setAttribute('aria-label','Hairline model');
 button.setAttribute('aria-pressed','false');
 button.title='Switch model style';
 const mark=document.createElement('span');
 mark.className='model-theme-mark';
 mark.setAttribute('aria-hidden','true');
 button.append(mark);
 device.host.parentElement.classList.add('model-switch-stage');
 device.host.before(button);

 let hairline,enabled=false,revision=0;
 async function select(next,animate=false){
  const request=++revision;
  button.setAttribute('aria-pressed',String(next));
  button.disabled=true;
  try{
   if(next!==enabled){
    // Let the lever finish before building outlines or rendering the new materials.
    const wait=animate&&!matchMedia('(prefers-reduced-motion: reduce)').matches
     ?new Promise(resolve=>setTimeout(resolve,150)):null;
    const [module]=await Promise.all([next&&!hairline?import('./hairline.mjs'):null,wait]);
    if(request!==revision)return;
    if(module)hairline=module.createHairline(device);
    else hairline.setEnabled(next);
    enabled=next;
    device.updateModelTheme=enabled?hairline.update:null;
   }
   button.title='Switch model style';
  }catch(error){
   if(request!==revision)return;
   console.error('Could not switch model style',error);
   button.setAttribute('aria-pressed',String(enabled));
   button.title='Could not load Hairline. Click to retry.';
  }finally{
   if(request===revision)button.disabled=false;
  }
  return request===revision&&enabled===next;
 }
 button.addEventListener('click',async()=>{
  const next=!enabled;
  if(!await select(next,true))return;
  try{localStorage.setItem(preferenceKey,next?'hairline':'original');}catch{}
 });
 window.addEventListener('storage',event=>{
  if(event.key===preferenceKey||event.key===null)void select(event.newValue==='hairline');
 });
 try{if(localStorage.getItem(preferenceKey)==='hairline')void select(true);}catch{}
}
