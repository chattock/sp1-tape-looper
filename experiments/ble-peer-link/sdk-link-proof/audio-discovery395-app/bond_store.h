/* Release candidate: never replace an unfamiliar, corrupt, or differently bound
 * VS record. A failed read is not evidence that a slot is unused. No BND1 upgrade.
 * Only an explicit SDK NOT_FOUND result permits creation. Hardware qualification
 * of that result and NVRAM compaction remains required before installation.
 */
#ifndef SP1_BOND_STORE_H
#define SP1_BOND_STORE_H
#include "wiced_hal_nvram.h"
#include "wiced_bt_dev.h"
#include "bond_policy.h"
#include "bond_abi.h"
_Static_assert(sizeof(wiced_bt_device_link_keys_t)<=RB_MAX,"key size");
_Static_assert(sizeof(wiced_bt_local_identity_keys_t)<=RB_MAX,"identity size");
static unsigned bond_flags;
static unsigned bond_size(unsigned kind){return kind?sizeof(wiced_bt_local_identity_keys_t):sizeof(wiced_bt_device_link_keys_t);}
static int bond_identity(uint8_t *a){wiced_bt_dev_read_local_addr(a);return rb_address(a);}
static int bond_load(unsigned kind,unsigned n,uint8_t *r,uint8_t *a,int *absent){
 *absent=0;
 if(kind>1||n!=bond_size(kind)||!bond_identity(a)){bond_flags|=128;return 0;}
 wiced_result_t status=WICED_BT_ERROR;
 /* Read maximum record size: a longer foreign record must not look like ours. */
 unsigned got=wiced_hal_read_nvram(0x70+kind,255,r,&status);
 if(got==0&&status==WICED_NOT_FOUND){*absent=1;return 0;}
 if(status!=WICED_BT_SUCCESS||!rb_owned(r,got,kind,n,a,sp1_bond_abi)){bond_flags|=128;return 0;}
 return 1;
}
static int bond_read(unsigned kind,void *out,unsigned n){
 uint8_t r[255],a[6];int absent;
 if(!bond_load(kind,n,r,a,&absent)||r[7]!=1)return 0;
 memcpy(out,r+RB_PAYLOAD,n);return 1;
}
static int bond_write(unsigned kind,const void *data,unsigned n,int valid){
 uint8_t old[255],a[6],r[RB_BYTES];int absent;
 if(valid!=0&&valid!=1)return 0;
 int owned=bond_load(kind,n,old,a,&absent);
 if(!owned&&!absent)return 0;
 rb_make(r,kind,a,sp1_bond_abi,data,n,valid);
 if(owned&&!memcmp(r,old,RB_BYTES))return 1;
 wiced_result_t status=WICED_BT_ERROR;
 if(wiced_hal_write_nvram(0x70+kind,RB_BYTES,r,&status)!=RB_BYTES||status!=WICED_BT_SUCCESS){bond_flags|=128;return 0;}
 if(!bond_load(kind,n,old,a,&absent)||memcmp(r,old,RB_BYTES)){bond_flags|=128;return 0;}
 return 1;
}
static int bond_clear(void){
 wiced_bt_device_link_keys_t old;int had=bond_read(0,&old,sizeof old);uint8_t zero[sizeof old]={0};
 if(!bond_write(0,zero,sizeof zero,0))return 0;
 if(had)wiced_bt_dev_delete_bonded_device(old.bd_addr);
 bond_flags=(bond_flags&128)|64;return 1;
}
static wiced_result_t bond_event(wiced_bt_management_evt_t e,wiced_bt_management_evt_data_t *d){
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT){
  if(!bond_write(0,&d->paired_device_link_keys_update,sizeof d->paired_device_link_keys_update,1))return WICED_BT_ERROR;
  bond_flags=(bond_flags&~64u)|16;return WICED_BT_SUCCESS;
 }
 if(e==BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT){
  wiced_bt_device_link_keys_t k;
  if(!bond_read(0,&k,sizeof k)||memcmp(k.bd_addr,d->paired_device_link_keys_request.bd_addr,6))return WICED_BT_ERROR;
  d->paired_device_link_keys_request=k;bond_flags|=16|32;return WICED_BT_SUCCESS;
 }
 if(e==BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT)return bond_write(1,&d->local_identity_keys_update,sizeof d->local_identity_keys_update,1)?WICED_BT_SUCCESS:WICED_BT_ERROR;
 if(e==BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT)return bond_read(1,&d->local_identity_keys_request,sizeof d->local_identity_keys_request)?WICED_BT_SUCCESS:WICED_BT_ERROR;
 return WICED_BT_ERROR;
}
#endif
