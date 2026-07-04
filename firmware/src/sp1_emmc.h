/*
 * SP-1 eMMC 1-bit bit-bang driver (Zephyr port of Tim Knapen's SP-1-dev emmc.c).
 * Toshiba THGBMNG5D1LBAIL, 4 GB, connected CLK/CMD/DAT0/RST_n + VCCQ only.
 * Ported to use the nrfx GPIO HAL + k_busy_wait so it builds in the Zephyr app.
 * The DATA-phase clock half-period is runtime-configurable (g_emmc_clk_half_us);
 * the firmware sets it to 0 (delay-free, fastest) after init. Commands/init
 * always use a fixed safe clock internally.
 */
#ifndef SP1_EMMC_H
#define SP1_EMMC_H

#include <stdint.h>
#include <stdbool.h>

/* Build selector (the only knob that differs between the two builds).
 *   0 = 48 kHz: flash bus at 32 MHz (M32), full rate.
 *   1 = 24 kHz: flash bus at the in-spec 16 MHz (M16). Half the data rate gives
 *       ~2x bandwidth headroom, so it stays rock-solid under a full 4-track load.
 *       The 32 MHz overclock (over the eMMC's 26 MHz spec) was what made an earlier
 *       24 kHz build glitch into white noise; M16 cures it. Both builds keep the
 *       eMMC write cache ON (it absorbs record bursts so writes don't stall the bus
 *       and crackle the playing tracks).
 * It drives DECIM (sample rate) and the SPIM flash clock. */
#ifndef SP1_BUILD_24K
#define SP1_BUILD_24K 0
#endif

#define EMMC_BLOCK_SIZE 512u

/* DATA-transfer clk half-period in microseconds. 0 = fastest (no busy-wait, just
 * GPIO register toggles); the streamer sets 0 after emmc_init(). Commands/init
 * always use a fixed safe clock internally. */
extern volatile uint32_t g_emmc_clk_half_us;
extern volatile uint32_t emmc_crc_rd_errs;   /* verified-read CRC catches  */
extern volatile uint32_t emmc_crc_wr_errs;   /* write status-token catches */

/* ---- diagnostics, filled by emmc_init() to pinpoint where bring-up fails ---- */
extern bool    emmc_dbg_cmd0_sent;
extern int     emmc_dbg_cmd1_retries;   /* retries until ready; -1 = never ready */
extern bool    emmc_dbg_cmd2_resp;
extern bool    emmc_dbg_cmd3_resp;
extern bool    emmc_dbg_cmd7_resp;
extern bool    emmc_dbg_cmd16_resp;
extern uint8_t emmc_dbg_ocr[6];          /* CMD1 R3 response bytes */
extern uint8_t emmc_dbg_r1[6];           /* CMD7 R1 response bytes */
/* Last single-block read/write command response flags (for read/write tests). */
extern bool    emmc_dbg_last_cmd_resp;
extern int     emmc_dbg_resp_clocks;   /* clocks until last response start bit (-1=none) */
extern int     emmc_dbg_cmd2_clocks;   /* clocks-to-response captured for CMD2 (-1=none) */
extern int     emmc_dbg_cmd2_tries;    /* CMD2 attempts before a response */
extern int     emmc_dbg_wr_status;     /* CRC status token from last block write:
					* 0b010=2 accepted, 0b101=5 CRC err, 0b110=6 write err, -1=none */
extern uint16_t emmc_dbg_rd_crc;
extern volatile uint32_t emmc_dbg_wr_busy_max;  /* diag: max post-write program busy-wait clk iterations */       /* CRC16 the card appended to the last read block */
/* Wall-clock stall diagnostics (us). THE numbers the crackle hunt needs: the
 * old iteration counters were uncalibrated and their silent expiry hid the
 * true stall length. window = reset by the diag printer; peak = session max. */
extern volatile uint32_t emmc_dbg_wr_busy_us_max;    /* worst write program-busy, us (window) */
extern volatile uint32_t emmc_dbg_wr_busy_us_peak;   /* worst write program-busy, us (session) */
extern volatile uint32_t emmc_dbg_rd_wait_us_max;    /* worst read start-bit access wait, us (window) */
extern volatile uint32_t emmc_dbg_switch_busy_us_max;/* worst R1b busy: CMD6 flush/BKOPS/TRIM, us */
extern volatile uint32_t emmc_dbg_busy_timeouts;     /* busy-poll expiries (SEPARATE from CRC errs) */
extern volatile uint32_t emmc_dbg_hpi_fires;         /* HPI aborts issued (maintenance ops cut short) */


bool emmc_init(void);
bool emmc_is_ready(void);
bool emmc_cmd13(uint8_t *r1_out);      /* SEND_STATUS — card status register R1 */
bool emmc_read_ext_csd(uint8_t *buf);  /* CMD8 SEND_EXT_CSD -> 512-byte EXT_CSD (read-only) */
bool emmc_cache_enable(void);          /* EXT_CSD CACHE_CTRL=1 (volatile write cache on) */
bool emmc_cache_flush(void);           /* EXT_CSD FLUSH_CACHE=1 (program cache to NAND) */
/* FTL-maintenance ops (eMMC 4.5+/5.0). ALL fail-safe: gate on the EXT_CSD
 * support bytes and treat any false return as "carry on without". */
bool emmc_trim(uint32_t start_blk, uint32_t end_blk, uint32_t busy_us);
bool emmc_cache_flush_try(void);       /* ABORTABLE flush for mid-session idle windows (needs HPI) */
bool emmc_hpi_enable(void);            /* EXT_CSD HPI_MGMT[161]=1; gate on HPI_FEATURES[503] bit0 */
void emmc_set_abort_cb(bool (*cb)(void)); /* busy-abort hook polled by abortable R1b waits */
bool emmc_bkops_auto_enable(void);     /* EXT_CSD[163] |= AUTO_EN (SET-BITS; reversible, never touches OTP MANUAL_EN) */
bool emmc_pon_powered_on(void);        /* EXT_CSD[34]=1 POWERED_ON (boot) */
bool emmc_pon_power_off_short(void);   /* EXT_CSD[34]=2 POWER_OFF_SHORT (shutdown, after the cache flush) */
bool emmc_read_blocks(uint32_t block_addr, uint8_t *buf, uint32_t count);
bool emmc_spim_active(void);           /* diag: true = 32MHz SPIM3 DMA path live, false = bit-bang */
bool emmc_write_blocks(uint32_t block_addr, const uint8_t *buf, uint32_t count);

#endif /* SP1_EMMC_H */
