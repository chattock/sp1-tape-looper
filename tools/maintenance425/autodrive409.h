#include "../import504/crc.h"
/* Persistent gate binds a drive session to device, WORK and physical geometry.
 * Host never sees native FAT; native publication occurs once after eject. */
#define AUTO425_TAG 0x35323444u
static bool auto_mode,auto_return;
static unsigned auto_state,auto_error;
/* The index-active flag lives in the existing state word, outside reusable
 * decoder RAM. Input bytes cannot forge an active cache after invalidation. */
#define AUTO509_INDEX 0x100u
static unsigned auto_phase(void){return auto_state&255u;}
bool overlay509_enabled(void){return auto_phase()==3&&(auto_state&AUTO509_INDEX)&&!host_active;}
void overlay509_enable(bool active){if(!active)auto_state&=~AUTO509_INDEX;else if(auto_phase()==3&&!host_active)auto_state|=AUTO509_INDEX;}
void *overlay509_workspace(size_t*bytes){if(!auto_mode||auto_phase()!=3){*bytes=0;return NULL;}*bytes=sizeof(shared529.bytes)-8192;return shared529.bytes+8192;}
/* READY is entered only after converter admission. Read-only flash tables
 * also serve the exclusive host session; no runtime/decoder RAM is used. */
bool sp1_crc513_enabled(void){return auto_mode&&((auto_phase()==2&&host_active)||(auto_phase()==3&&!host_active));}
static bool offline507_active(void){return auto_mode&&auto_phase()==3&&!host_active;}
static uint8_t auto_hash[32];
static pool_io auto_raw;
static uint64_t auto_identity;
static uint32_t auto_work,auto_work_blocks;
static void auto_fail(unsigned why){auto_error=why;auto_state=4;}
static void auto_activity(void){uint32_t t=k_uptime_get_32();drive_keys_poll(t);if(drive_boot.pending){for(unsigned i=0;i<4;i++)lamp(i,(i==0||i==3)&&t%400<200);return;}/* A pulsing phase light distinguishes validation, planning, import and publication. */unsigned phase=editor.phase;for(unsigned i=0;i<4;i++)lamp(i,auto_phase()==3&&phase>=1&&phase<=4?(i==phase-1&&t%600<400):i==(t/150)%4);}
static bool edit_owned(void){const sp1_gate_record*r=(const sp1_gate_record*)gate_page;return sp1_gate_valid(r)&&get32(r->hash)==AUTO425_TAG&&get32(r->hash+4)==NRF_FICR->DEVICEID[0]&&get32(r->hash+8)==NRF_FICR->DEVICEID[1];}
static bool auto_locked(void*c,uint64_t identity,uint32_t first){ARG_UNUSED(c);return identity==auto_identity&&first==auto_work&&edit_owned()&&!memcmp(((const sp1_gate_record*)gate_page)->hash,auto_hash,32);}
static bool converter469_valid(void){
 const uint32_t*h=(const uint32_t*)0xb3c00u;
 return h[0]==0x36344d49u&&h[1]==0x00315639u&&h[2]==0xb4000u&&h[3]&&h[3]<=0x1a000u&&h[4]==~sp1_crc504_bootstrap(UINT32_MAX,(const uint8_t*)0xb4000u,h[3]);
}
static bool auto_geometry(void){
 if(!converter469_valid())return false;
 if(!sd_admit(&shared,drive_read,capacity)||shared.native_mask!=15||shared.native_bytes[0]!=4194304||shared.native_bytes[1]!=4194304||!shared.native_bytes[2]||shared.native_bytes[3]<67108864)return false;
 for(unsigned k=0;k<4;k++){uint32_t c=shared.native_start[k],n,count=0;do{if(!sd_next(&shared,c,&n))return false;count++;if(n>=DRIVE_EOC)break;if(n!=c+1)return false;c=n;}while(count<shared.end);
 if(count!=(shared.native_bytes[k]+(uint64_t)shared.spc*512-1)/(shared.spc*512))return false;}
 auto_raw=(pool_io){.read=library_rd,.write=library_wr,.sync=library_sync,.device_blocks=capacity};
 auto_raw.catalog[0]=sd_lba(&shared,shared.native_start[0]);auto_raw.catalog[1]=sd_lba(&shared,shared.native_start[1]);auto_raw.pool_first=sd_lba(&shared,shared.native_start[2]);auto_raw.pool_blocks=shared.native_bytes[2]/512;
 auto_work=sd_lba(&shared,shared.native_start[3]);auto_work_blocks=shared.native_bytes[3]/512;
 auto_identity=(uint64_t)NRF_FICR->DEVICEID[1]<<32|NRF_FICR->DEVICEID[0];
 uint32_t words[8]={AUTO425_TAG,NRF_FICR->DEVICEID[0],NRF_FICR->DEVICEID[1],auto_work,auto_work_blocks,auto_raw.pool_first,auto_raw.pool_blocks,0};
 uint32_t geo[]={capacity,shared.first,shared.blocks,shared.spc,auto_raw.catalog[0],auto_raw.catalog[1]};words[7]=~pool_crc(UINT32_MAX,geo,sizeof geo);memcpy(auto_hash,words,32);return true;
}
static void auto_complete(void){if(!persistent_unlock(auto_hash)){auto_fail(8);return;}auto_state=5;if(drive_boot.pending){drive_boot.pending=false;boot_requested=true;normal_requested=false;auto_return=false;}else{auto_return=true;normal_requested=true;}}
static uint32_t eject452_start[5],eject452_ms[5],eject452_calls[5];
static uint32_t eject449_marks[10],eject451_visit_start,eject451_visit_ms;
/*513: host-only timestamps reuse the existing phase array until commit starts.
 * No new static storage; media callbacks hold the ownership lock. */
void host513_phase(unsigned phase){
 if(!auto_mode||auto_phase()!=2||phase>6)return;
 uint32_t now=k_uptime_get_32();eject449_marks[phase]=now;
 if(phase==3&&now-eject449_marks[2]>eject449_marks[4])eject449_marks[4]=now-eject449_marks[2];
}
void eject449_phase(unsigned phase){uint32_t now=k_uptime_get_32();if(phase<10)eject449_marks[phase]=now;else if(phase==10)eject451_visit_start=now;else if(phase==11)eject451_visit_ms+=now-eject451_visit_start;else if(phase>=12&&phase<=21){unsigned i=(phase-12)/2;if(!(phase&1))eject452_start[i]=now;else{eject452_ms[i]+=now-eject452_start[i];eject452_calls[i]++;}}}
/* WORK prefix sector8 is outside arena headers0/1, saved roots2..5,
 * publication journal6/7 and all manifests starting at64. Status is never
 * authority for recovery. Failed write/readback retains the protective gate. */
static bool import458_save(unsigned code,const uint8_t*name){
 uint8_t b[512]={0},check[512];memcpy(b,"IMPORT458",9);memcpy(b+16,&auto_identity,8);memcpy(b+24,&paged_drive.arena.epoch,8);memcpy(b+32,&code,4);if(name)memcpy(b+36,name,12);uint32_t crc=~pool_crc(UINT32_MAX,b,508);memcpy(b+508,&crc,4);
 return library_wr(NULL,auto_work+8,b,1)&&library_sync(NULL)&&library_rd(NULL,auto_work+8,check,1)&&!memcmp(b,check,512);
}
#include "import_space571.h"
__attribute__((section(".fresh529.text.report571"),noinline))
void import458_report(uint32_t off,uint8_t*out,uint32_t len){
 char text[512]={0};uint8_t b[512];uint32_t code=0;
 if(auto_work&&library_rd(NULL,auto_work+8,b,1)&&!memcmp(b,"IMPORT458",9)&&!memcmp(b+16,&auto_identity,8)&&get32(b+508)==~pool_crc(UINT32_MAX,b,508)){
 code=get32(b+32);snprintf(text,sizeof text,"Device: %016llx\r\nDrive session: %llu\r\nResult session: %llu\r\nLast import result: %s\r\nReason code: %u\r\nFile (short name): %.11s\r\n%s\r\n",(unsigned long long)auto_identity,(unsigned long long)paged_drive.arena.epoch,(unsigned long long)(get32(b+24)|((uint64_t)get32(b+28)<<32)),code?"Changes rejected; original library verified":"Changes saved",code,b+36,code==1?"Unsupported file type. Prepare it with the website and copy again.":code==2?"Unsupported/damaged audio or decoder memory limit. Keep the computer original; prepare it with the website.":code==3?"Keep the original SONGS / SONG / TRACK / TAKES folders.":code?"Keep computer originals and review this import.":"Ready to play.");
 }else snprintf(text,sizeof text,"Device: %016llx\r\nDrive session: %llu\r\nNo completed import result recorded.\r\n",(unsigned long long)auto_identity,(unsigned long long)paged_drive.arena.epoch);
 unsigned native593_at=strlen(text);if(native593_at<sizeof text)snprintf(text+native593_at,sizeof text-native593_at,"Native import: p14s-v1\r\nLibrary generation: %llu\r\n",(unsigned long long)paged_drive.arena.generation);
 uint32_t capacity571=0,free571=0;if(staging571_space(b,&capacity571,&free571)){unsigned at=strlen(text);if(at<sizeof text)snprintf(text+at,sizeof text-at,"Source capacity bytes: %u\r\nSource free bytes: %u\r\n",capacity571,free571);}
 memset(out,0,len);if(off<sizeof text){uint32_t n=sizeof text-off;if(n>len)n=len;memcpy(out,text+off,n);}
}
#ifdef IMPORT469
__attribute__((section(".conv469.text.reject593"),noinline))
#endif
static bool auto_reject593(void){
 unsigned input=editor.discovery.input_error;
 if(!input||(editor.phase!=1&&editor.phase!=3))return false;
 /* No published target is discarded: discard independently verifies the old
  * roots/payloads and refuses a durable publication intent. I/O faults do not
  * set input_error and remain gated for inspection/recovery. */
 if(!editor422_discard(&editor,&paged_drive,&workspace)||!import458_save(input,editor.discovery.input_name)){auto_fail(32);return true;}
 auto_error=200+input;auto_complete();return true;
}
#ifdef IMPORT469
__attribute__((section(".conv469.text.autocheck507"),noinline))
#endif
static void auto_check_return(void){
 uint32_t host_stamps513[7];memcpy(host_stamps513,eject449_marks,sizeof host_stamps513);
 memset(eject452_ms,0,sizeof eject452_ms);memset(eject452_calls,0,sizeof eject452_calls);eject451_visit_ms=0;memset(eject449_marks,0,sizeof eject449_marks);eject449_marks[0]=k_uptime_get_32();auto_state=3;auto_error=0;host_read_calls=host_write_calls=host_read_blocks=host_write_blocks=host_sync_calls=0;host_read_us=host_write_us=read_time_us=0;if(host_active||host_failed||!library_sync(NULL)){auto_fail(6);return;}
 maintenance_progress=auto_activity;bool ok=editor422_commit(&editor,&paged_drive,&workspace);maintenance_progress=NULL;
 eject449_marks[7]=k_uptime_get_32();
 /* Emit only after commit, only to an attached diagnostic listener. No prints
  * or USB waits in validation, and no change to the return/recovery decision. */
 uint32_t listening=0;uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&listening);
 if(listening)reply("EJECT451 ms=%u,%u,%u,%u,%u,%u,%u,%u scan_begin=%u scan_end=%u ok=%u error=%u dir_hits=%u dir_misses=%u visitor_ms=%u\n",eject449_marks[0],eject449_marks[1],eject449_marks[2],eject449_marks[3],eject449_marks[4],eject449_marks[5],eject449_marks[6],eject449_marks[7],eject449_marks[8],eject449_marks[9],ok,editor.error,editor.discovery.directory_hits,editor.discovery.directory_misses,eject451_visit_ms);
 if(listening)reply("EJECT513 HOST last_read=%u last_write=%u sync_begin=%u sync_end=%u sync_max_ms=%u eject_begin=%u eject_end=%u commit_begin=%u\n",host_stamps513[0],host_stamps513[1],host_stamps513[2],host_stamps513[3],host_stamps513[4],host_stamps513[5],host_stamps513[6],eject449_marks[0]);
 if(listening)reply("EJECT509 INDEX entries=%u\n",overlay509_count(&paged_drive.overlay));
 if(listening)reply("EJECT507 IO read_calls=%u read_blocks=%u read_us=%llu write_calls=%u write_blocks=%u write_us=%llu sync_calls=%u sync_us=%llu\n",host_read_calls,host_read_blocks,(unsigned long long)host_read_us,host_write_calls,host_write_blocks,(unsigned long long)host_write_us,host_sync_calls,(unsigned long long)read_time_us);
 if(listening)reply("EJECT452 work_ms=%u,%u,%u,%u,%u calls=%u,%u,%u,%u,%u\n",eject452_ms[0],eject452_ms[1],eject452_ms[2],eject452_ms[3],eject452_ms[4],eject452_calls[0],eject452_calls[1],eject452_calls[2],eject452_calls[3],eject452_calls[4]);
#ifdef IMPORT469
 if(listening)maintenance472_stack_report();
#endif
 if(!ok){
  if(auto_reject593())return;
  auto_fail(100+editor.error);return;
 }
 if(!import458_save(0,NULL)){auto_fail(33);return;}auto_complete();
}
static void auto_recover(void){
 if(!auto_mode||host_active||!auto_locked(NULL,auto_identity,auto_work)||paged_drive.arena.state!=ARENA422_COMMITTING){return;}
 host_failed=false;maintenance_progress=auto_activity;bool ok=editor422_recover(&editor,&paged_drive,&workspace);maintenance_progress=NULL;
 if(!ok){if(auto_reject593())return;auto_fail(100+editor.error);return;}auto_complete();
}
static void auto_discard(void){
 if(!auto_mode||host_active||!auto_locked(NULL,auto_identity,auto_work)){return;}
 maintenance_progress=auto_activity;bool ok=editor422_discard(&editor,&paged_drive,&workspace);maintenance_progress=NULL;
 if(!ok){auto_fail(31);return;}host_failed=false;auto_complete();
}
static void auto_start(uint8_t marker){
 bool blocked=sp1_gate_blocked(gate_page),owned=edit_owned();if(marker!=0xa8&&marker!=0xaa&&!owned)return;
 auto_mode=true;auto_state=1;if(blocked&&!owned){auto_fail(1);return;}
 if(!identify()||!auto_geometry()){auto_fail(2);return;}
 editor471_checkpoint(100);
 if(owned&&memcmp(((const sp1_gate_record*)gate_page)->hash,auto_hash,32)){auto_fail(3);return;}
 if(!emmc_cache_enable()){auto_fail(5);return;}cache_on=true;
 if(!cat422_banks_open(&workspace.index,&workspace.checkpoint,&workspace.device,auto_raw)){auto_fail(10);return;}
 if(!owned&&(!write_admitted||!persistent_lock(auto_hash))){auto_fail(4);return;}
 maintenance_progress=auto_activity;
 bool prior=arena422_latest(&paged_drive.arena,auto_raw,auto_work,auto_work_blocks,auto_identity,auto_locked,NULL),ok=false;
 editor471_checkpoint(101);
 if(prior){unsigned state=paged_drive.arena.state;
  if(state==ARENA422_COMMITTED||state==ARENA422_ABORTED){
   if(owned){/* A retained gate may be from a new visit before arena renewal.
     * Terminal arenas have no pending host edits; verify the current,
     * non-older catalog before releasing that gate. */
    uint64_t g=workspace.index.generation,source=paged_drive.arena.generation;
    if(g<source){maintenance_progress=NULL;auto_fail(14);return;}
    if(!editor422_verify(&workspace,auto_raw,g)){maintenance_progress=NULL;auto_fail(15);return;}maintenance_progress=NULL;auto_complete();return;
   }
   ok=drive422_next(&paged_drive,&workspace);
  }else if(owned&&state==ARENA422_PREPARING){
   /* Preparation never exposes a host drive. Discard only after verifying
    * every original object and the exact source generation. No remount. */
   maintenance_progress=NULL;auto_discard();return;
  }else if(owned&&(state==ARENA422_READY||state==ARENA422_COMMITTING)){
   uint64_t gen=paged_drive.arena.generation;
   ok=drive422_reopen(&paged_drive,auto_raw,auto_work,auto_work_blocks,auto_identity,gen,auto_locked,NULL);
   /* A retained gate means the previous visit never completed. Keep its
    * overlay for explicit recovery/cancel; never automatically remount it. */
   if(ok){maintenance_progress=NULL;auto_fail(state==ARENA422_COMMITTING?20:34);return;}
  }
 }else if(!owned)ok=drive422_prepare(&paged_drive,&workspace,auto_raw,auto_work,auto_work_blocks,auto_identity,auto_locked,NULL);
 maintenance_progress=NULL;editor471_checkpoint(ok?102:103);if(!ok){auto_fail(12);return;}
 /* No host has seen this session yet. Finish preparation, then use the
  * normal verified discard path before honoring a queued bootloader exit. */
 if(drive_boot.pending){auto_discard();return;}
 exchange.first=0;exchange.blocks=paged_drive.view.sectors;exchange.data=paged_drive.view.data;
 memset(eject449_marks,0,sizeof eject449_marks);host_active=true;auto_state=2;
}
static void auto_poll(void){if(!auto_mode||auto_state!=2)return;k_mutex_lock(&media_lock,K_FOREVER);if(host_failed){/* host_status already reports NOMEDIA and all I/O refuses. */host_active=false;auto_fail(6);}else if(host_ejected&&!host_active)auto_check_return();k_mutex_unlock(&media_lock);}
