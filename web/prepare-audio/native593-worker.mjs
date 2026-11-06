import {encodeNative593} from './native593.mjs?v=593';
self.onmessage=e=>{try{const b=encodeNative593(new Uint8Array(e.data.source),e.data.binding);self.postMessage({bytes:b.buffer},[b.buffer])}catch(error){self.postMessage({error:error.message||String(error)})}};
