static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
bool wav_appledouble(wav_source s){
 uint8_t h[26],entries[16][12];
 if(!read_at(s,0,h,26)||be32(h)!=0x00051607||be32(h+4)!=0x00020000)return false;
 unsigned n=(unsigned)h[24]*256+h[25];if(!n||n>16||!read_at(s,26,entries,n*12))return false;
 for(unsigned i=0;i<n;i++){
  uint32_t id=be32(entries[i]),off=be32(entries[i]+4),len=be32(entries[i]+8);
  if(!id||off<26+n*12||off>s.bytes||len>s.bytes-off)return false;
  for(unsigned j=0;j<i;j++){uint32_t a=be32(entries[j]+4),z=be32(entries[j]+8);if(id==be32(entries[j])||(len&&z&&off<a+z&&a<off+len))return false;}
 }return true;
}
