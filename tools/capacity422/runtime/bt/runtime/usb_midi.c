/* Minimal USB-MIDI1 device class. Shared UDC pool; no private sample buffers. */
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/sys/atomic.h>
#include <string.h>
extern void bt308_usb_packet(const uint8_t p[4]);
extern void bt308_usb_detach(void);
static const struct usbd_class_data *cls;
static atomic_t enabled,rx_busy,tx_busy;
/* USB-MIDI1 AC + MS, one embedded/external jack in each direction. */
static uint8_t iad[]={8,11,0,2,1,1,0,0};
static uint8_t ac[]={9,4,0,0,0,1,1,0,0};
static uint8_t ach[]={9,36,1,0,1,9,0,1,1};
static uint8_t ms[]={9,4,1,0,2,1,3,0,0};
static uint8_t msh[]={7,36,1,0,1,65,0};
static uint8_t ji[]={6,36,2,1,1,0},je[]={6,36,2,2,2,0};
static uint8_t jo[]={9,36,3,1,3,1,2,1,0},jx[]={9,36,3,2,4,1,1,1,0};
static uint8_t eo[]={9,5,1,2,64,0,0,0,0},eco[]={5,37,1,1,1};
static uint8_t ei[]={9,5,0x81,2,64,0,0,0,0},eci[]={5,37,1,1,3};
static uint8_t end[]={0};
static const void *const desc[]={iad,ac,ach,ms,msh,ji,je,jo,jx,eo,eco,ei,eci,end};
static void *get_desc(struct usbd_class_data *c,enum usbd_speed speed){(void)c;(void)speed;ach[8]=ms[2];return (void*)desc;}
static void arm_rx(void){
 if(!atomic_get(&enabled)||!atomic_cas(&rx_busy,0,1))return;
 struct net_buf *b=usbd_ep_buf_alloc(cls,eo[2],64);
 if(!b){atomic_clear(&rx_busy);return;}
 if(usbd_ep_enqueue(cls,b)){usbd_ep_buf_free(usbd_class_get_ctx(cls),b);atomic_clear(&rx_busy);}
}
int sp1_usb_midi_ready(void){return atomic_get(&enabled);}
void sp1_usb_midi_poll(void){arm_rx();}
int sp1_usb_midi_send(const uint8_t *p,unsigned n){
 if(!n||n>64||n%4||!atomic_get(&enabled)||!atomic_cas(&tx_busy,0,1))return 0;
 struct net_buf*b=usbd_ep_buf_alloc(cls,ei[2],n);
 if(!b){atomic_clear(&tx_busy);return 0;}net_buf_add_mem(b,p,n);
 if(usbd_ep_enqueue(cls,b)){usbd_ep_buf_free(usbd_class_get_ctx(cls),b);atomic_clear(&tx_busy);return 0;}return 1;
}
static int request(struct usbd_class_data*c,struct net_buf*b,int err){
 struct udc_buf_info *i=udc_get_buf_info(b);
 if(USB_EP_DIR_IS_OUT(i->ep)){
  if(!err&&atomic_get(&enabled)&&!(b->len%4))for(unsigned n=0;n<b->len;n+=4)bt308_usb_packet(b->data+n);
  atomic_clear(&rx_busy);
 }else atomic_clear(&tx_busy);
 usbd_ep_buf_free(usbd_class_get_ctx(c),b);return 0;
}
/* Required by pinned device_next: absent init returns -ENOTSUP and aborts
 * the entire composite device before the host can enumerate CDC/audio/MIDI. */
static int init(struct usbd_class_data*c){cls=c;ach[8]=ms[2];return 0;}
static void enable(struct usbd_class_data*c){cls=c;ach[8]=ms[2];atomic_set(&enabled,1);arm_rx();}
static void disable(struct usbd_class_data*c){(void)c;atomic_clear(&enabled);bt308_usb_detach();}
static const struct usbd_class_api api={.init=init,.request=request,.get_desc=get_desc,.enable=enable,.disable=disable,.suspended=disable,.resumed=enable};
USBD_DEFINE_CLASS(sp1_midi1,&api,NULL,NULL);
