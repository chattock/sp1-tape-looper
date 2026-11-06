#include <zephyr/sys/reboot.h>
/* USB-only Reusable bench sector qualification. No audio/eMMC. Visible startup. */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/sys/ring_buffer.h>
#include <sample_usbd.h>
#include <soc.h>
#include "sp1_bt_probe.h"
#include "startup.h"
#include "exit524.h"
#include "../../recovery579/radio_keys.h"
#include "../../exit524/mode_policy.h"
#include "gate607.h"
#define EXIT524 __attribute__((section(".exit524.text"),noinline))
static const char source_capability565[] __attribute__((section(".exit524.text.feldd565_cap")))="BTID565 SOURCE feldd565-v1 protected=1 vs=append-only\n";
/* Startup-only retained breadcrumbs: never overwrite radio recovery authority.
 * No mutable RAM, IRQ, thread or playback change. Cleared before host commands. */
static void startup551_stamp(uint8_t stage){if(NRF_POWER->GPREGRET2!=0xa5u&&NRF_POWER->GPREGRET2!=0xa4u&&!clean607_pending()){NRF_POWER->GPREGRET2=stage;__DSB();}}
static int startup551_early(void){
 /* Reload an inherited running watchdog before cold setup driver init.
  * Never enable/disable it or change its window; bounded startup still resets
  * if a later operation stalls. Recovery authority remains untouched. */
 if(NRF_WDT->RUNSTATUS)for(unsigned i=0;i<8;i++)NRF_WDT->RR[i]=WDT_RR_RR_Reload;
 startup551_stamp(0xb0u);return 0;
}
SYS_INIT(startup551_early,PRE_KERNEL_1,0);
static const struct device *const cdc=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
RING_BUF_DECLARE(input,2048);
static volatile bool overflow;
static struct setup_power power_state;
static NRF_GPIO_Type *const lamps[]={NRF_P0,NRF_P0,NRF_P1,NRF_P1};
static const uint8_t pins[]={29,26,15,14};
static void lamp(unsigned i,bool on){if(on)lamps[i]->OUTSET=1u<<pins[i];else lamps[i]->OUTCLR=1u<<pins[i];}
static void feed(void){for(unsigned i=0;i<8;i++)NRF_WDT->RR[i]=WDT_RR_RR_Reload;}
static void receive(const struct device *dev,void *unused){
 ARG_UNUSED(unused);if(!uart_irq_update(dev))return;
 for(unsigned pass=0;pass<4&&uart_irq_rx_ready(dev);pass++){
  uint8_t b[16];int n=uart_fifo_read(dev,b,sizeof b);if(n<=0)break;
  if(ring_buf_put(&input,b,n)!=(unsigned)n)overflow=true;
 }
}
extern bool sp1_setup_return_safe(void);
static EXIT524 void off(void){
 /* Idle shutdown must not leave the next boot trapped in setup. */
 if(sp1_setup_return_safe())NRF_POWER->GPREGRET2=0;
 __DSB();
 sp1_bt_probe_stop();for(unsigned i=0;i<4;i++)lamp(i,false);
 NRF_P0->PIN_CNF[27]=(GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos)|(GPIO_PIN_CNF_SENSE_Low<<GPIO_PIN_CNF_SENSE_Pos);
 NRF_POWER->RESETREAS=0xffffffffu;__DSB();NRF_POWER->SYSTEMOFF=1;__DSB();
 for(;;)k_msleep(100);
}
extern bool sp1_setup_transaction_active(void);
static unsigned exit_match;
static const char exit_command[]="A336EXIT?";
/* A retained recovery marker survives ordinary reset and FN power-off.
 * It is not a replacement for the host journal after complete power loss. */
EXIT524 bool sp1_setup_recovery_required(void){return NRF_POWER->GPREGRET2==0xa5u||clean607_pending();}
EXIT524 void sp1_setup_write_begin(void){NRF_POWER->GPREGRET2=clean607_pending()?0xa4u:0xa5u;__DSB();}
EXIT524 void sp1_setup_write_verified(void){NRF_POWER->GPREGRET2=0;__DSB();}
EXIT524 void sp1_setup_consume_entry(void){NRF_POWER->GPREGRET2=setup524_radio_entry(NRF_POWER->GPREGRET2);__DSB();}
static EXIT524 bool keys524_wait(volatile uint32_t*event){
 uint32_t start=k_cycle_get_32();while(!*event)if((uint32_t)(k_cycle_get_32()-start)>k_us_to_cyc_ceil32(2000))return false;return true;
}
/* Setup owns SAADC exclusively; no ADC driver, IRQ, thread or retained buffer.
 * AIN0 and gain/reference/acquisition match the existing maintenance ladder. */
static EXIT524 bool keys524_read(int*raw){
 uint32_t sample=0;bool ok=false;
 NRF_SAADC->ENABLE=1;NRF_SAADC->INTENCLR=0xffffffffu;
 NRF_SAADC->RESOLUTION=SAADC_RESOLUTION_VAL_12bit;NRF_SAADC->OVERSAMPLE=0;NRF_SAADC->SAMPLERATE=0;
 for(unsigned i=1;i<8;i++){NRF_SAADC->CH[i].PSELP=0;NRF_SAADC->CH[i].PSELN=0;}
 NRF_SAADC->CH[0].PSELP=SAADC_CH_PSELP_PSELP_AnalogInput0;NRF_SAADC->CH[0].PSELN=0;
 NRF_SAADC->CH[0].CONFIG=(SAADC_CH_CONFIG_GAIN_Gain1_6<<SAADC_CH_CONFIG_GAIN_Pos)|(SAADC_CH_CONFIG_REFSEL_Internal<<SAADC_CH_CONFIG_REFSEL_Pos)|(SAADC_CH_CONFIG_TACQ_20us<<SAADC_CH_CONFIG_TACQ_Pos);
 NRF_SAADC->RESULT.PTR=(uint32_t)&sample;NRF_SAADC->RESULT.MAXCNT=1;
 NRF_SAADC->EVENTS_STARTED=NRF_SAADC->EVENTS_END=NRF_SAADC->EVENTS_STOPPED=0;
 NRF_SAADC->TASKS_START=1;
 if(keys524_wait(&NRF_SAADC->EVENTS_STARTED)){NRF_SAADC->TASKS_SAMPLE=1;ok=keys524_wait(&NRF_SAADC->EVENTS_END)&&NRF_SAADC->RESULT.AMOUNT==1;}
 NRF_SAADC->TASKS_STOP=1;ok=keys524_wait(&NRF_SAADC->EVENTS_STOPPED)&&ok;NRF_SAADC->ENABLE=0;
 if(ok){*raw=(int16_t)sample;ok=*raw>=-16&&*raw<=4095;}return ok;
}
static const char keys580_format[] __attribute__((section(".exit524.text.keys580_format")))="BTID580 KEYS raw=%d valid=%u fn_released=%u safe=%u active=%u spent=%u kind=%u pending=%u\n";
static EXIT524 void keys580_report(const radio_keys579*keys,bool fn_released){
 int raw=-1;bool valid=keys524_read(&raw);
 printk(keys580_format,raw,valid,fn_released,sp1_setup_return_safe(),sp1_setup_transaction_active(),keys->spent,keys->kind,keys->pending);
}
EXIT524 int main(void){
 startup551_stamp(0xb1u);
 sp1_setup_consume_entry(); /* Setup request is one-shot; recovery marker is not. */
 uint32_t reset=NRF_POWER->RESETREAS;NRF_POWER->RESETREAS=0xffffffffu;
 for(unsigned i=0;i<4;i++){lamp(i,false);lamps[i]->PIN_CNF[pins[i]]=1u;}
 lamp(0,true); /* main reached, before USB initialization */
 NRF_P1->OUTSET=1u<<10;NRF_P1->PIN_CNF[10]=1; /* existing track-ladder power */
 NRF_P0->PIN_CNF[27]=GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos;
 NRF_P0->PIN_CNF[22]=NRF_P0->PIN_CNF[24]=GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos;
 NRF_P0->OUTCLR=1u<<21;NRF_P0->PIN_CNF[21]=1u;
 const struct device *wdt=DEVICE_DT_GET(DT_NODELABEL(wdt0));
 if(device_is_ready(wdt)){wdt_install_timeout(wdt,&(struct wdt_timeout_cfg){.window.max=4000});wdt_setup(wdt,0);}
 feed();
 /* Let USB power settle while remaining visible and feeding inherited WDT.
  * A single charger /PGOOD observation must not immediately shut down setup. */
 for(unsigned i=0;i<50;i++){feed();k_msleep(10);}
 startup551_stamp(0xb2u);
 struct usbd_context *usb=sample_usbd_init_device(NULL);
 startup551_stamp(0xb3u);
 int error=usb?0:-19;
 if(usb){usbd_device_set_bcd_device(usb,0x0335);error=usbd_enable(usb);}
 startup551_stamp(0xb4u);
 bool enabled=error==0;
 if(enabled){error=uart_irq_callback_user_data_set(cdc,receive,NULL);if(!error)uart_irq_rx_enable(cdc);}
 lamp(1,enabled);lamp(3,error!=0);
 startup551_stamp(0xb5u);
 struct setup_host host={0};radio_keys579 keys={.spent=true};uint32_t keys_at=0;
 for(;;){
  feed();uint32_t now=k_uptime_get_32();
  bool vbus=(NRF_POWER->USBREGSTATUS&POWER_USBREGSTATUS_VBUSDETECT_Msk)!=0;
  bool pgood=(NRF_P0->IN&(1u<<24))==0;
  bool pressed=(NRF_P0->IN&(1u<<27))==0;
  /* FN intent remains latched in power_state until the active radio
   * transaction finishes or times out. Never abort a flash command to sleep. */
  if(setup_should_off(&power_state,now,pressed,vbus,pgood)&&!sp1_setup_transaction_active())off();
  uint32_t dtr=0;if(enabled)uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&dtr);
  lamp(0,(now%1000)<500);lamp(2,dtr!=0);lamp(3,error||overflow);
  /* One greeting after DTR has settled, renewed only after host disconnect. */
  if(setup_hello(&host,now,dtr!=0)){
   /* At a fresh greeting, host commands have not been admitted yet. */
   if(NRF_POWER->GPREGRET2!=0xa5u&&NRF_POWER->GPREGRET2!=0xa4u&&!clean607_pending())NRF_POWER->GPREGRET2=0;
   keys580_report(&keys,!pressed);
   printk("BTID551 STARTUP reached=host-greeting\n");
   printk(source_capability565);
   printk("BTID335 SETUP identity=%08x%08x radio_writes=bounded-host-plan audio=disabled storage=disabled\n",NRF_FICR->DEVICEID[1],NRF_FICR->DEVICEID[0]);
   printk("BTID335 BOOT reset=%08x usb_error=%d vbus=%u pgood=%u\n",reset,error,vbus,pgood);
   printk("BTID535 SETUP direct_return=1\n");
   printk("BTID545 SPEED autobaud=1000000 export=batched pipeline=1 rx_wakeup=1 read_only=0\n");
   printk("BTID607 CLEAN profile=static-v1 forward=1 pending=%u\n",clean607_pending());
   printk("BTID524 SETUP recovery=%u return_gesture=t2+t3 hold_ms=3000\n",sp1_setup_recovery_required());
  }
  bool open=host.ready;
  uint8_t b;unsigned count=0;while(count++<128&&ring_buf_get(&input,&b,1))
   if(open&&!overflow&&!error){
    /*535: share the existing parser word; no new retained RAM. A fresh
     * updater session may restart with recovery retained, never exit to audio. */
    if(!sp1_setup_transaction_active()){
     unsigned pos=exit_match&255u,kind=exit_match>>8;
     if(!pos){exit_match=b=='A'?1u:0u;}
     else if(pos==1u){exit_match=b=='3'?0x102u:b=='5'?0x202u:0u;}
     else{
      const char*command=kind==1u?exit_command:"A535NEW?";
      if(b==(uint8_t)command[pos]){
       pos++;exit_match=(kind<<8)|pos;
       if(!command[pos]){
        exit_match=0;
        if(kind==2u){NRF_POWER->GPREGRET2=clean607_pending()?0xa4u:sp1_setup_recovery_required()?0xa5u:0xa7u;__DSB();sys_reboot(SYS_REBOOT_COLD);}
        else if(sp1_setup_return_safe()){NRF_POWER->GPREGRET2=0xa9u;__DSB();sys_reboot(SYS_REBOOT_COLD);}
       }
      }else exit_match=b=='A'?1u:0u;
     }
    }else exit_match=0;
    sp1_bt_probe_byte(b);
   }
  sp1_bt_probe_service(open&&!overflow&&!error&&(vbus||pgood));
  if(open||!dtr)sp1_bt_probe_report();
  if((uint32_t)(now-keys_at)>=50){
   keys_at=now;int raw=0;bool safe=sp1_setup_return_safe();bool valid=keys524_read(&raw);
   unsigned action=radio_keys579_step(&keys,now,raw,valid,!pressed,sp1_setup_transaction_active(),safe);
   if(action&&!sp1_setup_transaction_active()&&(action==2||sp1_setup_return_safe())){
    /* The transaction finished or failed before reset; preserve recovery even
     * when music is unavailable. Never reset an active erase/program command. */
    sp1_bt_probe_stop();NRF_POWER->RESETREAS=0xffffffffu;NRF_POWER->GPREGRET=action==2?0x57u:0;
    NRF_POWER->GPREGRET2=clean607_pending()?0xa4u:setup524_return(NRF_POWER->GPREGRET2,false,action==2);__DSB();sys_reboot(SYS_REBOOT_COLD);
   }
   if(keys.kind||keys.pending){unsigned kind=keys.pending?keys.pending:keys.kind;lamp(kind==2?0:1,(now/250)%2);lamp(kind==2?3:2,(now/250)%2);}
  }
  k_msleep(1);
 }
}
