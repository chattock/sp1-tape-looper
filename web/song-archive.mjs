// Native song archive v1: immutable payload hashes, exact packed audio, selected song metadata.
const u16=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint16(o,true);
const u32=(b,o)=>new DataView(b.buffer,b.byteOffset,b.byteLength).getUint32(o,true);
const put16=(b,o,n)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint16(o,n,true);
const put32=(b,o,n)=>new DataView(b.buffer,b.byteOffset,b.byteLength).setUint32(o,n,true);
const sum=(b,a,z)=>b.slice(a,z).reduce((s,n)=>(s+n)&65535,0);
const b64=b=>{let s='';for(let i=0;i<b.length;i++)s+=String.fromCharCode(b[i]);return btoa(s);};
const un64=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
export async function digest(b){return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',b)),x=>x.toString(16).padStart(2,'0')).join('');}
export async function packSong(layout,slot,header,tracks){
 if(header.length!==3072||layout.numSlots!==16||layout.ntrk!==4)throw Error('Unsupported native song layout');
 const manifest={schema:1,slot,geometry:layout,header:b64(header),headerHash:await digest(header),tracks:[]};
 for(const b of tracks)manifest.tracks.push({bytes:b.length,sha256:await digest(b)});
 const json=new TextEncoder().encode(JSON.stringify(manifest)),prefix=new Uint8Array(12);prefix.set(new TextEncoder().encode('SPSONG01'));put32(prefix,8,json.length);
 return new Blob([prefix,json,...tracks],{type:'application/octet-stream'});
}
export async function unpackSong(blob,layout){
 if(blob.size>256*1024*1024||blob.size<12)throw Error('Invalid archive size');const b=new Uint8Array(await blob.arrayBuffer());
 if(new TextDecoder().decode(b.slice(0,8))!=='SPSONG01')throw Error('Not an SP-1 native song archive');const n=u32(b,8);if(n>16384||n+12>b.length)throw Error('Invalid archive header');
 const m=JSON.parse(new TextDecoder().decode(b.slice(12,12+n)));
 if(m.schema!==1||!Number.isInteger(m.slot)||m.slot<0||m.slot>=16||!Array.isArray(m.tracks)||m.tracks.length!==4)throw Error('Unsupported archive');
 for(const k of ['blockSize','numSlots','ntrk','trackBlocks','magic'])if(m.geometry?.[k]!==layout[k])throw Error('Archive geometry does not match this device');
 const header=un64(m.header);if(header.length!==3072||await digest(header)!==m.headerHash)throw Error('Archive metadata checksum failed');
 if(u32(header,0)!==layout.magic)throw Error('Archive index mismatch');let at=12+n;const tracks=[];
 for(const t of m.tracks){if(!Number.isInteger(t.bytes)||t.bytes<0||t.bytes%512||t.bytes>layout.trackBlocks*512||at+t.bytes>b.length)throw Error('Invalid track size');const data=b.slice(at,at+t.bytes);if(await digest(data)!==t.sha256)throw Error('Archive track checksum failed');tracks.push(data);at+=t.bytes;}
 if(at!==b.length)throw Error('Unexpected archive tail');
 const counts=songBlockCounts(header,m.slot,layout);for(let i=0;i<4;i++)if(counts[i]*512!==tracks[i].length)throw Error('Track payload does not match metadata');
 return {manifest:m,header,tracks};
}
export function songBlockCounts(header,slot,layout){
 const base=8+44*slot,content=716+16*slot,x=1536;
 if(layout.numSlots!==16||layout.blockSize!==512)throw Error('Unsupported metadata version');
 return Array.from({length:4},(_,i)=>{if(!header[base+8+i])return 0;const row=x+16+(slot*4+i)*16,codec=header[row+12];if(codec!==5&&codec!==6)throw Error('Native archive requires P14S/P16M tracks');
 const spb=codec===6?496:280,blocks=u32(header,row+8)||u32(header,content+4*i)||u32(header,base+12+4*i)||Math.ceil(u32(header,base+4)/spb);
 if(!blocks||blocks>layout.trackBlocks)throw Error('Invalid native track geometry');return blocks;});
}
// Copy selected song fields, leaving all other songs and global preferences unchanged.
export function mergeSongHeaders(current,source,from,to){
 if(current.length!==3072||source.length!==3072||![from,to].every(n=>Number.isInteger(n)&&n>=0&&n<16))throw Error('Invalid song metadata');
 for(let i=0;i<4;i++)if(current[8+44*to+8+i])throw Error('Restore requires an empty destination song');
 const out=current.slice(),copy=(base,stride,n)=>out.set(source.slice(base+from*stride,base+from*stride+n),base+to*stride);
 copy(8,44,44);copy(716,16,16);copy(976,2,2);copy(1008,1,1);
 if(!(out[1008+to]&15))out[1008+to]=(out[1008+to]&240)|(u32(source,712)?2:1);
 // GRD1 BPM uses a sum of uint16 values; later extensions sum bytes.
 const g=1024;function validGrid(b,base,magic,start,end,check,words=false){if(u32(b,base)!==magic)return false;let s=0;for(let i=start;i<end;i+=words?2:1)s=(s+(words?u16(b,i):b[i]))&65535;return s===u16(b,check);}
 const specs=[[g,0x31445247,g+4,g+36,g+36,true,[[g+4,2,2]]],[g+256,0x32445247,g+260,g+356,g+356,false,[[g+260,1,1],[g+276,1,1],[g+292,4,4]]],[g+368,0x33445247,g+372,g+452,g+452,false,[[g+372,1,1],[g+388,4,4]]],[g+456,0x34445247,g+460,g+476,g+476,false,[[g+460,1,1]]]];
 for(const [base,magic,start,end,check,words,fields] of specs){if(!validGrid(out,base,magic,start,end,check,words)){out.fill(0,start,check+2);put32(out,base,magic);}const valid=validGrid(source,base,magic,start,end,check,words);for(const [b,stride,n] of fields){if(valid)copy(b,stride,n);else out.fill(0,b+to*stride,b+to*stride+n);}let s=0;for(let i=start;i<end;i+=words?2:1)s=(s+(words?u16(out,i):out[i]))&65535;put16(out,check,s);}
 const x=1536;
 // Exact magic is checked from the destination/source equality to preserve compatible firmware spelling.
 if(u32(source,x)!==0x53453358||u32(current,x)!==0x53453358||u16(source,x+4)!==u16(current,x+4)||u16(source,x+6)!==sum(source,x+16,x+1040)||u16(current,x+6)!==sum(current,x+16,x+1040))throw Error('Invalid extended metadata');
 copy(x+16,64,64);put16(out,x+6,sum(out,x+16,x+1040));
 const tr=x+1040,validTrim=b=>u32(b,tr)===0x324d5254&&u16(b,tr+6)===sum(b,tr+8,tr+392);
 if(!validTrim(out)){out.fill(0,tr,tr+392);put32(out,tr,0x324d5254);put16(out,tr+4,2);}
 for(const [base,stride,n] of [[tr+8,4,4],[tr+72,4,4],[tr+136,8,8],[tr+264,8,8]]){if(validTrim(source))copy(base,stride,n);else out.fill(0,base+to*stride,base+to*stride+n);}
 put16(out,tr+6,sum(out,tr+8,tr+392));return out;
}
