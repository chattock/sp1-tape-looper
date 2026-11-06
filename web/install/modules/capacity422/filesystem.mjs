import {inspect as inspectFat,contiguous,read,u32,equal} from './fat.mjs';
import {decodeIndex} from './paged/binary.mjs';
import {validateCatalog} from './paged/catalog.mjs';
import {crc32} from '../shared396/site/backup/core.mjs';
export const INDEX_SECTORS=8192;
export async function inspect422(d,progress=()=>{}){
 return inspectFat(d,progress,async({g,fat,root,dirs,guards})=>{
  const own=root.entries.find(e=>e.name==='TAPELOOP   ');if(!own)return null;
  const dir=dirs.find(x=>x.c===own.cluster);if(!dir)return null;
  const entries=['INDEX0  BIN','INDEX1  BIN','AUDIO   BIN'].map(n=>dir.entries.find(e=>e.name===n));
  if(!entries[0]&&!entries[1])return null;
  if(entries.some(e=>!e||e.attr&16)||entries[0].bytes!==INDEX_SECTORS*512||entries[1].bytes!==INDEX_SECTORS*512)throw Error('Incomplete paged library');
  // Old firmware must fail admission, never operate a stale parallel catalog.
  if(dir.entries.some(e=>['CAT0    BIN','CAT1    BIN'].includes(e.name)))throw Error('Legacy catalog still writable under new namespace');
  const workspaceEntry=dir.entries.find(e=>e.name==='WORK    BIN');
  let workspace=null;if(workspaceEntry){if(workspaceEntry.attr&16||workspaceEntry.bytes<64*1024*1024||workspaceEntry.bytes%32768)throw Error('Invalid maintenance workspace');workspace=contiguous(g,fat,workspaceEntry.cluster,workspaceEntry.bytes);}
  const extents=entries.map(e=>contiguous(g,fat,e.cluster,e.bytes));if(extents[2].blocks%g.spc)throw Error('Pool slack not owned');
  const headers=[],generations=[],tails=[];
  for(let bank=0;bank<2;bank++){
   const e=extents[bank],h=await read(d,e.lba,2);headers.push(h);guards.push({lba:e.lba,b:h});
   let gen=null,tail=0;
   for(let i=0;i<2;i++){const b=h.subarray(i*512,(i+1)*512);
    if(new TextDecoder().decode(b.subarray(0,8))!=='PG422IDX'||u32(b,8)!==0||u32(b,508)!==crc32(b.subarray(0,508)))continue;
    const t=u32(b,24),root=u32(b,20),g=BigInt(u32(b,12))+(BigInt(u32(b,16))<<32n);
    if(t<2||t>INDEX_SECTORS||(root&&(root<2||root>=t)))throw Error('Invalid bank header geometry');
    tail=Math.max(tail,t);if(gen===null||g>gen)gen=g;
   }
   generations.push(gen);tails.push(tail);
  }
  if(generations.every(x=>x===null))throw Error('No valid paged checkpoint');
  const active=generations[0]===null||(generations[1]!==null&&generations[1]>generations[0])?1:0,banks=[null,null];
  for(let bank=0;bank<2;bank++){
   // A retired bank may contain a partially compacted tree. Only a bank tied
   // for newest generation can be authoritative; never follow retired pages.
   if(generations[bank]!==generations[active])continue;
   const image=new Uint8Array(INDEX_SECTORS*512);image.set(await read(d,extents[bank].lba,tails[bank]));
   const index=decodeIndex(image),catalog=validateCatalog(index);if(catalog.poolBlocks!==extents[2].blocks)throw Error('Index/pool size disagreement');banks[bank]={index,catalog,bytes:image.subarray(0,index.tail*512)};
   progress({phase:'inspect-paged-index',done:bank+1,total:2});
  }
  if(banks[0]&&banks[1]&&!equal(banks[0].bytes,banks[1].bytes))throw Error('Conflicting index banks');
  return {path:'/TAPELOOP',workspace,format:5,active,extents,banks,catalog:banks[active].catalog};
 });
}
