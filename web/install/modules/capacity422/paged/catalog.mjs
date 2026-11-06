// Versioned fixed-width records consumed by the paged catalog adapter.
// ID0=header,1..65=64 track lanes + unassigned legacy archive lane.
// Extents remain pool-relative and payload bytes are not transformed.
import {encodeIndex,decodeIndex} from './binary.mjs';
const get=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
const record=(...words)=>{const b=new Uint8Array(32),v=new DataView(b.buffer);words.forEach((x,i)=>v.setUint32(i*4,x,true));return b};
export function convertLegacy(c,poolBlocks,{sectors=8192}={}){
 if(!Number.isInteger(poolBlocks)||poolBlocks<=0||c.refs.length!==128)throw Error('Invalid legacy geometry');
 c={...c,lanes:c.lanes??Array(64).fill(0),ranks:c.ranks??Array(64).fill(0)};
 const objects=new Map(c.objects.map(x=>[x.id,x])),records=new Map(),lanes=Array.from({length:65},()=>[]),objectId=x=>x===65535?0:66+x,annotationId=x=>x?66+x-1:0;
 const needed=new Set(),counts=new Map();
 const count=id=>counts.set(id,(counts.get(id)??0)+1);
 for(let i=0;i<128;i++)if(c.refs[i]!==65535){needed.add(c.refs[i]);count(c.refs[i]);if(c.annotations[i]){needed.add(c.annotations[i]-1);count(c.annotations[i]-1);}}
 if(c.control){needed.add(c.control-1);count(c.control-1);}
 for(const id of needed){const o=objects.get(id);if(!o||!o.blocks||o.first<0||o.first+o.blocks>poolBlocks)throw Error('Invalid source extent');records.set(66+id,record(3,o.first,o.blocks,o.crc,o.tag,counts.get(id)));}
 for(let i=0;i<64;i++)if(c.refs[64+i]!==65535){const owner=c.lanes[i]&127;if(owner>64)throw Error('Invalid lane owner');lanes[owner?owner-1:64].push(i);}
 for(let lane=0;lane<65;lane++){
  const saved=lanes[lane].sort((a,b)=>c.ranks[a]-c.ranks[b]||a-b),ids=saved.map(i=>194+i);let selected=0;
  saved.forEach((i,n)=>{if(c.lanes[i]&128){if(selected)throw Error('Multiple selected takes');selected=194+i;}records.set(194+i,record(4,objectId(c.refs[64+i]),annotationId(c.annotations[64+i]),lane,ids[n-1]??0,ids[n+1]??0,n+1));});
  const audio=lane<64?objectId(c.refs[lane]):0,annotation=lane<64?annotationId(c.annotations[lane]):0;
  const matching=audio?saved.findIndex(i=>objectId(c.refs[64+i])===audio&&annotationId(c.annotations[64+i])===annotation):-1;
  selected=matching>=0?ids[matching]:0;
  records.set(1+lane,record(2,audio,annotation,selected,ids[0]??0,ids.at(-1)??0,ids.length,audio?(matching>=0?matching+1:ids.length+1):0));
 }
 const gen=BigInt(c.generation);records.set(0,record(1,1,258,c.control?objectId(c.control-1):0,poolBlocks,0,0));
 const result=encodeIndex(records,{generation:gen+1n,sectors});validateCatalog(decodeIndex(result.image));return result;
}
export function validateCatalog(index){
 const r=index.records,h=r.get(0);if(!h||get(h,0)!==1||get(h,4)!==1||get(h,8)<258||get(h,8)>123*123*15||!get(h,16)||get(h,20)||get(h,24))throw Error('Catalog header');
 const pool=get(h,16),next=get(h,8),objects=new Map(),seen=new Set();
 for(const [id,b]of r){if(id>=next)throw Error('ID beyond allocation frontier');if(get(b,0)===3){if(get(b,24))throw Error('Unpublished payload reservation');const first=get(b,4),blocks=get(b,8);if(!blocks||first>=pool||blocks>pool-first)throw Error('Payload extent');objects.set(id,{first,blocks,crc:get(b,12),tag:get(b,16)});}else if(![1,2,4,5].includes(get(b,0)))throw Error('Record type');}
 const references=new Map(),use=id=>{if(id)references.set(id,(references.get(id)??0)+1)};
 const check=(audio,annotation)=>{if(audio&&!objects.has(audio)||annotation&&(!audio||!objects.has(annotation)||objects.get(annotation).blocks!==1))throw Error('Dangling payload');use(audio);use(annotation);};
 if(get(h,12)&&!objects.has(get(h,12)))throw Error('Control extent');use(get(h,12));
 const lanes=[];
 for(let lane=0;lane<65;lane++){
  const b=r.get(lane+1);if(!b||get(b,0)!==2)throw Error('Missing lane');check(get(b,4),get(b,8));let id=get(b,16),previous=0;const entries=[];
  while(id){if(seen.has(id)||id<66)throw Error('Archive cycle/alias');seen.add(id);const a=r.get(id);if(!a||get(a,0)!==4||get(a,12)!==lane||get(a,16)!==previous)throw Error('Archive linkage');check(get(a,4),get(a,8));if(!get(a,4))throw Error('Empty saved take');entries.push(id);previous=id;id=get(a,20);}
  if(previous!==get(b,20)||entries.length!==get(b,24)||get(b,12)&&!entries.includes(get(b,12)))throw Error('Lane selection/count');
  const selected=get(b,12),ordinal=get(b,28),expected=get(b,4)?(selected?entries.indexOf(selected)+1:entries.length+1):0;
  if(ordinal!==expected||!get(b,4)&&selected)throw Error('Lane ordinal');
  if(selected){const a=r.get(selected);if(get(a,4)!==get(b,4)||get(a,8)!==get(b,8))throw Error('Selected take mismatch');}
  lanes.push({ordinal,audio:get(b,4),annotation:get(b,8),selected:get(b,12),entries});
 }
 for(const [id,b]of r)if(get(b,0)===4&&!seen.has(id))throw Error('Unreachable archive');
 const free=new Set();let f=get(h,28);while(f){if(f<66||free.has(f))throw Error('Free ID cycle');free.add(f);const b=r.get(f);if(!b||(get(b,0)!==5&&(get(b,0)!==3||get(b,20)||get(b,24))))throw Error('Free ID is still live');f=get(b,28);}
 for(const [id,b]of r)if((get(b,0)===5||get(b,0)===3&&!get(b,20))&&!free.has(id))throw Error('Lost free ID');
 // Aliases share a single descriptor ID; distinct descriptors must not overlap.
 for(const [id]of objects)if(get(r.get(id),20)!==(references.get(id)??0))throw Error('Reference count mismatch');
 const extents=[...objects].filter(([id])=>references.has(id)).map(([,o])=>o).sort((a,b)=>a.first-b.first);for(let i=1;i<extents.length;i++)if(extents[i].first<extents[i-1].first+extents[i-1].blocks)throw Error('Overlapping objects');
 return {lanes,objects,poolBlocks:pool,nextId:next,control:get(h,12),generation:index.generation};
}
