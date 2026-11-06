// One expiry timer shared by changed nodes; no polling, reflow or animation loop.
const active=new Map();let timer;
function expire(){timer=null;const now=performance.now();for(const [node,end] of active)if(end<=now||!node.isConnected){node.classList.remove('device-active');active.delete(node);}schedule();}
function schedule(){if(timer||!active.size)return;let end=Infinity;for(const value of active.values())end=Math.min(end,value);timer=setTimeout(expire,Math.max(1,end-performance.now()));}
export function highlight(node){if(!node)return;node.classList.add('device-active');active.set(node,performance.now()+850);schedule();}
export function clearHighlights(){clearTimeout(timer);timer=null;for(const node of active.keys())node.classList.remove('device-active');active.clear();}
export function setReading(node,value,container=node,threshold=0){const text=String(value);if(node.textContent===text)return;const before=node.textContent;node.textContent=text;if(!before||before==='—'||text==='—'){node._highlightAnchor=parseFloat(text);return;}if(threshold){const value=parseFloat(text);if(!Number.isFinite(node._highlightAnchor))node._highlightAnchor=parseFloat(before);if(Math.abs(value-node._highlightAnchor)<threshold)return;node._highlightAnchor=value;}highlight(container);}
