/* Browser owns the synthetic drive exclusively. No raw-media write command is
 * reachable here; all writes use the existing bounded WORK overlay. */
static bool web_resume;
static uint32_t web_seq;
static bool web_upload;
static bool web_ready(void){return browser_mode&&auto_mode&&auto_state==2&&host_active&&!host_failed&&!failed&&!tx_failed&&paged_drive.ready;}
static void web_error(void){host_failed=true;web_upload=false;upload.active=false;reply("LIBRARY462 ERROR session blocked\n");}
static bool web_request(const char*line,unsigned length){
 if(length==10&&!memcmp(line,"A462HELLO?",10)){reply("LIBRARY462 HELLO mode=%s identity=%016llx\n",browser_mode?"library":"maintenance",(unsigned long long)((uint64_t)NRF_FICR->DEVICEID[1]<<32|NRF_FICR->DEVICEID[0]));return true;}
 if(length==9&&!memcmp(line,"S462CAPS?",9)){reply("LIBRARY462 CAPS version=1 browser=%u resume=%u identity=%016llx epoch=%llu sectors=%u max_blocks=16 state=%u\n",browser_mode,web_resume||web_seq!=0,(unsigned long long)auto_identity,(unsigned long long)paged_drive.arena.epoch,paged_drive.view.sectors,auto_state);return true;}
 if(!browser_mode)return false;
 if(length==9&&!memcmp(line,"S462SAVE!",9)){
  if(!web_ready()||web_resume||upload.active){web_error();return true;}
  host_active=false;host_ejected=true;auto_check_return();reply("LIBRARY462 SAVED state=%u reason=%u epoch=%llu\n",auto_state,auto_error,(unsigned long long)paged_drive.arena.epoch);return true;
 }
 if(length==9&&!memcmp(line,"S462DROP!",9)){
  if(!auto_mode||upload.active||auto_phase()==3){web_error();return true;}
  host_active=false;maintenance_progress=auto_activity;bool ok=editor422_discard(&editor,&paged_drive,&workspace);maintenance_progress=NULL;if(!ok)auto_fail(31);else{host_failed=false;failed=false;auto_error=0;auto_complete();}reply("LIBRARY462 DROPPED state=%u reason=%u\n",auto_state,auto_error);return true;
 }
 if(length<5||(memcmp(line,"R462 ",5)&&memcmp(line,"P462 ",5)&&memcmp(line,"C462 ",5))){reply("LIBRARY462 ERROR use library controls\n");return true;}
 if(!web_ready()||web_resume){web_error();return true;}
 if(length==13&&!memcmp(line,"C462 ",5)){
  uint32_t seq;if(!web_upload||!hex8(line+5,&seq)||!gate_ready(&upload,seq,~pool_crc(UINT32_MAX,upload.bytes,upload.blocks*512))){web_error();return true;}
  upload.active=false;web_upload=false;
  if(!drive422_write(&paged_drive,upload.lba,upload.bytes,upload.blocks)||!drive422_read(&paged_drive,upload.lba,command590_read,upload.blocks)||memcmp(command590_read,upload.bytes,upload.blocks*512)){web_error();return true;}
  reply("LIBRARY462 WRITTEN seq=%08x\n",seq);return true;
 }
 if(upload.active){web_error();return true;}
 uint32_t seq,lba,n,crc;
 bool rd=length==31&&!memcmp(line,"R462 ",5),wr=length==40&&!memcmp(line,"P462 ",5);
 if((!rd&&!wr)||line[13]!=' '||line[22]!=' '||!hex8(line+5,&seq)||!hex8(line+14,&lba)||seq<=web_seq){web_error();return true;}
 if(rd){if(!hex8(line+23,&n)) {web_error();return true;}}
 else if(line[31]!=' '||!hex8(line+23,&crc)||!hex8(line+32,&n)){web_error();return true;}
 if(!n||n>16||lba>=paged_drive.view.sectors||n>paged_drive.view.sectors-lba){web_error();return true;}web_seq=seq;
 if(wr){upload.seq=seq;upload.lba=lba;upload.blocks=n;upload.crc=crc;upload.filled=0;upload.active=true;web_upload=true;reply("LIBRARY462 UPLOAD seq=%08x\n",seq);return true;}
 if(!drive422_read(&paged_drive,lba,command590_read,n)){web_error();return true;}
 crc=~pool_crc(UINT32_MAX,command590_read,n*512);size_t frame=command590_frame(response,true,seq,lba,n,crc);
 if(!frame){web_error();return true;}
 if(!send_bytes(response,frame))host_failed=true;return true;
}
