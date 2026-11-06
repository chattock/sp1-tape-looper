/* Paged library USB ownership. All host writes land only in WORK.BIN. */
#include <zephyr/storage/disk_access.h>
#include <stdatomic.h>
#include <zephyr/usb/class/usbd_msc.h>
#include "../storage-prototype/release377/exchange_policy.h"
#include "../capacity422/maintenance/editor.h"
K_MUTEX_DEFINE(media_lock);
static _Atomic bool host_active,host_ejected,host_failed;
static uint32_t host_read_blocks,host_write_blocks,host_read_calls,host_write_calls,host_max_blocks;
static uint64_t host_read_us,host_write_us;
static uint32_t host_rejects,host_reject_lba,host_reject_offset,host_reject_region;
static uint32_t host_sync_calls,host_sync_failures,host_eject_calls;
static sp1_exchange exchange;
USBD_DEFINE_MSC_LUN(exchange390,"SP1X","SP1","SP1 MUSIC","0425");
#include "shared_drive.h"
#include "../fresh529/fresh.h"
/*590: reinvest the command buffer's reclaimed8KiB in the existing offline
 * arena. Drive/fresh/decoder sizes remain unchanged; only the frozen eject
 * index may use the extension, after host ownership ends. */
static union {shared_drive drive;fresh529_work fresh;uint8_t bytes[sizeof(shared_drive)+8192];} shared529;
_Static_assert(sizeof(shared529)==((sizeof(shared_drive)+8192+7u)&~7u),"offline arena budget");
#define shared shared529.drive
int sp1_library_readonly(const char*disk){ARG_UNUSED(disk);return 0;}
static drive422 paged_drive;
static editor422 editor;
static cat422_workspace workspace;
/* Reuse completed host-session counters only after host ownership ends.
 * No buffers or static RAM added; timings never run in the audio runtime. */
static bool offline507_active(void);
static bool library_rd(void*c,uint32_t l,void*b,uint32_t n){ARG_UNUSED(c);feed();bool measure=offline507_active();uint32_t t=measure?k_cycle_get_32():0;bool ok=emmc_read_blocks(l,b,n);if(measure){host_read_calls++;host_read_blocks+=n;host_read_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);}return ok;}
static bool library_wr(void*c,uint32_t l,const void*b,uint32_t n){ARG_UNUSED(c);feed();bool measure=offline507_active();uint32_t t=measure?k_cycle_get_32():0;bool ok=emmc_write_blocks(l,b,n);if(measure){host_write_calls++;host_write_blocks+=n;host_write_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);}return ok;}
static bool library_sync(void*c){ARG_UNUSED(c);feed();bool measure=offline507_active();uint32_t t=measure?k_cycle_get_32():0;bool ok=emmc_cache_flush();if(measure){host_sync_calls++;read_time_us+=k_cyc_to_us_floor32(k_cycle_get_32()-t);}return ok;}
static bool drive_read(uint32_t l,uint8_t*b,uint32_t n){return library_rd(NULL,l,b,n);}
/* Browser raw-mount commands cannot bypass the paged transaction. */
static bool exchange_geometry(void){return false;}
static int host_init(struct disk_info*d){ARG_UNUSED(d);return 0;}
static int host_status(struct disk_info*d){ARG_UNUSED(d);return !browser_mode&&host_active&&!host_failed?DISK_STATUS_OK:DISK_STATUS_NOMEDIA;}
void host513_phase(unsigned phase);
static int host_read(struct disk_info*d,uint8_t*b,uint32_t l,uint32_t n){
 ARG_UNUSED(d);k_mutex_lock(&media_lock,K_FOREVER);int r=-EIO;uint32_t began=k_cycle_get_32();
 if(!browser_mode&&host_active&&!host_failed&&drive422_read(&paged_drive,l,b,n)){r=0;host_read_calls++;host_read_blocks+=n;host_read_us+=k_cyc_to_us_floor32(k_cycle_get_32()-began);if(n>host_max_blocks)host_max_blocks=n;}else if(!browser_mode&&host_active)host_failed=true;
 host513_phase(0);k_mutex_unlock(&media_lock);return r;
}
static int host_write(struct disk_info*d,const uint8_t*b,uint32_t l,uint32_t n){
 ARG_UNUSED(d);k_mutex_lock(&media_lock,K_FOREVER);int r=-EIO;uint32_t began=k_cycle_get_32();
 if(!browser_mode&&host_active&&!host_failed&&drive422_write(&paged_drive,l,b,n)){r=0;host_write_calls++;host_write_blocks+=n;host_write_us+=k_cyc_to_us_floor32(k_cycle_get_32()-began);if(n>host_max_blocks)host_max_blocks=n;}else if(!browser_mode&&host_active){host_failed=true;host_rejects++;host_reject_lba=l;}
 host513_phase(1);k_mutex_unlock(&media_lock);return r;
}
static int host_ioctl(struct disk_info*d,uint8_t cmd,void*p){
 ARG_UNUSED(d);switch(cmd){case DISK_IOCTL_CTRL_INIT:case DISK_IOCTL_CTRL_DEINIT:return 0;
 case DISK_IOCTL_GET_SECTOR_COUNT:*(uint32_t*)p=exchange.blocks;return 0;
 case DISK_IOCTL_GET_SECTOR_SIZE:*(uint32_t*)p=512;return 0;
 case DISK_IOCTL_GET_ERASE_BLOCK_SZ:*(uint32_t*)p=16;return 0;
 case DISK_IOCTL_CTRL_SYNC:{if(browser_mode)return -ENOMEDIUM;k_mutex_lock(&media_lock,K_FOREVER);host_sync_calls++;host513_phase(2);bool ok=host_active&&!host_failed&&library_sync(NULL);if(!ok){host_sync_failures++;if(host_active)host_failed=true;}host513_phase(3);k_mutex_unlock(&media_lock);return ok?0:-EIO;}
 default:return -EINVAL;}
}
static const struct disk_operations host_ops={.init=host_init,.status=host_status,.read=host_read,.write=host_write,.ioctl=host_ioctl};
static struct disk_info host_disk={.name="SP1X",.ops=&host_ops};
int sp1_exchange_eject(const char*disk){if(strcmp(disk,"SP1X"))return 0;if(browser_mode)return -ENOMEDIUM;k_mutex_lock(&media_lock,K_FOREVER);host_eject_calls++;host513_phase(5);int r=-EIO;
 if(host_active&&library_sync(NULL)){host_active=false;host_ejected=true;r=0;}else if(!browser_mode&&host_active)host_failed=true;host513_phase(6);k_mutex_unlock(&media_lock);return r;}
