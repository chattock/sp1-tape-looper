import {u16,u32,text} from './fat.mjs?v=396.2';
import {crc32} from './backup/core.mjs?v=396.2';
const fail=s=>{throw Error(s)};
export function catalog(b,poolBlocks){
 if(b.length!==4096||text(b.slice(0,8))!=='SP1POOL2'||![2,3,4].includes(u32(b,8))||crc32(b.slice(0,4092))!==u32(b,4092))fail('Invalid catalog checksum/version');
 const version=u32(b,8),objects=[],refs=[],annotations=[],used=new Set();
 for(let i=24;i<32;i++)if(b[i])fail('Reserved catalog field');
 for(let i=version===4?2720:(version===3?2592:2336);i<4092;i++)if(b[i])fail('Reserved catalog bytes');
 for(let i=0;i<128;i++){const o=32+i*16;objects.push({id:i,first:u32(b,o),blocks:u32(b,o+4),crc:u32(b,o+8),tag:u32(b,o+12)});}
 for(const o of objects){if(!o.blocks)continue;if(o.first>=poolBlocks||o.blocks>poolBlocks-o.first)fail('Object outside pool');for(const p of objects.slice(0,o.id))if(p.blocks&&o.first<p.first+p.blocks&&p.first<o.first+o.blocks)fail('Overlapping objects');}
 const control=u32(b,20);if(control>128||control&&!objects[control-1].blocks)fail('Invalid controls');if(control)used.add(control-1);
 for(let i=0;i<128;i++){const r=u16(b,2080+i*2),a=version>=3?u16(b,2336+i*2):0;if(r!==65535&&(r>=128||!objects[r].blocks))fail('Invalid reference');if(a>128||a&&(r===65535||!objects[a-1].blocks))fail('Invalid annotation');refs.push(r);annotations.push(a);if(r!==65535)used.add(r);if(a)used.add(a-1);}
 const extra={};if(version===4){
  const lanes=Array.from(b.slice(2592,2656)),ranks=Array.from(b.slice(2656,2720));
  for(let i=0;i<64;i++){const lane=lanes[i]&127,rank=ranks[i];if(lane>64||rank>64||(!lane&&(rank||lanes[i]))||!!lane!==!!rank||(lane&&refs[64+i]===65535))fail('Invalid stash lane');for(let j=0;j<i;j++)if(lane&&(lanes[j]&127)===lane&&(ranks[j]===rank||((lanes[i]&128)&&(lanes[j]&128))))fail('Conflicting stash lane');}
  Object.assign(extra,{lanes,ranks});
 }
 return {...extra,generation:(BigInt(u32(b,16))<<32n|BigInt(u32(b,12))).toString(),refs,annotations,control,objects:objects.filter(o=>used.has(o.id))};
}
