#include "storage373.h"
/*
 * ============================================================================
 *  SP-1 eMMC flash driver  (1-bit MMC protocol over the nRF52840)
 * ============================================================================
 *  The SP-1's 4 GB flash is wired to the nRF with just three lines (CLK, CMD,
 *  DAT0) — "1-bit" MMC mode. This driver speaks that protocol in two layers:
 *
 *    * COMMAND / control phases (init, CMD17/18/24/25 headers, the write-status
 *      token, busy polling) are bit-banged on GPIO. They are short and timing-
 *      insensitive, so simple software toggling is fine.
 *
 *    * The 512-byte DATA payloads — the throughput-critical part — ride the
 *      nRF's SPIM3 SPI engine with DMA at 32 MHz (M32; eMMC default-speed mode
 *      allows up to 26 MHz). This is ~40x faster than bit-banging the data and
 *      frees the CPU during each transfer. SPIM3 is the only SPI instance that
 *      runs above 8 MHz, and is otherwise unused in this firmware.
 *
 *  INTEGRITY: every block read is verified against the card's CRC16, and every
 *  block write checks the card's CRC-status token. On a mismatch the call
 *  returns false and the caller retries — so the fast bus is self-correcting
 *  (emmc_crc_rd_errs / emmc_crc_wr_errs count any catches).
 *
 *  Protocol logic is ported from Tim Knapen's SP-1-dev emmc.c; the SPIM3 data
 *  path and CRC enforcement are additions for the looper's bandwidth needs.
 * ============================================================================
 */
#include "sp1_emmc.h"
#include <zephyr/kernel.h>
#include <soc.h>            /* NRF_P0 register block for the fast data loops */
#include <hal/nrf_gpio.h>
#include <string.h>

/* Pins (from Tim Knapen's stemplayer_pins.h) */
#define PIN_EMMC_CLK   NRF_GPIO_PIN_MAP(0, 6)
#define PIN_EMMC_DAT0  NRF_GPIO_PIN_MAP(0, 7)
#define PIN_EMMC_CMD   NRF_GPIO_PIN_MAP(0, 8)
#define PIN_EMMC_RST   NRF_GPIO_PIN_MAP(1, 8)
#define PIN_EMMC_VCCQ  NRF_GPIO_PIN_MAP(0, 14)

#define CMD_SAFE_HALF_US 1u   /* slow clock for the IDENTIFICATION phase only */

/* Command-phase half-period: starts safe (eMMC identification requires a slow
 * clock), switched to 0 (full-speed bit-bang, ~1-2 MHz) once init completes —
 * the data path already proved the bus at 32 MHz, and each CMD18/CMD25/CMD12
 * handshake at the slow clock cost ~400-600 us of pure overhead per chunk. */
static uint32_t s_cmd_half_us = CMD_SAFE_HALF_US;

volatile uint32_t g_emmc_clk_half_us = CMD_SAFE_HALF_US;

static bool s_ready;
static uint32_t s_rca;

/* diagnostics (see header) */
bool    emmc_dbg_cmd0_sent;
int     emmc_dbg_cmd1_retries = -1;
bool    emmc_dbg_cmd2_resp;
bool    emmc_dbg_cmd3_resp;
bool    emmc_dbg_cmd7_resp;
bool    emmc_dbg_cmd16_resp;
uint8_t emmc_dbg_ocr[6];
uint8_t emmc_dbg_r1[6];
bool    emmc_dbg_last_cmd_resp;
int     emmc_dbg_resp_clocks = -1;   /* clocks until last response start bit */
int     emmc_dbg_cmd2_clocks = -1;   /* same, captured specifically for CMD2 */
int     emmc_dbg_cmd2_tries  = 0;    /* how many CMD2 attempts before a response */
int     emmc_dbg_wr_status   = -1;   /* CRC status token from last block write */
uint16_t emmc_dbg_rd_crc     = 0;    /* CRC16 the card appended to last read block */

#define CLK_HIGH()   nrf_gpio_pin_set(PIN_EMMC_CLK)
#define CLK_LOW()    nrf_gpio_pin_clear(PIN_EMMC_CLK)
#define CMD_HIGH()   nrf_gpio_pin_set(PIN_EMMC_CMD)
#define CMD_LOW()    nrf_gpio_pin_clear(PIN_EMMC_CMD)
#define DAT0_HIGH()  nrf_gpio_pin_set(PIN_EMMC_DAT0)
#define DAT0_LOW()   nrf_gpio_pin_clear(PIN_EMMC_DAT0)
#define DAT0_IN()    nrf_gpio_cfg_input(PIN_EMMC_DAT0, NRF_GPIO_PIN_PULLUP)
/* DAT0 as a HIGH-DRIVE output (H0H1) so writes have fast, clean edges. */
#define DAT0_OUT()   nrf_gpio_cfg(PIN_EMMC_DAT0, NRF_GPIO_PIN_DIR_OUTPUT, \
				  NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL, \
				  NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE)
#define CMD_IN()     nrf_gpio_cfg_input(PIN_EMMC_CMD, NRF_GPIO_PIN_PULLUP)
#define CMD_OUT()    nrf_gpio_cfg_output(PIN_EMMC_CMD)
#define READ_CMD()   nrf_gpio_pin_read(PIN_EMMC_CMD)
#define READ_DAT0()  nrf_gpio_pin_read(PIN_EMMC_DAT0)

/* Direct port-0 register access for the throughput-critical DATA loops.
 * CLK = P0.06 (bit 6), DAT0 = P0.07 (bit 7) — both on GPIO port 0. These are
 * ~3 cycles each vs ~130 for the nrf_gpio HAL calls, which is what pinned the
 * bit-bang at ~58 KB/s. The command/init path keeps the HAL macros above. */
#define P0_CLK_BIT   (1u << 6)
#define P0_DAT_BIT   (1u << 7)
#define RCLK_HIGH()  (NRF_P0->OUTSET = P0_CLK_BIT)
#define RCLK_LOW()   (NRF_P0->OUTCLR = P0_CLK_BIT)
#define RDAT_HIGH()  (NRF_P0->OUTSET = P0_DAT_BIT)
#define RDAT_LOW()   (NRF_P0->OUTCLR = P0_DAT_BIT)
#define RDAT_GET()   ((NRF_P0->IN >> 7) & 1u)
/* CMDFAST-712: the command pin the same way (CMD = P0.08) */
#define P0_CMD_BIT   (1u << 8)
#define RCMD_HIGH()  (NRF_P0->OUTSET = P0_CMD_BIT)
#define RCMD_LOW()   (NRF_P0->OUTCLR = P0_CMD_BIT)
#define RCMD_GET()   ((NRF_P0->IN >> 8) & 1u)
/* A few NOPs of settle after a clock edge for the delay-free (hd==0) path:
 * covers the card's data-output valid time without throttling to a busy-wait. */
#define EDGE_SETTLE() __asm__ volatile("nop\nnop\nnop")
#define HALF(hd)      do { if (hd) { k_busy_wait(hd); } } while (0)

/* ===== SPIM3 hardware-accelerated DATA path ===================================
 * The bit-bang moves data at ~1.3 Mbit/s with the CPU pinned for every bit;
 * SPIM3 + EasyDMA clocks the identical wire format at 32 MHz with the CPU free
 * on the 48 kHz build (the 24 kHz build uses M16).
 * eMMC DAT0 at default speed is SPI-mode-0 compatible: the host launches data
 * while CLK is low, the card samples (and launches) on the rising edge, MSB
 * first. Only the raw 512-byte payloads ride SPIM; commands, start-bit hunts,
 * the CRC-status token and busy polling stay bit-banged (slow, protocol-
 * fiddly, timing-insensitive phases). SPIM3 (0x4002F000) is the only SPIM that
 * supports >8MHz and is otherwise unused (I2C=TWIM0, UARTE1 deleted). */
/* SPIM3 (the only instance with >8MHz) flash clock. eMMC default-speed mode is
 * spec'd to <=26 MHz; the nRF SPIM has no 26 MHz step, so the choices are
 * M16 (16 MHz, in spec) or M32 (32 MHz, slightly over). M32 doubles the data
 * rate -- it halves the per-read data floor AND speeds the record flush,
 * which is what attacks the real 4-stream bottleneck (card-stall + flush-rate),
 * not CPU. It is a calculated overclock: the CRC verify+retry layer catches any
 * signal-integrity errors (watch rerr=/werr= in the diag) and a corrupted write
 * is rejected by the card and retried, so the worst case is throughput loss /
 * retries, never silent corruption. If werr climbs under load, revert to M16.
 * SPIM3 anomaly 198 (TX corruption on concurrent RAM access) is covered by the
 * same integrity layer. */
#define SPIM_FREQ_M16 0x0A000000u
#define SPIM_FREQ_M32 0x14000000u
/* 48 kHz overclocks to 32 MHz for bandwidth; 24 kHz stays at the in-spec 16 MHz
 * (it has the headroom and the overclock was the 24 kHz white-noise cause). */
#if SP1_BUILD_24K
#define SPIM_FREQ_ACTIVE SPIM_FREQ_M16
#else
#define SPIM_FREQ_ACTIVE SPIM_FREQ_M32
#endif
static bool   s_spim_ok;
static uint8_t s_dma_tx[517];        /* FF gap | FE start | 512 data | CRC16  (trailing idle byte intentionally NOT sent — see write_data_block) */
#define s_dma_rx s_dma_tx            /* DMAUNION-746: 512 data | CRC16 read into the write frame's buffer (one eMMC op at a time, all on the streamer) */

static void spim_data_init(void)
{
	NRF_SPIM3->ENABLE    = 0;
	NRF_SPIM3->PSEL.SCK  = PIN_EMMC_CLK;
	NRF_SPIM3->PSEL.MOSI = 0xFFFFFFFFu;  /* attached per-transfer */
	NRF_SPIM3->PSEL.MISO = 0xFFFFFFFFu;
	NRF_SPIM3->PSEL.CSN  = 0xFFFFFFFFu;
	NRF_SPIM3->FREQUENCY = SPIM_FREQ_ACTIVE;
	NRF_SPIM3->CONFIG    = 0;            /* MSB first, CPOL0/CPHA0 (mode 0) */
	NRF_SPIM3->ORC       = 0xFF;         /* idle-high filler */
	s_spim_ok = true;
}

/* One blocking DMA transfer with the wires temporarily owned by SPIM. While
 * ENABLED the peripheral drives SCK (+MOSI for TX) / samples MISO; on disable
 * the pins fall back to their GPIO latches (CLK low, DAT0 as configured), so
 * the surrounding bit-bang phases continue seamlessly. ~65 us per 64 bytes. */
static void spim_xfer(const uint8_t *tx, uint32_t txlen, uint8_t *rx, uint32_t rxlen)
{
	NRF_SPIM3->PSEL.MOSI = tx ? PIN_EMMC_DAT0 : 0xFFFFFFFFu;
	NRF_SPIM3->PSEL.MISO = rx ? PIN_EMMC_DAT0 : 0xFFFFFFFFu;
	NRF_SPIM3->ENABLE    = 7;
	NRF_SPIM3->TXD.PTR    = (uint32_t)tx;
	NRF_SPIM3->TXD.MAXCNT = tx ? txlen : 0;
	NRF_SPIM3->RXD.PTR    = (uint32_t)rx;
	NRF_SPIM3->RXD.MAXCNT = rx ? rxlen : 0;
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->TASKS_START = 1;
	while (!NRF_SPIM3->EVENTS_END) {
		/* ~130 us for a full block at 32 MHz (measured 32.02 Mbit/s) */
	}
	NRF_SPIM3->ENABLE = 0;
}

static inline void half_delay(uint32_t us)
{
	if (us) {
		k_busy_wait(us);
	}
}

/* Safe clock pulse for command/CRC phases. */
static inline void clk_pulse(void)
{
	if (s_cmd_half_us == 0u) {   /* CMDFAST-712: the data path's pulse */
		RCLK_HIGH(); EDGE_SETTLE();
		RCLK_LOW();  EDGE_SETTLE();
		return;
	}
	CLK_HIGH();
	half_delay(s_cmd_half_us);
	CLK_LOW();
	half_delay(s_cmd_half_us);
}

static void cmd_send_bit(uint8_t bit)
{
	/* caller (send_command) sets CMD_OUT() once — reconfiguring the pin
	 * per bit was ~86 redundant HAL calls (~170 us) per command */
	if (bit) {
		CMD_HIGH();
	} else {
		CMD_LOW();
	}
	clk_pulse();
}

static uint8_t cmd_recv_bit(void)
{
	/* caller sets CMD_IN() once before the response read */
	CLK_HIGH();
	half_delay(s_cmd_half_us);
	uint8_t b = (uint8_t)READ_CMD();
	CLK_LOW();
	half_delay(s_cmd_half_us);
	return b;
}

static uint8_t crc7(const uint8_t *data, uint8_t len)
{
	uint8_t crc = 0;
	for (uint8_t i = 0; i < len; i++) {
		uint8_t byte = data[i];
		for (int b = 7; b >= 0; b--) {
			crc <<= 1;
			if (((byte >> b) & 1) ^ ((crc >> 7) & 1)) {
				crc ^= 0x09;
			}
			crc &= 0x7F;
		}
	}
	return (crc << 1) | 1;
}

/* CRC error counters: at 32 MHz SPIM speeds, occasional bit errors are a fact of
 * life on this bus — every read is now verified and every write's CRC-status
 * token is enforced, with the caller retrying. These count the catches. */
volatile uint32_t emmc_crc_rd_errs;
volatile uint32_t emmc_crc_wr_errs;
volatile uint32_t emmc_dbg_wr_busy_max;   /* diag: worst post-write program busy-wait, clk iterations */
/* Wall-clock stall diagnostics (see header). k_cycle_get_32 runs on the 32768 Hz
 * RTC (~30.5 us resolution) — plenty for ms-scale FTL stalls and nearly free. */
volatile uint32_t emmc_dbg_wr_busy_us_max;
volatile uint32_t emmc_dbg_wr_busy_us_peak;
volatile uint32_t emmc_dbg_rd_wait_us_max;
volatile uint32_t emmc_dbg_switch_busy_us_max;
volatile uint32_t emmc_dbg_busy_timeouts;
bool emmc_spim_active(void) { return s_spim_ok; }  /* diag: 32MHz SPIM3 DMA live vs slow bit-bang fallback */

/* Table-driven CRC16-CCITT: the bitwise version costs ~14% CPU at the 48 kHz
 * read rate; the table costs ~1%. Built once at init. */
static const uint16_t s_crc16_tab[256] = {   /* CRCFLASH-745: precomputed, CRC-16/XMODEM 0x1021 */
	0x0000u, 0x1021u, 0x2042u, 0x3063u, 0x4084u, 0x50A5u, 0x60C6u, 0x70E7u,
	0x8108u, 0x9129u, 0xA14Au, 0xB16Bu, 0xC18Cu, 0xD1ADu, 0xE1CEu, 0xF1EFu,
	0x1231u, 0x0210u, 0x3273u, 0x2252u, 0x52B5u, 0x4294u, 0x72F7u, 0x62D6u,
	0x9339u, 0x8318u, 0xB37Bu, 0xA35Au, 0xD3BDu, 0xC39Cu, 0xF3FFu, 0xE3DEu,
	0x2462u, 0x3443u, 0x0420u, 0x1401u, 0x64E6u, 0x74C7u, 0x44A4u, 0x5485u,
	0xA56Au, 0xB54Bu, 0x8528u, 0x9509u, 0xE5EEu, 0xF5CFu, 0xC5ACu, 0xD58Du,
	0x3653u, 0x2672u, 0x1611u, 0x0630u, 0x76D7u, 0x66F6u, 0x5695u, 0x46B4u,
	0xB75Bu, 0xA77Au, 0x9719u, 0x8738u, 0xF7DFu, 0xE7FEu, 0xD79Du, 0xC7BCu,
	0x48C4u, 0x58E5u, 0x6886u, 0x78A7u, 0x0840u, 0x1861u, 0x2802u, 0x3823u,
	0xC9CCu, 0xD9EDu, 0xE98Eu, 0xF9AFu, 0x8948u, 0x9969u, 0xA90Au, 0xB92Bu,
	0x5AF5u, 0x4AD4u, 0x7AB7u, 0x6A96u, 0x1A71u, 0x0A50u, 0x3A33u, 0x2A12u,
	0xDBFDu, 0xCBDCu, 0xFBBFu, 0xEB9Eu, 0x9B79u, 0x8B58u, 0xBB3Bu, 0xAB1Au,
	0x6CA6u, 0x7C87u, 0x4CE4u, 0x5CC5u, 0x2C22u, 0x3C03u, 0x0C60u, 0x1C41u,
	0xEDAEu, 0xFD8Fu, 0xCDECu, 0xDDCDu, 0xAD2Au, 0xBD0Bu, 0x8D68u, 0x9D49u,
	0x7E97u, 0x6EB6u, 0x5ED5u, 0x4EF4u, 0x3E13u, 0x2E32u, 0x1E51u, 0x0E70u,
	0xFF9Fu, 0xEFBEu, 0xDFDDu, 0xCFFCu, 0xBF1Bu, 0xAF3Au, 0x9F59u, 0x8F78u,
	0x9188u, 0x81A9u, 0xB1CAu, 0xA1EBu, 0xD10Cu, 0xC12Du, 0xF14Eu, 0xE16Fu,
	0x1080u, 0x00A1u, 0x30C2u, 0x20E3u, 0x5004u, 0x4025u, 0x7046u, 0x6067u,
	0x83B9u, 0x9398u, 0xA3FBu, 0xB3DAu, 0xC33Du, 0xD31Cu, 0xE37Fu, 0xF35Eu,
	0x02B1u, 0x1290u, 0x22F3u, 0x32D2u, 0x4235u, 0x5214u, 0x6277u, 0x7256u,
	0xB5EAu, 0xA5CBu, 0x95A8u, 0x8589u, 0xF56Eu, 0xE54Fu, 0xD52Cu, 0xC50Du,
	0x34E2u, 0x24C3u, 0x14A0u, 0x0481u, 0x7466u, 0x6447u, 0x5424u, 0x4405u,
	0xA7DBu, 0xB7FAu, 0x8799u, 0x97B8u, 0xE75Fu, 0xF77Eu, 0xC71Du, 0xD73Cu,
	0x26D3u, 0x36F2u, 0x0691u, 0x16B0u, 0x6657u, 0x7676u, 0x4615u, 0x5634u,
	0xD94Cu, 0xC96Du, 0xF90Eu, 0xE92Fu, 0x99C8u, 0x89E9u, 0xB98Au, 0xA9ABu,
	0x5844u, 0x4865u, 0x7806u, 0x6827u, 0x18C0u, 0x08E1u, 0x3882u, 0x28A3u,
	0xCB7Du, 0xDB5Cu, 0xEB3Fu, 0xFB1Eu, 0x8BF9u, 0x9BD8u, 0xABBBu, 0xBB9Au,
	0x4A75u, 0x5A54u, 0x6A37u, 0x7A16u, 0x0AF1u, 0x1AD0u, 0x2AB3u, 0x3A92u,
	0xFD2Eu, 0xED0Fu, 0xDD6Cu, 0xCD4Du, 0xBDAAu, 0xAD8Bu, 0x9DE8u, 0x8DC9u,
	0x7C26u, 0x6C07u, 0x5C64u, 0x4C45u, 0x3CA2u, 0x2C83u, 0x1CE0u, 0x0CC1u,
	0xEF1Fu, 0xFF3Eu, 0xCF5Du, 0xDF7Cu, 0xAF9Bu, 0xBFBAu, 0x8FD9u, 0x9FF8u,
	0x6E17u, 0x7E36u, 0x4E55u, 0x5E74u, 0x2E93u, 0x3EB2u, 0x0ED1u, 0x1EF0u,
};
/* SLICE-BY-4: three companion tables. Each one advances a byte further
 * than the last, so crc16() can fold four bytes in one pass. The tables
 * are built at init from the same polynomial, so no constants go in
 * flash and the result is bit-identical to the byte-at-a-time version.
 *
 * WHY: a thread census found the processor saturated at high tape speed
 * (0% idle). CRC16 cost 0.34 ms of the 0.81 ms spent on each block read.
 * That is 42%, the largest single item in the read path, and it is pure
 * computation that this firmware owns. The change took the same bench
 * routine from 122 starve events to 0.
 *
 * VERIFIED off-target before the first build: all 65,536 two-byte inputs,
 * 280,000 random cases at every unaligned offset, and the CRC-16/XMODEM
 * known answer ("123456789" = 0x31c3). Verified on hardware by rerr=0
 * and werr=0 across a full bench run. A wrong CRC cannot hide. It shows
 * up at once as a storm of retries.
 *
 * COST: 1,536 bytes of RAM. */
static const uint16_t s_crc16_t1[256] = {   /* CRCFLASH-745: precomputed, CRC-16/XMODEM 0x1021 */
	0x0000u, 0x3331u, 0x6662u, 0x5553u, 0xCCC4u, 0xFFF5u, 0xAAA6u, 0x9997u,
	0x89A9u, 0xBA98u, 0xEFCBu, 0xDCFAu, 0x456Du, 0x765Cu, 0x230Fu, 0x103Eu,
	0x0373u, 0x3042u, 0x6511u, 0x5620u, 0xCFB7u, 0xFC86u, 0xA9D5u, 0x9AE4u,
	0x8ADAu, 0xB9EBu, 0xECB8u, 0xDF89u, 0x461Eu, 0x752Fu, 0x207Cu, 0x134Du,
	0x06E6u, 0x35D7u, 0x6084u, 0x53B5u, 0xCA22u, 0xF913u, 0xAC40u, 0x9F71u,
	0x8F4Fu, 0xBC7Eu, 0xE92Du, 0xDA1Cu, 0x438Bu, 0x70BAu, 0x25E9u, 0x16D8u,
	0x0595u, 0x36A4u, 0x63F7u, 0x50C6u, 0xC951u, 0xFA60u, 0xAF33u, 0x9C02u,
	0x8C3Cu, 0xBF0Du, 0xEA5Eu, 0xD96Fu, 0x40F8u, 0x73C9u, 0x269Au, 0x15ABu,
	0x0DCCu, 0x3EFDu, 0x6BAEu, 0x589Fu, 0xC108u, 0xF239u, 0xA76Au, 0x945Bu,
	0x8465u, 0xB754u, 0xE207u, 0xD136u, 0x48A1u, 0x7B90u, 0x2EC3u, 0x1DF2u,
	0x0EBFu, 0x3D8Eu, 0x68DDu, 0x5BECu, 0xC27Bu, 0xF14Au, 0xA419u, 0x9728u,
	0x8716u, 0xB427u, 0xE174u, 0xD245u, 0x4BD2u, 0x78E3u, 0x2DB0u, 0x1E81u,
	0x0B2Au, 0x381Bu, 0x6D48u, 0x5E79u, 0xC7EEu, 0xF4DFu, 0xA18Cu, 0x92BDu,
	0x8283u, 0xB1B2u, 0xE4E1u, 0xD7D0u, 0x4E47u, 0x7D76u, 0x2825u, 0x1B14u,
	0x0859u, 0x3B68u, 0x6E3Bu, 0x5D0Au, 0xC49Du, 0xF7ACu, 0xA2FFu, 0x91CEu,
	0x81F0u, 0xB2C1u, 0xE792u, 0xD4A3u, 0x4D34u, 0x7E05u, 0x2B56u, 0x1867u,
	0x1B98u, 0x28A9u, 0x7DFAu, 0x4ECBu, 0xD75Cu, 0xE46Du, 0xB13Eu, 0x820Fu,
	0x9231u, 0xA100u, 0xF453u, 0xC762u, 0x5EF5u, 0x6DC4u, 0x3897u, 0x0BA6u,
	0x18EBu, 0x2BDAu, 0x7E89u, 0x4DB8u, 0xD42Fu, 0xE71Eu, 0xB24Du, 0x817Cu,
	0x9142u, 0xA273u, 0xF720u, 0xC411u, 0x5D86u, 0x6EB7u, 0x3BE4u, 0x08D5u,
	0x1D7Eu, 0x2E4Fu, 0x7B1Cu, 0x482Du, 0xD1BAu, 0xE28Bu, 0xB7D8u, 0x84E9u,
	0x94D7u, 0xA7E6u, 0xF2B5u, 0xC184u, 0x5813u, 0x6B22u, 0x3E71u, 0x0D40u,
	0x1E0Du, 0x2D3Cu, 0x786Fu, 0x4B5Eu, 0xD2C9u, 0xE1F8u, 0xB4ABu, 0x879Au,
	0x97A4u, 0xA495u, 0xF1C6u, 0xC2F7u, 0x5B60u, 0x6851u, 0x3D02u, 0x0E33u,
	0x1654u, 0x2565u, 0x7036u, 0x4307u, 0xDA90u, 0xE9A1u, 0xBCF2u, 0x8FC3u,
	0x9FFDu, 0xACCCu, 0xF99Fu, 0xCAAEu, 0x5339u, 0x6008u, 0x355Bu, 0x066Au,
	0x1527u, 0x2616u, 0x7345u, 0x4074u, 0xD9E3u, 0xEAD2u, 0xBF81u, 0x8CB0u,
	0x9C8Eu, 0xAFBFu, 0xFAECu, 0xC9DDu, 0x504Au, 0x637Bu, 0x3628u, 0x0519u,
	0x10B2u, 0x2383u, 0x76D0u, 0x45E1u, 0xDC76u, 0xEF47u, 0xBA14u, 0x8925u,
	0x991Bu, 0xAA2Au, 0xFF79u, 0xCC48u, 0x55DFu, 0x66EEu, 0x33BDu, 0x008Cu,
	0x13C1u, 0x20F0u, 0x75A3u, 0x4692u, 0xDF05u, 0xEC34u, 0xB967u, 0x8A56u,
	0x9A68u, 0xA959u, 0xFC0Au, 0xCF3Bu, 0x56ACu, 0x659Du, 0x30CEu, 0x03FFu,
};
static const uint16_t s_crc16_t2[256] = {   /* CRCFLASH-745: precomputed, CRC-16/XMODEM 0x1021 */
	0x0000u, 0x3730u, 0x6E60u, 0x5950u, 0xDCC0u, 0xEBF0u, 0xB2A0u, 0x8590u,
	0xA9A1u, 0x9E91u, 0xC7C1u, 0xF0F1u, 0x7561u, 0x4251u, 0x1B01u, 0x2C31u,
	0x4363u, 0x7453u, 0x2D03u, 0x1A33u, 0x9FA3u, 0xA893u, 0xF1C3u, 0xC6F3u,
	0xEAC2u, 0xDDF2u, 0x84A2u, 0xB392u, 0x3602u, 0x0132u, 0x5862u, 0x6F52u,
	0x86C6u, 0xB1F6u, 0xE8A6u, 0xDF96u, 0x5A06u, 0x6D36u, 0x3466u, 0x0356u,
	0x2F67u, 0x1857u, 0x4107u, 0x7637u, 0xF3A7u, 0xC497u, 0x9DC7u, 0xAAF7u,
	0xC5A5u, 0xF295u, 0xABC5u, 0x9CF5u, 0x1965u, 0x2E55u, 0x7705u, 0x4035u,
	0x6C04u, 0x5B34u, 0x0264u, 0x3554u, 0xB0C4u, 0x87F4u, 0xDEA4u, 0xE994u,
	0x1DADu, 0x2A9Du, 0x73CDu, 0x44FDu, 0xC16Du, 0xF65Du, 0xAF0Du, 0x983Du,
	0xB40Cu, 0x833Cu, 0xDA6Cu, 0xED5Cu, 0x68CCu, 0x5FFCu, 0x06ACu, 0x319Cu,
	0x5ECEu, 0x69FEu, 0x30AEu, 0x079Eu, 0x820Eu, 0xB53Eu, 0xEC6Eu, 0xDB5Eu,
	0xF76Fu, 0xC05Fu, 0x990Fu, 0xAE3Fu, 0x2BAFu, 0x1C9Fu, 0x45CFu, 0x72FFu,
	0x9B6Bu, 0xAC5Bu, 0xF50Bu, 0xC23Bu, 0x47ABu, 0x709Bu, 0x29CBu, 0x1EFBu,
	0x32CAu, 0x05FAu, 0x5CAAu, 0x6B9Au, 0xEE0Au, 0xD93Au, 0x806Au, 0xB75Au,
	0xD808u, 0xEF38u, 0xB668u, 0x8158u, 0x04C8u, 0x33F8u, 0x6AA8u, 0x5D98u,
	0x71A9u, 0x4699u, 0x1FC9u, 0x28F9u, 0xAD69u, 0x9A59u, 0xC309u, 0xF439u,
	0x3B5Au, 0x0C6Au, 0x553Au, 0x620Au, 0xE79Au, 0xD0AAu, 0x89FAu, 0xBECAu,
	0x92FBu, 0xA5CBu, 0xFC9Bu, 0xCBABu, 0x4E3Bu, 0x790Bu, 0x205Bu, 0x176Bu,
	0x7839u, 0x4F09u, 0x1659u, 0x2169u, 0xA4F9u, 0x93C9u, 0xCA99u, 0xFDA9u,
	0xD198u, 0xE6A8u, 0xBFF8u, 0x88C8u, 0x0D58u, 0x3A68u, 0x6338u, 0x5408u,
	0xBD9Cu, 0x8AACu, 0xD3FCu, 0xE4CCu, 0x615Cu, 0x566Cu, 0x0F3Cu, 0x380Cu,
	0x143Du, 0x230Du, 0x7A5Du, 0x4D6Du, 0xC8FDu, 0xFFCDu, 0xA69Du, 0x91ADu,
	0xFEFFu, 0xC9CFu, 0x909Fu, 0xA7AFu, 0x223Fu, 0x150Fu, 0x4C5Fu, 0x7B6Fu,
	0x575Eu, 0x606Eu, 0x393Eu, 0x0E0Eu, 0x8B9Eu, 0xBCAEu, 0xE5FEu, 0xD2CEu,
	0x26F7u, 0x11C7u, 0x4897u, 0x7FA7u, 0xFA37u, 0xCD07u, 0x9457u, 0xA367u,
	0x8F56u, 0xB866u, 0xE136u, 0xD606u, 0x5396u, 0x64A6u, 0x3DF6u, 0x0AC6u,
	0x6594u, 0x52A4u, 0x0BF4u, 0x3CC4u, 0xB954u, 0x8E64u, 0xD734u, 0xE004u,
	0xCC35u, 0xFB05u, 0xA255u, 0x9565u, 0x10F5u, 0x27C5u, 0x7E95u, 0x49A5u,
	0xA031u, 0x9701u, 0xCE51u, 0xF961u, 0x7CF1u, 0x4BC1u, 0x1291u, 0x25A1u,
	0x0990u, 0x3EA0u, 0x67F0u, 0x50C0u, 0xD550u, 0xE260u, 0xBB30u, 0x8C00u,
	0xE352u, 0xD462u, 0x8D32u, 0xBA02u, 0x3F92u, 0x08A2u, 0x51F2u, 0x66C2u,
	0x4AF3u, 0x7DC3u, 0x2493u, 0x13A3u, 0x9633u, 0xA103u, 0xF853u, 0xCF63u,
};
static const uint16_t s_crc16_t3[256] = {   /* CRCFLASH-745: precomputed, CRC-16/XMODEM 0x1021 */
	0x0000u, 0x76B4u, 0xED68u, 0x9BDCu, 0xCAF1u, 0xBC45u, 0x2799u, 0x512Du,
	0x85C3u, 0xF377u, 0x68ABu, 0x1E1Fu, 0x4F32u, 0x3986u, 0xA25Au, 0xD4EEu,
	0x1BA7u, 0x6D13u, 0xF6CFu, 0x807Bu, 0xD156u, 0xA7E2u, 0x3C3Eu, 0x4A8Au,
	0x9E64u, 0xE8D0u, 0x730Cu, 0x05B8u, 0x5495u, 0x2221u, 0xB9FDu, 0xCF49u,
	0x374Eu, 0x41FAu, 0xDA26u, 0xAC92u, 0xFDBFu, 0x8B0Bu, 0x10D7u, 0x6663u,
	0xB28Du, 0xC439u, 0x5FE5u, 0x2951u, 0x787Cu, 0x0EC8u, 0x9514u, 0xE3A0u,
	0x2CE9u, 0x5A5Du, 0xC181u, 0xB735u, 0xE618u, 0x90ACu, 0x0B70u, 0x7DC4u,
	0xA92Au, 0xDF9Eu, 0x4442u, 0x32F6u, 0x63DBu, 0x156Fu, 0x8EB3u, 0xF807u,
	0x6E9Cu, 0x1828u, 0x83F4u, 0xF540u, 0xA46Du, 0xD2D9u, 0x4905u, 0x3FB1u,
	0xEB5Fu, 0x9DEBu, 0x0637u, 0x7083u, 0x21AEu, 0x571Au, 0xCCC6u, 0xBA72u,
	0x753Bu, 0x038Fu, 0x9853u, 0xEEE7u, 0xBFCAu, 0xC97Eu, 0x52A2u, 0x2416u,
	0xF0F8u, 0x864Cu, 0x1D90u, 0x6B24u, 0x3A09u, 0x4CBDu, 0xD761u, 0xA1D5u,
	0x59D2u, 0x2F66u, 0xB4BAu, 0xC20Eu, 0x9323u, 0xE597u, 0x7E4Bu, 0x08FFu,
	0xDC11u, 0xAAA5u, 0x3179u, 0x47CDu, 0x16E0u, 0x6054u, 0xFB88u, 0x8D3Cu,
	0x4275u, 0x34C1u, 0xAF1Du, 0xD9A9u, 0x8884u, 0xFE30u, 0x65ECu, 0x1358u,
	0xC7B6u, 0xB102u, 0x2ADEu, 0x5C6Au, 0x0D47u, 0x7BF3u, 0xE02Fu, 0x969Bu,
	0xDD38u, 0xAB8Cu, 0x3050u, 0x46E4u, 0x17C9u, 0x617Du, 0xFAA1u, 0x8C15u,
	0x58FBu, 0x2E4Fu, 0xB593u, 0xC327u, 0x920Au, 0xE4BEu, 0x7F62u, 0x09D6u,
	0xC69Fu, 0xB02Bu, 0x2BF7u, 0x5D43u, 0x0C6Eu, 0x7ADAu, 0xE106u, 0x97B2u,
	0x435Cu, 0x35E8u, 0xAE34u, 0xD880u, 0x89ADu, 0xFF19u, 0x64C5u, 0x1271u,
	0xEA76u, 0x9CC2u, 0x071Eu, 0x71AAu, 0x2087u, 0x5633u, 0xCDEFu, 0xBB5Bu,
	0x6FB5u, 0x1901u, 0x82DDu, 0xF469u, 0xA544u, 0xD3F0u, 0x482Cu, 0x3E98u,
	0xF1D1u, 0x8765u, 0x1CB9u, 0x6A0Du, 0x3B20u, 0x4D94u, 0xD648u, 0xA0FCu,
	0x7412u, 0x02A6u, 0x997Au, 0xEFCEu, 0xBEE3u, 0xC857u, 0x538Bu, 0x253Fu,
	0xB3A4u, 0xC510u, 0x5ECCu, 0x2878u, 0x7955u, 0x0FE1u, 0x943Du, 0xE289u,
	0x3667u, 0x40D3u, 0xDB0Fu, 0xADBBu, 0xFC96u, 0x8A22u, 0x11FEu, 0x674Au,
	0xA803u, 0xDEB7u, 0x456Bu, 0x33DFu, 0x62F2u, 0x1446u, 0x8F9Au, 0xF92Eu,
	0x2DC0u, 0x5B74u, 0xC0A8u, 0xB61Cu, 0xE731u, 0x9185u, 0x0A59u, 0x7CEDu,
	0x84EAu, 0xF25Eu, 0x6982u, 0x1F36u, 0x4E1Bu, 0x38AFu, 0xA373u, 0xD5C7u,
	0x0129u, 0x779Du, 0xEC41u, 0x9AF5u, 0xCBD8u, 0xBD6Cu, 0x26B0u, 0x5004u,
	0x9F4Du, 0xE9F9u, 0x7225u, 0x0491u, 0x55BCu, 0x2308u, 0xB8D4u, 0xCE60u,
	0x1A8Eu, 0x6C3Au, 0xF7E6u, 0x8152u, 0xD07Fu, 0xA6CBu, 0x3D17u, 0x4BA3u,
};
static void crc16_tab_init(void)
{
	/* CRCFLASH-745: the tables are const in flash now; nothing to build. */
}
/* READ-ONLY -O2: -O2 just this CRC + the read bit-bang below. -O2 is proven safe
 * for the flash READ path (read CRC errors stayed 0) but BREAKS the write bit-bang
 * (write_data_block stays at the file's -Os). crc16 is pure computation, so -O2
 * only speeds it -- the value (used by writes too) is unchanged. */
__attribute__((optimize("O2")))
static uint16_t crc16(const uint8_t *data, uint32_t len)
{
	/* CRC4-625 (W302): four 4-byte steps per loop and a 32-bit running
	 * value. The four table entries are 16-bit, so their XOR never exceeds
	 * 16 bits and (crc << 16) needs no mask -- the per-step uxth is gone
	 * with three of every four compare+branch pairs. Bit-identical to the
	 * slice-by-4 loop it replaces (proven on the host in the build). */
	uint32_t crc = 0;
	uint32_t i = 0;
#define CRC16_S4(off) do { \
		uint32_t _w; __builtin_memcpy(&_w, data + i + (off), 4); \
		uint32_t _t = (crc << 16) ^ __builtin_bswap32(_w); \
		crc = (uint32_t)s_crc16_t3[(_t >> 24) & 0xFFu] ^ (uint32_t)s_crc16_t2[(_t >> 16) & 0xFFu] ^ \
		      (uint32_t)s_crc16_t1[(_t >>  8) & 0xFFu] ^ (uint32_t)s_crc16_tab[_t & 0xFFu]; \
	} while (0)
	while (len - i >= 16u) {
		CRC16_S4(0u); CRC16_S4(4u); CRC16_S4(8u); CRC16_S4(12u);
		i += 16u;
	}
	while (len - i >= 4u) { CRC16_S4(0u); i += 4u; }
#undef CRC16_S4
	for (; i < len; i++)
		crc = (uint32_t)(uint16_t)((crc << 8) ^ s_crc16_tab[((crc >> 8) ^ data[i]) & 0xFFu]);
	return (uint16_t)crc;
}

/* CMDFAST-712: the full-speed command -- the same frame, clocks and sampling points as
 * send_command() below, through the port registers. Only after identification. */
static bool send_command_fast(uint8_t cmd_index, uint32_t arg, uint8_t *r1_out)
{
	uint8_t frame[6];
	frame[0] = 0x40 | (cmd_index & 0x3F);      /* start 0, transmission 1, index */
	frame[1] = (uint8_t)(arg >> 24);
	frame[2] = (uint8_t)(arg >> 16);
	frame[3] = (uint8_t)(arg >> 8);
	frame[4] = (uint8_t)(arg);
	frame[5] = crc7(frame, 5);                  /* (crc << 1) | 1 : the end bit is in it */

	CMD_IN();
	for (int i = 0; i < 24; i++) { RCLK_HIGH(); EDGE_SETTLE(); RCLK_LOW(); EDGE_SETTLE(); }
	CMD_OUT();
	for (int i = 0; i < 6; i++) {
		const uint32_t by = frame[i];
		for (int b = 7; b >= 0; b--) {
			if ((by >> b) & 1u) RCMD_HIGH(); else RCMD_LOW();
			RCLK_HIGH(); EDGE_SETTLE();
			RCLK_LOW();  EDGE_SETTLE();
		}
	}

	CMD_IN();
	emmc_dbg_resp_clocks = -1;
	for (int t = 0; t < 200; t++) {
		RCLK_HIGH(); EDGE_SETTLE();
		RCLK_LOW();  EDGE_SETTLE();
		if (!RCMD_GET()) {                      /* sampled after the pulse, as before */
			emmc_dbg_resp_clocks = t;
			break;
		}
	}
	if (emmc_dbg_resp_clocks < 0) {
		return false;
	}

	if (!r1_out) {
		return true;
	}

	uint8_t resp[6] = {0};
	for (int i = 0; i < 38; i++) {
		RCLK_HIGH(); EDGE_SETTLE();
		const uint8_t bit = (uint8_t)RCMD_GET();   /* after the rising edge, as cmd_recv_bit */
		RCLK_LOW();  EDGE_SETTLE();
		resp[i / 8] |= (uint8_t)(bit << (7 - (i % 8)));
	}
	memcpy(r1_out, resp, 6);

	return true;
}

static bool send_command(uint8_t cmd_index, uint32_t arg, uint8_t *r1_out)
{
	if (s_cmd_half_us == 0u)
		return send_command_fast(cmd_index, arg, r1_out);   /* CMDFAST-712 */
	uint8_t frame[6];
	frame[0] = 0x40 | (cmd_index & 0x3F);
	frame[1] = (uint8_t)(arg >> 24);
	frame[2] = (uint8_t)(arg >> 16);
	frame[3] = (uint8_t)(arg >> 8);
	frame[4] = (uint8_t)(arg);
	frame[5] = crc7(frame, 5);

	/* PRE-COMMAND GAP on an UNDRIVEN line: a 48-bit response minus the
	 * hunted start bit and the 38 bits read leaves ~9 bits the card is
	 * still driving after the previous command, and JEDEC Nrc wants >=8
	 * more clocks before the next command. The old code gave 8 clocks
	 * while DRIVING the line push-pull into the card's final bits — the
	 * measured ~30/s response misses under load. Clocking the gap HERE
	 * (line released, pulled up) gives the card its tail + Nrc with no
	 * contention, and never touches a read's data phase the way a post-
	 * response trailer does. */
	CMD_IN();
	for (int i = 0; i < 24; i++) {
		clk_pulse();
	}
	CMD_OUT();
	cmd_send_bit(0);
	cmd_send_bit(1);
	for (int b = 5; b >= 0; b--) {
		cmd_send_bit((frame[0] >> b) & 1);
	}
	for (int i = 1; i <= 4; i++) {
		for (int b = 7; b >= 0; b--) {
			cmd_send_bit((frame[i] >> b) & 1);
		}
	}
	for (int b = 7; b >= 1; b--) {
		cmd_send_bit((frame[5] >> b) & 1);
	}
	cmd_send_bit(1);

	CMD_IN();
	emmc_dbg_resp_clocks = -1;
	for (int t = 0; t < 200; t++) {
		clk_pulse();
		if (!READ_CMD()) {
			emmc_dbg_resp_clocks = t;
			break;
		}
	}
	if (emmc_dbg_resp_clocks < 0) {
		return false;
	}

	if (!r1_out) {
		return true;
	}

	uint8_t resp[6] = {0};
	for (int i = 0; i < 38; i++) {
		uint8_t bit = cmd_recv_bit();
		resp[i / 8] |= (bit << (7 - (i % 8)));
	}
	memcpy(r1_out, resp, 6);

	/* Leave CMD as an INPUT (pulled up) — no trailer clocks here! A read
	 * command's data block can start on DAT0 within ~2 clocks of the
	 * response ending, so ANY post-response clocking eats the data token
	 * and misframes the whole payload (hardware-confirmed: a 32-clock
	 * trailer here CRC-failed every burst read = total silence). The
	 * response tail + inter-command Nrc gap are honoured by the PRE-command
	 * gap at the top of this function instead. */
	return true;
}

/* Bit-banged MMC commands intermittently miss the response on the first try
 * (settling after the previous command); retry until the card answers. */
volatile uint32_t emmc_dbg_cmd_retries;   /* first-try misses recovered (diag) */
static bool send_command_retry(uint8_t cmd, uint32_t arg, uint8_t *r1_out, int tries)
{
	for (int t = 0; t < tries; t++) {
		if (send_command(cmd, arg, r1_out)) {
			return true;
		}
		emmc_dbg_cmd_retries++;
		if (t == 0) {
			/* First miss = the card still settling after the previous
			 * burst: a handful of idle clocks is all it needs. NEVER
			 * sleep here — a 2 ms nap per miss at the measured ~50
			 * misses/s during 4-track+record donated ~10% of the CPU
			 * and stalled the refill pipeline (the residual cut-outs
			 * after every other layer was fixed). */
			for (int c = 0; c < 16; c++) {
				clk_pulse();
			}
		} else {
			k_msleep(2);
		}
	}
	return false;
}

/* DATA read: per-bit CLK toggle uses the configurable (possibly 0) half-period. */
__attribute__((optimize("O2")))   /* read path only: -O2 safe for reads, NOT writes */
static bool read_data_block(uint8_t *buf)
{
	const uint32_t hd = g_emmc_clk_half_us;

	DAT0_IN();
	/* START-BIT HUNT, TIME-BASED (was 10k iterations ~ 1-5 ms): a GC-busy card
	 * can legitimately delay the first data token 100+ ms (this part declares
	 * NO minimum write performance, so stalls are spec-unbounded). The old
	 * short hunt false-failed the whole CMD18 mid-stall and the retry just
	 * re-queued against the same busy card, collapsing play refill exactly
	 * when the rings were draining. Waiting inside ONE command delivers data
	 * the instant the card frees up. The card only advances its output on OUR
	 * clock edges, so pausing the clock to yield can never miss the token. */
	{
		uint32_t t0 = k_cycle_get_32();
		const uint32_t lim = k_us_to_cyc_ceil32(80000u);    /* 80 ms bound: rides real
		                                                     * GC read delays, but caps the
		                                                     * worst CMD18 at ~150+80 ms —
		                                                     * under the 341 ms rec horizon */
		const uint32_t yield_at = k_us_to_cyc_ceil32(500u);
		bool got_start = false;
		for (;;) {
			for (int burst = 0; burst < 64 && !got_start; burst++) {
				RCLK_HIGH();
				HALF(hd); EDGE_SETTLE();
				if (!RDAT_GET()) {
					got_start = true;    /* leave with RCLK HIGH (as before) */
					break;
				}
				RCLK_LOW();
				HALF(hd);
			}
			uint32_t el = k_cycle_get_32() - t0;
			if (got_start) {
				uint32_t us = k_cyc_to_us_floor32(el);
				if (us > emmc_dbg_rd_wait_us_max) emmc_dbg_rd_wait_us_max = us;
				break;
			}
			if (el >= lim) {
				emmc_dbg_busy_timeouts++;
				return false;
			}
			if (el >= yield_at)
				k_usleep(50);   /* long stall: let MIDI/main breathe */
		}
	}
	RCLK_LOW();
	HALF(hd);

	if (s_spim_ok) {
		/* FAST PATH: the start bit was just consumed by the bit-bang hunt
		 * above, so the remaining 512 data bytes + CRC16 are exactly byte-
		 * aligned — one 32 MHz SPIM RX DMA (~130 us vs ~1.7 ms bit-banged). */
		spim_xfer(NULL, 0, s_dma_rx, EMMC_BLOCK_SIZE + 2u);   /* DMAUNION-746: was sizeof(s_dma_rx) = 514 */
		memcpy(buf, s_dma_rx, EMMC_BLOCK_SIZE);
		emmc_dbg_rd_crc = (uint16_t)(((uint16_t)s_dma_rx[EMMC_BLOCK_SIZE] << 8) |
					     s_dma_rx[EMMC_BLOCK_SIZE + 1]);
		RCLK_HIGH(); HALF(hd); RCLK_LOW(); HALF(hd);  /* end bit */
		if (crc16(buf, EMMC_BLOCK_SIZE) != emmc_dbg_rd_crc) {
			emmc_crc_rd_errs++;          /* corrupt read: caller retries */
			DAT0_OUT();
			DAT0_HIGH();
			return false;
		}
	} else {
		for (uint32_t i = 0; i < EMMC_BLOCK_SIZE; i++) {
			uint8_t byte = 0;
			for (int b = 7; b >= 0; b--) {
				RCLK_HIGH();
				HALF(hd); EDGE_SETTLE();
				byte |= (uint8_t)(RDAT_GET() << b);
				RCLK_LOW();
				HALF(hd);
			}
			buf[i] = byte;
		}

		uint16_t rdcrc = 0;     /* capture the card's CRC16 to validate ours */
		for (int i = 0; i < 16; i++) {
			RCLK_HIGH(); HALF(hd); EDGE_SETTLE();
			rdcrc = (uint16_t)((rdcrc << 1) | RDAT_GET());
			RCLK_LOW(); HALF(hd);
		}
		emmc_dbg_rd_crc = rdcrc;
		RCLK_HIGH(); HALF(hd); RCLK_LOW(); HALF(hd);  /* end bit */
	}

	DAT0_OUT();
	DAT0_HIGH();
	return true;
}

bool emmc_cmd13(uint8_t *r1_out)
{
	return send_command_retry(13, s_rca, r1_out, 8);
}

static bool write_data_block(const uint8_t *buf)
{
	const uint32_t hd = g_emmc_clk_half_us;

	/* Write convention: change DAT0 while CLK is LOW, then a full half-period of
	 * setup before the rising edge where the card latches it.
	 * First hold DAT0 idle-HIGH for several clocks (the Nwr gap) so the card does
	 * NOT mistake a stray low for an early start bit and mis-frame the token. */
	DAT0_OUT();
	RDAT_HIGH();
	if (s_spim_ok) {
		/* FAST PATH: the whole framed block — Nwr gap, start bit, 512 data
		 * bytes, CRC16, end bit — as one 32 MHz SPIM DMA burst (~130 us vs
		 * ~3.3 ms bit-banged, with the CPU free for the audio engine). */
		uint16_t crc = crc16(buf, EMMC_BLOCK_SIZE);
		s_dma_tx[0] = 0xFF;                       /* Nwr idle gap          */
		s_dma_tx[1] = 0xFE;                       /* 7 idle bits + START 0 */
		memcpy(&s_dma_tx[2], buf, EMMC_BLOCK_SIZE);
		s_dma_tx[2 + EMMC_BLOCK_SIZE]     = (uint8_t)(crc >> 8);
		s_dma_tx[2 + EMMC_BLOCK_SIZE + 1] = (uint8_t)crc;
		RCLK_LOW();
		/* TX ends EXACTLY at the CRC's last bit — no trailing idle byte!
		 * The card emits its CRC-status token 2 clocks after the END bit;
		 * a byte of idle clocks inside the DMA let the token fly by before
		 * the bit-bang hunt below ever looked (status read as garbage —
		 * which froze recording solid once the status became enforced).
		 * The END bit is clocked by hand right after, on time. */
		spim_xfer(s_dma_tx, 2 + EMMC_BLOCK_SIZE + 2, NULL, 0);
		/* END bit: DAT0 is back at its GPIO latch (output HIGH) — clock it */
		HALF(hd); EDGE_SETTLE();
		RCLK_HIGH(); HALF(hd); RCLK_LOW();
	} else {
		for (int i = 0; i < 8; i++) { HALF(hd); RCLK_HIGH(); HALF(hd); RCLK_LOW(); }
		RDAT_LOW();
		HALF(hd); EDGE_SETTLE();
		RCLK_HIGH(); HALF(hd); RCLK_LOW();            /* start bit */

		for (uint32_t i = 0; i < EMMC_BLOCK_SIZE; i++) {
			uint8_t byte = buf[i];
			for (int b = 7; b >= 0; b--) {
				if ((byte >> b) & 1) {
					RDAT_HIGH();
				} else {
					RDAT_LOW();
				}
				HALF(hd); EDGE_SETTLE();
				RCLK_HIGH(); HALF(hd); RCLK_LOW();
			}
		}

		uint16_t crc = crc16(buf, EMMC_BLOCK_SIZE);
		for (int b = 15; b >= 0; b--) {
			if ((crc >> b) & 1) {
				RDAT_HIGH();
			} else {
				RDAT_LOW();
			}
			HALF(hd); EDGE_SETTLE();
			RCLK_HIGH(); HALF(hd); RCLK_LOW();
		}

		RDAT_HIGH();
		HALF(hd); EDGE_SETTLE();
		RCLK_HIGH(); HALF(hd); RCLK_LOW();            /* end bit */
	}

	/* Read the CRC status token: the card drives DAT0 low (start bit), then 3
	 * status bits (010=accepted, 101=CRC err, 110=write err), then high. */
	DAT0_IN();
	emmc_dbg_wr_status = -1;
	for (int i = 0; i < 16; i++) {
		RCLK_HIGH(); HALF(hd); EDGE_SETTLE();
		int start = (int)RDAT_GET();
		RCLK_LOW(); HALF(hd);
		if (!start) {
			int st = 0;
			for (int k = 0; k < 3; k++) {
				RCLK_HIGH(); HALF(hd); EDGE_SETTLE();
				st = (st << 1) | (int)RDAT_GET();
				RCLK_LOW(); HALF(hd);
			}
			emmc_dbg_wr_status = st;
			break;
		}
	}
	/* wait for programming to finish (card holds DAT0 low while busy).
	 * TIME-BASED (was 200k clk iterations, uncalibrated tens-of-ms): a GC
	 * stall can hold busy for hundreds of ms and the old counter expired
	 * SILENTLY as success — the next block's start token was then clocked
	 * into a still-busy card, misframing the rest of the CMD25 burst into a
	 * cascade of CRC-status failures (the corruption amplifier). Now: wait up
	 * to 1 s wall-clock, measured + reported, yielding periodically so lower-
	 * priority threads keep running; on expiry FAIL the write so the caller
	 * retries with the data still intact in the ring. */
	bool prog_done = false;
	{
		uint32_t _wbi = 0;
		uint32_t t0 = k_cycle_get_32();
		const uint32_t lim = k_us_to_cyc_ceil32(1000000u);  /* 1 s hard bound */
		const uint32_t yield_at = k_us_to_cyc_ceil32(500u);
		for (;;) {
			for (int burst = 0; burst < 64; burst++) {
				clk_pulse();
				_wbi++;
				if (READ_DAT0()) { prog_done = true; break; }
			}
			uint32_t el = k_cycle_get_32() - t0;
			if (prog_done || el >= lim) {
				uint32_t us = k_cyc_to_us_floor32(el);
				if (us > emmc_dbg_wr_busy_us_max)  emmc_dbg_wr_busy_us_max  = us;
				if (us > emmc_dbg_wr_busy_us_peak) emmc_dbg_wr_busy_us_peak = us;
				break;
			}
			if (el >= yield_at)
				k_usleep(50);   /* long stall: let MIDI/main breathe */
		}
		if (_wbi > emmc_dbg_wr_busy_max) emmc_dbg_wr_busy_max = _wbi;
	}

	if (!prog_done) {
		/* card STILL busy after 1 s: count separately from CRC errors (the
		 * old conflation is how the first diagnostic missed the stall) and
		 * fail the block — emmc_write_blocks aborts the burst with CMD12 and
		 * the streamer retries the same blocks next pass. DAT0 stays an
		 * INPUT: driving it push-pull HIGH against the card's active-low
		 * busy driver would short two output stages together for the rest
		 * of the (possibly multi-second) program; every subsequent bus
		 * phase reconfigures the pin direction for itself anyway. */
		emmc_dbg_busy_timeouts++;
		return false;
	}
	DAT0_OUT();
	DAT0_HIGH();
	/* ENFORCE the CRC-status token: 0b010 = accepted. Anything else means the
	 * card rejected the block (bus bit error) — returning false makes the
	 * caller retry instead of silently storing a glitch into the loop. */
	if (emmc_dbg_wr_status != 0x2) {
		emmc_crc_wr_errs++;
		return false;
	}
	return true;
}

bool emmc_init(void)
{
	s_ready = false;
	g_emmc_clk_half_us = CMD_SAFE_HALF_US;

	nrf_gpio_cfg(PIN_EMMC_CLK, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);  /* high-drive CLK */
	nrf_gpio_cfg_output(PIN_EMMC_CMD);
	DAT0_OUT();                          /* high-drive DAT0 */
	nrf_gpio_cfg_output(PIN_EMMC_RST);
	nrf_gpio_cfg_output(PIN_EMMC_VCCQ);

	spim_data_init();                    /* hardware-clocked data path */
	crc16_tab_init();

	CLK_LOW();
	CMD_HIGH();
	DAT0_HIGH();

	nrf_gpio_pin_set(PIN_EMMC_VCCQ);
	k_msleep(10);

	nrf_gpio_pin_clear(PIN_EMMC_RST);
	k_msleep(1);
	nrf_gpio_pin_set(PIN_EMMC_RST);
	k_msleep(2);

	CMD_HIGH();
	for (int i = 0; i < 80; i++) {
		clk_pulse();
	}

	send_command(0, 0x00000000, NULL);   /* CMD0 GO_IDLE (no response expected) */
	emmc_dbg_cmd0_sent = true;
	k_msleep(1);

	uint8_t r3[6] = {0};
	emmc_dbg_cmd1_retries = -1;
	for (int retry = 0; retry < 1000; retry++) {
		bool ok = send_command(1, 0x40FF8000, r3); /* CMD1 SEND_OP_COND, HCS=1 */
		k_msleep(1);
		if (ok && (r3[1] & 0x80)) {       /* response seen AND busy bit set = ready */
			emmc_dbg_cmd1_retries = retry;
			break;
		}
	}
	memcpy(emmc_dbg_ocr, r3, 6);
	if (emmc_dbg_cmd1_retries < 0) {      /* card never responded ready -> stop */
		return false;
	}

	/* CMD2 ALL_SEND_CID returns a 136-bit R2. send_command only waits for the
	 * start bit (r1_out=NULL); on success we must clock out the remaining 135
	 * bits so the bus is clean for CMD3. Retry a few times + capture timing. */
	for (int t = 0; t < 8; t++) {
		emmc_dbg_cmd2_tries = t + 1;
		emmc_dbg_cmd2_resp = send_command(2, 0, NULL);
		emmc_dbg_cmd2_clocks = emmc_dbg_resp_clocks;
		if (emmc_dbg_cmd2_resp) {
			for (int i = 0; i < 135; i++) { clk_pulse(); }  /* drain R2 (CID) */
			break;
		}
		k_msleep(2);
	}
	k_msleep(1);

	uint8_t r6[6] = {0};
	s_rca = 0x0001u << 16;
	emmc_dbg_cmd3_resp = send_command_retry(3, s_rca, r6, 8);   /* CMD3 SET_RELATIVE_ADDR */
	k_msleep(1);

	uint8_t r1[6] = {0};
	emmc_dbg_cmd7_resp = send_command_retry(7, s_rca, r1, 8);   /* CMD7 SELECT_CARD */
	memcpy(emmc_dbg_r1, r1, 6);
	k_msleep(1);
	emmc_dbg_cmd16_resp = send_command_retry(16, EMMC_BLOCK_SIZE, r1, 8); /* CMD16 SET_BLOCKLEN */
	k_msleep(1);

	/* strict: ready only if the card actually selected AND accepted block length */
	s_ready = emmc_dbg_cmd7_resp && emmc_dbg_cmd16_resp;
	if (s_ready) {
		s_cmd_half_us = 0u;   /* identification done: full-speed commands */
	}
	return s_ready;
}

bool emmc_is_ready(void)
{
	return s_ready;
}

/* Power-off: release the bus pins (floating, input-disconnected — nothing may
 * back-feed the unpowered rail) and cut the VCCQ I/O rail. Call only AFTER the
 * final cache flush; the card is gone until the next boot re-runs emmc_init().
 * Without this the retained-high VCCQ kept the card in standby through
 * SYSTEM_OFF — part of the "battery drains overnight" reports. */
void emmc_power_down(void)
{
	s_ready = false;
	nrf_gpio_pin_clear(PIN_EMMC_RST);
	nrf_gpio_cfg_default(PIN_EMMC_CLK);
	nrf_gpio_cfg_default(PIN_EMMC_CMD);
	nrf_gpio_cfg_default(PIN_EMMC_DAT0);
	nrf_gpio_cfg_default(PIN_EMMC_RST);
	nrf_gpio_pin_clear(PIN_EMMC_VCCQ);     /* rail off (pin stays an output) */
}

/* CMD8 SEND_EXT_CSD: an ADTC (read) command -- the card responds R1, then sends a
 * single 512-byte EXT_CSD data block on DAT0 exactly like CMD17. Read-only and
 * safe (no write-path touch). buf must be >= EMMC_BLOCK_SIZE (512). Used at boot
 * to probe the write cache: CACHE_SIZE@249-252 and EXT_CSD_REV@192 (see
 * streamer_thread). */
bool emmc_read_ext_csd(uint8_t *buf)
{
	if (!s_ready) {
		return false;
	}
	uint8_t r1[6];
	if (!send_command_retry(8, 0, r1, 8)) {
		return false;
	}
	return read_data_block(buf);
}

/* CMD6 SWITCH (R1b): write one EXT_CSD byte. arg = (0b11<<24)|(index<<8 ... );
 * the card responds R1 then holds DAT0 LOW while it applies the change. Wait the
 * busy out (same mechanism as the post-write program wait), then verify via
 * CMD13: SWITCH_ERROR = card-status bit7 = r1[4]&0x80; READY_FOR_DATA = bit8 =
 * r1[3]&0x01. Command-phase + DAT0 busy only -- does NOT touch the write bit-bang. */
/* Busy-abort hook + HPI: during ABORTABLE R1b waits (idle cache flush, TRIM)
 * the wait polls the app-registered callback ~1 kHz; when it returns true and
 * HPI is enabled, we fire an HPI (CMD12 with the HPI bit) and the card must
 * release the bus within its declared OUT_OF_INTERRUPT_TIME (100 ms on this
 * part) — so a maintenance op can NEVER hold the bus while the audio rings
 * drain toward a dropout. Power-off paths use the non-abortable waits. */
static bool (*s_abort_cb)(void);
static bool s_hpi_on;
static int s_step_abort;
static uint32_t s_step_started;
volatile uint32_t emmc_dbg_hpi_fires;
/* HK-613: the write cache is DIRTY from the first write after a flush until a
 * flush succeeds. The streamer's idle-window flush (main.c) fires only while
 * dirty and at most once a second -- it used to issue CMD6+CMD13 every 50 ms
 * of idle whether or not anything had been written (audit §2.3). */
static volatile bool s_cache_dirty;
volatile uint32_t emmc_dbg_cf_fires;   /* idle flushes attempted */
volatile uint32_t emmc_dbg_cf_ok;      /* idle flushes that completed */
bool emmc_cache_dirty(void) { return s_cache_dirty; }

/* Fire HPI: CMD12 with arg = RCA | HPI bit, then clock until DAT0 releases
 * (bounded well above the 100 ms OUT_OF_INTERRUPT_TIME). On timeout DAT0 is
 * left as an input — never drive against a busy card. */
static bool emmc_hpi_break(void)
{
	uint8_t r1[6];
	if (!s_hpi_on) {
		return false;
	}
	emmc_dbg_hpi_fires++;
	(void)send_command_retry(12, s_rca | 1u, r1, 4);
	uint32_t t0 = k_cycle_get_32();
	const uint32_t lim = k_us_to_cyc_ceil32(300000u);
	DAT0_IN();
	for (;;) {
		for (int b = 0; b < 64; b++) {
			clk_pulse();
			if (READ_DAT0()) {
				DAT0_OUT();
				DAT0_HIGH();
				return true;
			}
		}
		if ((k_cycle_get_32() - t0) >= lim) {
			return false;
		}
	}
}

/* Clock the bus while the card holds DAT0 low (R1b busy), up to max_us wall
 * clock. Shared by CMD6 SWITCH (cache enable/flush, HPI_MGMT) and CMD38 TRIM —
 * all delay-tolerant control phases. Yields while spinning so lower-priority
 * threads keep running under a long (seconds) busy; duration lands in
 * emmc_dbg_switch_busy_us_max. abortable=true adds the HPI escape hatch.
 * Returns false if busy never released (DAT0 then STAYS an input — no push-
 * pull contention against a still-busy card) or if the wait was HPI-aborted. */
static bool dat0_busy_wait_impl(uint32_t max_us, bool abortable)
{
	uint32_t t0 = k_cycle_get_32();
	const uint32_t lim = k_us_to_cyc_ceil32(max_us);
	const uint32_t yield_at = k_us_to_cyc_ceil32(500u);
	bool done = false;
	DAT0_IN();
	for (;;) {
		for (int burst = 0; burst < 64; burst++) {
			clk_pulse();
			if (READ_DAT0()) { done = true; break; }
		}
		uint32_t el = k_cycle_get_32() - t0;
		if (done || el >= lim) {
			uint32_t us = k_cyc_to_us_floor32(el);
			if (us > emmc_dbg_switch_busy_us_max) emmc_dbg_switch_busy_us_max = us;
			break;
		}
		if (el >= yield_at) {
			if (abortable && s_abort_cb && s_hpi_on && s_abort_cb()) {
				uint32_t us = k_cyc_to_us_floor32(el);
				if (us > emmc_dbg_switch_busy_us_max)
					emmc_dbg_switch_busy_us_max = us;
				s_step_abort=emmc_hpi_break()?1:-1; /* require bus release before resuming */
				return false;
			}
			k_usleep(50);
		}
	}
	if (!done) {
		emmc_dbg_busy_timeouts++;   /* card still busy: DAT0 stays an input */
		return false;
	}
	DAT0_OUT();
	DAT0_HIGH();
	return true;
}

static bool dat0_busy_wait_us(uint32_t max_us)
{
	return dat0_busy_wait_impl(max_us, false);
}

static bool emmc_switch_us(uint32_t arg, uint32_t busy_us)
{
	uint8_t r1[6];
	if (!send_command_retry(6, arg, r1, 8)) {
		return false;
	}
	if (!dat0_busy_wait_us(busy_us)) {
		return false;                 /* busy never released */
	}
	if (!emmc_cmd13(r1)) {
		return false;
	}
	if (r1[4] & 0x80) {                    /* SWITCH_ERROR */
		return false;
	}
	if (!(r1[3] & 0x01)) {                 /* not READY_FOR_DATA */
		return false;
	}
	return true;
}

static bool emmc_switch(uint32_t arg)
{
	return emmc_switch_us(arg, 1500000u);  /* 1.5 s default R1b bound */
}

void emmc_set_abort_cb(bool (*cb)(void))
{
	s_abort_cb = cb;
}

/* Enable HPI (EXT_CSD HPI_MGMT[161]=1, volatile per boot). Gate on
 * HPI_FEATURES[503] bit0 at the call site. */
bool emmc_hpi_enable(void)
{
	if (!s_ready) {
		return false;
	}
	s_hpi_on = emmc_switch(0x03A10100u);
	return s_hpi_on;
}

/* Enable the eMMC internal volatile write cache (EXT_CSD CACHE_CTRL[33] = 1) so
 * the card acks write bursts into its RAM and programs/GCs in the background
 * instead of stalling the bus mid-write. The cache is VOLATILE: it is explicitly
 * flushed (emmc_cache_flush) ONLY at power-off, via g_cache_flush_req in
 * stop_and_flush() -- NEVER during record/play, where a mid-stream flush would
 * block the bus and starve playback. The card still programs to NAND on its own
 * in the background; the power-off flush just forces the volatile remainder out. */
bool emmc_cache_enable(void)
{
	if (!s_ready) {
		return false;
	}
	return emmc_switch(0x03210100u);      /* access=write-byte, index=33, value=1 */
}

/* Force the cache to program to NAND (EXT_CSD FLUSH_CACHE[32] = 1). Blocks until
 * the flush completes (the program time) -- call at SAFE points, not mid-record. */
bool emmc_cache_flush(void)
{
	if (!s_ready) {
		return false;
	}
	{
		bool ok = emmc_switch_us(0x03200100u, 8000000u); /* write-byte FLUSH_CACHE[32]=1; up to 8 s for a full 4MB cache */
		if (ok) s_cache_dirty = false;   /* HK-613 */
		return ok;
	}
}

/* ABORTABLE cache flush for mid-session idle windows: if the busy-abort
 * callback trips (take armed / ring draining), the wait fires an HPI and this
 * returns false — the flush is simply re-tried at the next idle window. Only
 * call when HPI is enabled; power-off uses the blocking emmc_cache_flush(). */
bool emmc_cache_flush_try(void)
{
	if (!s_ready) {
		return false;
	}
	emmc_dbg_cf_fires++;   /* HK-613 */
	uint8_t r1[6];
	if (!send_command_retry(6, 0x03200100u, r1, 8)) {
		return false;
	}
	if (!dat0_busy_wait_impl(8000000u, true)) {
		return false;
	}
	if (!emmc_cmd13(r1)) {
		return false;
	}
	if (r1[4] & 0x80) {
		return false;
	}
	if (!(r1[3] & 0x01)) {
		return false;
	}
	s_cache_dirty = false;   /* HK-613 */
	emmc_dbg_cf_ok++;
	return true;
}

/* ---- FTL-maintenance ops (eMMC 4.5+/5.0) --------------------------------
 * These attack the GC-stall problem at its ROOT: the FTL's spare-block pool.
 * All are optional performance hygiene — every caller must treat a false
 * return as "carry on exactly as before". */

/* TRIM: CMD35 (range start) -> CMD36 (range end, inclusive) -> CMD38 arg=1,
 * then R1b busy on DAT0. Marks the blocks as holding no valid data so they
 * rejoin the FTL's spare pool (the datasheet budgets 300 ms per 4 MB erase
 * group worst-case; callers chunk multi-GB ranges accordingly). */
bool emmc_trim(uint32_t start_blk, uint32_t end_blk, uint32_t busy_us)
{
	if (storage373_pool()) return false;
	if (!storage373_writable()) return false;
	uint8_t r1[6];
	if (!s_ready) {
		return false;
	}
	if (!send_command_retry(35, start_blk, r1, 8)) {
		return false;
	}
	if (r1[1] & 0xFDu) {                   /* ERASE_SEQ/PARAM/ADDR errors */
		return false;
	}
	if (!send_command_retry(36, end_blk, r1, 8)) {
		return false;
	}
	if (r1[1] & 0xFDu) {
		return false;
	}
	if (!send_command_retry(38, 0x00000001u, r1, 8)) {   /* arg 1 = TRIM */
		return false;
	}
	if (r1[1] & 0xFDu) {
		return false;
	}
	/* ABORTABLE: a mid-session TRIM must never outlast the audio cushions —
	 * the busy-abort callback + HPI cut it short and the caller retries. */
	if (!dat0_busy_wait_impl(busy_us, true)) {
		return false;
	}
	if (!emmc_cmd13(r1)) {
		return false;
	}
	/* r1[1] = card-status[31:24]: ADDRESS_OUT_OF_RANGE/MISALIGN, BLOCK_LEN,
	 * ERASE_SEQ/PARAM, WP_VIOLATION, LOCK_UNLOCK_FAILED — mask out bit25
	 * (CARD_IS_LOCKED state, not an error). */
	if (r1[1] & 0xFDu) {
		return false;
	}
	if (!(r1[3] & 0x01)) {                 /* not READY_FOR_DATA */
		return false;
	}
	return true;
}

/* AUTO BKOPS: SET-BITS write of EXT_CSD[163] bit1 AUTO_EN (R/W/E, reversible).
 * The card then self-schedules garbage collection in bus-idle gaps — which
 * this looper provides between takes with the rings pinned full. NEVER write
 * bit0 MANUAL_EN: it is ONE-TIME-PROGRAMMABLE per JEDEC and permanently
 * obliges the host to service urgent-BKOPS forever. */
bool emmc_bkops_auto_enable(void)
{
	if (!s_ready) {
		return false;
	}
	return emmc_switch(0x01A30200u);   /* access=SET_BITS, index=163, value=0x02 */
}

/* POWER_OFF_NOTIFICATION (EXT_CSD[34]): declare managed power. POWERED_ON at
 * boot licenses more aggressive cached-write paths on several FTLs; the
 * datasheet explicitly instructs a power-off notification before power-down
 * when the cache is in use. */
bool emmc_pon_powered_on(void)
{
	if (!s_ready) {
		return false;
	}
	return emmc_switch(0x03220100u);   /* write-byte PON=1 POWERED_ON */
}

bool emmc_pon_power_off_short(void)
{
	if (!s_ready) {
		return false;
	}
	return emmc_switch(0x03220200u);   /* write-byte PON=2 POWER_OFF_SHORT */
}


static bool emmc_read_blocks_raw(uint32_t block_addr, uint8_t *buf, uint32_t count)
{
	if (!s_ready || !buf || !count ||
	    count > UINT32_MAX / EMMC_BLOCK_SIZE ||
	    block_addr > UINT32_MAX - (count - 1u)) {
		return false;
	}
	uint8_t r1[6];
	if (count == 1) {
		emmc_dbg_last_cmd_resp = send_command_retry(17, block_addr, r1, 8);
		if (!emmc_dbg_last_cmd_resp) {
			return false;
		}
		return read_data_block(buf);
	}
	/* Closed-ended transfer, matching the asynchronous reader: declare the
	 * exact count before CMD18. Do not issue CMD12 after successful completion;
	 * it is reserved for abort/recovery, never used to delimit a good burst.
	 * If either command fails, leave through the existing bounded stop path. */
	if (!send_command_retry(23, count, r1, 2) ||
	    !send_command_retry(18, block_addr, r1, 4)) {
		(void)send_command_retry(12, 0, r1, 3);
		return false;
	}
	/* BURST DEADLINE: every per-block bound (80 ms access hunt) can be
	 * ridden UNDER by a sustained-throttle card, pinning the streamer inside
	 * one CMD18 for seconds while the sibling rings drain. Cap the whole
	 * call, checked BEFORE each block (checking after let one command run
	 * deadline+hunt = ~400 ms — past the 341 ms record-ring horizon): worst
	 * single CMD18 is now ~150+80 ms. The caller only advances pointers on
	 * success, so an abort + whole-burst retry is idempotent. */
	uint32_t bt0 = k_cycle_get_32();
	const uint32_t blim = k_us_to_cyc_ceil32(150000u);
	for (uint32_t i = 0; i < count; i++) {
		if (i && (k_cycle_get_32() - bt0) >= blim) {
			(void)send_command_retry(12, 0, r1, 3);
			emmc_dbg_busy_timeouts++;
			return false;
		}
		if (!read_data_block(buf + i * EMMC_BLOCK_SIZE)) {
			(void)send_command_retry(12, 0, r1, 3);
			return false;
		}
	}
	/* Match the async reader's post-transfer clocks after the fixed count. */
	for (unsigned i = 0; i < 16; i++) clk_pulse();
	return true;
}
/* M46d: duty-cycled streamer priority boost — the TL-3/M46 dropout fix.
 * When a play ring runs low (g_emmc_sprint, set by the audio thread),
 * the streamer lifts itself to prio 1 so USB-era interrupt load can't
 * stretch its reads past the ring margin. Bursts are capped at 150 ms
 * with a forced 15 ms breather at normal priority: main keeps feeding
 * the bootloader-armed 5 s watchdog and the controls stay live.
 * Bench lineage + numbers: SP1-M40-PRE-BENCH-AUDIT.md 6b-6i. */
bool emmc_read_blocks(uint32_t block_addr, uint8_t *buf, uint32_t count)
{
	extern volatile uint8_t g_emmc_sprint, g_pb_on;
	extern struct k_thread *g_str_tid; extern int g_pb_orig;
	extern volatile uint32_t g_pb_t0;

	if (g_str_tid && k_current_get() == g_str_tid) {
		uint32_t _now = k_cycle_get_32();
		if (g_pb_on && (_now - g_pb_t0) > k_us_to_cyc_ceil32(150000u)) {
			k_thread_priority_set(k_current_get(), g_pb_orig);
			g_pb_on = 0;
			k_msleep(15);            /* the WDT/controls breather */
			_now = k_cycle_get_32();
		}
		if (g_emmc_sprint && !g_pb_on) {
			k_thread_priority_set(k_current_get(), 1);
			g_pb_on = 1; g_pb_t0 = _now;
		} else if (!g_emmc_sprint && g_pb_on) {
			k_thread_priority_set(k_current_get(), g_pb_orig);
			g_pb_on = 0;
		}
	}
	return emmc_read_blocks_raw(block_addr, buf, count);
}

#define SP1_ASYNC_WRITE 1   /* W3 flag: 0 restores the sync write path */
volatile uint32_t emmc_write_failure402;
volatile uint32_t write406_trace[4];
bool emmc_write_blocks_async_physical(uint32_t blk, const uint8_t *buf, uint32_t count);
bool emmc_write_blocks_physical(uint32_t block_addr, const uint8_t *buf, uint32_t count)
{
	emmc_write_failure402=0;
	if (!storage373_writable()) {emmc_write_failure402=W402_REASON(W402_ADMISSION,0,0);return false;}
	if (!s_ready) {
		emmc_write_failure402=W402_REASON(W402_NOT_READY,0,0);
		return false;
	}
	uint8_t r1[6];
	if (count == 1) {
		emmc_dbg_last_cmd_resp = send_command_retry(24, block_addr, r1, 8);
		if (!emmc_dbg_last_cmd_resp) {
			emmc_write_failure402=W402_REASON(W402_CMD24,0,0);
			return false;
		}
		bool ok=write_data_block(buf);
		if(!ok)emmc_write_failure402=W402_REASON(W402_SINGLE,0,emmc_dbg_wr_status);
		return ok;
	}
#if SP1_ASYNC_WRITE
	return emmc_write_blocks_async_physical(block_addr, buf, count);
#endif
	if (!send_command_retry(25, block_addr, r1, 4)) {   /* see CMD18: settle-miss retry */
		return false;
	}
	/* BURST DEADLINE (see emmc_read_blocks): a per-block throttle under the
	 * 1 s program bound must not pin the streamer inside one CMD25 for
	 * 32 x 1 s. Abort + retry is idempotent (r_r only advances on success;
	 * re-programming identical data to the same LBAs is harmless). */
	uint32_t bt0 = k_cycle_get_32();
	const uint32_t blim = k_us_to_cyc_ceil32(250000u);
	for (uint32_t i = 0; i < count; i++) {
		if (i && (k_cycle_get_32() - bt0) >= blim) {
			(void)send_command_retry(12, 0, r1, 3);
			emmc_dbg_busy_timeouts++;
			return false;
		}
		if (!write_data_block(buf + i * EMMC_BLOCK_SIZE)) {
			(void)send_command_retry(12, 0, r1, 3);
			return false;
		}
	}
	(void)send_command_retry(12, 0, r1, 3);
	/* CMD12 after a write burst is R1b: the card holds DAT0 low while it
	 * commits (prg state) and IGNORES data commands until done. Returning
	 * with the card still busy made the NEXT command miss its response
	 * window (a 200-clock hunt + resend) — the same wall time is cheaper
	 * paid here, watching DAT0. Bounded; a timeout just falls through to
	 * the existing miss/retry path. */
	(void)dat0_busy_wait_impl(500000u, false);
	return true;
}


/* ==================== M50 ASYNC READ PORT (BENCH ONLY) ====================
 * Transliterated from TimK's reference driver (KB assets, storagethingies
 * EmmcDriver.cpp, credited "Tim Knapen's eMMC driver implementation").
 * Mechanism: PWM0 drives CLK one period per hunt burst (seq word 0x8004,
 * COUNTERTOP 8); GPIOTE ch0 watches DAT0 HiToLo (the start bit); PPI ch0
 * wires GPIOTE -> PWM TASKS_STOP so the clock freezes ON the start bit;
 * the STOPPED ISR hands off to SPIM3 DMA (512+2 bytes straight into the
 * caller's buffer); ENDRX ISR advances and re-arms the hunt. The caller
 * sleeps on a semaphore. CPU per block: two short ISRs. */
#define M50_PIN_CLK   6u
#define M50_PIN_DAT0  7u
static volatile uint8_t  m50_state;     /* 0 idle, 1 busy, 2 done */
static volatile uint8_t  m50_waiting, m50_ok;
static uint8_t          *m50_buf;
static uint32_t          m50_nblk, m50_idx;
static struct k_sem      m50_sem;
static uint8_t           m50_init_done;
static uint16_t __aligned(4) m50_pwm_seq = 0x8004;   /* reference value */
volatile uint32_t g_m50_wait_bursts, g_m50_pwm_stops, g_m50_blk_irqs;
volatile uint32_t g_m50_f_busy, g_m50_f_c23, g_m50_f_c18, g_m50_last_r1;

uint8_t m54_tails[32][2];   /* M54 tails (M71r5: 32 = a whole turn) */
static void m50_start_wait_burst(void);

static void m50_start_block_receive(void)
{
	if (!m50_buf || m50_idx >= m50_nblk) {
		m50_ok = 0; m50_state = 2; k_sem_give(&m50_sem); return;
	}
	/* zero-copy DMA into the caller's buffer; the 2-byte CRC tail of
	 * each block lands in the next block's slot and is overwritten —
	 * the LAST block's tail needs +2 bytes of slack (caller provides). */
	/* r6: spim_xfer attaches PSEL per transfer — a WRITE leaves MOSI
	 * on DAT0 and MISO disconnected (samples ZEROS). Re-orient for
	 * receive before every DMA arm. */
	NRF_SPIM3->PSEL.MOSI = 0xFFFFFFFFu;
	NRF_SPIM3->PSEL.MISO = PIN_EMMC_DAT0;
	NRF_SPIM3->RXD.PTR    = (uint32_t)(m50_buf + m50_idx * 512u);
	NRF_SPIM3->RXD.MAXCNT = 514;
	NRF_SPIM3->TXD.MAXCNT = 0;
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->EVENTS_ENDRX = 0;
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
	NRF_SPIM3->INTENSET = SPIM_INTENSET_ENDRX_Msk;
	NRF_SPIM3->ENABLE = 7;
	NRF_SPIM3->TASKS_START = 1;
}

static void m50_start_wait_burst(void)
{
	m50_waiting = 1;
	g_m50_wait_bursts++;
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
	NRF_SPIM3->ENABLE = 0;
	NRF_GPIOTE->EVENTS_IN[0] = 0;
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->ENABLE = 1;   /* r2: take the pin for this burst only */
	NRF_PWM0->INTENSET = 2;                  /* STOPPED */
	NRF_PWM0->TASKS_SEQSTART[0] = 1;
}

static void m50_pwm_isr(const void *arg)
{
	ARG_UNUSED(arg);
	if (!NRF_PWM0->EVENTS_STOPPED) return;
	g_m50_pwm_stops++;
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 2;
	NRF_PWM0->ENABLE = 0;   /* r2: release the CLK pin */
	if (!m50_waiting || m50_state != 1) return;
	m50_waiting = 0;
	m50_start_block_receive();
}

static void m50_spim_isr(const void *arg)
{
	ARG_UNUSED(arg);
	/* r4: clear + disarm FIRST, on every path — a pending ENDRX with
	 * INTEN still set re-enters this ISR forever (the r3 WDT storm). */
	if (!NRF_SPIM3->EVENTS_ENDRX) { NRF_SPIM3->INTENCLR = 0xFFFFFFFF; return; }
	NRF_SPIM3->EVENTS_ENDRX = 0;
	NRF_SPIM3->EVENTS_END = 0;
	if (m50_state != 1) { NRF_SPIM3->INTENCLR = 0xFFFFFFFF; NRF_SPIM3->ENABLE = 0; return; }
	g_m50_blk_irqs++;
	if (m50_idx < 32u) {   /* M54: stash CRC tail before next DMA (r5) */
		m54_tails[m50_idx][0] = m50_buf[m50_idx * 512u + 512u];
		m54_tails[m50_idx][1] = m50_buf[m50_idx * 512u + 513u];
	}
	NRF_SPIM3->ENABLE = 0;
	m50_idx++;
	if (m50_idx >= m50_nblk) {
		NRF_SPIM3->INTENCLR = 0xFFFFFFFF;   /* r4: disarm before handoff */
		m50_ok = 1; m50_state = 2; k_sem_give(&m50_sem); return;
	}
	m50_start_wait_burst();
}

void emmc_m50_setup(void)
{
	if (m50_init_done) return;
	k_sem_init(&m50_sem, 0, 1);
	/* PWM0: one clock period per SEQSTART on the CLK pin */
	NRF_PWM0->PSEL.OUT[0] = (0u << 31) | (0u << 5) | M50_PIN_CLK; /* Connected, port 0 */
	NRF_PWM0->PSEL.OUT[1] = (1u << 31);
	NRF_PWM0->PSEL.OUT[2] = (1u << 31);
	NRF_PWM0->PSEL.OUT[3] = (1u << 31);
	NRF_PWM0->MODE = 0;                       /* Up */
	NRF_PWM0->PRESCALER = 0;                  /* 16 MHz */
	NRF_PWM0->COUNTERTOP = 8;                 /* 2 MHz hunt clock */
	NRF_PWM0->LOOP = 0;
	NRF_PWM0->DECODER = 0;
	NRF_PWM0->SEQ[0].PTR = (uint32_t)&m50_pwm_seq;
	NRF_PWM0->SEQ[0].CNT = 1;
	NRF_PWM0->SEQ[0].REFRESH = 0;
	NRF_PWM0->SEQ[0].ENDDELAY = 0;
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
	NRF_PWM0->ENABLE = 0;   /* r2: enabled ONLY during a hunt burst — an enabled PWM owns the CLK pin */
	/* GPIOTE ch0: DAT0 falling edge = the start bit */
	NRF_GPIOTE->EVENTS_IN[0] = 0;
	NRF_GPIOTE->CONFIG[0] = (1u << 0)         /* MODE Event */
	                      | (M50_PIN_DAT0 << 8)
	                      | (2u << 16);       /* POLARITY HiToLo */
	/* PPI ch0: start bit stops the hunt clock IN HARDWARE */
	NRF_PPI->CH[0].EEP = (uint32_t)&NRF_GPIOTE->EVENTS_IN[0];
	NRF_PPI->CH[0].TEP = (uint32_t)&NRF_PWM0->TASKS_STOP;
	NRF_PPI->FORK[0].TEP = 0;
	NRF_PPI->CHENSET = 1;
	{ void w3_isr_hookup(void); w3_isr_hookup(); }
	m50_init_done = 1;
}

/* Start an async multi-block read. buffer needs n*512+2 bytes. */
int emmc_m50_read_async(uint32_t blk, uint8_t *buf, uint32_t n)
{
	uint8_t r1[6];
	if (m50_state == 1 || !buf || !n) return 0;
	m50_state = 1; m50_buf = buf; m50_nblk = n; m50_idx = 0; m50_ok = 0;
	k_sem_reset(&m50_sem);
	if (!dat0_busy_wait_us(100000u)) { g_m50_f_busy++; goto fail; }
	if (!send_command_retry(23, n, r1, 2)) { g_m50_f_c23++; g_m50_last_r1 = r1[0]; goto fail; }
	if (!send_command_retry(18, blk, r1, 2)) { g_m50_f_c18++; g_m50_last_r1 = r1[0]; goto fail; }
	m50_start_wait_burst();
	return 1;
fail:
	(void)send_command_retry(12, 0, r1, 1);   /* r2: never leave the card mid-state */
	for (int i = 0; i < 16; i++) clk_pulse();
	m50_state = 0;
	return 0;
}

/* Wait for completion. 1 = ok, 0 = failed, -1 = timeout (torn down). */
int emmc_m50_wait(int ms)
{
	if (k_sem_take(&m50_sem, K_MSEC(ms)) != 0) {
		uint8_t r1[6];
		NRF_PWM0->INTENCLR = 2;
		NRF_PWM0->ENABLE = 0;
		NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
		NRF_SPIM3->ENABLE = 0;
		m50_waiting = 0;
		(void)send_command_retry(12, 0, r1, 2);   /* stop a stuck stream */
		for (int i = 0; i < 16; i++) clk_pulse();
		m50_state = 0;
		return -1;
	}
	{ int ok = m50_ok;
	  NRF_SPIM3->INTENCLR = 0xFFFFFFFF;          /* r4: belt and suspenders */
	  NRF_PWM0->INTENCLR = 2;
	  for (int i = 0; i < 16; i++) clk_pulse();  /* post-read clocks (reference) */
	  m50_state = 0;
	  return ok ? 1 : 0; }
}


/* M54: bench access to the driver's slice-by-4 CRC16 */
void emmc_m54_crc_init(void) { crc16_tab_init(); }
uint16_t emmc_m54_crc16(const uint8_t *d, uint32_t n) { return crc16(d, n); }
/* ==== W1-r2: ISR-CHAIN WRITE ENGINE (bench-only build) ==============
 * §38's one change: the chain advances in ISRs, never in a thread.
 *   thread: CMD25 -> aw2_start_tx(block 0) -> sem_take(2 s)
 *   SPIM END ISR: teardown (r4 rule: clear+disarm on EVERY path),
 *     end bit + 16-clock CRC-status hand-clock (verbatim from W1,
 *     proven werr=0 over 142,416 blocks; measured 16 us/blk),
 *     then: DAT0 high -> advance; DAT0 low -> hardware busy hunt
 *   busy hunt: PWM0 free-runs CLK (LOOPSDONE->SEQSTART short);
 *     GPIOTE ch1 (LoToHi on DAT0) -> PPI ch1 -> PWM TASKS_STOP;
 *     PWM STOPPED ISR books the busy time and advances.
 *   (read port uses GPIOTE ch0/PPI ch0 HiToLo — ch1 keeps W2's
 *    concurrent-read future open. PWM enabled only while hunting:
 *    an enabled PWM owns the CLK pin — gotcha #1.)
 * Time base: DWT->CYCCNT 64 MHz, /64 -> us, accumulated in us. */
volatile uint32_t g_aw2_tx_us, g_aw2_st_us, g_aw2_bz_us, g_aw2_bz_max;
volatile uint32_t g_aw2_werr, g_aw2_bzto, g_aw2_blk, g_aw2_irqs;
static volatile uint8_t  aw2_state;          /* 0 idle, 1 run, 2 done */
static volatile uint8_t  aw2_ok;
static const uint8_t    *aw2_frames;
static uint32_t          aw2_n, aw2_idx, aw2_t0, aw2_bz_t0;
static struct k_sem      aw2_sem;
static uint8_t           aw2_init;
static uint16_t __aligned(4) aw2_pwm_seq = 0x8004;   /* 288 reference */

void aw2_build_frame(uint8_t *f, const uint8_t *data);
static const uint8_t *aw3_src;   /* W3: raw 512-stride source */
static void aw2_start_tx(void)
{
	const uint8_t *f;
	if (aw3_src) {
		aw2_build_frame((uint8_t *)s_dma_tx, aw3_src + aw2_idx * 512u);
		f = (const uint8_t *)s_dma_tx;
	} else {
		f = aw2_frames + aw2_idx * 516u;
	}
	DAT0_OUT();
	RDAT_HIGH();
	RCLK_LOW();
	/* spim_xfer's exact recipe (dump 152-162), interrupt-driven */
	NRF_SPIM3->PSEL.MOSI = PIN_EMMC_DAT0;
	NRF_SPIM3->PSEL.MISO = 0xFFFFFFFFu;
	NRF_SPIM3->ENABLE    = 7;
	NRF_SPIM3->TXD.PTR    = (uint32_t)f;
	NRF_SPIM3->TXD.MAXCNT = 516;
	NRF_SPIM3->RXD.PTR    = 0;
	NRF_SPIM3->RXD.MAXCNT = 0;
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
	NRF_SPIM3->INTENSET = SPIM_INTENSET_END_Msk;
	aw2_t0 = DWT->CYCCNT;
	NRF_SPIM3->TASKS_START = 1;
}

static void aw2_advance(void)
{
	aw2_idx++;
	if (aw2_idx >= aw2_n) {
		aw2_ok = 1; aw2_state = 2; k_sem_give(&aw2_sem); return;
	}
	aw2_start_tx();
}

static void aw2_hunt_arm(void)
{
	NRF_GPIOTE->EVENTS_IN[1] = 0;
	NRF_PPI->CHENSET = 2;                /* ch1: DAT0 rise -> PWM STOP */
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
	NRF_PWM0->INTENSET = 2;              /* STOPPED */
	NRF_PWM0->SHORTS = (1u << 2);        /* LOOPSDONE -> SEQSTART0 */
	NRF_PWM0->LOOP = 1;
	NRF_PWM0->ENABLE = 1;
	NRF_PWM0->TASKS_SEQSTART[0] = 1;
	/* race close: DAT0 rose between check and arm -> edge missed.
	 * Stop by hand; the STOPPED ISR advances either way. */
	if (READ_DAT0())
		NRF_PWM0->TASKS_STOP = 1;
}

static void aw2_spim_isr(const void *arg)
{
	ARG_UNUSED(arg);
	if (!NRF_SPIM3->EVENTS_END) { NRF_SPIM3->INTENCLR = 0xFFFFFFFF; return; }
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
	NRF_SPIM3->ENABLE = 0;
	if (aw2_state != 1)
		return;
	g_aw2_irqs++;
	g_aw2_tx_us += (DWT->CYCCNT - aw2_t0) / 64u;
	const uint32_t hd = g_emmc_clk_half_us;
	uint32_t t0 = DWT->CYCCNT;
	HALF(hd); EDGE_SETTLE();
	RCLK_HIGH(); HALF(hd); RCLK_LOW();   /* end bit (W1 verbatim) */
	DAT0_IN();
	int st = -1;
	for (int i = 0; i < 16; i++) {
		RCLK_HIGH(); HALF(hd); EDGE_SETTLE();
		int s0 = (int)RDAT_GET();
		RCLK_LOW(); HALF(hd);
		if (!s0) {
			st = 0;
			for (int k = 0; k < 3; k++) {
				RCLK_HIGH(); HALF(hd); EDGE_SETTLE();
				st = (st << 1) | (int)RDAT_GET();
				RCLK_LOW(); HALF(hd);
			}
			break;
		}
	}
	g_aw2_st_us += (DWT->CYCCNT - t0) / 64u;
	if (st != 2) {
		emmc_write_failure402=W402_REASON(W402_TOKEN,aw2_idx,st);
		g_aw2_werr++; aw2_ok = 0; aw2_state = 2;
		k_sem_give(&aw2_sem); return;
	}
	/* two clocks so the card can assert busy (W1 verbatim) */
	RCLK_HIGH(); HALF(hd); RCLK_LOW();
	RCLK_HIGH(); HALF(hd); RCLK_LOW();
	if (READ_DAT0()) { g_aw2_blk++; aw2_advance(); return; }
	aw2_bz_t0 = DWT->CYCCNT;
	aw2_hunt_arm();
}

static void aw2_pwm_isr(const void *arg)
{
	ARG_UNUSED(arg);
	if (!NRF_PWM0->EVENTS_STOPPED) { NRF_PWM0->INTENCLR = 0xFFFFFFFF; return; }
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
	NRF_PWM0->SHORTS = 0;
	NRF_PWM0->LOOP = 0;
	NRF_PWM0->ENABLE = 0;                /* release the CLK pin */
	NRF_PPI->CHENCLR = 2;
	if (aw2_state != 1)
		return;
	if (!READ_DAT0()) {                  /* stopped but still busy: rearm */
		aw2_hunt_arm();
		return;
	}
	uint32_t bz = (DWT->CYCCNT - aw2_bz_t0) / 64u;
	g_aw2_bz_us += bz;
	if (bz > g_aw2_bz_max) g_aw2_bz_max = bz;
	g_aw2_blk++;
	aw2_advance();
}

void aw2_setup(void)
{
	if (aw2_init) return;
	k_sem_init(&aw2_sem, 0, 1);
	/* DWT cycle counter on (64 MHz time base) */
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	/* W3-r2: never reset CYCCNT -- tree-wide M73/M81 time base (artifact #10) */
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	/* PWM0: 288's reference hunt-clock config, one period per loop */
	NRF_PWM0->PSEL.OUT[0] = (0u << 31) | (0u << 5) | 6u;  /* CLK P0.6 */
	NRF_PWM0->PSEL.OUT[1] = (1u << 31);
	NRF_PWM0->PSEL.OUT[2] = (1u << 31);
	NRF_PWM0->PSEL.OUT[3] = (1u << 31);
	NRF_PWM0->MODE = 0;
	NRF_PWM0->PRESCALER = 0;
	NRF_PWM0->COUNTERTOP = 8;            /* 2 MHz */
	NRF_PWM0->LOOP = 0;
	NRF_PWM0->DECODER = 0;
	NRF_PWM0->SEQ[0].PTR = (uint32_t)&aw2_pwm_seq;
	NRF_PWM0->SEQ[0].CNT = 1;
	NRF_PWM0->SEQ[0].REFRESH = 0;
	NRF_PWM0->SEQ[0].ENDDELAY = 0;
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
	NRF_PWM0->ENABLE = 0;
	/* GPIOTE ch1: DAT0 RISING edge = busy release (ch0 stays free
	 * for the read port's start-bit hunt — W2 runs both) */
	NRF_GPIOTE->EVENTS_IN[1] = 0;
	NRF_GPIOTE->CONFIG[1] = (1u << 0)             /* MODE Event */
	                      | ((uint32_t)PIN_EMMC_DAT0 << 8)
	                      | (1u << 16);           /* POLARITY LoToHi */
	NRF_PPI->CH[1].EEP = (uint32_t)&NRF_GPIOTE->EVENTS_IN[1];
	NRF_PPI->CH[1].TEP = (uint32_t)&NRF_PWM0->TASKS_STOP;
	NRF_PPI->FORK[1].TEP = 0;
	{ void w3_isr_hookup(void); w3_isr_hookup(); }
	aw2_init = 1;
}

void aw2_build_frame(uint8_t *f, const uint8_t *data)
{
	uint16_t crc = crc16(data, EMMC_BLOCK_SIZE);
	f[0] = 0xFF;
	f[1] = 0xFE;
	memcpy(&f[2], data, EMMC_BLOCK_SIZE);
	f[2 + EMMC_BLOCK_SIZE]     = (uint8_t)(crc >> 8);
	f[2 + EMMC_BLOCK_SIZE + 1] = (uint8_t)crc;
}

static void aw2_disarm_all(void)
{
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
	NRF_SPIM3->ENABLE = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
	NRF_PWM0->SHORTS = 0;
	NRF_PWM0->LOOP = 0;
	/*413: a missing status token happens in the SPIM phase, when PWM is
	 * already disabled. STOP cannot complete a sequence that is not running.
	 * Do not spend100000 peripheral reads waiting for that absent event.
	 * An enabled busy-hunt still follows the existing bounded STOP path. */
	if (NRF_PWM0->ENABLE) {
		NRF_PWM0->TASKS_STOP = 1;
		for (int g = 0; g < 100000 && !NRF_PWM0->EVENTS_STOPPED; g++) { }
	}
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->ENABLE = 0;
	NRF_PPI->CHENCLR = 2;
}

/* frames: n pre-built 516-byte frames, packed. Thread-paced only at
 * the burst edges (CMD25 / CMD12 + R1b) — never per block. */
bool aw2_write_burst(uint32_t blk, const uint8_t *frames, uint32_t n)
{
	if (storage373_pool()) return false;
	if (!storage373_writable()) return false;
	uint8_t r1[6];
	if (!send_command_retry(25, blk, r1, 4)) return false;
	aw2_frames = frames; aw2_n = n; aw2_idx = 0; aw2_ok = 0;
	k_sem_reset(&aw2_sem);
	aw2_state = 1;
	aw2_start_tx();
	if (k_sem_take(&aw2_sem, K_MSEC(2000)) != 0) {
		aw2_state = 0;
		aw2_disarm_all();
		g_aw2_bzto++;
		(void)send_command_retry(12, 0, r1, 3);
		(void)dat0_busy_wait_impl(500000u, false);
		return false;
	}
	aw2_state = 0;
	(void)send_command_retry(12, 0, r1, 3);
	(void)dat0_busy_wait_impl(500000u, false);
	return aw2_ok != 0;
}

/* ==== W3: emmc_write_blocks' multi-block body on the chain =========
 * Contract preserved from the sync path (WRITE-DRIVER-DUMP 1140-1168):
 * CMD25 entry, 250 ms whole-burst deadline (emmc_dbg_busy_timeouts++,
 * false), CMD12 on every exit path, bounded R1b wait ONLY on success,
 * false => caller's idempotent retry. PPI ch0 parked during the burst
 * (W2-proven). Sole-owner rule makes this collision-free with reads. */
static uint32_t write402_remaining(uint32_t started,uint32_t budget_us){
	uint32_t elapsed=k_cyc_to_us_floor32(k_cycle_get_32()-started);
	return elapsed<budget_us?budget_us-elapsed:0;
}
static bool write402_ready_status(const uint8_t r1[6]){
	/* Common R1 fields: Zephyr include/zephyr/sd/sd_spec.h:88-143
	 * (SD_R1_* and SDMMC_R1_TRANSFER). Existing emmc_switch_us maps
	 * SWITCH_ERROR bit7 to r1[4]bit7 and READY bit8 to r1[3]bit0.
	 * Refuse range/address/length/erase/WP/lock, ECC/controller/general,
	 * CSD-overwrite, erase-skip and SWITCH_ERROR. CRC-error bit23 can be
	 * the original transient; ILLEGAL_CMD bit22 can be our redundant
	 * CMD12 after the original cleanup already stopped the transfer.
	 * Those two are allowed ONLY with DAT0 released and READY+TRANSFER. */
	uint32_t status=(uint32_t)r1[1]<<24|(uint32_t)r1[2]<<16|(uint32_t)r1[3]<<8|r1[4];
	return !(status&0xff398080u)&&(status&0x1f00u)==0x0900u;
}
/*406: a missing CMD12 response is not proof the stop failed. Prove the
 * card is idle/READY/TRANSFER before replaying the immutable private burst.
 * Only error recovery uses this; no extra bus work on normal writes. */
static bool write406_command(uint8_t command,uint32_t arg,uint8_t r1[6],uint32_t started,uint32_t budget_us){
 for(unsigned attempt=0;attempt<3;attempt++){
  /* Reserve room for the bounded command probe; all commands are fast after init. */
  if(write402_remaining(started,budget_us)<1000u){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);return false;}
  memset(r1,0,6);write406_trace[command==12?1:2]++;
  if(send_command(command,arg,r1))return true;
  for(unsigned i=0;i<16;i++)clk_pulse();
 }
 return false;
}
static bool write406_resync(uint32_t started,uint32_t budget_us){
 uint8_t r1[6]={0};
 bool stopped=write406_command(12,0,r1,started,budget_us);
 uint32_t remaining=write402_remaining(started,budget_us);
 if(remaining<1000u){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);return false;}
 if(!dat0_busy_wait_impl(remaining,false)){emmc_write_failure402=W402_REASON(W402_BUSY,0,0);return false;}
 if(!write406_command(13,s_rca,r1,started,budget_us)){
  if((emmc_write_failure402>>24)!=W402_BUDGET)emmc_write_failure402=W402_REASON(stopped?W402_STATUS:W402_STOP,0,0);
  return false;
 }
 write406_trace[3]=(uint32_t)r1[1]<<24|(uint32_t)r1[2]<<16|(uint32_t)r1[3]<<8|r1[4];
 if(!write402_ready_status(r1)){emmc_write_failure402=W402_REASON(W402_STATUS,(uint32_t)r1[1]<<8|r1[2],r1[3]|(r1[4]&0x80u));return false;}
 if(!write402_remaining(started,budget_us)){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);return false;}
 return true;
}
static bool write_blocks_async402(uint32_t blk,const uint8_t *buf,uint32_t count,uint32_t budget_us)
{
	emmc_write_failure402=0;
	if (!storage373_writable()) {emmc_write_failure402=W402_REASON(W402_ADMISSION,0,0);return false;}
	uint8_t r1[6];
	uint32_t started=budget_us?k_cycle_get_32():0;
	s_cache_dirty = true;   /* HK-613 */
	aw2_setup();
	if (!send_command_retry(25, blk, r1, budget_us?1:4)) {
		emmc_write_failure402=W402_REASON(W402_CMD25,0,0);
		return false;
	}
	uint32_t remaining=budget_us?write402_remaining(started,budget_us):250000u;
	if(!remaining){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);(void)send_command_retry(12,0,r1,budget_us?1:3);return false;}
	NRF_PPI->CHENCLR = 1;
	aw3_src = buf; aw2_frames = NULL; aw2_n = count; aw2_idx = 0; aw2_ok = 0;
	k_sem_reset(&aw2_sem);
	aw2_state = 1;
	aw2_start_tx();
	int rc = k_sem_take(&aw2_sem, budget_us?K_USEC(remaining):K_MSEC(250));
	aw2_state = 0;
	aw3_src = NULL;
	NRF_PPI->CHENSET = 1;
	if (rc != 0) {
		aw2_disarm_all();
		emmc_write_failure402=W402_REASON(W402_TIMEOUT,aw2_idx,0);
		emmc_dbg_busy_timeouts++;
		(void)send_command_retry(12, 0, r1, budget_us?1:3);
		return false;
	}
	if (!aw2_ok) {
        aw2_disarm_all();
		write406_trace[0]=send_command_retry(12, 0, r1, budget_us?1:3)?1u:2u;
		return false;
	}
	if(budget_us)return write406_resync(started,budget_us);
    if(!send_command_retry(12,0,r1,3)){emmc_write_failure402=W402_REASON(W402_STOP,aw2_idx,0);return false;}
    if(!dat0_busy_wait_impl(500000u,false)){emmc_write_failure402=W402_REASON(W402_BUSY,aw2_idx,0);return false;}
	return true;
}
bool emmc_write_blocks_async_physical(uint32_t blk,const uint8_t *buf,uint32_t count){
	return write_blocks_async402(blk,buf,count,0);
}

/* Recovery is only entered after a private audio write returned false. The
 * chain is already disarmed; never start another DMA while card state is
 * uncertain. No reset, HPI, cache command, metadata write or data repack.
 * Up to three bounded probes per control phase; busy polling yields.
 * The native R1 layout already used above has READY at r1[3] bit0 and
 * CURRENT_STATE at r1[3] bits4:1 (4 = transfer). */
bool emmc_write_blocks_retry402(uint32_t blk,const uint8_t *buf,uint32_t count,uint32_t budget_ms){
	if(!storage373_writable()){emmc_write_failure402=W402_REASON(W402_ADMISSION,0,0);return false;}
	if(!s_ready){emmc_write_failure402=W402_REASON(W402_NOT_READY,0,0);return false;}
	if(!buf||count<2||count>16||!budget_ms||budget_ms>40){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);return false;}
	uint32_t started=k_cycle_get_32(),budget_us=budget_ms*1000u;
    aw2_disarm_all();
    if(!write406_resync(started,budget_us))return false;
    uint32_t remaining=write402_remaining(started,budget_us);
    /* Zero means unbounded first-attempt mode to the lower layer. Never pass
     * an exhausted recovery budget through that sentinel. */
    if(remaining<1000u){emmc_write_failure402=W402_REASON(W402_BUDGET,0,0);return false;}
	return write_blocks_async402(blk,buf,count,remaining);
}

/* ==== W3-r4: split burst API for the flush pipeline ================
 * start() returns with the chain IN FLIGHT (frames built per block in
 * the ISR from aw3_src); the caller may touch RAM (pack the next
 * page) but NOT the bus until wait(). Same deadline/CMD12 contract
 * as emmc_write_blocks_async. */
bool aw3_burst_start(uint32_t blk, const uint8_t *buf, uint32_t count)
{
	if (storage373_pool()) return false;
	if (!storage373_writable()) return false;
	uint8_t r1[6];
	aw2_setup();
	if (!send_command_retry(25, blk, r1, 4)) return false;
	NRF_PPI->CHENCLR = 1;
	aw3_src = buf; aw2_frames = NULL; aw2_n = count; aw2_idx = 0; aw2_ok = 0;
	k_sem_reset(&aw2_sem);
	aw2_state = 1;
	aw2_start_tx();
	return true;
}
bool aw3_burst_wait(void)
{
	uint8_t r1[6];
	int rc = k_sem_take(&aw2_sem, K_MSEC(250));
	aw2_state = 0;
	aw3_src = NULL;
	NRF_PPI->CHENSET = 1;
	if (rc != 0) {
		aw2_disarm_all();
		emmc_dbg_busy_timeouts++;
		(void)send_command_retry(12, 0, r1, 3);
		return false;
	}
	(void)send_command_retry(12, 0, r1, 3);
	(void)dat0_busy_wait_impl(500000u, false);
	return aw2_ok != 0;
}

/* ==== W3 dispatcher: one storage owner (looper.html sole-owner rule)
 * means read and write bursts never overlap; route the shared
 * SPIM3/PWM0 interrupts on port state. Idle default: clear + disarm
 * (the r4 rule) so a stray event can never storm. */
static void w3_spim_isr(const void *arg)
{
	if (m50_state == 1) { m50_spim_isr(arg); return; }
	if (aw2_state == 1) { aw2_spim_isr(arg); return; }
	NRF_SPIM3->EVENTS_END = 0;
	NRF_SPIM3->EVENTS_ENDRX = 0;
	NRF_SPIM3->INTENCLR = 0xFFFFFFFF;
}
static void w3_pwm_isr(const void *arg)
{
	if (m50_state == 1) { m50_pwm_isr(arg); return; }
	if (aw2_state == 1) { aw2_pwm_isr(arg); return; }
	NRF_PWM0->EVENTS_STOPPED = 0;
	NRF_PWM0->INTENCLR = 0xFFFFFFFF;
}
void w3_isr_hookup(void)
{
	static uint8_t w3_hooked;
	if (w3_hooked) return;
	IRQ_CONNECT(SPIM3_IRQn, 3, w3_spim_isr, NULL, 0);
	IRQ_CONNECT(PWM0_IRQn, 3, w3_pwm_isr, NULL, 0);
	irq_enable(SPIM3_IRQn);
	irq_enable(PWM0_IRQn);
	w3_hooked = 1;
}

static bool commit_slice_elapsed(void){return k_cycle_get_32()-s_step_started>=k_us_to_cyc_ceil32(2000);}
int emmc_commit_flush_step(void){
 if(!s_ready||!s_hpi_on)return -1;
 bool (*saved)(void)=s_abort_cb;s_abort_cb=commit_slice_elapsed;
 s_step_abort=0;s_step_started=k_cycle_get_32();
 bool ok=emmc_cache_flush_try();s_abort_cb=saved;
 return ok?1:s_step_abort==1?0:-1;
}
