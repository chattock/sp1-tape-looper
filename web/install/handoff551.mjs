// USB discovery only. This module never opens a port or issues commands.
export async function discoverHandoff(serial,pid,{wait=ms=>new Promise(r=>setTimeout(r,ms)),now=()=>performance.now(),timeout=8000}={}){
 const started=now();let products=[];
 for(;;){
  const ports=(await serial.getPorts?.()??[]).filter(p=>p.connected!==false&&p.getInfo().usbVendorId===0x2fe3);
  products=ports.map(p=>p.getInfo().usbProductId);const matches=ports.filter(p=>p.getInfo().usbProductId===pid);
  if(matches.length===1)return {port:matches[0],products};
  if(matches.length>1||now()-started>=timeout)return {port:null,products};
  await wait(Math.min(250,Math.max(0,timeout-(now()-started))));
 }
}
