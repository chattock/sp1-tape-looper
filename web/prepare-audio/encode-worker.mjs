import {encode} from './audio.mjs?v=571';
self.onmessage=e=>{try{const x=encode(e.data.channels);self.postMessage({ok:true,...x},[x.bytes.buffer])}catch(e){self.postMessage({ok:false,error:e.message})}};
