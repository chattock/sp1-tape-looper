/* Reusable host-plan adapter qualification. WRITES BOUNDED RADIO SECTORS.
 * H4 HCI_RESET=01 03 0c 00, confirmed against Feldd 0.29.0 binary and Infineon guidance.
 * UART pin directions per Tim Knapen SP-1 dev wiki, Bluetooth-Module.
 * IRQ-driven TX, bounded RX capture, no extra thread, no polling wait loops.
 * Radio startup requires explicit SP1PLAN335; writes require a fresh bounded plan.
 */
#include "sp1_bt_probe.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <soc.h>
#include "gate607_api.h"

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static atomic_t requested;


static const uint8_t query[] = {0x01, 0x03, 0x0c, 0x00};
#include "ram_verify_core.h"
static struct ram_verify rv;
#include "export_read_core.h"
#include "export_line542.h"
#include "wake545.h"
static struct export_read er;
#define DIAG_TAG "BTID335"
#define DIAG_MODE "sdk-reusable-plan-bench-b405"
static unsigned test_arm;
static uint8_t launch_command[] = {0x01,0x4e,0xfc,0x04,0x01,0x02,0x0d,0x00};
static bool launch_sent, launch_ack;
static bool cts_released, host_ready_seen;
static unsigned handoff_high;
static int64_t launch_at;
static unsigned launch_ms;
static unsigned tx_len=4;
static const uint8_t *tx_query = query;
static const uint8_t ack[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
static unsigned attempts;
static volatile unsigned tx_total;
static int64_t retry_at;
static bool got_ack;
static uint8_t rx[256];
static volatile unsigned rx_n, rx_drop, tx_n, errors;
static bool used, active, pending_report, pins_owned, uart_ready;
static uint32_t reset_high, reset_low, cts_high, cts_low;
static uint32_t tx_amount, tx_end, tx_stopped, reset_level, strap_level;
static uint32_t rts_saved, rts_out_saved;
static uint32_t cts_boot_high, cts_boot_low, startup_rx;
static uint32_t hw_enable, hw_config, hw_baud, hw_tx, hw_rx, hw_rts, hw_cts;
static int result;
static int64_t deadline, overall_deadline;
enum { IDLE, RESET_LOW, BOOT_WAIT, LISTEN, QUERY, RAM, LAUNCH, OBSERVE, PRELAUNCH, READ_BEFORE, READ_AFTER, READ_EXPORT, WAIT_ARM, UPLOAD_SECTOR, WRITE_SECTOR };
static unsigned phase;
static void finish(void);
#include "bridge.inc"
/* Host-side P1.05 snapshots; all three arms temporarily drives the recovery strap.
 * IN is not an electrical observation when PIN_CNF.INPUT is disconnected.
 * Records are sequential register reads, not simultaneous waveform captures. */
static uint32_t cs_context[5][3], cs_valid;
static bool cs_owned, cs_started, cs_restored, cs_low_seen;
static int64_t cs_reset_released_at;
static unsigned cs_release_ms;
/* All arms, only the observed input/disconnected/no-pull, OUT0 starting state.
 * Assert under radio reset. Release to exactly that input state after >=50ms.
 * On every abort, assert radio reset BEFORE releasing this strap. */
static void cs_restore(void)
{
 if(!cs_owned) return;
 NRF_P1->PIN_CNF[5]=2u;
 cs_owned=false;cs_restored=NRF_P1->PIN_CNF[5]==2u && !(NRF_P1->OUT & (1u<<5));
}
static void cs_snapshot(unsigned stage)
{
 if(stage>=5) return;
 cs_context[stage][0]=NRF_P1->PIN_CNF[5];
 cs_context[stage][1]=(NRF_P1->OUT>>5)&1u;
 cs_context[stage][2]=(NRF_P1->IN>>5)&1u;
 cs_valid|=1u<<stage;
}

/* Nonblocking recovery entry. The module's CTS is HOST RTS P1.01.
 * Keep it LOW across reset release. Arm2 also pulses P1.05 LOW across reset.
 * 20ms reset / 1000ms boot / 300ms listen / 2000ms query capture are conservative test windows,
 * not measured device boot-time requirements. Only one attempt per boot. */
static void reset_write(bool high)
{
 if(high) NRF_P0->OUTSET=1u<<10; else NRF_P0->OUTCLR=1u<<10;
 NRF_P0->PIN_CNF[10]=1u; /* output, input connected, no pull, standard drive */
}
void sp1_bt_probe_byte(uint8_t b)
{
 bridge_byte(b);

}
static void radio_irq(const struct device *dev, void *arg)
{
 if (!uart_irq_update(dev)) return;
 unsigned before=rx_n,dropped_before=rx_drop;
 int err=uart_err_check(dev); if (err>0) errors |= (unsigned)err;
 /* A fixed per-IRQ bound prevents a noisy radio monopolizing the processor. */
 for (unsigned pass=0; pass<4 && uart_irq_rx_ready(dev); ++pass) {
  uint8_t b[16]; int n=uart_fifo_read(dev,b,sizeof b);
  if (n<=0) break;
  for (int i=0;i<n;++i) { if(rx_n<sizeof rx) rx[rx_n++]=b[i]; else ++rx_drop; }
 }
 if (uart_irq_tx_ready(dev)) {
  if (tx_n<tx_len) {
   int n=uart_fifo_fill(dev,tx_query+tx_n,tx_len-tx_n);
   if(n>0) { tx_n+=(unsigned)n; tx_total+=(unsigned)n; }
  }
  if(tx_n==tx_len) uart_irq_tx_disable(dev);
 }
 /* Reuse existing callback userdata for the sleeping service thread. No new
  * retained buffer/semaphore/thread. Wake only when a response boundary is
  * crossed or a new transport fault occurs; the1ms sleep remains a fallback. */
 unsigned goal=0;
 if(phase==QUERY||phase==LAUNCH)goal=7;
 else if(phase==RAM){if(rv.phase==RV_READ)goal=7+rv.size;else if(rv.phase==RV_ENTRY||rv.phase==RV_WRITE)goal=7;}
 else if(phase==READ_EXPORT)goal=7+er.size;
 else if(phase==WRITE_SECTOR&&sector.inflight){unsigned total=7+(sector.state==SG_READ?sector.count:0);if(sector.received<total)goal=total-sector.received;}
 if(arg&&wake545(goal,before,rx_n,err>0||rx_drop!=dropped_before))k_wakeup((k_tid_t)arg);

}
static bool ack_received(void)
{
 unsigned n=rx_n;
 for(unsigned i=0;i+sizeof ack<=n;++i) {
  unsigned j=0; while(j<sizeof ack && rx[i+j]==ack[j]) ++j;
  if(j==sizeof ack) return true;
 }
 return false;
}
/* RX is consumed while IRQs are locked; transfer is strictly one command at a time. */
static void ram_send(void)
{
 uart_irq_tx_disable(radio);
 rx_n=0;
 if(rv.phase==RV_SETTLE) return;
 tx_query=rv.frame;tx_len=rv.frame_len;tx_n=0;
 uart_irq_tx_enable(radio);
}
static void finish(void)
{
 cs_snapshot(3);
 if(launch_sent) launch_ms=(unsigned)(k_uptime_get()-launch_at);
 reset_level=(NRF_P0->IN>>10)&1;
 if(uart_ready) {
  hw_enable=NRF_UARTE0->ENABLE; hw_config=NRF_UARTE0->CONFIG;
  hw_baud=NRF_UARTE0->BAUDRATE;
  hw_tx=NRF_UARTE0->PSEL.TXD; hw_rx=NRF_UARTE0->PSEL.RXD;
  hw_rts=NRF_UARTE0->PSEL.RTS; hw_cts=NRF_UARTE0->PSEL.CTS;
  tx_amount=NRF_UARTE0->TXD.AMOUNT;
  tx_end=NRF_UARTE0->EVENTS_ENDTX;
  tx_stopped=NRF_UARTE0->EVENTS_TXSTOPPED;
 }
 /* First quiesce the external application, including on shutdown/abort. */
 if(pins_owned) { reset_write(false); NRF_P1->OUTCLR=1u<<1; }
 cs_restore();
 cs_snapshot(4);
 if(uart_ready) {
  uart_irq_tx_disable(radio); uart_irq_rx_disable(radio);
  NRF_UARTE0->TASKS_STOPTX=1; NRF_UARTE0->TASKS_STOPRX=1;
 } else if(pins_owned) {
  if(rts_out_saved) NRF_P1->OUTSET=1u<<1; else NRF_P1->OUTCLR=1u<<1;
  NRF_P1->PIN_CNF[1]=rts_saved;
 }
 if(result==1&&mode==3&&arm_received&&write_verified&&(!plan_count||post_read)&&admission_complete(&admission)){
  if(!clean607_clear())result=-40;else sp1_setup_write_verified();
 }else if(result==1&&write_started&&write_verified&&post_read&&admission_complete(&admission))sp1_setup_write_verified();
 active=false; pending_report=true;
}
void sp1_bt_probe_stop(void)
{
 if(active) { result=-3; finish(); }
 /* Reset stays asserted after an attempted startup, also across SYSTEM_OFF. */
 if(pins_owned) { reset_write(false); NRF_P1->OUTCLR=1u<<1; }
 atomic_set(&requested,0);
}
void sp1_bt_probe_service(bool allowed)
{
 int64_t now=k_uptime_get();
 if(host_length&&now>=host_deadline)protocol_error=true;
 if(protocol_error){if(active){result=-30;finish();}atomic_set(&requested,0);return;}
 if(active) {
  uint32_t host_dtr=0;
  bool connected=!uart_line_ctrl_get(console,UART_LINE_CTRL_DTR,&host_dtr) && host_dtr;
  if(!allowed || !connected || now>=overall_deadline || errors || rx_drop) {
   result=(errors || rx_drop) ? -10 : -3;finish();return;
  }
  if(cs_owned && phase==BOOT_WAIT && now>=cs_reset_released_at+50) {
   cs_release_ms=(unsigned)(now-cs_reset_released_at);cs_restore();
   if(!cs_restored) {result=-20;finish();}
  }
  if(!active) return;
  if(NRF_P0->IN & (1u<<10)) ++reset_high; else ++reset_low;
  if(uart_ready) {
   if(phase==QUERY || phase==LISTEN || phase==RAM || phase==LAUNCH || phase==OBSERVE) {
    if(NRF_P1->IN & (1u<<3)) ++cts_high; else ++cts_low;
   } else {
    if(NRF_P1->IN & (1u<<3)) ++cts_boot_high; else ++cts_boot_low;
   }
  }

  if(!allowed || !connected || now>=overall_deadline) { result=-3; finish(); }
  else if(launch_sent && now>=launch_at+720000 && (phase==READ_AFTER || phase==READ_EXPORT)) {
   
   result=phase==READ_EXPORT?-21:1;finish();
  }
  else if(errors || rx_drop) { result=-10; finish(); }
  else if(phase==QUERY && now<deadline && tx_n==tx_len && ack_received()) {
   printk("BTID545 AUTOBAUD baud=1000000 reset_ack=1\n");got_ack=true;unsigned key=irq_lock();rv_begin(&rv,now);phase=RAM;
   overall_deadline=now+61000;ram_send();irq_unlock(key);
  }
  else if(phase==RAM) {
   unsigned key=irq_lock();int action=(rv.phase!=RV_SETTLE && tx_n!=tx_len && (rx_n || now>=rv.deadline)) ? rv_fail(&rv,11) : rv_poll(&rv,now,rx,rx_n);
   if(action==RV_SEND) ram_send();
   irq_unlock(key);
   if(action==RV_FAIL) { result=-11;finish(); }
   else if(action==RV_FINISH) {
    /* The immutable image must be completely verified before the ONLY launch. */
    if(rv.phase!=RV_DONE || rv.written!=39 || rv.verified!=39 || rv.bytes!=8966 || rv.error || launch_sent) {
     result=-12;finish();
    } else {
     uart_irq_tx_disable(radio);NRF_P1->OUTSET=1u<<1;
     phase=PRELAUNCH;deadline=now+20;
     cts_released=false;host_ready_seen=false;handoff_high=0;
    }
   }
  }
  else if(phase==WAIT_ARM || phase==UPLOAD_SECTOR || phase==WRITE_SECTOR) { bridge_service(now); }
  else if(phase==READ_EXPORT) {
   unsigned k=irq_lock();
   int action=(tx_n!=tx_len && (rx_n || now>=er.deadline))?er_fail(&er,7):er_poll(&er,now,rx,rx_n);
   if(action==ER_SEND){uart_irq_tx_disable(radio);rx_n=0;tx_n=0;tx_query=er.command;tx_len=9;uart_irq_tx_enable(radio);}
   irq_unlock(k);
   if(action==ER_EMIT){
    /* Hash every duplicate-verified byte before exporting. */
    if(!admission_feed(&admission,er.flash_offset,er.chunk,er.size,post_read)){printk(DIAG_TAG " ADMISSION_MISMATCH sector=%05x post=%u\n",(admission.offset-1)&~4095u,post_read);result=-31;finish();return;}
    char line[512];
    if(!export_line542(line,post_read?1:0,er.flash_offset,er.chunk,er.size)){result=-21;finish();return;}
    /* The immutable stack copy permits the next READ to overlap USB export.
     * Still only one outstanding UART request, two reads per byte, same hash.
     * UART IRQ owns rx; no writer is active during READ_EXPORT. */
    unsigned key=irq_lock();action=er_emitted(&er,k_uptime_get());
    if(action==ER_SEND){uart_irq_tx_disable(radio);rx_n=0;tx_n=0;tx_query=er.command;tx_len=9;uart_irq_tx_enable(radio);}
    irq_unlock(key);
    printk("%s",line);
   }
   if(action==ER_FAIL){result=-21;finish();}
   else if(action==ER_DONE){
    if(!admission_complete(&admission)){result=-31;finish();}
    else if(post_read){result=1;finish();}
    else {uart_irq_tx_disable(radio);rx_n=0;phase=WAIT_ARM;deadline=now+90000;if(!bridge_challenge()){result=-35;finish();}}
   }
  }
  else if(phase==PRELAUNCH) {
   if(now>=deadline) {
    handoff_high=(NRF_P1->IN>>1)&1;
    if(!handoff_high) {result=-14;finish();}
    else {
     unsigned key=irq_lock();rx_n=0;
     tx_query=launch_command;tx_len=sizeof launch_command;tx_n=0;
     cs_snapshot(2);
     launch_sent=true;launch_at=now;launch_ms=0;phase=LAUNCH;
     deadline=now+1000;overall_deadline=now+721000;
     uart_irq_tx_enable(radio);irq_unlock(key);
    }
   }
  }
  else if(phase==LAUNCH) {
   if(!cts_released && now-launch_at>=500) {
    NRF_P1->OUTCLR=1u<<1;cts_released=true;
   }
   if(cts_released && !(NRF_P1->IN & (1u<<1))) host_ready_seen=true;
   /* Only a complete successful Command Complete for FC4E counts as launch ACK.
    * An absent/negative/malformed reply resets the radio; it is never retried. */
   unsigned key=irq_lock();unsigned n=rx_n;
   bool complete=n>=7;
   bool good=now<deadline && complete && rx[0]==4 && rx[1]==14 && rx[2]==4 && rx[3]>0 && rx[4]==0x4e && rx[5]==0xfc && rx[6]==0 && tx_n==tx_len;
   if(good && cts_released && host_ready_seen) {launch_ack=true;phase=OBSERVE;deadline=launch_at+720000;}
   irq_unlock(key);
   if((!good && complete) || (phase==LAUNCH && now>=deadline)) {result=host_ready_seen ? -13 : -15;finish();}
  }
  else if(phase==OBSERVE) {
   if(now>=deadline) {result=-21;finish();}
   else if(now-launch_at>=500) {
    unsigned k=irq_lock();
    er_begin(&er,now,test_arm);uart_irq_tx_disable(radio);rx_n=0;tx_n=0;
    tx_query=er.command;tx_len=9;phase=READ_EXPORT;
    uart_irq_tx_enable(radio);irq_unlock(k);
   }
  }
  else if(now>=deadline) {
   if(phase==RESET_LOW) {
    strap_level=(NRF_P1->IN>>1)&1;
    if(strap_level) { result=-5; finish(); }
    else if(cs_owned && (NRF_P1->PIN_CNF[5]!=1u || (NRF_P1->IN & (1u<<5)))) {result=-19;finish();}
    else { if(cs_owned) cs_low_seen=true;reset_write(true);cs_reset_released_at=now;phase=BOOT_WAIT;deadline=now+1000; }
   } else if(phase==BOOT_WAIT) {
    reset_level=(NRF_P0->IN>>10)&1;
    strap_level=(NRF_P1->IN>>1)&1;
    if(!reset_level || strap_level) { result=-6; finish(); }
    else {
     /* Keep host readiness low during recovery capture. */
     NRF_P1->OUTCLR=1u<<1;
     phase=LISTEN; deadline=now+300;
    }
   } else if(phase==LISTEN) {
    startup_rx=rx_n;phase=QUERY;deadline=now+2000;
    attempts=1; retry_at=now+150; uart_irq_tx_enable(radio);
   } else if(phase==QUERY) { result=-45; finish(); }

  }
  else if(phase==QUERY && attempts<8 && now>=retry_at && tx_n==tx_len) {
   /* Disable first: no ISR can resend an old command while the counter resets. */
   uart_irq_tx_disable(radio); tx_n=0; ++attempts; retry_at=now+150; uart_irq_tx_enable(radio);
  }
 }
 int selected=atomic_set(&requested,0);
 if(!selected || active) return;
 if(!allowed) { result=-1; pending_report=true; return; }
 if(used) { result=-2; pending_report=true; return; }
 used=true;
 cs_valid=0;cs_snapshot(0);
 cs_owned=cs_started=cs_restored=cs_low_seen=false;cs_release_ms=0;cs_reset_released_at=0;
 test_arm=(unsigned)selected-1;
 /* Fixed vendor entry; selector changes only the bounded read window. */
 /* Do not take pins already driven by another owner, or a live UART. */
 if((NRF_P0->PIN_CNF[10]&1u) || (NRF_P1->PIN_CNF[1]&1u) || NRF_UARTE0->ENABLE) {
  result=-4; pending_report=true; return;
 }
 if((NRF_P1->PIN_CNF[5]!=2u || (NRF_P1->OUT & (1u<<5)))) {
  result=-18;pending_report=true;return;
 }
 rts_saved=NRF_P1->PIN_CNF[1];rts_out_saved=(NRF_P1->OUT>>1)&1;
 pins_owned=true;
 reset_write(false);
 {
  NRF_P1->OUTCLR=1u<<5;NRF_P1->PIN_CNF[5]=1u;
  cs_owned=cs_started=true;
 }
 cs_snapshot(1);
 /* Configure UART while module is held in reset: TX idles high and
  * RX/CTS inputs are valid before release. RTS is deliberately omitted from pinctrl and driven manually LOW
  * through recovery entry, then LOW during bounded capture. No live PSEL changes. */
 NRF_UARTE0->PSEL.RTS=0xffffffffu; /* UART disabled here; reserve manual RTS */
 int rc=device_init(radio);
 if(rc<0 || !device_is_ready(radio)) { result=rc<0 ? rc-100 : -100; finish(); return; }
 uart_ready=true;
 if(NRF_UARTE0->BAUDRATE!=0x10000000u){result=-44;finish();return;}
 if(NRF_UARTE0->PSEL.RTS!=0xffffffffu) { result=-7; finish(); return; }
 if(NRF_UARTE0->CONFIG & 1u) { result=-8; finish(); return; } /* HWFC must be off. */
 rc=uart_irq_callback_user_data_set(radio,radio_irq,(void*)k_current_get());
 if(rc<0) { result=rc-100; finish(); return; }

 NRF_P1->OUTCLR=1u<<1;
 NRF_P1->PIN_CNF[1]=1u;
 rx_n=rx_drop=tx_n=errors=0; attempts=tx_total=0; got_ack=false; tx_query=query; tx_len=sizeof query; memset(&rv,0,sizeof rv);memset(&er,0,sizeof er);launch_sent=launch_ack=false;launch_at=0;launch_ms=0;cts_released=host_ready_seen=false;handoff_high=0;
 uart_irq_rx_enable(radio);
 phase=RESET_LOW;deadline=now+20;overall_deadline=now+5000;active=true;
}
void sp1_bt_probe_report(void)
{
 static bool was_open;
 uint32_t dtr=0;
 if(uart_line_ctrl_get(console,UART_LINE_CTRL_DTR,&dtr) || !dtr) { was_open=false; return; }
 if(!was_open) { printk(DIAG_TAG " READY base=guarded-helper probe " DIAG_MODE "\n"); was_open=true; }
 if(!pending_report || active) return;
 pending_report=false;
 /* Called with the existing, DTR-gated diagnostics, not from an ISR.
  * Keep output bounded; normal watchdog feed follows the diagnostics. */
 printk(DIAG_TAG " ARM %u\n",test_arm);
 printk(DIAG_TAG " STRAP started=%u low_seen=%u restored=%u release_ms=%u\n",
  (unsigned)cs_started,(unsigned)cs_low_seen,(unsigned)cs_restored,cs_release_ms);
 for(unsigned i=0;i<5;++i) if(cs_valid & (1u<<i))
  printk(DIAG_TAG " CS stage=%u cnf=%08x out=%u in=%u\n",i,
   (unsigned)cs_context[i][0],(unsigned)cs_context[i][1],(unsigned)cs_context[i][2]);
 printk(DIAG_TAG " result=%d tx_accepted=%u rx=%u dropped=%u errors=%u cts=%u\n",
  result,tx_total,rx_n,rx_drop,errors,(unsigned)((NRF_P1->IN>>3)&1));
 printk(DIAG_TAG " STATE phase=%u reset_before_stop=%u strap_high=%u tx_amount=%u endtx=%u txstopped=%u\n",
  phase,(unsigned)reset_level,(unsigned)strap_level,(unsigned)tx_amount,(unsigned)tx_end,(unsigned)tx_stopped);
 printk(DIAG_TAG " SENSE reset_high=%u reset_low=%u cts_high=%u cts_low=%u reset_held_after=%u\n",
  (unsigned)reset_high,(unsigned)reset_low,(unsigned)cts_high,(unsigned)cts_low,(unsigned)pins_owned);
 printk(DIAG_TAG " UART enable=%u config=%08x baud=%08x tx=%u rx=%u rts=%u cts=%u startup_rx=%u boot_cts_high=%u boot_cts_low=%u\n",
  (unsigned)hw_enable,(unsigned)hw_config,(unsigned)hw_baud,(unsigned)hw_tx,(unsigned)hw_rx,(unsigned)hw_rts,(unsigned)hw_cts,
  (unsigned)startup_rx,(unsigned)cts_boot_high,(unsigned)cts_boot_low);
 for(unsigned i=0;i<rx_n;i+=16) {
  printk(DIAG_TAG " RX ");
  for(unsigned j=i;j<rx_n && j<i+16;++j) printk("%02x ",rx[j]);
  printk("\n");
 }
 printk(DIAG_TAG " HCI attempts=%u ack=%u\n",attempts,(unsigned)got_ack);
 printk(DIAG_TAG " RAM written=%u verified=%u bytes=%u phase=%u error=%d entry_silent=%u launch=%u\n",rv.written,rv.verified,rv.bytes,rv.phase,rv.error,(unsigned)rv.entry_silent,(unsigned)launch_sent);
 printk(DIAG_TAG " LAUNCH sent=%u ack=%u elapsed_ms=%u reset_held_after=%u\n",(unsigned)launch_sent,(unsigned)launch_ack,launch_ms,(unsigned)pins_owned);
 printk(DIAG_TAG " HANDOFF high_before_launch=%u high_window_ms=500 ready_restored=%u low_seen=%u\n",handoff_high,(unsigned)cts_released,(unsigned)host_ready_seen);
 printk(DIAG_TAG " FAST status=%d bytes=%u blocks=%u duplicate_reads=2 error=%d\n",er.status,er.flash_offset,er.flash_blocks,er.error);
 printk(DIAG_TAG " WRITE started=%u verified=%u full_verified=%u mode=%u sectors=%u\n",write_started,write_verified,(post_read||(arm_received&&!plan_count))&&admission_complete(&admission),mode,plan_count);
 printk(DIAG_TAG " END\n");
}

bool sp1_setup_transaction_active(void){return active||atomic_get(&requested);}

bool sp1_setup_return_safe(void){return !sp1_setup_recovery_required()&&!active&&!atomic_get(&requested)&&(!write_started||(result==1&&write_verified&&post_read&&admission_complete(&admission)));}
