// Build a new regular FAT32 image from a verified music backup. Never writes a
// device. Layout matches the native pool2 and take1 codecs; all writes land in
// a host file chosen explicitly by the caller. No public migration UI yet.
import {verifyBackup,crc32} from './core.mjs?v=396.1';
const enc=new TextEncoder(),TAG=0x314b4154,CONTROL=0x314c5443;
const view=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
const get=(b,o)=>view(b).getUint32(o,true),put=(b,o,x)=>view(b).setUint32(o,x,true);
function geometry(blocks,spc=64){
 if(![8,16,32,64,128].includes(spc))throw Error("Unsupported cluster size");
 const reserved=32;let fat=1;
 for(let i=0;i<16;i++){const clusters=Math.floor((blocks-reserved-2*fat)/spc),next=Math.ceil((clusters+2)*4/512);if(next===fat){if(clusters<65525||clusters>=0x0ffffff0)throw Error('Unsupported FAT32 geometry');return {blocks,spc,reserved,fat,clusters,data:reserved+2*fat}}fat=next}
 throw Error('FAT geometry did not converge');
}
function dirEntry(name,attr,cluster,bytes=0){const b=new Uint8Array(32);b.fill(32,0,11);b.set(enc.encode(name),0);b[11]=attr;view(b).setUint16(20,cluster>>>16,true);view(b).setUint16(26,cluster&65535,true);put(b,28,bytes);return b}
export function takeHeader(meta,f){
 const b=new Uint8Array(8192),s=f.song-1,t=f.track-1,row=8+s*44,xe=1536+16+(s*4+t)*16;
 const fpb=f.codec===6?496:280,loop=get(meta,row+4),length=get(meta,row+12+t*4)||Math.floor((loop+fpb/2)/fpb);
 let frames=length*fpb,start=get(meta,row+28+t*4)*fpb;
 if(loop&&loop%fpb){frames=Math.max(1,Math.floor((length*fpb+loop/2)/loop))*loop}
 const exact=get(meta,xe+4),exactStart=get(meta,xe);
 if(exact&&Math.floor((exact+fpb/2)/fpb)===length&&Math.floor(exactStart/fpb)===get(meta,row+28+t*4)){frames=exact;start=exactStart}
 const content=get(meta,716+(s*4+t)*4),mode=meta[1008+s];
 // Mutable per-song/track configuration is retained exactly in the control
 // snapshot; these immutable headers capture the core take identity/geometry.
 const u16=o=>view(meta).getUint16(o,true),sum=(o,n)=>meta.subarray(o,o+n).reduce((a,v)=>(a+v)&65535,0);
 // Mirror maintained native validation, including GRD3/4 being gated by GRD2.
 const grid=1024,g2=grid+256,g3=grid+368,g4=grid+456,tr=1536+1040;
 const gridOK=get(meta,grid)===0x31445247 && Array.from({length:16},(_,i)=>u16(grid+4+i*2)).reduce((a,v)=>(a+v)&65535,0)===u16(grid+36);
 const g2ok=get(meta,g2)===0x32445247&&sum(g2+4,96)===u16(g2+100);
 const g3ok=g2ok&&get(meta,g3)===0x33445247&&sum(g3+4,80)===u16(g3+84);
 const g4ok=g2ok&&get(meta,g4)===0x34445247&&sum(g4+4,16)===u16(g4+20);
 const trimOK=get(meta,tr)===0x324d5254&&sum(tr+8,384)===u16(tr+6);
 const bpm=gridOK?u16(grid+4+s*2):0,beats=g3ok?meta[g3+4+s]:0;
 const origin=g3ok?get(meta,g3+20+s*4):0;
 const beatFrames=beats&&loop?Math.floor(loop/beats):bpm?Math.floor(48000*60*256/bpm):0;
 const preset=g2ok&&[1,2,4,8].includes(meta[g2+20+s])?meta[g2+20+s]:0;
 const bpb=g4ok&&[3,5,7].includes(meta[g4+4+s])?meta[g4+4+s]:0;
 const fields=[f.codec===6?2:1,48000,fpb,f.blocks,frames,content,start,loop,get(meta,row),
 trimOK?u16(tr+136+(s*4+t)*2):0,trimOK?u16(tr+264+(s*4+t)*2):0,
 trimOK?get(meta,tr+8+s*4):0,trimOK?get(meta,tr+72+s*4):0,
 origin,beatFrames,bpm,beats,bpb,meta[xe+14],(mode>>(4+t))&1,meta[976+s*2],meta[977+s*2],
 g2ok?meta[g2+36+s*4+t]:0,preset,g2ok?meta[g2+4+s]:0,0,meta[xe+13]&7,mode&15,length];
 if(!frames||length>f.blocks||content>f.blocks||fields[8]>131072||fields[20]>64||fields[21]>63||fields[27]>2)throw Error(`Unsupported musical metadata song=${f.song} track=${f.track} frames=${frames} capacity=${f.blocks*fpb} content=${content} speed=${fields[8]} chop=${fields[20]}/${fields[21]} mode=${fields[27]}`);
 b.set(enc.encode('SP1TAKE1'));put(b,8,2);fields.forEach((x,i)=>put(b,12+i*4,x));put(b,508,crc32(b.subarray(0,508)));return b;
}
export async function buildMusicImage(folder,destination,progress=()=>{},options={}){
 const m=await verifyBackup(folder),partitionFirst=options.partitionFirst??0;
 if(!Number.isSafeInteger(partitionFirst)||partitionFirst<0||partitionFirst>=m.capacity||partitionFirst%2048)throw Error('Invalid partition offset');
 if(partitionFirst&&partitionFirst<4096+64*86016)throw Error('Partition overlaps legacy music');
 const g=geometry(m.capacity-partitionFirst,options.sectorsPerCluster??64),metadata=new Uint8Array(await (await (await folder.getFileHandle('metadata.bin')).getFile()).arrayBuffer());
 const audioClusters=Math.floor((g.clusters-6)*.75),audioFirst=g.data+6*g.spc,audioBlocks=audioClusters*g.spc;
 if(audioBlocks*512>0xffffffff)throw Error('Audio pool exceeds FAT32 file limit');
 const objects=[];let cursor=0;
 for(const f of m.files.slice(1)){cursor+=((16-(audioFirst+cursor)%16)%16);const header=takeHeader(metadata,f);objects.push({f,first:cursor,blocks:f.blocks+16,tag:TAG,header});cursor+=f.blocks+16}
 cursor+=((16-(audioFirst+cursor)%16)%16);objects.push({first:cursor,blocks:6,tag:CONTROL});cursor+=6;
 if(cursor>audioBlocks)throw Error('Music does not fit in reserved native pool');
 const boot=new Uint8Array(512);boot.set([0xeb,0x58,0x90]);boot.set(enc.encode('SP1FS2  '),3);view(boot).setUint16(11,512,true);boot[13]=g.spc;view(boot).setUint16(14,g.reserved,true);boot[16]=2;boot[21]=0xf8;view(boot).setUint16(24,63,true);view(boot).setUint16(26,255,true);put(boot,28,partitionFirst);put(boot,32,g.blocks);put(boot,36,g.fat);put(boot,44,2);view(boot).setUint16(48,1,true);view(boot).setUint16(50,6,true);boot[64]=0x80;boot[66]=0x29;put(boot,67,0x53503102);boot.set(enc.encode('SP1 MUSIC  '),71);boot.set(enc.encode('FAT32   '),82);boot[510]=0x55;boot[511]=0xaa;
 const info=new Uint8Array(512);put(info,0,0x41615252);put(info,484,0x61417272);put(info,488,g.clusters-6-audioClusters);put(info,492,8+audioClusters);put(info,508,0xaa550000);
 const fat=new Uint8Array(g.fat*512);put(fat,0,0x0ffffff8);put(fat,4,0x0fffffff);for(let cl=2;cl<8;cl++)put(fat,cl*4,0x0fffffff);for(let cl=8;cl<8+audioClusters;cl++)put(fat,cl*4,cl+1===8+audioClusters?0x0fffffff:cl+1);
 const root=new Uint8Array(g.spc*512);root.set(dirEntry('SP1        ',16,3));root.set(dirEntry('IMPORTS    ',16,4),32);root.set(dirEntry('EXPORTS    ',16,5),64);
 const dirs=[3,4,5].map(cl=>{const b=new Uint8Array(g.spc*512);b.set(dirEntry('.          ',16,cl));b.set(dirEntry('..         ',16,0),32);return b});
 dirs[0].set(dirEntry('CAT0    BIN',0,6,4096),64);dirs[0].set(dirEntry('CAT1    BIN',0,7,4096),96);dirs[0].set(dirEntry('AUDIO   BIN',0,8,audioBlocks*512),128);
 const w=await destination.createWritable();const write=async(lba,bytes)=>{await w.seek((partitionFirst+lba)*512);await w.write(bytes)};
 try{
  await w.truncate(m.capacity*512);await write(0,boot);await write(1,info);await write(6,boot);await write(7,info);await write(g.reserved,fat);await write(g.reserved+g.fat,fat);await write(g.data,root);for(let i=0;i<3;i++)await write(g.data+(i+1)*g.spc,dirs[i]);
  const catalog=new Uint8Array(4096);catalog.set(enc.encode('SP1POOL2'));put(catalog,8,2);put(catalog,12,1);catalog.fill(255,2080,2336);
  for(let i=0;i<objects.length;i++){
   const o=objects[i];let bytes;if(o.f){const audio=new Uint8Array(await (await (await folder.getFileHandle(o.f.name)).getFile()).arrayBuffer());bytes=new Uint8Array(o.blocks*512);bytes.set(o.header);bytes.set(audio,8192);view(catalog).setUint16(2080+((o.f.song-1)*4+o.f.track-1)*2,i,true)}else{bytes=metadata.slice(0,3072);put(catalog,20,i+1)}
   put(catalog,32+i*16,o.first);put(catalog,36+i*16,o.blocks);put(catalog,40+i*16,crc32(bytes));put(catalog,44+i*16,o.tag);await write(audioFirst+o.first,bytes);progress({completed:i+1,total:objects.length});
  }
  put(catalog,4092,crc32(catalog.subarray(0,4092)));await write(g.data+4*g.spc,catalog);await write(g.data+5*g.spc,catalog);
  if(partitionFirst){
   const mbr=new Uint8Array(512);mbr.set(enc.encode('SP1TAIL3'),0);mbr[450]=0x0c;
   put(mbr,454,partitionFirst);put(mbr,458,g.blocks);mbr[510]=0x55;mbr[511]=0xaa;
   await w.seek(0);await w.write(mbr); /* Activation is the final emitted write. */
  }
  await w.close();
  return {schema:'sp1-pool2-fat32-image',identity:m.identity,blocks:m.capacity,partitionFirst,poolFirst:partitionFirst+audioFirst,poolBlocks:audioBlocks,usedBlocks:cursor,objects:objects.length,hardwareQualified:false};
 }catch(e){await w.abort().catch(()=>{});throw e}
}
