#include "../exit524/mode_policy.h"
#include "../eject505/settle.h"
/* Generic maintenance qualification converted snapshot recovery and protected host exchange. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>
#include <sample_usbd.h>
#include <soc.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "sp1_emmc.h"
#include "../commit421/runtime/src/pool.h"
#include "recovery.h"
#include "protocol.h"
#include "write_gate.h"
#include <zephyr/drivers/flash.h>
#include "../storage-prototype/release377/boot_gate.h"
volatile uint8_t g_emmc_sprint,g_pb_on;
struct k_thread *g_str_tid;int g_pb_orig;volatile uint32_t g_pb_t0;
#include "command_frame590.h"
static uint8_t response[COMMAND590_BYTES] __aligned(4);
#define command590_read (response+COMMAND590_PREFIX)
_Static_assert(COMMAND590_PREFIX%4==0,"eMMC command scratch alignment");
static uint64_t read_time_us, crc_time_us, queue_time_us, read_payload_bytes;
static uint32_t read_calls;
K_SEM_DEFINE(rx_ready,0,1);
static const struct device *const cdc=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
RING_BUF_DECLARE(input,16384);
static volatile bool overflow;
static volatile uint32_t rx_bytes,rx_overflows;
static uint32_t stage,last_seq,last_lba,requests,tx_timeouts;
static uint32_t reset_reason;
/* One-shot compare-and-write lease. Cannot be reused, extended or used unlocked. */
static uint32_t lease_lba,lease_blocks,lease_crc;
static bool lease_active,shared_upload;
static struct k_spinlock tx_lock;
static const uint8_t *tx_data;
static unsigned tx_left;
static bool tx_failed;
K_SEM_DEFINE(tx_done,0,1);

static NRF_GPIO_Type *const lamps[]={NRF_P0,NRF_P0,NRF_P1,NRF_P1};
static const uint8_t pins[]={29,26,15,14};
static void lamp(unsigned i,bool on){if(on)lamps[i]->OUTSET=1u<<pins[i];else lamps[i]->OUTCLR=1u<<pins[i];}
static void(*maintenance_progress)(void);
static unsigned conversion469_phase;
static uint32_t conversion469_done,conversion469_total,conversion469_start,conversion469_ms;
static void feed(void){if(maintenance_progress)maintenance_progress();for(unsigned i=0;i<8;i++)NRF_WDT->RR[i]=WDT_RR_RR_Reload;}
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void receive(const struct device *dev,void *unused){
 ARG_UNUSED(unused);if(!uart_irq_update(dev))return;
 if(uart_irq_tx_ready(dev)){
  k_spinlock_key_t key=k_spin_lock(&tx_lock);
  if(tx_left){int n=uart_fifo_fill(dev,tx_data,tx_left);if(n>0){tx_data+=n;tx_left-=n;}}
  if(!tx_left){uart_irq_tx_disable(dev);k_sem_give(&tx_done);}
  k_spin_unlock(&tx_lock,key);
 }
 for(unsigned p=0;p<4&&uart_irq_rx_ready(dev);p++){uint8_t b[16];int n=uart_fifo_read(dev,b,sizeof(b));if(n<=0)break;rx_bytes+=n;k_sem_give(&rx_ready);if(ring_buf_put(&input,b,n)!=(unsigned)n){overflow=true;rx_overflows++;}}
}
/* Queued TX, never poll_out: CDC poll_out drops bytes when its FIFO is full.
 * Wait at most5seconds for a segment, feeding WDT and watching host presence.
 * USB callback is the sole FIFO writer; static buffers remain valid on failure. */
static bool send_bytes(const uint8_t *data,unsigned length){
 if(tx_failed)return false;
 k_sem_reset(&tx_done);
 k_spinlock_key_t key=k_spin_lock(&tx_lock);tx_data=data;tx_left=length;k_spin_unlock(&tx_lock,key);
 uart_irq_tx_enable(cdc);uint32_t start=k_uptime_get_32();
 for(;;){
  feed();if(!k_sem_take(&tx_done,K_MSEC(10)))return true;
  uint32_t dtr=0;uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&dtr);
  if(!dtr||k_uptime_get_32()-start>=5000){
   key=k_spin_lock(&tx_lock);uart_irq_tx_disable(cdc);tx_left=0;k_spin_unlock(&tx_lock,key);tx_failed=true;tx_timeouts++;return false;
  }
 }
}
static void reply(const char *fmt,...){
 static char text[512];va_list ap;va_start(ap,fmt);int n=vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
 if(n>0&&(unsigned)n<sizeof(text))send_bytes((const uint8_t *)text,n);else tx_failed=true;
}
static bool read_crc(uint32_t at,uint32_t n,uint32_t *crc){
 feed();uint32_t t=k_cycle_get_32();if(!emmc_read_blocks(at,command590_read,n))return false;read_time_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);t=k_cycle_get_32();*crc=~pool_crc(UINT32_MAX,command590_read,n*512);crc_time_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);feed();return true;
}
static uint32_t capacity;
static bool failed;
static bool browser_mode;
#include "drive_boot.h"
#include "../recovery578/exit_policy.h"
static drive_boot_gesture drive_boot;
static bool boot_requested,normal_requested,radio_requested;
static bool clean_mode;
static uint32_t original_zero_crc;
static struct write_gate upload;
static uint32_t writes_attempted,writes_verified;
static bool write_admitted,cache_on,recovery_only;
static const struct device *const gate_flash=DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
static const uint32_t *const gate_page=(const uint32_t *)SP1_GATE_ADDRESS;
static bool gate_hash(const char *text,uint8_t out[32]){
 for(unsigned i=0;i<8;i++){uint32_t x;if(!hex8(text+i*8,&x))return false;for(unsigned j=0;j<4;j++)out[i*4+j]=(uint8_t)(x>>(24-j*8));}return true;
}
static bool persistent_lock(const uint8_t hash[32]){
 sp1_gate_record r;sp1_gate_make(&r,hash);
 if(sp1_gate_valid((const sp1_gate_record*)gate_page))return !memcmp(((const sp1_gate_record*)gate_page)->hash,hash,32);
 if(!device_is_ready(gate_flash)||!sp1_gate_can_lock(gate_page,&r))return false;
 /* Marker first: reset at ANY later instruction routes to maintenance. */
 feed();if(flash_write(gate_flash,SP1_GATE_ADDRESS,&r.magic,4))return false;
 if(gate_page[0]!=r.magic)return false;
 if(flash_write(gate_flash,SP1_GATE_ADDRESS+4,(const uint8_t*)&r+4,sizeof(r)-4))return false;
 __DSB();return !memcmp(gate_page,&r,sizeof(r));
}
static bool persistent_unlock(const uint8_t hash[32]){
 const sp1_gate_record *r=(const sp1_gate_record*)gate_page;
 if(!sp1_gate_valid(r)||memcmp(r->hash,hash,32)||upload.active||failed)return false;
 if(cache_on&&!emmc_cache_flush())return false;
 feed();if(flash_erase(gate_flash,SP1_GATE_ADDRESS,SP1_GATE_BYTES))return false;
 __DSB();return !sp1_gate_blocked(gate_page);
}
/* Separate first-boot admission page, outside runtime/gate/selector regions. */
static bool onboard(void){
 const uint32_t expected[2]={0x53503149u,0xacafceb6u};
 const uint32_t *p=(const uint32_t*)0x7d000;
 if(!memcmp(p,expected,sizeof expected))return true;
 if((p[0]&expected[0])!=expected[0]||(p[1]&expected[1])!=expected[1])return false;
 if(flash_write(gate_flash,0x7d000,expected,sizeof expected))return false;
 return !memcmp(p,expected,sizeof expected);
}
static bool __attribute__((section(".fresh529.text.identify"))) identify(void){
 feed();if(!emmc_init()||!emmc_spim_active())return false;
 g_emmc_clk_half_us=0;feed();if(!emmc_read_ext_csd(command590_read))return false;
 capacity=get32(command590_read+212);
 /* Begin only from a cold cache-off C; host saves original data before enable. */
 write_admitted=command590_read[33]==0;
 if(capacity!=7733248u||!emmc_read_blocks(0,command590_read,1))write_admitted=false;
 recovery_only=get32(command590_read)!=0x53453341u&&memcmp(command590_read,"SP1TAIL3",8);
 return capacity==7733248u;
}
#include "exchange.h"
void __attribute__((section(".conv469.text.progress"))) wav_progress469(unsigned phase,uint32_t done,uint32_t total){
 if(phase!=conversion469_phase){uint32_t now=k_uptime_get_32();if(conversion469_phase)conversion469_ms+=now-conversion469_start;conversion469_start=now;}
 conversion469_phase=phase;conversion469_done=done;conversion469_total=total;feed();
}
void *wav_work469(size_t *bytes){overlay509_invalidate();*bytes=sizeof(shared);return &shared;}
void *wav_native_work509(size_t *bytes){*bytes=sizeof(shared);return &shared;} /* Exclusive after physical geometry snapshot; next admission rebuilds it. */
static void maintenance472_stack_report(void);
static void drive_keys_poll(uint32_t now);
#include "autodrive409.h"
#include "../import471/diagnostic.h"
#include "library462.h"
/* Hardware return is allowed only after the persistent recovery gate clears.
 * No direct host mount or unverified return through a button shortcut. */
static const struct device *const keys_adc=DEVICE_DT_GET(DT_NODELABEL(adc));
static bool keys_ready;static uint32_t keys_last,keys_since;static bool keys_held,keys_spent;
static void drive_keys_init(void){
 NRF_P1->OUTSET=1u<<10;NRF_P1->PIN_CNF[10]=1;
 const struct adc_channel_cfg cfg={.gain=ADC_GAIN_1_6,.reference=ADC_REF_INTERNAL,.acquisition_time=ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS,20),.channel_id=0,.input_positive=NRF_SAADC_AIN0};
 keys_ready=device_is_ready(keys_adc)&&adc_channel_setup(keys_adc,&cfg)==0;
}
static bool __attribute__((noinline,section(".fresh529.text.boot582"))) drive_boot_held582(void){
 if(!keys_ready)return false;int16_t raw=0;
 struct adc_sequence seq={.channels=1,.buffer=&raw,.buffer_size=sizeof raw,.resolution=12};
 return !adc_read(keys_adc,&seq)&&raw>=1256&&raw<1347&&(NRF_P0->IN&(1u<<27));
}
static void __attribute__((noinline,section(".fresh529.text.keys579"))) drive_keys_poll(uint32_t now){
 bool busy=auto_mode&&(auto_phase()>=1&&auto_phase()<=3);
 if(drive_boot.pending&&!busy&&!host_active&&!upload.active){drive_boot.pending=false;boot_requested=true;normal_requested=false;radio_requested=false;return;}
 if(!keys_ready||now-keys_last<50)return;keys_last=now;int16_t raw=0;
 struct adc_sequence seq={.channels=1,.buffer=&raw,.buffer_size=sizeof raw,.resolution=12};
 if(adc_read(keys_adc,&seq)){keys_held=false;drive_boot.held=false;return;}
 if(drive_boot_step(&drive_boot,now,raw,(NRF_P0->IN&(1u<<27))!=0,host_active||busy,upload.active)&&drive_boot_admit570(sp1_gate_blocked(gate_page),auto_mode,auto_phase()==4,host_active,upload.active)){drive_boot.pending=false;boot_requested=true;normal_requested=false;radio_requested=false;}
 bool chord=raw>=925&&raw<1044&&(NRF_P0->IN&(1u<<27));
 if(!chord){keys_held=false;if(raw>=0&&raw<110)keys_spent=false;return;}
 if(keys_spent)return;if(!keys_held){keys_held=true;keys_since=now;}
 lamp(1,(now/250)%2);lamp(2,(now/250)%2);
 if(now-keys_since>=3000){keys_spent=true;
  if(!busy&&!host_active&&!host_failed&&!upload.active&&!failed&&!sp1_gate_blocked(gate_page)){auto_return=auto_mode;normal_requested=true;}
 }
}
/* Main flash must diagnose a converter that has NOT passed admission. */
static void __attribute__((noinline,section(".fresh529.text.admission578"))) admission578_report(void){
 const uint32_t*h=(const uint32_t*)0xb3c00u;uint32_t actual=0;
 bool bounds=h[2]==0xb4000u&&h[3]&&h[3]<=0x1a000u;
 if(bounds)actual=~sp1_crc504_bootstrap(UINT32_MAX,(const uint8_t*)0xb4000u,h[3]);
 reply("SETUP578 CONVERTER magic=%08x version=%08x address=%08x bytes=%u expected=%08x actual=%08x bounds=%u valid=%u\n",h[0],h[1],h[2],h[3],h[4],actual,bounds,converter469_valid());
}
static bool __attribute__((noinline,section(".fresh529.text.exit578"))) exit578_command(const char*line,unsigned length){
 bool boot=length==9&&!memcmp(line,"S396BOOT!",9),normal=length==11&&!memcmp(line,"S396NORMAL!",11);
 if(!boot&&!normal)return false;
 bool busy=auto_mode&&(auto_phase()>=1&&auto_phase()<=3);
 if(!exit578_admit(boot,host_active,upload.active,busy,sp1_gate_blocked(gate_page),failed,host_failed)){
  reply("STORAGE578 EXIT deferred host=%u upload=%u busy=%u recovery=%u\n",host_active,upload.active,busy,sp1_gate_blocked(gate_page));return true;
 }
 boot_requested=boot;normal_requested=normal;radio_requested=false;auto_return=normal&&auto_mode;
 reply("STORAGE578 EXIT accepted destination=%s\n",boot?"bootloader":"looper");return true;
}
static void request_inner(const char *line,unsigned length){
 if(web_request(line,length))return;
 if(length==9&&!memcmp(line,"S578CAPS?",9)){admission578_report();return;}
 if(exit578_command(line,length))return;
 if(length==9&&!memcmp(line,"S471DIAG?",9)){diagnostic471_report();return;}
 if(length==9&&!memcmp(line,"S458CAPS?",9)){reply("IMPORT458 CAPS version=2 wav_pcm=8,16,24,32 wav_float=32,64 rates=8k-96k output_rate=48000 channels=2 aiff_pcm=1 mp3=1 flac=1 frames_exact=1 session_atomic=1 reject_return=1 result=1\n");return;}
 if(length==9&&!memcmp(line,"S470CAPS?",9)){reply("SETUP470 CAPS converter=469 verified=%u flow=postflash\n",converter469_valid());return;}
 if(length==9&&!memcmp(line,"S469CAPS?",9)){reply("IMPORT469 phase=%u frames=%u total=%u prepare_ms=%u converter=469 input=wav,aiff,mp3,flac output=48000-stereo-p14s\n",conversion469_phase,conversion469_done,conversion469_total,conversion469_ms);return;}
 if(length==11&&!memcmp(line,"S458RESULT?",11)){uint8_t text[512];import458_report(0,text,512);reply("IMPORT458 RESULT %s\n",text);return;}
 if(length==9&&!memcmp(line,"S411EDIT?",9)){reply("STORAGE411 EDIT phase=%u error=%u done=%u total=%u changes=%u overlay=%u/%u prepared=%u arena=%u editor_phase=%u files=%u\n",auto_state,editor.error,editor.done,editor.changes,editor.changes,paged_drive.overlay.next,paged_drive.overlay.limit,paged_drive.ready,paged_drive.arena.state,editor.phase,editor.discovery.files.count);return;}
 if(length==9&&!memcmp(line,"S411AUTO?",9)){reply("STORAGE411 AUTO state=%u error=%u gate=%u host=%u ejected=%u rejects=%u\n",auto_state,auto_error,sp1_gate_blocked(gate_page),host_active,host_ejected,host_rejects);return;}
 if(length==9&&!memcmp(line,"S425DROP!",9)){auto_discard();reply("STORAGE425 DISCARD state=%u error=%u\n",auto_state,auto_error);return;}
 if(length==9&&!memcmp(line,"S425RECO!",9)){auto_recover();reply("STORAGE425 RECOVERY state=%u error=%u\n",auto_state,auto_error);return;}
 /* Autonomous sessions never accept browser write/unlock/boot shortcuts. */
 if(auto_mode&&memchr(line,'!',length)){reply("STORAGE411 ERROR autonomous session; eject or inspect\n");return;}

 if(length==9&&!memcmp(line,"S396PERF?",9)){
  reply("STORAGE396 PERF calls=%u bytes=%llu read_us=%llu crc_us=%llu queue_us=%llu max_blocks=16\n",read_calls,(unsigned long long)read_payload_bytes,(unsigned long long)read_time_us,(unsigned long long)crc_time_us,(unsigned long long)queue_time_us);return;
 }

 if(length==9&&!memcmp(line,"S425CAPS?",9)){staging571_caps();return;}
 if(length==9&&!memcmp(line,"S411CAPS?",9)){reply("STORAGE411 DRIVE library-transactional-edit-v1\n");return;}
 if(length==9&&!memcmp(line,"S396HOST!",9)){
  if(host_active||host_ejected||failed||!cache_on||!exchange_geometry()){reply("STORAGE396 ERROR host admission\n");return;}
  host_active=true;reply("STORAGE396 HOST active\n");return;
 }
 if(length==9&&!memcmp(line,"S396HOST?",9)){reply("STORAGE396 HOST active=%u ejected=%u failed=%u\n",host_active,host_ejected,host_failed);return;}
 if(length==9&&!memcmp(line,"S396METR?",9)){
  k_mutex_lock(&media_lock,K_FOREVER);
  reply("STORAGE396 METR read_blocks=%u write_blocks=%u read_calls=%u write_calls=%u max_blocks=%u read_us=%llu write_us=%llu rejects=%u reject_lba=%u reject_offset=%u reject_region=%u sync_calls=%u sync_failures=%u eject_calls=%u\n",host_read_blocks,host_write_blocks,host_read_calls,host_write_calls,host_max_blocks,(unsigned long long)host_read_us,(unsigned long long)host_write_us,host_rejects,host_reject_lba,host_reject_offset,host_reject_region,host_sync_calls,host_sync_failures,host_eject_calls);
  k_mutex_unlock(&media_lock);return;
 }
 if(length==11&&!memcmp(line,"S396STATUS?",11)){
  reply("STORAGE396 STATUS ms=%u stage=%u seq=%u lba=%u requests=%u tx_left=%u tx_timeouts=%u rx_bytes=%u rx_overflows=%u failed=%u reset_reason=%u writes_attempted=%u writes_verified=%u patch=606\n",k_uptime_get_32(),stage,last_seq,last_lba,requests,tx_left,tx_timeouts,rx_bytes,rx_overflows,failed,reset_reason,writes_attempted,writes_verified);return;
 }
 if(host_active){reply("STORAGE396 ERROR host owns storage; eject first\n");return;}
 if(host_failed){failed=true;}
 if(length==9&&!memcmp(line,"S396GATE?",9)){
  const sp1_gate_record *r=(const sp1_gate_record*)gate_page;
  reply("STORAGE396 GATE blocked=%u valid=%u\n",sp1_gate_blocked(gate_page),sp1_gate_valid(r));return;
 }
 if(length==74&&(!memcmp(line,"S396LOCK! ",10)||!memcmp(line,"S396OPEN! ",10))){
  uint8_t hash[32];bool locking=line[4]=='L';
  if(!capacity||!write_admitted||upload.active||!gate_hash(line+10,hash)||
     !(locking?persistent_lock(hash):persistent_unlock(hash))){failed=true;reply("STORAGE396 ERROR gate\n");return;}
  if(locking)recovery_only=false;
  reply("STORAGE396 GATE %s\n",locking?"locked":"open");return;
 }


 if(length==19&&!memcmp(line,"S396CLEAN! ",11)){
  if(!write_admitted||upload.active||!sp1_gate_valid((const sp1_gate_record*)gate_page)||!hex8(line+11,&original_zero_crc))goto write_error;
  clean_mode=true;recovery_only=false;reply("STORAGE396 CLEAN admitted\n");return;
 }
 if(length==12&&!memcmp(line,"S396ONBOARD!",12)){
  if(!capacity||failed||upload.active||sp1_gate_blocked(gate_page)||!onboard())goto write_error;
  reply("STORAGE396 ONBOARD accepted\n");return;
 }
 if(length==10&&!memcmp(line,"S396RADIO!",10)){
  if(failed||upload.active||sp1_gate_blocked(gate_page))goto write_error;
  radio_requested=true;reply("STORAGE396 RADIO accepted\n");return;
 }
 if(length==11&&!memcmp(line,"S396NORMAL!",11)&&!upload.active&&!failed&&!sp1_gate_blocked(gate_page)){normal_requested=true;reply("STORAGE396 NORMAL accepted\n");return;}
 if(storage_boot_command(line,length)){boot_requested=true;reply("STORAGE396 BOOT accepted\n");return;}
 if(failed){reply("STORAGE396 ERROR latched\n");return;}
 if(length==10&&!memcmp(line,"S396HELLO?",10)){
  stage=1;if(!capacity&&!identify()){failed=true;reply("STORAGE396 ERROR init\n");return;}
  stage=0;reply("STORAGE396 ID identity=%08x%08x blocks=%u read_only=0\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0],capacity);return;
 }
 /* Cache enable is explicit and precedes the bounded rewrite. */
 if(length==12&&!memcmp(line,"S396RECOVER!",12)){recovery_only=true;reply("STORAGE396 RECOVERY only_sector0\n");return;}
 if(length==12&&!memcmp(line,"S396CACHEON!",12)){
  if(!write_admitted||upload.active||!sp1_gate_valid((const sp1_gate_record*)gate_page))goto write_error;
  if(cache_on){reply("STORAGE396 CACHE enabled\n");return;}
  feed();if(!emmc_cache_enable()){failed=true;reply("STORAGE396 ERROR cache\n");return;}
  cache_on=true;reply("STORAGE396 CACHE enabled\n");return;
 }
 if(length==10&&!memcmp(line,"S396FLUSH!",10)){
  if(!cache_on||upload.active)goto write_error;
  feed();uint32_t start=k_cycle_get_32();bool ok=emmc_cache_flush();uint32_t us=k_cyc_to_us_floor32(k_cycle_get_32()-start);feed();
  if(!ok)failed=true;reply("STORAGE396 FLUSH ok=%u us=%u\n",ok,us);return;
 }
 if(length==40&&!memcmp(line,"L396 ",5)){
  uint32_t lba,n,before,after;
  if(failed||upload.active||lease_active||!cache_on||!write_admitted||!sp1_gate_valid((const sp1_gate_record*)gate_page)||
     line[13]!=' '||line[22]!=' '||line[31]!=' '||!hex8(line+5,&lba)||!hex8(line+14,&n)||!hex8(line+23,&before)||!hex8(line+32,&after)||
     !n||n>16||lba>=capacity||n>capacity-lba)goto write_error;
  feed();if(!emmc_read_blocks(lba,command590_read,n)||~pool_crc(UINT32_MAX,command590_read,n*512)!=before)goto write_error;
  lease_lba=lba;lease_blocks=n;lease_crc=after;lease_active=true;
  reply("STORAGE396 LEASE admitted\n");return;
 }
 if(length==40&&!memcmp(line,"P396 ",5)){
  uint32_t seq,lba,crc,blocks;
  if(line[13]!=' '||line[22]!=' '||!hex8(line+5,&seq)||!hex8(line+14,&lba)||!hex8(line+23,&crc)||line[31]!=' '||!hex8(line+32,&blocks)||!capacity)goto write_error;
  shared_upload=false;
  if(lease_active){
   if(!seq||upload.active||lba!=lease_lba||blocks!=lease_blocks||crc!=lease_crc||!sp1_gate_valid((const sp1_gate_record*)gate_page))goto write_error;
   upload.seq=seq;upload.lba=lba;upload.crc=crc;upload.blocks=blocks;upload.filled=0;upload.active=true;
   lease_active=false;shared_upload=true;
  }else if(!gate_begin(&upload,seq,lba,crc,blocks,write_admitted&&cache_on&&sp1_gate_valid((const sp1_gate_record*)gate_page)&&(!recovery_only||lba==0)))goto write_error;
  reply("STORAGE396 UPLOAD seq=%08x\n",seq);return;
 }
 if(length==13&&!memcmp(line,"C396 ",5)){
  uint32_t seq;if(!hex8(line+5,&seq)||!gate_ready(&upload,seq,~pool_crc(UINT32_MAX,upload.bytes,upload.blocks*512))||!write_admitted||!sp1_gate_valid((const sp1_gate_record*)gate_page))goto write_error;
  if(upload.lba==0&&!shared_upload){
   bool legacy=get32(upload.bytes)==0x53453341u;
   bool tail=!memcmp(upload.bytes,"SP1TAIL3",8)&&upload.bytes[450]==12&&get32(upload.bytes+454)==5509120u&&get32(upload.bytes+458)==2224128u&&upload.bytes[510]==85&&upload.bytes[511]==170;
   if(!legacy&&!tail&&!(clean_mode&&~pool_crc(UINT32_MAX,upload.bytes,512)==original_zero_crc))goto write_error;
  }
  /* Consume authorization BEFORE a media call: duplicate commits cannot repeat it. */
  upload.active=false;stage=8;writes_attempted++;feed();uint32_t started=k_cycle_get_32();
  if(!emmc_write_blocks(upload.lba,upload.bytes,upload.blocks)){failed=true;stage=10;reply("STORAGE396 ERROR write\n");return;}
  uint32_t write_us=k_cyc_to_us_floor32(k_cycle_get_32()-started);stage=9;feed();if(!emmc_read_blocks(upload.lba,command590_read,upload.blocks)||memcmp(command590_read,upload.bytes,upload.blocks*512)){failed=true;stage=10;reply("STORAGE396 ERROR readback\n");return;}
  writes_verified++;stage=0;reply("STORAGE396 WRITTEN seq=%08x lba=%u blocks=%u us=%u\n",seq,upload.lba,upload.blocks,write_us);return;
 }
 if(upload.active)goto write_error;
 uint32_t seq,lba,n,crc;
 if(!read_request(line,length,capacity,&seq,&lba,&n)){failed=true;reply("STORAGE396 ERROR request\n");return;}
 last_seq=seq;last_lba=lba;requests++;stage=2;
 if(!read_crc(lba,n,&crc)){stage=7;failed=true;reply("STORAGE396 ERROR read\n");return;}
 stage=3;
 size_t frame=command590_frame(response,false,seq,lba,n,crc);
 if(!frame){failed=true;return;}
 stage=4;uint32_t t=k_cycle_get_32();
 if(!send_bytes(response,frame)){stage=6;failed=true;return;}
 queue_time_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);read_payload_bytes+=n*512;read_calls++;stage=0;return;

write_error: lease_active=false;shared_upload=false;upload.active=false;failed=true;reply("STORAGE396 ERROR write protocol\n");
}
static void request(const char *line,unsigned length){k_mutex_lock(&media_lock,K_FOREVER);request_inner(line,length);k_mutex_unlock(&media_lock);}
#include "../fresh529/adapter.inc"
int main(void){
 uint8_t entry_marker=NRF_POWER->GPREGRET2;NRF_POWER->GPREGRET2=setup524_storage_entry(entry_marker);browser_mode=entry_marker==0xaau;web_resume=browser_mode&&sp1_gate_blocked(gate_page);
 reset_reason=NRF_POWER->RESETREAS;NRF_POWER->RESETREAS=reset_reason;
 for(unsigned i=0;i<4;i++){lamp(i,false);lamps[i]->PIN_CNF[pins[i]]=1;}
 lamp(0,true);NRF_P0->PIN_CNF[27]=GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos;
 NRF_P0->OUTCLR=1u<<21;NRF_P0->PIN_CNF[21]=1;
 const struct device *wdt=DEVICE_DT_GET(DT_NODELABEL(wdt0));
 if(device_is_ready(wdt)){wdt_install_timeout(wdt,&(struct wdt_timeout_cfg){.window.max=12000});wdt_setup(wdt,0);}
 for(unsigned i=0;i<50;i++){feed();k_msleep(10);}
 if(fresh529_boot()<0)failed=true;
 drive_keys_init();
 int disk_error=disk_access_register(&host_disk);
 struct usbd_context *usb=disk_error?NULL:sample_usbd_init_device(NULL);int error=usb?0:-1;
 if(usb){usbd_device_set_bcd_device(usb,0x0396);if(browser_mode){usbd_device_set_vid(usb,0x2fe3);usbd_device_set_pid(usb,0x5210);}error=usbd_enable(usb);}
 if(!error){error=uart_irq_callback_user_data_set(cdc,receive,NULL);if(!error)uart_irq_rx_enable(cdc);}
 lamp(1,error==0);bool released=false,held=false,ready=false;uint32_t since=0,host_since=0;
 char line[96];unsigned used=0;
 if(!error&&!failed)auto_start(entry_marker);
 for(;;){
  feed();uint32_t now=k_uptime_get_32(),dtr=0;uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&dtr);
  lamp(0,now%1000<500);lamp(3,error||overflow||failed||tx_failed);
  if(!dtr){lease_active=false;if(upload.active){upload.active=false;failed=true;}host_since=0;ready=false;used=0;tx_failed=false;}else if(!host_since)host_since=now;
  else if(!ready&&now-host_since>=250){ready=true;reply("STORAGE396 READY read_only=0\n");}
  uint8_t b;unsigned count=0;while(count++<1024&&ring_buf_get(&input,&b,1)){
   if(overflow){failed=true;upload.active=false;continue;}
   if(!ready||error)continue;
   if(upload.active&&upload.filled<upload.blocks*512){
    if(!gate_data(&upload,upload.seq,upload.filled,&b,1)){failed=true;upload.active=false;}
    else if(upload.filled==upload.blocks*512)reply(web_upload?"LIBRARY462 PAYLOAD seq=%08x\n":"STORAGE396 PAYLOAD seq=%08x\n",upload.seq);
    continue;
   }
   if(b=='\n'){request(line,used);used=0;lamp(2,capacity&&!failed);}
   else if(b!='\r'){if(used<sizeof(line))line[used++]=(char)b;else{overflow=true;failed=true;}}

  }
  drive_keys_poll(now);
  if(!boot_requested)auto_poll();
  if(auto_mode){bool pulse=(now%1000<120)||(now%1000>=240&&now%1000<360);for(unsigned i=0;i<4;i++)lamp(i,auto_state==4?pulse:(i==1||i==2)?true:(auto_state==1?((now/250)%2):false));}
  if(drive_boot.pending&&!boot_requested){lamp(0,(now/200)%2);lamp(3,(now/200)%2);lamp(1,false);lamp(2,false);}
  if((boot_requested||normal_requested||radio_requested)&&cache_on){feed();if(!emmc_cache_flush()){boot_requested=false;normal_requested=false;radio_requested=false;failed=true;if(auto_mode)auto_fail(9);reply("STORAGE396 ERROR shutdown flush\n");}else cache_on=false;}
  /* A queued physical request must still be held through protected boot scan.
   * If released, finish a verified normal return or remain in recovery. */
  if(boot_requested&&drive_boot.spent&&!drive_boot_held582()){
   boot_requested=false;drive_boot.pending=false;
   if(auto_mode&&auto_phase()==5&&!failed&&!host_failed&&!host_active&&!upload.active&&!sp1_gate_blocked(gate_page)){auto_return=true;normal_requested=true;}
  }
  if(boot_requested||normal_requested||radio_requested){
   /*505: Finder has relinquished ownership and native completion/gate
    * release are durable. Keep the long ACK window for serial/setup paths. */
   uint32_t dtr505=1;uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&dtr505);
   unsigned settle505=eject505_settle_ms(auto_mode&&auto_return&&auto_state==5,normal_requested,host_ejected,host_active,upload.active,dtr505!=0,tx_left!=0,tx_failed);
   for(unsigned i=0;i<settle505/10;i++){feed();k_msleep(10);}
   if(usb)usbd_disable(usb);emmc_power_down();
   for(unsigned i=0;i<4;i++)lamp(i,true);
   NRF_POWER->RESETREAS=0xffffffffu;NRF_POWER->GPREGRET=(normal_requested||radio_requested)?0:0x57u;
   if(normal_requested)NRF_POWER->GPREGRET2=setup524_return(NRF_POWER->GPREGRET2,false,false); /*524: awake unless radio recovery remains. */
   if(radio_requested)NRF_POWER->GPREGRET2=setup524_return(NRF_POWER->GPREGRET2,true,false);
   if(boot_requested)NRF_POWER->GPREGRET2=setup524_return(NRF_POWER->GPREGRET2,false,true); /* recovery remains intact */
   __DSB();NVIC_SystemReset();for(;;){}
  }
  bool pressed=!(NRF_P0->IN&(1u<<27));
  if(!pressed){released=true;held=false;}else if(released){if(!held){held=true;since=now;}if(now-since>=2000){if(host_active&&!host_failed){released=false;continue;}if(host_failed)host_active=false;if(cache_on){feed();if(!emmc_cache_flush()){failed=true;released=false;continue;}cache_on=false;}if(usb)usbd_disable(usb);emmc_power_down();for(unsigned i=0;i<4;i++)lamp(i,false);
 /* Wait for FN release before arming active-low wake, otherwise the held
  * shutdown button immediately wakes us back into the application. */
 while(!(NRF_P0->IN&(1u<<27))){feed();k_msleep(20);}
 k_msleep(60);
 NRF_P0->PIN_CNF[27]=(GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos)|(GPIO_PIN_CNF_SENSE_Low<<GPIO_PIN_CNF_SENSE_Pos);NRF_POWER->RESETREAS=0xffffffffu;__DSB();NRF_POWER->SYSTEMOFF=1;__DSB();}}
  if(!ring_buf_size_get(&input))k_sem_take(&rx_ready,K_MSEC(10));
 }
}
