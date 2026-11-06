import {UsbClient} from '../usb-client.mjs';
import {TransferClient} from '../transfer-client.mjs';
const button=document.getElementById('connect'),status=document.getElementById('status');
let client=null,storage=null,connected=false,busy=false;
const announce=()=>window.dispatchEvent(new Event('sp1-capabilities-changed'));
export function checkLegacyLayout(bytes){
 if(bytes.length!==24)throw Error('Unsupported storage layout');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),n=o=>v.getUint32(o,true);
 const known=new Set([0x53453341,0x53453234,0x53453241,0x53453441,0x53453841,0x53453136,0x53383136]);
 if(n(0)!==512||![8,16].includes(n(4))||n(8)!==4||n(12)<2||n(12)>4096||!n(16)||n(12)+n(4)*n(8)*n(16)>7733248||!known.has(n(20)))throw Error('This tool needs an older Tape Looper library. Use Library on the main site for v4 takes.');
}
window.sp1Workspace={
 get connected(){return connected;},get accessMessage(){return 'Connect SP-1 above, then open storage.';},
 async openTransfer(){if(!connected||storage)throw Error('Connect SP-1 and close any existing transfer first.');const session=new TransferClient(client);try{const layout=await session.enter();checkLegacyLayout(layout);storage=session;return{session,layout};}catch(e){await session.exit().catch(()=>{});throw e;}},
 async closeTransfer(){if(storage)await storage.exit();storage=null;announce();}
};
button.onclick=async()=>{if(busy)return;busy=true;button.disabled=true;try{
 if(client){if(storage&&!(await document.getElementById('tool').contentWindow.sp1CloseTransfer()))return;await client.close();client=null;connected=false;status.textContent='Not connected';}
 else{client=new UsbClient();client.onError=e=>{connected=false;status.textContent=e.message;announce();window.dispatchEvent(new Event('sp1-storage-disconnected'));};await client.connect();connected=true;status.textContent='Connected · open storage below';}
}catch(e){status.textContent=e.name==='NotFoundError'?'No device selected':e.message;await client?.close();client=null;connected=false;}finally{busy=false;button.disabled=false;button.textContent=client?'Disconnect':'Connect SP-1';announce();}};
window.addEventListener('beforeunload',e=>{if(storage){e.preventDefault();e.returnValue='';}});
announce();
