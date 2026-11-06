// One bounded, sequential worker per Library import batch. Terminate on error,
// abort or deadline; no stale worker reply can satisfy a later decode.
export function createDecoderSession(){
 let worker=null,busy=false,closed=false;
 function close(){closed=true;worker?.terminate();worker=null;}
 async function decode(bytes,{stream=null,channelMode='auto',clip=null,signal,phase=()=>{}}={}){
  if(closed)throw Error('Preparation session closed');if(busy)throw Error('Decoder already preparing audio');
  signal?.throwIfAborted();busy=true;
  try{return await new Promise((resolve,reject)=>{
   worker??=new Worker(new URL('./decode-worker.js?v=audio-source-1',import.meta.url));
   let settled=false;const timer=setTimeout(()=>finish(Error('Decoder deadline exceeded')),120000);
   const abort=()=>finish(signal.reason||Error('Cancelled'));
   function finish(error,value){if(settled)return;settled=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);if(error){worker?.terminate();worker=null;}else{worker.onmessage=null;worker.onerror=null;}error?reject(error):resolve(value);}
   signal?.addEventListener('abort',abort,{once:true});
   worker.onmessage=e=>{if(e.data.phase){phase(e.data.phase);return;}e.data.ok?finish(null,e.data):finish(Object.assign(Error(e.data.error),{code:e.data.code,duration:e.data.duration}));};
   worker.onerror=e=>finish(Error(e.message));worker.postMessage({bytes,stream,channelMode,clip},[bytes]);
  });}finally{busy=false;}
 }
 return {decode,close};
}
