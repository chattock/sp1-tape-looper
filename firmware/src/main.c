/*
 * ============================================================================
 *  SP-1 LOOPER  —  custom firmware for the Teenage Engineering SP-1
 * ============================================================================
 *  A four-track, hold-to-record audio looper / sketchpad. Audio comes in over
 *  USB-C (the SP-1 appears as a USB sound card); you record loops by holding
 *  the track buttons, and they play back layered together out of the speaker
 *  or headphones. Loops are stored on the SP-1's internal 4 GB flash, so they
 *  survive power-off and even re-flashing the firmware.
 *
 *  ---- HOW THE AUDIO FLOWS ----
 *    USB-C in  ->  [USB ring]  ->  audio engine  ->  I2S bus  ->  speaker / HP
 *                                       |  ^
 *                              record  v  |  play
 *                                  [ eMMC flash, 1 region per track ]
 *
 *  ---- THE THREADS (highest audio priority first) ----
 *    audio_thread   : runs every I2S block (256 frames). Mixes the 4 playback
 *                     tracks + the live USB monitor, and decimates the live
 *                     input down into the track being recorded. Never blocked.
 *    streamer_thread: the only thing that touches the flash. Flushes the
 *                     track being recorded TO flash, and reads the playing
 *                     tracks back FROM flash into their ring buffers ahead of
 *                     the playhead. (sp1_emmc.c is the flash driver.)
 *    midi_thread    : (optional) MIDI clock housekeeping.
 *    main           : ~8 ms control loop — buttons, faders, LEDs, power, the
 *                     USB-serial status line (controls_diag).
 *
 *  ---- KEY DESIGN POINTS ----
 *    * Clocking: the board's 3.072 MHz oscillator drives the I2S bit clock and
 *      the CS42L42 headphone codec masters a true 48 kHz frame; the nRF and the
 *      speaker amp are clock slaves (see the "I2S audio bus" section).
 *    * Loops play at full 48 kHz; recording is mono and decimated by DECIM (see
 *      the LOOPER ENGINE section) — the flash write speed sets that ceiling.
 *    * Storage uses the nRF's SPIM3 SPI engine at 32 MHz (a calculated overclock
 *      above the 26 MHz default-speed max) with hardware CRC checking + retry,
 *      so the flash bus is fast and self-correcting. The card's internal write
 *      cache is enabled to absorb record bursts; it's flushed only at power-off.
 *
 *  ---- BOOTLOADER SAFETY (the SP-1 "BIG FIVE") ----
 *    app lives at 0x20000; watchdog fed < 5 s; we do NOT re-init bootloader-
 *    owned clocks/peripherals; SYSTEM_OFF returns to the bootloader; RESETREAS
 *    is cleared on boot and before SYSTEM_OFF. (There is no hardware reset pin
 *    on the SP-1, so a clean path back to the bootloader is mandatory.)
 *
 *  See README.md in this folder for the player's controls and a fuller tour.
 * ============================================================================
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/sys/ring_buffer.h>
#include <sample_usbd.h>

/* From the patched Zephyr UAC2 class (zephyr-patches/): selects the Full-Speed
 * explicit-feedback wire format at runtime. false = 3-byte Q10.14 (USB spec —
 * what Apple hosts require), true = 4-byte Q16.16 (what Microsoft's
 * usbaudio2.sys requires). The two are mutually incompatible per host, so the
 * main loop auto-negotiates: see the feedback-format watchdog in main(). */
extern bool uac2_fs_fb_windows_fmt;
#include <soc.h>
#include <math.h>
#include <string.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include "sp1_emmc.h"

/* FAILSAFE: turn ANY unrecoverable fault (bad pointer, stack overflow, kernel
 * panic, failed assert) into a clean reboot instead of a dead hang, so the
 * device can never get stuck in a bricked-looking state.
 * CRASH FORENSICS: this silent reboot is also why crashes left no trail —
 * stash the fault reason + faulting PC in __noinit RAM (survives the soft
 * reboot); the next boot prints them in the diag line as flt=reason@pc. */
static __noinit uint32_t g_fault_key;            /* 0xFA17FA17 = breadcrumb valid */
static __noinit uint32_t g_fault_reason;
static __noinit uint32_t g_fault_pc;
static uint32_t g_resetreas;                     /* NRF_POWER->RESETREAS at boot */
static uint32_t g_last_fault_reason = 0xFFFFFFFFu; /* from the PREVIOUS boot (diag) */
static uint32_t g_last_fault_pc;
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	g_fault_reason = reason;
	g_fault_pc = esf ? esf->basic.pc : 0u;
	g_fault_key = 0xFA17FA17u;
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

#define WDT_NODE DT_ALIAS(watchdog0)

/* ---- the 4 playback LEDs (center row, verified pin map) ---- */
struct led { NRF_GPIO_Type *port; uint32_t pin; };
static const struct led leds[] = {
	{ NRF_P1, 13 }, { NRF_P0, 0 }, { NRF_P1, 12 }, { NRF_P0, 1 },
};
#define NUM_LEDS (sizeof(leds) / sizeof(leds[0]))

/* ---- the 4 TRACK LEDs (directly above buttons 1-4) ---- */
static const struct led track_leds[] = {
	{ NRF_P0, 29 }, { NRF_P0, 26 }, { NRF_P1, 15 }, { NRF_P1, 14 },
};
#define NUM_TRACK_LEDS (sizeof(track_leds) / sizeof(track_leds[0]))

/* 1 = dim LEDs (soft-PWM render), 0 = full brightness. Toggled by the
 * FUNCTION+PLAY double-tap; persisted in the song index tail (led_full).
 * Declared here (not with the dimmer) because xfer_commit persists it. */
static volatile uint8_t g_led_dim = 1;
static void led_hw_refresh(void);   /* LEDPWM-710: the xfer-mode dim toggle sits above the LED block */

static void track_led_on(int i);
static void track_led_off(int i);   /* LED-549 r10: show_page_sweep */
static void track_all_off(void);
static bool usb_present(void);
static bool charging(void);

/* ---- power / function button: P0.27, active-low with pull-up ---- */
#define PWR_PORT        NRF_P0
#define PWR_PIN         27u

/* ---- BQ24232 battery charger control (verified pins from TimK pinout) ---- */
#define BQ_PORT         NRF_P0
#define BQ_NCE_PIN      21u   /* charge enable, ACTIVE-LOW: drive low = charging on */
#define BQ_NCHG_PIN     22u   /* charge status, open-drain, LOW = charging now      */
#define BQ_NPGOOD_PIN   24u   /* power good,    open-drain, LOW = USB power present  */

/* hold this long (ms) to power off - "a few seconds" like the real device */
#define HOLD_MS_TO_OFF  2500
/* M27: how long TRACK 1 + TRACK 4 must be held before we reset into the
 * bootloader. Was 1200 ms, which collided with using 1+4 as a musical
 * gesture; a mute tap is 100-200 ms, so 3000 gives ~15x margin and lines
 * up with the power-off hold above. */
#define DFU_HOLD_MS     3000

/* ---- button ladders (Milestone 1: read + report the controls) ----
 * The PLAY/track and Vol/FWD/RWD buttons are resistor ladders read on the
 * SAADC. They are only powered when BTN_COM (P1.10) is driven high, so we
 * raise that rail before sampling. Raw 12-bit codes are streamed over the
 * USB serial console so we can map each button press to a voltage band. */
#define BTN_COM_PORT    NRF_P1
#define BTN_COM_PIN     10u

static const struct adc_dt_spec adc_ladder[] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),  /* AIN0: PLAY + tracks   */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),  /* AIN1: Vol + FWD/RWD   */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),  /* AIN3: Fader 1 (track1 vol) */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 3),  /* AIN6: Fader 2 (track2 vol) */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 4),  /* AIN2: Fader 3 (track3 vol) */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 5),  /* AIN7: Fader 4 (track4 vol) */
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 6),  /* AIN4: battery level (divider) */
};
#define LAD_TRACKS 0
#define LAD_VOL    1
#define LAD_FADER0 2     /* faders are ladder indices 2..5 */
#define LAD_BATT   6     /* battery voltage via on-board divider (AIN4) */
#define NUM_LADDERS (sizeof(adc_ladder) / sizeof(adc_ladder[0]))

/* the USB CDC ACM serial console (chosen,console in the devicetree) */
static const struct device *const cdc =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static int16_t adc_sample;

/* ---- audio codecs over I2C (Milestone 2a: just confirm they answer) ----
 *   CS42L42 headphone codec @ 0x48   (reset: P0.15, active-low)
 *   TAS2505 speaker amp     @ 0x18   (reset: P0.09 / NFC1, active-low)
 * We release both resets, then scan the bus and report what ACKs.
 * (Verified on hardware 2026-06-05: both ACK; CS42L42 straps to 0x48.) */
#define CS42_RST_PORT   NRF_P0
#define CS42_RST_PIN    15u
#define TAS_RST_PORT    NRF_P0
#define TAS_RST_PIN     9u
#define CS42L42_ADDR    0x48u
#define TAS2505_ADDR    0x18u

static const struct device *const i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static uint8_t i2c_found[16];
static int     i2c_found_n;
static bool    cs42_ok, tas_ok;
static bool    i2c_scanned;

/* Oversampled ladder read: average 2 conversions. Audio/USB activity couples
 * noise into the shared BTN_COM rail, so a single 12-bit sample can land a band
 * boundary off; averaging quietens every ladder, and the sticky debounce does
 * the rest. CAREFUL with the count: blocking ADC reads run on the main thread,
 * which PREEMPTS the eMMC streamer — at 4x across 6 ladders the stolen CPU
 * slowed the bit-banged card below the ~26.6 blk/s a take produces and brought
 * back record-ring overflows (corrupt loops). 2x + round-robin faders keeps the
 * main loop's ADC cost at the level the working builds had.
 * Returns -1 on ADC error (callers treat <0 as "no change / hold last"). */
/* ADCSCAN-710: one scan of every channel per control pass; ladder_read() reads the
 * cache. g_lad_ok = 0 until the first scan (or after a failed one) -> the old
 * blocking single-channel path, unchanged. */
static int16_t g_lad_buf[NUM_LADDERS];
static int     g_lad_val[NUM_LADDERS];
static uint8_t g_lad_ok;
static uint8_t g_lad_scan_ok;   /* init: every channel id == its ladder index (buffer order) */
static void ladder_scan(void)
{
	if (!g_lad_scan_ok) { g_lad_ok = 0; return; }
	int16_t first[NUM_LADDERS];
	struct adc_sequence seq = {
		.buffer      = g_lad_buf,
		.buffer_size = sizeof(g_lad_buf),
	};
	if (adc_sequence_init_dt(&adc_ladder[0], &seq) < 0) { g_lad_ok = 0; return; }
	seq.channels = 0u;
	for (int i = 0; i < (int)NUM_LADDERS; i++) seq.channels |= BIT(adc_ladder[i].channel_id);
	if (adc_read_dt(&adc_ladder[0], &seq) < 0) { g_lad_ok = 0; return; }
	memcpy(first, g_lad_buf, sizeof(first));
	if (adc_read_dt(&adc_ladder[0], &seq) < 0) { g_lad_ok = 0; return; }
	for (int i = 0; i < (int)NUM_LADDERS; i++)
		g_lad_val[i] = (int)(((int32_t)first[i] + (int32_t)g_lad_buf[i]) / 2);
	g_lad_ok = 1;
}
static int ladder_read(const struct adc_dt_spec *spec)
{
	if (g_lad_ok) return g_lad_val[spec - adc_ladder];   /* ADCSCAN-710: this pass's scan */
	struct adc_sequence seq = {
		.buffer      = &adc_sample,
		.buffer_size = sizeof(adc_sample),
	};
	if (adc_sequence_init_dt(spec, &seq) < 0)
		return -1;
	int32_t acc = 0;
	for (int n = 0; n < 2; n++) {
		if (adc_read_dt(spec, &seq) < 0)
			return -1;
		acc += adc_sample;
	}
	return (int)(acc / 2);
}

/* Power the ladder rail, set up the ADC channels, bring USB up. Safe to call
 * once at boot; never blocks waiting for a host. */
static void controls_init(void)
{
	BTN_COM_PORT->OUTSET = (1u << BTN_COM_PIN);
	BTN_COM_PORT->PIN_CNF[BTN_COM_PIN] =
		(GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)   |
		(GPIO_PIN_CNF_DRIVE_S0S1    << GPIO_PIN_CNF_DRIVE_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
	BTN_COM_PORT->OUTSET = (1u << BTN_COM_PIN);

	g_lad_scan_ok = 1u;   /* ADCSCAN-710 */
	for (int i = 0; i < NUM_LADDERS; i++) {
		if (device_is_ready(adc_ladder[i].dev))
			adc_channel_setup_dt(&adc_ladder[i]);
		if (adc_ladder[i].channel_id != (uint8_t)i || adc_ladder[i].dev != adc_ladder[0].dev)
			g_lad_scan_ok = 0u;   /* the scan's buffer order would not be the ladder order: keep the old path */
	}

	/* USB is brought up later in main() on the device_next stack (UAC2 audio
	 * + CDC console composite); nothing to enable here anymore. */
}

/* Drive one bare-metal GPIO high (used to release the codec reset lines). */
static void gpio_drive_high(NRF_GPIO_Type *port, uint32_t pin)
{
	port->OUTSET = (1u << pin);
	port->PIN_CNF[pin] =
		(GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)   |
		(GPIO_PIN_CNF_DRIVE_S0S1    << GPIO_PIN_CNF_DRIVE_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
	port->OUTSET = (1u << pin);
}

static void gpio_drive_low(NRF_GPIO_Type *port, uint32_t pin)
{
	port->OUTCLR = (1u << pin);
}

/* Release the codec resets, then probe every I2C address once and record
 * which devices answer. Reading a single byte is a harmless presence test. */
static void codec_init(void)
{
	gpio_drive_high(CS42_RST_PORT, CS42_RST_PIN);   /* un-reset CS42L42 */
	gpio_drive_high(TAS_RST_PORT,  TAS_RST_PIN);    /* un-reset TAS2505 */
	k_msleep(20);

	if (!device_is_ready(i2c_bus))
		return;

	i2c_found_n = 0;
	cs42_ok = tas_ok = false;
	for (uint8_t a = 0x08; a <= 0x77; a++) {
		uint8_t b;
		if (i2c_read(i2c_bus, &b, 1, a) == 0) {
			if (i2c_found_n < (int)sizeof(i2c_found))
				i2c_found[i2c_found_n++] = a;
			if (a == CS42L42_ADDR) cs42_ok = true;
			if (a == TAS2505_ADDR) tas_ok = true;
		}
	}
	i2c_scanned = true;
}

/* ========================= I2S audio bus =================================
 * CLOCK TOPOLOGY (the way Teenage Engineering wired the board — see the
 * SP-1-dev wiki): the on-board 3.072 MHz oscillator (enabled via OSC_EN
 * P0.13) drives the shared I2S bit clock, and the CS42L42 headphone codec is
 * the FRAME master — it divides that oscillator by 64 to make a LRCK of
 * exactly 3.072 MHz / 64 = 48000 Hz. The nRF52840 I2S peripheral and the
 * TAS2505 speaker amp are both CLOCK SLAVES on this bus.
 *
 * (Pins: SCLK P0.12, LRCK P0.11, DOUT P1.09.)
 *
 * An earlier design had the nRF master the clocks at ~47619 Hz instead — it
 * crackled on the speaker and produced only noise on the headphones, because
 * the CS42L42 was never given the clock it was built to run from. Driving the
 * board the way TE intended fixed both, so everything below assumes a true,
 * codec-mastered 48.000 kHz. */
#define OSC_EN_PORT     NRF_P0
#define OSC_EN_PIN      13u

#define I2S_SR          48000
#define I2S_TRUE_HZ     48000u   /* real LRCK = osc / 64, CS42L42 is frame master */
#define BLK_FRAMES      256
#define BLK_BYTES       (BLK_FRAMES * 2 * (int)sizeof(int16_t))   /* stereo 16-bit slots */

K_MEM_SLAB_DEFINE(tx_slab, BLK_BYTES, 8, 4);    /* TXSLAB-773: 8 blocks (was 10, "the PROVEN WORKING.bin
                                                 * value"). U3-471's probe printed txhi=7 on every capture
                                                 * since it went in, the corner included, and 7 is
                                                 * STRUCTURAL: the nrfx TX queue (CONFIG_I2S_NRFX_TX_BLOCK_COUNT
                                                 * = 4) + the two EasyDMA buffers + the block being filled.
                                                 * 8 = 7 + one spare for the failsafe re-prime. -2,048 B. */
static const struct device *const i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));

static int  audio_cfg_rc = 1;        /* i2s_configure() result, for serial diag */
static bool tas_cfg_ok;              /* did the TAS2505 register writes all ACK?  */
static volatile bool audio_started;  /* did i2s START trigger fire?               */

/* ---- TAS2505 speaker-amp setup (ported from TimK SP-1-dev, 16-bit I2S) ---- */
static int tas_wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c_bus, TAS2505_ADDR, reg, val);
}
static void tas_page(uint8_t p) { (void)tas_wr(0x00, p); }

/* Power the speaker amp on/off (page-1 reg 0x2D: 0x02 = driver up, 0x00 = off).
 * Used by the headphone auto-mute. Main-thread only (audio thread never touches
 * I2C after init), so no locking needed. */
static void tas_set_speaker(int on)
{
	tas_page(0x01);
	(void)tas_wr(0x2D, on ? 0x02 : 0x00);
	tas_page(0x00);
}

/* TAS2505 speaker bring-up, following TI Application Reference Guide SLAU472C
 * Section 5.1 ("Play Digital Data Through DAC and Headphone/Speaker Outputs").
 *
 * CLOCKING: the speaker DAC is clocked from a PLL locked to the I2S bit clock
 * (BCLK = the 3.072 MHz oscillator). The PLL multiplies BCLK so the DAC's
 * internal rates land where the sigma-delta modulator wants them:
 *   f_PLL  = BCLK x J = 3.072 MHz x 32 = 98.304 MHz
 *   DAC_FS = f_PLL / (NDAC2 x MDAC8 x DOSR128) = 48000 Hz  (exact)
 * Locking to BCLK (not a free-running MCLK) means the DAC tracks the bus
 * exactly, so the speaker never slips or crackles. BCLK must be running before
 * the PLL can lock, so the I2S stream is started before this runs. */
static bool tas2505_configure(void)
{
	int rc = 0;

	tas_page(0x00);
	rc |= tas_wr(0x01, 0x01);          /* software reset */
	k_msleep(5);

	/* Page 1: LDO output 1.8 V, analog level shifters powered up */
	tas_page(0x01);
	rc |= tas_wr(0x02, 0x00);

	/* Page 0: clocking (PLL locked to BCLK) + interface */
	tas_page(0x00);
	rc |= tas_wr(0x04, 0x07);          /* PLL_CLKIN = BCLK, CODEC_CLKIN = PLL */
	rc |= tas_wr(0x05, 0x91);          /* PLL powered, P=1, R=1 */
	/* TE-native bus: BCLK = the 3.072 MHz oscillator, WCLK = 48000 (64 SCLK per
	 * frame, CS42L42 frame master). PLL J=32 puts f_PLL = 3.072M x 32 = 98.304
	 * MHz (inside the ~80-110 MHz lock range); NDAC=2 x MDAC=8 x DOSR=128 = 2048
	 * brings DAC_FS = 98.304M/2048 = 48000 = WCLK exactly. */
	rc |= tas_wr(0x06, 0x20);          /* PLL J = 32  -> f_PLL = 98.304 MHz */
	rc |= tas_wr(0x07, 0x00);          /* PLL D = 0 (MSB) */
	rc |= tas_wr(0x08, 0x00);          /* PLL D = 0 (LSB) */
	k_msleep(15);                      /* wait for PLL to lock */
	rc |= tas_wr(0x0B, 0x82);          /* NDAC = 2, powered */
	rc |= tas_wr(0x0C, 0x88);          /* MDAC = 8, powered */
	rc |= tas_wr(0x0D, 0x00);          /* DOSR MSB */
	rc |= tas_wr(0x0E, 0x80);          /* DOSR = 128 -> DAC_FS = 48000 */
	rc |= tas_wr(0x1B, 0x00);          /* I2S, 16-bit, slave (matches nRF Philips I2S) */
	rc |= tas_wr(0x1C, 0x00);          /* data slot offset 0 */
	rc |= tas_wr(0x3C, 0x02);          /* DAC processing block PRB_P2 (mono) */

	/* DAC power + digital volume — these live on PAGE 0 */
	rc |= tas_wr(0x3F, 0x90);          /* DAC powered, left data -> left, soft-step */
	rc |= tas_wr(0x41, 0x00);          /* DAC digital gain 0 dB */
	rc |= tas_wr(0x40, 0x04);          /* DAC not muted */

	/* Page 1: analog reference, routing, speaker driver */
	tas_page(0x01);
	rc |= tas_wr(0x01, 0x10);          /* master analog reference powered ON */
	rc |= tas_wr(0x0A, 0x00);          /* output common mode 0.9 V */
	rc |= tas_wr(0x0C, 0x04);          /* Mixer P out -> output mixer (DAC routed) */
	rc |= tas_wr(0x16, 0x00);          /* HP volume 0 dB */
	rc |= tas_wr(0x18, 0x00);          /* AINL volume / mixer */
	rc |= tas_wr(0x09, 0x20);          /* power up HP driver */
	rc |= tas_wr(0x10, 0x00);          /* unmute HP, 0 dB */
	rc |= tas_wr(0x2E, 0x00);          /* speaker attenuation 0 dB (max) */
	/* Class-D driver gain, P1/R48 bits D6-D4: 000=mute 001=6dB 010=12dB
	 * 011=18dB 100=24dB. Was 6 dB — user wants a fair bit louder; 18 dB is
	 * one step below the chip's max (24 dB = 0x40 if ever needed). */
	rc |= tas_wr(0x30, 0x30);          /* speaker driver gain +18 dB */
	rc |= tas_wr(0x2D, 0x02);          /* speaker driver powered up */

	tas_page(0x00);
	k_msleep(10);

	tas_cfg_ok = (rc == 0);
	return tas_cfg_ok;
}

/* ---- HEADPHONE output (always on), SELF-SELECTING driver ---------------------
 * Probes the codec and picks the right register scheme at boot:
 *   PATH 1 (expected): a real CS42L42/CS42L83 — our chip ACKs 0x48, the genuine
 *     CS42L42 address. Full 16-bit paged init taken VERBATIM from the Linux
 *     kernel driver (sound/soc/codecs/cs42l42.c): PLL from SCLK using the
 *     1.536 MHz pll_ratio_table row {prediv 0, div_int 0x7D, frac 0, mode 3,
 *     divout 0x10 x n 2, cal 125, mclk_int 12 MHz}. Our SCLK is 1.5238 MHz
 *     (-0.8%), so every derived clock scales with the bus = self-consistent.
 *     CRITICALLY this path needs NO MCLK: the 3.072 MHz oscillator stays OFF —
 *     turning it on permanently was what made the speaker crackle (the comment
 *     in audio_init predicted exactly that).
 *   PATH 2 (fallback): TimK's 8-bit-register variant (SP-1-dev repo, forum-
 *     confirmed on his unit at 0x4A). Only this path powers the oscillator,
 *     since his CLK_CTL 0x04 is MCLK auto-detect.
 * No jack detect — headphones simply run alongside the speaker. */
static volatile int g_hp_on;     /* diag: 0=none, 1=CS42L42 16-bit, 2=TimK 8-bit */
static uint8_t g_cs42_addr = CS42L42_ADDR;
static uint8_t g_cs42_id8;       /* diag: 8-bit-scheme reg 0x01 readback */
static uint8_t g_cs42_dev[3];    /* diag: 16-bit DEVID A/B, C/D, E (0x42 0xA4 x = CS42L42) */
static uint8_t g_hp_pll;         /* diag: PLL lock status readback */
static volatile int g_hp_in = -1;   /* headphones detected in jack: 1 yes, 0 no, -1 unknown */
static bool cs42_wr8(uint8_t reg, uint8_t val)
{
	uint8_t b[2] = { reg, val };
	return i2c_write(i2c_bus, b, 2, g_cs42_addr) == 0;
}
static bool cs42_rd8(uint8_t reg, uint8_t *val)
{
	return i2c_write_read(i2c_bus, g_cs42_addr, &reg, 1, val, 1) == 0;
}
static bool cs42_wr16(uint16_t reg, uint8_t val)
{
	uint8_t b[3] = { (uint8_t)(reg >> 8), (uint8_t)reg, val };
	return i2c_write(i2c_bus, b, 3, g_cs42_addr) == 0;
}
static bool cs42_rd16(uint16_t reg, uint8_t *val)
{
	uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
	return i2c_write_read(i2c_bus, g_cs42_addr, a, 2, val, 1) == 0;
}
/* HP_TIM_TEST 1 builds the SEPARATE headphone test binary: the exact init from
 * Tim Knapen's wiki (github.com/timknapen/SP-1-dev/wiki/I2C — proven on real
 * SP-1 hardware, and it uses the page-select protocol we independently
 * confirmed), adapted to OUR clock topology: nRF stays I2S master, the 3.072 MHz
 * oscillator stays OFF (TE's design has the osc drive the shared SCLK line —
 * enabling it against the nRF master is what caused the crackle), PLL row for
 * our 1.524 MHz SCLK, 16-bit channels. Key registers Tim has that we never
 * wrote: 0x1007 (Serial Port SRC routing), 0x2601/0x2609 (SRC rates), 0x240E
 * (EQ input unmute), 0x1121 (headset switch). The main binary keeps 0. */
#ifndef HP_TIM_TEST
#define HP_TIM_TEST 1     /* Tim-wiki headphone init is now the NORMAL build */
#endif
#if HP_TIM_TEST
static bool tpw(uint16_t reg, uint8_t val)   /* paged write: page reg 0x00 first */
{
	uint8_t p[2] = { 0x00, (uint8_t)(reg >> 8) };
	uint8_t b[2] = { (uint8_t)reg, val };
	if (i2c_write(i2c_bus, p, 2, g_cs42_addr) != 0) return false;
	return i2c_write(i2c_bus, b, 2, g_cs42_addr) == 0;
}
static bool tpr(uint16_t reg, uint8_t *val)  /* paged read */
{
	uint8_t p[2] = { 0x00, (uint8_t)(reg >> 8) };
	uint8_t o = (uint8_t)reg;
	if (i2c_write(i2c_bus, p, 2, g_cs42_addr) != 0) return false;
	return i2c_write_read(i2c_bus, g_cs42_addr, &o, 1, val, 1) == 0;
}

/* Headphone presence from the CS42L42: DET_STATUS1 (page 0x1B reg 0x77) bit7,
 * per Tim's wiki "request headphone status". 1=plugged, 0=unplugged, -1=read failed. */
static int hp_detect_connected(void)
{
	uint8_t st;
	if (!tpr(0x1B77, &st)) return -1;
	return (st >> 7) & 1;
}
#endif

static void hp_codec_init(int pllcfg)
{
	static const uint8_t addrs[2] = { 0x48u, 0x4Au };
	(void)pllcfg;                /* unused when the HP graft is compiled out */
	g_hp_on = 0;

#if HP_TIM_TEST
	/* hard reset pulse — without it the codec is wedged and NAKs everything */
	gpio_drive_low(CS42_RST_PORT, CS42_RST_PIN);
	k_msleep(5);
	gpio_drive_high(CS42_RST_PORT, CS42_RST_PIN);
	k_msleep(10);

	g_cs42_addr = 0x48u;
	(void)tpr(0x1001, &g_cs42_dev[0]);            /* DEVID_AB (0x42 = CS42L42) */
	(void)tpr(0x1002, &g_cs42_dev[1]);
	(void)tpr(0x1003, &g_cs42_dev[2]);
	if (g_cs42_dev[0] != 0x42) return;            /* not answering -> leave alone */

	/* ===== TIM'S WIKI SEQUENCE, VERBATIM — native TE topology. =====
	 * The CS42L42 is the I2S frame MASTER here (its designed role on this
	 * board): PLL referenced from the oscillator-driven 3.072 MHz SCLK, LRCK
	 * generated at exactly 48 kHz, the nRF and TAS2505 follow as slaves.
	 * Every value below is from github.com/timknapen/SP-1-dev/wiki/I2C, the
	 * config proven to play headphone audio on this exact hardware. The ONLY
	 * deviation was mixer volume -19 dB; RESTORED to his full-scale 0x00 —
	 * the -19 dB pad capped max headphone loudness ~1/9th of stock. The
	 * digital path already soft-limits before the codec, so 0 dB is safe. */
	(void)tpw(0x1508, 0x10);   /* PLL Control 3                         */
	(void)tpw(0x1504, 0x80);   /* PLL Division Fractional Byte 2        */
	(void)tpw(0x1505, 0x3E);   /* PLL Division Integer                  */
	(void)tpw(0x150A, 0x7D);   /* PLL Calibration Ratio                 */
	(void)tpw(0x1009, 0x00);   /* MCLK Control                          */
	(void)tpw(0x1201, 0x01);   /* MCLK Source Select                    */
	(void)tpw(0x120A, 0x01);   /* Input ASRC Clock Select               */
	(void)tpw(0x120B, 0x01);   /* Output ASRC Clock Select              */
	(void)tpw(0x1501, 0x01);   /* PLL Control 1: start                  */
	(void)tpw(0x1107, 0x01);   /* Oscillator Switch (SCLK is running —
	                              the 3.072 MHz osc drives it)          */
	for (int t = 0; t < 10; t++) {   /* wait for "SCLK selected" (0x02) */
		k_msleep(1);
		if (tpr(0x1109, &g_hp_pll) && g_hp_pll == 0x02) break;
	}
	(void)tpw(0x1007, 0x13);   /* Serial Port SRC Control               */
	(void)tpw(0x1203, 0x1F);   /* FSYNC Pulse Width Lower (64-SCLK frame) */
	(void)tpw(0x1205, 0x3F);   /* FSYNC Period Lower                    */
	(void)tpw(0x1207, 0x34);   /* ASP Clock Config: MASTER              */
	(void)tpw(0x1208, 0x1A);   /* ASP Frame Configuration               */
	(void)tpw(0x2A02, 0x02);   /* Channel 1: 24-bit                     */
	(void)tpw(0x2A05, 0x42);   /* Channel 2: phase + 24-bit             */
	(void)tpw(0x2601, 0x4C);   /* SRC Input Sample Rate                 */
	(void)tpw(0x2609, 0x4C);   /* SRC Output Sample Rate                */
	(void)tpw(0x2A01, 0x0C);   /* ASP Receive Enable                    */
	(void)tpw(0x240E, 0x01);   /* Equalizer Input Mute Control          */
	(void)tpw(0x2301, 0x00);   /* Mixer A vol 0 dB (Tim's full scale)   */
	(void)tpw(0x2303, 0x00);   /* Mixer B vol                           */
	(void)tpw(0x1101, 0x96);   /* power up the codec                    */
	k_msleep(10);              /* HP amp operational after 10 ms        */
	(void)tpw(0x1121, 0x41);   /* Headset switch control                */
	(void)tpw(0x1B74, 0x03);   /* Miscellaneous detect control          */
	(void)tpw(0x1129, 0x01);   /* Headset clamp disable                 */
	(void)tpw(0x2001, 0x0D);   /* HP Control: mute all                  */
	(void)tpw(0x1F06, 0x84);   /* DAC Control 2                         */
	(void)tpw(0x2301, 0x00);   /* Mixer A vol again                     */
	(void)tpw(0x2303, 0x00);   /* Mixer B vol again                     */
	(void)tpw(0x1B73, 0xC2);   /* Tip Sense Control                     */
	(void)tpw(0x1B75, 0x9F);   /* Mic detect control 1                  */
	(void)tpw(0x2001, 0x01);   /* UNMUTE headphones                     */
	g_hp_on = 1;
	return;
#endif

	for (int a = 0; a < 2; a++) {
		g_cs42_addr = addrs[a];

		/* read both ID schemes (8-bit read first: harmless on either chip) */
		uint8_t id8 = 0;
		if (!cs42_rd8(0x01, &id8)) continue;          /* nothing ACKs here */
		g_cs42_id8 = id8;
		(void)cs42_rd16(0x1001, &g_cs42_dev[0]);      /* CS42L42_DEVID_AB */
		(void)cs42_rd16(0x1002, &g_cs42_dev[1]);      /* CS42L42_DEVID_CD */
		(void)cs42_rd16(0x1003, &g_cs42_dev[2]);      /* CS42L42_DEVID_E  */

		if (g_cs42_dev[0] == 0x42) {
			/* ---- PATH 1: genuine CS42L42/L83, kernel-exact init ---- */
			/* clocking: internal-FS = 12 MHz family (mclk_int 12000000) */
			(void)cs42_wr16(0x1009, 0x00);  /* MCLK_CTL: INTERNAL_FS=0     */
			/* PLL dividers — pll_ratio_table row for SCLK 1.536 MHz      */
			(void)cs42_wr16(0x120C, 0x00);  /* PLL_DIV_CFG1: SCLK_PREDIV /1 */
			(void)cs42_wr16(0x1505, 0x7D);  /* PLL_DIV_INT   0x7D (125)    */
			(void)cs42_wr16(0x1502, 0x00);  /* PLL_DIV_FRAC0               */
			(void)cs42_wr16(0x1503, 0x00);  /* PLL_DIV_FRAC1               */
			(void)cs42_wr16(0x1504, 0x00);  /* PLL_DIV_FRAC2               */
			(void)cs42_wr16(0x151B, 0x03);  /* PLL_CTL4: mode 3            */
			(void)cs42_wr16(0x1508, 0x20);  /* PLL_CTL3: DIVOUT 0x10 * n=2 */
			(void)cs42_wr16(0x150A, 0x7D);  /* PLL_CAL_RATIO 125           */
			/* serial port: slave I2S, 50/50 frame, 1.0-cycle FSD, 16-bit  */
			(void)cs42_wr16(0x1207, 0x20);  /* ASP_CLK_CFG: SCLK_EN, slave */
			(void)cs42_wr16(0x1208, 0x0A);  /* ASP_FRM_CFG: 5050 | FSD_1_0 */
			(void)cs42_wr16(0x2A02, 0x01);  /* RX CH1: AP low,  RES 16-bit */
			(void)cs42_wr16(0x2A03, 0x00);  /* CH1 bit offset MSB          */
			(void)cs42_wr16(0x2A04, 0x00);  /* CH1 bit offset LSB          */
			(void)cs42_wr16(0x2A05, 0x41);  /* RX CH2: AP high, RES 16-bit */
			(void)cs42_wr16(0x2A06, 0x00);  /* CH2 bit offset MSB          */
			(void)cs42_wr16(0x2A07, 0x00);  /* CH2 bit offset LSB          */
			(void)cs42_wr16(0x2A01, 0x0C);  /* ASP_RX_DAI0_EN: CH1+CH2     */
			(void)cs42_wr16(0x1209, 0x03);  /* FS_RATE_EN: IASRC+OASRC 96K */
			(void)cs42_wr16(0x120A, 0x00);  /* IN_ASRC_CLK: IASRC_SEL_6    */
			(void)cs42_wr16(0x2301, 0x00);  /* MIXER_CHA_VOL: 0 dB         */
			(void)cs42_wr16(0x2303, 0x00);  /* MIXER_CHB_VOL: 0 dB         */
			/* power up: keep ASP-TX, EQ, ADC down; enable DAI+MIXER+HP    */
			(void)cs42_wr16(0x1101, 0x94);  /* PWR_CTL1                    */
			k_msleep(5);
			/* start the PLL (reference = SCLK, runs whenever I2S runs)    */
			(void)cs42_wr16(0x1501, 0x01);  /* PLL_CTL1: PLL_START         */
			for (int t = 0; t < 40; t++) {  /* poll PLL_LOCK_STATUS 0x130E */
				k_msleep(1);
				if (cs42_rd16(0x130E, &g_hp_pll) && (g_hp_pll & 1))
					break;
			}
			(void)cs42_wr16(0x1201, 0x01);  /* MCLK_SRC_SEL: PLL           */
			(void)cs42_wr16(0x1107, 0x01);  /* OSC_SWITCH: SCLK present    */
			k_msleep(2);
			(void)cs42_wr16(0x2001, 0x00);  /* HP_CTL: unmute A+B          */
			g_hp_on = 1;
			return;
		}
		if ((id8 & 0xF8) == 0x20) {
			/* ---- PATH 2: TimK's 8-bit variant (needs the MCLK osc) ---- */
			gpio_drive_high(OSC_EN_PORT, OSC_EN_PIN);
			k_msleep(5);
			(void)cs42_wr8(0x1D, 0x00);   /* out of hibernate            */
			(void)cs42_wr8(0x1B, 0x04);   /* CLK_CTL: MCLK auto-detect   */
			(void)cs42_wr8(0x2F, 0x01);   /* ASP RX: slave, I2S          */
			(void)cs42_wr8(0x30, 0x60);   /* ASP RX fmt                  */
			(void)cs42_wr8(0x1C, 0x07);   /* signal path: ASP->DAC->HP   */
			(void)cs42_wr8(0x19, 0x00);   /* power on                    */
			(void)cs42_wr8(0x1D, 0x00);   /* unmute HP                   */
			(void)cs42_wr8(0x35, 19);     /* vol A                       */
			(void)cs42_wr8(0x36, 19);     /* vol B                       */
			k_msleep(10);
			g_hp_on = 2;
			return;
		}
	}
}

static void hp_init(void)
{
	hp_codec_init(0);
}

/* ---- continuous I2S TX thread ---- */
static K_THREAD_STACK_DEFINE(audio_stack, 1536);  /* RD-474: was 3072. 473 U4S measured 408 B peak over a 76.6 s corner with the audio thread fully exercised -> 3.8x margin. */  /* +1K margin over the historical 2048: the
                                                   * PREEMPT(0) mixer takes USB-thread
                                                   * preemptions (incl. FPU lazy-stacking
                                                   * frames) on top of its own worst case —
                                                   * the top-ranked candidate for the
                                                   * unexplained record-start crash */
static struct k_thread audio_tcb;

/* Fill one stereo I2S block with silence. Used to prime the I2S DMA at start-up
 * and after an underrun recovery, before the looper engine takes over. */
static void fill_block(int16_t *s)
{
	memset(s, 0, BLK_FRAMES * 2 * sizeof(int16_t));
}

/* ================== Milestone 3: USB-C audio in (UAC2) ==================
 * The host streams 48 kHz / 16-bit / stereo PCM into the SP-1 over a USB
 * isochronous OUT endpoint. The UAC2 data callback (USB thread) pushes those
 * 16-bit frames into this lock-free SPSC ring; audio_thread (below) drains the
 * ring, expands each sample into the existing 24-bit I2S word, and clocks it out
 * to the TAS2505 speaker. The ring is the elastic buffer that absorbs the gap
 * between the host's 48000 Hz send rate and the SP-1's 48000 Hz I2S rate; the
 * explicit-feedback regulator keeps it centred (see feedback_update). */
#define USB_FRAME_BYTES   4u                 /* 2 ch * 16-bit */
#define USB_RING_FRAMES   1024u              /* USBRING-746: was 2048 (RD-474), 4096 before that. This ring buffers the
                                              * host's UAC2 stream, i.e. the RECORD SOURCE.
                                              * The old note warned the 2048 trim "was never
                                              * validated and rode along in every failed build".
                                              * It is validated now: cumulative-since-boot peak
                                              * fill was 1262/4096 (471) and 1199/4096 (473),
                                              * with uo=0 uu=0 both runs. FB_SETPOINT is 1024,
                                              * so at 2048 the regulator sits DEAD CENTRE with
                                              * 1024 frames each way vs a worst measured
                                              * excursion of +238 -- a better-shaped elastic
                                              * buffer than 4096, where 75%% of it sat above the
                                              * setpoint and was unreachable when regulated.
                                              * FALSIFIERS: uo>0, ufl climbing, U3B ringhi
                                              * near 2048, or input glitching by ear. Any of
                                              * those and this goes back to 4096.
                                              * USBRING-746 (STACK R R2a): 1024 frames, setpoint 512 --
                                              * still centred, 512 frames of slack each way against
                                              * the +238 worst excursion ever measured (RD-474), and
                                              * uo=0 uu=0 on every corner since. The 4,096 B fund the
                                              * reverb's own line (RVLINE-746). FALSIFIERS: uu>0, uo>0
                                              * or zp>0 at the corner with USB in -> back to 2048. */
/* Target ring fill (frames, ~21 ms). Used both as the prebuffer target before
 * the consumer starts draining a freshly-enabled stream, and as the feedback
 * regulator's setpoint, so the hand-off from prebuffering to draining is smooth. */
#define FB_SETPOINT       512   /* USBRING-746: half the 1024 ring (was 1024 of 2048) */
RING_BUF_DECLARE(usb_audio_ring, USB_RING_FRAMES * USB_FRAME_BYTES);

static volatile bool g_usb_streaming;        /* host has enabled the UAC2 terminal */

/* Diagnostics streamed over the CDC console (controls_diag): if the ring keeps
 * underrunning (drain faster than host delivers) or overflowing (host faster),
 * the rate-matching is off and audio will glitch. If both stay ~0 but it still
 * sounds wrong, the problem is NOT the buffer (look at level/codec instead). */
static volatile uint32_t g_ring_underruns;
static volatile uint32_t g_ring_overflows;
static volatile uint32_t g_usb_pkts;               /* diag: ISO packets received (~1000/s streaming) */
static volatile uint32_t g_usb_frames;             /* diag: audio frames received (~48000/s streaming) */
static volatile uint32_t g_sof_cnt;                /* diag: SOFs seen by the feedback regulator (1000/s) */
static volatile uint32_t g_zero_pad;               /* diag: silence frames padded into short blocks */
static volatile uint32_t g_rx_nobuf;               /* diag: ISO packets DROPPED — rx pool empty (the
                                                    * exact mechanism: ISO never retries a NAKed buffer) */
static volatile uint32_t g_rx_slab_min = 0xFFFF;   /* diag: window MIN free rx buffers */
static volatile int32_t  g_usb_lowat = 0x7FFFFFFF; /* diag: window MIN usb-in ring fill, frames */
static volatile uint32_t g_usb_hiwat;              /* diag: window MAX usb-in ring fill, frames */
/* U3-471 (RAM/U-diet grounding): CUMULATIVE peaks -- never reset, so a
 * blind Protocol-A run can be read afterwards. Capacities for reference:
 * usb_audio_ring 4096 frames, tx_slab 10 blocks, uac2 rx slab (Zephyr). */
static volatile uint32_t g_u3_ring_hi;             /* max usb-in fill, frames */
static volatile uint32_t g_u3_ring_lo = 0xFFFFFFFFu;/* min usb-in fill while primed */
static volatile uint32_t g_u3_tx_hi;               /* max tx_slab blocks in use */
static volatile uint32_t g_u3_rx_lo = 0xFFFFu;     /* min FREE uac2 rx buffers */

/* Drain up to BLK_FRAMES stereo frames from the USB ring into one I2S block,
 * expanding each 16-bit sample into the 24-in-32-bit I2S word with the same <<8
 * left-justify the sine path uses. Underrun (ring empty) -> silence. */
/* Output volume / headroom, Q8 (256 = unity). The PLAY test tone that sounds
 * clean is generated at amplitude 6000/32768 ~= 0.18 of full scale (~-15 dB); the
 * little speaker + TAS2505 +6 dB driver distort well below full scale. So play
 * USB music at the SAME proven-clean level as that tone: 48/256 ~= 0.1875.
 * 32767 * 48 == tone peak. Raise toward 64/96 for more volume IF it stays clean;
 * lower if loud passages still distort. */
#define SPK_VOL_Q8     48

/* ================== LOOPER ENGINE (4 tracks, eMMC-streamed) ==============
 * Loops are mono int16 decimated from the 48000 Hz live input by DECIM and
 * stored on the eMMC (one region per track). A background streamer thread does
 * the blocking eMMC reads/writes into per-track SPSC rings; THIS audio code only
 * touches RAM. Playback is interpolated back up to the I2S rate; the 4 tracks
 * are mixed with per-track (fader) + master volume over the live monitor.
 * Recording is HOLD-to-record, UNQUANTIZED: the FIRST take you hold sets the
 * master length — exactly what you held, rounded only to the 256-sample storage
 * block (~±19 ms; works for podcasts/speech, nothing snaps or jumps). Overdubs
 * start at the next block (~38 ms = effectively instant) and record exactly one
 * loop, wrapping. "BPM" is just the varispeed label (80 = 1.0x); there is NO
 * tempo grid — the beat constants below only pace the LED pulse + MIDI clock. */
#if SP1_BUILD_24K
#define DECIM            2u                               /* 24 kHz build (see SP1_BUILD_24K) */
#else
#define DECIM            1u                               /* 48 kHz build (default) */
#endif
#define LOOP_RATE        (I2S_TRUE_HZ / DECIM)             /* 48000/DECIM Hz mono */
/* ===== STORAGE CODEC TOGGLE (compile-time) ===============================
 * Loop audio is stored COMPRESSED on flash to cut the WRITE+READ traffic that
 * is the eMMC reliability bottleneck. The audio engine is UNCHANGED: the rec
 * ring (g_rring) and play rings (trk[].pring) and the whole mix stay int16.
 * We ONLY encode on the flash write and decode on the flash read, at the three
 * flush-boundary sites (codec_pack / codec_unpack). SAMP_PER_BLK = int16
 * samples represented by ONE 512-byte flash block; it is codec-conditional.
 *   PCM   (0): 16-bit, 256 samp/blk, 1:1  (current format, memcpy-equivalent)
 *   ULAW  (1):  8-bit G.711 u-law, 512 samp/blk, 2:1
 *   ADPCM (2):  4-bit IMA, self-contained blocks: 4-byte header (predictor
 *               int16 + step-index uint8 + 1 pad) + 508 nibble-bytes = 1016
 *               samp/blk, ~4:1. Predictor RESETS at the start of every block so
 *               any block decodes standalone (random-access loop seeks work).
 * NOTE: 256 and 512 are powers of two; 1016 is NOT. The only bitmask use of
 * SAMP_PER_BLK (the prime align at the promotion site) is converted to a
 * division-based align so the non-power-of-two ADPCM value is correct. All
 * other SAMP_PER_BLK uses are already /,*,%  (block-domain). The int16 ring
 * masks (RING_MASK / RRING_MASK) are sample-domain and stay powers of two. */
#define SP1_CODEC_PCM    0
#define SP1_CODEC_ULAW   1
#define SP1_CODEC_ADPCM  2
#define SP1_CODEC_A7    3u   /* M63b: SP1-ADPCM7, the FROZEN 3.0 codec */
#ifndef SP1_CODEC
#define SP1_CODEC        SP1_CODEC_A7    /* FULL 16-BIT PCM — the proven WORKING.bin
                                           * format (magic SE4A). The u-law/ADPCM
                                           * compressed builds never worked right on
                                           * the user's hardware; do not rebase on
                                           * them again. */
#endif
#if   SP1_CODEC == SP1_CODEC_PCM
#define SAMP_PER_BLK     (EMMC_BLOCK_SIZE / 2u)            /* 256 int16 / block */
#elif SP1_CODEC == SP1_CODEC_ULAW
#define SAMP_PER_BLK     (EMMC_BLOCK_SIZE)                 /* 512 samp / block (8-bit) */
#elif SP1_CODEC == SP1_CODEC_ADPCM
#define SAMP_PER_BLK     1016u                             /* 4B hdr + 508 nibble-bytes = 1016 samp / block (4-bit IMA) */
#elif SP1_CODEC == SP1_CODEC_A7
#define SAMP_PER_BLK     280u   /* FRAMES/block: 16 B hdr + 70x7 B payload (spec 4.1b) */
#else
#error "SP1_CODEC must be 0 (PCM), 1 (ULAW), 2 (ADPCM) or 3 (A7)"
#endif
/* ---- storage codec pack/unpack (full bodies just before streamer_thread) ----
 * codec_pack:   int16 ring -> flash bytes  (encode), one CMD25 burst of n blocks
 * codec_unpack: flash bytes -> int16 ring  (decode), one CMD18 burst of n blocks
 * Both take (ring, ring_mask, ring_start_sample, flashbuf, nblocks) and handle
 * the power-of-two ring wrap internally. PCM = memcpy-equivalent. */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk);
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk);
#define LOOP_BPM_BASE    80u                               /* BPM label for 1.0x varispeed */
/* FULL-RATE LOOPS: the SPIM3 hardware eMMC path measures 1333 blk/s sustained
 * REWRITE (2026-06-12 capture) — 48 kHz mono needs 187.5 blk/s write + 750
 * blk/s read (4 tracks): ~14% / ~60% of capacity. DECIM=1 also means the
 * decimator/interpolator is bit-transparent — loops record and play exactly
 * what the engine hears. Mono remains the only compromise. */
#define BEAT_SAMPLES_I2S 35840u                            /* I2S frames / beat (140 blocks ÷256) */
#define BEAT_SAMPLES_L   (BEAT_SAMPLES_I2S / DECIM)        /* 35840 = 140 blocks (÷256) */
#define BAR_SAMPLES      (BEAT_SAMPLES_L * 4u)             /* 4 beats — for display / phrasing */
#define MAX_BEATS        643u                              /* longest loop 8:00 at 1.0x (the 2.0 long-
                                                            * take bump): 16 songs x 4 tracks = 76.4% of
                                                            * the 4 GB card (7,553,024 blocks), ~912 MB
                                                            * spare. Recording follows tape speed, so a
                                                            * slowed tape holds proportionally more. */
#define MAX_LOOP_SAMPLES (BEAT_SAMPLES_L * MAX_BEATS)
/* eMMC blocks for the longest loop. At 800 beats the 4 songs × 4 tracks use ~452 MB
 * (12 kHz) / ~301 MB (8 kHz) of the 4 GB card. RAM is unchanged (always streamed). */
#define MAX_LOOP_BLOCKS  (MAX_LOOP_SAMPLES / SAMP_PER_BLK)
#define MIDI_DIV         ((BEAT_SAMPLES_L + 12u) / 24u)    /* loop samples per 24-PPQN clock (rounded) */
#define NTRK             4
/* eMMC region per track, rounded UP to a 16-block (8 KB) multiple: the card's
 * internal pages are 8 KB (TE's own format writes 8 KB sectors — see the wiki's
 * Data-Structure page). With regions 8KB-ALIGNED, every 16-block flush burst
 * lands exactly on one internal page and the card can program it without a
 * read-modify-write, which is far slower than a clean page-aligned burst. */
/* round the per-track region UP to a 4096-block (2MB) multiple so every track
 * region stays 2MB-aligned. (The original reason was a pre-erase pass that has
 * since been removed; the alignment is harmless and is kept so the on-card
 * layout / META_MAGIC do not change.) */
#define TRACK_BLOCKS     ((((MAX_LOOP_BLOCKS + 8u) + 4095u) / 4096u) * 4096u)
#define RING_SAMPLES     8192u   /* M63a: STEREO FRAMES (~170 ms @48k; was 16384 mono) */                            /* ~341 ms read-ahead @48k (reverted 8192->16384 to give the compressed codecs comfortable play-ring margin) */
#define RING_MASK        (RING_SAMPLES - 1u)
/* TUNE2-576 (W264): frames of HISTORY the streamer must leave behind the
 * read position, so the wobble's backward offset never wraps into the
 * future. Must exceed WOB_BASE_SAMP + WOB_WOW_PEAK + WOB_FLT_PEAK. */
#define WOB_RING_RSV     0u     /* WOBBUS-673: the wobble left the ring; the streamer fills to the brim again */
/* RA-491: how full a STARVED track's ring must get before it is
 * audible again. Was RING_SAMPLES/2 = 4096 frames = 14.6 blocks =
 * ~85 ms of SILENCE on that track for a ring that ran dry by TWO
 * frames. The starve COUNT was never the audible thing -- the HOLE
 * each starve opens is. /8 = 1024 frames = ~21 ms, still 4x the
 * 5 ms fade-in ramp, so there is real hysteresis left.
 * TE's own engine holds the last good frame on a late read
 * (KB references/12-audio-engine-internals.md: held_frame_), i.e.
 * it never opens a hole at all. This is the cheap half of that. */
#define PLAY_REARM_FRAMES (RING_SAMPLES / 8u)
/* Play-ring critical margin for scheduling decisions: 128 ms expressed in
 * samples at the loop rate — EXPLICIT and codec-independent (the old
 * 24u*SAMP_PER_BLK silently varied 2.5x across codec block sizes). */
#define PLAY_CRIT_SAMPLES (128u * (LOOP_RATE / 1000u))

/* ---- SONG SLOTS + eMMC layout ----------------------------------------------
 * The looper owns the whole eMMC starting at block 0: block 0 holds the slot
 * metadata (this OVERWRITES the original TE "ALBUM_PR" index, deleting the songs
 * and reclaiming the space — they couldn't be played anyway), tracks follow.
 * NUM_SLOTS independent songs, each with its own saved BPM + 4 tracks. There are
 * 16 songs shown on the 4 status LEDs with TWO lights: the POSITION LED
 * (song % 4) is solid and the BANK LED (song / 4) blinks ~2 Hz. When the two
 * roles land on the same LED (songs 1, 6, 11, 16) it flutters fast (~4 Hz). */
#define NUM_SLOTS        16u
#define META_BLOCK       0u
#define META_BLOCKS      2u     /* 16-song index = 972 B — the exact 2-block maximum */
#define SLOT0_BLOCK      4096u  /* 2MB-aligned (block 0 = meta, 1-4095 spare) so every trk_blk stays 2MB-aligned */
/* FIXED storage signature: reflashing KEEPS the saved songs (the earlier
 * wipe-on-reflash build stamp is gone — user prefers persistence; double-tap a
 * track to delete it instead). Storage only re-formats if this constant or the
 * layout ever changes. */
/* The two sample-rate builds use different on-flash layouts (TRACK_BLOCKS
 * scales with DECIM), so each gets its own magic: switching builds is detected
 * as "unformatted" and reformats, rather than reading the other rate's data. */
/* The on-flash byte format now depends on BOTH the sample-rate build (DECIM)
 * AND the storage codec (SP1_CODEC): a different codec packs the same loop into
 * a different number of bytes/block, so the two are not interchangeable. Give
 * each (rate,codec) pair its own magic; switching either is detected as
 * "unformatted" and triggers a one-time reformat instead of mis-reading the
 * other format's bytes. */
#if DECIM == 1u
#  if   SP1_CODEC == SP1_CODEC_PCM
#define META_MAGIC       0x53453341 /* 'SE3A' (was 'S816'): a build switch reformats */u                       /* 'S816' — 48 kHz PCM, 16-song 2-block index,
                                                            * 643-beat (8:00) regions. TRACK_BLOCKS
                                                            * differs from earlier layouts, so this is a
                                                            * format break: any other index reads as
                                                            * unformatted and loop storage reformats on
                                                            * first boot — export loops as WAVs first,
                                                            * re-upload after. Grids (block 2) survive,
                                                            * same as site uploads today. */
#  elif SP1_CODEC == SP1_CODEC_ULAW
#define META_MAGIC       0x53455534u                       /* 'SEU4' — 48 kHz, u-law 8-bit */
#  else
#define META_MAGIC       0x53453341u   /* 'SE3A' 3.0 two-tier. MG-509: was
 * 0x53454134 'SEA4', which is the 48 kHz IMA-ADPCM magic AND is listed
 * in SP1-3.0-FORMAT-BREAK-SPEC section 7 among the magics 3.0 must
 * REFUSE. 500 edited the 'S816' literal instead -- but that lives in the
 * SP1_CODEC_PCM arm, which the same script kills by selecting A7, so the
 * edit landed on dead code and the log printed a magic we never built.
 * THIS line is the live #else arm. Verified from the shipped binary. */                       /* 'SEA4' — 48 kHz, IMA-ADPCM 4-bit */
#  endif
#else
#  if   SP1_CODEC == SP1_CODEC_PCM
#define META_MAGIC       0x53453241u                       /* 'SE2A' — 24 kHz, PCM 16-bit */
#  elif SP1_CODEC == SP1_CODEC_ULAW
#define META_MAGIC       0x53455532u                       /* 'SEU2' — 24 kHz, u-law 8-bit */
#  else
#define META_MAGIC       0x53454132u                       /* 'SEA2' — 24 kHz, IMA-ADPCM 4-bit */
#  endif
#endif
static inline uint32_t trk_blk(uint32_t slot, uint32_t t)
{
	return SLOT0_BLOCK + (slot * NTRK + t) * TRACK_BLOCKS;
}
/* loop_len = this song's loop length in loop-samples (a whole number of bars,
 * 0 = empty/no loop yet). Saved so a song resumes at its own length + tempo. */
/* SEGMENT looper: each track also remembers its own length (a whole multiple of
 * the base loop_len) and its phase anchor, so a song reloads with the same
 * per-track loop lengths it was recorded with. */
struct slot_state {
	uint32_t speed_q16;
	uint32_t loop_len;
	uint8_t  present[NTRK];
	uint32_t trk_len[NTRK];      /* per-track length in eMMC blocks (0 -> base) */
	uint32_t trk_start[NTRK];    /* per-track segment-0 transport-block anchor */
};
struct meta_blk {
	uint32_t magic;
	uint32_t cur_slot;
	struct slot_state slot[NUM_SLOTS];
	uint32_t fixed_len;        /* persisted loop-length mode (0=variable, 1=fixed).
	                            * APPENDED after the slots: old metas read 0 here
	                            * (the format zeroes the block), and the transfer
	                            * site reads only the slots, so this is layout-safe. */
	uint32_t trk_content[NUM_SLOTS][NTRK]; /* per-track recorded content length in blocks; 0 = whole
	                                        * track. Also appended in the tail -> layout-safe; a website
	                                        * upload zeroes it (0 = full track = correct for uploads). */
	uint32_t led_full;         /* SETTINGS WORD, site-owned (adopted in
	                            * xfer_commit like fixed_len; tail-appended
	                            * -> layout-safe). BIT 0: 1 = full LED
	                            * brightness (0 = dim, the default). BIT 1
	                            * (M41-r5): 1 = CLASSIC record arm (no head
	                            * recovery), i.e. head recovery is the
	                            * DEFAULT and bit 1 opts out. The struct is
	                            * exactly 1024 B (the 2-block maximum), so
	                            * new settings ride spare bits here. Old
	                            * indexes/sites write 0/1 -> bit 1 = 0. */
	uint8_t  chop[NUM_SLOTS][2]; /* M7a: per-song chop window: [0]=div (0/1=none,
	                              * 2..64), [1]=offset. Zeros = unchopped. */
	uint8_t  song_mode[NUM_SLOTS]; /* LOW nibble, M7c: recorded-with mode stamp:
	                                * 0 = unset (inherit the global preference),
	                                * 1 = variable, 2 = fixed.
	                                * HIGH nibble, M7-r4: per-track MUTE bits
	                                * (bit4 = track 1 .. bit7 = track 4) — a
	                                * song's muted tracks come back muted. Old
	                                * indexes read 0 = no mutes; same 'SE16'. */
};
/* The index must fit its reserved blocks: 16 songs = 972 of 1024 bytes — the
 * exact maximum of a 2-block index (17 would need three). Compile error here
 * beats storage corruption there. */
BUILD_ASSERT(sizeof(struct meta_blk) <= META_BLOCKS * EMMC_BLOCK_SIZE,
	     "meta_blk outgrew its reserved index blocks");
static struct meta_blk   g_meta;
/* ==== M72: the v3 PER-TRACK EXTENDED TABLE (blocks 3-5), M61-proven.
 * Self-validating (magic+sum): stale stock bytes read as "no table".
 * Carries SAMPLE-EXACT anchors/lengths across power cycles — closes
 * the <=2.7 ms block-rounding reload limit (Phase-B), which the v3
 * codec's bigger blocks would have widened to 5.83 ms. Streamer owns
 * writes; audio reads with a consistency cross-check, so a torn or
 * stale entry degrades to the block-derived value, never worse. ==== */
#define X3_MAGIC   0x53453358u        /* 'SE3X' */
#define X3_VER     1u
/* Card-table codec numbering (FREEZES AT SHIP -- see 414 header).
 * 0 PCM16, 1 u-law, 2 IMA4, 3 SP1-ADPCM5, 4 SP1-ADPCM7.
 * NOT the same namespace as the build-time SP1_CODEC_* selector,
 * where A7 == 3. Do not merge them. */
#define X3_CODEC_A7 4u
#define X3_BLK     3u
#define X3_NBLK    3u
struct x3_trk {
	uint32_t start_samps;
	uint32_t len_samps;
	uint32_t content_blocks;
	uint8_t  codec_id;            /* 0 PCM16 1 u-law 2 IMA4 3 A5 4 A7 */
	uint8_t  flags;               /* bit0 stereo content */
	uint8_t  pan;
	uint8_t  rsv;
};
struct x3_tab {
	uint32_t magic;
	uint16_t ver;
	uint16_t sum;                 /* over the entry bytes only */
	uint32_t rsv0, rsv1;
	struct x3_trk t[NUM_SLOTS][NTRK];
};
static struct x3_tab g_x3;
static volatile uint8_t g_x3_ok;
static uint16_t x3_sum(const struct x3_tab *tb)
{
	const uint8_t *p = (const uint8_t *)&tb->t[0][0];
	uint32_t n = (uint32_t)sizeof(tb->t), i;
	uint16_t s = 0;
	for (i = 0; i < n; i++) s = (uint16_t)(s + p[i]);
	return s;
}
static inline __attribute__((always_inline)) int x3_valid(const struct x3_tab *tb)   /* X3RELOAD-677: 2 callers, both inline */
{
	return tb->magic == X3_MAGIC && tb->ver == X3_VER &&
	       tb->sum == x3_sum(tb);
}
static volatile uint32_t g_slot;
/* PLACE-715: the track's place (balance) lives in the x3 row's pan byte; 0 = unset = centre. */
static volatile uint32_t g_place_dirty_ms;   /* last fader move (|1); 0 = clean */
static uint8_t place_get(int i)
{
	return (g_slot < NUM_SLOTS && g_x3.t[g_slot][i].pan) ? g_x3.t[g_slot][i].pan : 128u;
}
static void place_set(int i, uint8_t v)
{
	if (g_slot >= NUM_SLOTS || g_x3.t[g_slot][i].pan == v) return;
	g_x3.t[g_slot][i].pan = v;
	g_place_dirty_ms = k_uptime_get_32() | 1u;
}
static volatile int      g_slot_switch_req;   /* main -> audio: reload tracks for the new slot */
static volatile int      g_meta_save_req;     /* -> streamer: persist g_meta to eMMC */
/* ---- TAPPED GRID (M8a): per-song tempo grid taught by FN-taps ----
 * 4+ taps in rhythm set it (first tap = downbeat); independent of the tape.
 * bpm persists in flash block 2 — unused spare, self-validating 'GRD1' tag +
 * sum, so NO format break, the site never touches it (blocks 0-1 only), and
 * older firmware simply ignores it. Phase is session-only by design: after
 * boot it re-anchors to the next tap run (or provisionally to "now"). */
#define GRID_EXT_BLOCK  2u
#define GRID_EXT_MAGIC  0x31445247u   /* 'GRD1' */
struct grid_ext {
	uint32_t magic;
	uint16_t bpm_q8[NUM_SLOTS];   /* Q8.8 BPM per song, 0 = no grid */
	uint16_t sum;                 /* 16-bit sum of bpm_q8[] (torn-write guard) */
};
BUILD_ASSERT(sizeof(struct grid_ext) <= 512, "grid ext must fit one block");
static volatile uint16_t g_grid_bpm_q8[NUM_SLOTS];
/* GRIDCORE-733: THE GRID FROM THE CORE. A song with a loop has NO anchor: its beat
 * is one n-th of the loop and its "1" is the loop sample O, both persisted (block
 * 2 tail, GRD3). Everything musical is a function of g_consume_pos -- see
 * grid_follow_tape() (the service, kept under its old name for the gates). The
 * tapped clock (g_grid_anchor / g_grid_beat_frames as set by the tap) is only the
 * grid of an EMPTY song; the first take converts it. */
static volatile uint8_t  g_grid_n[NUM_SLOTS];  /* beats in the master loop; 0 = no tape grid */
static volatile uint32_t g_grid_o[NUM_SLOTS];  /* the loop sample the bar starts on, < loop_len */
static uint32_t          g_grid_p_lo;          /* the last g_consume_pos seen (32-bit) ... */
static uint64_t          g_grid_p64;           /* ... and its 64-bit shadow (no 2^32 wrap) */
static uint32_t          g_grid_tick_prev;     /* the tape tick index at the last block */
static uint8_t           g_grid_tick_ok;       /* tick_prev is valid (same regime, no jump) */
static uint8_t           g_grid_src;           /* 0 none, 1 tape-derived, 2 tapped clock (diag + regime edge) */
static volatile uint8_t  g_grid_punch_k;       /* beat-in-bar of the scheduled punch line (first take -> O) */
static volatile uint64_t g_grid_o_req_w;       /* controls thread: "the 1 is at this wall frame" (a tap) */
static volatile uint8_t  g_grid_o_req;         /* ... pending; the service converts it with coherent (W, P) */
static volatile uint32_t g_grid_lap_tk;        /* diag: ticks emitted in the last completed lap (must read 24n) */
static uint32_t          g_grid_lap_acc;       /* ... the running count */
static void __attribute__((noinline)) grid_adopt_loop(uint32_t beats, uint32_t start_samps, uint32_t k);   /* the first take's stop calls it (defined with the service) */
static volatile uint64_t g_grid_anchor;       /* sample-clock frame of a downbeat */
static volatile uint32_t g_grid_beat_frames;  /* I2S frames per grid beat (current song) */
/* STACKT-716: page 7 TAKE & TIMING, per song. */
static volatile int8_t   g_grid_off_q8[NUM_SLOTS];  /* the downbeat offset, -128..127 = -1/2..+1/2 beat */
static volatile uint8_t  g_take_preset[NUM_SLOTS];  /* 0 off, 1/2/4/8 bars (fixed mode: base loops) */
static volatile uint8_t  g_trk_nudge[NUM_SLOTS][NTRK];   /* 717: the loop-phase nudge, 128 = centre, 0 = unset (stored here for the tail) */
static volatile uint32_t g_take_auto_at;            /* recorder: raise the stop when rec_count reaches this */
static int16_t g_seam_head[64][2];                  /* SEAMX-727: the take's first 64 stored pairs (128 emissions), the crossfade target of its tail */
static volatile uint32_t g_grid_dirty_ms;           /* last page-7 move (|1); 0 = clean */
static volatile uint64_t g_grid_anchor_e;           /* THE EFFECTIVE ANCHOR every reader uses: raw + the offset */
static uint64_t grid_anchor_eff(void)
{
	/* GRIDCORE-733: the TAPPED clock's effective anchor (an empty song). The readers
	 * subtract it from g_sample_clock in uint64, so the offset is applied as plain
	 * modular arithmetic -- no "bar back" trick. A song with a loop never reads this:
	 * the service publishes g_grid_anchor_e from the tape (its offset is in samples). */
	const uint64_t a = g_grid_anchor;
	const uint32_t bf = g_grid_beat_frames;
	const int32_t q = (g_slot < NUM_SLOTS) ? (int32_t)g_grid_off_q8[g_slot] : 0;
	if (!q || !bf) return a;
	return a + (uint64_t)(((int64_t)q * (int64_t)bf) >> 8);
}
#define GRID_EXT2_OFF   256u
#define GRID_EXT2_MAGIC 0x32445247u   /* 'GRD2' */
struct grid_ext2 {
	uint32_t magic;
	int8_t   off_q8[NUM_SLOTS];
	uint8_t  preset[NUM_SLOTS];
	uint8_t  nudge[NUM_SLOTS][NTRK];
	uint16_t sum;                 /* over off_q8 .. nudge */
};
BUILD_ASSERT(GRID_EXT2_OFF + sizeof(struct grid_ext2) <= 512, "grid ext2 must fit block 2's tail");
/* GRIDCORE-733: the tape grid per song -- n beats in the loop, O the "1" (samples). */
#define GRID_EXT3_OFF   368u
#define GRID_EXT3_MAGIC 0x33445247u   /* 'GRD3' */
struct grid_ext3 {
	uint32_t magic;
	uint8_t  n[NUM_SLOTS];
	uint32_t o[NUM_SLOTS];
	uint16_t sum;                 /* over n .. o */
};
BUILD_ASSERT(GRID_EXT2_OFF + sizeof(struct grid_ext2) <= GRID_EXT3_OFF, "grid ext3 must follow ext2");
BUILD_ASSERT(GRID_EXT3_OFF + sizeof(struct grid_ext3) <= 512, "grid ext3 must fit block 2's tail");
static uint16_t grid_ext3_sum(const struct grid_ext3 *e)
{
	const uint8_t *b = (const uint8_t *)&e->n[0];
	const size_t nb = (size_t)((const uint8_t *)&e->sum - b);
	uint16_t sum = 0;
	for (size_t i = 0; i < nb; i++) sum = (uint16_t)(sum + b[i]);
	return sum;
}
static uint16_t grid_ext2_sum(const struct grid_ext2 *e)
{
	const uint8_t *b = (const uint8_t *)&e->off_q8[0];
	const size_t n = (size_t)((const uint8_t *)&e->sum - b);
	uint16_t sum = 0;
	for (size_t i = 0; i < n; i++) sum = (uint16_t)(sum + b[i]);
	return sum;
}
static void __attribute__((noinline)) grid_ext2_store(uint8_t *blk)
{
	struct grid_ext2 *e = (struct grid_ext2 *)(blk + GRID_EXT2_OFF);
	memset(e, 0, sizeof(*e));
	e->magic = GRID_EXT2_MAGIC;
	for (uint32_t i = 0; i < NUM_SLOTS; i++) {
		e->off_q8[i] = g_grid_off_q8[i]; e->preset[i] = g_take_preset[i];
		for (int k = 0; k < NTRK; k++) e->nudge[i][k] = g_trk_nudge[i][k];
	}
	e->sum = grid_ext2_sum(e);
	{	/* GRIDCORE-733: GRD3 */
		struct grid_ext3 *e3 = (struct grid_ext3 *)(blk + GRID_EXT3_OFF);
		memset(e3, 0, sizeof(*e3));
		e3->magic = GRID_EXT3_MAGIC;
		for (uint32_t i = 0; i < NUM_SLOTS; i++) { e3->n[i] = g_grid_n[i]; e3->o[i] = g_grid_o[i]; }
		e3->sum = grid_ext3_sum(e3);
	}
}
static void __attribute__((noinline)) grid_ext2_load(const uint8_t *blk)
{
	const struct grid_ext2 *e = (const struct grid_ext2 *)(blk + GRID_EXT2_OFF);
	if (e->magic != GRID_EXT2_MAGIC || grid_ext2_sum(e) != e->sum) return;   /* an older card: all zero */
	for (uint32_t i = 0; i < NUM_SLOTS; i++) {
		g_grid_off_q8[i] = e->off_q8[i];
		g_take_preset[i] = (e->preset[i] == 1u || e->preset[i] == 2u || e->preset[i] == 4u || e->preset[i] == 8u) ? e->preset[i] : 0u;
		for (int k = 0; k < NTRK; k++) g_trk_nudge[i][k] = e->nudge[i][k];
	}
	{	/* GRIDCORE-733: GRD3 (an older card: all zero -> migrated at the song load) */
		const struct grid_ext3 *e3 = (const struct grid_ext3 *)(blk + GRID_EXT3_OFF);
		if (e3->magic == GRID_EXT3_MAGIC && grid_ext3_sum(e3) == e3->sum)
			for (uint32_t i = 0; i < NUM_SLOTS; i++) { g_grid_n[i] = e3->n[i]; g_grid_o[i] = e3->o[i]; }
	}
}
static volatile uint64_t g_grid_next_tick;
static uint64_t          g_grid_tick_base;      /* M22-A: exact tick schedule base */
static uint64_t          g_grid_tick_base_sync; /* M22-A: last value WE wrote to next_tick */
static uint32_t          g_grid_tick_idx;       /* M22-A: ticks since the base */
/* GRIDCORE-733: the reference pair, the lock, the rescale counter are gone -- the state sits with g_grid_bpm_q8. */
/* DIAGDEL-733: the M22b-r2 / M25 / M22c grid instrumentation (18 diag words + the
 * convergence landmark) is gone -- RAM for GRIDCORE's per-song n and O. */
/* M43 GRID FREEZE. A grid is a trust contract: loose punches are absorbed
 * by a FIXED clock. Once set — tapped, rounded, or detected — the grid
 * does not move; re-tap to change it. 0 gates the two CONTENT-CHASING
 * retunes (F8 refine at the first gridded stop; M22-C convergence at
 * every gridded stop). KEPT: the M22-A achieved-length retune (the grid
 * following the LOOP's own quantization — removing that would bring back
 * the v2.5.0 grid-vs-loop slide) and the explicit snap gesture. The
 * chase's one deliverable was multi-minute lock to an external AUDIO
 * source: zero field requests in the corpus, two maintainer-found bugs
 * in 48 h (rows 81, 82); external-sync users ask for MIDI clock IN
 * (3.0). The estimator + refine still RUN — g_dbg_rf / g_dbg_cnv_*
 * record what WOULD have been applied, field data for the 3.0 call. */
#define SP1_GRID_FOLLOW 0
/* M23 INTEGER-BPM SNAP (session-only, opt-in). Phase B is what makes this
 * worth having: with block-quantized lengths the flash grid dominated and
 * rounding the BPM changed nothing (at 128 BPM an 8-beat loop was 703.125
 * blocks either way). Sample-exact loops mean an integer BPM lands EXACTLY —
 * 2880000/128 = 22500 frames, no residual — so when the source really is a
 * whole number, snapping puts the grid on truth immediately, instead of
 * waiting for the convergence to walk there. It is opt-in because DJs pitch
 * their decks: a mix sitting at 122.2 is hurt by being forced to 122, which
 * is exactly the call the player has to make, not the machine. */
static volatile uint8_t  g_snap_sweep;     /* LED confirm: 0=idle, else frames left */
static volatile uint8_t  g_snap_took;      /* diag: the last nudge actually moved */
#define SNAP_GATE_BPM100 40u   /* 0.40 BPM, in hundredths — see bpm_snap */
/* Round a beat length (frames) to the nearest WHOLE BPM, if it is close
 * enough to be a rounding rather than a reinterpretation. Applied LAST at
 * every point the beat is decided (tap commit, F8 refine, convergence) — if
 * it ran anywhere else the two would fight, one rounding to 122 while the
 * other measures 122.2. */
static uint32_t bpm_snap(uint32_t bf)
{
	if (!bf) return bf;
	uint32_t bpm100 = (uint32_t)(((uint64_t)48000u * 60u * 100u + bf / 2u) / bf);
	uint32_t whole  = (bpm100 + 50u) / 100u;
	if (whole < 50u || whole > 200u) return bf;
	/* M25-r10 DISTANCE GATE. The nearest whole number is never more than
	 * 0.5 BPM away, so this window only ever rejects the outer part of
	 * that range — at 0.35 it passes 70% of it. The point is not safety,
	 * it is HONESTY: past the gate the machine says "that is not a whole
	 * tempo" with a shrug instead of silently inventing one. Convergence
	 * would walk a wrong snap back anyway, so a decline costs nothing.
	 * The number is set by TAP SCATTER, not by taste. marc's bench taps
	 * against a 128.000 click landed 0.02-0.45 from whole; at 0.35 five
	 * of fourteen would have shrugged at a genuinely whole tempo, which
	 * is the machine arguing with his finger. 0.40 passes thirteen of the
	 * fourteen and still declines a deliberately pitched 122.2, which is
	 * the only case the gate was ever for. Adjust only against real taps:
	 * the bench ones are the evidence, not a preference. */
	uint32_t w100 = whole * 100u;
	uint32_t d100 = (bpm100 > w100) ? (bpm100 - w100) : (w100 - bpm100);
	if (d100 > SNAP_GATE_BPM100) return bf;   /* too far: the caller shrugs */
	uint32_t nf = (uint32_t)(((uint64_t)48000u * 60u + whole / 2u) / whole);
	if (!nf) return bf;
	g_snap_took = (nf != bf);
	return nf;
}    /* next 24-PPQN tick, sample-clock domain */
static volatile uint8_t  g_grid_active;       /* current song has a live grid */
static volatile uint8_t  g_grid_save_req;     /* control -> streamer: write block 2 */
/* M8b quantized capture: with a grid, arming PUNCHES IN on the next bar line
 * (auto-start-on-sound is bypassed) and the stop rounds to the nearest grid
 * BEAT — reusing fixed mode's run-on/snap-back machinery with the tapped beat
 * as the base. Lengths quantize to a block-rounded beat so all grid takes are
 * multiples of the SAME base = mutually locked forever. */
static volatile uint64_t g_grid_punch_at;      /* sample-clock of the scheduled punch-in (0 = none) */
/* M20 F1: grid phase is TRUTH only when it came from taps THIS session on
 * THIS song ("fresh") — tapping means "I'm syncing to something external",
 * so the first take punches ON the tapped grid instead of re-anchoring it
 * (bench: instant starts planted downbeats 21-82 ms off the source).
 * Grid-from-first-take songs keep the classic your-take-IS-the-"1" feel. */
static volatile uint8_t  g_grid_fresh;
static volatile uint64_t g_arm_press_sclk;  /* A-r2: sample-clock of the
                                             * PRESS behind the current arm
                                             * (first-take punch schedules
                                             * from the finger, not the arm) */
static volatile uint8_t  g_gridrec;            /* current take was grid-punched */
static volatile uint32_t g_gridrec_beat_samps; /* grid beat in STORED samples at punch speed */
/* M20 F7: the BASE of a gridded song — beat count and block length of its
 * first grid take. Loop lengths live in whole flash blocks (256 samples,
 * 5.33 ms), and the old code rounded EACH BEAT to blocks before multiplying:
 * at 120 BPM a beat is 93.75 blocks, forced to 94, so a PERFECT tap still
 * recorded a loop that plays at 119.68 BPM — +1.33 ms every beat, compounding
 * every lap (marc's bench: 10.7 ms per 8-beat lap, 160 ms after a minute; the
 * metronome LEDs run off the unquantized grid clock, which is exactly why the
 * lights looked locked while the audio slid). Now the WHOLE take is rounded
 * once, and every later take is referenced to this base, so lengths stay exact
 * multiples of each other AND track the true tempo. */
static volatile uint32_t g_grid_base_beats;
static volatile uint32_t g_grid_base_blocks;

/* GP-518 (GEOM-PREP): per-track block-geometry accessors. In THIS build
 * they are CONSTANT 280 -- value-identical to SAMP_PER_BLK, zero new
 * state -- so this bin must BEHAVE EXACTLY like 514. The P16M codec
 * build redefines them to read the track's codec geometry. The (void)
 * arg keeps every call site compile-checked NOW, so the later flip
 * cannot surface ~70 latent argument errors at once. */
#define TSPB(tp)   ((uint32_t)((tp)->p16m ? 496u : 280u))   /* P16-522: LIVE per-track geometry */
#define TSPBI(ix)  ((uint32_t)(trk[ix].p16m ? 496u : 280u))
#define TLOOPB(ix) ({ uint32_t _ll = g_loop_len;   /* VF-523: ONE volatile read */ \
		      _ll ? (uint32_t)((_ll + TSPBI(ix) / 2u) / TSPBI(ix)) : 0u; })
#define P16M_DEFAULT 0u   /* GS-531: STEREO default; double-tap a track to
                           * toggle its record mode (gesture map v2). */

/* Blocks for n grid beats. Base-referenced when the song has one at the same
 * tempo (siblings then lock exactly); otherwise the whole run is rounded once
 * — error <= half a block per TAKE instead of half a block per BEAT. */
static uint32_t grid_len_blocks(uint32_t nbeats, uint32_t spb)   /* GP-518 */
{
	uint32_t bs = g_gridrec_beat_samps;
	if (nbeats < 1u) nbeats = 1u;
	if (!bs) return nbeats;
	if (g_grid_base_beats && g_grid_base_blocks) {
		uint32_t bb = (uint32_t)(((uint64_t)g_grid_base_blocks *
					  spb) / g_grid_base_beats);
		uint32_t d = (bb > bs) ? (bb - bs) : (bs - bb);
		if ((uint64_t)d * 100u <= (uint64_t)bs)   /* same tempo (<1%) */
			return (uint32_t)(((uint64_t)nbeats * g_grid_base_blocks +
					   g_grid_base_beats / 2u) / g_grid_base_beats);
	}
	return (uint32_t)(((uint64_t)nbeats * bs + spb / 2u) / spb);
}
/* M8c: performance layer. Mute/unmute WAITS for the bar line on gridded songs
 * (launch quantize); a tap run over EXISTING loops beatmatches (retunes the
 * tape + resyncs the loop start to the tapped downbeat at the next bar). */
/* GRIDCORE-733: the bar line (only the dead beatmatch resync read it), the snap
 * counter and the resync (never set since TAPFIX-663) are gone. */
/* PASS 2 forensics (printed + zeroed each diag window): blocks delivered per
 * track, dead-history snaps per track, and round aborts (rec yield / read fail). */
static volatile uint32_t g_p2blk[4];
static volatile uint32_t g_p2snap[4];
static volatile uint32_t g_p2yield, g_p2rfail;
static volatile uint32_t g_prime_ovf;   /* M63b-r2: prime room clip bit */
/* M63b-r4 PHASE TIMING: cycles spent per streamer phase, so the
 * codec cost stops being an argument and becomes a number.
 *
 * Use DWT->CYCCNT (64 MHz core), NOT k_cycle_get_32(). The tree
 * says at the eMMC CRC counters that k_cycle_get_32 runs on the
 * 32768 Hz RTC, ~30.5 us resolution -- coarser than a whole block
 * decode. The audio engine already profiles with DWT for exactly
 * this reason ('(DWT->CYCCNT - _c0) / 64u; 64 MHz -> us'), and
 * CYCCNT is enabled once at init. 1000x the resolution, and it
 * kills the 32768/1000 = 32 integer-division bias too.
 *
 * Accumulate in uint64_t: at 64 MHz a uint32_t of CYCLES saturates
 * after only 67 s of accumulated phase time, which a real run
 * exceeds. The per-call DELTA is always short, so it stays 32-bit
 * and wraps correctly. */
/* M95-B: the inner-loop probes run 384x/block at 1.5x and cost an
 * estimated 220-500 us -- six DWT->CYCCNT PPB loads plus two volatile
 * uint64 RMWs per iteration, and they pin r11 hard enough to force two
 * stack spills, partly undoing the M93 hoist. Set to 1 to profile
 * PASS A's interior again. M81/M8X/M8Y are BLOCK-level and unaffected,
 * so `a=` still measures PASS A either way. */
#define M82_PROBES 0
static volatile uint64_t g_t_rd, g_t_dc, g_t_en, g_t_wr;
/* M95-A: CORNER-GATED twins of the M73 phase timers.
 * The originals are cumulative-since-boot, which dilutes the corner
 * across all the idle time around it -- artifact #6, the same defect
 * that made `CPU aud=` disagree with the 1 ms census. These accumulate
 * ONLY while the corner condition holds, so they answer the question
 * the ~35%% design target never actually answered.
 * Counts ride alongside so cost-per-unit is derivable, which is what
 * turns "what the streamer GOT" into "what the streamer NEEDS". */
static volatile uint64_t g_t_rd_cx, g_t_dc_cx, g_t_en_cx, g_t_wr_cx;
static volatile uint32_t g_t_rd_cxn, g_t_dc_cxn, g_t_en_cxn, g_t_wr_cxn;
/* CY-475: the same four phases under the INPUT-corner gate
 * (high speed + audio in, recording NOT required) -- the W4Y gate,
 * one layer down. */
static volatile uint64_t g_t_rd_cy, g_t_dc_cy, g_t_en_cy, g_t_wr_cy;
static volatile uint32_t g_t_rd_cyn, g_t_dc_cyn, g_t_en_cyn, g_t_wr_cyn;
/* UP-476: the PCM14S unpack, nested. ps is inside pk. */
static volatile uint64_t g_t_pk, g_t_pk_cx, g_t_pk_cy;
static volatile uint32_t g_t_pk_cxn, g_t_pk_cyn;
static volatile uint64_t g_t_ps, g_t_ps_cx, g_t_ps_cy;
static volatile uint32_t g_t_ps_cxn, g_t_ps_cyn;
/* blocks unpacked at the corner -- turns cycles into cycles/block
 * and cycles/sample without any arithmetic on the reader's part. */
static volatile uint32_t g_pk_blk;
/* RB-475: read-burst size histogram at the live PASS-2 read.
 * The burst is NOT set by a constant -- it is whatever ring headroom
 * allows, so its distribution is the only honest way to know how big
 * reads actually are at the corner. Buckets: 1-2,3-4,5-8,9-16,17-32. */
static volatile uint32_t g_rb_n[5];
static volatile uint32_t g_rb_blk, g_rb_cnt;
static volatile uint32_t g_dcc;   /* M75: a7_decode_block calls */
static volatile uint32_t g_dcu, g_dcp, g_dcr; /* M76: by caller */
static volatile uint8_t g_cap_stereo = 1u;  /* M63b-2 step 1: R=L
                                              * capture. Ring is mono
                                              * (M91); the encoder's
                                              * stereo branch reads
                                              * ring[fi] for BOTH L and
                                              * R by construction, so
                                              * setting this flag stamps
                                              * takes stereo with R = L
                                              * identical. Step 2 will
                                              * add a real R source. */
/* M80: cumulative DWT time inside the two UAC2 callbacks. */
static volatile uint64_t g_t_cb, g_t_sof;
/* M81: per-phase time in looper_audio_block. Cumulative + max. */
static volatile uint64_t g_ph[4];
static volatile uint32_t g_phmax[4];
/* M8X: the same four phases, but advancing ONLY at the corner.
 * g_ph[] is cumulative from boot and a 58 s corner vanishes into
 * the average -- the same dilution that made W4C useless until
 * W4X gated it. Gate the instrument on the condition. */
static volatile uint64_t g_cph[4];
static volatile uint32_t g_cph_n, g_cph_blk;
/* M8Y: identical to M8X except the gate -- high speed but NOT
 * recording. Differencing the two isolates the record path's cost
 * at speed from the speed-scaled walk itself. */
static volatile uint64_t g_dph[4];
static volatile uint32_t g_dph_blk;
#if M82_PROBES
static volatile uint64_t g_pa82[2];  /* M82: PASS A sub-spans */
static volatile uint32_t g_t1min = 0xFFFFFFFFu; /* M86 min cyc/call */
#endif
static volatile uint32_t g_enmin = 0xFFFFFFFFu; /* M88 min cyc/emit */
static volatile uint32_t g_dbg_rw, g_dbg_rr, g_dbg_rc; /* M86-r2 raw */
static volatile uint32_t g_w4_pk, g_w4_pb, g_w4_sq, g_w4_tq; /* W4P */
#define X3_CODEC_P14S 5u
#define X3_CODEC_P16M 6u   /* P16-522: PCM16-mono take -- 496 engine fr / 248 stored samp per block */
#define P14S_MARK     0x5Bu
static uint8_t  g_p14s_mask;              /* bit i: track i is P14S. SHWDEL-649: WRITE-ONLY now -- the
                                           * mixer's store at take start stays so its text is unchanged (W291) */
static int32_t  g_p14s_e1[2];             /* encode shaper, carried within a take */
static int16_t  g_p14s_prev[NTRK][2];     /* decode continuity per track */
static uint32_t g_p14s_sh;                /* BG-470: encoder's current block-gain shift */
/* W4C: 1 ms statistical census — the timer ISR samples the
 * INTERRUPTED thread. Cumulative per-tid counts (Protocol A:
 * deltas belong to the analysis, not the firmware). */
static struct k_timer g_w4c_tmr;
static struct { void *tid; uint32_t n; } g_w4c_tab[8];
static volatile uint32_t g_w4c_miss;
/* CX: the corner census. g_w4c_tab is cumulative from boot and is
 * diluted by every idle second; these tick ONLY while the transport
 * is at high speed AND a take is recording -- the corner's defining
 * condition. Indices match g_w4c_tab, so no second tid table. */
#define CX_SPEED_MIN 90000u   /* ~1.373x (1.5x == 98304) */
/* g_rec_track is DEFINED further down the file than the census
 * tick that reads it -- the first CX build failed to compile on
 * exactly this, the same ordering class as the batchbuf bug in
 * 432. A tentative definition here is legal C at file scope and
 * binds to the real one below. */
static volatile int g_rec_track;
static uint32_t g_cur_speed_q16;
static volatile uint32_t g_cx_n[8];
static volatile uint32_t g_cx_tot, g_cx_hs;
/* CXW-473: the INPUT corner. Same 8 indices, a WIDER gate: high
 * speed AND the host is streaming audio in -- recording NOT
 * required. This is the condition marc actually reproduces. */
static volatile bool g_usb_streaming;
static volatile uint32_t g_cy_n[8];
static volatile uint32_t g_cy_tot;
/* RC-484: the RECORD corner at ANY speed -- marc's real workflow.
 * Same 8 indices; the only gate is that a take is recording. */
static volatile uint32_t g_cz_n[8];
static volatile uint32_t g_cz_tot;
/* EP-484: the PCM14S packer, gated on the record corner. */
static volatile uint64_t g_t_ep;
static volatile uint32_t g_t_epn, g_ep_blk;
static volatile uint32_t g_gap_max;   /* W4N: worst rec-ring gap this boot */
static uint8_t g_w4n_once;
static uint8_t g_w4c_on;
static void w4c_tick(struct k_timer *t)
{
	ARG_UNUSED(t);
	void *cur = (void *)k_current_get();
	int _cxhs = (g_cur_speed_q16 >= CX_SPEED_MIN);
	int _cx = _cxhs && (g_rec_track >= 0);
	int _cy = _cxhs && g_usb_streaming;   /* CXW-473: input corner */
	int _cz = (g_rec_track >= 0);         /* RC-484: record, ANY speed */
	if (_cxhs) g_cx_hs++;
	for (int i = 0; i < 8; i++) {
		if (g_w4c_tab[i].tid == cur) { g_w4c_tab[i].n++;
			if (_cx) { g_cx_n[i]++; g_cx_tot++; }
			if (_cy) { g_cy_n[i]++; g_cy_tot++; }
			if (_cz) { g_cz_n[i]++; g_cz_tot++; } return; }
		if (!g_w4c_tab[i].tid) { g_w4c_tab[i].tid = cur; g_w4c_tab[i].n = 1;
			if (_cx) { g_cx_n[i] = 1; g_cx_tot++; }
			if (_cy) { g_cy_n[i] = 1; g_cy_tot++; }
			if (_cz) { g_cz_n[i] = 1; g_cz_tot++; } return; }
	}
	g_w4c_miss++;
}
#define M81_LAP(K) do { uint32_t _n81 = DWT->CYCCNT, _d81 = _n81 - _lt81; \
	g_ph[K] += _d81; uint32_t _u81 = _d81 / 64u; \
	if (_u81 > g_phmax[K]) g_phmax[K] = _u81; \
	if (_cx81) g_cph[K] += _d81; \
	if (_dx81) g_dph[K] += _d81; \
	_lt81 = _n81; } while (0)
#define M73_T0() uint32_t _t73 = DWT->CYCCNT
/* M95-A: same accumulate, plus a corner-gated twin. The gate reads
 * g_play_speed_q16 (volatile, rocker-set) rather than g_cur_speed_q16
 * -- the latter is documented audio-thread-only and M73_ADD runs on
 * the STREAMER thread. */
#define M73_CX_NOW() (g_play_speed_q16 >= CX_SPEED_MIN && g_rec_track >= 0)
/* CY-475: the input-corner gate. Same shape as M73_CX_NOW, but keyed
 * on the host streaming audio IN rather than on recording. */
#define M73_CY_NOW() (g_play_speed_q16 >= CX_SPEED_MIN && g_usb_streaming)
#define M73_ADD(acc) do { uint32_t _dcx73 = (uint32_t)(DWT->CYCCNT - _t73); \
        (acc) += _dcx73; \
        if (M73_CX_NOW()) { (acc##_cx) += _dcx73; (acc##_cxn)++; } \
        if (M73_CY_NOW()) { (acc##_cy) += _dcx73; (acc##_cyn)++; } } while (0)
static volatile int      g_meta_loaded;       /* streamer -> main: g_meta read at boot */

/* Persist the 2-block song index MAGIC-LAST: block 1 (songs 9-16 + tail)
 * first, then block 0 (magic + songs 1-8). A power cut between the two
 * writes leaves the old block 0 — the old index stays fully authoritative —
 * so a torn half-new index is impossible by ordering. (Both writes usually
 * land in the card's write cache and flush together anyway; this closes the
 * rare flush-between window. See SP1-SIDE-EFFECTS-AUDIT.md §1.1.) */
static bool meta_write_blocks(const uint8_t *buf)
{
	bool ok1 = emmc_write_blocks(META_BLOCK + 1u, buf + EMMC_BLOCK_SIZE, 1);
	bool ok0 = emmc_write_blocks(META_BLOCK, buf, 1);
	return ok0 && ok1;
}

enum trk_state { TS_EMPTY, TS_ARMED, TS_REC, TS_DONE, TS_PLAY };

struct looptrk {
	volatile uint8_t  state;
	uint8_t  p16m_next;                  /* GS2-532: record mode for the NEXT take --
	                                      * the double-tap toggles THIS, never p16m,
	                                      * so a loaded take's live geometry can
	                                      * never flip under the streamer (the
	                                      * half-plugged-guitar noise, W147). */
	uint8_t  gsh;                        /* BNC2-604: PRINT GAIN shift. A bounce is stored
	                                      * bus >> gsh and played back vol << gsh (folded
	                                      * into PASS B's per-block hoist). 0 = a normal take. */
	uint8_t  p16m;                       /* P16-522, VF-523: NOT volatile -- see W141.
	                                      * Written only at take-create/load; a stale
	                                      * read for one pass is harmless, and volatile
	                                      * reads at the ~70 geometry sites reproduce
	                                      * the W139 uniform-stall corner regression. */
	volatile uint16_t vol_q8;            /* fader volume, 256 = unity */
	int16_t  pring[RING_SAMPLES * 2u] __attribute__((aligned(4)));
	                                     /* M63a play ring: STEREO frames (L,R int16
	                                      * pairs); RING_SAMPLES counts FRAMES.
	                                      * streamer writes, audio reads */
	volatile uint32_t p_w;               /*   streamer fill frontier (loop samples) */
	volatile uint32_t r_w;               /*   rec ring: audio produce (into g_rring) */
	volatile uint32_t r_r;               /*   rec ring: streamer consume */
	volatile uint32_t rec_count;         /* samples recorded so far (audio) */
	volatile uint32_t rec_target;        /* stop after this many samples (0 = open, first loop) */
	volatile uint8_t  rec_silence;       /* live phrase ended; pad silence to rec_target */
	volatile uint8_t  muted;             /* tap-to-mute: track silenced but kept */
	volatile uint8_t  starved;           /* ring underran; silent until half-refilled */
	uint16_t          fade;              /* starve-recovery fade-in position (256 = full; mixer-only) */
	uint8_t           held;              /* HOLDFADE-787: the last frame is being held (dry, gain up); a refill finishes the duck on it first (mixer-only) */
	uint32_t          hold_pos;          /* HOLDFADE-787: the held frame's position (mixer-only) */
	uint16_t          vol_now;           /* gain actually applied last block (mixer-only; ramps toward fader/mute target) */
	uint8_t           rec_fade;          /* stop-pad fade-down remaining, of 128 (recorder-only) */
	uint8_t           rec_fstep;         /* fade decrement per sample (fits the fade inside the pad) */
	uint8_t           pan_al, pan_ar;    /* PLACE-715: side attenuation 0..255 applied last block (0 = unity, 255 = -48 dB; slewed <= 4 a block) -- in the pad: no offset moves */
	uint32_t flush_blk;                  /* streamer: next loop block to write */
	uint32_t flush_mod;                  /* wrap the flush at this many blocks (overdub = loop len) */
	/* SEGMENT looper: a track's length is a whole multiple of the base loop. The
	 * first take sets the base; an overdub records ONE base-length segment as a
	 * bounded take, and if the button is still held when the segment boundary is
	 * reached it appends another base-length segment (and another), each one a
	 * bounded take through the same proven flush path -- never the old open-ended
	 * "record until release, then figure out the length". len_blocks is the
	 * track's total length; start_blk is the transport block where its segment 0
	 * began (the phase anchor used to line playback up with where it was cut). */
	uint32_t len_blocks;                 /* this track's total LOOP length in eMMC blocks (N * base) */
	uint32_t content_blocks;             /* blocks actually recorded; [content_blocks, len_blocks) plays
	                                      * as SILENCE synthesised on read (never written to flash), so a
	                                      * fixed-mode take finalises INSTANTLY instead of real-time-
	                                      * padding a bar of zeros. 0 == whole track (old/variable/uploaded). */
	uint32_t start_blk;                  /* transport block of this take's segment 0 (playback anchor) */
	uint32_t len_samps;                  /* M22-B: loop length in SAMPLES (the wrap the
	                                      * streamer honours on a plain, unchopped loop).
	                                      * 0 or a block multiple = classic behaviour. */
	uint32_t start_samps;                /* M22-B: playback anchor in SAMPLES */
	/* AUTO-START-ON-SOUND: a take ARMS on the button hold and the recorder only
	 * begins capturing at the first input past SOUND_THRESHOLD (armed waits
	 * as a fallback), so dead air before the first note never lands in the loop. */
	volatile int32_t  wait_peak;
	volatile uint32_t wait_ticks;
};
static struct looptrk trk[NTRK];

/* ONE SHARED record ring. Only one take is ever in flight (the press handler
 * refuses to arm while any track is ARMED/REC/DONE), so the four per-track rec
 * rings were waste: one ring TWICE the size costs 32 KB less RAM and absorbs
 * twice the eMMC-write transient (~2.4 s at the loop rate) before overflowing.
 * Overflow = a permanently corrupted take, so headroom here is what matters. */
#define RRING_SAMPLES    8192u   /* CD-463: STORED 24 kHz stereo frames.
 * Byte-identical ring (8192 x 2ch x 2B = 32,768 B) but each stored frame
 * covers TWO engine frames -> the ring spans 16,384 engine frames of time:
 * 341 ms @1.0x, ~248 ms at max (1.373x) -- the 104 ms worst write-cache
 * stall fits with margin (scenario-2 fix, watchlist W1). Counters (r_w,
 * r_r, rec_count) stay ENGINE-based; only the store/read is half-rate.
 * rhw= on the STV line is the meter; overrun line is now 2x RRING. */  /* M91: MONO frames -- capture is a mono downmix; the stereo-frame ring stored every sample twice. 341 ms backlog, the PCM-era headroom back */   /* M63a: STEREO FRAMES (~170 ms backlog; was 16384 mono) */   /* ~341 ms record backlog (reverted 32768->16384): the compressed codecs cut flush traffic, so the doubled rec ring is no longer needed; this reclaims RAM for the play-ring revert */
#define RRING_MASK       (RRING_SAMPLES - 1u)
static int16_t g_rring[RRING_SAMPLES * 2u]  /* CD-463: STORED 24k stereo frames (each = 2 engine frames; ring spans RRING_SAMPLES*2 engine frames of time) */ __attribute__((aligned(4)));
/* CD-463: capture-side boxcar hold. The pair grid is ABSOLUTE (bit0 of the
 * engine counter): even frame -> hold, odd frame -> store (hold+cur+1)>>1 at
 * physical index (counter>>1). Audio-thread only; shared by the TS_REC writer
 * and both pre-roll writers because they are one sequential stream. */
static int16_t g_cd_holdL, g_cd_holdR;
/* DMP-466: one-shot take dump state (diagnostic build only) */
static volatile uint8_t g_dmp_arm, g_dmp_state;
static uint32_t g_dmp_blk, g_dmp_n;
/* M63a: stereo frames; g_pre_w / r_w / r_r count FRAMES as before */
/* M20 PRE-ROLL: the record ring sits IDLE whenever nothing is being captured,
 * so the input is written into it continuously — the machine always remembers
 * the last stretch of what it heard. A punch that arrives LATE can then start
 * exactly on the grid line it missed, filled in from that memory: you cannot
 * record the past, but you can remember it. Costs zero RAM (the buffer already
 * existed) and one store per sample while idle. Capped at half the ring so a
 * backfilled take never starts the flush against a full buffer. */
#define PREROLL_MAX      (RRING_SAMPLES * 3u / 2u)  /* CD-463: engine frames; 3/4 of the
                                                     * 2x-engine ring = ~256 ms reach again */
/* M20b-r2: how far back a punch may REACH. Human lateness is measured in
 * milliseconds, not in beats, so the reach window is a quarter of a beat
 * CAPPED here — see the rescue site for why half a beat was too generous. */
#define PREROLL_REACH_MS 180u
static volatile uint32_t g_pre_w;         /* pre-roll write frontier (ring index) */
static volatile uint32_t g_pre_valid;     /* consecutive valid pre-rolled samples */
static volatile uint32_t g_pre_phase;     /* decimator phase while the transport is idle */
static volatile uint32_t g_pre_speed;     /* tape speed the idle ring was filled at */
static volatile uint8_t  g_done_pending;  /* a take is still flushing: ring is BUSY */
/* M41 HEAD RECOVERY (row 79), ON BY DEFAULT. The start rules are
 * unchanged (ungridded = first sound, gridded = on the line); when set,
 * an ungridded trigger scans the pre-roll ring back to the sound's
 * ONSET (capped at the press) so the 100/180 ms arm window never eats
 * the head. Clear = the exact shipped slight-hold feel. Opt-out lives
 * in BIT 1 of the index settings word (led_full): SET = classic. Set
 * on the transfer site; adopted in xfer_commit and at the boot index
 * load, like brightness (M8c). Old indexes read bit 1 = 0 -> instant,
 * so the default reaches every existing device. */
static volatile uint8_t  g_instant_rec = 1;
static volatile uint32_t g_rec_overruns;         /* diag: rec ring overflow events */
static volatile uint32_t g_starve_cnt[NTRK];     /* diag: per-track play-ring underrun episodes */
/* M98: g_starve_cnt is cumulative-since-boot, and Protocol A connects
 * the capture AFTER the run -- so every stv we have ever read is a
 * single end-of-session snapshot. Slicing the captures by tick shows
 * flat lines: identical on all 269 ticks of one run. Four snapshots
 * from four differently-shaped sessions cannot answer WHERE the
 * dropouts happen. Same defect as artifact #6, same fix as W4X:
 * gate by phase. Both bump sites are in PASS B on the audio thread,
 * so the gate reads variables already in hand and only runs when a
 * ring has ALREADY underrun -- it cannot perturb what it measures. */
static volatile uint32_t g_stv_lo;   /* below high speed              */
static volatile uint32_t g_stv_up;   /* high speed, NOT recording     */
static volatile uint32_t g_stv_cx;   /* high speed AND recording      */
static volatile uint32_t g_stv_pf;   /* a take still flushing         */
static volatile uint32_t g_stv_re;   /* RA-491: starve DURING fade-in */
static volatile uint32_t g_stv_hr;   /* HOLDFADE-787: hold-resumes (a refill landed before the duck reached 0) */
#define STV_BUMP() do { \
        if (trk[i].fade < 256u) g_stv_re++;   /* RA-491 thrash */ \
        if (g_cur_speed_q16 < CX_SPEED_MIN) g_stv_lo++; \
        else if (g_rec_track >= 0)          g_stv_cx++; \
        else if (g_done_pending)            g_stv_pf++; \
        else                                g_stv_up++; \
} while (0)
/* ---- M46d: duty-cycled streamer priority boost (the TL-3 dropout fix).
 * USB interrupt load dilates the CPU-paced eMMC reads ~2x (measured:
 * cmd 455->857 us, data 474->847 us per 512 B block). When a playing
 * ring runs low the streamer briefly outranks everything but audio so
 * reads finish on time; a governor keeps main feeding the bootloader's
 * 5 s watchdog. Constants measured on hardware 2026-08-16. ---- */
volatile uint8_t g_emmc_sprint;      /* audio -> streamer: ring is low */
struct k_thread *g_str_tid;          /* the streamer, for the wrapper */
int g_pb_orig = 12345;               /* streamer's normal priority */
volatile uint8_t g_pb_on;            /* boost currently applied */
volatile uint32_t g_pb_t0;           /* boost burst start (cycles) */
static volatile uint32_t g_stored_glitch_cnt;    /* diag: wfail advance-anyway commits — a STORED glitch
                                                  * replays at the same loop spot every pass (vs a live
                                                  * underrun, which is one-shot). Separating the two is
                                                  * what previous crackle hunts were missing. */
static volatile uint32_t g_i2s_wfail_cnt;        /* diag: I2S write failures (audio-path exoneration) */
static volatile uint32_t g_audio_us_max;         /* diag: worst looper_audio_block exec time, us (DWT, session) */
static volatile int32_t  g_play_lowat = 0x7FFFFFFF; /* diag: window MIN play-ring margin, samples */
static volatile uint32_t g_rec_hiwat;            /* diag: window MAX rec-ring fill, samples */
static volatile uint8_t  g_extcsd_dump[9];       /* diag: EXT_CSD[167,166,231,502,503,198,246,192,175] */
static volatile uint8_t  g_hpi_on;               /* 1 = HPI enabled (abort lever for maintenance ops; also proves
                                                  * the card's HPI works, for a possible future write-path V4) */
static volatile uint8_t  g_emmc_quiesce;         /* 1 = shutdown flush done: park the eMMC bus */
/* eMMC internal write cache: enabled at boot if the card has one. It absorbs the
 * record write-bursts so an overdub doesn't overflow the rec ring. The cache is
 * volatile, so it is flushed to NAND once at power-off (via g_cache_flush_req) to
 * keep the loops -- never during play, which would stall the bus. */
static volatile uint8_t  g_cache_on;           /* 1 = card write cache enabled */
static volatile uint32_t g_cache_kb;           /* diag: EXT_CSD CACHE_SIZE (KB) the card reports */
static volatile int      g_cache_flush_req;    /* power-off: streamer, flush the cache now */

/* ---- USB block-transfer mode (the file-transfer website talks to this) -----
 * A tiny binary protocol over the CDC serial console lets a WebSerial page
 * read/write raw eMMC blocks, so loops can be up/downloaded as WAV. The host
 * sends an 8-byte magic to ENTER; the streamer (the only eMMC user) then pauses
 * audio and services one command at a time. Auto-exits on 'X' or a 15 s idle. */
#define SP1_XFER_ENABLE 1                      /* 1 = USB loop-transfer (website upload/download) enabled */
#if SP1_XFER_ENABLE
static volatile uint8_t  g_xfer_mode;          /* 1 = in block-transfer mode (audio paused) */
RING_BUF_DECLARE(g_cdc_rx, 1024);              /* CDC serial RX bytes, filled by the ISR */
#else
#define g_xfer_mode 0u                         /* transfer out: constant 0 so every g_xfer_mode branch drops */
#endif

static volatile uint32_t g_consume_pos;          /* shared playhead (loop samples, free-running) */
static volatile uint8_t  g_loop_active;          /* a loop exists / master clock running */
static volatile uint32_t g_loop_len;             /* master loop length, loop-samples (0 = unset) */
static volatile uint32_t g_loop_blocks;          /* g_loop_len / SAMP_PER_BLK (streamer wrap) */
static volatile int      g_rec_track = -1;       /* the one track currently recording, or -1 */
/* Master volume Q8. Default = the proven-clean speaker level (the audio firmware's
 * SPK_VOL_Q8 = 48 ~= 0.19 full-scale): the little TAS2505-driven speaker distorts
 * well below full scale, and the looper sums up to 4 tracks + the live monitor, so
 * this also keeps the mix from hard-clipping. Adjustable up to 256 via the buttons. */
/* Master volume Q8 (256 = unity). The VOL +/- buttons step a perceptual curve
 * (~3 dB/step) so each press is an equal-loudness change, smooth from full down
 * to silence. g_vol_idx = current position. (Per-track faders set vol_q8 directly.) */
static const uint16_t g_vol_table[] = {
	0, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 90, 128, 181, 256,
};
#define VOL_STEPS ((int)(sizeof(g_vol_table) / sizeof(g_vol_table[0])) - 1)  /* 15 */
static volatile int      g_vol_idx = 10;          /* -> 45 */
static volatile uint16_t g_master_vol_q8 = 45;
static volatile int      g_arm_req[NTRK];         /* main -> engine: track i pressed (start rec) */
static volatile int      g_stop_req;               /* main -> engine: track released (stop rec) */
static volatile int      g_del_req[NTRK];          /* main -> engine: double-tap = delete track i */
static volatile int      g_restart_req;            /* main -> engine: hold PLAY = jump to song start */
/* GLOBAL LOOP CHOP (performance window, scheme A'): play only 1/div of every
 * track's loop — the off'th slice. Non-destructive playback-window remap in
 * the streamer's fill math only: recorded audio, loop lengths, beat grid and
 * MIDI clock are untouched; div=1/off=0 is bit-identical to the original
 * math. Persisted per song since M7a (index chop[] bytes). */
static volatile uint32_t g_chop_div = 1;           /* 1,2,4,... CHOP_DIV_MAX (1 = full loop) */
static volatile uint32_t g_chop_off = 0;           /* window index: 0..div-1 */
/* CHOPCAP-690 (row 132): the window may now shrink all the way to ONE
 * block. Card bytes chop[slot][0..1] stay uint8: a value < 0x80 in [0]
 * is the legacy literal div (1..64) with [1] the offset, unchanged for
 * every card written so far and for older firmware reading a song that
 * never went past 64. Past 64 the byte is 0x80 | log2(div) (bits 0-3)
 * with offset bits 8-10 in bits 4-6 and the low 8 in [1]. Older
 * firmware sees > 64 and falls back to div 1, nothing worse. */
#define CHOP_DIV_MAX 1024u
static void chop_meta_decode(const uint8_t *c, uint32_t *d, uint32_t *o)
{
	uint32_t cd, co;
	if (c[0] & 0x80u) {
		cd = 1u << (c[0] & 0x0Fu);
		co = (((uint32_t)(c[0] >> 4) & 7u) << 8) | c[1];
	} else {
		cd = c[0]; co = c[1];
	}
	if (cd < 1u || cd > CHOP_DIV_MAX) cd = 1u;
	if (co >= cd) co = 0u;
	*d = cd; *o = co;
}
static void chop_meta_encode(uint8_t *c, uint32_t d, uint32_t o)
{
	if (d <= 64u && o <= 255u) { c[0] = (uint8_t)d; c[1] = (uint8_t)o; return; }
	uint32_t l = 0u;
	while ((1u << l) < d) l++;
	c[0] = (uint8_t)(0x80u | (l & 0x0Fu) | (((o >> 8) & 7u) << 4));
	c[1] = (uint8_t)o;
}
/* Largest useful div: the window can't be finer than one block, so stop
 * doubling once the grid (or, gridless, the longest track) is exhausted.
 * Never below the old 64 so nothing that worked before is refused. */
/* GRIDLAP-806 (W351): the stored loop was written in the GRID's sample domain and is generally not a whole
 * number of blocks, but the streamer serves whole blocks -- so re-derive it from the lap that WILL be served.
 * The shortest present take is the base (every take is a whole multiple of it), measured in THAT take's own
 * blocks: the old line rounded with a hard-coded 280, which is 1.77x wrong for a mono song. Nothing is written
 * back to the card -- an existing song simply stops drifting against its own metronome and MIDI clock.
 * Its own function because the caller is the slot-switch service, which lives inside the mixer (W291). */
static void __attribute__((noinline)) gridlap_load(void)
{
	uint32_t _lb = 0u, _sp = 0u;
	for (int _k = 0; _k < NTRK; _k++)   /* the BASE take's format: the shortest present one */
		if (trk[_k].state == TS_PLAY && trk[_k].len_blocks &&
		    (!_lb || trk[_k].len_blocks < _lb)) { _lb = trk[_k].len_blocks; _sp = TSPBI(_k); }
	if (!_sp) _sp = SAMP_PER_BLK;
	if (g_loop_len) {   /* snap the STORED loop -- never the take's own length: if the base track was deleted
	                     * the shortest survivor is a 2x or 3x multiple, and taking ITS length would double the song */
		g_loop_blocks = (g_loop_len + _sp / 2u) / _sp;
		g_loop_len    = g_loop_blocks * _sp;
	}
}
static uint32_t chop_div_cap(void)
{
	uint32_t ref = 0u;   /* CAPREF-806: the real takes first -- g_loop_blocks rounds with a literal 280 whatever
	                      * the takes are, so on a mono song the cap was measured against a 1.77x wrong length */
	for (int i = 0; i < NTRK; i++)
		if (trk[i].len_blocks > ref) ref = trk[i].len_blocks;
	if (!ref) ref = g_loop_blocks;
	uint32_t cap = 64u;
	while (cap < CHOP_DIV_MAX && (cap << 1) <= ref) cap <<= 1;
	return cap;
}
static volatile int      g_chop_req;               /* main -> engine: window changed, snap rings */
static volatile uint8_t  g_chop_defer;             /* M24: a CONTINUOUS window gesture is in
                                                    * progress — accumulate the edits and pay
                                                    * for them ONCE when the finger lifts. See
                                                    * the FUNCTION-release hook. */
static volatile int64_t  g_defer_t;                /* r3: last deferred edit (ms) — gates the release settle */
/* M13 HEADS MODE (prototype, session-only): FN+PLAY TRIPLE-tap toggles it.
 * Tracks 2-4 stop playing their own loops and become three extra TAPE HEADS
 * on track 1's loop, offset by quarters of its audible cycle (Count-to-Five
 * style): same audio, four phases. Faders and mutes act per head, so one
 * loop becomes a canon/texture instrument. Recording and delete are blocked
 * while active (toggle off to record); the heads' own hidden content is
 * untouched and returns when the mode ends. */
static volatile uint8_t  g_heads_mode;
/* M19a: the heads SOURCE is any track (bharris22/JustyB) — g_head_src.
 * Entry picks the lowest playing track (so heads work when track 1 is
 * empty); holding a LOADED track in heads mode makes IT the tape. */
static volatile uint8_t  g_head_src;
static uint8_t           g_head_mute_save;   /* song mutes across heads mode */
/* R1-597: the M19b offline block-copy bounce is DELETED. A bounce is now a
 * take fed from the bus (BNC-597), on every layer, heads included. */
static volatile uint8_t  g_led_shrug;      /* track row "no" double-blink */
static volatile uint8_t  g_fn_held;        /* LED-549: FUNCTION is down (LED routing) */
static volatile uint8_t  g_vu;             /* LED-549: master VU, 0..255, decayed */
static volatile uint8_t  g_pg_sweep;       /* LED-549 r8: page sweep/land counter */
static volatile uint8_t  g_pg_swmode;      /* LEDS-725: 0 = the entry walk + land, 1 = the COUNT sweep (FN + VOL walk) */
#define PG_LAST 8u   /* PG8-560: EIGHT pages, every one with a handler.
                       * (was 12, of which ten fell through to MODE -- W212.)
                       * LED-549 r12: all reachable,
                       * content or not, so the counting scheme can be
                       * exercised across all three group shapes. */
static volatile uint8_t  g_pg_exit;        /* LED-549 r11: exit sweep counter */
static volatile uint16_t g_pg_cnt;         /* LED-549 r11: page-number count phase */
static volatile uint8_t  g_pg_open;        /* PG-533: a page is open (PF-545: sticky) */
static volatile uint8_t  g_pg_id;          /* FXP-547: WHICH page -- 4 = MODE (T4), 2 = FX (T2) */
static uint8_t           g_fx_pick[4];     /* FXP-547: fader owes a pickup cross on this page */
static int               g_fx_lastq[4];    /* FXP-547: last raw read while un-picked */
/* STACKT-716: DMP-466's private 512 B read buffer is gone -- the dump borrows the streamer's
 * metabuf (same thread, same pass; it runs only with nothing recording or draining). */
#define head_active(i) (g_heads_mode && (i) != g_head_src && \
			trk[g_head_src].state == TS_PLAY)
/* HG-646 (W185): the geometry of the BYTES a ring plays -- its own track's,
 * or the SOURCE's under a head. Every position<->block conversion for a ring
 * goes through this, never TSPBI(i): a head whose codec differs from the
 * source's otherwise strides its ring wrong ("half-plugged cable"). */
#define TSPB_SRC(ix) TSPBI(head_active(ix) ? (int)g_head_src : (ix))
/* M14 HEADS v2: each head's position on the loop is a live Q8 phase (0-255
 * of the audible cycle). Quarters at entry = the v1 sound; FUNCTION+fader
 * scrubs them (all four — track 1's own phase slides too, locked decision).
 * g_head_blip asks the mixer for a ~16 ms per-track dip masking a head's
 * ring re-anchor — the master is never ducked for a one-head edit. */
static volatile uint8_t  g_head_pos[NTRK];
/* M15: per-head DIRECTION (heads mode double-tap — delete is blocked there,
 * so the gesture was free). Gated on heads_engaged(): normal playback can
 * never see it; reset forward at every heads entry. Session-only. */
static volatile uint8_t  g_head_rev[NTRK];
static volatile uint8_t  g_head_blip[NTRK];
/* M16 FREE WINDOW (session-only): while FUNCTION is held OUTSIDE heads mode,
 * fader1 = window START, fader2 = END (free Q8 fractions of the chop period
 * — any width, any place, not just the stepped div/off), fader3 = SHIFT
 * (width and order kept), fader4 = nothing (the fx slot stays pended). If
 * START crosses past END the window plays in REVERSE (nervouskidz), riding
 * the M15 reverse engine. Any chop BUTTON press reclaims the stepped world
 * (clears the flag); song switch and power-off clear it too. */
static volatile uint8_t  g_win_free;
static volatile uint8_t  g_win_s8;
static volatile uint8_t  g_win_e8 = 255;
static volatile uint8_t  g_win_rev;
/* M17 DJ FILTER (session-only, M11 decisions finally cashed in): while
 * FUNCTION is held outside heads mode, FADER 4 is a center-neutral DJ
 * filter on the master sum — center = clean bypass (wide 12% notch),
 * below = LP sweep down to ~80 Hz, above = HP sweep up to ~4.5 kHz.
 * One 2-pole state-variable filter in PASS C (after the mix, before the
 * limiter — colors everything, the DJ-correct spot), Q14 coefficients
 * from the tables below, per-block smoothed so sweeps never zipper.
 * Latches where the fader leaves it; resets to neutral at power-on. */
static volatile uint8_t  g_flt_pos = 128;
/* DST-548: stock-parity DISTORTION on the FX page (fader 3).
 * 0 = clean (the branch is skipped entirely), 255 = full drive.
 * Cubic soft clip y = x - x^3/3 in Q15, pre-gained: gentle at low
 * settings, hard-clipped grit at the top, no lookup table. */
static volatile uint8_t  g_dst_amt;
/* FX2-550 tuning. EVERY ONE of these was wrong on the first pass and was
 * caught by simulating the kernels before building -- worth stating,
 * because all four are the kind of error that sounds like a broken
 * effect rather than a mistuned one.
 * CHR_LFO_INC: one LFO cycle is 2^32/INC frames, so 60850 gives
 * ~0.68 Hz at 48 kHz. The first value was 48x too fast (33 Hz) --
 * that is ring modulation, not chorus.
 * CHR_D_SPAN: the delay sweeps CHR_D_MIN..+SPAN frames = 4..18 ms,
 * the classic chorus window. The first value swept to 60 ms, which
 * is a slapback echo. */
#define CHR_LFO_INC   60850u       /* ~0.68 Hz sweep */
#define FX2_LFO_INC 107374u   /* FX2-558: ~1.2 Hz at 48 kHz (2^32 / 48000 = 89478 per Hz). Slow enough to feel like motion rather than modulation. */
#define CHR_D_MIN     192          /* 4 ms, in frames */
#define CHR_D_SPAN    672          /* +14 ms, in frames */
/* FX2-550 r3. The threshold is now RELATIVE TO THE SIGNAL'S OWN RUNNING
 * PEAK, not to an absolute level, because I got the absolute scale wrong
 * twice and the second wrong answer was indistinguishable from the first
 * to anyone using it: the fader just made everything silent.
 * An absolute threshold has to be stated in SOME unit, and there is no
 * stable one here. The kernel sees the pre-master mix, whose level moves
 * with the master volume, with all four track faders, and -- once the
 * distortion is engaged -- with its output trim, which caps the signal
 * near 3,700 counts however loud the take was. Any fixed number is
 * therefore right for one setting of the instrument and wrong for the
 * rest, and "wrong" here means the gate is either deaf or shut.
 * A peak follower with a ~1.4 s release tracks how loud this material
 * actually runs, and the fader sets a FRACTION of that, 0 to 0.78. Now
 * the control means the same thing at every volume, with or without
 * distortion, on a quiet take and a loud one: it cannot be mis-scaled,
 * because it no longer carries a scale. */
/* TG-551 PATTERNS: 16 steps, bit 0 = step 0. The RATE sets each step's
 * duration, so one pattern set spans a bar-long swell (1/4) through a
 * 32nd stutter. */
/* r2: STRAIGHT (0xFFFF) is GONE and its removal is a bug fix, not a
 * taste change -- every step on means the gate NEVER CLOSES, so it
 * was a no-op, and it was pattern 0, the DEFAULT. The first thing
 * fader 4 did on a fresh boot was nothing at all. OFFBEAT is gone
 * at marc's request. Every surviving pattern has holes in it, so
 * the control is audible the moment it leaves the bottom. */
/* r22 (marc): only the 2nd and 3rd survive. TRESILLO and THREES
 * were both SPARSE (6 of 16 steps), and at this band -- where a
 * step is 30-80 ms -- sparse reads as dropout rather than as
 * rhythm. The two that kept their character are the DENSE ones. */
static const uint16_t tg_pat[2] = {
	0xDDDDu,   /* 0 GALLOP -- x.xx per four, 12 of 16 steps */
	0x3333u,   /* 1 HALVES -- two on two off,  8 of 16 steps */
};
/* Steps per BEAT per rate. r2 (marc: "they feel way too slow except
 * for the max and max even kind of feels a little too slow"): the
 * ladder is now a NARROW, FAST band, tuned by ear over three
 * rounds. It went 1/4..1/32 (all too slow), then 1/16..1/64T
 * (top too fast), and now sits between those: 1/16T at the
 * bottom to 1/32T at the top -- the top being a little faster
 * than the 1/32 that felt closest, and the whole slow half
 * removed. At a bar-long step you were not hearing a gate, you
 * were hearing the loop stop; past ~25 ms it stops being rhythm
 * and becomes timbre. This band is the part that is neither.
 * The middle rungs (7, 9, 10 per beat) are not classical
 * divisions, but they are still exact multiples of the beat and
 * re-anchored to the grid every block, so they stay locked --
 * they simply repeat over 2 or 3 bars instead of one.
 * r23: the TOP goes up, the bottom stays. 83 ms at the bottom,
 * 21 ms at the top (was 31). The rungs spread rather than
 * shift, so the familiar middle is where it was.
 * Note 21 ms was REJECTED as too fast two passes ago -- with
 * SPARSE patterns. The dense ones are what make the fast end
 * work: a sparse pattern at 21 ms is mostly silence, a dense
 * one is flutter. Same number, different effect, because the
 * rate and the pattern are not independent controls.
 * 1/16T  1/32  ~1/40  1/32T  1/64  1/64T */
static const uint8_t tg_spb[7] = { 0u, 6u, 8u, 10u, 12u, 16u, 24u };
#define TG_ATK  256   /* 16 frames = 0.33 ms open (stock ramps one block) */
#define TG_REL   64   /* 64 frames = 1.3 ms close. Softer than the open,
                       * which is what reads as rhythmic rather than as
                       * damage. Both are far shorter than the shortest
                       * step (1/32 at 160 BPM ~ 1,125 frames). */
static volatile uint8_t g_gat_pat;   /* pattern index; the T4 tap cycles */
static uint32_t g_tg_idx;            /* step 0..15, re-anchored per block */
static uint32_t g_tg_ph;             /* frames into the current step */
/* The gate gain is Q12: 4096 = unity. */
static int32_t g_gat_g = 4096;
#define CHR_LEN   1024u          /* 21.3 ms at 48 kHz */
#define CHR_MASK  (CHR_LEN - 1u)
static int16_t  g_chr_buf[CHR_LEN];
static uint32_t g_chr_w;
static uint32_t g_chr_ph;         /* LFO phase, full 32-bit wrap */
static volatile uint8_t  g_chr_mix;       /* FX2-550: chorus depth/mix, fader 2 */
static volatile uint8_t  g_gat_amt;       /* FX2-550: gate threshold,   fader 4 */

/* ===== FX2-558: PAGE 5 -- four effects that need ZERO new RAM =====
 * Each is skipped outright when its control is at rest, exactly as the
 * FX1 chain already does (flt_mode / chr_mix / dst_g / tg_sf). Neutral
 * is genuinely free -- verified by reading the loop, not the values.
 *   fader 1  BITCRUSH  0 = clean
 *   fader 2  RING MOD  0 = off  (LAYOUT-562; width was cut)
 *   fader 3  AUTO-WAH  0 = off
 *   fader 4  ECHO      reserved -- inert until Campaign R
 * PAGE 3 is phaser / sweep / tremolo / [empty].
 * ECHO is NOT here: the play ring holds only ~39 ms behind the read
 * pointer and the streamer overwrites it, so the "free ring tap" does
 * not exist (W203). A real echo needs ~7 KB and waits on Campaign R. */
static volatile uint8_t g_bcr_amt;         /* bitcrush depth */
static volatile uint8_t g_swp_amt;         /* auto-pan depth */
static volatile uint8_t g_trm_amt;         /* tremolo depth */
static uint32_t g_fx2_lfo;                 /* shared LFO phase, 32-bit wrap (A3: the chorus's; the page-3 lanes have their own below) */
/* STACKA-664 A3/A4/A5: per-lane clocks and types (runtime only, cleared by the FX reset). */
static uint32_t          g_lfo_ph[3];              /* A3: phaser / sweep / tremolo phase */
static volatile uint8_t  g_lfo_div[3] = { 0u, 0u, 2u };   /* A3: 0 = beat, 1 = half, 2 = quarter */
static volatile uint32_t g_lane_per[7];            /* A4: tapped period in engine frames; 0 = the grid. 0 gate 1 echo 2 phs 3 swp 4 trm 5 chorus 6 wobble (WOBTAP-675) */
static volatile uint64_t g_lane_anc;               /* A4: the gate's tapped downbeat (sample clock) */
static volatile uint8_t  g_dst_typ;                /* A5: 0 = soft (cubic), 1 = hard, 2 = fold, 3 = OFF */
static int32_t  g_bcr_hL, g_bcr_hR;        /* sample-and-hold state */
static uint32_t g_bcr_ph;

/* ===== FX 3 (PG8-560) -- page 3: ring mod / auto-wah / phaser / tremolo =====
 * All three new kernels are ZERO-or-near-zero RAM, which is the whole reason
 * they were chosen over echo: echo needs ~7 KB and the build is at 96.08%.
 *   RING MOD  0 B   -- one phase accumulator in .bss
 *   AUTO-WAH  20 B  -- its OWN SVF pair + envelope. It does not borrow the
 *                      FX1 filter: an effect on page 3 that only worked when
 *                      page 1 happened to be engaged would be a trap.
 *   PHASER    32 B  -- 4 allpass stages x 2 channels
 * Every one of them is inside the FXFAST-559 fx_any gate, so with the page at
 * neutral they cost exactly nothing -- not even the test. */
static volatile uint8_t g_rng_amt;         /* ring mod: depth AND carrier freq */
static volatile uint8_t g_awh_amt;         /* auto-wah depth */
static volatile uint8_t g_phs_amt;         /* phaser depth */
static uint32_t g_rng_ph;                  /* ring carrier phase */
static int32_t  g_awh_lowL, g_awh_bandL;   /* auto-wah SVF, L */
static int32_t  g_awh_lowR, g_awh_bandR;   /* auto-wah SVF, R */
static int32_t  g_awh_env;                 /* envelope follower */
static int32_t  g_phs_xi[8], g_phs_yo[8];  /* FXCOST-578: direct-form allpass state, 4 stages x 2 ch */
static int32_t  g_phs_fbL, g_phs_fbR;      /* FXRST-563: phaser feedback */
static volatile uint8_t g_fxrst_lock;      /* FXRST2-564: swallow the chord release */

/* ===== BNC-570 B1: PRINT TO NOWHERE =====
 * The bounce campaign's one unproven assumption is that the tap is in the
 * right place. These four counters answer it with a number. Nothing is
 * written to a take, so nothing can be damaged by being wrong. */
static volatile uint32_t g_bt_pk;    /* PASS C tap peak, master gain removed */
static volatile uint32_t g_bt_lat;   /* the same, latched for PASS A */
static volatile uint32_t g_bt_acc;   /* PASS A's running mean of the tap */
static volatile uint32_t g_bt_n;     /* blocks PASS A has consumed */

/* ===== BNC-597 B2: PRINT TO A REAL TAKE =====
 * A bounce is a take whose SOURCE is the bus instead of the jack. Same
 * recorder, same ring, same flush, same codec, same stop. Two flags:
 *   g_bnc_arm  main -> engine: the g_arm_req[] that follows is a bounce
 *   g_bnc_on   engine: the take in flight reads the bus (PASS A checks
 *              it together with g_rec_track, so it can never outlive
 *              the take it belongs to) */
static volatile int8_t   g_bnc_arm = -1;
static int64_t           g_bnc_arm_t;   /* TAPECOPY-684: uptime of the chord that armed (controls thread) */
static int8_t            g_bk_trk_ctl = -1;   /* HOLDSTOP-686: the bounce's track, as the controls thread armed it */
static int8_t            g_bk_hold_trk = -1;  /* HOLDSTOP-686: >= 0 while a HELD bounce waits for TN to come up */
static volatile uint32_t g_bk_stop_pos;       /* SMPSTART-687: the transport position when the held bounce stopped */
static volatile uint8_t  g_bnc_on;
static volatile uint32_t g_bnc_prints;     /* diag: bounces armed since boot */
static int16_t           g_bnc_live[BLK_FRAMES * 2u];   /* BNC3-605: the jack, saved for the monitor */
/* ===== INFX-672: THE INPUT PATH (STACK D) ===== */
#define RT_BOTH 0u
#define RT_IN   1u
#define RT_TRK  2u
static volatile uint8_t  g_pg_route[9];              /* per page (1..4 used): RT_BOTH / RT_IN / RT_TRK */
static int16_t           g_live_blk[BLK_FRAMES * 2u]; /* the jack, this block (was the mixer's tmp[]) */
static volatile uint32_t g_live_got;                  /* frames of it that are real (the rest read as 0) */
static volatile uint8_t  g_in_on;                     /* the input slot has work (controls thread) */
static volatile uint8_t  g_mon_mute;                  /* MUTEANY-738: the live monitor out of the mix, anywhere (session-only) */
static int32_t           g_mon_g_s = 256;             /* the mute's ramped gain, Q8 */
static volatile uint8_t  g_rt_flash;                  /* status-row flashes pending (routing feedback) */
static uint32_t          g_rt_tick;
static volatile uint8_t  g_mt_flash;                  /* MUTEANY-738 / MUTEFIX-739: the mute's sweep pending (1 = on: down, 2 = off: up) */
static uint8_t           g_mt_tog, g_mt_rel, g_mt_tap, g_mt_rst, g_mt_sp, g_mt_last;   /* MTDIAG-742: the PLAY-release trap */
static uint8_t           g_mt_cmb;                    /* MUTEFIX4-743: chord-release mute events */
static uint16_t          g_mt_tr;                     /* MUTEFIX4-743: the last track-ladder reading while the VOL pair was held (the sag, measured) */
static uint8_t           g_br_on, g_br_n;             /* BEATREP-749: the beat repeat is live; engages. BRCHOP-800: the chop is never touched by the repeat (no save / restore) */
static uint32_t          g_br_div;                    /* BRCHOP-800: the repeat's division of the loop (the rocker halves / doubles it) */
static volatile uint8_t  g_br_live;                   /* BRWIN-754: the streamer maps through g_br_w/g_br_b */
static volatile uint32_t g_br_w[NTRK], g_br_b[NTRK];   /* BRWIN-754 / BRCHOP-800: per track, in its own blocks: the window (<= the audible cycle) and its offset INSIDE THE AUDIBLE CYCLE (w = 0: not repeated) */
static volatile uint32_t g_br_a[NTRK];                /* BRSTICK-763: the window's phase anchor (0 at the engage; the playhead at a resize) */
static uint16_t          g_br_s8, g_br_len8;          /* BRFN-765 / BRCHOP-800: the window as Q8 fractions of the AUDIBLE cycle: start (0..255) and length (1..256) */
static volatile uint32_t g_br_shift;                  /* LOOPHOLD-804: the tape held by the repeat, in LOOP SAMPLES (mod the loop); session only, cleared on a song switch */
static uint8_t           g_vol_pair;                  /* MUTEFIX3-741: the VOL pair was consumed under PLAY and has not been released (mirrors _rt_swallow for the track-ladder code) */
static uint32_t          g_mt_tick;
#define INW_N 768u                                    /* delay line, frames: > WOB_BASE_SAMP + peaks (672) */
static int16_t           g_inw_line[INW_N * 2u];      /* 3,072 B: the monitor's wobble */
static uint32_t          g_inw_w;
static volatile uint32_t g_inw_blk, g_in_blk;        /* diag: blocks wobbled / blocks through the slot */
static int32_t           mix32[BLK_FRAMES];           /* the LEFT bus (was the mixer's static local) */
static int32_t           mix32R[BLK_FRAMES];          /* M63a: the RIGHT bus */
#define BNC_PRE_HALF 4u   /* PRE-608: the pre-emphasis FIR's delay, bus samples */
static int32_t           g_bnc_pre_hist[2][8];   /* PRE-608: the FIR's last 8 bus samples, L/R */
static volatile uint8_t  g_bnc_anch;        /* the take's first REC block has been anchored */
/* BAKE-619 (W298): the SPEED-BAKE. A print armed at tape speed s is
 * resampled by s on its way to flash (the streamer's flush), so it stores
 * what was heard at 1x. Captured at the anchor; consumed by the flush;
 * cleared at promotion. 1.0x bounces leave g_bk_spd at 0 = the 616 path. */
#define BK_ONE 65536u
static volatile uint32_t g_bk_spd;        /* Q16 input frames per baked frame; 0 = no bake */
static volatile int8_t   g_bk_trk = -1;   /* the print being baked */
static volatile uint32_t g_bk_ph;         /* Q16 phase from p0 (the frame at r_r) */
static volatile uint32_t g_bk_blocks;     /* baked blocks committed this print */
static uint32_t          s_bk_ph_next, s_bk_cons_next;   /* pack -> commit handoff */
static volatile uint32_t g_bk_last_spd; static volatile uint16_t g_bk_prints; static volatile uint8_t g_bk_capped;   /* diag (SPEEDBAKE-768: shrunk) */
/* SPEEDBAKE-768 (row 121): the audio thread's integral of the baked frames (Q16 = whole + rem),
 * the recorder count it last saw, and the loop's sample-exact baked length chosen at the stop. */
static uint32_t          g_bk_accf, g_bk_rc_prev, g_bk_lens;
static uint16_t          g_bk_accr;
/* SPEEDBAKE2-769: the bake's reference speed -- a tape copy (mode 1) prints the tape at its
 * anchor speed and a sweep RELATIVE to it; a sampler print (mode 2) prints what was heard
 * (relative to 1x). g_bk_m1: the integral has been re-based to identity for a tape copy. */
static uint32_t          g_bk_ref = BK_ONE;
static uint8_t           g_bk_m1;
/* SPDLOG-777: the speed log -- per audio block, the recorder count at the block's end and the
 * absolute tape speed the block was recorded at. Written by the audio thread (bnc_postpass),
 * read by the streamer's bake (bk_spd_at). 64 entries = 341 ms at 1x = the rec ring. */
#define SL_N 64u
static uint32_t          g_sl_rc[SL_N];     /* rec_count (samples since the take's start) at the END of the block; 0 = empty */
static uint32_t          g_sl_sp[SL_N];     /* the absolute speed, Q16 (floored at 12288) */
static volatile uint8_t  g_sl_wr;           /* the next slot */
static uint32_t          g_sl_base;         /* r_w - rec_count at the anchor: ring index -> rec_count units */
static volatile uint32_t g_bk_len;   /* TRUE-621: the loop in baked blocks, chosen at the stop (0 = derive at promotion) */
static volatile uint8_t  g_bk_mode;  /* TAPECOPY-684: 0 undecided, 1 TAPE COPY (no bake), 2 SAMPLER (bake) */
#define BK_DECIDE_MS 180             /* TAPECOPY-684: TN still down this long after the chord = sampler */
/* SPEEDBAKE2-769: the bake's speed relative to the print's reference (after g_bk_mode: it reads it). */
static inline uint32_t bk_rel(uint32_t cur)
{
	if (cur < 12288u) cur = 12288u;   /* RANGE-655: the floor (a stopped tape never divides by 0) */
	const uint32_t ref = (g_bk_mode == 1u) ? g_bk_ref : BK_ONE;
	uint32_t r = (ref == BK_ONE) ? cur : (uint32_t)(((uint64_t)cur << 16) / ref);
	return (r < 4096u) ? 4096u : r;
}
static uint8_t           g_bnc_p16m_prev;   /* the target's next-record mode before the arm */
static uint8_t           g_arm_gsh_prev;    /* the target's print gain before ANY arm (cancel restores) */
#define BNC_GSH 2u   /* BNC2-604: a print is stored 12 dB down -- room for four
                      * full-scale tracks + the monitor before the limiter -- and
                      * played back x4. Two bits of x3 flags; max 3. */

/* ===== TAPE-569: page 4 (T4) -- drive / tone / hiss / wobble =====
 * Ported from charlesvestal's tape-page branch. Every one of these has a
 * STATIC INITIALISER rather than an init function: his tfx_init/wob_init
 * are never called anywhere in his tree, so his structs get BSS zero-init
 * and tone starts 'fully dark' instead of flat. A value in .data cannot
 * be left uninitialised by a call that never happens. */
static volatile uint8_t g_tp_drive = 0u;
static volatile uint8_t g_tp_tone  = 128u;   /* 128 = flat */
static volatile uint8_t g_tp_hiss  = 128u;   /* HISS2-701: bipolar -- 128 = off, above = the cassette hiss, below = vinyl crackle */
static volatile uint8_t g_tp_wob   = 0u;
static int32_t  g_tp_toneL, g_tp_toneR;      /* tilt one-pole state */
static int32_t  g_tp_ck1, g_tp_ck2;          /* HISS2-701: the crackle resonator (two-pole, ~2.4 kHz, ~1.2 ms) */
static uint16_t g_tp_hsi;                    /* FXCOST-578: hiss table index */
#define TP_HISS_N     16384u
#define TP_HISS_MASK  (TP_HISS_N - 1u)
/* FXCOST-578: CASSETTE BAND (W269): half-pink tilt -> HPF 600 Hz -> LPF 9 kHz, rendered
 * OFFLINE, RMS 3200. Rumble -11.5, body flat 300-3k, shhh -5 at 3-8k, -13/-33 above. Loop 341 ms (2.9 Hz): noise has no pitch, the seam is inaudible.
 * -24/-46/-68 dB across the top three bands: cassette, not pink. Costs 2 loads
 * + 2 muls per frame, replacing 3 IIR poles x 2 channels + an LPF (~75 cyc). */
static const int16_t tp_hiss_tbl[TP_HISS_N] = {
	 -1423,  -2210,  -2268,  -1880,  -1540,  -1507,  -1672,  -1776,  -1548,   -859,     73,    714,    594,   -184,   -976,  -1285,
	 -1125,   -678,     13,    950,   2015,   2950,   3463,   3316,   2373,    738,  -1103,  -2516,  -3113,  -2821,  -1803,   -441,
	   709,   1112,    593,   -495,  -1501,  -1982,  -2018,  -1945,  -1813,  -1351,   -435,    698,   1733,   2490,   2964,   3285,
	  3535,   3638,   3516,   3207,   2795,   2373,   2047,   1867,   1775,   1594,   1116,    366,   -324,   -612,   -419,    104,
	   799,   1531,   1979,   1743,    838,   -190,   -716,   -572,   -131,    210,    433,    613,    595,    127,   -903,  -2364,
	 -3885,  -4993,  -5383,  -5167,  -4726,  -4355,  -4164,  -4046,  -3694,  -2893,  -1709,   -433,    593,   1132,   1145,    790,
	   341,     32,    -97,   -178,   -339,   -578,   -687,   -417,    172,    794,   1243,   1380,   1054,    150,  -1370,  -3306,
	 -5135,  -6313,  -6553,  -5841,  -4450,  -2901,  -1631,   -728,   -152,     12,   -231,   -540,   -505,    -39,    637,   1257,
	  1615,   1624,   1441,   1484,   2142,   3317,   4410,   4926,   4919,   4739,   4570,   4290,   3786,   3226,   2805,   2345,
	  1371,   -284,  -2035,  -3010,  -2802,  -1501,    593,   3043,   5299,   6789,   7045,   5914,   3777,   1422,   -487,  -1752,
	 -2569,  -3245,  -3861,  -4179,  -3917,  -3134,  -2277,  -1768,  -1586,  -1323,   -712,     76,    633,    693,    379,     35,
	  -158,   -264,   -324,   -295,   -191,    -48,    169,    506,    945,   1393,   1607,   1202,    -75,  -2017,  -3948,  -5187,
	 -5576,  -5421,  -4950,  -4104,  -2767,  -1002,    833,   2139,   2374,   1506,    209,   -663,   -713,    -21,   1129,   2291,
	  2921,   2695,   1651,    113,  -1373,  -2284,  -2474,  -2158,  -1550,   -868,   -480,   -590,  -1003,  -1336,  -1237,   -583,
	   373,   1291,   2135,   2951,   3576,   3931,   4214,   4633,   5161,   5563,   5566,   5016,   3858,   2173,    359,  -1010,
	 -1568,  -1273,   -399,    519,   1001,    961,    624,    231,   -101,   -403,   -712,   -872,   -697,   -307,     51,    409,
	   932,   1455,   1547,   1096,    472,    133,    296,    932,   1869,   2816,   3295,   2841,   1442,   -327,  -1689,  -2281,
	 -2269,  -1812,   -874,    300,   1066,    917,    -58,  -1306,  -2329,  -2997,  -3421,  -3786,  -4244,  -4694,  -4787,  -4323,
	 -3405,  -2192,   -878,    150,    548,    339,   -219,   -804,  -1087,   -962,   -680,   -581,   -776,  -1125,  -1441,  -1629,
	 -1605,  -1311,   -820,   -270,    261,    656,    686,    259,   -460,  -1277,  -2006,  -2329,  -1988,  -1116,    -96,    797,
	  1499,   1977,   2112,   1923,   1749,   1924,   2378,   2809,   3092,   3282,   3336,   3150,   2788,   2406,   1979,   1259,
	    88,  -1307,  -2418,  -2818,  -2361,  -1151,    445,   1807,   2414,   2305,   2018,   2036,   2310,   2310,   1546,     14,
	 -1850,  -3413,  -4093,  -3702,  -2687,  -1826,  -1578,  -1767,  -1892,  -1636,  -1010,   -160,    721,   1356,   1483,   1065,
	   402,    -34,      7,    292,    328,   -102,   -705,  -1092,  -1165,  -1014,   -706,   -243,    413,   1283,   2302,   3238,
	  3733,   3574,   2918,   2182,   1688,   1423,   1213,   1058,   1212,   1977,   3304,   4586,   5097,   4537,   3038,    985,
	 -1112,  -2847,  -4091,  -4803,  -4856,  -4207,  -3145,  -2162,  -1565,  -1365,  -1471,  -1793,  -2264,  -2920,  -3849,  -4982,
	 -6071,  -6850,  -7136,  -6881,  -6185,  -5232,  -4137,  -2921,  -1760,   -991,   -748,   -765,   -608,    -73,    676,   1289,
	  1470,   1119,    450,    -38,     56,    769,   1896,   3125,   4063,   4388,   3999,   3031,   1846,   1049,   1273,   2645,
	  4528,   5968,   6371,   5788,   4704,   3610,   2684,   1837,   1011,    279,   -331,   -875,  -1374,  -1670,  -1473,   -788,
	  -104,    171,    174,    304,    782,   1497,   2128,   2365,   2035,   1125,   -165,  -1528,  -2800,  -3893,  -4495,  -4181,
	 -2938,  -1250,    438,   1908,   2996,   3596,   3662,   3182,   2256,   1021,   -421,  -1897,  -3109,  -3824,  -4085,  -4222,
	 -4684,  -5632,  -6625,  -7013,  -6533,  -5366,  -3816,  -2107,   -466,    782,   1340,   1144,    551,    286,    839,   1900,
	  2577,   2163,    778,   -660,  -1146,   -309,   1405,   2993,   3529,   2859,   1614,    523,     -9,     74,    423,    510,
	   188,   -196,   -286,   -108,     23,   -184,   -767,  -1447,  -1807,  -1609,   -864,    321,   1657,   2614,   2853,   2612,
	  2457,   2711,   3229,   3622,   3543,   2848,   1777,    882,    613,   1106,   2220,   3527,   4402,   4440,   3732,   2592,
	  1263,     38,   -705,   -853,   -755,  -1000,  -1996,  -3566,  -4980,  -5607,  -5427,  -4855,  -4329,  -4086,  -4051,  -3865,
	 -3181,  -2063,  -1054,   -719,  -1043,  -1367,  -1104,   -342,    429,    903,   1053,    927,    739,    841,   1255,   1508,
	  1173,    375,   -345,   -516,    -85,    664,   1478,   2281,   3015,   3513,   3600,   3301,   2991,   3191,   3933,   4570,
	  4500,   3762,   2777,   1840,   1048,    457,     57,   -240,   -400,   -277,     31,     74,   -435,  -1171,  -1549,  -1401,
	 -1108,  -1190,  -1965,  -3419,  -5088,  -6179,  -6169,  -5218,  -3906,  -2716,  -1731,   -740,    440,   1736,   2771,   3110,
	  2662,   1720,    621,   -443,  -1305,  -1845,  -2164,  -2430,  -2528,  -2142,  -1192,    -30,    838,   1152,   1035,    669,
	   -22,  -1143,  -2390,  -3145,  -2937,  -1767,   -135,   1399,   2511,   2936,   2505,   1415,    124,   -894,  -1279,   -970,
	  -251,    497,   1056,   1475,   1936,   2406,   2545,   2102,   1199,    144,   -738,  -1181,  -1270,  -1514,  -2307,  -3367,
	 -3862,  -3087,  -1030,   1660,   4117,   5606,   5691,   4440,   2492,    685,   -531,  -1238,  -1664,  -1881,  -1899,  -1746,
	 -1303,   -432,    752,   1918,   2756,   3046,   2685,   1873,   1088,    654,    479,    266,   -111,   -494,   -655,   -523,
	  -244,    -79,   -211,   -725,  -1609,  -2626,  -3457,  -3925,  -3922,  -3408,  -2591,  -1905,  -1713,  -2043,  -2549,  -2644,
	 -1804,     31,   2461,   4985,   7123,   8390,   8431,   7310,   5613,   3961,   2419,    738,  -1005,  -2272,  -2506,  -1659,
	  -326,    618,    657,     -2,   -776,  -1354,  -1812,  -2291,  -2773,  -3117,  -3163,  -2838,  -2204,  -1438,   -755,   -291,
	   -84,   -133,   -369,   -606,   -613,   -285,    254,    793,   1274,   1795,   2389,   2884,   3115,   3103,   2881,   2345,
	  1382,    136,   -899,  -1220,   -797,   -154,    -50,   -960,  -2655,  -4321,  -5225,  -5177,  -4360,  -3034,  -1513,   -171,
	   741,   1290,   1756,   2308,   2890,   3240,   2975,   1878,    173,  -1543,  -2661,  -2863,  -2191,  -1001,    223,   1129,
	  1519,   1275,    537,   -149,   -189,    450,   1244,   1704,   1723,   1369,    689,   -181,   -944,  -1263,   -981,   -296,
	   356,    719,    973,   1496,   2314,   3000,   3145,   2668,   1807,    980,    529,    505,    639,    519,    -30,   -785,
	 -1463,  -1978,  -2334,  -2479,  -2273,  -1611,   -625,    309,    788,    608,    -91,   -859,  -1239,  -1042,   -421,    251,
	   717,   1013,   1233,   1396,   1517,   1575,   1520,   1390,   1376,   1754,   2522,   3206,   3308,   2710,   1567,    245,
	  -701,  -1010,  -1054,  -1475,  -2592,  -4190,  -5728,  -6671,  -6754,  -6139,  -5229,  -4187,  -2908,  -1509,   -386,    264,
	   588,    760,    748,    443,    -73,   -540,   -714,   -476,    136,    983,   1864,   2424,   2276,   1457,    469,   -163,
	  -218,    203,    818,   1356,   1672,   1688,   1316,    517,   -607,  -1827,  -2861,  -3458,  -3475,  -2902,  -1867,   -573,
	   765,   1897,   2628,   2985,   3206,   3590,   4267,   4965,   5078,   4183,   2498,    644,   -874,  -1894,  -2514,  -2795,
	 -2558,  -1704,   -673,   -126,   -220,   -602,   -923,   -941,   -476,    418,   1432,   2111,   2096,   1439,    584,     14,
	  -111,    -10,     48,    -32,   -249,   -528,   -731,   -755,   -670,   -684,   -780,   -581,    135,   1079,   1753,   1877,
	  1453,    640,   -307,  -1138,  -1739,  -2052,  -1965,  -1499,   -967,   -725,   -943,  -1553,  -2214,  -2497,  -2252,  -1651,
	  -929,   -318,   -121,   -482,  -1050,  -1194,   -557,    748,   2298,   3551,   4021,   3453,   1990,    188,  -1338,  -2238,
	 -2351,  -1590,    -87,   1746,   3383,   4344,   4328,   3449,   2231,   1254,    850,    966,   1117,    619,   -814,  -2668,
	 -4012,  -4301,  -3689,  -2697,  -1841,  -1529,  -1921,  -2721,  -3355,  -3428,  -2916,  -1960,   -671,    791,   2077,   2815,
	  2914,   2630,   2386,   2490,   2880,   3185,   3105,   2696,   2191,   1590,    660,   -603,  -1752,  -2282,  -2064,  -1321,
	  -376,    493,   1143,   1623,   2023,   2308,   2432,   2476,   2515,   2476,   2234,   1756,   1147,    614,    353,    361,
	   395,    245,   -169,   -898,  -1842,  -2674,  -3048,  -2801,  -1922,   -523,   1033,   2167,   2484,   2031,   1030,   -421,
	 -2178,  -3731,  -4464,  -4231,  -3421,  -2497,  -1712,  -1110,   -707,   -706,  -1343,  -2419,  -3309,  -3486,  -2818,  -1480,
	    90,   1289,   1669,   1247,    512,     51,    106,    547,   1120,   1581,   1741,   1582,   1239,    859,    619,    704,
	  1058,   1472,   1919,   2448,   2919,   3059,   2671,   1739,    486,   -642,  -1189,  -1023,   -377,    444,   1261,   1885,
	  2032,   1578,    737,   -187,  -1067,  -1872,  -2536,  -3003,  -3217,  -3028,  -2271,  -1047,    216,   1077,   1335,   1021,
	   346,   -391,   -952,  -1248,  -1344,  -1406,  -1566,  -1722,  -1637,  -1306,  -1012,   -949,   -934,   -709,   -383,   -297,
	  -509,   -744,   -796,   -708,   -606,   -545,   -506,   -481,   -457,   -294,    184,    846,   1205,    913,    167,   -574,
	 -1044,  -1140,   -732,    256,   1627,   2889,   3573,   3613,   3326,   2920,   2296,   1363,    286,   -628,  -1138,  -1210,
	  -995,   -697,   -516,   -627,  -1103,  -1861,  -2591,  -2819,  -2308,  -1323,   -320,    422,    795,    684,     32,   -905,
	 -1498,  -1149,    129,   1669,   2733,   2987,   2610,   2119,   1971,   2265,   2747,   3050,   2997,   2662,   2114,   1232,
	  -140,  -1912,  -3582,  -4371,  -3722,  -1751,    784,   2920,   4036,   4128,   3494,   2317,    761,   -790,  -1904,  -2424,
	 -2459,  -2155,  -1654,  -1130,   -768,   -807,  -1388,  -2260,  -2988,  -3398,  -3471,  -3089,  -2228,  -1145,   -151,    583,
	   917,    671,   -146,  -1188,  -1996,  -2320,  -2124,  -1399,   -201,   1173,   2343,   3140,   3492,   3287,   2605,   1830,
	  1362,   1295,   1385,   1313,    913,    304,   -126,     -1,    709,   1621,   2261,   2450,   2290,   1905,   1365,    765,
	   227,   -229,   -640,   -962,  -1130,  -1110,   -936,   -844,  -1245,  -2418,  -4209,  -6020,  -7139,  -7148,  -6062,  -4241,
	 -2328,   -955,   -266,     21,    264,    807,   1795,   2946,   3697,   3586,   2536,    892,   -843,  -2338,  -3467,  -4028,
	 -3682,  -2333,   -341,   1794,   3712,   5144,   5899,   5920,   5291,   4259,   3147,   2145,   1248,    376,   -507,  -1282,
	 -1634,  -1278,   -253,   1142,   2501,   3247,   2780,    941,  -1688,  -3997,  -5018,  -4526,  -3031,  -1365,   -272,   -131,
	  -862,  -2076,  -3277,  -3930,  -3630,  -2451,   -933,    381,   1254,   1741,   2040,   2240,   2258,   2038,   1699,   1482,
	  1593,   2082,   2739,   3193,   3256,   3055,   2803,   2566,   2176,   1371,    223,   -797,  -1283,  -1155,   -485,    624,
	  1945,   2998,   3266,   2709,   1729,    603,   -613,  -1683,  -2152,  -1764,   -810,     18,     78,   -800,  -2250,  -3650,
	 -4435,  -4265,  -3140,  -1438,    231,   1325,   1539,    897,   -133,   -879,   -995,   -606,    -21,    473,    713,    784,
	   924,   1281,   1765,   2104,   2050,   1533,    669,   -305,  -1106,  -1525,  -1550,  -1369,  -1234,  -1243,  -1239,  -1020,
	  -628,   -224,    286,   1119,   2196,   3162,   3660,   3450,   2549,   1332,    313,   -222,   -320,   -207,   -140,   -279,
	  -640,  -1195,  -1901,  -2606,  -3021,  -2877,  -2139,  -1108,   -281,     40,      0,    -61,    -27,    -14,   -109,   -301,
	  -544,   -664,   -416,    186,    771,    918,    498,   -185,   -645,   -736,   -724,   -827,  -1062,  -1493,  -2128,  -2725,
	 -3008,  -2941,  -2705,  -2554,  -2609,  -2762,  -2893,  -3081,  -3466,  -3999,  -4414,  -4395,  -3758,  -2552,   -976,    761,
	  2466,   3969,   5203,   6141,   6771,   7257,   7787,   8183,   8051,   7265,   6022,   4609,   3317,   2370,   1764,   1357,
	  1203,   1508,   2201,   2899,   3242,   3092,   2535,   1804,   1145,    712,    485,    267,   -110,   -622,  -1207,  -1947,
	 -2832,  -3477,  -3460,  -2772,  -1757,   -760,     37,    565,    831,    931,    958,   1007,   1202,   1514,   1747,   1859,
	  1976,   1997,   1627,    785,   -333,  -1442,  -2305,  -2825,  -3000,  -2831,  -2405,  -1928,  -1506,  -1051,   -488,    123,
	   661,    998,   1060,    847,    412,   -184,   -905,  -1685,  -2418,  -2965,  -3161,  -2988,  -2665,  -2424,  -2370,  -2543,
	 -2945,  -3516,  -4120,  -4539,  -4631,  -4457,  -4062,  -3331,  -2199,   -745,    860,   2387,   3596,   4205,   3957,   2882,
	  1355,    -90,   -945,   -962,   -292,    715,   1746,   2571,   3017,   3038,   2820,   2634,   2500,   2142,   1260,    -62,
	 -1275,  -1872,  -1857,  -1627,  -1532,  -1599,  -1686,  -1724,  -1644,  -1346,   -884,   -537,   -507,   -666,   -690,   -329,
	   392,   1161,   1653,   1702,   1127,   -138,  -1680,  -2727,  -2750,  -1907,   -888,   -275,   -147,   -297,   -501,   -625,
	  -716,   -906,  -1150,  -1227,   -898,    -35,   1171,   2143,   2336,   1815,   1307,   1532,   2489,   3512,   3986,   3827,
	  3300,   2793,   2703,   3102,   3601,   3733,   3379,   2694,   1744,    497,   -818,  -1745,  -2016,  -1667,   -909,    -54,
	   578,    728,    323,   -372,   -928,  -1162,  -1327,  -1842,  -2782,  -3676,  -3967,  -3551,  -2702,  -1698,   -760,   -178,
	  -145,   -492,   -835,   -982,   -988,   -991,  -1138,  -1462,  -1702,  -1476,   -706,    317,   1303,   2132,   2697,   2836,
	  2564,   2181,   1983,   1926,   1713,   1236,    729,    462,    453,    450,    160,   -379,   -780,   -778,   -541,   -415,
	  -542,   -739,   -779,   -692,   -632,   -601,   -409,     57,    444,    107,  -1166,  -2860,  -4203,  -4713,  -4344,  -3367,
	 -2210,  -1183,   -329,    432,   1084,   1519,   1598,   1134,    100,  -1135,  -2075,  -2545,  -2623,  -2375,  -1838,  -1026,
	    32,   1207,   2243,   2805,   2650,   1922,   1070,    436,     81,   -103,   -293,   -698,  -1473,  -2508,  -3358,  -3604,
	 -3090,  -1845,   -144,   1445,   2461,   2982,   3380,   3683,   3563,   2894,   1922,    890,   -104,   -824,   -877,    -97,
	  1225,   2571,   3567,   4080,   4121,   3780,   3171,   2377,   1506,    695,    -35,   -797,  -1615,  -2268,  -2436,  -2033,
	 -1377,   -945,   -953,  -1250,  -1473,  -1291,   -686,     53,    629,    785,    264,   -881,  -2158,  -3047,  -3377,  -3289,
	 -2946,  -2301,  -1268,    -75,    877,   1451,   1822,   2180,   2589,   2934,   2911,   2324,   1447,    767,    427,    155,
	  -341,   -980,  -1400,  -1468,  -1403,  -1356,  -1114,   -318,   1048,   2431,   3140,   2873,   1724,     46,  -1659,  -2939,
	 -3542,  -3458,  -2873,  -2059,  -1342,   -994,  -1023,  -1286,  -1712,  -2172,  -2406,  -2193,  -1462,   -418,    366,    373,
	  -186,   -499,   -112,    616,   1097,   1217,   1327,   1614,   1749,   1349,    558,    -18,     60,    609,   1047,    899,
	   120,   -869,  -1528,  -1670,  -1460,  -1058,   -487,    211,    893,   1402,   1749,   2133,   2723,   3411,   3860,   3767,
	  3040,   1861,    712,    140,    360,   1072,   1682,   1817,   1582,   1279,   1084,    994,    790,    217,   -647,  -1485,
	 -2106,  -2504,  -2693,  -2657,  -2480,  -2359,  -2300,  -2012,  -1270,   -239,    715,   1429,   2019,   2594,   3000,   2903,
	  2139,    874,   -561,  -1768,  -2342,  -2101,  -1312,   -558,   -192,    -60,     80,    211,    203,     81,     59,    256,
	   400,     75,   -740,  -1604,  -2100,  -2233,  -2337,  -2667,  -3120,  -3370,  -3231,  -2759,  -2044,  -1111,     -9,   1043,
	  1802,   2332,   2799,   3158,   3363,   3544,   3800,   4017,   3974,   3566,   2787,   1627,    278,   -725,   -878,   -141,
	  1074,   2197,   2831,   2911,   2622,   2199,   1835,   1597,   1408,   1231,   1091,    808,     51,  -1201,  -2466,  -3167,
	 -3115,  -2520,  -1642,   -680,     85,    315,   -167,  -1282,  -2846,  -4645,  -6302,  -7326,  -7445,  -6832,  -5912,  -4962,
	 -3980,  -2836,  -1446,     69,   1339,   1952,   1891,   1607,   1515,   1670,   2016,   2561,   3222,   3761,   3997,   3832,
	  3127,   1864,    299,  -1197,  -2306,  -2773,  -2445,  -1391,     79,   1549,   2638,   3120,   3046,   2720,   2486,   2530,
	  2820,   3130,   3201,   2988,   2704,   2513,   2321,   1942,   1352,    648,    -62,   -645,  -1013,  -1307,  -1807,  -2657,
	 -3718,  -4496,  -4398,  -3345,  -1940,   -897,   -607,  -1176,  -2353,  -3472,  -3889,  -3476,  -2597,  -1698,   -998,   -412,
	   382,   1541,   2711,   3344,   3166,   2274,   1141,    388,    271,    565,    940,   1210,   1319,   1298,   1204,   1071,
	   926,    808,    748,    707,    581,    325,     17,   -265,   -553,   -876,  -1089,   -966,   -519,    -21,    377,    748,
	  1086,   1255,   1202,    941,    428,   -305,  -1009,  -1372,  -1272,   -816,   -207,    398,    955,   1433,   1550,    851,
	  -681,  -2315,  -3154,  -2864,  -1794,   -651,    -36,     17,      1,    206,    314,   -118,  -1099,  -2153,  -2668,  -2372,
	 -1612,  -1062,  -1093,  -1554,  -2030,  -2122,  -1676,   -934,   -340,   -146,   -306,   -592,   -747,   -689,   -536,   -424,
	  -382,   -276,     73,    528,    700,    482,    175,     62,    200,    544,   1029,   1461,   1525,   1054,    187,   -766,
	 -1500,  -1856,  -1841,  -1482,   -790,    119,   1009,   1726,   2302,   2777,   3027,   2927,   2536,   1957,   1169,    130,
	 -1013,  -1903,  -2218,  -1829,   -788,    561,   1577,   1742,   1063,    -30,  -1070,  -1785,  -2068,  -1901,  -1356,   -544,
	   460,   1546,   2556,   3438,   4137,   4417,   4026,   2968,   1561,    298,   -391,   -338,    325,   1305,   2288,   2976,
	  3161,   2860,   2289,   1663,   1027,    242,   -890,  -2316,  -3582,  -4203,  -4133,  -3698,  -3236,  -2887,  -2661,  -2595,
	 -2645,  -2501,  -1848,   -814,    105,    522,    402,    -31,   -495,   -816,   -930,   -772,   -270,    533,   1337,   1717,
	  1489,    786,   -182,  -1193,  -1927,  -2094,  -1709,  -1079,   -467,     38,    403,    716,   1248,   2175,   3305,   4141,
	  4261,   3687,   2857,   2219,   1911,   1860,   2018,   2414,   2991,   3501,   3676,   3482,   3176,   3104,   3355,   3694,
	  3820,   3585,   3019,   2152,    843,  -1022,  -3191,  -5117,  -6332,  -6653,  -6195,  -5306,  -4358,  -3523,  -2766,  -1991,
	 -1075,    141,   1585,   2796,   3189,   2451,    844,   -801,  -1653,  -1440,   -487,    521,    919,    551,   -221,   -963,
	 -1480,  -1768,  -1898,  -1924,  -1864,  -1785,  -1758,  -1749,  -1712,  -1614,  -1378,   -978,   -467,     26,    289,    203,
	  -136,   -511,   -683,   -508,   -129,    -35,   -691,  -1991,  -3262,  -3901,  -3794,  -3071,  -1817,   -291,    955,   1504,
	  1483,   1443,   1800,   2361,   2480,   1858,    892,     86,   -541,  -1348,  -2522,  -3756,  -4422,  -4007,  -2564,   -760,
	   622,   1110,    692,   -143,   -627,   -273,    729,   1778,   2483,   2832,   2901,   2747,   2529,   2373,   2208,   1877,
	  1336,    680,     14,   -701,  -1521,  -2164,  -2011,   -776,    964,   2246,   2574,   2119,   1357,    739,    558,    837,
	  1331,   1779,   2079,   2198,   2069,   1665,   1157,    867,    974,   1340,   1676,   1770,   1451,    618,   -494,  -1386,
	 -1713,  -1443,   -731,    158,    826,    886,    294,   -660,  -1735,  -2935,  -4247,  -5284,  -5436,  -4503,  -2893,  -1109,
	   571,   1922,   2774,   3133,   3042,   2532,   1722,    797,    -50,   -615,   -714,   -211,    902,   2253,   3068,   2675,
	  1045,  -1178,  -3077,  -4015,  -3949,  -3173,  -1933,   -474,    856,   1783,   2294,   2520,   2424,   1801,    614,   -888,
	 -2385,  -3627,  -4351,  -4179,  -2868,   -779,   1160,   2078,   1747,    617,   -657,  -1642,  -2098,  -1900,  -1215,   -552,
	  -422,   -961,  -1801,  -2355,  -2383,  -2137,  -1916,  -1711,  -1240,   -171,   1508,   3317,   4663,   5275,   5178,   4455,
	  3205,   1622,     36,  -1138,  -1615,  -1454,  -1067,   -942,  -1326,  -2022,  -2438,  -2013,   -740,    781,   1833,   2038,
	  1522,    802,    427,    613,   1238,   1989,   2451,   2228,   1235,   -132,  -1249,  -1713,  -1605,  -1366,  -1337,  -1410,
	 -1210,   -564,    338,   1251,   2068,   2651,   2813,   2488,   1762,    798,   -165,   -793,   -756,      5,   1106,   1988,
	  2338,   2249,   2032,   1832,   1489,    800,   -193,  -1138,  -1562,  -1295,   -719,   -407,   -688,  -1580,  -2863,  -4143,
	 -5041,  -5365,  -5112,  -4325,  -3039,  -1535,   -390,    108,    320,    751,   1570,   2714,   3973,   4931,   5164,   4547,
	  3409,   2344,   1735,   1538,   1488,   1381,   1118,    587,   -221,  -1090,  -1751,  -1971,  -1571,   -571,    759,   2017,
	  2788,   2821,   2198,   1213,     61,  -1108,  -2002,  -2359,  -2134,  -1447,   -568,    137,    408,    182,   -542,  -1757,
	 -3261,  -4466,  -4605,  -3429,  -1499,    382,   1640,   1964,   1409,    491,   -199,   -442,   -286,    153,    651,    924,
	   757,    116,   -726,  -1342,  -1569,  -1537,  -1361,  -1041,   -543,     36,    408,    395,    192,     87,    113,    264,
	   759,   1689,   2612,   2874,   2247,   1122,    113,   -462,   -807,  -1440,  -2679,  -4270,  -5517,  -5878,  -5351,  -4241,
	 -2845,  -1406,   -172,    549,    438,   -513,  -1766,  -2504,  -2115,   -542,   1659,   3606,   4550,   4195,   2763,    801,
	 -1114,  -2566,  -3270,  -3022,  -1773,    235,   2487,   4412,   5584,   5807,   5115,   3758,   2190,    995,    696,   1461,
	  2880,   4141,   4585,   4163,   3241,   2052,    599,  -1023,  -2538,  -3565,  -3688,  -2757,  -1176,    422,   1717,   2652,
	  3050,   2679,   1712,    707,     53,   -264,   -346,   -169,    255,    773,   1277,   1836,   2518,   3104,   3168,   2423,
	   952,   -840,  -2426,  -3397,  -3560,  -2962,  -1878,   -651,    464,   1246,   1589,   1644,   1559,   1259,    734,    228,
	    29,    179,    423,    456,    163,   -407,  -1093,  -1688,  -2010,  -1967,  -1656,  -1312,  -1119,  -1188,  -1618,  -2325,
	 -2943,  -3072,  -2529,  -1403,    -38,   1108,   1675,   1472,    430,  -1308,  -3226,  -4599,  -4946,  -4217,  -2734,  -1108,
	    75,    666,    920,   1128,   1353,   1482,   1499,   1573,   1790,   1936,   1704,   1099,    433,    -58,   -276,   -171,
	   139,    318,     77,   -484,   -847,   -493,    546,   1687,   2392,   2519,   2071,   1065,   -258,  -1464,  -2365,  -3137,
	 -3869,  -4401,  -4610,  -4491,  -4076,  -3435,  -2642,  -1769,   -974,   -514,   -484,   -625,   -638,   -506,   -380,   -397,
	  -617,   -938,  -1161,  -1152,   -910,   -521,    -44,    507,   1072,   1586,   2060,   2555,   3075,   3502,   3651,   3382,
	  2632,   1429,    -69,  -1540,  -2599,  -2929,  -2381,  -1012,    865,   2661,   3660,   3352,   1746,   -596,  -2881,  -4474,
	 -4966,  -4150,  -2130,    589,   3239,   5023,   5533,   5072,   4312,   3690,   3222,   2701,   2052,   1459,   1112,   1056,
	  1206,   1375,   1377,   1139,    740,    399,    369,    866,   1913,   3078,   3682,   3506,   3010,   2689,   2487,   2031,
	  1235,    403,   -259,   -994,  -2110,  -3350,  -4075,  -3962,  -3196,  -2100,   -969,   -166,    -21,   -560,  -1516,  -2617,
	 -3737,  -4811,  -5683,  -6034,  -5565,  -4230,  -2190,    278,   2727,   4606,   5573,   5642,   4981,   3670,   1796,   -250,
	 -1835,  -2526,  -2360,  -1778,  -1349,  -1352,  -1659,  -2006,  -2249,  -2278,  -1831,   -738,    814,   2445,   3653,   3886,
	  2992,   1482,    121,   -570,   -471,    215,   1064,   1616,   1597,    999,    -85,  -1612,  -3514,  -5502,  -7010,  -7497,
	 -6860,  -5412,  -3509,  -1456,    412,   1829,   2697,   3048,   2980,   2651,   2197,   1559,    560,   -753,  -1967,  -2542,
	 -2159,   -898,    722,   1946,   2281,   1847,   1227,    980,   1325,   2202,   3324,   4150,   4181,   3395,   2227,   1106,
	   184,   -505,   -953,  -1297,  -1809,  -2538,  -3066,  -2915,  -2105,  -1047,   -111,    446,    429,    -76,   -561,   -561,
	  -232,   -123,   -477,  -1036,  -1483,  -1828,  -2251,  -2692,  -2824,  -2395,  -1443,   -288,    602,    848,    362,   -649,
	 -1863,  -3011,  -3884,  -4282,  -4071,  -3291,  -2198,  -1188,   -559,   -245,    124,    718,   1209,   1197,    754,    343,
	   345,    806,   1609,   2636,   3626,   4168,   4014,   3373,   2894,   3236,   4561,   6358,   7719,   7980,   7230,   6151,
	  5339,   4850,   4393,   3771,   3036,   2312,   1562,    637,   -482,  -1644,  -2716,  -3590,  -4106,  -4182,  -3973,  -3683,
	 -3256,  -2534,  -1626,   -844,   -407,   -293,   -204,    237,   1044,   1711,   1609,    532,  -1148,  -2747,  -3657,  -3642,
	 -2979,  -2206,  -1664,  -1371,  -1146,   -758,   -165,    387,    612,    389,   -256,  -1152,  -2112,  -3080,  -3996,  -4634,
	 -4661,  -3831,  -2291,   -645,    483,   1000,   1307,   1740,   2329,   2962,   3461,   3535,   2968,   1921,    937,    520,
	   721,   1177,   1478,   1473,   1258,   1033,   1016,   1353,   2015,   2700,   2941,   2507,   1634,    781,    299,    330,
	   748,   1079,    819,   -118,  -1410,  -2669,  -3647,  -4174,  -4084,  -3348,  -2154,   -725,    828,   2388,   3687,   4387,
	  4278,   3351,   1921,    578,   -260,   -620,   -806,  -1108,  -1521,  -1713,  -1430,   -834,   -404,   -538,  -1167,  -1837,
	 -2165,  -2076,  -1728,  -1382,  -1181,   -967,   -486,    230,    939,   1554,   2123,   2538,   2492,   1684,     51,  -2044,
	 -3920,  -4957,  -4938,  -4048,  -2582,   -798,    927,   2047,   2168,   1438,    419,   -450,  -1087,  -1520,  -1642,  -1320,
	  -655,     23,    431,    520,    359,    -68,   -715,  -1346,  -1768,  -1915,  -1718,  -1224,   -670,   -267,      8,    395,
	  1068,   1843,   2271,   2068,   1375,    507,   -321,  -1002,  -1501,  -1818,  -1980,  -2115,  -2373,  -2775,  -3205,  -3422,
	 -3110,  -2097,   -631,    726,   1554,   1870,   2068,   2501,   3097,   3509,   3548,   3365,   3251,   3289,   3208,   2572,
	  1177,   -643,  -2160,  -2666,  -1947,   -488,    995,   2188,   3182,   3928,   3987,   3030,   1347,   -383,  -1557,  -1832,
	 -1239,   -144,   1031,   1989,   2550,   2619,   2166,   1271,     91,  -1248,  -2555,  -3411,  -3386,  -2505,  -1268,   -127,
	   726,   1113,    967,    589,    368,    356,    368,    322,    259,    296,    593,   1053,   1243,    947,    449,     33,
	  -436,  -1302,  -2642,  -4135,  -5321,  -5803,  -5394,  -4367,  -3337,  -2769,  -2777,  -3229,  -3718,  -3656,  -2697,  -1029,
	   845,   2531,   3901,   4847,   5134,   4662,   3695,   2711,   2073,   1767,   1453,    882,    150,   -516,  -1060,  -1543,
	 -1891,  -1972,  -1897,  -1788,  -1490,   -856,     14,    979,   2012,   3103,   4186,   5150,   5737,   5644,   4894,   3783,
	  2464,    868,   -985,  -2797,  -4136,  -4638,  -4179,  -3016,  -1782,  -1216,  -1641,  -2781,  -4121,  -5155,  -5380,  -4502,
	 -2691,   -473,   1547,   2878,   3355,   3106,   2294,   1065,   -263,  -1166,  -1254,   -564,    512,   1470,   1972,   1955,
	  1472,    648,   -262,   -914,  -1117,  -1052,  -1105,  -1362,  -1423,   -814,    515,   2163,   3650,   4658,   5011,   4719,
	  4027,   3207,   2360,   1488,    597,   -187,   -574,   -363,    245,    739,    733,    291,   -131,    -30,    701,   1645,
	  2150,   1812,    804,   -212,   -643,   -444,     84,    731,   1359,   1795,   1991,   1979,   1664,   1010,    254,   -381,
	  -941,  -1604,  -2550,  -3790,  -4929,  -5372,  -4941,  -3962,  -2776,  -1549,   -491,    111,    127,   -232,   -581,   -675,
	  -569,   -496,   -579,   -718,   -772,   -701,   -580,   -523,   -472,   -175,    422,    963,   1150,   1207,   1571,   2341,
	  3288,   4106,   4490,   4267,   3435,   2057,    316,  -1368,  -2533,  -3049,  -3214,  -3336,  -3290,  -2662,  -1274,    496,
	  1886,   2288,   1677,    555,   -520,  -1321,  -1823,  -2012,  -2019,  -2052,  -2116,  -2017,  -1550,   -604,    686,   1907,
	  2742,   3250,   3622,   3881,   3911,   3625,   3093,   2503,   2077,   2000,   2276,   2633,   2569,   1676,     96,  -1507,
	 -2464,  -2515,  -1981,  -1552,  -1725,  -2455,  -3284,  -3763,  -3814,  -3704,  -3772,  -4139,  -4579,  -4725,  -4339,  -3403,
	 -2068,   -578,    850,   2038,   2816,   3073,   2826,   2171,   1278,    448,    -44,   -167,      2,    495,   1301,   2170,
	  2807,   3191,   3469,   3601,   3327,   2488,   1261,    -59,  -1303,  -2330,  -2909,  -2850,  -2273,  -1610,  -1201,  -1134,
	 -1448,  -2084,  -2668,  -2688,  -2013,  -1113,   -536,   -305,   -187,   -209,   -452,   -672,   -614,   -412,   -373,   -489,
	  -377,    343,   1591,   2715,   3021,   2466,   1572,    721,    -19,   -540,   -606,   -138,    673,   1510,   2084,   2210,
	  1849,   1120,    255,   -505,   -975,  -1066,   -810,   -355,     41,     84,   -299,   -811,  -1052,   -951,   -835,  -1075,
	 -1705,  -2446,  -3021,  -3331,  -3429,  -3370,  -3023,  -2167,   -846,    531,   1590,   2314,   2851,   3269,   3579,   3784,
	  3896,   3965,   4134,   4531,   4953,   4859,   3940,   2498,   1116,    104,   -692,  -1622,  -2744,  -3708,  -4141,  -3963,
	 -3370,  -2666,  -2073,  -1634,  -1320,  -1197,  -1421,  -2064,  -3010,  -4090,  -5145,  -5846,  -5821,  -5092,  -3946,  -2437,
	  -488,   1648,   3377,   4220,   4118,   3369,   2438,   1749,   1587,   2061,   3041,   4162,   5003,   5280,   4889,   3940,
	  2763,   1626,    520,   -560,  -1392,  -1730,  -1506,   -846,     41,   1036,   2111,   3153,   3881,   3939,   3079,   1440,
	  -413,  -1782,  -2230,  -1812,   -934,     19,    826,   1277,   1261,   1028,    921,    872,    588,     41,   -510,   -813,
	  -791,   -546,   -333,   -450,   -949,  -1545,  -1980,  -2281,  -2531,  -2624,  -2485,  -2235,  -1984,  -1712,  -1404,  -1197,
	 -1331,  -1905,  -2726,  -3361,  -3382,  -2712,  -1709,   -869,   -502,   -645,  -1154,  -1800,  -2293,  -2354,  -1787,   -516,
	  1290,   3256,   5007,   6149,   6333,   5700,   4910,   4437,   4159,   3683,   2824,   1746,    849,    512,    795,   1388,
	  1842,   1819,   1286,    587,    107,    -84,   -163,   -300,   -556,   -911,  -1286,  -1525,  -1488,  -1162,   -660,    -71,
	   605,   1331,   1940,   2267,   2243,   1908,   1505,   1375,   1536,   1508,    711,   -972,  -3077,  -5038,  -6578,  -7623,
	 -8184,  -8307,  -7971,  -7142,  -5863,  -4214,  -2303,   -439,    855,   1230,    831,    122,   -458,   -635,   -265,    689,
	  2043,   3361,   4222,   4476,   4176,   3430,   2375,   1195,    190,   -223,    244,   1406,   2586,   3153,   3139,   3057,
	  3171,   3248,   2887,   1908,    595,   -410,   -674,   -378,    -77,   -217,   -823,  -1558,  -2121,  -2446,  -2561,  -2512,
	 -2332,  -1971,  -1409,   -786,   -347,   -258,   -465,   -791,  -1059,  -1091,   -788,   -292,      0,   -229,   -834,  -1301,
	 -1252,   -745,   -119,    339,    523,    368,   -246,  -1221,  -2009,  -2047,  -1347,   -429,    215,    589,   1089,   1880,
	  2533,   2377,   1177,   -512,  -1746,  -1858,   -784,    863,   2052,   2057,   1056,    -84,   -769,  -1153,  -1521,  -1773,
	 -1722,  -1357,   -786,   -166,    392,    871,   1200,   1165,    653,    -94,   -735,  -1233,  -1740,  -2145,  -2094,  -1471,
	  -509,    528,   1437,   1972,   1929,   1324,    471,   -149,   -238,    137,    725,   1335,   1807,   2025,   2088,   2172,
	  2233,   2046,   1477,    618,   -383,  -1523,  -2877,  -4340,  -5522,  -5928,  -5296,  -3811,  -1860,    401,   2909,   5272,
	  6869,   7291,   6484,   4671,   2328,     95,  -1454,  -2009,  -1623,   -688,    213,    573,    339,     22,    224,   1091,
	  2246,   3136,   3492,   3541,   3619,   3578,   2858,   1236,   -670,  -1857,  -1861,  -1070,   -238,    177,    241,    193,
	   114,    -15,   -219,   -484,   -785,  -1144,  -1499,  -1681,  -1643,  -1508,  -1351,  -1174,  -1046,   -983,   -846,   -592,
	  -464,   -712,  -1211,  -1524,  -1362,   -858,   -361,    -86,      8,     99,    279,    528,    864,   1376,   2021,   2479,
	  2381,   1711,    815,     79,   -240,     13,    778,   1593,   1812,   1183,     88,   -828,  -1166,   -990,   -693,   -707,
	 -1280,  -2375,  -3656,  -4693,  -5245,  -5321,  -5046,  -4559,  -3964,  -3339,  -2758,  -2240,  -1747,  -1291,   -961,   -802,
	  -718,   -465,    208,   1348,   2692,   3831,   4477,   4520,   3900,   2685,   1297,    342,    142,    530,   1131,   1716,
	  2233,   2715,   3213,   3630,   3727,   3326,   2370,    905,   -828,  -2328,  -3079,  -2988,  -2458,  -1976,  -1693,  -1458,
	 -1219,  -1150,  -1341,  -1571,  -1487,   -942,   -184,    331,    314,   -272,  -1340,  -2614,  -3573,  -3767,  -3155,  -2125,
	 -1262,   -988,  -1294,  -1705,  -1561,   -527,   1119,   2666,   3401,   3001,   1641,   -166,  -1782,  -2570,  -2107,   -501,
	  1563,   3255,   4145,   4290,   3930,   3353,   2943,   2947,   3173,   3116,   2462,   1320,    -94,  -1731,  -3548,  -5214,
	 -6118,  -5758,  -4261,  -2327,   -650,    389,    763,    843,   1187,   2090,   3311,   4284,   4605,   4190,   3061,   1380,
	  -372,  -1622,  -2077,  -1697,   -558,   1060,   2633,   3648,   3933,   3674,   3142,   2465,   1692,    952,    387,     20,
	  -257,   -694,  -1619,  -3056,  -4491,  -5272,  -5124,  -4204,  -2931,  -1740,   -876,   -390,   -171,   -129,   -392,  -1027,
	 -1630,  -1683,  -1254,   -829,   -645,   -732,  -1224,  -2069,  -2713,  -2453,  -1052,   1039,   2934,   3827,   3467,   2216,
	   674,   -757,  -1954,  -2825,  -3151,  -2767,  -1796,   -546,    813,   2239,   3493,   4145,   4000,   3311,   2442,   1552,
	   747,    168,   -181,   -451,   -756,  -1109,  -1505,  -1931,  -2382,  -2844,  -3193,  -3264,  -3071,  -2795,  -2573,  -2409,
	 -2212,  -1773,   -828,    643,   2286,   3661,   4564,   4914,   4563,   3544,   2346,   1562,   1423,   1840,   2583,   3327,
	  3789,   3823,   3377,   2594,   1851,   1478,   1526,   1844,   2194,   2375,   2389,   2382,   2388,   2238,   1799,   1305,
	  1209,   1575,   1918,   1692,    802,   -314,  -1020,   -985,   -493,   -227,   -707,  -1903,  -3368,  -4659,  -5513,  -5820,
	 -5683,  -5325,  -4861,  -4309,  -3717,  -3105,  -2374,  -1447,   -476,    273,    736,   1158,   1890,   2978,   3966,   4262,
	  3625,   2284,    759,   -380,   -746,   -302,    564,   1169,    918,   -289,  -2020,  -3681,  -4867,  -5413,  -5295,  -4646,
	 -3679,  -2478,   -970,    816,   2543,   3649,   3724,   2851,   1522,    281,   -533,   -923,  -1162,  -1318,  -1056,   -128,
	  1244,   2593,   3488,   3653,   3063,   1924,    558,   -719,  -1664,  -2084,  -1885,  -1235,   -558,   -221,   -335,   -840,
	 -1613,  -2552,  -3559,  -4323,  -4372,  -3542,  -2164,   -684,    752,   2212,   3581,   4474,   4522,   3639,   2136,    605,
	  -382,   -500,    180,   1218,   2223,   3052,   3525,   3420,   2751,   1793,    924,    363,     62,    -25,    210,    776,
	  1313,   1302,    578,   -475,  -1296,  -1656,  -1789,  -2031,  -2400,  -2613,  -2520,  -2323,  -2255,  -2293,  -2252,  -2030,
	 -1745,  -1723,  -2219,  -3008,  -3465,  -3140,  -2097,   -784,    270,    710,    468,   -268,  -1153,  -1752,  -1684,   -840,
	   541,   1982,   2977,   3256,   2981,   2671,   2791,   3331,   3858,   3854,   3027,   1503,   -241,  -1608,  -2137,  -1772,
	  -894,    -10,    594,    948,   1121,   1015,    562,   -111,   -694,   -804,   -222,    962,   2433,   3682,   4157,   3612,
	  2229,    511,   -974,  -1994,  -2682,  -3203,  -3682,  -4219,  -4681,  -4759,  -4341,  -3511,  -2309,   -853,    412,    873,
	   264,  -1105,  -2668,  -4016,  -5006,  -5537,  -5357,  -4280,  -2416,   -204,   1718,   2907,   3401,   3518,   3527,   3558,
	  3613,   3592,   3410,   3088,   2684,   2262,   1948,   1854,   2001,   2407,   3009,   3481,   3441,   2769,   1556,    -33,
	 -1740,  -3155,  -3710,  -2988,  -1252,    507,   1209,    547,   -747,  -1584,  -1381,   -334,    819,   1262,    644,   -646,
	 -1859,  -2541,  -2628,  -2176,  -1329,   -301,    658,   1190,   1013,    267,   -550,   -956,   -712,    113,   1094,   1625,
	  1427,    882,    704,   1286,   2424,   3676,   4790,   5612,   5884,   5413,   4305,   2871,   1506,    506,   -173,   -715,
	 -1075,  -1046,   -575,     99,    606,    751,    529,    -38,   -771,  -1283,  -1334,  -1071,   -850,  -1019,  -1682,  -2583,
	 -3396,  -4007,  -4408,  -4527,  -4264,  -3597,  -2610,  -1454,   -371,    417,    834,    867,    574,    201,     59,    277,
	   644,    699,    135,   -902,  -2038,  -2971,  -3463,  -3368,  -2892,  -2537,  -2534,  -2491,  -1901,   -746,    582,   1632,
	  2078,   1869,   1191,    297,   -605,  -1361,  -1797,  -1724,  -1158,   -516,   -357,   -812,  -1427,  -1600,  -1069,     -7,
	  1122,   1777,   1737,   1287,    822,    504,    420,    734,   1516,   2559,   3506,   4067,   4052,   3415,   2406,   1453,
	   827,    546,    493,    551,    711,    997,   1250,   1255,   1109,   1169,   1543,   1907,   1836,   1186,    215,   -729,
	 -1552,  -2250,  -2585,  -2244,  -1184,    225,   1324,   1647,   1312,    771,    401,    448,    940,   1510,   1664,   1223,
	   380,   -598,  -1590,  -2502,  -3094,  -3135,  -2591,  -1661,   -649,    198,    759,   1082,   1414,   1925,   2341,   2161,
	  1393,    736,    807,   1466,   2089,   2149,   1356,   -290,  -2391,  -4287,  -5284,  -4846,  -2938,   -240,   2190,   3602,
	  3956,   3671,   3034,   1975,    461,  -1118,  -2098,  -1964,   -736,    854,   1810,   1616,    496,   -938,  -2129,  -2764,
	 -2792,  -2369,  -1731,  -1136,   -800,   -688,   -536,   -212,    161,    461,    690,    821,    744,    426,    -31,   -522,
	  -976,  -1310,  -1467,  -1418,  -1153,   -732,   -286,     26,     90,    -44,   -122,    240,   1293,   2780,   4011,   4357,
	  3595,   1971,     45,  -1595,  -2514,  -2479,  -1507,     33,   1431,   1999,   1615,    820,    210,   -182,   -659,  -1217,
	 -1464,  -1066,   -100,    952,   1525,   1286,    267,  -1183,  -2611,  -3627,  -4012,  -3909,  -3648,  -3345,  -2952,  -2465,
	 -1838,   -999,    -96,    561,    726,    276,   -638,  -1488,  -1668,   -952,    284,   1370,   1895,   1906,   1747,   1794,
	  2174,   2685,   3034,   3047,   2708,   2195,   1818,   1776,   1969,   2068,   1790,   1200,    631,    183,   -377,  -1137,
	 -1806,  -2026,  -1732,  -1149,   -545,   -101,     74,     -7,   -220,   -468,   -693,   -788,   -642,   -166,    642,   1495,
	  1841,   1302,     79,  -1197,  -2033,  -2340,  -2233,  -1861,  -1411,  -1024,   -767,   -654,   -656,   -814,  -1219,  -1790,
	 -2268,  -2400,  -2058,  -1276,   -238,    748,   1368,   1560,   1590,   1789,   2140,   2227,   1773,   1034,    529,    469,
	   618,    661,    487,    100,   -445,   -906,  -1041,   -993,  -1158,  -1622,  -2029,  -1988,  -1450,   -781,   -412,   -353,
	  -234,    169,    667,    952,   1025,   1210,   1751,   2601,   3626,   4699,   5486,   5517,   4675,   3531,   2878,   2918,
	  3176,   3133,   2632,   1729,    435,  -1175,  -2815,  -4133,  -4812,  -4589,  -3504,  -2014,   -693,    171,    637,    804,
	   638,     73,   -889,  -2073,  -3142,  -3806,  -4023,  -3999,  -4031,  -4184,  -4100,  -3396,  -2191,  -1040,   -389,   -144,
	   180,    884,   1777,   2437,   2575,   2123,   1207,     72,  -1001,  -1652,  -1553,   -702,    507,   1598,   2403,   3035,
	  3462,   3539,   3375,   3232,   3224,   3372,   3660,   3877,   3651,   2786,   1554,    564,    285,    658,   1118,   1097,
	   506,   -374,  -1282,  -2147,  -3008,  -3820,  -4381,  -4436,  -3857,  -2863,  -1934,  -1439,  -1411,  -1566,  -1445,   -759,
	   312,   1255,   1664,   1559,   1268,   1007,    755,    497,    308,    223,    227,    345,    623,   1062,   1517,   1750,
	  1600,   1070,    287,   -639,  -1690,  -2876,  -4189,  -5511,  -6559,  -6998,  -6635,  -5672,  -4664,  -3961,  -3386,  -2583,
	 -1463,   -291,    660,   1458,   2293,   3092,   3597,   3613,   3049,   1921,    451,   -919,  -1696,  -1577,   -680,    395,
	   834,    119,  -1555,  -3395,  -4572,  -4681,  -3787,  -2220,   -371,   1392,   2836,   3929,   4752,   5401,   5943,   6388,
	  6649,   6483,   5566,   3815,   1697,     12,   -740,   -580,    244,   1355,   2174,   2343,   2193,   2336,   2947,   3734,
	  4376,   4647,   4310,   3279,   1813,    315,   -969,  -1895,  -2317,  -2249,  -1988,  -1938,  -2278,  -2835,  -3373,  -3841,
	 -4234,  -4449,  -4380,  -3976,  -3265,  -2357,  -1405,   -507,    332,   1127,   1830,   2261,   2237,   1914,   1631,   1454,
	  1271,   1090,    931,    664,     98,   -858,  -2048,  -3068,  -3430,  -2834,  -1458,     73,   1125,   1411,   1026,    365,
	   -47,    118,    784,   1568,   2095,   2177,   1835,   1234,    468,   -551,  -1777,  -2846,  -3378,  -3250,  -2574,  -1708,
	 -1165,  -1223,  -1716,  -2309,  -2859,  -3347,  -3593,  -3303,  -2321,   -726,   1141,   2774,   3698,   3600,   2555,   1114,
	   -29,   -522,   -557,   -605,   -775,   -623,    281,   1840,   3488,   4527,   4480,   3395,   1849,    528,   -188,   -386,
	  -413,   -477,   -568,   -574,   -275,    466,   1387,   1946,   1750,    863,   -214,   -968,  -1267,  -1252,  -1024,   -586,
	    46,    763,   1424,   2028,   2684,   3315,   3633,   3509,   3056,   2297,   1107,   -413,  -1777,  -2525,  -2637,  -2377,
	 -1915,  -1260,   -390,    676,   1756,   2425,   2286,   1433,    515,    197,    560,   1081,   1161,    615,   -219,   -802,
	  -894,   -634,   -188,    431,   1267,   2241,   2994,   3030,   2053,    136,  -2155,  -3911,  -4616,  -4575,  -4367,  -4198,
	 -3926,  -3351,  -2327,   -844,    782,   1918,   1994,   1001,   -412,  -1447,  -1770,  -1651,  -1595,  -1904,  -2540,  -3258,
	 -3812,  -4070,  -4025,  -3727,  -3262,  -2803,  -2647,  -2965,  -3430,  -3398,  -2557,  -1175,    233,   1254,   1688,   1622,
	  1349,   1154,   1217,   1535,   1883,   2068,   2217,   2599,   3197,   3771,   4238,   4660,   4930,   4811,   4272,   3511,
	  2692,   1897,   1306,   1096,   1166,   1184,    900,    337,   -280,   -688,   -823,   -946,  -1286,  -1670,  -1860,  -1869,
	 -1687,  -1174,   -370,    420,    930,   1146,   1125,    792,     87,   -790,  -1351,  -1226,   -624,   -155,   -311,  -1172,
	 -2371,  -3392,  -3978,  -4169,  -4031,  -3535,  -2626,  -1306,    266,   1713,   2593,   2682,   2035,    901,   -318,  -1299,
	 -2030,  -2616,  -3056,  -3398,  -3847,  -4489,  -4983,  -4731,  -3500,  -1686,     45,   1237,   1831,   2037,   2003,   1721,
	  1140,    312,   -421,   -558,     51,   1071,   2074,   2836,   3321,   3513,   3377,   3022,   2688,   2464,   2239,   1961,
	  1773,   1835,   2092,   2298,   2210,   1767,   1113,    555,    401,    490,    144,  -1054,  -2771,  -4368,  -5322,  -5204,
	 -3817,  -1510,    986,   3126,   4690,   5517,   5556,   5054,   4326,   3518,   2742,   2196,   1961,   1845,   1611,   1213,
	   680,      9,   -683,  -1026,   -697,    136,    935,   1274,   1102,    649,    117,   -418,   -917,  -1363,  -1793,  -2242,
	 -2730,  -3320,  -3920,  -4132,  -3666,  -2777,  -1955,  -1378,   -844,   -129,    627,    900,    277,  -1091,  -2592,  -3687,
	 -4269,  -4404,  -3935,  -2736,  -1030,    742,   2098,   2605,   2175,   1218,    373,    149,    723,   1857,   2989,   3482,
	  3009,   1788,    467,   -249,    -67,    719,   1576,   2147,   2338,   2162,   1562,    582,   -329,   -654,   -439,   -103,
	   274,    927,   1882,   2835,   3437,   3565,   3326,   2854,   2224,   1427,    358,   -982,  -2344,  -3395,  -3888,  -3670,
	 -2749,  -1486,   -492,    -65,     76,    225,    238,    -68,   -470,   -584,   -295,    264,   1013,   1986,   3076,   3821,
	  3719,   2771,   1518,    528,     13,   -260,   -774,  -1811,  -3168,  -4304,  -4665,  -4019,  -2604,  -1008,     96,    309,
	  -135,   -572,   -583,   -298,   -134,   -282,   -477,   -384,    -17,    333,    345,   -137,   -915,  -1560,  -1859,  -1959,
	 -1987,  -1789,  -1098,    115,   1420,   2172,   2014,   1074,   -187,  -1322,  -2174,  -2708,  -2725,  -2051,   -916,    186,
	   884,   1035,    692,    122,   -311,   -368,      4,    666,   1307,   1656,   1698,   1564,   1206,    355,  -1070,  -2674,
	 -3837,  -4065,  -3225,  -1637,    147,   1662,   2579,   2664,   1923,    644,   -720,  -1631,  -1704,  -1009,      3,    971,
	  1870,   2738,   3410,   3737,   3698,   3191,   2126,    693,   -796,  -2111,  -3040,  -3358,  -3043,  -2492,  -2308,  -2726,
	 -3335,  -3480,  -2939,  -2059,  -1340,  -1132,  -1516,  -2226,  -2737,  -2542,  -1488,    -16,   1120,   1531,   1436,   1195,
	   960,    819,    835,    883,    731,    340,   -110,   -472,   -606,   -288,    481,   1291,   1690,   1561,   1136,    819,
	   871,   1166,   1359,   1290,   1141,   1122,   1181,   1149,   1003,    912,   1134,   1836,   2858,   3825,   4582,   5252,
	  5845,   6118,   5809,   4863,   3527,   2214,   1216,    577,    182,    -78,   -226,   -212,    -64,     40,    -75,   -399,
	  -757,   -964,   -944,   -800,   -735,   -906,  -1433,  -2369,  -3518,  -4421,  -4646,  -4069,  -2916,  -1557,   -325,    513,
	   837,    761,    445,   -189,  -1221,  -2291,  -2738,  -2230,  -1084,     53,    766,   1019,    928,    619,    228,   -143,
	  -565,  -1234,  -2118,  -2819,  -2965,  -2587,  -2069,  -1768,  -1677,  -1493,  -1058,   -549,   -166,    167,    692,   1499,
	  2316,   2700,   2568,   2295,   2222,   2406,   2739,   2943,   2678,   1842,    665,   -449,  -1250,  -1910,  -2777,  -3854,
	 -4746,  -5034,  -4521,  -3303,  -1740,   -272,    773,   1247,   1229,    919,    369,   -418,  -1114,  -1160,   -300,   1113,
	  2374,   2978,   2986,   2798,   2613,   2317,   1835,   1343,   1051,    997,   1150,   1509,   2018,   2552,   2931,   2919,
	  2358,   1307,     31,  -1186,  -2283,  -3343,  -4268,  -4772,  -4658,  -3953,  -2863,  -1612,   -289,   1106,   2484,   3579,
	  4109,   3999,   3280,   1983,    331,  -1218,  -2268,  -2709,  -2709,  -2513,  -2224,  -1825,  -1345,   -896,   -524,    -67,
	   721,   1778,   2676,   3013,   2737,   2187,   1789,   1703,   1780,   1811,   1717,   1459,    972,    287,   -472,  -1166,
	 -1637,  -1849,  -2052,  -2562,  -3388,  -4216,  -4758,  -5004,  -5004,  -4577,  -3419,  -1455,    894,   2826,   3733,   3612,
	  2744,   1305,   -416,  -1793,  -2289,  -1932,  -1138,   -314,    220,    270,    -52,   -382,   -453,   -314,   -295,   -737,
	 -1650,  -2646,  -3219,  -3054,  -2185,   -938,    233,    937,   1001,    457,   -489,  -1461,  -2083,  -2203,  -1845,  -1042,
	   154,   1465,   2363,   2441,   1867,   1286,   1135,   1350,   1714,   2052,   2240,   2344,   2510,   2689,   2718,   2639,
	  2660,   2822,   2903,   2652,   2026,   1176,    319,   -363,   -774,   -820,   -371,    499,   1349,   1651,   1220,    354,
	  -494,  -1084,  -1427,  -1557,  -1350,   -671,    388,   1525,   2402,   2860,   2964,   2877,   2783,   2825,   2928,   2805,
	  2285,   1541,    902,    512,    243,    -69,   -472,   -924,  -1275,  -1370,  -1343,  -1524,  -2007,  -2532,  -2807,  -2759,
	 -2435,  -1863,  -1108,   -334,    201,    233,   -275,  -1001,  -1527,  -1654,  -1373,   -776,   -183,   -130,   -945,  -2339,
	 -3627,  -4291,  -4147,  -3226,  -1833,   -550,    141,    237,    130,    172,    432,    768,    999,   1040,    967,    935,
	  1049,   1227,   1176,    702,      5,   -547,   -773,   -637,   -114,    557,    811,    295,   -723,  -1592,  -1833,  -1448,
	  -724,     34,    557,    736,    753,    875,   1108,   1208,    952,    276,   -710,  -1712,  -2448,  -2846,  -2866,  -2338,
	 -1210,    285,   1739,   2682,   2766,   2042,    973,    118,   -223,    -84,    363,    938,   1405,   1562,   1437,   1240,
	  1138,   1104,   1019,    894,    822,    810,    812,    884,   1207,   1883,   2723,   3316,   3316,   2601,   1232,   -576,
	 -2437,  -3816,  -4219,  -3492,  -1931,   -103,   1494,   2604,   3125,   3013,   2375,   1553,    947,    708,    686,    584,
	   130,   -747,  -1807,  -2580,  -2662,  -2132,  -1587,  -1590,  -2062,  -2283,  -1583,     11,   1784,   2882,   2882,   1974,
	   612,   -811,  -1902,  -2210,  -1514,   -130,   1247,   2013,   2007,   1465,    747,     93,   -473,  -1069,  -1801,  -2694,
	 -3627,  -4258,  -4264,  -3634,  -2562,  -1255,     30,   1005,   1554,   1804,   1883,   1859,   1843,   1829,   1557,    790,
	  -338,  -1454,  -2256,  -2533,  -2252,  -1747,  -1455,  -1449,  -1470,  -1335,  -1087,   -872,   -911,  -1468,  -2542,  -3676,
	 -4305,  -4262,  -3778,  -3097,  -2344,  -1697,  -1308,   -986,   -294,    992,   2709,   4415,   5472,   5394,   4327,   3011,
	  2238,   2310,   2974,   3825,   4567,   4946,   4754,   3976,   2894,   1956,   1441,   1345,   1513,   1763,   2013,   2294,
	  2622,   2958,   3244,   3335,   3126,   2676,   1954,    794,   -716,  -2201,  -3346,  -3989,  -3966,  -3136,  -1682,   -157,
	   888,   1267,   1208,   1130,   1305,   1774,   2498,   3353,   4082,   4336,   3760,   2221,    -76,  -2790,  -5573,  -7974,
	 -9436,  -9602,  -8593,  -6903,  -5071,  -3542,  -2606,  -2290,  -2401,  -2701,  -3006,  -3210,  -3251,  -3125,  -2926,  -2823,
	 -2931,  -3156,  -3251,  -3012,  -2341,  -1298,   -117,    967,   1721,   1871,   1367,    590,    107,    214,    736,   1254,
	  1427,   1248,   1110,   1484,   2430,   3473,   3991,   3685,   2695,   1436,    369,   -181,    -91,    571,   1539,   2406,
	  2808,   2666,   2183,   1544,    816,     78,   -621,  -1280,  -1735,  -1764,  -1393,   -791,     25,   1102,   2256,   3124,
	  3436,   3136,   2386,   1485,    727,    350,    482,   1086,   2002,   2982,   3729,   4052,   3943,   3459,   2596,   1255,
	  -646,  -2865,  -4601,  -4931,  -3698,  -1686,    -25,    501,     -1,   -664,   -777,   -457,   -395,  -1017,  -1974,  -2604,
	 -2676,  -2417,  -2064,  -1733,  -1572,  -1644,  -1707,  -1323,   -211,   1316,   2331,   2086,    738,   -906,  -2240,  -3197,
	 -3854,  -4073,  -3640,  -2597,  -1350,   -401,    -20,   -141,   -463,   -727,   -906,  -1025,   -892,   -220,   1062,   2659,
	  3991,   4516,   4078,   2987,   1746,    734,    175,    210,    805,   1712,   2640,   3349,   3556,   2993,   1632,   -336,
	 -2500,  -4134,  -4562,  -3677,  -1951,    -68,   1406,   2190,   2340,   2161,   1947,   1779,   1668,   1670,   1773,   1828,
	  1639,   1128,    463,    -25,    -98,    283,   1040,   1964,   2639,   2582,   1601,     14,  -1613,  -2909,  -3714,  -3880,
	 -3305,  -2105,   -730,    190,    285,   -299,  -1110,  -1789,  -2202,  -2375,  -2465,  -2608,  -2770,  -2905,  -3094,  -3408,
	 -3706,  -3645,  -2968,  -1746,   -292,    977,   1713,   1840,   1466,    710,   -186,   -700,   -318,    924,   2377,   3314,
	  3473,   3157,   2888,   2922,   2961,   2441,   1249,    -99,  -1052,  -1425,  -1258,   -614,    385,   1440,   2241,   2720,
	  2967,   3115,   3343,   3734,   4038,   3808,   2862,   1303,   -736,  -2997,  -4898,  -5779,  -5374,  -4018,  -2346,   -775,
	   524,   1411,   1848,   1924,   1700,   1234,    707,    361,    316,    496,    757,    960,    941,    605,      3,   -684,
	 -1227,  -1512,  -1630,  -1625,  -1318,   -568,    468,   1401,   1807,   1518,    825,    232,     20,    165,    496,    781,
	   773,    317,   -423,   -922,   -831,   -449,   -390,   -842,  -1363,  -1530,  -1506,  -1657,  -1917,  -1893,  -1387,   -518,
	   513,   1440,   1879,   1600,    763,   -303,  -1364,  -2315,  -3119,  -3709,  -3901,  -3476,  -2440,  -1126,    -31,    438,
	   229,   -380,  -1038,  -1438,  -1324,   -611,    547,   1740,   2462,   2412,   1662,    536,   -669,  -1817,  -2768,  -3306,
	 -3356,  -3038,  -2500,  -1930,  -1535,  -1287,   -933,   -374,    190,    616,   1065,   1744,   2537,   3077,   3133,   2745,
	  2077,   1411,   1117,   1332,   1736,   1808,   1273,    258,   -805,  -1350,   -923,    516,   2477,   4144,   4867,   4613,
	  3872,   3101,   2319,   1277,      9,  -1073,  -1715,  -2013,  -2090,  -1990,  -1762,  -1410,   -963,   -623,   -552,   -528,
	  -196,    516,   1481,   2483,   3145,   3181,   2634,   1624,    189,  -1433,  -2728,  -3237,  -2886,  -2072,  -1328,   -904,
	  -706,   -520,   -252,     51,    336,    572,    698,    683,    657,    819,   1135,   1255,    706,   -724,  -2671,  -4346,
	 -5141,  -4997,  -4174,  -2907,  -1450,   -138,    812,   1437,   1900,   2323,   2661,   2682,   2238,   1533,    882,    353,
	  -168,   -730,  -1203,  -1417,  -1380,  -1360,  -1750,  -2693,  -3831,  -4529,  -4394,  -3579,  -2582,  -1776,  -1198,   -773,
	  -492,   -249,    234,   1067,   1984,   2707,   3271,   3815,   4256,   4349,   3988,   3276,   2445,   1818,   1546,   1349,
	   753,   -372,  -1601,  -2366,  -2444,  -1967,  -1168,   -281,    456,    846,    938,   1088,   1550,   2097,   2293,   1977,
	  1325,    630,    203,    235,    553,    668,    159,   -945,  -2133,  -2722,  -2335,  -1174,    206,   1359,   2152,   2705,
	  3143,   3357,   3110,   2417,   1611,   1000,    637,    392,     88,   -376,   -933,  -1339,  -1354,  -1034,   -748,   -789,
	 -1018,  -1008,   -507,    372,   1327,   2086,   2501,   2522,   2271,   2075,   2099,   2038,   1438,    252,  -1083,  -2050,
	 -2335,  -1910,  -1115,   -440,    -60,    167,    251,    -52,   -922,  -2147,  -3122,  -3345,  -2864,  -2046,  -1222,   -594,
	   -90,    568,   1385,   1952,   1882,   1181,    185,   -686,  -1147,  -1082,   -479,    516,   1504,   1974,   1666,    717,
	  -550,  -1815,  -2761,  -3175,  -3173,  -3048,  -2857,  -2432,  -1716,   -967,   -599,   -748,  -1114,  -1334,  -1264,   -926,
	  -492,   -349,   -842,  -1906,  -3144,  -4079,  -4287,  -3637,  -2428,  -1166,   -268,     59,   -113,   -424,   -397,    239,
	  1370,   2549,   3211,   3098,   2505,   2001,   1951,   2382,   3099,   3766,   4042,   3826,   3330,   2849,   2460,   2075,
	  1719,   1539,   1508,   1405,   1126,    772,    426,    103,   -170,   -358,   -372,    -77,    391,    570,    194,   -473,
	 -1009,  -1359,  -1795,  -2426,  -2951,  -2948,  -2345,  -1632,  -1533,  -2286,  -3275,  -3602,  -2890,  -1398,    306,   1700,
	  2536,   2880,   3016,   3248,   3637,   3972,   4016,   3674,   3020,   2341,   1972,   1975,   2041,   1747,    949,     27,
	  -336,    151,    954,   1250,    740,   -277,  -1383,  -2358,  -3156,  -3711,  -3854,  -3508,  -2910,  -2425,  -2185,  -2174,
	 -2370,  -2653,  -2830,  -2771,  -2437,  -1860,  -1060,    -31,   1048,   1688,   1478,    543,   -605,  -1644,  -2663,  -3752,
	 -4637,  -4917,  -4514,  -3749,  -2997,  -2336,  -1636,   -819,     36,    665,    807,    531,    202,    133,    402,    869,
	  1320,   1713,   2134,   2505,   2653,   2602,   2530,   2515,   2498,   2535,   2896,   3676,   4527,   5003,   4850,   3966,
	  2538,   1121,    298,    240,    593,    819,    735,    599,    587,    511,    223,    -58,    -62,    213,    630,   1045,
	  1242,   1066,    616,    127,   -258,   -515,   -597,   -321,    495,   1773,   3124,   4042,   4261,   3940,   3429,   2908,
	  2337,   1652,    902,    168,   -542,  -1303,  -2212,  -3242,  -4258,  -5199,  -6052,  -6670,  -6714,  -5846,  -4150,  -2321,
	 -1209,  -1199,  -1999,  -2838,  -3002,  -2355,  -1322,   -465,    -78,    -48,     58,    767,   2301,   4385,   6431,   7738,
	  7749,   6385,   4137,   1765,   -104,  -1169,  -1540,  -1645,  -1795,  -1868,  -1595,  -1020,   -509,   -318,   -381,   -549,
	  -716,   -651,    -40,   1181,   2645,   3701,   3826,   2871,    960,  -1457,  -3515,  -4389,  -3847,  -2345,   -748,    185,
	   236,   -374,  -1373,  -2582,  -3835,  -4980,  -5867,  -6210,  -5713,  -4429,  -2797,  -1279,   -166,    320,    103,   -471,
	  -781,   -405,    641,   1992,   3133,   3674,   3634,   3336,   2976,   2530,   2040,   1705,   1646,   1678,   1380,    479,
	  -788,  -1839,  -2367,  -2512,  -2501,  -2491,  -2565,  -2541,  -2013,   -734,   1080,   2762,   3577,   3283,   2263,   1170,
	   474,    111,   -466,  -1614,  -2986,  -3984,  -4425,  -4441,  -4075,  -3320,  -2410,  -1790,  -1630,  -1681,  -1675,  -1578,
	 -1422,  -1080,   -363,    742,   2153,   3815,   5510,   6764,   7266,   7135,   6590,   5732,   4709,   3789,   3221,   2996,
	  2798,   2382,   1869,   1495,   1239,    990,    863,    981,   1076,    671,   -294,  -1296,  -1731,  -1445,   -779,   -128,
	   457,   1162,   2046,   2909,   3424,   3343,   2604,   1305,   -353,  -2093,  -3671,  -5005,  -6035,  -6528,  -6276,  -5409,
	 -4311,  -3271,  -2283,  -1233,   -186,    612,    888,    552,   -116,   -653,   -874,   -897,   -823,   -676,   -571,   -706,
	 -1127,  -1687,  -2187,  -2381,  -2129,  -1562,   -884,   -151,    661,   1550,   2407,   3067,   3425,   3357,   2668,   1324,
	  -359,  -1808,  -2517,  -2317,  -1344,    179,   2021,   3869,   5310,   5979,   5694,   4494,   2621,    478,  -1458,  -2832,
	 -3628,  -4044,  -3999,  -3140,  -1487,    289,   1326,   1195,    204,   -871,  -1495,  -1798,  -2237,  -3007,  -3882,  -4358,
	 -3960,  -2596,   -626,   1341,   2717,   3249,   3075,   2446,   1562,    633,   -137,   -660,  -1019,  -1278,  -1284,   -825,
	    58,   1016,   1584,   1471,    772,    -38,   -415,   -151,    612,   1637,   2704,   3672,   4390,   4586,   4095,   3043,
	  1706,    384,   -597,   -982,   -807,   -495,   -606,  -1390,  -2511,  -3304,  -3311,  -2574,  -1581,   -875,   -656,   -801,
	 -1256,  -2219,  -3742,  -5290,  -5990,  -5398,  -3823,  -1907,   -114,   1401,   2665,   3656,   4212,   4143,   3392,   2094,
	   660,   -307,   -485,   -104,    441,   1000,   1629,   2392,   3165,   3566,   3282,   2444,   1587,   1168,   1147,   1158,
	   927,    378,   -465,  -1443,  -2187,  -2418,  -2252,  -1865,  -1170,   -104,   1059,   1769,   1541,    371,  -1257,  -2751,
	 -3628,  -3561,  -2525,   -882,    872,   2382,   3383,   3636,   3054,   1733,    -66,  -1903,  -3346,  -4167,  -4366,  -4106,
	 -3522,  -2650,  -1586,   -569,    216,    884,   1656,   2423,   2864,   2899,   2726,   2576,   2588,   2743,   2843,   2701,
	  2375,   2122,   2083,   2097,   1867,   1289,    481,   -458,  -1474,  -2267,  -2306,  -1293,    404,   1918,   2540,   2130,
	   973,   -498,  -1857,  -2751,  -3030,  -2940,  -2983,  -3372,  -3724,  -3485,  -2577,  -1459,   -670,   -498,   -993,  -1953,
	 -2939,  -3533,  -3595,  -3214,  -2572,  -1872,  -1269,   -840,   -574,   -320,    175,    995,   1839,   2187,   1850,   1219,
	   855,   1072,   1894,   3044,   3933,   3914,   2736,    797,  -1086,  -2208,  -2411,  -2036,  -1575,  -1340,  -1247,   -925,
	  -157,    859,   1768,   2351,   2490,   2120,   1460,   1078,   1416,   2297,   3051,   3087,   2387,   1374,    432,   -339,
	 -1095,  -2005,  -2892,  -3258,  -2753,  -1524,    -98,   1056,   1851,   2383,   2518,   2003,    982,     67,   -205,    223,
	  1063,   1966,   2672,   3067,   3113,   2778,   2040,    955,   -120,   -620,   -320,    430,    981,    744,   -395,  -1921,
	 -3177,  -3900,  -4221,  -4336,  -4276,  -3925,  -3225,  -2336,  -1549,  -1032,   -652,   -107,    681,   1424,   1818,   1881,
	  1804,   1632,   1235,    550,   -272,  -1053,  -1781,  -2473,  -3042,  -3360,  -3335,  -2960,  -2267,  -1338,   -414,    240,
	   577,    779,   1072,   1506,   1904,   2088,   2041,   1794,   1336,    606,   -414,  -1470,  -2124,  -2136,  -1576,   -731,
	    -2,    352,    438,    615,   1119,   1794,   2135,   1752,    950,    570,   1068,   2121,   3143,   3804,   4064,   3911,
	  3320,   2557,   2085,   1997,   1838,   1109,   -173,  -1572,  -2692,  -3288,  -3198,  -2445,  -1266,     28,   1201,   2039,
	  2255,   1620,    188,  -1580,  -3013,  -3707,  -3703,  -3240,  -2550,  -1825,  -1209,   -874,  -1006,  -1607,  -2410,  -3076,
	 -3414,  -3354,  -2792,  -1680,   -244,   1099,   2101,   2731,   2941,   2748,   2427,   2261,   2229,   2142,   2003,   2024,
	  2287,   2648,   2946,   3054,   2806,   2154,   1301,    461,   -349,  -1195,  -2008,  -2466,  -2244,  -1449,   -665,   -585,
	 -1521,  -3090,  -4491,  -5125,  -4820,  -3759,  -2429,  -1350,   -712,   -410,   -324,   -424,   -649,   -796,   -621,     59,
	  1404,   3374,   5486,   6999,   7423,   6753,   5278,   3411,   1578,     62,  -1041,  -1719,  -1955,  -1718,  -1027,    -64,
	   851,   1451,   1543,   1004,     82,   -501,    -59,   1471,   3460,   4999,   5449,   4707,   3102,   1166,   -655,  -2156,
	 -3166,  -3436,  -2947,  -2053,  -1222,   -752,   -682,   -867,  -1112,  -1417,  -2013,  -2903,  -3664,  -3975,  -4008,  -4140,
	 -4523,  -4941,  -4995,  -4340,  -2828,   -651,   1650,   3484,   4482,   4489,   3571,   2058,    466,   -697,  -1182,  -1043,
	  -451,    386,   1209,   1782,   1941,   1600,    763,   -392,  -1461,  -2050,  -2037,  -1465,   -380,   1075,   2560,   3670,
	  4151,   4005,   3369,   2418,   1448,    742,    286,   -166,   -764,  -1465,  -2139,  -2679,  -3048,  -3196,  -2987,  -2344,
	 -1426,   -534,    211,    875,   1397,   1564,   1274,    646,   -122,   -914,  -1615,  -1957,  -1618,   -474,   1220,   2930,
	  4198,   4778,   4518,   3372,   1583,   -346,  -1921,  -2917,  -3448,  -3792,  -4033,  -4029,  -3728,  -3298,  -3001,  -3069,
	 -3508,  -3986,  -4046,  -3397,  -2064,   -387,   1047,   1648,   1199,    -40,  -1529,  -2692,  -3083,  -2517,  -1180,    397,
	  1670,   2433,   2788,   2904,   2899,   2791,   2517,   2044,   1431,    813,    400,    369,    612,    666,    175,   -614,
	 -1121,  -1025,   -361,    741,   2055,   3092,   3331,   2720,   1755,   1099,   1094,   1473,   1678,   1464,   1033,    686,
	   542,    507,    387,    -37,   -919,  -2157,  -3330,  -3946,  -3807,  -3118,  -2233,  -1267,    -91,   1278,   2472,   2946,
	  2282,    464,  -1994,  -4203,  -5325,  -5102,  -4011,  -2745,  -1620,   -571,    508,   1557,   2439,   3165,   3842,   4439,
	  4805,   4863,   4664,   4301,   3913,   3731,   3819,   3922,   3815,   3516,   3065,   2388,   1387,    127,  -1099,  -2004,
	 -2614,  -3189,  -3781,  -4079,  -3845,  -3230,  -2570,  -2075,  -1698,  -1207,   -370,    804,   1994,   2874,   3434,   3810,
	  3937,   3623,   2903,   2151,   1703,   1513,   1337,   1140,   1036,    889,    272,  -1079,  -2878,  -4375,  -4920,  -4452,
	 -3445,  -2453,  -1700,  -1032,   -283,    421,    825,    929,   1003,   1170,   1207,    837,     70,   -884,  -1830,  -2626,
	 -3102,  -3077,  -2512,  -1571,   -539,    336,    935,   1219,   1200,   1029,    875,    724,    442,    -48,   -710,  -1464,
	 -2271,  -3105,  -3822,  -4177,  -4016,  -3380,  -2460,  -1550,   -922,   -656,   -557,   -313,    206,    821,   1249,   1339,
	  1149,    970,   1110,   1515,   1807,   1699,   1310,   1121,   1462,   2076,   2462,   2503,   2424,   2317,   1920,    903,
	  -615,  -1986,  -2642,  -2534,  -1859,   -748,    711,   2371,   3877,   4696,   4470,   3287,   1641,    195,   -608,   -746,
	  -482,    -99,    349,    974,   1601,   1693,    929,   -342,  -1352,  -1614,  -1275,   -779,   -417,   -171,    181,    694,
	  1048,    831,   -103,  -1452,  -2521,  -2601,  -1495,    243,   1665,   2135,   1670,    692,   -333,  -1132,  -1639,  -1897,
	 -2059,  -2335,  -2733,  -2938,  -2593,  -1701,   -655,    131,    557,    779,    803,    362,   -577,  -1485,  -1828,  -1678,
	 -1459,  -1323,  -1012,   -223,    922,   1902,   2348,   2312,   2059,   1879,   1933,   2147,   2402,   2661,   2770,   2493,
	  1825,   1054,    571,    620,   1053,   1339,   1092,    533,    176,    263,    744,   1437,   1969,   1891,   1056,   -188,
	 -1267,  -1852,  -2051,  -2180,  -2399,  -2619,  -2768,  -2973,  -3323,  -3647,  -3693,  -3411,  -2961,  -2471,  -1923,  -1340,
	  -893,   -784,  -1007,  -1249,  -1186,   -752,    -53,    676,   1053,    870,    423,    236,    596,   1408,   2258,   2727,
	  2768,   2663,   2702,   2851,   2698,   1840,    306,  -1484,  -3106,  -4271,  -4671,  -4090,  -2742,  -1133,    412,   1902,
	  3367,   4563,   5124,   4917,   4101,   3063,   2320,   2161,   2410,   2731,   2975,   3082,   2943,   2453,   1549,    383,
	  -583,   -943,   -664,     83,   1171,   2408,   3396,   3689,   3166,   2131,    946,   -273,  -1484,  -2451,  -2969,  -3192,
	 -3354,  -3459,  -3437,  -3237,  -2816,  -2295,  -1966,  -1982,  -2306,  -2970,  -3977,  -5049,  -5822,  -6129,  -5970,  -5404,
	 -4483,  -3263,  -1881,   -534,    609,   1474,   2124,   2732,   3484,   4414,   5187,   5189,   4110,   2395,    958,    438,
	   708,   1068,    852,    -53,  -1187,  -1995,  -2310,  -2461,  -2850,  -3382,  -3570,  -3118,  -2148,  -1020,    -79,    571,
	  1037,   1397,   1561,   1393,    934,    427,    117,    222,    880,   1813,   2328,   1943,    921,     49,   -126,    226,
	   371,   -440,  -2312,  -4532,  -6138,  -6534,  -5688,  -3996,  -1943,     32,   1477,   2014,   1619,    791,    262,    361,
	   890,   1615,   2425,   3036,   3074,   2613,   2227,   2285,   2590,   2731,   2529,   2171,   1980,   2152,   2722,   3534,
	  4225,   4494,   4339,   3961,   3453,   2707,   1709,    689,   -163,   -833,  -1352,  -1734,  -1989,  -2145,  -2236,  -2218,
	 -1976,  -1469,   -772,    -64,    424,    599,    595,    683,   1024,   1462,   1686,   1608,   1454,   1445,   1520,   1437,
	   920,   -176,  -1626,  -3023,  -4135,  -4858,  -5131,  -4946,  -4301,  -3343,  -2461,  -2050,  -2195,  -2676,  -3175,  -3405,
	 -3212,  -2621,  -1742,   -794,    -69,    369,    679,    871,    835,    683,    623,    659,    705,    780,   1025,   1578,
	  2417,   3349,   4108,   4513,   4599,   4492,   4142,   3413,   2444,   1738,   1720,   2213,   2599,   2444,   1727,    692,
	  -334,  -1159,  -1812,  -2291,  -2440,  -2159,  -1572,  -1006,   -813,  -1037,  -1413,  -1728,  -1951,  -2110,  -2194,  -2106,
	 -1782,  -1348,  -1073,  -1240,  -2032,  -3305,  -4506,  -4907,  -4030,  -2066,    174,   1866,   2570,   2274,   1445,    794,
	   624,    685,    784,   1042,   1482,   1789,   1537,    493,  -1216,  -3071,  -4386,  -4766,  -4360,  -3680,  -3180,  -3011,
	 -3086,  -3183,  -3041,  -2478,  -1451,    -96,   1171,   1734,   1186,   -226,  -1660,  -2295,  -1943,  -1119,   -553,   -550,
	  -776,   -713,   -242,    396,   1153,   2245,   3618,   4860,   5634,   5794,   5328,   4580,   4172,   4436,   5108,   5601,
	  5459,   4606,   3293,   1886,    693,   -137,   -663,  -1099,  -1555,  -1901,  -1891,  -1378,   -539,    202,    639,   1014,
	  1694,   2639,   3309,   3152,   2076,    404,  -1427,  -3024,  -4087,  -4467,  -4177,  -3361,  -2336,  -1507,  -1131,  -1240,
	 -1710,  -2356,  -3054,  -3759,  -4334,  -4525,  -4171,  -3379,  -2400,  -1441,   -686,   -208,    198,    828,   1691,   2494,
	  2941,   2902,   2385,   1567,    754,    137,   -260,   -345,     90,   1048,   2145,   2945,   3332,   3412,   3180,   2454,
	  1153,   -465,  -1846,  -2437,  -2007,   -754,    792,   2038,   2617,   2501,   1914,   1134,    398,   -105,   -282,   -157,
	   192,    647,   1067,   1342,   1313,    748,   -372,  -1599,  -2259,  -1963,   -905,    271,    968,   1027,    647,    117,
	  -322,   -544,   -639,   -817,  -1080,  -1145,   -804,   -189,    399,    833,   1255,   1834,   2517,   2973,   2763,   1753,
	   287,  -1153,  -2300,  -3020,  -3197,  -2863,  -2216,  -1469,   -859,   -544,   -450,   -363,    -75,    508,   1287,   2091,
	  2785,   3209,   3210,   2797,   2146,   1459,    898,    553,    386,    259,     38,   -341,   -907,  -1761,  -2999,  -4478,
	 -5724,  -6160,  -5461,  -3758,  -1612,    282,   1510,   2061,   2152,   2022,   1818,   1641,   1663,   2004,   2537,   2980,
	  3070,   2612,   1570,    156,  -1254,  -2384,  -3284,  -4174,  -5050,  -5595,  -5532,  -4884,  -3823,  -2529,  -1265,   -274,
	   417,    928,   1344,   1651,   1753,   1516,    995,    603,    869,   1920,   3296,   4350,   4699,   4247,   3081,   1573,
	   317,   -271,   -168,    441,   1326,   2235,   2796,   2511,   1140,   -961,  -2953,  -4041,  -3946,  -2956,  -1714,   -876,
	  -657,   -700,   -459,    348,   1656,   3169,   4474,   5238,   5346,   4736,   3267,    993,  -1571,  -3673,  -4755,  -4702,
	 -3925,  -3162,  -2904,  -2993,  -2972,  -2662,  -2218,  -1872,  -1697,  -1542,  -1245,   -885,   -691,   -769,   -948,   -870,
	  -264,    826,   2094,   3166,   3742,   3698,   3136,   2373,   1736,   1242,    692,     90,   -196,    225,   1365,   2790,
	  3803,   3746,   2425,    298,  -1893,  -3535,  -4200,  -3811,  -2809,  -1824,  -1230,  -1005,   -879,   -683,   -500,   -414,
	  -246,    251,    990,   1524,   1509,   1109,    800,    761,    687,    282,   -355,   -968,  -1453,  -1771,  -1808,  -1522,
	 -1074,   -631,   -129,    587,   1439,   2071,   2108,   1435,    226,  -1190,  -2442,  -3195,  -3332,  -3062,  -2673,  -2360,
	 -2290,  -2433,  -2443,  -2039,  -1242,   -135,   1177,   2309,   2707,   2162,   1047,    -78,   -910,  -1394,  -1539,  -1324,
	  -691,    336,   1621,   2968,   4110,   4807,   5014,   4802,   4184,   3155,   1958,   1184,   1326,   2194,   3047,   3270,
	  2833,   2199,   1844,   1849,   1885,   1631,   1130,    608,     87,   -583,  -1440,  -2334,  -2956,  -2892,  -1931,   -413,
	   966,   1648,   1508,    904,    364,     86,   -106,   -424,   -897,  -1450,  -2055,  -2757,  -3649,  -4807,  -6096,  -7199,
	 -7898,  -8103,  -7680,  -6508,  -4743,  -2847,  -1290,   -317,     64,      2,   -266,   -446,   -267,    360,   1230,   2005,
	  2508,   2755,   2933,   3393,   4282,   5280,   5920,   6014,   5675,   5114,   4493,   3911,   3418,   2997,   2552,   1986,
	  1272,    373,   -694,  -1714,  -2406,  -2660,  -2529,  -2115,  -1512,   -715,    288,   1264,   1699,   1170,   -146,  -1462,
	 -2067,  -1896,  -1406,  -1097,  -1186,  -1579,  -1971,  -2038,  -1747,  -1455,  -1559,  -2063,  -2505,  -2300,  -1177,    624,
	  2531,   4071,   4969,   4874,   3615,   1791,    470,    198,    689,   1281,   1403,    806,   -375,  -1676,  -2517,  -2557,
	 -1895,   -918,     24,    794,   1393,   1776,   1839,   1559,   1068,    586,    195,   -282,  -1003,  -1854,  -2612,  -3156,
	 -3500,  -3752,  -3975,  -4159,  -4280,  -4305,  -4118,  -3517,  -2353,   -720,   1000,   2402,   3314,   3758,   3863,   3908,
	  4144,   4514,   4739,   4508,   3598,   2150,    659,   -430,   -974,  -1145,  -1264,  -1443,  -1549,  -1580,  -1732,  -2069,
	 -2413,  -2525,  -2296,  -1753,   -934,    126,   1375,   2720,   3886,   4484,   4364,   3682,   2609,   1224,   -280,  -1526,
	 -2284,  -2668,  -2931,  -3144,  -3035,  -2299,  -1021,    326,   1137,   1021,     89,  -1066,  -1702,  -1486,   -745,    -29,
	   422,    562,    308,   -231,   -642,   -559,    129,   1357,   2911,   4381,   5274,   5270,   4416,   3025,   1370,   -336,
	 -1754,  -2579,  -2767,  -2429,  -1736,   -921,   -158,    471,    835,    795,    369,   -261,   -842,  -1116,   -868,    -68,
	   984,   1843,   2243,   2106,   1458,    539,   -253,   -659,   -752,   -864,  -1318,  -2209,  -3383,  -4524,  -5272,  -5356,
	 -4708,  -3418,  -1603,    432,   1974,   2174,    749,  -1619,  -3649,  -4315,  -3440,  -1627,    193,   1262,   1452,   1361,
	  1763,   2912,   4367,   5322,   5173,   4002,   2462,   1148,    250,   -321,   -779,  -1262,  -1663,  -1826,  -1811,  -1764,
	 -1671,  -1442,  -1125,   -874,   -807,   -968,  -1351,  -1952,  -2701,  -3328,  -3519,  -3181,  -2421,  -1414,   -400,    352,
	   707,    794,    969,   1465,   2079,   2425,   2341,   1964,   1545,   1234,   1064,   1023,    928,    457,   -512,  -1614,
	 -2098,  -1414,    244,   2015,   3029,   3018,   2301,   1308,    306,   -485,   -800,   -505,    304,   1372,   2385,   3085,
	  3342,   3106,   2408,   1445,    487,   -273,   -689,   -682,   -384,     12,    535,   1286,   1982,   2078,   1435,    424,
	  -628,  -1631,  -2520,  -3143,  -3351,  -3149,  -2673,  -2016,  -1213,   -295,    652,   1419,   1795,   1753,   1386,    831,
	   330,     76,    -89,   -509,  -1166,  -1578,  -1349,   -644,    -48,     19,   -322,   -691,   -910,  -1037,  -1080,   -917,
	  -574,   -362,   -521,   -945,  -1345,  -1491,  -1318,   -939,   -499,     34,    815,   1822,   2836,   3732,   4583,   5436,
	  6118,   6287,   5641,   4177,   2292,    505,   -886,  -1776,  -2107,  -1934,  -1419,   -743,   -111,    332,    549,    540,
	   412,    486,   1038,   1915,   2587,   2591,   1842,    646,   -545,  -1429,  -1963,  -2299,  -2613,  -2830,  -2617,  -1768,
	  -629,      2,   -423,  -1621,  -2752,  -3141,  -2580,  -1252,    416,   1820,   2241,   1230,   -949,  -3440,  -5260,  -5771,
	 -5039,  -3747,  -2594,  -1794,  -1142,   -455,    224,    751,    958,    682,    -77,  -1098,  -2056,  -2648,  -2679,  -2097,
	 -1072,     23,    911,   1586,   2112,   2422,   2451,   2288,   2044,   1764,   1547,   1587,   2025,   2680,   3056,   2749,
	  1766,    437,   -850,  -1808,  -2315,  -2450,  -2410,  -2360,  -2372,  -2475,  -2697,  -2963,  -2982,  -2396,  -1095,    582,
	  2024,   2761,   2749,   2379,   2171,   2298,   2491,   2500,   2444,   2542,   2768,   2929,   2906,   2637,   2080,   1367,
	   820,    702,   1037,   1555,   1799,   1500,    762,   -112,   -848,  -1323,  -1498,  -1288,   -585,    569,   1824,   2647,
	  2701,   2121,   1363,    737,    178,   -537,  -1549,  -2861,  -4218,  -5080,  -4981,  -3887,  -2070,    165,   2478,   4290,
	  4900,   3997,   1992,   -235,  -1933,  -2848,  -3156,  -3120,  -2841,  -2314,  -1737,  -1572,  -2131,  -3219,  -4309,  -4932,
	 -4937,  -4517,  -4007,  -3677,  -3666,  -3991,  -4496,  -4849,  -4689,  -3800,  -2229,   -346,   1323,   2417,   2945,   3270,
	  3817,   4637,   5371,   5634,   5337,   4658,   3817,   2953,   2150,   1520,   1233,   1295,   1324,    859,   -127,  -1235,
	 -2049,  -2373,  -2274,  -2035,  -1896,  -1878,  -1896,  -1989,  -2246,  -2449,  -2177,  -1327,   -239,    740,   1599,   2489,
	  3454,   4405,   5084,   5063,   4130,   2645,   1377,   1000,   1664,   2941,   4169,   4852,   4828,   4125,   2874,   1354,
	  -130,  -1482,  -2684,  -3503,  -3618,  -2973,  -1859,   -700,    147,    438,    178,   -230,   -296,    133,    900,   1726,
	  2355,   2629,   2465,   1897,   1013,   -199,  -1668,  -2961,  -3475,  -2900,  -1474,     70,    913,    824,    342,     78,
	   173,    526,    969,   1225,   1088,    643,    111,   -475,  -1263,  -2218,  -3082,  -3717,  -4146,  -4283,  -3987,  -3389,
	 -2802,  -2295,  -1736,  -1191,   -981,  -1330,  -2025,  -2494,  -2365,  -1823,  -1274,   -953,   -980,  -1390,  -1951,  -2167,
	 -1590,   -192,   1587,   3271,   4720,   6002,   7020,   7378,   6711,   5196,   3536,   2417,   2159,   2755,   4016,   5687,
	  7306,   8111,   7591,   5853,   3309,    529,  -1694,  -2746,  -2742,  -2348,  -2091,  -2036,  -2002,  -1932,  -1995,  -2376,
	 -3028,  -3670,  -3928,  -3535,  -2553,  -1367,   -378,    261,    605,    789,    984,   1326,   1761,   2003,   1808,   1265,
	   602,   -144,  -1103,  -2350,  -3726,  -4951,  -5872,  -6421,  -6482,  -6034,  -5223,  -4225,  -3166,  -2107,  -1004,    204,
	  1407,   2321,   2768,   2833,   2639,   2119,   1180,    -62,  -1262,  -1916,  -1630,   -455,   1118,   2533,   3437,   3610,
	  3012,   2003,   1125,    625,    392,    204,   -182,   -871,  -1624,  -2082,  -2115,  -1728,   -935,     95,   1032,   1648,
	  1979,   2085,   1841,   1157,    140,   -948,  -1730,  -1933,  -1715,  -1446,  -1162,   -618,    249,   1212,   1913,   2044,
	  1632,   1103,    819,    585,   -149,  -1606,  -3444,  -5029,  -5702,  -5189,  -3980,  -2917,  -2387,  -2214,  -2159,  -2134,
	 -2059,  -1857,  -1517,  -1084,   -632,   -250,     31,    257,    548,   1107,   2056,   3238,   4355,   5159,   5396,   4983,
	  4293,   3786,   3375,   2606,   1353,     -5,  -1017,  -1551,  -1787,  -1915,  -1932,  -1757,  -1461,  -1251,  -1163,   -944,
	  -400,    367,   1140,   1686,   1788,   1368,    580,   -270,   -999,  -1513,  -1592,  -1077,    -87,   1107,   2289,   3313,
	  3971,   4007,   3421,   2616,   2111,   2094,   2292,   2329,   2043,   1425,    495,   -680,  -1942,  -3047,  -3709,  -3750,
	 -3222,  -2406,  -1768,  -1726,  -2304,  -3164,  -3936,  -4433,  -4656,  -4641,  -4320,  -3638,  -2727,  -1856,  -1135,   -384,
	   552,   1453,   1957,   2033,   1994,   2122,   2475,   2953,   3324,   3330,   2915,   2230,   1373,    411,   -362,   -524,
	    47,   1041,   2015,   2661,   2843,   2548,   1975,   1534,   1482,   1639,   1721,   1731,   1787,   1854,   1745,   1251,
	   394,   -527,  -1233,  -1676,  -1923,  -1936,  -1596,   -968,   -306,    231,    595,    712,    596,    436,    323,    130,
	  -143,   -138,    554,   1850,   3022,   3252,   2282,    531,  -1217,  -2238,  -2266,  -1674,  -1111,   -873,   -845,   -871,
	  -891,   -927,  -1082,  -1398,  -1745,  -1957,  -1907,  -1510,   -886,   -330,     -9,     78,    -97,   -493,   -820,   -756,
	  -345,    109,    441,    653,    701,    470,    -93,   -889,  -1663,  -2113,  -2080,  -1678,  -1174,   -760,   -413,     87,
	  1051,   2622,   4466,   5879,   6315,   5801,   4837,   3932,   3325,   2997,   2738,   2288,   1609,   1060,   1092,   1613,
	  1988,   1760,   1000,    -20,  -1097,  -2126,  -3001,  -3510,  -3503,  -3167,  -2825,  -2569,  -2364,  -2379,  -2908,  -3934,
	 -4928,  -5323,  -5067,  -4483,  -3841,  -3312,  -3033,  -3018,  -3177,  -3420,  -3629,  -3562,  -2954,  -1793,   -416,    743,
	  1532,   2159,   2838,   3511,   3966,   4102,   3971,   3621,   2984,   1949,    571,   -715,  -1300,  -1046,   -478,   -147,
	  -135,   -252,   -372,   -452,   -352,    142,   1158,   2602,   4094,   5128,   5369,   4683,   3060,    796,  -1415,  -2924,
	 -3586,  -3581,  -3039,  -2073,   -941,     13,    549,    663,    623,    769,   1229,   1792,   2030,   1654,    835,     54,
	  -443,   -858,  -1456,  -2087,  -2149,  -1038,   1180,   3631,   5323,   5801,   5124,   3556,   1442,   -787,  -2552,  -3213,
	 -2511,   -845,   1102,   2781,   3728,   3517,   2104,     86,  -1589,  -2287,  -2051,  -1268,   -374,    127,    -94,   -859,
	 -1634,  -2008,  -1927,  -1554,  -1072,   -582,   -186,   -141,   -654,  -1606,  -2680,  -3567,  -4123,  -4446,  -4594,  -4392,
	 -3680,  -2585,  -1481,   -799,   -807,  -1437,  -2325,  -3058,  -3377,  -3269,  -2945,  -2575,  -2080,  -1357,   -524,    211,
	   749,   1091,   1234,   1195,   1091,   1000,    932,   1051,   1594,   2505,   3500,   4388,   5093,   5450,   5195,   4130,
	  2319,    217,  -1531,  -2605,  -3144,  -3298,  -2945,  -1926,   -335,   1540,   3434,   5217,   6815,   7971,   8233,   7389,
	  5725,   3806,   2168,   1123,    628,    334,    -95,   -692,  -1193,  -1300,   -985,   -510,   -164,     -6,    135,    399,
	   761,   1195,   1736,   2242,   2344,   1737,    406,  -1419,  -3356,  -4864,  -5306,  -4371,  -2571,  -1033,   -495,   -785,
	 -1390,  -2080,  -2810,  -3477,  -3931,  -3967,  -3421,  -2365,  -1051,    328,   1599,   2455,   2626,   2256,   1795,   1359,
	   579,   -768,  -2324,  -3398,  -3568,  -3062,  -2553,  -2662,  -3607,  -4940,  -5653,  -4966,  -3026,   -710,   1185,   2425,
	  3175,   3573,   3535,   2908,   1772,    504,   -416,   -699,   -462,    -95,    135,    196,    120,    -78,   -394,   -802,
	 -1242,  -1632,  -1869,  -1864,  -1526,   -769,    344,   1564,   2606,   3256,   3413,   3124,   2591,   2073,   1706,   1428,
	  1059,    516,    -19,   -285,   -248,    -19,    434,   1192,   2109,   2805,   2847,   2080,    841,   -300,   -984,  -1226,
	 -1205,  -1028,   -651,     43,   1036,   2048,   2691,   2729,   2231,   1484,    754,    164,   -248,   -487,   -634,   -860,
	 -1292,  -1760,  -1908,  -1570,   -826,    157,   1087,   1518,   1240,    566,     50,      6,    307,    633,    888,   1261,
	  1924,   2837,   3786,   4402,   4316,   3414,   1865,    108,  -1257,  -1832,  -1587,   -760,    262,    984,    944,     81,
	 -1135,  -2159,  -2729,  -2765,  -2313,  -1624,  -1055,   -864,  -1046,  -1376,  -1678,  -1932,  -2105,  -2059,  -1641,   -900,
	  -174,    153,    -36,   -514,   -975,  -1346,  -1665,  -1862,  -1935,  -2157,  -2738,  -3342,  -3250,  -2083,   -228,   1479,
	  2376,   2353,   1795,   1207,    923,   1055,   1470,   1872,   2075,   2116,   2054,   1766,   1095,    153,   -712,  -1247,
	 -1531,  -1825,  -2209,  -2398,  -2019,  -1137,   -282,    128,     62,   -413,  -1310,  -2577,  -3966,  -5167,  -6039,  -6521,
	 -6418,  -5583,  -4240,  -2910,  -2062,  -1818,  -1918,  -1925,  -1548,   -798,    169,   1165,   1908,   2188,   2070,   1747,
	  1274,    560,   -325,   -973,   -983,   -376,    433,   1009,   1262,   1429,   1668,   1857,   1835,   1688,   1657,   1773,
	  1773,   1420,    839,    544,   1005,   2132,   3322,   3984,   3916,   3341,   2739,   2483,   2465,   2248,   1672,   1146,
	  1191,   1763,   2297,   2447,   2374,   2288,   2099,   1538,    511,   -703,  -1685,  -2208,  -2195,  -1590,   -382,   1270,
	  2943,   4068,   4317,   3806,   2767,   1348,   -108,  -1083,  -1354,  -1170,   -906,   -651,   -312,     27,    135,     -6,
	   -45,    395,   1261,   2125,   2570,   2411,   1702,    691,   -276,   -981,  -1414,  -1590,  -1439,   -882,    -43,    713,
	  1056,    890,    378,   -100,   -223,      1,    322,    497,    409,     15,   -604,  -1180,  -1437,  -1346,  -1191,  -1396,
	 -2173,  -3300,  -4217,  -4435,  -3854,  -2655,  -1198,    -35,    448,    360,     14,   -404,   -772,  -1020,  -1119,  -1006,
	  -762,   -768,  -1310,  -2196,  -2909,  -3041,  -2555,  -1733,  -1037,   -887,  -1268,  -1667,  -1558,   -881,     11,    810,
	  1566,   2478,   3501,   4323,   4729,   4784,   4674,   4565,   4574,   4676,   4638,   4222,   3503,   2761,   2094,   1327,
	   352,   -643,  -1408,  -1982,  -2588,  -3236,  -3663,  -3695,  -3470,  -3210,  -2955,  -2597,  -2017,  -1340,  -1143,  -1977,
	 -3577,  -4909,  -5086,  -4066,  -2574,  -1487,  -1176,  -1299,  -1277,   -935,   -559,   -460,   -633,   -806,   -781,   -634,
	  -525,   -438,   -103,    808,   2270,   3727,   4528,   4452,   3707,   2643,   1599,    948,   1037,   1886,   3043,   3977,
	  4544,   4892,   5014,   4681,   3846,   2889,   2317,   2284,   2557,   2797,   2669,   1929,    681,   -506,  -1069,  -1086,
	 -1109,  -1438,  -1961,  -2560,  -3240,  -3876,  -4089,  -3509,  -2232,   -865,     66,    501,    607,    566,    587,    752,
	   890,    746,    176,   -740,  -1696,  -2391,  -2744,  -2793,  -2552,  -2187,  -2033,  -2194,  -2424,  -2478,  -2323,  -2034,
	 -1731,  -1480,  -1188,   -771,   -260,    377,   1131,   1718,   1776,   1186,    143,   -873,  -1180,   -305,   1454,   3075,
	  3595,   2828,   1409,    149,   -567,   -808,   -803,   -727,   -597,   -317,    125,    643,   1163,   1582,   1825,   2010,
	  2271,   2587,   2922,   3248,   3403,   3177,   2551,   1728,    959,    390,    -45,   -544,  -1230,  -2032,  -2803,  -3392,
	 -3567,  -3076,  -1839,     -4,   2004,   3502,   3928,   3369,   2429,   1449,    223,  -1552,  -3832,  -6171,  -8016,  -8945,
	 -8675,  -7075,  -4296,   -904,   2165,   4038,   4450,   3790,   2597,   1145,   -401,  -1732,  -2530,  -2671,  -2149,  -1053,
	   222,   1095,   1329,   1250,   1334,   1735,   2219,   2441,   2289,   1924,   1567,   1337,   1259,   1288,   1316,   1162,
	   742,    305,    152,    295,    780,   1881,   3634,   5494,   6540,   6086,   4247,   1834,   -222,  -1299,  -1316,   -738,
	  -295,   -509,  -1438,  -2770,  -3992,  -4629,  -4583,  -4178,  -3863,  -3938,  -4444,  -5156,  -5688,  -5704,  -5095,  -3958,
	 -2499,  -1003,    294,   1321,   2085,   2629,   3038,   3311,   3342,   3045,   2398,   1496,    582,    -45,   -153,    387,
	  1543,   3037,   4483,   5625,   6320,   6386,   5645,   4139,   2152,     -5,  -2121,  -4025,  -5592,  -6714,  -7161,  -6669,
	 -5196,  -3058,   -893,    635,   1287,   1295,   1021,    649,    142,   -552,  -1391,  -2304,  -3134,  -3590,  -3446,  -2737,
	 -1655,   -317,   1122,   2181,   2395,   1947,   1545,   1680,   2270,   2923,   3274,   3084,   2365,   1508,   1033,   1093,
	  1345,   1298,    692,   -429,  -1863,  -3309,  -4339,  -4657,  -4399,  -3925,  -3435,  -2956,  -2468,  -1863,   -962,    312,
	  1797,   3124,   3928,   4078,   3636,   2760,   1602,    269,  -1058,  -2131,  -2814,  -3000,  -2513,  -1384,     42,   1478,
	  2766,   3644,   3930,   3734,   3187,   2224,    794,   -929,  -2568,  -3610,  -3563,  -2347,   -441,   1491,   2901,   3386,
	  2838,   1540,    -49,  -1551,  -2733,  -3388,  -3308,  -2479,  -1225,      5,    866,   1152,    960,    791,   1212,   2360,
	  3727,   4491,   4262,   3451,   2815,   2827,   3504,   4530,   5359,   5417,   4475,   2874,   1281,    152,   -482,   -781,
	  -976,  -1351,  -1941,  -2350,  -2120,  -1339,   -643,   -481,   -708,  -1007,  -1232,  -1306,  -1175,   -891,   -527,    -81,
	   391,    684,    732,    771,   1024,   1410,   1647,   1526,   1078,    550,    184,    -55,   -448,  -1235,  -2388,  -3639,
	 -4680,  -5245,  -5208,  -4680,  -3954,  -3313,  -2841,  -2424,  -1919,  -1187,    -67,   1395,   2771,   3542,   3482,   2665,
	  1374,    175,   -302,     92,    933,   1676,   2022,   1973,   1588,    776,   -571,  -2243,  -3637,  -4086,  -3373,  -2054,
	 -1079,   -918,  -1223,  -1390,  -1171,   -698,   -232,     31,     60,    -79,   -324,   -626,   -990,  -1514,  -2271,  -3115,
	 -3627,  -3419,  -2513,  -1307,   -284,    263,    416,    532,    897,   1513,   2146,   2480,   2262,   1448,    260,   -857,
	 -1392,  -1130,   -368,    311,    504,    298,     86,     93,    181,    141,     -2,    -37,    261,   1000,   2081,   3134,
	  3620,   3129,   1686,   -261,  -2101,  -3315,  -3676,  -3350,  -2748,  -2126,  -1338,   -157,   1247,   2388,   2953,   2988,
	  2639,   2004,   1244,    576,    155,     86,    401,    930,   1402,   1688,   1840,   1923,   1873,   1494,    624,   -617,
	 -1775,  -2361,  -2230,  -1632,   -963,   -552,   -542,   -832,  -1148,  -1253,  -1133,   -956,   -773,   -349,    607,   2050,
	  3457,   4086,   3567,   2273,    910,   -133,   -838,  -1318,  -1710,  -2093,  -2456,  -2777,  -3081,  -3385,  -3661,  -3864,
	 -3990,  -4092,  -4151,  -4032,  -3614,  -2827,  -1700,   -485,    478,   1092,   1552,   2130,   2951,   3870,   4518,   4614,
	  4237,   3709,   3200,   2615,   1987,   1692,   1966,   2509,   2804,   2697,   2546,   2743,   3165,   3294,   2787,   1745,
	   475,   -868,  -2259,  -3588,  -4701,  -5483,  -5728,  -5140,  -3628,  -1523,    606,   2362,   3613,   4235,   4014,   2961,
	  1538,    369,    -99,    221,    935,   1395,   1149,    209,   -893,  -1487,  -1444,  -1218,  -1216,  -1500,  -2007,  -2648,
	 -3105,  -2896,  -1845,   -340,   1004,   1887,   2360,   2447,   2064,   1262,    312,   -398,   -520,     20,    951,   1857,
	  2355,   2186,   1365,    181,  -1069,  -2175,  -2821,  -2719,  -2013,  -1239,   -794,   -684,   -796,  -1113,  -1602,  -2079,
	 -2310,  -2293,  -2375,  -2899,  -3760,  -4441,  -4367,  -3241,  -1296,    722,   2009,   2230,   1584,    530,   -392,   -627,
	    93,   1421,   2697,   3617,   4339,   5024,   5536,   5624,   5242,   4577,   3833,   3091,   2397,   1894,   1731,   1823,
	  1845,   1628,   1448,   1697,   2355,   2956,   3044,   2564,   1794,    949,     -1,  -1098,  -2169,  -3009,  -3586,  -3904,
	 -3879,  -3476,  -2968,  -2932,  -3730,  -5015,  -5952,  -5889,  -4815,  -3249,  -1771,   -722,   -115,    324,    846,   1404,
	  1840,   2098,   2174,   2096,   1859,   1293,    228,  -1344,  -3337,  -5483,  -7151,  -7661,  -6857,  -5155,  -3131,  -1155,
	   643,   2009,   2491,   2031,   1231,    734,    610,    558,    486,    600,   1082,   1929,   2920,   3652,   3810,   3418,
	  2808,   2409,   2433,   2692,   2754,   2242,   1131,    -76,   -607,    -96,   1086,   2238,   2736,   2194,    754,   -908,
	 -2166,  -2825,  -2990,  -2963,  -3177,  -3756,  -4231,  -3909,  -2512,   -508,   1235,   2074,   1963,   1471,   1366,   1982,
	  3025,   3866,   3942,   3037,   1321,   -697,  -2286,  -2896,  -2515,  -1530,   -404,    465,    801,    530,   -211,  -1138,
	 -1969,  -2618,  -3145,  -3509,  -3489,  -2924,  -1903,   -695,    411,   1276,   1901,   2277,   2350,   2122,   1700,   1231,
	   886,    769,    751,    655,    558,    637,    811,    758,    296,   -293,   -518,   -263,    120,    218,   -107,   -755,
	 -1511,  -1983,  -1734,   -739,    587,   1859,   2909,   3566,   3623,   3084,   2266,   1601,   1392,   1745,   2600,   3650,
	  4388,   4444,   3823,   2819,   1805,   1136,   1062,   1510,   2031,   2178,   1826,   1082,    128,   -802,  -1447,  -1789,
	 -2185,  -2956,  -3979,  -4868,  -5385,  -5550,  -5432,  -4958,  -4013,  -2733,  -1570,   -928,   -681,   -391,     33,    267,
	    41,   -595,  -1480,  -2558,  -3659,  -4374,  -4406,  -3804,  -2894,  -2112,  -1748,  -1674,  -1430,   -661,    562,   1841,
	  2764,   3243,   3596,   4190,   5023,   5652,   5529,   4414,   2654,   1108,    488,    677,    951,    835,    430,     12,
	  -272,   -447,   -644,   -910,  -1087,   -939,   -409,    192,    434,    216,   -289,   -832,  -1057,   -730,    -57,    481,
	   456,   -340,  -1761,  -3327,  -4554,  -5236,  -5389,  -4997,  -4027,  -2626,  -1121,    170,   1097,   1699,   2156,   2656,
	  3205,   3601,   3643,   3257,   2522,   1725,   1291,   1448,   2021,   2609,   2841,   2475,   1490,    189,   -837,  -1024,
	  -290,    903,   1931,   2364,   2113,   1421,    692,    249,    160,    254,    330,    385,    489,    513,    299,     16,
	    19,    391,    807,    815,    179,   -972,  -2306,  -3435,  -3867,  -3111,  -1178,   1161,   2945,   3647,   3165,   1690,
	  -261,  -2021,  -3126,  -3473,  -3227,  -2646,  -2011,  -1626,  -1778,  -2581,  -3808,  -4963,  -5508,  -5182,  -4265,  -3364,
	 -2885,  -2723,  -2485,  -1989,  -1359,   -702,    -32,    597,   1115,   1504,   1791,   2010,   2166,   2322,   2622,   3027,
	  3238,   3078,   2721,   2416,   2218,   2101,   2108,   2240,   2314,   2022,   1170,     38,   -677,   -560,     32,    389,
	    95,   -726,  -1452,  -1363,   -295,   1175,   2290,   2691,   2451,   1849,   1229,    802,    555,    500,    733,   1196,
	  1680,   2062,   2305,   2255,   1641,    393,  -1109,  -2244,  -2627,  -2379,  -1910,  -1575,  -1514,  -1593,  -1547,  -1212,
	  -601,    104,    605,    579,    -72,  -1007,  -1642,  -1601,   -900,    105,    935,   1257,   1045,    555,     92,   -259,
	  -643,  -1117,  -1451,  -1455,  -1273,  -1173,  -1258,  -1540,  -1995,  -2424,  -2553,  -2385,  -2216,  -2299,  -2661,  -3058,
	 -3093,  -2562,  -1625,   -580,    400,   1219,   1807,   2271,   2809,   3364,   3637,   3466,   3009,   2555,   2311,   2336,
	  2534,   2625,   2274,   1462,    601,    129,    194,    720,   1420,   1889,   1868,   1348,    596,     50,    -82,    -19,
	   -19,    -44,     92,    439,    911,   1362,   1552,   1130,   -234,  -2529,  -5124,  -6968,  -7304,  -6217,  -4467,  -2782,
	 -1357,    -43,   1088,   1691,   1658,   1321,   1080,   1053,   1079,    889,    382,   -163,   -355,   -121,    289,    510,
	   208,   -625,  -1510,  -1809,  -1138,    459,   2453,   3967,   4323,   3552,   2204,    764,   -480,  -1238,  -1291,   -771,
	  -157,    202,    390,    641,    977,   1236,   1216,    865,    434,    177,    -27,   -432,  -1033,  -1624,  -2024,  -2129,
	 -1893,  -1391,   -956,  -1021,  -1664,  -2570,  -3354,  -3718,  -3540,  -2962,  -2215,  -1437,   -679,     33,    588,    783,
	   503,   -126,   -772,  -1124,  -1040,   -480,    489,   1568,   2418,   3000,   3464,   3783,   3717,   3100,   2005,    680,
	  -480,  -1062,   -891,   -165,    744,   1594,   2331,   2990,   3649,   4357,   5044,   5486,   5387,   4602,   3353,   2176,
	  1540,   1530,   1951,   2539,   2912,   2609,   1403,   -433,  -2241,  -3403,  -3680,  -3282,  -2768,  -2724,  -3299,  -4220,
	 -5181,  -5909,  -6082,  -5565,  -4525,  -3235,  -2019,  -1232,  -1056,  -1425,  -2157,  -3030,  -3811,  -4333,  -4397,  -3711,
	 -2306,   -784,    237,    667,    747,    732,    814,   1047,   1360,   1663,   1818,   1667,   1175,    497,     10,    134,
	   897,   1907,   2761,   3228,   3185,   2652,   1772,    695,   -368,  -1086,  -1257,  -1027,   -744,   -751,  -1228,  -1978,
	 -2487,  -2312,  -1400,   -211,    607,    859,    851,    832,    757,    512,    144,   -112,      0,    602,   1602,   2655,
	  3406,   3803,   3955,   3866,   3457,   2745,   1919,   1192,    565,   -144,  -1005,  -1859,  -2396,  -2239,  -1197,    467,
	  2144,   3306,   3787,   3663,   3064,   2187,   1287,    539,      3,   -276,   -230,    200,    904,   1474,   1432,    712,
	  -215,   -857,  -1134,  -1228,  -1376,  -1719,  -2083,  -2187,  -2117,  -2278,  -2912,  -3758,  -4172,  -3622,  -2155,   -434,
	   707,    899,    455,    -12,    -92,    193,    568,    886,   1215,   1600,   1956,   2072,   1599,    363,  -1218,  -2409,
	 -2846,  -2682,  -2180,  -1403,   -348,    725,   1351,   1341,    990,    710,    626,    587,    483,    389,    393,    408,
	   285,    -16,   -408,   -719,   -756,   -518,   -324,   -487,   -964,  -1484,  -1768,  -1619,   -965,     85,   1137,   1787,
	  2089,   2407,   2831,   2997,   2483,   1276,   -213,  -1480,  -2185,  -2332,  -2254,  -2260,  -2311,  -2112,  -1413,   -276,
	   803,   1202,    757,    -90,   -798,  -1127,  -1053,   -568,    288,   1386,   2569,   3628,   4232,   4081,   3178,   1814,
	   321,  -1086,  -2227,  -2797,  -2507,  -1426,    -61,    953,   1264,    960,    454,    208,    446,   1140,   2068,   2828,
	  3036,   2574,   1606,    439,   -734,  -1955,  -3250,  -4384,  -4930,  -4488,  -2931,   -574,   1908,   3747,   4467,   4147,
	  3328,   2592,   2173,   1974,   1765,   1326,    651,     75,    -57,    151,    122,   -629,  -1936,  -3125,  -3639,  -3439,
	 -2877,  -2347,  -2017,  -1764,  -1436,  -1114,   -922,   -823,   -744,   -690,   -739,  -1020,  -1599,  -2370,  -3043,  -3220,
	 -2642,  -1349,    396,   2155,   3388,   3756,   3372,   2685,   2166,   1994,   2047,   2119,   1939,   1219,    -37,  -1427,
	 -2346,  -2313,  -1311,    225,   1896,   3512,   4802,   5391,   5055,   3885,   2331,    939,   -103,   -965,  -1717,  -2166,
	 -2215,  -2056,  -1911,  -1887,  -2113,  -2660,  -3367,  -3972,  -4286,  -4216,  -3745,  -2888,  -1726,   -582,    100,    198,
	    31,    -66,   -116,   -283,   -468,   -404,     20,    742,   1623,   2515,   3279,   3737,   3704,   3086,   1874,    175,
	 -1696,  -3292,  -4155,  -3959,  -2755,   -999,    669,   1681,   1865,   1555,   1208,   1066,   1157,   1367,   1458,   1121,
	   156,  -1178,  -2184,  -2357,  -1792,   -932,   -192,    246,    421,    395,    228,     59,    -28,    -78,   -145,   -317,
	  -711,  -1257,  -1613,  -1392,   -500,    783,   2085,   3151,   3912,   4378,   4403,   3777,   2533,    875,   -982,  -2725,
	 -3926,  -4291,  -3920,  -3240,  -2681,  -2404,  -2220,  -1815,  -1109,   -378,    -11,    -74,   -210,    -19,    578,   1281,
	  1628,   1395,    801,    344,    463,   1141,   1858,   2121,   1886,   1438,   1143,   1301,   1970,   2819,   3203,   2529,
	   780,  -1404,  -3339,  -4763,  -5644,  -5998,  -5953,  -5643,  -5004,  -3929,  -2509,  -1013,    254,   1059,   1378,   1472,
	  1664,   1966,   2110,   1993,   1822,   1731,   1539,   1014,    306,    -35,    368,   1095,   1239,    459,   -616,  -1192,
	 -1134,   -805,   -533,   -515,   -841,  -1370,  -1735,  -1588,   -868,    197,   1324,   2333,   3113,   3506,   3369,   2775,
	  1994,   1254,    721,    677,   1376,   2704,   4185,   5269,   5628,   5221,   4100,   2342,    312,  -1324,  -2109,  -2141,
	 -1752,  -1068,   -142,    744,   1186,    934,     47,  -1105,  -1973,  -2144,  -1625,   -791,    -60,    375,    498,    280,
	  -277,  -1088,  -2040,  -2980,  -3720,  -4125,  -4137,  -3732,  -2912,  -1773,   -594,    220,    275,   -576,  -1980,  -3211,
	 -3723,  -3484,  -2741,  -1638,   -232,   1276,   2573,   3536,   4321,   5013,   5358,   4964,   3686,   1736,   -510,  -2626,
	 -4104,  -4568,  -4083,  -3126,  -2317,  -2065,  -2261,  -2465,  -2341,  -1857,  -1206,   -564,    101,    853,   1537,   1957,
	  2170,   2274,   2199,   2002,   2006,   2413,   3023,   3366,   2987,   1725,   -136,  -1980,  -3247,  -3775,  -3769,  -3473,
	 -3023,  -2605,  -2493,  -2786,  -3234,  -3406,  -3033,  -2245,  -1421,   -716,     77,   1117,   2228,   3216,   4157,   5100,
	  5754,   5808,   5296,   4495,   3704,   3144,   2902,   2970,   3278,   3553,   3413,   2681,   1494,     82,  -1467,  -3093,
	 -4459,  -5011,  -4390,  -2758,   -776,    732,   1172,    444,   -996,  -2358,  -3079,  -3220,  -3317,  -3723,  -4162,  -4130,
	 -3448,  -2158,   -374,   1589,   3299,   4428,   4841,   4661,   4153,   3461,   2677,   2136,   2193,   2726,   3160,   2929,
	  1794,   -109,  -2257,  -3866,  -4397,  -3936,  -2939,  -1695,   -138,   1841,   3927,   5491,   6154,   5933,   5024,   3763,
	  2600,   1728,    856,   -346,  -1677,  -2573,  -2744,  -2433,  -2115,  -1985,  -1857,  -1505,   -888,   -159,    328,    211,
	  -603,  -1946,  -3475,  -4698,  -5185,  -4825,  -3816,  -2592,  -1688,  -1429,  -1774,  -2458,  -3133,  -3527,  -3660,  -3704,
	 -3673,  -3502,  -3312,  -3290,  -3469,  -3741,  -3896,  -3617,  -2599,   -754,   1631,   3855,   5050,   4804,   3504,   1974,
	   964,    916,   1791,   3097,   4306,   5155,   5483,   5182,   4401,   3488,   2769,   2367,   2120,   1778,   1296,    708,
	  -177,  -1596,  -3346,  -4754,  -5168,  -4361,  -2588,   -427,   1539,   2976,   3680,   3406,   2156,    491,   -831,  -1465,
	 -1511,  -1137,   -470,    302,    996,   1571,   2103,   2621,   3041,   3251,   3253,   3289,   3693,   4433,   5102,   5364,
	  5138,   4464,   3550,   2807,   2399,   1980,   1074,   -534,  -2698,  -4856,  -6221,  -6288,  -5186,  -3522,  -2036,  -1350,
	 -1788,  -3201,  -4981,  -6391,  -6904,  -6357,  -5026,  -3565,  -2557,  -2125,  -2110,  -2344,  -2591,  -2515,  -1872,   -715,
	   613,   1773,   2718,   3545,   4101,   3980,   3010,   1541,    146,   -788,  -1173,  -1083,   -616,     44,    602,    834,
	   839,   1010,   1483,   1899,   1948,   1733,   1491,   1287,   1064,    855,    770,    801,    772,    537,    130,   -283,
	  -511,   -419,     67,    965,   2114,   3081,   3504,   3468,   3279,   2967,   2293,   1182,    -44,   -849,   -865,   -148,
	   853,   1526,   1447,    796,    253,    358,   1087,   1944,   2319,   1860,    759,   -316,   -697,   -272,    386,    501,
	  -266,  -1542,  -2570,  -2733,  -1855,   -310,   1100,   1717,   1540,   1067,    726,    532,    186,   -594,  -1781,  -3013,
	 -3837,  -4068,  -3965,  -3978,  -4229,  -4281,  -3603,  -2330,  -1275,  -1127,  -1869,  -2902,  -3467,  -3081,  -1824,   -268,
	   980,   1721,   2113,   2276,   2138,   1634,    866,     13,   -703,   -978,   -670,   -133,    -23,   -582,  -1418,  -2092,
	 -2517,  -2792,  -2999,  -3146,  -3159,  -2891,  -2201,  -1164,   -139,    494,    587,    224,   -297,   -468,    105,   1303,
	  2532,   3184,   3157,   2862,   2658,   2568,   2484,   2371,   2277,   2146,   1774,   1147,    553,    195,      5,   -107,
	     9,    644,   1759,   2764,   2980,   2122,    450,  -1235,  -2026,  -1664,   -585,    670,   1719,   2321,   2427,   2152,
	  1592,    799,    -89,   -842,  -1142,   -737,    184,   1075,   1482,   1168,    158,  -1158,  -2234,  -2716,  -2616,  -2266,
	 -2131,  -2535,  -3389,  -4111,  -4067,  -3213,  -2161,  -1587,  -1601,  -1754,  -1600,  -1093,   -438,    269,   1095,   2003,
	  2768,   3156,   3089,   2705,   2316,   2294,   2773,   3378,   3509,   2943,   1987,   1133,    704,    684,    749,    476,
	  -372,  -1677,  -3024,  -3944,  -4186,  -3837,  -3263,  -2916,  -2996,  -3225,  -3061,  -2150,   -560,   1284,   2834,   3702,
	  3893,   3742,   3454,   2859,   1890,   1027,    887,   1566,   2581,   3334,   3565,   3415,   3061,   2464,   1586,    608,
	  -192,   -674,   -887,   -946,   -986,  -1132,  -1337,  -1372,  -1095,   -586,    -21,    429,    633,    520,     88,   -613,
	 -1533,  -2628,  -3724,  -4484,  -4575,  -3879,  -2611,  -1240,   -310,   -138,   -542,   -998,  -1148,  -1051,   -950,   -998,
	 -1241,  -1667,  -2144,  -2319,  -1765,   -393,   1421,   3115,   4141,   4090,   3013,   1529,    365,   -182,   -289,   -320,
	  -595,  -1159,  -1617,  -1464,   -577,    755,   2057,   2886,   3163,   3194,   3215,   3069,   2425,   1203,   -309,  -1626,
	 -2325,  -2322,  -1927,  -1520,  -1144,   -539,    499,   1907,   3358,   4403,   4690,   4134,   3011,   1907,   1349,   1405,
	  1804,   2319,   2824,   3117,   2862,   1865,    416,   -908,  -1715,  -1891,  -1533,   -946,   -490,   -430,   -810,  -1293,
	 -1424,  -1184,  -1006,  -1232,  -1810,  -2539,  -3318,  -4060,  -4631,  -4883,  -4739,  -4288,  -3824,  -3631,  -3618,  -3322,
	 -2365,   -730,   1292,   3266,   4750,   5456,   5337,   4419,   2720,    525,  -1599,  -3141,  -3776,  -3357,  -2151,   -943,
	  -463,   -846,  -1806,  -2926,  -3734,  -3991,  -3856,  -3487,  -2693,  -1206,    840,   2909,   4530,   5577,   6001,   5626,
	  4429,   2878,   1703,   1268,   1335,   1472,   1556,   1752,   2109,   2369,   2182,   1531,    892,    758,   1090,   1381,
	  1225,    695,    154,    -63,    260,   1125,   2123,   2616,   2277,   1254,   -152,  -1536,  -2362,  -2326,  -1532,   -182,
	  1487,   2990,   3715,   3396,   2427,   1596,   1319,   1339,   1213,    737,    -54,   -976,  -1734,  -2093,  -2062,  -1821,
	 -1483,   -996,   -311,    392,    757,    501,   -355,  -1508,  -2649,  -3585,  -4107,  -4054,  -3656,  -3476,  -3730,  -3902,
	 -3315,  -1961,   -564,     98,   -135,   -747,  -1169,  -1326,  -1537,  -2025,  -2609,  -2818,  -2322,  -1296,   -286,    372,
	   817,   1321,   1996,   2791,   3523,   4080,   4507,   4655,   4080,   2615,    736,   -764,  -1394,  -1322,  -1019,   -742,
	  -593,   -674,   -900,   -980,   -731,   -234,    265,    497,    365,     38,   -187,    -74,    377,    852,   1003,    863,
	   748,    828,   1015,   1161,   1214,   1232,   1224,    968,    169,  -1149,  -2500,  -3296,  -3270,  -2575,  -1613,   -739,
	  -131,    109,     31,    -41,     84,     90,   -445,  -1407,  -2199,  -2370,  -1934,  -1178,   -407,    189,    461,    238,
	  -398,   -969,  -1088,   -820,   -349,    303,   1080,   1750,   2111,   2226,   2349,   2618,   2896,   3034,   3215,   3769,
	  4636,   5352,   5499,   5019,   4120,   2910,   1331,   -528,  -2386,  -3915,  -4764,  -4696,  -3828,  -2575,  -1363,   -437,
	   137,    373,    261,   -163,   -685,  -1005,   -952,   -631,   -338,   -290,   -495,   -809,   -950,   -704,   -176,    303,
	   461,    185,   -524,  -1503,  -2479,  -3258,  -3733,  -3779,  -3419,  -2980,  -2877,  -3248,  -3948,  -4762,  -5403,  -5449,
	 -4609,  -3072,  -1472,   -480,   -322,   -594,   -648,   -147,    770,   1689,   2268,   2450,   2356,   2105,   1783,   1479,
	  1316,   1525,   2306,   3413,   4260,   4598,   4754,   5186,   5968,   6718,   6954,   6463,   5339,   3845,   2365,   1332,
	   962,   1080,   1253,   1162,    913,    839,    955,    875,    273,   -770,  -1788,  -2266,  -2071,  -1559,  -1242,  -1415,
	 -2045,  -2744,  -3038,  -2921,  -2839,  -3053,  -3401,  -3680,  -3891,  -4111,  -4333,  -4527,  -4721,  -4877,  -4792,  -4321,
	 -3604,  -2853,  -2046,  -1046,     54,    906,   1305,   1422,   1559,   1800,   1917,   1666,   1211,   1009,   1290,   1875,
	  2467,   2919,   3240,   3504,   3758,   3993,   4170,   4169,   3797,   2993,   2017,   1331,   1205,   1495,   1912,   2345,
	  2797,   3128,   3017,   2283,   1164,    103,   -637,  -1097,  -1393,  -1503,  -1350,   -938,   -355,    183,    300,   -260,
	 -1371,  -2668,  -3808,  -4506,  -4650,  -4531,  -4614,  -4967,  -5210,  -4961,  -4119,  -2887,  -1685,   -893,   -614,   -628,
	  -579,   -302,     64,    311,    383,    368,    395,    493,    566,    548,    432,    140,   -383,   -962,  -1200,   -767,
	   197,   1071,   1264,    682,   -256,   -930,   -857,     27,   1372,   2703,   3678,   4095,   3858,   3046,   1953,    978,
	   478,    563,   1009,   1469,   1756,   1947,   2169,   2378,   2431,   2324,   2176,   1986,   1559,    711,   -580,  -2158,
	 -3721,  -4908,  -5449,  -5309,  -4662,  -3653,  -2256,   -494,   1365,   2860,   3509,   3133,   2093,   1092,    695,    943,
	  1424,   1716,   1671,   1390,   1105,    998,   1001,    867,    463,    -85,   -532,   -737,   -731,   -682,   -794,  -1079,
	 -1264,  -1018,   -270,    707,   1538,   1969,   1925,   1562,   1187,    896,    442,   -247,   -764,   -761,   -271,    562,
	  1450,   1881,   1562,    705,   -258,   -968,  -1236,  -1071,   -751,   -706,  -1088,  -1497,  -1377,   -680,    116,    643,
	   971,   1288,   1561,   1595,   1288,    719,     38,   -645,  -1260,  -1673,  -1743,  -1531,  -1286,  -1196,  -1276,  -1415,
	 -1453,  -1257,   -787,   -118,    475,    518,   -281,  -1705,  -3188,  -4240,  -4761,  -4998,  -5180,  -5281,  -5143,  -4643,
	 -3736,  -2566,  -1441,   -522,    218,    808,   1335,   1952,   2698,   3468,   4095,   4433,   4454,   4267,   3963,   3492,
	  2780,   1987,   1425,   1164,   1056,   1139,   1603,   2432,   3416,   4294,   4657,   4027,   2279,   -120,  -2360,  -3794,
	 -4182,  -3647,  -2552,  -1253,    110,   1451,   2644,   3661,   4490,   4977,   4959,   4394,   3356,   1989,    462,  -1002,
	 -2101,  -2605,  -2418,  -1563,   -299,    846,   1328,    918,   -234,  -1707,  -2945,  -3538,  -3560,  -3472,  -3663,  -4100,
	 -4420,  -4340,  -3915,  -3444,  -3155,  -3034,  -3007,  -3068,  -3192,  -3305,  -3373,  -3426,  -3465,  -3414,  -3271,  -3179,
	 -3282,  -3546,  -3708,  -3451,  -2716,  -1752,   -807,     78,    894,   1487,   1727,   1685,   1542,   1527,   1922,   2853,
	  4100,   5253,   6020,   6345,   6299,   5933,   5234,   4160,   2759,   1303,    217,   -274,   -335,   -234,     -2,    518,
	  1401,   2490,   3468,   4031,   3968,   3213,   1964,    702,    -68,   -203,    -37,    -99,   -625,  -1260,  -1437,  -1049,
	  -494,    -92,    202,    432,    341,   -264,  -1110,  -1744,  -2013,  -2021,  -1877,  -1660,  -1435,  -1249,  -1182,  -1315,
	 -1577,  -1722,  -1466,   -736,    117,    592,    570,    380,    460,   1101,   2223,   3288,   3737,   3525,   2975,   2256,
	  1363,    510,    143,    507,   1404,   2435,   3235,   3443,   2751,   1197,   -653,  -1966,  -2303,  -2013,  -1815,  -2092,
	 -2559,  -2442,  -1201,    745,   2348,   2975,   2748,   2007,    943,   -230,  -1140,  -1480,  -1366,  -1301,  -1605,  -2077,
	 -2310,  -2093,  -1555,  -1154,  -1399,  -2382,  -3689,  -4857,  -5646,  -5835,  -5195,  -3820,  -2198,   -841,    108,    796,
	  1267,   1397,   1064,    270,   -792,  -1783,  -2323,  -2074,  -1011,    478,   1907,   2949,   3529,   3817,   4033,   4207,
	  4165,   3752,   2997,   2055,   1164,    501,   -104,   -904,  -1667,  -1767,   -863,    652,   1885,   2117,   1251,   -257,
	 -1669,  -2340,  -2089,  -1300,   -548,    -41,    385,    751,    779,    229,   -751,  -1539,  -1484,   -567,    551,   1179,
	  1091,    449,   -446,  -1287,  -1892,  -2222,  -2385,  -2569,  -2635,  -2015,   -373,   1908,   4070,   5555,   6089,   5527,
	  3966,   1937,    211,   -753,  -1096,  -1231,  -1383,  -1519,  -1469,  -1084,   -398,    274,    460,     -9,   -644,   -711,
	    66,   1332,   2497,   3136,   3132,   2601,   1771,    981,    672,   1063,   1853,   2529,   2882,   3018,   2981,   2668,
	  2044,   1200,    321,   -313,   -495,   -253,     90,     67,   -491,  -1250,  -1826,  -2252,  -2775,  -3410,  -3930,  -4101,
	 -3856,  -3315,  -2634,  -1955,  -1489,  -1449,  -1927,  -2852,  -3934,  -4705,  -4813,  -4342,  -3692,  -3143,  -2754,  -2521,
	 -2333,  -1871,   -828,    707,   2194,   3117,   3464,   3565,   3641,   3690,   3608,   3318,   2940,   2777,   3026,   3516,
	  3735,   3168,   1684,   -328,  -2158,  -3149,  -3052,  -2179,  -1117,   -283,    195,    343,    230,    -11,   -212,   -333,
	  -397,   -237,    365,   1283,   2058,   2340,   2084,   1389,    468,   -354,   -834,   -875,   -447,    363,   1246,   1746,
	  1564,    822,    -64,   -726,  -1035,  -1117,  -1271,  -1715,  -2337,  -2788,  -2848,  -2648,  -2466,  -2326,  -1959,  -1239,
	  -405,    131,     93,   -421,   -970,  -1142,   -952,   -748,   -731,   -821,   -855,   -685,   -192,    556,   1218,   1511,
	  1497,   1271,    770,    187,      7,    373,    973,   1665,   2598,   3680,   4464,   4622,   4267,   3733,   3168,   2518,
	  1829,   1274,    832,    289,   -341,   -716,   -573,     -5,    657,    968,    559,   -588,  -2130,  -3629,  -4757,  -5265,
	 -4943,  -3812,  -2170,   -358,   1298,   2360,   2456,   1656,    490,   -401,   -714,   -681,   -662,   -571,     46,   1317,
	  2682,   3320,   2823,   1492,     48,   -915,  -1233,  -1077,   -718,   -345,     31,    499,   1012,   1268,    966,    156,
	  -839,  -1681,  -2093,  -1907,  -1173,    -45,   1394,   2979,   4238,   4603,   3784,   1888,   -609,  -2970,  -4520,  -4909,
	 -4172,  -2749,  -1265,   -113,    717,   1398,   1950,   2247,   2194,   1829,   1342,    942,    696,    598,    655,    808,
	   929,    890,    542,   -219,  -1322,  -2610,  -4021,  -5410,  -6337,  -6329,  -5349,  -3926,  -2804,  -2383,  -2502,  -2718,
	 -2703,  -2267,  -1229,    484,   2661,   4810,   6387,   7046,   6625,   5149,   3070,   1202,    106,   -346,   -626,  -1008,
	 -1472,  -1911,  -2249,  -2310,  -1730,   -306,   1585,   3137,   3721,   3318,   2442,   1674,   1262,   1121,   1158,   1478,
	  2091,   2600,   2543,   1870,    885,   -135,  -1224,  -2543,  -3906,  -4836,  -5126,  -4942,  -4452,  -3685,  -2725,  -1824,
	 -1342,  -1651,  -2857,  -4453,  -5529,  -5567,  -4787,  -3649,  -2379,  -1083,    104,   1101,   1759,   1863,   1484,   1055,
	  1048,   1685,   2798,   4000,   4936,   5359,   5129,   4235,   2815,   1223,    -12,   -527,   -313,    513,   1777,   3127,
	  4155,   4566,   4181,   3048,   1539,    132,   -955,  -1843,  -2662,  -3288,  -3492,  -3124,  -2118,   -581,   1113,   2479,
	  3125,   2922,   2097,   1131,    532,    490,    784,   1198,   1729,   2284,   2476,   1943,    744,   -678,  -1815,  -2303,
	 -2100,  -1575,  -1238,  -1319,  -1632,  -1770,  -1476,   -852,   -232,     18,   -357,  -1281,  -2301,  -2921,  -2946,  -2448,
	 -1538,   -410,    544,    988,    980,    891,    955,   1036,    828,    212,   -618,  -1324,  -1668,  -1631,  -1369,  -1109,
	 -1041,  -1172,  -1258,   -912,     92,   1482,   2579,   2826,   2173,    996,   -141,   -749,   -758,   -535,   -481,   -737,
	 -1229,  -1830,  -2408,  -2786,  -2834,  -2634,  -2382,  -2174,  -1918,  -1439,   -740,   -115,    137,     84,     57,    244,
	   598,   1069,   1634,   2151,   2400,   2323,   2155,   2184,   2411,   2626,   2812,   3164,   3737,   4399,   4953,   5104,
	  4586,   3486,   2279,   1389,    919,    768,    816,   1074,   1696,   2699,   3701,   4078,   3488,   2201,    896,    122,
	   -81,    -51,   -131,   -398,   -610,   -464,    -38,    106,   -512,  -1692,  -2829,  -3626,  -4098,  -4127,  -3546,  -2503,
	 -1406,   -677,   -635,  -1298,  -2172,  -2535,  -2061,  -1154,   -639,   -993,  -1924,  -2677,  -2644,  -1826,   -788,    -97,
	   102,    -69,   -439,   -790,  -1000,  -1135,  -1231,  -1203,  -1038,   -914,  -1000,  -1210,  -1403,  -1590,  -1709,  -1527,
	  -894,     74,   1005,   1478,   1414,   1127,    936,    954,   1148,   1328,   1147,    369,   -920,  -2383,  -3645,  -4491,
	 -4915,  -4978,  -4650,  -3925,  -3120,  -2771,  -2984,  -3207,  -2862,  -1876,   -618,    438,   1036,   1227,   1173,    911,
	   429,    -71,   -196,    323,   1336,   2286,   2692,   2530,   2052,   1521,   1204,   1382,   2176,   3332,   4303,   4651,
	  4414,   3990,   3700,   3625,   3680,   3670,   3436,   2969,   2374,   1869,   1728,   1986,   2303,   2275,   1763,    923,
	    43,   -604,   -815,   -516,    140,    802,   1178,   1218,    972,    386,   -561,  -1617,  -2346,  -2559,  -2577,  -2839,
	 -3365,  -3914,  -4463,  -5047,  -5275,  -4601,  -3097,  -1482,   -350,    373,   1113,   1991,   2671,   2793,   2355,   1639,
	   863,     71,   -750,  -1548,  -2066,  -1831,   -578,   1362,   3394,   5078,   6031,   5900,   4684,   2843,    972,   -566,
	 -1705,  -2454,  -2817,  -2874,  -2825,  -2810,  -2644,  -2054,  -1188,   -541,   -395,   -524,   -515,   -298,   -118,    -92,
	   -61,    129,    383,    471,    267,   -219,   -920,  -1764,  -2644,  -3313,  -3335,  -2359,   -594,   1136,   1998,   1779,
	   872,   -235,  -1332,  -2341,  -3021,  -3084,  -2518,  -1577,   -516,    556,   1672,   2890,   4100,   4902,   4841,   3893,
	  2510,   1175,    208,   -195,    -27,    550,   1377,   2251,   2671,   2124,    643,  -1207,  -2664,  -3127,  -2571,  -1524,
	  -572,    -11,    209,    323,    501,    798,   1237,   1743,   2061,   1955,   1504,   1095,   1051,   1364,   1762,   1961,
	  1856,   1445,    675,   -509,  -2078,  -3869,  -5477,  -6296,  -5836,  -4093,  -1719,    210,    909,    457,   -415,  -1041,
	 -1286,  -1430,  -1844,  -2713,  -3852,  -4798,  -5170,  -4888,  -4042,  -2677,   -856,   1118,   2752,   3650,   3789,   3501,
	  3038,   2281,   1128,   -103,   -934,  -1157,   -870,   -288,    350,    821,    994,    834,    388,   -198,   -736,  -1142,
	 -1485,  -1908,  -2430,  -2833,  -2760,  -1950,   -527,    993,   2077,   2450,   2128,   1486,   1086,   1263,   1940,   2842,
	  3744,   4503,   5011,   5248,   5306,   5289,   5090,   4442,   3277,   1844,    520,   -335,   -433,    308,   1520,   2456,
	  2509,   1599,    165,  -1176,  -2021,  -2179,  -1548,   -276,   1122,   2064,   2223,   1559,    303,  -1091,  -2235,  -3036,
	 -3590,  -3973,  -4170,  -4139,  -3774,  -2902,  -1497,     76,   1192,   1516,   1315,   1056,    908,    762,    467,    -65,
	  -749,  -1349,  -1590,  -1200,   -129,   1167,   2039,   2264,   2058,   1636,   1053,    315,   -594,  -1671,  -2744,  -3475,
	 -3529,  -2834,  -1729,   -734,   -136,    120,    175,      5,   -549,  -1539,  -2704,  -3527,  -3546,  -2719,  -1553,   -673,
	  -284,   -182,    -33,    361,    898,   1309,   1461,   1400,   1164,    763,    343,    133,    197,    440,    839,   1341,
	  1698,   1659,   1189,    540,    126,    109,    260,    406,    662,   1151,   1702,   1934,   1698,   1236,    822,    559,
	   495,    688,   1077,   1372,   1281,    824,    164,   -674,  -1644,  -2428,  -2546,  -1763,   -363,   1076,   2189,   2912,
	  3167,   2788,   1707,     43,  -1847,  -3262,  -3422,  -2138,   -122,   1558,   2303,   2150,   1363,    188,  -1095,  -2184,
	 -2838,  -2979,  -2776,  -2570,  -2631,  -2946,  -3246,  -3231,  -2846,  -2327,  -1968,  -1866,  -1852,  -1694,  -1360,  -1049,
	  -885,   -718,   -367,    175,    862,   1572,   2014,   1970,   1600,   1357,   1744,   2924,   4313,   4973,   4498,   3169,
	  1527,    214,   -190,    368,   1266,   1753,   1504,    597,   -650,  -1644,  -1709,   -700,    732,   1661,   1567,    570,
	  -668,  -1373,  -1335,   -993,   -788,   -736,   -615,   -370,   -261,   -574,  -1352,  -2452,  -3527,  -4023,  -3578,  -2365,
	  -885,    432,   1333,   1689,   1508,    937,    240,   -265,   -337,    122,   1009,   1973,   2634,   2861,   2633,   1973,
	  1084,    260,   -340,   -677,   -705,   -419,    -12,    175,    -82,   -692,  -1304,  -1558,  -1271,   -541,    261,    716,
	   752,    689,    795,    968,    875,    320,   -511,  -1129,  -1052,    -73,   1602,   3362,   4482,   4575,   3769,   2485,
	  1199,    340,    125,    397,    799,   1074,   1099,    887,    642,    587,    769,    973,    788,    -28,  -1302,  -2727,
	 -4175,  -5586,  -6786,  -7609,  -8153,  -8483,  -8187,  -6861,  -4776,  -2652,  -1089,   -363,   -525,  -1416,  -2498,  -3030,
	 -2656,  -1613,   -373,    704,   1403,   1605,   1450,   1426,   1896,   2558,   2793,   2390,   1649,   1021,    734,    566,
	   122,   -567,   -911,   -287,   1402,   3593,   5332,   5791,   4793,   2890,    956,   -278,   -557,   -163,    440,    922,
	  1077,    857,    493,    308,    441,    810,   1187,   1273,    885,    163,   -511,   -778,   -486,    366,   1747,   3434,
	  4895,   5634,   5555,   4769,   3435,   1969,    826,    -37,  -1021,  -2189,  -3058,  -3136,  -2519,  -1795,  -1476,  -1669,
	 -2092,  -2353,  -2288,  -1908,  -1189,   -221,    653,   1163,   1321,   1192,    840,    462,    240,    116,   -134,   -697,
	 -1486,  -1968,  -1444,    296,   2678,   4688,   5497,   4853,   3080,    783,  -1523,  -3503,  -4863,  -5310,  -4730,  -3418,
	 -1986,  -1007,   -782,  -1234,  -1949,  -2416,  -2330,  -1696,   -738,    236,    934,   1218,   1186,   1070,   1048,   1134,
	  1247,   1345,   1450,   1583,   1743,   1950,   2246,   2604,   2888,   2963,   2781,   2401,   2041,   1968,   2190,   2397,
	  2254,   1740,   1180,    880,    823,    792,    610,    187,   -426,  -1111,  -1832,  -2508,  -2879,  -2738,  -2255,  -1984,
	 -2426,  -3449,  -4323,  -4425,  -3706,  -2585,  -1573,   -872,   -286,    362,    893,    976,    528,    -95,   -438,   -451,
	  -384,   -422,   -639,   -989,  -1251,  -1149,   -548,    424,   1397,   1949,   1824,   1080,     85,   -635,   -686,    -84,
	   775,   1400,   1469,    961,     79,   -876,  -1607,  -1947,  -1952,  -1785,  -1516,  -1174,   -939,   -985,  -1228,  -1459,
	 -1505,  -1267,   -823,   -414,   -203,   -187,   -339,   -690,  -1162,  -1460,  -1301,   -743,   -164,     42,   -323,  -1114,
	 -1793,  -1788,  -1024,    -19,    590,    588,    293,    149,    266,    407,    405,    534,   1287,   2792,   4609,   6148,
	  7072,   7337,   7099,   6595,   5968,   5146,   3960,   2432,    820,   -598,  -1543,  -1751,  -1289,   -671,   -401,   -586,
	 -1019,  -1335,  -1255,   -871,   -488,   -246,   -171,   -428,  -1259,  -2683,  -4360,  -5773,  -6565,  -6709,  -6339,  -5552,
	 -4336,  -2711,   -981,    422,   1356,   1873,   1934,   1551,    963,    523,    461,    590,    411,   -275,  -1144,  -1797,
	 -2139,  -2234,  -2000,  -1233,     63,   1575,   2959,   4003,   4520,   4338,   3491,   2386,   1643,   1656,   2308,   3123,
	  3638,   3633,   3153,   2426,   1696,   1086,    655,    419,    279,     26,   -495,  -1126,  -1492,  -1433,  -1104,   -760,
	  -585,   -556,   -469,   -230,     -9,     -5,   -157,   -158,    275,   1169,   2232,   3052,   3362,   3088,   2361,   1551,
	   992,    633,    176,   -530,  -1455,  -2541,  -3700,  -4745,  -5411,  -5476,  -4894,  -3735,  -2077,   -125,   1721,   3088,
	  3779,   3775,   3229,   2381,   1399,    277,  -1099,  -2660,  -4038,  -4833,  -4936,  -4518,  -3750,  -2704,  -1506,   -427,
	   251,    474,    429,    201,   -220,   -529,   -283,    429,    983,    902,    228,   -551,   -738,    109,   1756,   3390,
	  4137,   3622,   2137,    270,  -1523,  -2937,  -3642,  -3397,  -2370,  -1033,    335,   1694,   2884,   3572,   3499,   2729,
	  1652,    619,   -271,   -983,  -1465,  -1779,  -2023,  -2133,  -1969,  -1464,   -528,    915,   2594,   3875,   4304,   3965,
	  3207,   2291,   1353,    569,    193,    308,    588,    535,   -123,  -1344,  -2902,  -4476,  -5672,  -6141,  -5822,  -5008,
	 -4146,  -3637,  -3693,  -4213,  -4768,  -4749,  -3631,  -1374,   1415,   3871,   5370,   5661,   4849,   3423,   2009,    930,
	   272,    214,    814,   1675,   2117,   1647,    289,  -1497,  -3071,  -3811,  -3448,  -2250,   -715,    794,   2090,   3102,
	  3875,   4477,   4895,   5076,   4972,   4577,   4027,   3488,   2837,   1795,    385,   -997,  -1923,  -2226,  -1987,  -1412,
	  -806,   -450,   -367,   -412,   -445,   -329,     46,    592,   1025,   1212,   1288,   1336,   1276,   1152,   1184,   1534,
	  2157,   2752,   2948,   2624,   1902,    992,    215,    -97,     45,    211,      3,   -587,  -1350,  -2071,  -2545,  -2704,
	 -2757,  -2921,  -3147,  -3270,  -3328,  -3600,  -4178,  -4628,  -4414,  -3525,  -2368,  -1254,   -266,    526,   1065,   1573,
	  2441,   3642,   4531,   4452,   3445,   2128,    955,    -73,  -1067,  -2004,  -2697,  -2833,  -2218,  -1102,    -49,    508,
	   442,    -96,   -716,  -1004,   -804,   -277,    198,    181,   -554,  -1768,  -2823,  -3157,  -2743,  -1948,  -1087,   -351,
	   104,    295,    460,    840,   1460,   2161,   2799,   3334,   3727,   3828,   3423,   2456,   1242,    245,   -381,   -882,
	 -1591,  -2660,  -3912,  -4783,  -4639,  -3365,  -1545,     66,   1084,   1462,   1289,    775,    175,   -300,   -472,   -298,
	    50,    269,    113,   -313,   -572,   -421,    -44,    387,    915,   1480,   1868,   2001,   1951,   1746,   1408,   1004,
	   555,     70,   -316,   -374,     72,   1117,   2696,   4332,   5201,   4750,   3178,   1163,   -775,  -2542,  -4164,  -5388,
	 -5826,  -5436,  -4636,  -3971,  -3674,  -3504,  -3011,  -1946,   -437,   1108,   2222,   2611,   2313,   1616,    862,    202,
	  -489,  -1252,  -1752,  -1564,   -566,    904,   2138,   2578,   2358,   2055,   2016,   2169,   2324,   2387,   2367,   2296,
	  2123,   1712,   1024,    279,   -135,    -38,    317,    534,    421,      3,   -621,  -1313,  -1851,  -2058,  -1954,  -1686,
	 -1361,  -1031,   -678,   -182,    472,   1087,   1551,   1968,   2484,   3160,   3889,   4390,   4405,   3822,   2619,    943,
	  -812,  -2186,  -2934,  -3194,  -3406,  -3900,  -4502,  -4650,  -3980,  -2735,  -1545,   -864,   -636,   -494,   -188,    212,
	   384,    -33,  -1171,  -2740,  -4135,  -4810,  -4552,  -3474,  -1980,   -727,   -275,   -657,  -1342,  -1680,  -1408,   -658,
	   411,   1701,   2967,   3855,   4227,   4220,   3981,   3472,   2585,   1415,    396,     62,    568,   1553,   2479,   3029,
	  3166,   2956,   2452,   1756,   1035,    512,    485,   1100,   2025,   2535,   1992,    403,  -1548,  -3090,  -3850,  -3950,
	 -3816,  -3757,  -3689,  -3319,  -2547,  -1577,   -732,   -298,   -441,  -1064,  -1825,  -2283,  -2063,  -1140,     10,    746,
	   722,    -43,  -1358,  -2862,  -4014,  -4371,  -3788,  -2442,   -809,    579,   1436,   1757,   1754,   1714,   1732,   1780,
	  2030,   2767,   3910,   4856,   4901,   3797,   1907,    -42,  -1377,  -1860,  -1695,  -1191,   -502,    277,   1001,   1526,
	  1703,   1399,    722,     23,   -383,   -447,   -336,   -132,    245,    714,    992,    935,    660,    417,    501,   1124,
	  2185,   3163,   3383,   2590,   1252,    104,   -526,   -786,   -888,   -863,   -712,   -503,   -178,    482,   1466,   2382,
	  2836,   2687,   2006,   1072,    179,   -637,  -1407,  -1901,  -1736,   -822,    383,   1193,   1201,    538,   -282,   -838,
	 -1152,  -1568,  -2322,  -3283,  -4130,  -4599,  -4526,  -3906,  -2925,  -1824,   -820,   -133,    162,    222,    222,    206,
	   120,    -75,   -264,   -185,    378,   1408,   2600,   3535,   3915,   3646,   2874,   1898,    929,     71,   -488,   -593,
	  -409,   -286,   -374,   -553,   -676,   -798,  -1099,  -1668,  -2413,  -3088,  -3445,  -3345,  -2785,  -2011,  -1387,  -1001,
	  -630,   -170,    268,    755,   1662,   3168,   4860,   5993,   6079,   5187,   3747,   2206,    949,    332,    455,    997,
	  1447,   1400,    779,    -92,   -792,  -1170,  -1295,  -1276,  -1139,   -747,     37,    950,   1347,    825,   -347,  -1584,
	 -2587,  -3416,  -4163,  -4762,  -5021,  -4810,  -4250,  -3561,  -2806,  -1944,   -985,    -10,    780,   1052,    562,   -567,
	 -1803,  -2533,  -2472,  -1723,   -598,    515,   1267,   1516,   1421,   1321,   1488,   1970,   2551,   2868,   2641,   1774,
	   414,  -1021,  -2106,  -2701,  -2718,  -1999,   -756,    354,    840,    695,    237,    -83,    133,   1039,   2418,   3731,
	  4468,   4465,   3832,   2777,   1610,    732,    377,    364,    264,   -128,   -552,   -619,   -285,    160,    470,    648,
	   846,   1193,   1765,   2535,   3251,   3546,   3208,   2275,    987,   -267,  -1097,  -1479,  -1917,  -2890,  -4238,  -5354,
	 -5649,  -4778,  -2915,   -830,    648,   1201,   1132,   1006,   1108,   1338,   1588,   1847,   2010,   1874,   1298,    388,
	  -446,   -761,   -438,    355,   1381,   2340,   2822,   2530,   1511,    142,  -1137,  -2106,  -2763,  -3166,  -3434,  -3649,
	 -3683,  -3346,  -2649,  -1812,  -1113,   -689,   -396,     -2,    509,    972,   1223,   1206,   1019,    835,    753,    766,
	   816,    835,    734,    445,    -33,   -624,  -1278,  -1981,  -2609,  -2935,  -2870,  -2465,  -1774,   -915,   -140,    332,
	   528,    783,   1468,   2517,   3354,   3404,   2584,   1304,     36,  -1082,  -2015,  -2483,  -2312,  -1782,  -1279,   -908,
	  -513,     45,    715,   1380,   1914,   2226,   2432,   2801,   3360,   3723,   3494,   2823,   2341,   2427,   2740,   2631,
	  1754,    189,  -1699,  -3374,  -4359,  -4494,  -3866,  -2679,  -1293,   -115,    631,    923,    846,    502,     23,   -255,
	    91,   1055,   2126,   2830,   3022,   2779,   2258,   1583,    832,    103,   -545,  -1071,  -1248,   -776,    224,   1097,
	  1160,    219,  -1362,  -2952,  -3933,  -3954,  -3221,  -2404,  -1980,  -1801,  -1513,  -1128,   -936,  -1084,  -1446,  -1838,
	 -2143,  -2215,  -1867,  -1129,   -487,   -674,  -1948,  -3681,  -4833,  -4595,  -2733,    237,   3279,   5459,   6352,   6030,
	  4934,   3638,   2553,   1793,   1260,    835,    562,    544,    680,    671,    167,  -1005,  -2558,  -3955,  -4875,  -5243,
	 -5060,  -4327,  -3066,  -1557,   -447,   -261,   -915,  -1776,  -2115,  -1641,   -662,    420,   1461,   2376,   3019,   3415,
	  3782,   4267,   4827,   5306,   5475,   5029,   3831,   2241,    952,    457,    720,   1202,   1247,    619,   -325,   -936,
	  -791,    -63,    672,    951,    686,     76,   -546,   -971,  -1279,  -1616,  -1933,  -2044,  -1828,  -1213,   -112,   1290,
	  2400,   2683,   2142,   1139,    -49,  -1270,  -2299,  -2835,  -2689,  -1851,   -493,   1034,   2358,   3315,   3912,   4169,
	  4019,   3279,   1826,   -113,  -1904,  -2853,  -2705,  -1768,   -632,    133,    201,   -399,  -1388,  -2406,  -3129,  -3458,
	 -3529,  -3476,  -3347,  -3141,  -2850,  -2564,  -2395,  -2213,  -1706,   -842,    -97,     26,   -414,   -946,  -1278,  -1483,
	 -1756,  -2131,  -2406,  -2254,  -1462,   -235,    756,    916,    368,   -229,   -406,   -134,    403,    972,   1369,   1493,
	  1262,    647,    -94,   -476,   -215,    568,   1585,   2707,   3971,   5254,   6106,   6048,   5081,   3772,   2843,   2708,
	  3200,   3681,   3556,   2715,   1514,    517,    114,    162,    122,   -335,  -1039,  -1715,  -2361,  -3042,  -3571,  -3652,
	 -3204,  -2415,  -1535,   -692,    104,    781,   1107,    821,   -152,  -1516,  -2647,  -2958,  -2335,  -1197,    -74,    771,
	  1231,   1228,    917,    619,    387,     -4,   -615,  -1170,  -1336,  -1030,   -372,    547,   1676,   2898,   4034,   4867,
	  5127,   4738,   4078,   3631,   3426,   3095,   2377,   1433,    710,    490,    669,    995,   1329,   1602,   1634,   1214,
	   381,   -620,  -1642,  -2653,  -3543,  -4134,  -4312,  -4034,  -3389,  -2678,  -2196,  -1971,  -1897,  -1939,  -2115,  -2430,
	 -2819,  -3096,  -3096,  -2822,  -2409,  -2053,  -1889,  -1860,  -1741,  -1300,   -541,    122,     78,   -981,  -2770,  -4473,
	 -5191,  -4521,  -2737,   -558,   1225,   2203,   2574,   2825,   3175,   3306,   2716,   1436,    228,   -104,    543,   1626,
	  2560,   3113,   3258,   2964,   2298,   1436,    452,   -660,  -1763,  -2503,  -2383,  -1218,    393,   1476,   1628,   1260,
	  1005,   1156,   1514,   1648,   1309,    487,   -741,  -2164,  -3430,  -4186,  -4231,  -3676,  -2929,  -2389,  -2150,  -2036,
	 -1891,  -1701,  -1563,  -1590,  -1741,  -1781,  -1354,   -147,   1696,   3489,   4549,   4649,   4037,   3308,   3025,   3182,
	  3314,   3165,   2906,   2671,   2253,   1415,    367,   -302,   -192,    534,   1326,   1574,    844,   -771,  -2511,  -3473,
	 -3353,  -2583,  -1872,  -1713,  -2190,  -3032,  -3782,  -4009,  -3489,  -2327,   -872,    480,   1376,   1614,   1296,    770,
	   361,    182,    183,    394,    947,   1752,   2377,   2448,   2006,   1464,   1322,   1834,   2788,   3568,   3539,   2546,
	  1034,   -392,  -1420,  -2084,  -2595,  -3151,  -3819,  -4582,  -5362,  -5942,  -6046,  -5587,  -4749,  -3811,  -2830,  -1522,
	   353,   2464,   4036,   4530,   4002,   2967,   2139,   1966,   2239,   2396,   2187,   1777,   1385,   1108,    940,    754,
	   481,    288,    400,    825,   1334,   1666,   1680,   1392,    997,    792,    953,   1435,   2151,   3000,   3645,   3634,
	  2802,   1325,   -475,  -2067,  -2808,  -2435,  -1361,   -324,    162,    -12,   -567,   -933,   -638,    268,   1242,   1764,
	  1733,   1391,   1097,   1119,   1453,   1847,   2070,   2091,   1969,   1741,   1469,   1247,   1053,    658,   -151,  -1274,
	 -2429,  -3452,  -4238,  -4659,  -4716,  -4483,  -3972,  -3288,  -2735,  -2565,  -2748,  -3083,  -3354,  -3365,  -3016,  -2321,
	 -1384,   -452,     85,   -114,  -1027,  -2134,  -2803,  -2807,  -2299,  -1362,     69,   1757,   3113,   3698,   3522,   2913,
	  2241,   1761,   1664,   2058,   2801,   3490,   3749,   3462,   2717,   1634,    311,  -1043,  -2059,  -2437,  -2187,  -1623,
	 -1113,   -818,   -718,   -775,   -914,  -1069,  -1263,  -1498,  -1784,  -2136,  -2358,  -2191,  -1667,   -966,   -162,    721,
	  1588,   2247,   2435,   1997,   1137,    371,    214,    879,   2186,   3708,   4958,   5535,   5264,   4231,   2819,   1700,
	  1375,   1631,   1799,   1488,    749,   -227,  -1279,  -2222,  -2769,  -2684,  -1994,   -958,     42,    577,    326,   -663,
	 -1870,  -2655,  -2755,  -2373,  -1858,  -1494,  -1518,  -2001,  -2702,  -3178,  -2995,  -2020,   -698,    247,    452,    103,
	  -339,   -390,    180,    998,   1296,    761,   -115,   -632,   -529,    -39,    452,    751,    924,   1027,    914,    422,
	  -489,  -1705,  -2910,  -3778,  -4224,  -4292,  -3948,  -3194,  -2216,  -1197,   -150,    876,   1673,   2085,   2177,   2089,
	  1866,   1600,   1530,   1749,   2006,   1994,   1684,   1267,    892,    515,     70,   -277,   -304,    -42,    257,    344,
	   142,   -204,   -394,   -128,    710,   1977,   3328,   4406,   4993,   4968,   4243,   2875,   1184,   -363,  -1401,  -1863,
	 -1921,  -1686,  -1111,   -236,    681,   1372,   1690,   1531,    905,      1,   -929,  -1757,  -2599,  -3564,  -4354,  -4458,
	 -3681,  -2207,   -385,   1369,   2713,   3568,   4050,   4182,   3865,   3176,   2453,   1904,   1276,    273,   -789,  -1204,
	  -644,    599,   1904,   2601,   2286,   1138,   -266,  -1484,  -2357,  -2817,  -2741,  -2004,   -699,    810,   2118,   2915,
	  2914,   1875,   -103,  -2379,  -4096,  -4736,  -4251,  -2918,  -1232,    323,   1461,   2052,   2120,   1833,   1377,    809,
	    48,   -960,  -2089,  -3023,  -3378,  -2902,  -1635,    105,   1877,   3313,   4267,   4749,   4752,   4245,   3330,   2302,
	  1406,    579,   -291,  -1061,  -1528,  -1703,  -1740,  -1696,  -1448,   -878,    -51,    743,   1111,    872,    258,   -342,
	  -728,   -883,   -814,   -572,   -422,   -755,  -1703,  -3012,  -4306,  -5255,  -5589,  -5220,  -4371,  -3374,  -2328,  -1154,
	    85,   1169,   1940,   2315,   2182,   1501,    424,   -864,  -2237,  -3439,  -4009,  -3611,  -2391,   -852,    553,   1650,
	  2490,   3109,   3369,   3092,   2311,   1339,    547,     75,   -174,   -276,   -160,    163,    447,    421,     59,   -457,
	  -873,   -871,   -180,   1149,   2756,   4258,   5485,   6447,   7020,   6872,   5818,   4068,   2150,    633,   -191,   -390,
	  -334,   -534,  -1239,  -2268,  -3310,  -4105,  -4368,  -3861,  -2652,  -1245,   -286,   -142,   -782,  -1837,  -2747,  -2993,
	 -2279,   -791,    697,   1317,    782,   -554,  -2157,  -3610,  -4616,  -4967,  -4538,  -3363,  -1759,   -231,    819,   1230,
	  1025,    327,   -663,  -1668,  -2410,  -2726,  -2679,  -2526,  -2434,  -2228,  -1567,   -293,   1453,   3363,   5038,   5980,
	  5779,   4479,   2666,   1098,    228,     12,      2,   -309,  -1014,  -1778,  -2279,  -2366,  -1887,   -809,    516,   1530,
	  1890,   1608,   1079,    942,   1489,   2343,   2899,   2877,   2347,   1574,    959,    803,    993,   1112,    810,     23,
	  -922,  -1478,  -1357,   -669,    328,   1298,   1827,   1684,   1001,    145,   -510,   -751,   -623,   -383,   -248,   -187,
	   -79,     -7,   -230,   -881,  -1818,  -2745,  -3424,  -3780,  -3813,  -3508,  -2908,  -2185,  -1522,   -965,   -475,    -45,
	   340,    780,   1358,   2062,   2789,   3317,   3357,   2774,   1747,    689,    -24,   -367,   -560,   -640,   -500,   -161,
	   296,    802,   1252,   1594,   1922,   2296,   2553,   2403,   1696,    644,   -283,   -763,   -842,   -708,   -459,   -119,
	   303,    700,    815,    470,   -145,   -549,   -448,     11,    510,    844,    892,    578,     92,    -29,    597,   1667,
	  2412,   2267,   1193,   -436,  -2082,  -3266,  -3720,  -3362,  -2269,   -765,    590,   1328,   1414,   1130,    745,    417,
	   213,    136,    205,    400,    531,    378,    -81,   -683,  -1129,  -1171,   -934,   -780,   -906,  -1317,  -1903,  -2432,
	 -2748,  -2923,  -3022,  -2893,  -2422,  -1735,  -1035,   -452,    -63,     68,   -140,   -733,  -1537,  -2084,  -1958,  -1255,
	  -589,   -503,   -840,   -899,   -224,    981,   2116,   2736,   2879,   2841,   2724,   2444,   2042,   1650,   1292,    987,
};
/* WBDIAG-577: two counters that turn 'crackle' into a number.
 * wbj = frames whose read position jumped outside [-1,+2] vs the previous
 *       frame (a discontinuity in posb[] itself)
 * wbx = track-blocks whose read landed inside the streamer's write zone
 *       (history exhausted: p_w - posb[0] > RING_SAMPLES - 64) */
static volatile uint32_t g_wbj, g_wbx;
/* FXSTAT2-585 (W274): per-effect starve accounting, OUTSIDE the mixer.
 * 584 put the classifier inside looper_audio_block: +520 B in the hottest
 * function, every loop after it moved, starves 3.4x (W274). Here the
 * mixer is byte-identical to 582; everything runs in audio_thread after
 * the mixer returns, off g_starve_cnt[] deltas and trk[].starved.
 *   stv = starve EVENTS (entries into silence)
 *   dry = track-blocks spent SILENT (what the ear hears -- W274)
 *   blk = mixer blocks in this class            17 x 3 x 4 B = 204 B */
#define FXS_N 19u   /* 0 none, 1-17 one effect (16 = reverb, REVERB-676; 17 = eq, EQ-691), 18 = more than one */
static uint32_t g_fxs_stv[FXS_N], g_fxs_dry[FXS_N], g_fxs_blk[FXS_N];
static uint32_t g_fxs_aus[FXS_N];   /* WOBCLAMP-681 FXA: worst looper_audio_block us per class */
static uint32_t g_fxs_over[FXS_N];  /* POPS-700 FXO: blocks over the 5,333 us period per class (output clicks) */
static volatile uint32_t g_rv_clip; /* POPS-700: the reverb's line-store clamp engaged (a clip inside the loop) */
/* POPTRAP-728 (W335): the output discontinuity trap. See pop_trap(). */
#define POP_TH 2000   /* TRAPLOW2-781: was 6000 (TRAPLOW-744: 12000) -- the 15:27 clicks at vu=5 sat under 6,000; the 4x isolation rule keeps content out of c= */
static volatile uint32_t g_pop_n;          /* blocks with a jump >= POP_TH (raw steps: content OR clicks) */
static volatile uint32_t g_pop_c;          /* POPTRAP2-734: blocks with an ISOLATED step (a click) */
static uint32_t g_pop_dp2, g_pop_prev_dpp;  /* POPTRAP2-734: the last two frames' packed diffs carry across the block edge */
#define POP_ISO_SHIFT 2u                   /* a click is >= 4x its neighbours' steps */
static volatile uint32_t g_pop_blkn;       /* blocks watched */
static volatile uint8_t  g_pop_pend;       /* a latched event waits for the diag */
/* POPWHO-779: the readback of the latched event -- see pop_who() */
static uint16_t g_pw_t[NTRK], g_pw_mon;    /* the largest 1-frame step around the click frame: each ring (raw), the jack */
static uint16_t g_pw_av[NTRK], g_pw_fd[NTRK];   /* each track's room (frames, clamped) and gain at the readback */
static uint16_t g_pw_bo[NTRK]; static uint32_t g_pw_so[NTRK];   /* BLKOFF-784: the click position inside its codec block / inside the loop (blocks) */
/* RUNLOG-785: the ring's shape around the click, the run's ends, and who wrote it */
#define RL_N 6u
struct rl_ent { uint32_t ms; uint16_t s, n; uint8_t p; };
static struct rl_ent g_rl_log[NTRK][RL_N]; static uint8_t g_rl_idx[NTRK];   /* per track, the last RL_N decodes */
static int16_t  g_rl_rv[24]; static uint16_t g_rl_rl, g_rl_rls, g_rl_rb, g_rl_rbs; static uint8_t g_rl_wt;
static struct rl_ent g_rl_out[RL_N];   /* the worst track's log, copied at the readback */
static void __attribute__((noinline)) rl_add(int trk, uint32_t start, uint32_t n, uint8_t path)
{
	if ((unsigned)trk >= NTRK) return;
	struct rl_ent *e = &g_rl_log[trk][g_rl_idx[trk]];
	g_rl_idx[trk] = (uint8_t)((g_rl_idx[trk] + 1u) % RL_N);
	e->ms = k_uptime_get_32(); e->s = (uint16_t)start; e->n = (uint16_t)((n > 65535u) ? 65535u : n); e->p = path;
}
static uint8_t  g_pw_st, g_pw_done;        /* starved bits; the readback ran for the held event */
static uint32_t g_pw_ecd, g_pw_ecd_step;   /* the echo delay last block; its change on the event's block */
static uint32_t g_pop_ms, g_pop_cus, g_pop_got, g_pop_spd;   /* POPLOG-730: hi (the EMMC48 line has it) and av/fade (the tags cover them) dropped for the floor */
static int16_t  g_pop_jmp, g_pop_pk;   /* POPTRAP2-734: int16 (both <= 32767) -- 4 B for the floor */
static uint16_t g_pop_at;
/* POPLOG-730 (STACK G): the transition census. See pop_trap(). */
struct pop_snap { uint32_t cpos, spd, w0, w1, snap; uint16_t vol[NTRK]; };
static struct pop_snap g_pop_ps[2];               /* [0] = the last block, [1] = the one before */
struct pop_ev { uint32_t ms, tags; uint16_t jmp; uint8_t at, ch; uint8_t ctl, age; };
#define POPL_N 4u
static struct pop_ev g_pop_log[POPL_N];
static uint8_t  g_pop_logn;                       /* events logged (POPSTICKY-788: saturates at POPL_N, the ring rolls) */
static uint8_t  g_pop_logi;                       /* POPSTICKY-788: the next slot */
static volatile uint8_t  g_pop_ctl;               /* last helper stamp: 1 fx_reset 2 rev_toggle 3 iso_engage 4 iso_release */
static volatile uint32_t g_pop_ctl_ms;
static inline void pop_stamp(uint8_t code) { g_pop_ctl = code; g_pop_ctl_ms = k_uptime_get_32(); }
static uint8_t  g_pop_ch, g_pop_stv, g_pop_pl, g_pop_usb;
static int16_t  g_pop_prev[2];
static uint32_t g_fxs_prev;
static uint32_t g_wb_lastpos;
static uint32_t g_tp_rng   = 0x89abcdefu;
static int32_t  g_tp_k0L, g_tp_k1L, g_tp_k2L;
static int32_t  g_tp_k0R, g_tp_k1R, g_tp_k2R;
static uint32_t g_wb_wowph = 0u;
static uint32_t g_wb_fltph = 0x40000000u;    /* the quarter-cycle offset */
static uint32_t g_wb_rng   = 0x1234567u;
static int32_t  g_wb_wnse, g_wb_fnse;
static int32_t  g_wb_off, g_wb_tgt;          /* Q16 read offset, ramped */

#define WOB_BASE_SAMP   340          /* TUNE3-577: must exceed peak wobble (300+32) */
#define WOB_WOW_PEAK    (300 << 16)   /* TUNE3-577: 3.1% / 54 cents at 0.8 Hz (marc: exaggerate) */
#define WOB_FLT_PEAK    ( 32 << 16)   /* TUNE3-577: 2.1% at 5 Hz */
#define WOB_WOW_INC      71582u      /* TUNE2-576: 0.8 Hz -- 0.3 Hz was DRIFT, not wobble */
#define WOB_FLT_INC     447392u      /* TUNE2-576: 5.0 Hz */
#define WOB_MAX_RATE_Q16  3600       /* TUNE3-577: 5.5% -- above the combined peak 5.2%.
                                      * (was 3.7% -- above wow+flutter's combined
                                      * peak (3.5%), so the LFO is never flattened. Also
                                      * the pitch-bend rate of a fader GLIDE: marc found
                                      * that and likes it -- a manual wow. Keep it.
                                      * (was 2.5%, which CLAMPED wow+flutter
                                      * at their combined peaks and flattened the flutter).
                                      * hard rate clamp: what the
                                      * ear judges is the DERIVATIVE, so
                                      * bound it directly rather than
                                      * hand-tuning the shapes. */
static const int16_t wob_sin_q15[256] = {
	     0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
	  6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
	 12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
	 18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
	 23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
	 27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
	 30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
	 32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
	 32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
	 32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
	 30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
	 27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
	 23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
	 18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
	 12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
	  6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
	     0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
	 -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
	-12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
	-18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
	-23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
	-27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
	-30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
	-32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
	-32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
	-32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
	-30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
	-27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
	-23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
	-18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
	-12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
	 -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};
static inline int32_t wob_sin(uint32_t ph)
{
	uint32_t idx = ph >> 24, fr = (ph >> 8) & 0xFFFFu;
	int32_t a = wob_sin_q15[idx], b = wob_sin_q15[(idx + 1u) & 255u];
	return a + (int32_t)(((int64_t)(b - a) * (int32_t)fr) >> 16);
}
/* Pade tanh(u) ~= u(27+u^2)/(27+9u^2), u in Q12. int32 THROUGHOUT and it
 * is proven, not hoped: |u| <= 12288 and n <= 147456, so |u*n| <= 1.812e9,
 * inside 2^31. Clamped at |u| = 3 because past the triple root the curve
 * climbs above 1 -- the drive knob would start EXPANDING. */
static inline int32_t tp_tanh_q12(int32_t u)
{
	if (u >=  (3 << 12)) return  4096;
	if (u <= -(3 << 12)) return -4096;
	int32_t u2 = (u * u) >> 12;   /* FXCOST-578: |u|<=12288 -> 1.5e8, int32 */
	int32_t n = 110592 + u2, d = 110592 + 9 * u2;
	int32_t num = u * n, half = d >> 1;
	return (num >= 0 ? num + half : num - half) / d;
}
/* Kellet 3-pole pink. ROUNDED feedback shifts: an arithmetic >>15 rounds
 * toward -inf, and the 0.99765 pole has a DC gain near 425, so that half-
 * LSB bias integrates into a large standing offset. */
static inline int32_t tp_pink(int32_t *k0, int32_t *k1, int32_t *k2)
{
	g_tp_rng = g_tp_rng * 1664525u + 1013904223u;
	int32_t w = ((int32_t)((int16_t)(g_tp_rng >> 16)) + 16) >> 5;
	*k0 = (int32_t)((((int64_t)*k0 * 32690) + 16384) >> 15) + (((w * 3245) + 16384) >> 15);
	*k1 = (int32_t)((((int64_t)*k1 * 31555) + 16384) >> 15) + (((w * 9716) + 16384) >> 15);
	*k2 = (int32_t)((((int64_t)*k2 * 18677) + 16384) >> 15) + (((w * 17247) + 8192) >> 14);
	return *k0 + *k1 + *k2 + (((w * 6055) + 16384) >> 15);
}

/* ===== ECHO2-610 (E2): page 2 fader 4, a DEDICATED post-FX line with feedback =====
 * E1 (568/572) tapped the RECORD ring, so it echoed the jack only and could
 * not feed back -- the recorder owns that ring. E2 owns its own line: the
 * whole post-FX bus goes in, the repeats recirculate, and a bounce prints
 * them (the print IS the post-FX bus, BNC-597). 12 kHz mono int16, 384 ms:
 * 1/16, dotted 1/16 and 1/8 all fit down to 80 BPM, 1/16 to 40 BPM; longer
 * clamps to the line -- never silence (the 572 rule). Written as 4-frame
 * L+R averages (a boxcar-4 nulls at exactly 12 and 24 kHz, so nothing
 * aliases), read with linear interpolation (no held-sample images, W134).
 * The repeats are dark, ~-5 dB at 4 kHz per pass: the tape-echo character,
 * and what keeps feedback from building up harsh. Cleared on engage. */
static volatile uint8_t  g_ec_mix;   /* 0 = dry, and the kernel is skipped */
static volatile uint8_t  g_ec_div;   /* 0 = 1/16, 1 = dotted-1/16, 2 = 1/8 */
static volatile uint32_t g_ec_dly;   /* the COMPUTED delay, engine frames */
#define EC2_LINE 4544u               /* ECHOTRIM-746: was 4608 (384 ms); 378.7 ms still holds the 1/8 at 80 BPM (375 ms); 9,088 B */
static int16_t  g_ec2_line[EC2_LINE];
static uint32_t g_ec2_w;             /* line write index, 0..EC2_LINE-1 */
static uint8_t  g_ec2_live;          /* the line holds CURRENT audio (cleared on engage) */
static volatile uint8_t g_rv_mix;    /* REVERB-676: page 3 fader 4: 0 = dry and the kernel is skipped */
/* EQ-691: PAGE 5, the global EQ. g_eq_g[b] = fader (128 = flat, A1 deadband);
 * g_eq_k[b] = the running gain (Q8, 0 = flat) the chain ramps toward the fader;
 * g_eq_live = the wrapper must run the page-5 group (set by the controls thread
 * on any write, cleared by the chain once every band has landed flat). */
static volatile uint8_t g_eq_g[4] = { 128u, 128u, 128u, 128u };
static volatile uint8_t g_eq_live;
/* SEC-695: the secondary layer. g_sec[page-1][lane], pages 1-4; the defaults
 * reproduce the kernels as they were before the layer existed. */
#define SEC_DEF { { 128u, 128u, 128u, 128u }, { 128u, 128u, 128u, 166u }, { 128u, 128u, 128u, 179u }, { 128u, 128u, 128u, 128u } }
static volatile uint8_t g_sec[4][4] = SEC_DEF;
/* SHAPE-696: the THIRD control -- hold TN + the fader to its right. g_sec2[page-1][lane];
 * wired: page 3 lane 3 (tremolo) = stereo spread (default 0 = mono tremolo). */
static volatile uint8_t g_sec2[4][4];
static volatile uint8_t g_sec_led;   /* 1..4 = that lane's LED shows its secondary (a hold is on) */
static volatile uint8_t g_sec_led2;  /* SHAPE-696: 1 = the LED shows the third control instead */
static void sec_reset(void)
{
	static const uint8_t _d[4][4] = SEC_DEF;
	for (int _p = 0; _p < 4; _p++) for (int _l = 0; _l < 4; _l++) { g_sec[_p][_l] = _d[_p][_l]; g_sec2[_p][_l] = 0u; }
}
/* SHAPE-696: the tremolo's shape morph on a 0..255 triangle. m < 128: blend toward
 * smoothstep (a sine-like S); m > 128: expand the centre and clamp (toward a square). */
static inline __attribute__((always_inline)) int32_t trm_shape(int32_t tt, int32_t m)
{
	if (m < 128) {
		int32_t sm = (tt * tt * (768 - 2 * tt)) >> 16;   /* 3t^2 - 2t^3, 0..255 */
		return tt + (((sm - tt) * (128 - m)) >> 7);
	} else {
		int32_t d = ((tt - 128) * (256 + (m - 128) * 14)) >> 8;   /* x1 .. x7.9: triangle -> trapezoid -> square */
		if (d > 127) d = 127; else if (d < -128) d = -128;
		return d + 128;
	}
}
static int32_t g_eq_k[4];
static int32_t g_eq_lo[4][2], g_eq_bp[4][2];   /* SVF state per band, L/R */
static const int32_t eq_f_q30[4] = { 213 << 16, 750 << 16, 3406 << 16, 6680 << 16 };   /* EQ2-692: Q30 = Q14 << 16; shelves 1 - exp(-2 pi fc / 48k) at 100 / 4000 Hz, peaks 2 sin(pi fc / 48k) at 350 / 1600 Hz */
static inline __attribute__((always_inline)) int32_t eq_k_from(uint32_t v)
{
	if (v >= 120u && v <= 136u) return 0;
	int32_t d = (int32_t)v - 128;
	return (d > 0) ? d * 4 : (d * 3) / 2;   /* +508 (+9.5 dB) .. -192 (-12 dB), Q8 */
}
/* EQ2-692: (a * b) >> 32 is ONE smmul on the M4; the operand << 2 restores the
 * Q14 step exactly (d < 2^22 on the bus, no overflow). Always inlined (W-677). */
#define EQ_STEP(fq30, d) ((int32_t)(((int64_t)(fq30) * ((int32_t)(d) << 2)) >> 32))
/* the SHELF pair on one channel: band 0 (low, one pole lp) + band 3 (high, one pole hp) */
static inline __attribute__((always_inline)) void eq_shelf_pair(int32_t *m, int32_t *st0, int32_t *st3, int32_t k0, int32_t k3)
{
	const int32_t f0 = eq_f_q30[0], f3 = eq_f_q30[3];
	int32_t lo0 = *st0, lo3 = *st3;
	/* CPU-781 C8: a band at gain 0 keeps its state and drops its output term -- three loops, one chosen per block */
	if (k0 == 0) {
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo0 += EQ_STEP(f0, x - lo0);
			lo3 += EQ_STEP(f3, x - lo3);
			m[f] = x + ((((x - lo3) >> 2) * k3) >> 6);
		}
	} else if (k3 == 0) {
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo0 += EQ_STEP(f0, x - lo0);
			const int32_t x1 = x + (((lo0 >> 2) * k0) >> 6);
			lo3 += EQ_STEP(f3, x1 - lo3);
			m[f] = x1;
		}
	} else {
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo0 += EQ_STEP(f0, x - lo0);
			const int32_t x1 = x + (((lo0 >> 2) * k0) >> 6);   /* cascade, as 691 */
			lo3 += EQ_STEP(f3, x1 - lo3);
			m[f] = x1 + ((((x1 - lo3) >> 2) * k3) >> 6);
		}
	}
	*st0 = lo0; *st3 = lo3;
}
/* the PEAK pair on one channel: bands 1 + 2, the M63a Chamberlin SVF, bandpass out */
static inline __attribute__((always_inline)) void eq_peak_pair(int32_t *m, int32_t *lo1p, int32_t *bp1p, int32_t *lo2p, int32_t *bp2p, int32_t k1, int32_t k2)
{
	const int32_t f1 = eq_f_q30[1], f2 = eq_f_q30[2];
	int32_t lo1 = *lo1p, bp1 = *bp1p, lo2 = *lo2p, bp2 = *bp2p;
	if (k1 == 0) {   /* CPU-781 C8 */
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo1 += EQ_STEP(f1, bp1);
			bp1 += EQ_STEP(f1, x - lo1 - bp1);
			lo2 += EQ_STEP(f2, bp2);
			bp2 += EQ_STEP(f2, x - lo2 - bp2);
			m[f] = x + (((bp2 >> 2) * k2) >> 6);
		}
	} else if (k2 == 0) {
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo1 += EQ_STEP(f1, bp1);
			bp1 += EQ_STEP(f1, x - lo1 - bp1);
			const int32_t x1 = x + (((bp1 >> 2) * k1) >> 6);
			lo2 += EQ_STEP(f2, bp2);
			bp2 += EQ_STEP(f2, x1 - lo2 - bp2);
			m[f] = x1;
		}
	} else {
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = m[f];
			lo1 += EQ_STEP(f1, bp1);
			bp1 += EQ_STEP(f1, x - lo1 - bp1);
			const int32_t x1 = x + (((bp1 >> 2) * k1) >> 6);   /* cascade, as 691 */
			lo2 += EQ_STEP(f2, bp2);
			bp2 += EQ_STEP(f2, x1 - lo2 - bp2);
			m[f] = x1 + (((bp2 >> 2) * k2) >> 6);
		}
	}
	*lo1p = lo1; *bp1p = bp1; *lo2p = lo2; *bp2p = bp2;
}

/* FXRST-563: ONE gesture puts every effect back to neutral.
 * Deliberately does NOT touch page 8 (mono/stereo) -- that is a record
 * setting, not an effect, and a panic button that silently changed how the
 * next take is encoded would be a trap.
 * Re-arming the pickup latch matters as much as the values: after a reset
 * every fader is stale, and without this the next fader touch would jump
 * its parameter straight back up to wherever the fader happens to sit. */
static void fx_reset_all(void)
{
	pop_stamp(1u);   /* POPLOG-730 */
        g_flt_pos = 128u;   /* bypass, not zero -- the filter is bipolar */
        g_chr_mix = 0u;  g_dst_amt = 0u;  g_gat_amt = 0u;
        g_bcr_amt = 0u;  g_rng_amt = 0u;  g_awh_amt = 0u;
        g_phs_amt = 0u;  g_swp_amt = 0u;  g_trm_amt = 0u;
        /* RSTFIX-582 (W271): the reset predates the echo (568) and the tape page
         * (569); marc: "FN + T1 + T4 is resetting all the FX except page 4". */
        g_ec_mix = 0u;  g_ec_div = 0u;  g_rv_mix = 0u;   /* REVERB-676 */
        for (int _b = 0; _b < 4; _b++) g_eq_g[_b] = 128u;   /* EQ-691: flat; the chain ramps down */
        sec_reset();   /* SEC-695 */
        g_eq_live = 1u;
        g_tp_drive = 0u;  g_tp_tone = 128u;  g_tp_hiss = 128u;  g_tp_wob = 0u;   /* HISS2-701: the hiss rests at centre */
        /* STACKA-664: the reset also clears every tapped rate, division and type */
        for (int _l = 0; _l < 7; _l++) g_lane_per[_l] = 0u;   /* WOBTAP-675: 7 lanes */
        g_lfo_div[0] = 0u; g_lfo_div[1] = 0u; g_lfo_div[2] = 2u;
        g_dst_typ = 0u;
        for (int _r = 0; _r < 9; _r++) g_pg_route[_r] = RT_BOTH;   /* INFX-672 */
        g_mon_mute = 0u;
        for (int _f = 0; _f < 4; _f++) {
                g_fx_pick[_f]  = 1;
                g_fx_lastq[_f] = -1;
        }
}
static const int16_t flt_lp_tab[14] = {   /* 80 Hz .. 6.5 kHz, exp */
	172, 241, 337, 473, 664, 931, 1306, 1831,
	2566, 3595, 5033, 7032, 9788, 13524 };
static const int16_t flt_hp_tab[14] = {   /* 30 Hz .. 4.5 kHz, exp */
	64, 95, 139, 204, 301, 442, 650, 955,
	1404, 2064, 3032, 4451, 6520, 9512 };
static uint8_t           g_fh_latch[NTRK];   /* fader owes a volume re-cross */
static int               g_fh_lastq[NTRK];   /* last raw read while latched */
#define heads_engaged() (g_heads_mode && trk[g_head_src].state == TS_PLAY)
static volatile uint8_t  g_dip_req;                /* M10: controls -> mixer, declick dip at a chop edit */
static volatile uint8_t  g_off_fade;               /* M10: power-off fade — master to 0 and HOLD */
static volatile uint32_t g_beat_phase;            /* phase within a beat (loop samples), for LEDs */
static volatile int      g_emmc_ready;
static volatile int      g_dbg_beat;              /* current beat number (diag) */
static volatile int      g_dbg_btn = -1;          /* committed track button (diag) */
static uint64_t          g_sample_clock;          /* free-running I2S frames (idle metronome) */
static int64_t           g_dec_acc;
static int64_t           g_dec_accR;               /* S2CAP: R-channel decimator */
static volatile uint32_t g_rw_hw;                  /* S2CAP: rec-ring fill high-water (frames) */                /* live accumulator for record decimation (int64: cannot overflow when the transport is stopped / step rounds to 0) */
static uint32_t          g_frames_since;           /* I2S frames since the last loop-sample tick */
static uint32_t          g_pphase;                 /* Q16 playback phase */
static volatile uint32_t g_play_speed_q16 = 65536; /* tape speed when playing (Q16, 65536=1.0x); rocker sets */
static volatile uint8_t  g_fixed_len;              /* EFFECTIVE mode of the CURRENT song (M7c):
                                                    * 0 = variable (independent loop lengths),
                                                    * 1 = fixed (overdubs snap to track 1's base).
                                                    * = the song's recorded-with stamp when set,
                                                    * else the global preference below. */
static volatile uint8_t  g_mode_pref;              /* M7c: global working preference — what empty
                                                    * songs inherit. Toggling FUNCTION+PLAY on an
                                                    * EMPTY song sets this; on a RECORDED song it
                                                    * stamps that song only. Persisted in the
                                                    * index's fixed_len field. */
/* Tempo as an INTEGER BPM (rocker steps it 1 BPM per click for fine control).
 * Speed is derived exactly: speed = bpm * 65536 / LOOP_BPM_BASE, so 80 BPM is
 * exactly 1.0x — no detent/snap logic needed. Range 40..120 = 0.5x..1.5x. */
#define BPM_MIN 20    /* RANGE-655: 0.25x (was 40 = 0.5x) */
#define BPM_MAX 120   /* CAP-665: 1.5x again (RANGE-655's 160 = 2x pended until the corner-degradation decision); BPM_MIN stays 20 = 0.25x */
static volatile int g_play_bpm = 80;
/* auto-start thresholds (loop-sample domain @ LOOP_RATE) */
#define SOUND_THRESHOLD  1000              /* int16 level (~ -30 dBFS) */
#define SOUND_WAIT_TICKS (LOOP_RATE * 4u)  /* ~4 s fallback */
/* PERFECT-LOOP R2: the stop gesture's CONSTANT pipeline latency — ladder
 * debounce (~24 ms) + sustained-commit gate (~24 ms) + control pass (~8 ms)
 * ~= 55 ms — backdated out of every take so the captured end lands where the
 * finger did, not where the pipeline noticed. */
#define STOP_COMP_SAMPLES 2600u             /* ~55 ms at 48 kHz */
/* M96: the stop gesture's latency is NOT constant. The debounce below
 * was PASS-COUNTED, and the control pass stretches when main is starved
 * (4.60%% CPU at the corner), so the real latency scales with load. We
 * now MEASURE it: g_stop_lat_ms is the wall time from first sighting of
 * a new ladder value to its commit. Used for the backdate and printed
 * as BTN, so the stretch is visible instead of merely felt. */
#define BTN_DEBOUNCE_MS   24
static volatile uint32_t g_stop_lat_ms;
static volatile uint32_t g_stop_lat_max;
/* M44 PRESS side of the same budget: ladder debounce (~24 ms) + control
 * pass (~8 ms) ~= 32 ms between skin-on-button and the committed press.
 * Added to the A-r2 press stamp so head recovery (M41) and the gridded
 * punch schedule reach the PHYSICAL touchdown, not the commit. */
#define PRESS_COMP_MS    32
/* track-button gesture timing */
#define HOLD_RECORD_MS   180   /* physical button-down this long (ms) => RECORD; shorter => TAP */
/* M44 INSTANT ARM: an EMPTY track arms at 48 ms — above the 40 ms
 * transit/graze bound (a finger sweeping to a higher button can commit
 * a lower band for ~24-32 ms; arming on that would steal the take from
 * the button actually pressed), below any real tap's finger-down time.
 * A tap means nothing on an empty track, so there is nothing else to
 * disambiguate. Fresh-tapped-grid empty keeps 0 (A-r2). */
#define EMPTY_ARM_MS     48
#define DTAP_GAP_MS      600   /* DELFIX-762: was 420 -- 2nd tap within this of the 1st tap's release => DOUBLE-TAP */
#define DTAP_DEL_HOLD_MS 400   /* GS-531 (map v2 row 99): the 2nd tap HELD this long = DELETE.
                                * A QUICK 2nd tap = the mono/stereo record toggle -- the
                                * destructive gesture gets the dwell, the safe one the tap. */

/* BEAT GRID for the LED pulse + MIDI clock — defaults to the nominal beat, but
 * the first-track TEMPO ESTIMATOR replaces it with the detected beat period so
 * the lights/clock track the music. It does NOT change playback speed/pitch
 * (the rocker still does tape varispeed); it's the metronome grid only. */
static volatile uint32_t g_beat_samples = BEAT_SAMPLES_L;
static volatile int      g_det_bpm;       /* diag: last detected BPM (0 = none) */
/* PRECOMPUTED MIDI-clock divisor: loop-samples per 24-PPQN tick = g_beat_samples/24.
 * Recomputed ONLY when the tempo is (re)detected, NOT per audio sample -- so the
 * detected tempo costs one divide once, not a runtime divide 48000x/sec on every
 * track (that per-sample divide was a big part of why this build lost v2's
 * headroom). The per-sample path just runs a cheap counter (g_midi_cnt). */
static volatile uint32_t g_midi_div = (BEAT_SAMPLES_L + 12u) / 24u;
static uint32_t          g_midi_cnt;      /* counts loop-samples toward the next MIDI tick */

/* Lightweight integer onset/tempo estimator, run only over the FIRST take of an
 * empty song. Envelope follower flags onsets (energy past half the running
 * peak); the median inter-onset gap is the beat period, folded to a musical
 * range. No FFT. */
#define TEMPO_MAX_ONSETS 48u
static struct {
	int      active;
	int32_t  env;
	int32_t  peak;
	int      above;
	uint32_t first_onset;
	uint32_t last_onset;
	uint32_t ioi[TEMPO_MAX_ONSETS];
	uint32_t n;
} g_tempo;
static void tempo_reset(void)
{
	memset((void *)&g_tempo, 0, sizeof(g_tempo));
	g_tempo.active = 1;
}
static inline void tempo_feed(int16_t sv, uint32_t pos)
{
	if (!g_tempo.active) return;
	int32_t a = sv < 0 ? -sv : sv;
	g_tempo.env += (a - g_tempo.env) >> 6;
	if (g_tempo.env > g_tempo.peak) g_tempo.peak = g_tempo.env;
	int32_t thr = g_tempo.peak >> 1;
	if (!g_tempo.above && g_tempo.env > thr && thr > 200) {
		g_tempo.above = 1;
		/* M42 (row 81): advance the IOI reference ONLY on a beat-scale
		 * gap. Near-sine hits sag the envelope between half-cycles and
		 * fire 2-3 crossings per hit; the <1/8 s gaps were already
		 * dropped, but the reference still moved to the LAST SPUTTER,
		 * so every real gap measured short — a uniform ~1-3% bias the
		 * agreement guards could not see (grid retuned after a gridded
		 * take; sim-reproduced on the narrowband corpus). Rolls and
		 * fast subdivisions now accumulate into beat-multiples instead,
		 * which the musical fold already handles. */
		if (g_tempo.last_onset) {
			uint32_t d = pos - g_tempo.last_onset;
			if (d > LOOP_RATE / 8u) {
				if (g_tempo.n < TEMPO_MAX_ONSETS)
					g_tempo.ioi[g_tempo.n++] = d;
				g_tempo.last_onset = pos;
			}
		} else {
			g_tempo.last_onset = pos;
		}
		if (!g_tempo.first_onset) g_tempo.first_onset = pos ? pos : 1u;
	} else if (g_tempo.above && g_tempo.env < (thr * 3 >> 2)) {
		g_tempo.above = 0;
	}
}
static void tempo_finish(void)
{
	g_tempo.active = 0;
	if (g_tempo.n < 2u) return;
	for (uint32_t i = 1; i < g_tempo.n; i++) {
		uint32_t v = g_tempo.ioi[i]; int j = (int)i - 1;
		while (j >= 0 && g_tempo.ioi[j] > v) { g_tempo.ioi[j + 1] = g_tempo.ioi[j]; j--; }
		g_tempo.ioi[j + 1] = v;
	}
	uint32_t beat = g_tempo.ioi[g_tempo.n / 2];
	uint32_t lo = (uint32_t)((uint64_t)LOOP_RATE * 60u / 176u);
	uint32_t hi = (uint32_t)((uint64_t)LOOP_RATE * 60u / 70u);
	while (beat > hi) beat >>= 1;
	while (beat && beat < lo) beat <<= 1;
	if (beat < lo || beat > hi) return;
	g_beat_samples = beat;
	g_midi_div = (beat + 12u) / 24u;          /* precompute once: no per-sample divide */
	g_det_bpm = (int)(((uint64_t)LOOP_RATE * 60u + beat / 2u) / beat);
}
/* M20 F8: CONTENT-DERIVED TEMPO REFINEMENT — the loop learns the source's real
 * tempo from the audio it just recorded. The onset estimator above has always
 * run through every first take (tempo_reset at the punch, tempo_feed per
 * sample); on a TAPPED grid its answer was simply discarded. But human tapping
 * lands ~0.2-1% off, and that error is exactly what walks a loop off the track
 * it was recorded from (marc's bench: a 0.27% error = 1.3 ms per beat = 160 ms
 * of slide per minute). The content knows better: measure the span between the
 * first and last onset, work out how many half-beats it covers using the tap as
 * the hypothesis (it is close enough to make that unambiguous), and divide.
 *
 * Guards, so this can only ever help: at least 4 onsets; span >= 2 beats;
 * the span is CAPPED at 24 beats so that miscounting the beats by one always
 * lands >2% away and gets rejected; and the refined beat must sit within 2% of
 * the tap. Anything ambiguous (pads, drones, rubato) keeps the tapped value. */
static uint32_t tempo_span(uint32_t cap)
{
	uint32_t acc = 0u, span = 0u;
	for (uint32_t i = 0; i < g_tempo.n; i++) {
		if (acc + g_tempo.ioi[i] > cap) break;
		acc += g_tempo.ioi[i];
		span = acc;
	}
	return span;
}
/* Median gap between onsets. Sorts the list, so it must be the LAST thing to
 * read it (the span walk above needs chronological order). */
static uint32_t tempo_median_ioi(void)
{
	if (g_tempo.n < 2u) return 0u;
	for (uint32_t i = 1; i < g_tempo.n; i++) {
		uint32_t v = g_tempo.ioi[i]; int j = (int)i - 1;
		while (j >= 0 && g_tempo.ioi[j] > v) {
			g_tempo.ioi[j + 1] = g_tempo.ioi[j]; j--;
		}
		g_tempo.ioi[j + 1] = v;
	}
	return g_tempo.ioi[g_tempo.n / 2u];
}
static uint32_t tempo_refine(uint32_t bs)
{
	/* PAD-594 / PADHOST-715 (W287): the mixer's 32-byte line; the build sets the count. */
	__asm__ volatile(".rept 4\n\tnop\n\t.endr");
	if (!bs || g_tempo.n < 4u) return 0u;
	if (!g_tempo.first_onset || g_tempo.last_onset <= g_tempo.first_onset)
		return 0u;
	/* M42 GATES: refinement can move a TAPPED grid, so the content must
	 * actually be discrete, regular onsets. The old code got that
	 * protection BY ACCIDENT — retrigger-corrupted gap lists failed the
	 * span checks. With true references (M42) a wobbling pad logs gaps
	 * pinned just above the 1/8 s floor, perfectly regular, plausible
	 * enough to move the tap 4%+ (caught in offline sim). Deliberate now:
	 *  - FLOOR CLEARANCE: median gap > 1.5x the floor — continuous
	 *    envelope wobble piles up at floor+eps, real onsets do not;
	 *  - REGULARITY: at least half the gaps within 12.5% of their
	 *    median — rubato and mixed material keep the tap. */
	{
		uint32_t tmp[TEMPO_MAX_ONSETS];
		memcpy(tmp, g_tempo.ioi, g_tempo.n * sizeof(tmp[0]));
		for (uint32_t i = 1; i < g_tempo.n; i++) {
			uint32_t v = tmp[i]; int j = (int)i - 1;
			while (j >= 0 && tmp[j] > v) {
				tmp[j + 1] = tmp[j]; j--;
			}
			tmp[j + 1] = v;
		}
		uint32_t med = tmp[g_tempo.n / 2u];
		if (med * 2u <= (LOOP_RATE / 8u) * 3u) return 0u;
		uint32_t good = 0;
		for (uint32_t i = 0; i < g_tempo.n; i++) {
			uint32_t d = (g_tempo.ioi[i] > med)
				   ? g_tempo.ioi[i] - med : med - g_tempo.ioi[i];
			if (d * 8u <= med) good++;
		}
		if (good * 2u < g_tempo.n) return 0u;
	}
	/* STAGE 1 — COARSE, over a SHORT span (<=4 beats). Counting half-beats
	 * here needs the hypothesis only to be better than ~6%, so even a badly
	 * tapped grid (marc's bench had one 3% out) still counts correctly. */
	uint32_t s1 = tempo_span(bs * 4u);
	if (s1 < bs * 2u) return 0u;
	uint32_t h1 = (uint32_t)(((uint64_t)s1 * 2u + bs / 2u) / bs);
	if (h1 < 4u) return 0u;
	uint32_t r1 = (uint32_t)(((uint64_t)s1 * 2u + h1 / 2u) / h1);
	uint32_t d1 = (r1 > bs) ? (r1 - bs) : (bs - r1);
	if ((uint64_t)d1 * 20u > (uint64_t)bs) return 0u;  /* >5% from the tap:
	                                                    * not the same tempo,
	                                                    * and past the count-
	                                                    * safety limit — keep
	                                                    * what was tapped */
	/* STAGE 2 — FINE, over the long span, counted with STAGE 1 (~0.15%
	 * accurate) as the hypothesis instead of the hand. A miscount here would
	 * need >1% of hypothesis error, so it cannot happen; the 24-beat cap plus
	 * the 2% agreement check make it safe twice over. Precision scales with
	 * the span: ~0.06% on percussive material. */
	uint32_t out = r1;
	uint32_t s2 = tempo_span(r1 * 24u);
	if (s2 >= r1 * 4u) {
		uint32_t h2 = (uint32_t)(((uint64_t)s2 * 2u + r1 / 2u) / r1);
		if (h2 >= 8u) {
			uint32_t r2 = (uint32_t)(((uint64_t)s2 * 2u + h2 / 2u) / h2);
			uint32_t d2 = (r2 > r1) ? (r2 - r1) : (r1 - r2);
			if ((uint64_t)d2 * 50u <= (uint64_t)r1) out = r2;
		}
	}
	/* SUBDIVISION SANITY. Both stages can share one mistake: if the onset
	 * that ends a measuring window sits on a TRIPLET or swung subdivision,
	 * the half-beat count lands wrong and the two stages agree with each
	 * other on the wrong answer (~5% out). So cross-check against a
	 * statistic that does not depend on the count at all — the typical gap
	 * between hits must be a simple fraction or multiple of the result
	 * (1/4, 1/3, 1/2, 1, 2, 3, 4). Straight material passes trivially; a
	 * triplet-corrupted answer misses every ratio and the tapped tempo is
	 * kept instead. */
	{
		uint32_t med = tempo_median_ioi();
		if (med) {
			static const uint8_t rn[7] = { 1u, 1u, 1u, 1u, 2u, 3u, 4u };
			static const uint8_t rd[7] = { 4u, 3u, 2u, 1u, 1u, 1u, 1u };
			int ok = 0;
			for (int k = 0; k < 7; k++) {
				uint32_t want = (uint32_t)(((uint64_t)out * rn[k] +
							    rd[k] / 2u) / rd[k]);
				if (!want) continue;
				uint32_t dm = (med > want) ? (med - want) : (want - med);
				if ((uint64_t)dm * 33u <= (uint64_t)want) { ok = 1; break; }
			}
			if (!ok) return 0u;   /* nothing musical fits: keep the tap */
		}
	}
	return out;
}

/* Apply a refined beat to the SONG GRID as well, so the metronome, the MIDI
 * clock and every later punch follow the corrected tempo instead of the tapped
 * one (otherwise overdubs would quantize to a grid the base loop no longer
 * agrees with). Phase is untouched — only the spacing changes. */
/* Forward tentative declaration: the smoothed tape speed is defined further
 * down (audio thread only) but beat_set has to derive the recording beat from
 * it, and it must be the SAME variable the punch uses — g_play_speed_q16 is
 * the rocker's setting, not the settled value, and picking a different one
 * here would reintroduce exactly the inconsistency this change removes. */
static uint32_t g_cur_speed_q16;

/* M25-r7 THE ONE OWNER. The beat lived in three variables written from twelve
 * places, tied together only by a convention asserted once at the punch and
 * never re-checked. That is how the loop and the grid ended up on tempos 0.48%
 * apart (rf=22612 vs bf=22504) and walked the first take ~290 ms/min away from
 * everything recorded after it — a divergence no read of the code could
 * explain, because nothing in the code forbade it.
 * Now: every mechanism PROPOSES a beat in grid-domain frames and calls this.
 * This is the only writer. The grid/recording identity is structural instead of
 * conventional, so the divergence is no longer expressible. */
static void beat_set(uint32_t nf)
{
	if (!nf) return;
	g_grid_beat_frames = nf;
	/* recording domain follows the tape, exactly as the punch derived it */
	uint32_t rs = (uint32_t)(((uint64_t)nf * g_cur_speed_q16) >> 16);
	if (!rs) rs = nf;
	g_gridrec_beat_samps = rs;
	g_beat_samples       = rs;
	g_midi_div           = rs / 24u;
	if (g_slot < NUM_SLOTS) {
		g_grid_bpm_q8[g_slot] = (uint16_t)((48000ULL * 60u * 256u) / nf);
		g_grid_save_req = 1;
	}
}

/* Convert a RECORDING-domain beat to grid domain and hand it to beat_set. The
 * three estimator paths (refine, achieved-length, convergence) all measure in
 * stored samples, so they come through here; the snap already has a grid-domain
 * number and calls beat_set directly. Same ratio maths as before — but it no
 * longer WRITES anything, so it can no longer preserve a divergence. */
static void grid_retune(uint32_t old_bs, uint32_t new_bs)
{
	if (!old_bs || !new_bs || !g_grid_beat_frames) return;
	beat_set((uint32_t)(((uint64_t)g_grid_beat_frames * new_bs +
			     old_bs / 2u) / old_bs));
	g_grid_next_tick = g_sample_clock;
}

/* Boot STOPPED (no auto-play): the saved song loads paused; PLAY (tap=resume,
 * hold=from the top) or recording starts the tape. The device used to blast the
 * last loop the instant it powered up — annoying after a flash or plug-in. */
static volatile uint8_t  g_playing = 0;            /* PLAY/STOP: target speed ramps to 0 when stopped */
static uint32_t          g_cur_speed_q16 = 0;      /* smoothed actual speed Q16 (audio thread only) */
static volatile int      g_midi_stop_pending;      /* send MIDI Stop on pause */
/* 24-PPQN clock: SINGLE-WRITER counters (audio produces, midi consumes its own
 * count). A shared pending counter with ++/-- from two threads loses pulses on
 * ARM (volatile is not atomic), drifting any synced external gear. */
static volatile uint32_t g_midi_clk_produced;      /* audio thread writes ONLY */
static volatile int      g_midi_start_pending;     /* send MIDI Start on loop activation */

static inline int16_t clamp16(int32_t x)
{
	if (x > 32767) return 32767;
	if (x < -32768) return -32768;
	return (int16_t)x;
}

/* SOFT LIMITER for the mix bus: 4 tracks at unity + the live monitor easily sum
 * past full-scale, and a hard clamp turns every peak into harsh square-wave
 * crunch ("bit-crushing" / distortion when channels stack). Below TH the signal
 * is untouched; above it the excess is compressed along a hyperbolic knee that
 * asymptotes to full-scale, so loud sums round off smoothly instead of clipping.
 * Integer, branch-light, ~no cost. */
/* SLINL-590 (W280): 589's disassembly shows the lean loop calling this OUT OF
 * LINE twice per frame (bl soft_limit) -- GCC's -Os inliner changed its mind
 * once the mixer shrank. ~50 cycles x 256 frames = the +0.23 ms/block PASS C
 * regression measured on 589. 585 and every bin before it had it inlined. */
static inline __attribute__((always_inline)) int16_t soft_limit(int32_t x)
{
	const int32_t TH = 26000;        /* ~0.8 FS linear region */
	const int32_t HEAD = 32767 - TH; /* room above the knee   */
	int32_t s = (x < 0) ? -1 : 1;
	int32_t a = x * s;               /* |x| */
	if (a > TH) {
		int32_t over = a - TH;       /* compress: y = TH + HEAD*over/(over+HEAD) */
		/* M95-C: the int64 form compiles to bl __aeabi_uldivmod, called
		 * 2x per output frame -- up to 512 library divisions per block on
		 * loud material. HEAD is 6767, so HEAD*over stays under 2^31 for
		 * over < 317346; below that the whole expression fits in uint32
		 * and becomes ONE hardware udiv. The 64-bit arm is kept so this
		 * is bit-exact for every input, not merely for reachable ones:
		 * clamping instead of branching diverges from |x| = 344002 up.
		 * Verified 0 differing samples over +/-2,000,000. Reachable max
		 * over = 137835 (4 tracks + monitor, full scale) -- 2.3x inside. */
		if (over < 317000)
			a = TH + (int32_t)(((uint32_t)HEAD * (uint32_t)over) / (uint32_t)(over + HEAD));
		else
			a = TH + (int32_t)(((int64_t)HEAD * over) / (over + HEAD));
	}
	return (int16_t)(s * a);         /* a <= 32767 by construction */
}
/* CPU-780: floor(f * v / 2^14) as ONE high-word multiply (smmul): fq17 = f << 17 (f < 2^14), v << 1
 * (|v| < 2^30 -- the bus and the SVF states are far inside). Bit-exact vs (int64)f * v >> 14: proven
 * on the host over 30,000 random blocks per kernel (reference/cpu780_kernel_harness.c). */
#define CPU780_MULH14(fq17, v) ((int32_t)(((int64_t)(fq17) * (int32_t)((uint32_t)(v) << 1)) >> 32))
/* mix-only -O2: the audio hot path. Safe here (unlike global -O2): the two signed-
 * overflow UB sites are fixed with int64 casts, -fno-strict-aliasing is global, and
 * this function contains NO flash-write code -- same per-function -O2 already proven
 * werr-safe on the eMMC read path. Speeds the per-frame interp/volume/limit work. */
/* M85-r2: the rec-write chain, machine-extracted from the tree
 * (M84) with a MECHANICAL field-pointer transform. Do not edit
 * by hand; r1 hand-transcription broke the stereo ring store. */
__attribute__((optimize("O2"), noinline))
static void rec_write_sample(int16_t lsamp, int16_t rsamp)
{
					struct looptrk *rt = &trk[g_rec_track];
					/* M85-r2: one address computation per field per CALL --
					 * struct looptrk is 32,840 B with control fields past
					 * +0x8000 (beyond Thumb-2 imm12); naive access pays
					 * movw+mla per field per sample (M84 disasm). Volatile
					 * semantics, store order, per-sample publish: UNCHANGED. */
					/* RECW-711: each counter is read ONCE (they are volatile for the
					 * streamer / controls readers; nothing else can write them inside
					 * the mixer), the two the recorder advances are stored once. */
					uint32_t _rw = rt->r_w, _rc = rt->rec_count;
					const uint32_t _rr = rt->r_r, _tg = rt->rec_target;
					if ((_rw - _rr) >= (RRING_SAMPLES * 2u))  /* CD-463: 2x engine capacity */
						g_rec_overruns++;   /* take corrupting: flush too slow */
					int16_t wsamp = lsamp;
					int16_t wsampR = rsamp;
					if (rt->rec_silence) {
						uint8_t fg = rt->rec_fade;
						if (fg) {
							wsamp  = (int16_t)(((int32_t)lsamp * fg) >> 7);
							wsampR = (int16_t)(((int32_t)rsamp * fg) >> 7);
							uint8_t st = rt->rec_fstep ? rt->rec_fstep : 1u;
							rt->rec_fade = (fg > st) ? (uint8_t)(fg - st) : 0u;
						} else {
							wsamp = 0; wsampR = 0;
						}
					} else if (_tg) {
						/* run-to-the-line: SEAMX-727 -- the final 128 emissions
						 * (~5 ms) CROSSFADE INTO THE TAKE'S OWN HEAD, so the wrap
						 * lands on head[0] (was: a fade to zero = a dip a lap, a
						 * stutter on a bounce of a continuous bus). */
						uint32_t rem = _tg - _rc;
						if (rem <= 128u && _tg >= 256u) {
							const uint32_t _hk = (128u - rem) >> 1;
							wsamp  = (int16_t)(((int32_t)lsamp * (int32_t)rem +
									   (int32_t)g_seam_head[_hk][0] * (int32_t)(128u - rem)) >> 7);
							wsampR = (int16_t)(((int32_t)rsamp * (int32_t)rem +
									   (int32_t)g_seam_head[_hk][1] * (int32_t)(128u - rem)) >> 7); }
					}
					{ /* CD-463: 24k store — boxcar pairs; counters stay engine-based */
					  if ((_rw & 1u) == 0u) { g_cd_holdL = wsamp; g_cd_holdR = wsampR; }
					  else { uint32_t _fi = ((_rw >> 1) & RRING_MASK);
					    const int16_t _pL = (int16_t)(((int32_t)g_cd_holdL + (int32_t)wsamp  + 1) >> 1);
					    const int16_t _pR = (int16_t)(((int32_t)g_cd_holdR + (int32_t)wsampR + 1) >> 1);
					    g_rring[_fi * 2u]      = _pL;
					    g_rring[_fi * 2u + 1u] = _pR;
					    if (_rc < 128u) { g_seam_head[_rc >> 1][0] = _pL; g_seam_head[_rc >> 1][1] = _pR; }   /* SEAMX-727: the head */ } }
					_rw++; _rc++;
					__asm__ volatile("" ::: "memory");   /* RECW-711: the ring store lands before r_w moves */
					rt->r_w = _rw; rt->rec_count = _rc;
					{	/* STACKT-716: the preset take length -- the same stop the finger
						 * raises, one block early, the backdate pinned to its floor
						 * (the trigger point adds it back): the stop path's nearest-beat
						 * / run-on rounding lands the take on the line. */
						const uint32_t _ta = g_take_auto_at;
						if (_ta && _rc + BLK_FRAMES >= _ta + STOP_COMP_SAMPLES && !g_stop_req) {
							g_take_auto_at = 0u; g_stop_lat_ms = 0u; g_stop_req = 1;
						}
					}
					{ uint32_t _bl = _rw - _rr;   /* S2CAP meter */
					  if (_bl > g_rw_hw) g_rw_hw = _bl; }
					/* RP: the per-sample pre-roll frontier store is DELETED
					 * 72,000/s. Its only reader is at block START and the
					 * block TAIL republishes that global regardless,
					 * so nothing could ever observe it. (It also means
					 * "pre-roll follows the take" has not worked since
					 * M90 -- a real bug, logged, NOT fixed here.) */
					if (g_tempo.active) tempo_feed(lsamp, _rc);
					if (_tg == 0u) {
						/* OPEN take (first take AND independent overdubs):
						 * force-stop at the maximum length. Only a FIRST
						 * take defines the song grid/BPM. */
						if (_rc >= MAX_LOOP_SAMPLES) {
							if (g_loop_len == 0u) {
								g_loop_len = MAX_LOOP_SAMPLES;
								g_loop_blocks = (g_loop_len + SAMP_PER_BLK / 2u) / SAMP_PER_BLK;
								tempo_finish();
								if (g_slot < NUM_SLOTS) {
									g_meta.slot[g_slot].loop_len = g_loop_len;
									g_meta_save_req = 1;
								}
							}
							rt->rec_target = MAX_LOOP_SAMPLES;
							rt->len_blocks = MAX_LOOP_BLOCKS;
							rt->len_samps = MAX_LOOP_SAMPLES;
							rt->content_blocks = MAX_LOOP_BLOCKS;  /* all content */
							rt->state = TS_DONE; g_rec_track = -1;
							g_done_pending = 1;   /* M20: ring busy */
						}
					} else if (_rc >= _tg) {
						/* Take FINALIZE: rec_target is set by the stop tap
						 * (free-length, block-rounded) — the recorder pads
						 * the sub-block remainder with silence and lands
						 * here. The take now loops at its own length. */
						rt->state = TS_DONE; g_rec_track = -1;
							g_done_pending = 1;   /* M20: ring busy */
					}
}
/* ==== EFXM2-588: PASS C effect chain, OUT of the mixer (W280) ====
 * Everything between the master-gain envelope and the lean loop that
 * belongs to an effect: the per-block constants, the ramps, the trance
 * gate clock, the echo taps, and the 14 effect-major passes (EFXM-586,
 * bit-identical by harness). Pure code motion from the mixer. */
static void __attribute__((noinline)) wob_tick_block(void);                                 /* WOBBUS-673 */
/* ===== REVERB-676 (E1): the Clouds reverb, 12 kHz -- RVLINE-746: IN ITS OWN LINE (echo AND reverb) =====
 * Ten sub-lines end to end: 4 input diffusers, then per loop damping + 2 allpasses + a
 * delay. Lengths are Clouds' at 32 kHz scaled to 12 kHz and then x0.57 to fit 4,608. */
#define RV_N 10u
static const uint16_t g_rv_len[RV_N]  = { 28u, 41u, 60u, 100u, 414u, 510u, 855u, 480u, 418u, 1182u };   /* RV2-680: sum 4088 <= 4096 */
static const uint16_t g_rv_base[RV_N] = { 0u, 28u, 69u, 129u, 229u, 643u, 1153u, 2008u, 2488u, 2906u };
#define RV_MASK 4095u                 /* RV2-680: one window of the line, one pointer (Clouds' FxEngine) */
static int16_t  g_rv_line[RV_MASK + 1u];   /* RVLINE-746: the reverb's OWN line (8,192 B) -- echo and reverb run together now */
static uint32_t g_rv_w;              /* the shared write pointer, decrements per step */
static int32_t  g_rv_lp1, g_rv_lp2;  /* the two loops' damping states */
static int32_t  g_rv_pl, g_rv_pr;    /* last group's wet L/R (the interpolation's start) */
static uint8_t  g_rv_live;           /* the line holds the REVERB (cleared on engage) */
static inline __attribute__((always_inline)) int32_t rv_rd(uint32_t k) { return g_rv_line[(g_rv_w + g_rv_base[k] + g_rv_len[k] - 1u) & RV_MASK]; }   /* RVLINE-746 */
static inline __attribute__((always_inline)) void rv_wr(uint32_t k, int32_t v)
{
	if ((uint32_t)(v + 32768) > 65535u) { g_rv_clip++; v = (v > 32767) ? 32767 : -32768; }   /* POPS-700: count the clamp -- a clip inside the loop recirculates; CPU-780 C9: one unsigned compare */
	g_rv_line[(g_rv_w + g_rv_base[k]) & RV_MASK] = (int16_t)v;   /* RV2-680: clamp, not the knee (the input diffusers only, since 729) */
}
/* RVKNEE-729 (W327 mechanism 3, confirmed by the pop trap 09-08 20:34): the LOOP
 * stores take the knee. A hard clamp inside a 0.98 feedback loop pins the loop at
 * the rail and a sign flip of a pinned loop is rail to rail in one frame (the
 * huge pop: jmp=32767 next to rvclip=250,408). The knee keeps the loop continuous
 * while it saturates -- the break-up at the top stays a sound, not a click. */
static inline __attribute__((always_inline)) void rv_wrk(uint32_t k, int32_t v)
{
	if ((uint32_t)(v + 32768) > 65535u) g_rv_clip++;   /* still counted: the pressure is the diagnostic (CPU-780 C9: one unsigned compare) */
	g_rv_line[(g_rv_w + g_rv_base[k]) & RV_MASK] = soft_limit(v);
}
/* one 12 kHz step of the Clouds network: in = the mono sum, krt = loop feedback q8 */
static inline __attribute__((always_inline)) void rv_step(int32_t in, int32_t krt, int32_t klp, int32_t *oL, int32_t *oR)
{
	const int32_t kap = 160;   /* 0.625; klp (0.7 by default) is the damping secondary since SEC-695 */
	int32_t acc = in >> 2, t;   /* RV2-680: -12 dB into the network (x4 out) */
#define RV_AP(k, s) t = rv_rd(k); acc += ((s) * kap * t) >> 8; rv_wr(k, acc); acc = (((-(s)) * kap * acc) >> 8) + t;
#define RV_APK(k, s) t = rv_rd(k); acc += ((s) * kap * t) >> 8; rv_wrk(k, acc); acc = (((-(s)) * kap * acc) >> 8) + t;   /* RVKNEE-729: in the loop */
	RV_AP(0u, 1) RV_AP(1u, 1) RV_AP(2u, 1) RV_AP(3u, 1)
	const int32_t apout = acc;
	acc = apout + ((krt * rv_rd(9u)) >> 8);
	g_rv_lp1 += ((acc - g_rv_lp1) * klp) >> 8; acc = g_rv_lp1;
	RV_APK(4u, -1) RV_APK(5u, 1)
	rv_wrk(6u, acc); *oL = acc;
	acc = apout + ((krt * rv_rd(6u)) >> 8);
	g_rv_lp2 += ((acc - g_rv_lp2) * klp) >> 8; acc = g_rv_lp2;
	RV_APK(7u, 1) RV_APK(8u, -1)
	rv_wrk(9u, acc); *oR = acc;
	g_rv_w = (g_rv_w - 1u) & RV_MASK;   /* RV2-680 */
#undef RV_AP
#undef RV_APK
}
static void __attribute__((optimize("O2"), noinline)) in_wobble_block(int32_t *bL, int32_t *bR);
static void __attribute__((optimize("O2"), noinline)) fx_chain_run(int32_t *mix32, int32_t *mix32R, uint32_t pm)
{
	/* INFX-672: pm = the pages this call runs (bit p-1 = page p). Per-block
	 * ramps step once per BLOCK whatever the number of calls. */
	const int rp1 = (pm & 1u) ? 1 : 0, rp2 = (pm & 2u) ? 1 : 0;
	const int rp3 = (pm & 4u) ? 1 : 0, rp4 = (pm & 8u) ? 1 : 0;
	const int rp5 = (pm & 16u) ? 1 : 0;   /* EQ-691: the wrapper's BOTH call only */
	static uint32_t _infx_clk = 0xFFFFFFFFu;
	const int _first = (_infx_clk != (uint32_t)g_sample_clock);
	_infx_clk = (uint32_t)g_sample_clock;
		/* M17 DJ FILTER: mode + target coefficient from the fader once
		 * per block; the coefficient RAMPS toward its target (~40 ms
		 * across a big jump) so sweeps are zipperless. On a mode change
		 * (LP <-> bypass <-> HP) the state is reprimed against the live
		 * signal — the M11b pop lesson: never swap filter topology on
		 * stale integrator state. */
		static int32_t flt_lowL, flt_bandL, flt_lowR, flt_bandR, flt_f;  /* M63a: L/R SVFs */
		static uint8_t flt_mode;   /* 0 = bypass, 1 = LP, 2 = HP */
		{
			uint8_t fp = g_flt_pos;
			uint8_t nm; int32_t tf;
			if (fp < 112u)      { nm = 1; tf = flt_lp_tab[fp >> 3]; }
			else if (fp > 143u) { nm = 2; tf = flt_hp_tab[(fp - 144u) >> 3]; }
			else                { nm = 0; tf = 0; }
			if (nm != flt_mode) {
				flt_mode = nm;
				flt_f = tf;
				/* M25: a HIGH-pass must start TRANSPARENT — it has
				 * no accumulated low content yet — while a LOW-pass
				 * starts at the signal. Priming both to mix32[0]
				 * made hi = x - flt_low - flt_band come out at ~0,
				 * so every entry into HP ducked for a few ms. The
				 * worst case now is a low-corner HP briefly passing
				 * more bass than it should while the integrator
				 * catches up, which is what any analog high-pass
				 * does when you patch it in. */
				flt_lowL = (nm == 2u) ? 0 : mix32[0];
				flt_lowR = (nm == 2u) ? 0 : mix32R[0];
				flt_bandL = 0; flt_bandR = 0;
			} else if (_first && tf != flt_f) {
				int32_t fd = (tf - flt_f) >> 3;
				if (fd == 0) fd = (tf > flt_f) ? 1 : -1;
				flt_f += fd;
			}
		}
		/* DST-548: per-block drive/trim, RAMPED like the filter's
		 * coefficient so fader moves never zipper. dst_g 4096 = unity;
		 * full drive is ~8x in, with the trim taking ~6 dB back out. */
		/* FX2-558 per-block constants -- hoisted so the frame loop only
		 * branches on an int, never recomputes a shift table. */
		const uint32_t bcr_hold = (rp2 && g_bcr_amt)
		                        ? (1u + (((uint32_t)g_bcr_amt * 15u) >> 8)) : 0u;
		const int32_t  bcr_mask = (int32_t)(0xFFFFFFFFu <<
		                          (((uint32_t)g_bcr_amt * 8u) >> 8));
		/* STACKA-664 A3/A4: ONE beat for every clocked lane this block -- the grid's
		 * beat, else the tape BPM's, else 500 ms -- and a tapped period per lane
		 * that overrides it (0 = follow the beat). Read once, like every page value. */
		const uint32_t lane_bf = (g_grid_active && g_grid_beat_frames) ? g_grid_beat_frames
		                       : ((g_play_bpm > 0) ? (48000u * 60u / (uint32_t)g_play_bpm) : 24000u);
		const uint32_t lane_p0 = g_lane_per[0], lane_p1 = g_lane_per[1];
		const uint32_t lane_p2 = g_lane_per[2], lane_p3 = g_lane_per[3], lane_p4 = g_lane_per[4], lane_p5 = g_lane_per[5];
		const int32_t  swp_d    = (!rp3 || g_lfo_div[1] >= 3u) ? 0 : (int32_t)g_swp_amt;   /* 660: division 3 = OFF */
		const int32_t  trm_d    = (!rp3 || g_lfo_div[2] >= 3u) ? 0 : (int32_t)g_trm_amt;
		const int      fx2_lfo_on = (swp_d || trm_d) ? 1 : 0;
		/* PG8-560 FX3 per-block constants. */
		const int32_t  rng_d    = rp2 ? (int32_t)g_rng_amt : 0;
		const int32_t  awh_d    = rp2 ? (int32_t)g_awh_amt : 0;
		const int32_t  awh_q    = (g_sec[1][2] < 40u) ? 40 : (int32_t)g_sec[1][2];   /* SEC-695: damping q8 (default 128 = the old >> 1); floor 40 keeps the SVF stable at the top of the sweep */
		const int32_t  phs_d    = (!rp3 || g_lfo_div[0] >= 3u) ? 0 : (int32_t)g_phs_amt;   /* 660: division 3 = OFF */
		/* Ring carrier: ONE fader sets depth and pitch together, 40 Hz at the
		 * bottom to ~1.2 kHz at the top. Low = growl, high = clangorous metal.
		 * 2^32 / 48000 = 89478 phase units per Hz. */
		const uint32_t rng_inc  = 3579139u + (uint32_t)g_rng_amt * 406000u;
		/* ECHO2-610 per-block constants: the delay in LINE samples, the wet and
		 * feedback gains, and the engage edge. Decided once per block. */
		const int32_t  ec_mix = (!rp2 || g_ec_div >= 3u) ? 0 : (int32_t)g_ec_mix;   /* 660: the 4th tap state is OFF */
		int ec_n = 0;
		uint32_t ec2_d = 0u;             /* delay, 12 kHz line samples */
		int32_t  ec2_fb = 0, ec2_wet = 0;
		if (ec_mix) {
			/* the trance gate's clock, verbatim -- survives varispeed and
			 * locks the repeats to the takes. */
			uint32_t _ebf = (g_grid_active && g_grid_beat_frames)
			              ? g_grid_beat_frames
			              : ((g_play_bpm > 0)
			                 ? (48000u * 60u / (uint32_t)g_play_bpm) : 0u);
			if (!_ebf) _ebf = 24000u;      /* ungridded: 500 ms 'beat' */
			uint32_t _ed = (g_ec_div == 1u) ? ((_ebf * 3u) >> 3)   /* dotted 1/16 */
			             : (g_ec_div >= 2u) ? (_ebf >> 1)          /* 1/8 */
			                                : (_ebf >> 2);         /* 1/16 */
			if (lane_p1) {   /* A4: the tapped period IS the echo time; halve until it fits the line */
				_ed = lane_p1;
				while (_ed > ((EC2_LINE - 1u) << 2)) _ed >>= 1;
			}
			ec2_d = (_ed + 2u) >> 2;       /* engine frames -> line samples */
			/* NO TEMPO MAY SILENCE THE EFFECT (572): shorten to the line
			 * rather than drop out. 378.7 ms (ECHOTRIM-746) holds every division to 80 BPM. */
			if (ec2_d < 1u) ec2_d = 1u;
			if (ec2_d > EC2_LINE - 1u) ec2_d = EC2_LINE - 1u;
			g_ec_dly = ec2_d << 2;         /* publish what was ACTUALLY used, engine frames */
			/* one fader: wet tops out at -6 dB, feedback at 0.65 -- a sustained
			 * tone can build to ~1.4x dry and no further; the bus is int32 and
			 * the lean loop's limiter is downstream. */
			ec2_wet = ec_mix >> 1;
			ec2_fb  = (int32_t)g_sec[1][3]; if (ec2_fb > 250) ec2_fb = 250;   /* SEC-695: feedback is the secondary (default 166 = 0.65, as before) */
			if (!g_ec2_live) {
				/* engage edge: an old tail must never play back */
				memset(g_ec2_line, 0, sizeof(g_ec2_line));
				g_ec2_w = 0u;
				g_ec2_live = 1u;   /* RVLINE-746: the reverb keeps its own line */
			}
			ec_n = 1;
		} else if (rp2) {
			g_ec2_live = 0u;   /* INFX-672: only the pass that owns the echo may disengage it */
		}
		/* REVERB-676 per-block constants -- RVLINE-746: the reverb has its own line, so the echo no
		 * longer gates it; echo (page 2) into reverb (page 3), in series, whenever both are up. */
		const int32_t rv_mix = !rp3 ? 0 : (int32_t)g_rv_mix;
		int rv_n = 0;
		int32_t rv_krt = 0, rv_wet = 0;
		const int32_t rv_klp = (g_sec[2][3] < 32u) ? 32 : (g_sec[2][3] > 250u) ? 250 : (int32_t)g_sec[2][3];   /* SEC-695: damping (default 179 = 0.7) */
		/* EQ-691 per-block: ramp each band's gain toward its fader (16 Q8 steps a
		 * block, a full throw in ~170 ms); the group runs while any band is off flat
		 * or still landing, and hands the wrapper's gate back once all four are 0. */
		int eq_n = 0;
		if (rp5) {
			for (int _b = 0; _b < 4; _b++) {
				const int32_t _t = eq_k_from(g_eq_g[_b]);
				int32_t _k = g_eq_k[_b];
				if (_k < _t) { _k += 16; if (_k > _t) _k = _t; }
				else if (_k > _t) { _k -= 16; if (_k < _t) _k = _t; }
				g_eq_k[_b] = _k;
				if (_k != 0 || _t != 0) eq_n = 1;
			}
			if (!eq_n) g_eq_live = 0u;
		}
		if (rv_mix) {
			rv_krt = 128 + ((rv_mix * 122) >> 8);   /* 0.5 (a room) .. 0.98 (a hall that barely dies) */
			rv_wet = rv_mix >> 1;                    /* wet tops out at -6 dB, like the echo */
			if (!g_rv_live) {
				memset(g_rv_line, 0, sizeof(g_rv_line));   /* RVLINE-746: its own line */
				g_rv_w = 0u;   /* RV2-680 */
				g_rv_lp1 = 0; g_rv_lp2 = 0; g_rv_pl = 0; g_rv_pr = 0;
				g_rv_live = 1u;
			}
			rv_n = 1;
		} else if (rp3) {
			g_rv_live = 0u;
		}
		/* TAPE-569 per-block constants. */
		const int32_t tp_dr = 4096 + ((int32_t)g_tp_drive * 112);   /* 1.0x..7.97x */
		const int32_t tp_mk = (256 * 4096) / tp_dr;   /* makeup DERIVED from the
		                                               * drive, not tuned apart
		                                               * from it -- an independent
		                                               * curve gave ~3.95x of net
		                                               * gain at drive=1 and read
		                                               * as 'loud right off the
		                                               * bat'. */
		/* TUNE2-576: -12 dB. -38 dBFS at full was intrusive against quiet
		 * loops; real tape hiss sits ~-50 dB below peak. */
		/* HISS2-701: the hiss fader is BIPOLAR, 120..136 dead. Above: the cassette
		 * hiss, (h - 128) >> 2 -- 31 at the top, the same as 700's 255 >> 3 (TUNE3-577).
		 * Below: vinyl -- tp_ck is the depth (8..128); the hiss loop runs at a faint
		 * surface-bed level (2..6) under the clicks. */
		const uint32_t _tph  = rp4 ? (uint32_t)g_tp_hiss : 128u;
		const int32_t  tp_ck = (_tph < 120u) ? (int32_t)(128u - _tph) : 0;
		const int32_t  tp_hs = (_tph > 136u) ? (int32_t)((_tph - 128u) >> 2) : (tp_ck ? (2 + (tp_ck >> 5)) : 0);
		/* A1: the tone fader has a DEADBAND (120..136 = flat), so the LED that
		 * lights on 'not 128' and the kernel that engages on 'not 128' agree with
		 * the hand: a fader parked near the middle is flat and dark. */
		const int32_t tp_tn = (g_tp_tone >= 120u && g_tp_tone <= 136u) ? 128 : (int32_t)g_tp_tone;
		const int32_t tp_glo = (tp_tn >= 128) ? (256 - (tp_tn - 128))
		                                      : (256 + (128 - tp_tn));
		const int32_t tp_ghi = (tp_tn >= 128) ? (256 + (tp_tn - 128) * 3)
		                                      : (256 - (128 - tp_tn) * 2);
		const int      tp_any = rp4 && ((g_tp_drive != 0u) || (tp_hs != 0) || (tp_ck != 0)   /* HISS2-701 */
		                     || (tp_tn != 128) || (g_tp_wob != 0u) || (g_wb_off != 0) || (g_wb_tgt != 0));   /* WOBBUS-673 */
		/* ONE accumulator serves sweep, tremolo and the phaser. */
		const int      lfo_on   = (swp_d || trm_d || phs_d) ? 1 : 0;
		/* FX2-550: both new kernels are read ONCE per block into a local,
		 * exactly like the filter and the distortion. Reading a volatile
		 * inside the frame loop would re-load it 128 times and would also
		 * let a fader move mid-block. */
		const int32_t chr_mix = rp1 ? (int32_t)g_chr_mix : 0;
		/* TG-551: the grid anchor, recomputed once per block.
		 * g_grid_beat_frames is in OUTPUT frames -- the tape domain has its
		 * own g_gridrec_beat_samps -- so this locks to WHAT YOU HEAR and
		 * stays locked under varispeed for free. Re-deriving the step index
		 * from the grid every block means the pattern can never drift, and
		 * the per-frame path is then just an increment and a compare.
		 * No grid on this song -> fall back to the tape's own BPM label. An
		 * effect that silently does nothing is indistinguishable from a
		 * broken one; today proved that twice. */
		const uint32_t tg_rate = g_gat_amt
		                       ? (1u + (((uint32_t)g_gat_amt * 6u) >> 8)) : 0u;
		uint32_t tg_sf = 0u;
		if (tg_rate && rp1) {
			uint32_t _bf = (g_grid_active && g_grid_beat_frames)
			             ? g_grid_beat_frames
			             : ((g_play_bpm > 0)
			                ? (48000u * 60u / (uint32_t)g_play_bpm) : 0u);
			uint64_t _anc = (g_grid_active && g_grid_beat_frames)
			              ? g_grid_anchor_e : 0u;
			if (lane_p0) { _bf = lane_p0; _anc = g_lane_anc; }   /* A4: the tapped beat, from its first tap */
			uint32_t _spb = tg_spb[tg_rate];
			if (_bf && _spb) tg_sf = _bf / _spb;
			/* r21: pattern slot 4 is OFF -- the fifth tap position. */
			if (g_gat_pat >= 2u) tg_sf = 0u;
			if (tg_sf) {
				uint64_t _ph = g_sample_clock - _anc;
				g_tg_idx = (uint32_t)((_ph / tg_sf) & 15u);
				g_tg_ph  = (uint32_t)(_ph % tg_sf);
			}
		}
		const uint16_t tg_mask = tg_pat[(g_gat_pat < 2u) ? g_gat_pat : 0u];
		/* r21: whenever the gate is not running -- fader down OR tapped to
		 * OFF -- hand the gain back at UNITY. Without this a bypass taken
		 * mid-close would leave the last partial gain applied forever. */
		if (!tg_sf && rp1) g_gat_g = 4096;   /* INFX-672: the other pass must not reset the gate's ramp */
		static int32_t dst_g_s;
		{
			/* DST-548 r2 (marc: "make it more obvious"). Two faults.
			 * (1) RANGE. Max drive was 8x = +18 dB, and real loop
			 * material peaks near -20 dBFS -- so at the TOP of the
			 * fader the signal barely reached full scale and barely
			 * clipped at all. Now 32x = +30 dB, which puts that same
			 * material 10 dB INTO the clipper at the top.
			 * (2) TAPER. It was LINEAR in gain, so the first 80%% of
			 * the throw spent itself on the quiet dBs and every
			 * audible change crowded into the last inch. Now it is
			 * EXPONENTIAL -- five octaves, so every 20%% of travel
			 * DOUBLES the drive and the grit arrives evenly.
			 * (2^x is linear-interpolated inside each octave: ~6%%
			 * worst-case error, inaudible, and no table or divide.) */
			int32_t tg;
			if (g_dst_amt == 0u) {
				tg = 0;
			} else {
				uint32_t _o5  = (uint32_t)g_dst_amt * 5u;   /* 0..1275 */
				uint32_t _oct = _o5 / 255u;                 /* 0..5    */
				uint32_t _fr  = _o5 - _oct * 255u;          /* 0..254  */
				/* r3 (marc: "louder in the bottom half, distortion only
				 * at the top"). The range now STARTS at 2x. Below ~5x
				 * nothing audible happens to a -20 dBFS take except
				 * level, so a range starting at 1x spent its whole
				 * bottom half being a volume knob. 2x..64x puts the
				 * clipping threshold around a THIRD of the way up. */
				uint32_t _b   = 8192u << _oct;   /* 2x .. 64x */
				tg = (int32_t)(_b + ((_b * _fr) / 255u));
			}
			if (_first) {   /* INFX-672: once per block */
			int32_t d = (tg - dst_g_s) >> 3;
			if (d == 0) d = (tg > dst_g_s) ? 1 : ((tg < dst_g_s) ? -1 : 0);
			dst_g_s += d;
			}
		}
		const int32_t dst_g = (!rp1 || (g_dst_amt == 0u && dst_g_s < 4160) || g_dst_typ >= 3u) ? 0 : dst_g_s;   /* 660: type 3 = OFF */
		/* DST-548 r2: the trim used to cancel the drive EXACTLY, which
		 * kept the level honest but also removed the loudness cue that
		 * makes distortion read as distortion. It now allows up to +6 dB
		 * of makeup and no more: below 2x drive there is no trim at all
		 * (the level simply rises), and above it the trim holds the net
		 * at 2x so everything further is pure GRIT, not volume. */
		/* DST-548 r3: THE TRIM LAW. r2 allowed +6 dB of makeup below
		 * 2x drive, which is exactly the "it just gets louder" marc
		 * heard -- and worse, past ~6x it kept dividing by the drive
		 * long after the clipper had stopped getting louder, so the
		 * top of the fader went QUIET. Both come from trimming by the
		 * drive when what matters is the OUTPUT.
		 * The soft clipper saturates at 2/3 full scale no matter how
		 * hard it is driven. So the honest trim is 1/drive only while
		 * the signal is still LINEAR, and a CONSTANT once it clips.
		 * The 700 floor is that constant. Measured against a -20 dBFS
		 * take the output now sits within ~1 dB from 2x to 64x: every
		 * bit of the fader is TIMBRE, and none of it is volume. */
		int32_t _dt = (dst_g == 0) ? 4096 : (int32_t)(16777216 / dst_g);
		if (_dt < 700) _dt = 700;
		const int32_t dst_trim = _dt;
		/* ===== FXFAST-559: ONE gate for the WHOLE chain =====
		 * The frame loop used to ask SEVEN questions per frame -- filter,
		 * chorus, drive, gate, bitcrush, width, LFO -- 1,792 branch
		 * evaluations a block with every fader down. Skipping a BODY is not
		 * the same as not ASKING (W205): at 48 kHz a test you take 12,288
		 * times a second is a real cost.
		 * So decide ONCE per block, then run a loop that cannot ask.
		 * The lean path is master volume -> limiter -> store -> decimated
		 * VU, and nothing else. It costs flash (a duplicated loop) and
		 * returns cycles, which is the right trade on a part with plenty of
		 * flash and ~4%% idle. ⚠ NOTE: this is an OPTIMISATION, not a
		 * regression fix -- W206 measured the same-bin corner spread at
		 * 24%% / 2x, so nothing smaller than that was ever demonstrated. */
		const int fx_any = (flt_mode != 0u && rp1) || (chr_mix != 0) || (dst_g != 0)
		                || (tg_sf != 0u)   || (bcr_hold != 0u)
		                || (fx2_lfo_on != 0)
		                || (rng_d != 0)    || (awh_d != 0)
		                || (phs_d != 0)    || (tp_any != 0)    || (ec_n != 0)
		                || (rv_n != 0)    /* RVFIX-678: the reverb runs on its own */
		                || (eq_n != 0);   /* EQ-691 (W323: a gated stage adds its term) */
		/* ==== EFXM-586: EFFECT-MAJOR PASS C (W279) ====
		 * 585's sweep: every PASS C effect cost the same ~50% of track-time
		 * silent at the corner -- gate (4 cyc of math) = distortion (45) --
		 * while the wobble, which never enters this chain, cost 8%. The
		 * price was the CHAIN, not the kernels: one loop carrying ~35
		 * block-constants on a core with 12 registers, so every one of the
		 * 12 per-frame gates was a stack load + compare + branch, 256 times
		 * a block. Now each effect is its own tight loop over mix32[], in
		 * the SAME order, with only its own constants live. Output is
		 * bit-identical: every effect is causal per-sample with private
		 * state, so 'all frames of A, then all frames of B' == 'A then B
		 * per frame'. The shared LFO is read as base + (f+1)*inc, exactly
		 * the value the old loop saw after its per-frame advance, and
		 * written back once. An OFF effect costs one branch per BLOCK. The
		 * lean loop below (master -> limiter -> store -> VU) is the proven
		 * 559 loop and now runs unconditionally. */
		if (fx_any) {
		/* A3: per-lane increments from the beat (or the tapped period). Division
		 * 0 = beat, 1 = half, 2 = quarter. One udiv per ENGAGED lane per block. */
		uint32_t phs_inc = 0u, swp_inc = 0u, trm_inc = 0u;
		if (phs_d) { uint32_t _pp = lane_p2 ? lane_p2 : (lane_bf >> g_lfo_div[0]); if (_pp < 64u) _pp = 64u; phs_inc = 0xFFFFFFFFu / _pp + 1u; }
		if (swp_d) { uint32_t _pp = lane_p3 ? lane_p3 : (lane_bf >> g_lfo_div[1]); if (_pp < 64u) _pp = 64u; swp_inc = 0xFFFFFFFFu / _pp + 1u; }
		if (trm_d) { uint32_t _pp = lane_p4 ? lane_p4 : (lane_bf >> g_lfo_div[2]); if (_pp < 64u) _pp = 64u; trm_inc = 0xFFFFFFFFu / _pp + 1u; }
		/* A4 (660): the chorus LFO is tappable too; its default is the 0.68 Hz sweep it always had */
		const uint32_t chr_inc = lane_p5 ? (0xFFFFFFFFu / (lane_p5 < 64u ? 64u : lane_p5) + 1u) : CHR_LFO_INC;
		const uint32_t lfo_base = g_fx2_lfo;   /* (A3: no page-3 lane reads this any more; kept for the fold below) */
		if (rp4) {   /* WOBBUS-673: the tape transport first, then the heads (hiss / drive / tone) */
			wob_tick_block();
			if (g_wb_off != 0 || g_wb_tgt != 0) in_wobble_block(mix32, mix32R);
		}
		if (tp_hs) {
			uint32_t _h_g_tp_hsi = g_tp_hsi;
			uint32_t _h_g_tp_rng = g_tp_rng;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
					/* TUNE-575: pink is only -3 dB/oct and at 48 kHz carries energy
					 * to 24 kHz -- it reads as WHITE. Cassette hiss is pink through
					 * the tape's own treble rolloff. One pole at ~3.5 kHz after the
					 * pink filter. TUNE2-576: 2.5 kHz (coefficient 9145/32768). */
					/* FXCOST-578: table hiss; L and R half a table apart. */
					/* TUNE4-581 (W270): a looped table has a line spectrum at 2.9 Hz
					 * spacing and marc heard a partial wavering at that rate. Skip one
					 * sample with p=1/16 per frame: the period drifts ~1000 samples a
					 * lap and the lines smear into a continuum. Band-limited noise is
					 * correlated over ~5 samples, so a 1-sample skip is inaudible.
					 * R reads BACKWARD from half a table away: decorrelated from L. */
					uint32_t _hi = _h_g_tp_hsi;
					_h_g_tp_rng = _h_g_tp_rng * 1664525u + 1013904223u;
					xL += ((int32_t)tp_hiss_tbl[_hi & TP_HISS_MASK] * tp_hs) >> 8;
					xR += ((int32_t)tp_hiss_tbl[((TP_HISS_N >> 1) - _hi) & TP_HISS_MASK] * tp_hs) >> 8;
					_h_g_tp_hsi = (uint16_t)(_hi + 1u + ((_h_g_tp_rng >> 28) == 0u ? 1u : 0u));
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_tp_hsi = (uint16_t)_h_g_tp_hsi;
			g_tp_rng = _h_g_tp_rng;
		}
		if (tp_ck) {
			/* HISS2-701 VINYL CRACKLE. The hiss's LCG decides a click with
			 * probability (150 + 26 depth) / 2^24 a frame (~1 to ~10 a second). A
			 * click is one excitation, depth x (1..4) (random per click, random
			 * sign), into a two-pole resonator: 2.4 kHz, r = 0.983 (~1.2 ms) --
			 * a band-limited TICK (peak ~1,550 = -26 dBFS at full depth, ~5 ms),
			 * not 658's raw impulse. The sign and size come from a second LCG
			 * step so they are the high bits, not the LCG's weak low bits. */
			uint32_t _h_g_tp_rng = g_tp_rng;
			int32_t  _y1 = g_tp_ck1, _y2 = g_tp_ck2;
			const uint32_t _ckp = 150u + (uint32_t)tp_ck * 26u;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				_h_g_tp_rng = _h_g_tp_rng * 1664525u + 1013904223u;
				int32_t _x = 0;
				if ((_h_g_tp_rng >> 8) < _ckp) {
					_h_g_tp_rng = _h_g_tp_rng * 1664525u + 1013904223u;
					_x = tp_ck * (1 + (int32_t)(_h_g_tp_rng >> 30));
					if (_h_g_tp_rng & 0x20000000u) _x = -_x;
				}
				int32_t _y = _x + ((_y1 * 30628) >> 14) - ((_y2 * 15825) >> 14);
				_y2 = _y1; _y1 = _y;
				mix32[f]  += _y;
				mix32R[f] += _y;
			}
			g_tp_rng = _h_g_tp_rng;
			g_tp_ck1 = _y1; g_tp_ck2 = _y2;
		}
		if (rp4 && tp_dr != 4096) {
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
					int32_t _uL = ((xL >> 3) * tp_dr) >> 12;   /* FXCOST-578: int32; 3 LSB into a tanh */
					int32_t _uR = ((xR >> 3) * tp_dr) >> 12;
					xL = ((tp_tanh_q12(_uL) * 8) * tp_mk) >> 8;
					xR = ((tp_tanh_q12(_uR) * 8) * tp_mk) >> 8;
				mix32[f] = xL; mix32R[f] = xR;
			}
		}
		if (rp4 && tp_tn != 128) {
			int32_t _h_g_tp_toneL = g_tp_toneL;
			int32_t _h_g_tp_toneR = g_tp_toneR;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
					/* skipped at neutral -- upstream runs this every sample
					 * whenever the chain is entered for ANY reason (559's
					 * lesson: skipping a body is not the same as not asking). */
					_h_g_tp_toneL += (((xL - _h_g_tp_toneL) >> 1) * 6700) >> 14;   /* FXCOST-578: int32; |diff|<=524k would overflow x6700 by 3%, so halve first */
					_h_g_tp_toneR += (((xR - _h_g_tp_toneR) >> 1) * 6700) >> 14;
					xL = ((_h_g_tp_toneL * tp_glo) + ((xL - _h_g_tp_toneL) * tp_ghi)) >> 8;
					xR = ((_h_g_tp_toneR * tp_glo) + ((xR - _h_g_tp_toneR) * tp_ghi)) >> 8;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_tp_toneL = _h_g_tp_toneL;
			g_tp_toneR = _h_g_tp_toneR;
		}
		if (flt_mode && rp1) {
			int32_t _h_flt_lowL = flt_lowL;
			int32_t _h_flt_bandL = flt_bandL;
			int32_t _h_flt_lowR = flt_lowR;
			int32_t _h_flt_bandR = flt_bandR;
			const int32_t _h_flt_f = flt_f;
			const int32_t _f17 = _h_flt_f << 17;   /* CPU-780 C1: (f << 17) * (v << 1) >> 32 = floor(f * v / 2^14), one high-word multiply (f <= 13,524) */
			/* STACKA-664 THE BAND FILTER (marc: 'only the new dj filter'). The same SVF
			 * and coefficient path as the LP/HP it replaces; the OUTPUT is the band,
			 * and it CROSSFADES in from the centre: dry inside the 112..143 bypass
			 * band, fully wet 32 counts further out. The two halves meet at dry, so
			 * the high-band / low-band seam the 658 version had at the centre is
			 * gone. Below centre the band sweeps DOWN from the highs; above it, UP
			 * from the lows. One filter, two handles (FN + fader 4 drives it too). */
			const uint32_t _fdc = (g_flt_pos >= 128u) ? ((uint32_t)g_flt_pos - 128u) : (128u - (uint32_t)g_flt_pos);
			const int32_t  _bpw = (_fdc <= 16u) ? 0 : ((_fdc - 16u) * 8u >= 256u ? 256 : (int32_t)((_fdc - 16u) * 8u));
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* Chamberlin SVF x2 (M63a): per-channel state, shared
				 * coefficient. int64 products per the M25 overflow fix. */
				_h_flt_lowL += CPU780_MULH14(_f17, _h_flt_bandL);
				int32_t hiL = xL - _h_flt_lowL - _h_flt_bandL;
				_h_flt_bandL += CPU780_MULH14(_f17, hiL);
				xL += (((_h_flt_bandL * 2) - xL) * _bpw) >> 8;   /* CPU-780 C1: int32 (_bpw <= 256, the bus < 2^23) */
				_h_flt_lowR += CPU780_MULH14(_f17, _h_flt_bandR);
				int32_t hiR = xR - _h_flt_lowR - _h_flt_bandR;
				_h_flt_bandR += CPU780_MULH14(_f17, hiR);
				xR += (((_h_flt_bandR * 2) - xR) * _bpw) >> 8;
				mix32[f] = xL; mix32R[f] = xR;
			}
			flt_lowL = _h_flt_lowL;
			flt_bandL = _h_flt_bandL;
			flt_lowR = _h_flt_lowR;
			flt_bandR = _h_flt_bandR;
		}
		if (chr_mix) {
			uint32_t _h_g_chr_w = g_chr_w;
			uint32_t _h_g_chr_ph = g_chr_ph;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* FX2-550 CHORUS: one mono delay line read at two LFO
				 * phases a quarter cycle apart -- that phase offset IS
				 * the stereo image, and it costs one buffer instead of
				 * two. Triangle LFO (no table, no divide), linearly
				 * interpolated read so the pitch bend is smooth rather
				 * than stepped -- an integer-only read is what makes a
				 * cheap chorus sound like a broken one. */
				{	/* CHRSAT-592 (W284): the bus is 32-bit and two loud tracks
					 * already exceed +-32767; the plain (int16_t) cast WRAPPED,
					 * filling the delay line with garbage -- marc: "crackles really
					 * loud as I slide up the fader", any speed, no input needed.
					 * Saturate before the store. Identical to before whenever the
					 * old code did not wrap. */
					int32_t _cm = (xL + xR) >> 1;
					if (_cm >  32767) _cm =  32767;
					if (_cm < -32767) _cm = -32767;
					g_chr_buf[_h_g_chr_w & CHR_MASK] = (int16_t)_cm;
				}
				_h_g_chr_ph += chr_inc;   /* A4 (660): tappable */
				for (int _c = 0; _c < 2; _c++) {
					uint32_t _ph = _h_g_chr_ph + (_c ? 0x40000000u : 0u);
					/* triangle: 0..65535 from the top 17 bits */
					uint32_t _t = _ph >> 15;             /* 0..131071 */
					int32_t  _tri = (_t < 65536u) ? (int32_t)_t
					                              : (int32_t)(131071u - _t);
					/* delay = 4 ms .. 18 ms, in Q8 frames */
					int32_t _d8 = (CHR_D_MIN << 8) +
					              ((_tri * CHR_D_SPAN) >> 8);
					uint32_t _di = (uint32_t)(_d8 >> 8);
					int32_t  _fr = _d8 & 255;
					int32_t  _a = g_chr_buf[(_h_g_chr_w - _di)      & CHR_MASK];
					int32_t  _b = g_chr_buf[(_h_g_chr_w - _di - 1u) & CHR_MASK];
					int32_t  _w = _a + (((_b - _a) * _fr) >> 8);
					if (_c) xR += (_w * chr_mix) >> 8;
					else    xL += (_w * chr_mix) >> 8;
				}
				_h_g_chr_w++;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_chr_w = _h_g_chr_w;
			g_chr_ph = _h_g_chr_ph;
		}
		if (dst_g) {
			const uint32_t _dtyp = g_dst_typ;   /* A5: read once per block */
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* DST-548: cubic soft clip, Q15, per channel. The
				 * drive gain rides IN (more drive = more clipping)
				 * and a compensating trim rides OUT, so the fader
				 * adds grit without becoming a volume knob. */
				/* DST-548 r2: 64-bit product. At the old 8x ceiling
				 * xL * dst_g just fit in int32; at 32x it does NOT
				 * (32767 * 131072 = 4.29e9 against a 2.15e9 limit)
				 * and would WRAP -- which sounds like a fuzz pedal
				 * exactly often enough to be mistaken for working. */
				int32_t dL = (int32_t)(((int64_t)xL * dst_g) >> 12);
				int32_t dR = (int32_t)(((int64_t)xR * dst_g) >> 12);
				if (dL >  32767) dL =  32767;
				if (dL < -32767) dL = -32767;
				if (dR >  32767) dR =  32767;
				if (dR < -32767) dR = -32767;
				if (_dtyp == 0u) {            /* SOFT: the cubic, as shipped (saturates at 2/3 FS) */
					{ int32_t t = (dL * dL) >> 15;
					  dL = dL - (((t * dL) >> 15) * 10923 >> 15); }
					{ int32_t t = (dR * dR) >> 15;
					  dR = dR - (((t * dR) >> 15) * 10923 >> 15); }
				} else if (_dtyp == 1u) {     /* HARD (660): a flat clip at 1/3 FS, then x2 -- square-wave buzz */
					if (dL >  10922) dL =  10922; if (dL < -10922) dL = -10922;
					if (dR >  10922) dR =  10922; if (dR < -10922) dR = -10922;
					dL *= 2; dR *= 2;
				} else {                      /* FOLD (660): reflect at 1/3 FS (one reflection covers +-3T), then x2 -- ring-like harmonics */
					if (dL >  10922) dL =  21844 - dL; else if (dL < -10922) dL = -21844 - dL;
					if (dR >  10922) dR =  21844 - dR; else if (dR < -10922) dR = -21844 - dR;
					dL *= 2; dR *= 2;
				}
				xL = (dL * dst_trim) >> 12;
				xR = (dR * dst_trim) >> 12;
				mix32[f] = xL; mix32R[f] = xR;
			}
		}
		if (tg_sf) {
			uint32_t _h_g_tg_ph = g_tg_ph;
			uint32_t _h_g_tg_idx = g_tg_idx;
			int32_t _h_g_gat_g = g_gat_g;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* TG-551 TRANCE GATE -- tempo-synced amplitude gating on a
				 * 16-step pattern. It obeys the CLOCK and does not listen to
				 * the signal, which is the whole difference from what was
				 * here before and the reason it cannot be mis-scaled.
				 * Last in the chain (filter -> chorus -> drive -> gate), so
				 * the grit gets chopped rather than the chop getting fuzzed. */
				if (++_h_g_tg_ph >= tg_sf) {
					_h_g_tg_ph = 0u;
					_h_g_tg_idx = (_h_g_tg_idx + 1u) & 15u;
				}
				int32_t _tgt = ((tg_mask >> _h_g_tg_idx) & 1u) ? 4096 : 0;
				if (_tgt > _h_g_gat_g) {
					_h_g_gat_g += TG_ATK;
					if (_h_g_gat_g > 4096) _h_g_gat_g = 4096;
				} else if (_tgt < _h_g_gat_g) {
					_h_g_gat_g -= TG_REL;
					if (_h_g_gat_g < 0) _h_g_gat_g = 0;
				}
				xL = (xL * _h_g_gat_g) >> 12;   /* FXCOST-578: int32; g <= 4096 */
				xR = (xR * _h_g_gat_g) >> 12;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_tg_ph = _h_g_tg_ph;
			g_tg_idx = _h_g_tg_idx;
			g_gat_g = _h_g_gat_g;
		}
		if (bcr_hold) {
			uint32_t _h_g_bcr_ph = g_bcr_ph;
			int32_t _h_g_bcr_hL = g_bcr_hL;
			int32_t _h_g_bcr_hR = g_bcr_hR;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* BITCRUSH: sample-and-hold + bit mask. The hold is what
				 * makes it sound like a sampler rather than just quiet
				 * distortion; the mask alone is nearly inaudible until it
				 * is severe. */
				if (++_h_g_bcr_ph >= bcr_hold) {
					_h_g_bcr_ph = 0u;
					_h_g_bcr_hL = xL & bcr_mask;
					_h_g_bcr_hR = xR & bcr_mask;
				}
				xL = _h_g_bcr_hL; xR = _h_g_bcr_hR;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_bcr_ph = _h_g_bcr_ph;
			g_bcr_hL = _h_g_bcr_hL;
			g_bcr_hR = _h_g_bcr_hR;
		}
		if (rng_d) {
			uint32_t _h_g_rng_ph = g_rng_ph;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* RING MOD: multiply by an audio-rate triangle carrier. No
				 * delay line, no filter state -- one accumulator and two
				 * multiplies. It is the most dramatic thing available for
				 * free, which is exactly why it is here instead of echo. */
				_h_g_rng_ph += rng_inc;
				int32_t _c = (int32_t)((_h_g_rng_ph >> 23) & 511u);
				_c = (_c < 256) ? (_c - 128) : (383 - _c);   /* -128..127 */
				int32_t _wL = (xL * _c) >> 7;
				int32_t _wR = (xR * _c) >> 7;
				xL += ((_wL - xL) * rng_d) >> 8;
				xR += ((_wR - xR) * rng_d) >> 8;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_rng_ph = _h_g_rng_ph;
		}
		if (awh_d) {
			int32_t _h_g_awh_env = g_awh_env;
			int32_t _h_g_awh_lowL = g_awh_lowL;
			int32_t _h_g_awh_bandL = g_awh_bandL;
			int32_t _h_g_awh_lowR = g_awh_lowR;
			int32_t _h_g_awh_bandR = g_awh_bandR;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* AUTO-WAH: an envelope follower driving its own Chamberlin
				 * SVF, bandpass output. Fast attack, slow release -- that
				 * asymmetry IS the wah; a symmetric follower just wobbles.
				 * Coefficients are the same Q14 domain as flt_lp_tab, so
				 * 850 ~ 400 Hz and 4900 ~ 2.3 kHz. */
				/* LAYOUT-562: fast attack, FAST release -- the filter has to
				 * close between hits or every note rides one drifting peak. */
				int32_t _aa = (xL < 0 ? -xL : xL) + (xR < 0 ? -xR : xR);
				if (_aa > _h_g_awh_env) _h_g_awh_env += (_aa - _h_g_awh_env) >> 4;
				else                 _h_g_awh_env -= (_h_g_awh_env - _aa) >> 7;
				int32_t _e = _h_g_awh_env >> 6;      /* 4x more sensitive */
				if (_e > 255) _e = 255;
				int32_t _cf = 300 + ((_e * awh_d) >> 3);   /* ~140 Hz .. ~4 kHz */
				if (_cf > 9000) _cf = 9000;
				const int32_t _cf17 = _cf << 17;   /* CPU-780 C2 */
				/* RESONANCE: damping 1/2 instead of 1 (Q = 2). Without a peak
				 * this is a moving tone control, not a wah -- that missing
				 * peak is why 560's version read as subtle. */
				_h_g_awh_lowL += CPU780_MULH14(_cf17, _h_g_awh_bandL);
				int32_t _hL = xL - _h_g_awh_lowL - ((_h_g_awh_bandL * awh_q) >> 8);   /* SEC-695 */
				_h_g_awh_bandL += CPU780_MULH14(_cf17, _hL);
				_h_g_awh_lowR += CPU780_MULH14(_cf17, _h_g_awh_bandR);
				int32_t _hR = xR - _h_g_awh_lowR - ((_h_g_awh_bandR * awh_q) >> 8);   /* SEC-695 */
				_h_g_awh_bandR += CPU780_MULH14(_cf17, _hR);
				xL += ((_h_g_awh_bandL - xL) * awh_d) >> 8;
				xR += ((_h_g_awh_bandR - xR) * awh_d) >> 8;
				mix32[f] = xL; mix32R[f] = xR;
			}
			g_awh_env = _h_g_awh_env;
			g_awh_lowL = _h_g_awh_lowL;
			g_awh_bandL = _h_g_awh_bandL;
			g_awh_lowR = _h_g_awh_lowR;
			g_awh_bandR = _h_g_awh_bandR;
		}
		if (phs_d) {
			/* EFXM-586: one channel at a time, the 4-stage state in REGISTERS.
			 * 585 re-loaded and re-stored all 16 state words every frame (the
			 * arrays are globals) -- most of the phaser's 77%% in the sweep.
			 * Same arithmetic, same order: bit-identical. */
			int16_t _pas[BLK_FRAMES];   /* CPU-781 C4: the LFO coefficient once per frame (700..3,800), read by both channel passes */
			{
				uint32_t _lfo = g_lfo_ph[0];
				for (uint32_t f = 0; f < BLK_FRAMES; f++) {
					_lfo += phs_inc;
					uint32_t _pu = _lfo >> 23;
					int32_t  _pt = (_pu < 256u) ? (int32_t)_pu : (int32_t)(511u - _pu);
					_pas[f] = (int16_t)(700 + ((_pt * 3100) >> 8));   /* Q12 coeff, FXRST-563 range */
				}
			}
			int32_t *_mx = mix32;
			for (int _ch = 0; _ch < 2; _ch++) {
				int32_t _s0 = g_phs_xi[_ch * 4 + 0], _s1 = g_phs_xi[_ch * 4 + 1];
				int32_t _s2 = g_phs_xi[_ch * 4 + 2], _s3 = g_phs_xi[_ch * 4 + 3];
				int32_t _t0 = g_phs_yo[_ch * 4 + 0], _t1 = g_phs_yo[_ch * 4 + 1];
				int32_t _t2 = g_phs_yo[_ch * 4 + 2], _t3 = g_phs_yo[_ch * 4 + 3];
				int32_t _fb = _ch ? g_phs_fbR : g_phs_fbL;
				for (uint32_t f = 0; f < BLK_FRAMES; f++) {
					const int32_t _pa = _pas[f];   /* CPU-781 C4 (A3: both channels from the same base) */
					int32_t _x = _mx[f];
					int32_t _y = _x + (_fb >> 1);   /* PHS3-565 feedback 1/2 */
					int32_t _o;
					_o = ((_pa * (_t0 - _y)) >> 12) + _s0; _s0 = _y; _t0 = _o; _y = _o;
					_o = ((_pa * (_t1 - _y)) >> 12) + _s1; _s1 = _y; _t1 = _o; _y = _o;
					_o = ((_pa * (_t2 - _y)) >> 12) + _s2; _s2 = _y; _t2 = _o; _y = _o;
					_o = ((_pa * (_t3 - _y)) >> 12) + _s3; _s3 = _y; _t3 = _o; _y = _o;
					_fb = _y;
					_mx[f] = _x + (((_y - _x) * phs_d) >> 9);
				}
				g_phs_xi[_ch * 4 + 0] = _s0; g_phs_xi[_ch * 4 + 1] = _s1;
				g_phs_xi[_ch * 4 + 2] = _s2; g_phs_xi[_ch * 4 + 3] = _s3;
				g_phs_yo[_ch * 4 + 0] = _t0; g_phs_yo[_ch * 4 + 1] = _t1;
				g_phs_yo[_ch * 4 + 2] = _t2; g_phs_yo[_ch * 4 + 3] = _t3;
				if (_ch) g_phs_fbR = _fb; else g_phs_fbL = _fb;
				_mx = mix32R;
			}
			g_lfo_ph[0] += (uint32_t)BLK_FRAMES * phs_inc;   /* A3 */
		}
		if (swp_d) {
			uint32_t _lfo = g_lfo_ph[1];   /* A3: the sweep's own clock */
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				_lfo += swp_inc;
				int32_t xL = mix32[f];
				int32_t xR = mix32R[f];
				/* ONE triangle LFO drives both sweep and tremolo -- they are
				 * the same motion applied to pan and to level, so sharing the
				 * phase makes them lock musically instead of beating. */
				uint32_t _t = _lfo >> 23;              /* 0..511 */
				int32_t  _tri = (_t < 256u) ? (int32_t)_t
				                            : (int32_t)(511u - _t);   /* 0..255 */
					int32_t _d = ((_tri - 128) * swp_d) >> 8;
					xL = (xL * (128 - _d)) >> 7;
					xR = (xR * (128 + _d)) >> 7;
				mix32[f] = xL; mix32R[f] = xR;
			}
		}
		if (trm_d) {
			uint32_t _lfo = g_lfo_ph[2];   /* A3: the tremolo's own clock */
			const uint32_t _sprd = (uint32_t)g_sec2[2][2] << 23;   /* SEC-695/SHAPE-696: R's phase lead, 0..~180 deg (the third control) */
			const int32_t  _shp  = (int32_t)g_sec[2][2];          /* SHAPE-696: 0 sine .. 128 triangle .. 255 square */
			/* ONE triangle LFO drives both sweep and tremolo -- they are the same motion applied to
			 * pan and to level, so sharing the phase makes them lock musically instead of beating.
			 * W209: the tremolo reads its clock at the quarter default (A3). CPU-780 C7: the invariant
			 * `_shp != 128` test leaves the frame loop -- the same arithmetic in two explicit loops. */
			if (_shp == 128) {
				for (uint32_t f = 0; f < BLK_FRAMES; f++) {
					_lfo += trm_inc;
					int32_t xL = mix32[f];
					int32_t xR = mix32R[f];
					uint32_t _ts = _lfo >> 23;             /* 0..511 */
					int32_t  _tt = (_ts < 256u) ? (int32_t)_ts : (int32_t)(511u - _ts);
					int32_t _g = 128 - ((_tt * trm_d) >> 9);
					uint32_t _tsR = (_lfo + _sprd) >> 23;   /* SEC-695: the spread */
					int32_t  _ttR = (_tsR < 256u) ? (int32_t)_tsR : (int32_t)(511u - _tsR);
					int32_t _gR = 128 - ((_ttR * trm_d) >> 9);
					xL = (xL * _g) >> 7;
					xR = (xR * _gR) >> 7;
					mix32[f] = xL; mix32R[f] = xR;
				}
			} else {
				for (uint32_t f = 0; f < BLK_FRAMES; f++) {
					_lfo += trm_inc;
					int32_t xL = mix32[f];
					int32_t xR = mix32R[f];
					uint32_t _ts = _lfo >> 23;
					int32_t  _tt = (_ts < 256u) ? (int32_t)_ts : (int32_t)(511u - _ts);
					_tt = trm_shape(_tt, _shp);   /* SHAPE-696 */
					int32_t _g = 128 - ((_tt * trm_d) >> 9);
					uint32_t _tsR = (_lfo + _sprd) >> 23;
					int32_t  _ttR = (_tsR < 256u) ? (int32_t)_tsR : (int32_t)(511u - _tsR);
					_ttR = trm_shape(_ttR, _shp);
					int32_t _gR = 128 - ((_ttR * trm_d) >> 9);
					xL = (xL * _g) >> 7;
					xR = (xR * _gR) >> 7;
					mix32[f] = xL; mix32R[f] = xR;
				}
			}
		}
		if (ec_n && rv_n) {
			/* CPU-780 C3: echo INTO reverb in ONE group loop -- the boxcar-4 sum computed once, one
			 * read-modify-write per frame. The reverb's input is the POST-echo sum: every frame of the
			 * group received its e on both channels, so S_post = S + 2 * (e0 + e1 + e2 + e3), exact.
			 * Same arithmetic, same order (echo first), same states as the two loops below. */
			uint32_t _w  = g_ec2_w;
			uint32_t _r  = (_w + EC2_LINE - ec2_d) % EC2_LINE;
			uint32_t _r1 = (_r + 1u == EC2_LINE) ? 0u : _r + 1u;
			int32_t  _da = g_ec2_line[_r];
			int32_t _pL = g_rv_pl, _pR = g_rv_pr;
			for (uint32_t k = 0u; k < BLK_FRAMES / 4u; k++) {
				const uint32_t f0 = k * 4u;
				const int32_t _s8 = mix32[f0]      + mix32R[f0]
				                  + mix32[f0 + 1u] + mix32R[f0 + 1u]
				                  + mix32[f0 + 2u] + mix32R[f0 + 2u]
				                  + mix32[f0 + 3u] + mix32R[f0 + 3u];
				g_ec2_line[_w] = soft_limit((_s8 >> 3) + ((_da * ec2_fb) >> 8));
				int32_t _db = g_ec2_line[_r1];
				int32_t _dd = _db - _da;
				const int32_t _e0 = (_da * ec2_wet) >> 8;
				const int32_t _e1 = ((_da + (_dd >> 2)) * ec2_wet) >> 8;
				const int32_t _e2 = ((_da + (_dd >> 1)) * ec2_wet) >> 8;
				const int32_t _e3 = ((_da + _dd - (_dd >> 2)) * ec2_wet) >> 8;
				_da = _db;
				_w  = (_w  + 1u == EC2_LINE) ? 0u : _w  + 1u;
				_r1 = (_r1 + 1u == EC2_LINE) ? 0u : _r1 + 1u;
				int32_t _in = (_s8 + 2 * (_e0 + _e1 + _e2 + _e3)) >> 3;
				int32_t _oL, _oR;
				_in = soft_limit(_in);   /* RVKNEE-729 */
				rv_step(_in, rv_krt, rv_klp, &_oL, &_oR);
				_oL = (_oL * rv_wet) >> 6; _oR = (_oR * rv_wet) >> 6;   /* RV2-680: x4 back out */
				const int32_t _dL = _oL - _pL, _dR = _oR - _pR;
				mix32[f0]      += _e0 + _pL;                     mix32R[f0]      += _e0 + _pR;
				mix32[f0 + 1u] += _e1 + _pL + (_dL >> 2);        mix32R[f0 + 1u] += _e1 + _pR + (_dR >> 2);
				mix32[f0 + 2u] += _e2 + _pL + (_dL >> 1);        mix32R[f0 + 2u] += _e2 + _pR + (_dR >> 1);
				mix32[f0 + 3u] += _e3 + _pL + _dL - (_dL >> 2);  mix32R[f0 + 3u] += _e3 + _pR + _dR - (_dR >> 2);
				_pL = _oL; _pR = _oR;
			}
			g_ec2_w = _w; g_rv_pl = _pL; g_rv_pr = _pR;
			ec_n = 0; rv_n = 0;   /* both done */
		}
		if (ec_n) {
			/* ECHO2-610: 64 line samples per block. Each is the L+R average of
			 * 4 bus frames; its repeat is read with linear interpolation across
			 * the same 4 frames. Per group: read the delayed sample, write
			 * in + fb * delayed (soft-limited -- the CHRSAT-592 lesson), then add
			 * the repeat to both channels. The k+1 sample the interpolation
			 * needs is always already written: the delay is >= 1. */
			uint32_t _w  = g_ec2_w;
			uint32_t _r  = (_w + EC2_LINE - ec2_d) % EC2_LINE;   /* read = write - delay */
			uint32_t _r1 = (_r + 1u == EC2_LINE) ? 0u : _r + 1u;
			int32_t  _da = g_ec2_line[_r];                       /* delayed sample k */
			for (uint32_t k = 0u; k < BLK_FRAMES / 4u; k++) {
				const uint32_t f0 = k * 4u;
				int32_t _in = (mix32[f0]      + mix32R[f0]
				             + mix32[f0 + 1u] + mix32R[f0 + 1u]
				             + mix32[f0 + 2u] + mix32R[f0 + 2u]
				             + mix32[f0 + 3u] + mix32R[f0 + 3u]) >> 3;
				/* the store is SOFT-limited (knee at 0.8 FS): the bus is designed to
				 * exceed int16 and a recirculating pad would otherwise hard-clip;
				 * this is the tape-echo self-limiting instead -- repeats compress
				 * gently and feedback can never run away. */
				g_ec2_line[_w] = soft_limit(_in + ((_da * ec2_fb) >> 8));
				int32_t _db = g_ec2_line[_r1];                   /* delayed sample k+1 */
				int32_t _dd = _db - _da;
				int32_t _e0 = (_da * ec2_wet) >> 8;
				int32_t _e1 = ((_da + (_dd >> 2)) * ec2_wet) >> 8;
				int32_t _e2 = ((_da + (_dd >> 1)) * ec2_wet) >> 8;
				int32_t _e3 = ((_da + _dd - (_dd >> 2)) * ec2_wet) >> 8;
				mix32[f0]      += _e0; mix32R[f0]      += _e0;
				mix32[f0 + 1u] += _e1; mix32R[f0 + 1u] += _e1;
				mix32[f0 + 2u] += _e2; mix32R[f0 + 2u] += _e2;
				mix32[f0 + 3u] += _e3; mix32R[f0 + 3u] += _e3;
				_da = _db;
				_w  = (_w  + 1u == EC2_LINE) ? 0u : _w  + 1u;
				_r1 = (_r1 + 1u == EC2_LINE) ? 0u : _r1 + 1u;
			}
			g_ec2_w = _w;
		}
		if (rv_n) {
			/* REVERB-676: 64 steps per block on the L+R boxcar-4 sum; the wet pair is
			 * interpolated across the 4 frames from the PREVIOUS step's value (one
			 * group of latency on the tail, 83 us). */
			int32_t _pL = g_rv_pl, _pR = g_rv_pr;
			for (uint32_t k = 0u; k < BLK_FRAMES / 4u; k++) {
				const uint32_t f0 = k * 4u;
				int32_t _in = (mix32[f0]      + mix32R[f0]
				             + mix32[f0 + 1u] + mix32R[f0 + 1u]
				             + mix32[f0 + 2u] + mix32R[f0 + 2u]
				             + mix32[f0 + 3u] + mix32R[f0 + 3u]) >> 3;
				int32_t _oL, _oR;
				_in = soft_limit(_in);   /* RVKNEE-729: the bus is int32; a full-scale live pair must not pin the loop */
				rv_step(_in, rv_krt, rv_klp, &_oL, &_oR);
				_oL = (_oL * rv_wet) >> 6; _oR = (_oR * rv_wet) >> 6;   /* RV2-680: x4 back out */
				const int32_t _dL = _oL - _pL, _dR = _oR - _pR;
				mix32[f0]      += _pL;                     mix32R[f0]      += _pR;
				mix32[f0 + 1u] += _pL + (_dL >> 2);        mix32R[f0 + 1u] += _pR + (_dR >> 2);
				mix32[f0 + 2u] += _pL + (_dL >> 1);        mix32R[f0 + 2u] += _pR + (_dR >> 1);
				mix32[f0 + 3u] += _pL + _dL - (_dL >> 2);  mix32R[f0 + 3u] += _pR + _dR - (_dR >> 2);
				_pL = _oL; _pR = _oR;
			}
			g_rv_pl = _pL; g_rv_pr = _pR;
		}
		if (eq_n) {   /* EQ-691: page 5, last in the chain -- a master control; EQ2-692: pair loops */
			if (g_eq_k[0] | g_eq_k[3]) {
				eq_shelf_pair(mix32,  &g_eq_lo[0][0], &g_eq_lo[3][0], g_eq_k[0], g_eq_k[3]);
				eq_shelf_pair(mix32R, &g_eq_lo[0][1], &g_eq_lo[3][1], g_eq_k[0], g_eq_k[3]);
			}
			if (g_eq_k[1] | g_eq_k[2]) {
				eq_peak_pair(mix32,  &g_eq_lo[1][0], &g_eq_bp[1][0], &g_eq_lo[2][0], &g_eq_bp[2][0], g_eq_k[1], g_eq_k[2]);
				eq_peak_pair(mix32R, &g_eq_lo[1][1], &g_eq_bp[1][1], &g_eq_lo[2][1], &g_eq_bp[2][1], g_eq_k[1], g_eq_k[2]);
			}
		}
		if (swp_d) g_lfo_ph[1] += (uint32_t)BLK_FRAMES * swp_inc;   /* A3 */
		if (trm_d) g_lfo_ph[2] += (uint32_t)BLK_FRAMES * trm_inc;   /* A3 */
		(void)lfo_base; (void)lfo_on;   /* A3: the shared phase is the chorus's now */
		}
}

/* ===== WOBBUS-673: THE TAPE WOBBLE LIVES ON THE BUS NOW =====
 * Was: a per-frame read OFFSET inside PASS A (posb/fracb walked back up to 672
 * frames into the play ring's history, which forced a 768-frame reserve on the
 * streamer's fill, defeated PASS B's unity fast path on every track while the
 * wobble was up, and added a branch per frame to the hot loop). Now: the same
 * LFOs and ramp (this helper, ONE call per block from the chain's page-4 group)
 * drive ONE modulated delay line on the mix bus (in_wobble_block), page 4's
 * first stage. Nothing in the mixer reads g_wb_*; the ring reserve is 0 again. */
static void __attribute__((noinline)) wob_tick_block(void)
{
	g_wb_off = g_wb_tgt;                 /* land the previous ramp */
	{
		/* WOBTAP-675: a tapped period (>= 4800 frames) sets the wow's rate; 0 = the free 0.8 Hz */
		const uint32_t _lp = g_lane_per[6];
		const uint32_t _wi = _lp ? (0xFFFFFFFFu / (_lp < 4800u ? 4800u : _lp) + 1u) : WOB_WOW_INC;
		g_wb_wowph += _wi * BLK_FRAMES;
	}
	g_wb_fltph += WOB_FLT_INC * BLK_FRAMES;
	g_wb_rng = g_wb_rng * 1664525u + 1013904223u;
	int32_t _n1 = (int32_t)((int16_t)(g_wb_rng >> 16));
	g_wb_rng = g_wb_rng * 1664525u + 1013904223u;
	int32_t _n2 = (int32_t)((int16_t)(g_wb_rng >> 16));
	g_wb_wnse += (_n1 - g_wb_wnse) >> 6;
	g_wb_fnse += (_n2 - g_wb_fnse) >> 4;
	int32_t _d = (int32_t)g_tp_wob, _tg;
	if (_d == 0) {
		/* idle target is ZERO, not the base tap. His rc2 hardware fix:
		 * pinning idle at the base tap while a fast path read at offset 0
		 * made crossing between them an instant 5 ms jump -- a click every
		 * time the fader crossed zero. Ramping to 0 makes engage and
		 * disengage the same glide. */
		_tg = 0;
	} else {
		/* TUNE-575: LINEAR depth. The squared curve left the bottom three
		 * quarters of the fader below 2 cents -- inaudible on a loop. */
		int32_t _d2 = _d;
		int32_t _ws = ((wob_sin(g_wb_wowph) * 7) >> 3) + (g_wb_wnse >> 3);
		int32_t _fs = ((wob_sin(g_wb_fltph) * 7) >> 3) + (g_wb_fnse >> 3);
		int32_t _w = (int32_t)(((int64_t)WOB_WOW_PEAK * _ws) >> 15);
		int32_t _l = (int32_t)(((int64_t)WOB_FLT_PEAK * _fs) >> 15);
		_tg = (int32_t)(WOB_BASE_SAMP << 16)
		    + (int32_t)(((int64_t)(_w + _l) * _d2) >> 8);
	}
	int32_t _ms = (int32_t)(WOB_MAX_RATE_Q16 * BLK_FRAMES);
	int32_t _dl = _tg - g_wb_off;
	if (_dl >  _ms) _tg = g_wb_off + _ms;
	if (_dl < -_ms) _tg = g_wb_off - _ms;
	if (_d != 0 && _tg < (1 << 16)) _tg = (1 << 16);
	g_wb_tgt = _tg;
}
/* ===== INFX-672: THE INPUT SLOT (STACK D) =====
 * Runs on the LIVE PAIR before PASS A, in the mixer's scratch buses (mix32 /
 * mix32R hold the previous block's post-FX bus, which nothing reads after the
 * lean loop except a bounce's prepass -- and the slot is skipped on a bounce
 * block). What comes out goes back into tmp[] as int16: the monitor hears it
 * and PASS A's recorder stores it (D3). */
static void __attribute__((optimize("O2"), noinline)) in_wobble_block(int32_t *bL, int32_t *bR)
{
	/* the SAME offset the tape wobble is ramping this block (PASS A lands
	 * g_wb_off and publishes g_wb_tgt before the slot runs) */
	int32_t _o = g_wb_off;
	const int32_t _d = (g_wb_tgt - _o) / (int32_t)BLK_FRAMES;
	uint32_t w = g_inw_w;
	/* CPU-780 C5: _o is linear in f, so the whole-frame offset is monotone across the block -- the two
	 * range clamps below are decided ONCE at the block's ends; in range (always, in practice:
	 * WOB_BASE_SAMP + peaks <= 672 < INW_N - 2) the loop runs without them. Same arithmetic. */
	{
		const int32_t _oa = (_o + _d) >> 16, _ob = (_o + _d * (int32_t)BLK_FRAMES) >> 16;
		if (_oa >= 0 && _ob >= 0 && (uint32_t)_oa <= INW_N - 2u && (uint32_t)_ob <= INW_N - 2u) {
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t xL = bL[f], xR = bR[f];
				{
					int32_t sL = xL >> 2, sR = xR >> 2;
					g_inw_line[2u * w]      = (int16_t)(sL > 32767 ? 32767 : (sL < -32768 ? -32768 : sL));
					g_inw_line[2u * w + 1u] = (int16_t)(sR > 32767 ? 32767 : (sR < -32768 ? -32768 : sR));
				}
				_o += _d;
				const uint32_t oi = (uint32_t)(_o >> 16);
				const uint32_t fr = (uint32_t)_o & 0xFFFFu;
				uint32_t r0 = w + INW_N - oi; if (r0 >= INW_N) r0 -= INW_N;
				uint32_t r1 = (r0 == 0u) ? INW_N - 1u : r0 - 1u;
				int32_t aL = g_inw_line[2u * r0], aR = g_inw_line[2u * r0 + 1u];
				int32_t cL = g_inw_line[2u * r1], cR = g_inw_line[2u * r1 + 1u];
				bL[f] = (aL + (((cL - aL) * (int32_t)(fr >> 1)) >> 15)) << 2;
				bR[f] = (aR + (((cR - aR) * (int32_t)(fr >> 1)) >> 15)) << 2;
				w = (w + 1u == INW_N) ? 0u : w + 1u;
			}
			g_inw_w = w;
			g_inw_blk++;
			return;
		}
	}
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		int32_t xL = bL[f], xR = bR[f];
		{	/* WOBCLAMP-681 (W324): -12 dB into the line, a clamp not the knee; x4 back out below */
			int32_t sL = xL >> 2, sR = xR >> 2;
			g_inw_line[2u * w]      = (int16_t)(sL > 32767 ? 32767 : (sL < -32768 ? -32768 : sL));
			g_inw_line[2u * w + 1u] = (int16_t)(sR > 32767 ? 32767 : (sR < -32768 ? -32768 : sR));
		}
		_o += _d;
		int32_t  oi = _o >> 16;                     /* whole frames behind */
		uint32_t fr = (uint32_t)_o & 0xFFFFu;
		if (oi < 0) { oi = 0; fr = 0u; }
		if ((uint32_t)oi > INW_N - 2u) { oi = (int32_t)(INW_N - 2u); fr = 0u; }
		uint32_t r0 = w + INW_N - (uint32_t)oi; if (r0 >= INW_N) r0 -= INW_N;   /* WOBCLAMP-681: no udiv (oi < INW_N) */
		uint32_t r1 = (r0 == 0u) ? INW_N - 1u : r0 - 1u;    /* one frame further back */
		int32_t aL = g_inw_line[2u * r0], aR = g_inw_line[2u * r0 + 1u];
		int32_t cL = g_inw_line[2u * r1], cR = g_inw_line[2u * r1 + 1u];
		bL[f] = (aL + (((cL - aL) * (int32_t)(fr >> 1)) >> 15)) << 2;   /* WOBCLAMP-681: x4 */
		bR[f] = (aR + (((cR - aR) * (int32_t)(fr >> 1)) >> 15)) << 2;
		w = (w + 1u == INW_N) ? 0u : w + 1u;
	}
	g_inw_w = w;
	g_inw_blk++;
}
static void __attribute__((optimize("O2"), noinline)) in_slot_block(int16_t *tmp, uint32_t got, int32_t *bL, int32_t *bR)
{
	if (g_bnc_on && g_rec_track >= 0) return;   /* a bounce block: PASS A records the bus, the page runs there */
	uint32_t pm = 0u;
	for (uint32_t p = 1u; p <= 4u; p++)
		if (g_pg_route[p] == RT_IN) pm |= 1u << (p - 1u);
	if (!pm) return;
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		bL[f] = (f < got) ? (int32_t)tmp[2u * f]      : 0;
		bR[f] = (f < got) ? (int32_t)tmp[2u * f + 1u] : 0;
	}
	fx_chain_run(bL, bR, pm);
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		tmp[2u * f]      = soft_limit(bL[f]);
		tmp[2u * f + 1u] = soft_limit(bR[f]);
	}
	g_in_blk++;
}
/* The MIX slot: the mixer's call, unchanged in name and signature. With
 * every page at BOTH this is exactly one fx_chain_run(pm = all) = 665. */
static void __attribute__((optimize("O2"), noinline)) fx_chain_block(int32_t *mix32, int32_t *mix32R)
{
	const int _bnc = (g_bnc_on && g_rec_track >= 0);
	uint32_t pm_trk = 0u, pm_both = 0u;
	for (uint32_t p = 1u; p <= 4u; p++) {
		uint8_t r = g_pg_route[p];
		if (r == RT_IN && _bnc) r = RT_BOTH;        /* a bounce records the bus: the page runs there */
		if (r == RT_TRK)       pm_trk  |= 1u << (p - 1u);
		else if (r == RT_BOTH) pm_both |= 1u << (p - 1u);
	}
	const int16_t *lv = _bnc ? g_bnc_live : g_live_blk;
	const uint32_t lg = g_live_got;
	if (pm_trk) {
		/* tracks only: take the monitor out (PASS A added exactly these
		 * values), run the page, put it back -- int32, exact */
		for (uint32_t f = 0; f < lg; f++) { mix32[f] -= lv[2u * f]; mix32R[f] -= lv[2u * f + 1u]; }
		fx_chain_run(mix32, mix32R, pm_trk);
		for (uint32_t f = 0; f < lg; f++) { mix32[f] += lv[2u * f]; mix32R[f] += lv[2u * f + 1u]; }
	}
	if (g_eq_live) pm_both |= 16u;   /* EQ-691: page 5 is always on the MIX */
	if (pm_both) fx_chain_run(mix32, mix32R, pm_both);
	/* monitor mute (pages mode): a gain on the live pair only, ramped over
	 * ~4 blocks so the toggle never clicks; the recorder never sees it */
	{
		const int32_t tg = g_mon_mute ? 0 : 256;   /* MUTEANY-738: anywhere */
		int32_t g = g_mon_g_s;
		if (g != tg || g != 256) {
			const int32_t g0 = g;
			if (g < tg) { g += 64; if (g > tg) g = tg; } else if (g > tg) { g -= 64; if (g < tg) g = tg; }
			g_mon_g_s = g;
			const int32_t gd = g - g0;
			for (uint32_t f = 0; f < lg; f++) {
				int32_t k = 256 - (g0 + ((gd * (int32_t)(f + 1u)) >> 8));   /* what to take away */
				mix32[f]  -= ((int32_t)lv[2u * f]      * k) >> 8;
				mix32R[f] -= ((int32_t)lv[2u * f + 1u] * k) >> 8;
			}
		}
	}
}

/* NOINL-589 (W280): with the effect chain gone (EFXM2-588) the mixer became
 * small enough for GCC to INLINE into audio_thread -- PASS A/B were then
 * re-generated inside a different function, the very thing 588 exists to
 * prevent. Keep it a function of its own, as it was in every bin up to 585. */
/* ===== BNC3-606: THE BOUNCE'S BLOCK-LEVEL CODE, OUTSIDE THE MIXER (W291) =====
 * -O2 allocates looper_audio_block as one unit; 604/605 showed that even
 * straight-line bounce code inside it re-rolled PASS A/B's registers
 * (426 -> 555 spills). noinline is the whole point, as with fx_chain_block. */
static void __attribute__((noinline)) bnc_arm_prep(int i)
{
	/* The print's mode is its SOURCES' mode -- stereo if any playing source
	 * is stereo, mono only if all are (BNC2-603). Restored on cancel. */
	uint8_t _st = 0u;
	for (int _k = 0; _k < NTRK; _k++)
		if (_k != i && trk[_k].state == TS_PLAY && !trk[_k].p16m) _st = 1u;
	g_bnc_p16m_prev = trk[i].p16m_next;
	trk[i].p16m_next = _st ? 0u : 1u;
	g_bnc_prints++;
	g_bnc_anch = 0u;
	memset(g_bnc_pre_hist, 0, sizeof(g_bnc_pre_hist));   /* PRE-608: fresh FIR state per print */
	/* The pre-roll ring holds the JACK. With its valid count at 0 the M20
	 * rescue can never reach back (it needs need <= _pre_val, and the arm is
	 * always after the line it would reach to), and the M41 onset scan only
	 * ever sees post-arm samples -- which are the bus. The echo's history
	 * dips for <= 256 ms. */
	g_pre_valid = 0u;
	if (g_grid_punch_at) {
		/* BLOCK-EXACT ANCHOR, decided here instead of tested per emit: move
		 * the punch forward by up to one block of tape so (position at the
		 * punch - one block of tape) is a multiple of TSPB -- start_blk is
		 * exact and the print sits sample-exact on its sources. Exact at
		 * 1.0x (one emit per frame); B4 owns the other speeds. */
		uint64_t _dt = (g_grid_punch_at > g_sample_clock) ? (g_grid_punch_at - g_sample_clock) : 0u;
		uint32_t _cpl = g_consume_pos + (uint32_t)((_dt * g_cur_speed_q16) >> 16);
		uint32_t _lag = (uint32_t)(((uint64_t)(BLK_FRAMES + BNC_PRE_HALF) * g_cur_speed_q16) >> 16);   /* PRE-608: + the FIR delay */
		uint32_t _need = (TSPBI(i) - ((_cpl - _lag) % TSPBI(i))) % TSPBI(i);
		if (_need && g_cur_speed_q16)
			g_grid_punch_at += (uint64_t)((((uint64_t)_need << 16) + g_cur_speed_q16 - 1u) / g_cur_speed_q16);
	}
}

/* TRUE-621 (W300): a BAKE's stop lands on a whole BAKED block. The close
 * machinery chose tgt in recorder units (a whole recorder block: what was
 * played, the grid beat, the chop cycle or the bar); a bake stores
 * tgt/spd of that, which is not a whole number of baked blocks, and 619
 * rounded the LOOP to one -- the recorder's fade-out then fell outside the
 * loop and every seam clicked (marc: "no longer a perfect loop"). Here the
 * TARGET moves instead: N = the nearest whole baked block (never snapping
 * back on an immediate stop, so the fade + pad always exist), and the
 * recorder runs to exactly the frames the kernel needs for those N blocks
 * (the last output's p0 + its 3-frame lookahead). Promotion uses this N when the loop is the
 * recorded length (g_bk_len); a snapped-back loop keeps its own length.
 * <= half a baked block from the musical length, like every loop here. */
/* SPEEDBAKE-768: the input frames the kernel needs for n baked blocks -- the last
 * output's p0 + its 3-frame lookahead -- given the integral so far (acc, Q16 baked
 * frames for the rcf frames recorded) and the speed now. Past or future alike: the
 * distance from acc to the last output, converted at cur. At a constant speed this
 * is TRUE-621's ((n*fpb-1)*spd>>16)+4 exactly. */
static uint32_t __attribute__((noinline)) bk_need(uint32_t n, uint32_t fpb, uint32_t rcf, uint64_t acc, uint32_t cur)
{
	const uint64_t out = ((uint64_t)(n * fpb - 1u) << 16);
	int64_t need = (int64_t)rcf + 4;
	if (out >= acc) need += (int64_t)((((out - acc) >> 8) * cur) >> 24);
	else            need -= (int64_t)((((acc - out) >> 8) * cur) >> 24);
	if (need < 0) need = 0;
	return (uint32_t)need;
}

static uint32_t __attribute__((noinline)) bnc_bake_target(int i, uint32_t tgt, uint8_t sil)
{
	const uint32_t spd = g_bk_spd;
	g_bk_len = 0u; g_bk_lens = 0u;
	if (!spd) return tgt;
	const uint32_t fpb = trk[i].p16m ? 248u : 140u;
	/* SPEEDBAKE-768: the baked frames at the target = the integral so far (what the kernel
	 * consumes for the frames already recorded) + the rest at the speed now. At a constant
	 * speed this is tgt/spd exactly as before; through a sweep it is what was heard. */
	const uint32_t cur = bk_rel(g_cur_speed_q16);   /* SPEEDBAKE2-769 */
	const uint32_t rcf = trk[i].rec_count >> 1, tf = tgt >> 1;
	const uint64_t acc = ((uint64_t)g_bk_accf << 16) + g_bk_accr;
	uint64_t at = acc;
	if (tf >= rcf) at += ((uint64_t)(tf - rcf) << 32) / cur;   /* Q16 */
	else { const uint64_t back = ((uint64_t)(rcf - tf) << 32) / cur; at = (at > back) ? at - back : 0u; }
	uint32_t n = (uint32_t)((at + ((uint64_t)fpb << 15)) / ((uint64_t)fpb << 16));   /* nearest whole baked block */
	if (n < 1u) n = 1u;
	uint64_t need = bk_need(n, fpb, rcf, acc, cur);   /* the last output's p0 + the lookahead */
	if (sil && need * 2u < (uint64_t)trk[i].rec_count) {   /* in emissions: an immediate stop never snaps back */
		n += 1u;
		need = bk_need(n, fpb, rcf, acc, cur);
	}
	if (n > MAX_LOOP_BLOCKS) n = MAX_LOOP_BLOCKS;
	if (trk[i].len_blocks * (fpb * 2u) == tgt) g_bk_len = n;   /* the loop IS the recorded length */
	{	/* BAKELEN-727 through the sweep: the sample-exact LOOP length, in baked samples -- the integral
		 * at the loop's frames. BAKELATE-776: in every case, not only the recorded-length one: a
		 * snapped-back loop (a late stop) is shorter than the recording, and its baked length is the
		 * integral backed off from the end at the speed now (the overhang was just recorded at ~it);
		 * one speed for the whole take was 2x wrong after an octave. */
		const uint32_t lf = trk[i].len_samps >> 1;
		uint64_t ls = acc;
		if (lf >= rcf) ls += ((uint64_t)(lf - rcf) << 32) / cur;   /* Q16 */
		else { const uint64_t back = ((uint64_t)(rcf - lf) << 32) / cur; ls = (ls > back) ? ls - back : 0u; }
		g_bk_lens = (uint32_t)(((ls + 32768u) >> 16) * 2u);
	}
	return (uint32_t)(need * 2u);
}

/* The cycle a bounce stopped under CHOP rounds to (BNC2-603), in track i's
 * blocks: the song base in fixed mode, the longest playing source otherwise;
 * 0 = no rounding (chop off, nothing to measure against, or too long). */
static uint32_t __attribute__((noinline)) bnc_cycle_blocks(int i)
{
	uint32_t cyc = 0u;
	if (!(g_bnc_on && (g_chop_div > 1u || g_win_free))) return 0u;
	if (g_fixed_len && TLOOPB(i)) {
		cyc = TLOOPB(i);
	} else {
		for (int _k = 0; _k < NTRK; _k++) {
			if (_k == i || trk[_k].state != TS_PLAY || !trk[_k].len_blocks) continue;
			uint32_t _bk = (uint32_t)(((uint64_t)trk[_k].len_blocks * TSPBI(_k)
			                            + TSPBI(i) / 2u) / TSPBI(i));
			if (_bk > cyc) cyc = _bk;
		}
	}
	if (cyc > MAX_LOOP_BLOCKS) cyc = 0u;
	return cyc;
}

/* Before PASS A on a bounce block: keep the jack, put the PRE-EMPHASISED,
 * limited, 12 dB-down bus into the input buffer; PASS A records it with the
 * code it always had.
 * PRE-608 (W292): one trip through the 24 kHz store is cos^3(pi f/48k) --
 * the record boxcar is cos, the playback midpoint upsample cos^2. The
 * sources carry one trip already; the print would carry two. This 9-tap
 * symmetric FIR (Q8, linear phase, 4-sample delay) is ~1/cos^3 to 10 kHz
 * and a lowpass above 13 kHz (nothing for the boxcar to fold back), so the
 * print comes back at the sources' brightness: within +/-0.7 dB to 10 kHz
 * end-to-end, where 607 was -2.0 dB @6k / -3.6 @8k / -5.2 @10k. Worst-case
 * gain 1.8x, inside the 12 dB print headroom. Bounce-only, outside the
 * mixer. */
static void __attribute__((noinline)) bnc_prepass(int16_t *tmp, const int32_t *mL, const int32_t *mR)
{
	int32_t l0 = g_bnc_pre_hist[0][0], l1 = g_bnc_pre_hist[0][1], l2 = g_bnc_pre_hist[0][2], l3 = g_bnc_pre_hist[0][3],
	        l4 = g_bnc_pre_hist[0][4], l5 = g_bnc_pre_hist[0][5], l6 = g_bnc_pre_hist[0][6], l7 = g_bnc_pre_hist[0][7];
	int32_t r0 = g_bnc_pre_hist[1][0], r1 = g_bnc_pre_hist[1][1], r2 = g_bnc_pre_hist[1][2], r3 = g_bnc_pre_hist[1][3],
	        r4 = g_bnc_pre_hist[1][4], r5 = g_bnc_pre_hist[1][5], r6 = g_bnc_pre_hist[1][6], r7 = g_bnc_pre_hist[1][7];
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		g_bnc_live[2u * f]      = tmp[2u * f];
		g_bnc_live[2u * f + 1u] = tmp[2u * f + 1u];
		const int32_t xl = mL[f], xr = mR[f];   /* newest = n; centre tap = n-4 */
		const int32_t yl = (205 * l4 + 97 * (l3 + l5) - 58 * (l2 + l6) - 49 * (l1 + l7) + 35 * (l0 + xl)) >> 8;
		const int32_t yr = (205 * r4 + 97 * (r3 + r5) - 58 * (r2 + r6) - 49 * (r1 + r7) + 35 * (r0 + xr)) >> 8;
		tmp[2u * f]      = soft_limit(yl >> BNC_GSH);
		tmp[2u * f + 1u] = soft_limit(yr >> BNC_GSH);
		l0 = l1; l1 = l2; l2 = l3; l3 = l4; l4 = l5; l5 = l6; l6 = l7; l7 = xl;
		r0 = r1; r1 = r2; r2 = r3; r3 = r4; r4 = r5; r5 = r6; r6 = r7; r7 = xr;
	}
	g_bnc_pre_hist[0][0] = l0; g_bnc_pre_hist[0][1] = l1; g_bnc_pre_hist[0][2] = l2; g_bnc_pre_hist[0][3] = l3;
	g_bnc_pre_hist[0][4] = l4; g_bnc_pre_hist[0][5] = l5; g_bnc_pre_hist[0][6] = l6; g_bnc_pre_hist[0][7] = l7;
	g_bnc_pre_hist[1][0] = r0; g_bnc_pre_hist[1][1] = r1; g_bnc_pre_hist[1][2] = r2; g_bnc_pre_hist[1][3] = r3;
	g_bnc_pre_hist[1][4] = r4; g_bnc_pre_hist[1][5] = r5; g_bnc_pre_hist[1][6] = r6; g_bnc_pre_hist[1][7] = r7;
}

/* After PASS A on a bounce block: the monitor seed was the bus; make it the
 * jack again, exactly (int32: what was added is what is subtracted). And on
 * the take's FIRST recording block: anchor one block of tape earlier (sample
 * 0 is the bus from one block ago) and set the print gain. Playback reads
 * neither before promotion. */
static void __attribute__((noinline)) bnc_postpass(int32_t *mL, int32_t *mR, const int16_t *tmp, uint32_t got_live)
{
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		int32_t lL = (f < got_live) ? (int32_t)g_bnc_live[2u * f]      : 0;
		int32_t lR = (f < got_live) ? (int32_t)g_bnc_live[2u * f + 1u] : 0;
		mL[f] += lL - (int32_t)tmp[2u * f];
		mR[f] += lR - (int32_t)tmp[2u * f + 1u];
	}
	int _rt = g_rec_track;
	if (!g_bnc_anch && _rt >= 0 && trk[_rt].state == TS_REC) {
		g_bnc_anch = 1u;
		uint32_t _lag = (uint32_t)(((uint64_t)(BLK_FRAMES + BNC_PRE_HALF) * g_cur_speed_q16) >> 16);   /* PRE-608: + the FIR delay */
		uint32_t sp = trk[_rt].start_samps;
		sp = (sp >= _lag) ? (sp - _lag) : 0u;
		trk[_rt].start_samps = sp;
		trk[_rt].start_blk   = (sp + TSPBI(_rt) / 2u) / TSPBI(_rt);
		trk[_rt].gsh = (uint8_t)BNC_GSH;
		/* BAKE-619 (W298): the print bakes the speed it was punched at */
		g_bk_spd = (g_bk_mode != 1u && g_cur_speed_q16 != BK_ONE) ? g_cur_speed_q16 : 0u;   /* TAPECOPY-684: a tape copy never bakes */
		g_bk_trk = (int8_t)_rt; g_bk_ph = 0u; g_bk_blocks = 0u; g_bk_len = 0u;   /* TRUE-621 */
		if (g_bk_spd) g_bk_prints++;
		g_bk_accf = 0u; g_bk_accr = 0u; g_bk_rc_prev = 0u; g_bk_lens = 0u;   /* SPEEDBAKE-768 */
		g_bk_ref = (g_cur_speed_q16 < 12288u) ? 12288u : g_cur_speed_q16; g_bk_m1 = 0u;   /* SPEEDBAKE2-769: the anchor speed */
		for (uint32_t _k = 0; _k < SL_N; _k++) g_sl_rc[_k] = 0u;   /* SPDLOG-777: a fresh log */
		g_sl_wr = 0u; g_sl_base = trk[_rt].r_w - trk[_rt].rec_count;
	}
	/* SPEEDBAKE-768 (row 121): the tape may move during a bounce. Per block: the integral
	 * of the baked frames (recorded frames / the speed they were recorded at, Q16 -- what
	 * the kernel will consume, ~150 ms later, at ~this speed); a HELD print leaving 1x joins
	 * the bake (its flushed blocks are baked blocks 1:1); a run-on stop's target walks with
	 * the speed so the recorder ends on exactly the n baked blocks the stop chose. */
	if (_rt >= 0 && g_bk_trk == (int8_t)_rt && trk[_rt].state == TS_REC) {
		const uint32_t _rc = trk[_rt].rec_count;
		const uint32_t _df = (_rc - g_bk_rc_prev) >> 1;   /* frames recorded since the last block */
		{	/* SPDLOG-777: this block's speed, at the position it ends -- the rc lands last (a reader sees 0 = empty until then) */
			const uint8_t _w = (uint8_t)(g_sl_wr & (SL_N - 1u));
			uint32_t _c = g_cur_speed_q16; if (_c < 12288u) _c = 12288u;
			g_sl_rc[_w] = 0u; g_sl_sp[_w] = _c; __asm__ volatile("" ::: "memory"); g_sl_rc[_w] = _rc;
			g_sl_wr = (uint8_t)(_w + 1u);
		}
		g_bk_rc_prev = _rc;
		if (g_bk_mode == 1u && !g_bk_m1) {   /* SPEEDBAKE2-769: the verdict is a tape copy -- the frames so far are the tape 1:1 */
			g_bk_m1 = 1u; g_bk_accf = _rc >> 1; g_bk_accr = 0u;
		}
		const uint32_t _sp = bk_rel(g_cur_speed_q16);   /* SPEEDBAKE2-769: relative to the print's reference */
		if (g_bk_mode != 0u && g_bk_spd == 0u && _sp != BK_ONE) {   /* the tape left the print's reference speed: bake from here */
			g_bk_ph = 0u; g_bk_spd = _sp; g_bk_prints++;
		}
		const uint64_t _q = (((uint64_t)_df << 32) / _sp) + g_bk_accr;   /* Q16 baked frames */
		g_bk_accf += (uint32_t)(_q >> 16); g_bk_accr = (uint16_t)(_q & 0xFFFFu);
		if (g_bk_spd && g_bk_len && trk[_rt].rec_target && !trk[_rt].rec_silence) {
			/* TRUE-621 walked: the frames the kernel needs for n blocks, from here, at the speed now */
			uint32_t _tg = bk_need(g_bk_len, trk[_rt].p16m ? 248u : 140u, _rc >> 1, ((uint64_t)g_bk_accf << 16) + g_bk_accr, _sp) * 2u;
			if (_tg < _rc) _tg = _rc;   /* already there: the recorder finalises on its next sample */
			trk[_rt].rec_target = _tg;
		}
	}
}

/* PASSA-726 (W329, Stack P2 rung 2): the ARMED path -- the punch wait, the pre-roll
 * rescue, the head-recovery scan and the take start -- OUT of PASS A's frame loop.
 * Runs once per emitted sample only while the record track is ARMED; the loop's
 * hoisted locals come in through the context and the two it writes go back. The
 * body is the loop's own text, unchanged (the harness gate proves it bit-exact). */
struct pa_ctx { uint32_t cpos, pre_w, pre_val, pphase, fsince; int64_t dec_acc, dec_accR; };
/* PASSA2-786: PASS A's frame loop for the IDLE transport (a loop playing, nothing armed / recording /
 * flushing / bouncing): the same arithmetic, the block-constant reads hoisted, the accumulators 32-bit.
 * Entry guard (the caller's): g_loop_active, g_rec_track < 0, !g_done_pending, step >= 4096 (an emit
 * every <= 16 frames, so fsince never reaches the int64 divide branch), fsince < 0x8000. Host-proven
 * bit-exact against the original loop (this build's harness). Its own function: W291. */
struct pa_idle { uint32_t pphase, fsince, pre_w, pre_val, cpos; int64_t dec_acc, dec_accR; };
static void __attribute__((optimize("O2"), noinline)) pa_idle_block(const int16_t *tmp, uint32_t got, uint32_t step,
										  uint32_t *posb, uint16_t *fracb, struct pa_idle *st)
{
	uint32_t pphase = st->pphase, cpos = st->cpos, pre_w = st->pre_w, pre_val = st->pre_val, fs = st->fsince;
	uint32_t acc = (uint32_t)st->dec_acc, accR = (uint32_t)st->dec_accR;   /* the low 32 bits: every consumer truncates there first */
	int32_t hL = g_cd_holdL, hR = g_cd_holdR;
	const uint32_t mdiv = (!g_grid_active && g_midi_div) ? g_midi_div : 0u;   /* both written below this thread's priority: block constants */
	uint32_t mcnt = g_midi_cnt, mprod = 0u;
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		const int32_t liveL = (f < got) ? (int32_t)tmp[2u * f]      : 0;
		const int32_t liveR = (f < got) ? (int32_t)tmp[2u * f + 1u] : 0;
		mix32[f] = liveL; mix32R[f] = liveR;
		posb[f] = cpos; fracb[f] = (uint16_t)(pphase & 0xFFFFu);
		acc += (uint32_t)liveL; accR += (uint32_t)liveR; fs++;
		pphase += step;
		while (pphase >= 65536u) {
			pphase -= 65536u;
			int16_t lsamp, rsamp;
			if (fs == 0u)      { lsamp = (int16_t)liveL; rsamp = (int16_t)liveR; }
			else if (fs == 1u) { lsamp = (int16_t)acc;   rsamp = (int16_t)accR; }
			else { lsamp = (int16_t)((int32_t)acc / (int32_t)fs); rsamp = (int16_t)((int32_t)accR / (int32_t)fs); }   /* hw SDIV, as before */
			acc = 0u; accR = 0u; fs = 0u;
			if ((pre_w & 1u) == 0u) { hL = lsamp; hR = rsamp; }   /* CD-463: the 24k pre-roll store, same pair grid */
			else { const uint32_t fi = ((pre_w >> 1) & RRING_MASK);
			       g_rring[fi * 2u]      = (int16_t)((hL + (int32_t)lsamp + 1) >> 1);
			       g_rring[fi * 2u + 1u] = (int16_t)((hR + (int32_t)rsamp + 1) >> 1); }
			pre_w++;
			if (pre_val < PREROLL_MAX) pre_val++;
			cpos++;
			if (mdiv && ++mcnt >= mdiv) { mcnt = 0u; mprod++; }
		}
	}
	g_cd_holdL = (int16_t)hL; g_cd_holdR = (int16_t)hR;
	g_midi_cnt = mcnt; if (mprod) g_midi_clk_produced += mprod;
	st->pphase = pphase; st->cpos = cpos; st->pre_w = pre_w; st->pre_val = pre_val; st->fsince = fs;
	st->dec_acc = (int64_t)(int32_t)acc; st->dec_accR = (int64_t)(int32_t)accR;   /* <= 16 frames since the last emit: exact */
}
static void __attribute__((noinline)) pa_armed(int rt_i, int16_t lsamp, uint32_t f, struct pa_ctx *c)
{
	uint32_t _cpos = c->cpos, _pre_w = c->pre_w, _pre_val = c->pre_val, _pphase = c->pphase, _fsince = c->fsince;
	int64_t  _dec_acc = c->dec_acc, _dec_accR = c->dec_accR;
	/* AUTO-START: hold armed until the input first crosses
	 * the threshold. NO TIMEOUT any more: the old ~4 s
	 * fallback started recording SILENCE on its own
	 * (community: "once armed it should only rely on sound
	 * input... after 8 tics it starts on its own"). An
	 * armed track now waits indefinitely — tap it to
	 * cancel; the blinking LED shows it is armed. */
	struct looptrk *rt = &trk[rt_i];
	int32_t aa = lsamp < 0 ? -lsamp : lsamp;
	int trigger;
	uint32_t pre_backfill = 0u;
	if (g_grid_active && g_grid_beat_frames && g_grid_punch_at) {
		/* M8b PUNCH-IN: start on the scheduled line.
		 * M22-B: SAMPLE-exact — g_sample_clock is the
		 * block START (it advances once per 256-frame
		 * block), so comparing it raw fired every
		 * punch 0-5.33 ms late, quantized to block
		 * edges. The frame index makes it exact; the
		 * bench showed the quantization as per-take
		 * scatter on top of the drift. */
		uint64_t now_f = g_sample_clock + f;
		trigger = (now_f >= g_grid_punch_at);
		/* M20 PRE-ROLL RESCUE: the punch is waiting
		 * for the NEXT line — but if the PREVIOUS one
		 * is less than half a beat back and still
		 * inside the pre-roll memory, start there
		 * instead and fill the gap in. The punch
		 * lands on the NEAREST line either way, so a
		 * press just after the beat is as good as a
		 * press just before it. */
		if (!trigger && _pre_val &&
		    now_f > g_grid_anchor_e) {
			uint64_t unit = g_grid_beat_frames;
			uint64_t off = now_f - g_grid_anchor_e;
			/* GP-506: this runs once per EMITTED SAMPLE for the
			 * whole ARMED punch wait -- up to a full beat. The
			 * 64/64 divide compiled to bl __aeabi_uldivmod
			 * (~150-190 cyc incl. 14 stack accesses). Both
			 * operands fit in 32 bits in every reachable case:
			 * `unit` is a beat in frames (72,000 at the 40 BPM
			 * floor) and `off` only reaches 2^32 if the anchor is
			 * more than 24.9 h old. Unsigned division of values
			 * below 2^32 yields the same quotient at either
			 * width, and the 64-bit arm is KEPT for the rest, so
			 * this is bit-exact by construction. */
			uint64_t _q;
			if (!(off >> 32) && !(unit >> 32))
				_q = (uint64_t)((uint32_t)off /
					       (uint32_t)unit);
			else
				_q = off / unit;
			uint64_t prev = g_grid_anchor_e + _q * unit;
			uint64_t back = now_f - prev;
			uint32_t need = (uint32_t)
				((back * g_cur_speed_q16) >> 16);
			/* M20b-r2 REACH LIMIT: half a beat back
			 * was ambiguous — a press 250 ms after a
			 * line usually MEANT the next line, and
			 * reaching back made the take a whole beat
			 * too long, so its head repeated at its
			 * tail. Only a genuinely LATE finger gets
			 * rescued: a quarter beat, capped in ms. */
			/* M25-r9: a THIRD of a beat. r5 tried this,
			 * r6 reverted it on suspicion, and the bisect
			 * then cleared it outright — the half-beat
			 * jump was a truncating clamp in the
			 * convergence rescale, present since M22-C
			 * and reproducible on M24-R1, which predates
			 * the reach change entirely. So this returns
			 * on its own merits: 156 ms of forgiveness at
			 * 128 instead of 117, still well short of the
			 * half beat that caused the head-repeats. */
			uint64_t reach = unit / 3u;
			uint64_t rcap = (uint64_t)
				(I2S_TRUE_HZ / 1000u) *
				PREROLL_REACH_MS;
			if (reach > rcap) reach = rcap;
			if (back <= reach && need &&
			    need <= _pre_val) {
				pre_backfill = need;
				g_grid_punch_at = prev;
				trigger = 1;
			}
		}
	} else {
		/* trigger directly on the first sample past
		 * threshold (no running-peak tracking) */
		trigger = (aa >= SOUND_THRESHOLD);
		/* M41 HEAD RECOVERY: the rule above is
		 * UNCHANGED — but the first sound may have
		 * arrived while the button was still in its
		 * 100/180 ms arm window, and shipped code then
		 * started the take mid-note. Scan the pre-roll
		 * ring BACKWARDS in 64-sample windows while
		 * each window's peak stays above the threshold
		 * and adopt that tail: the take begins at the
		 * sound's ONSET. Caps: the PRESS
		 * (g_arm_press_sclk, engine frames -> ring
		 * samples via tape speed — recording never
		 * reaches back before the finger) and
		 * _pre_val. Silence at arm = shipped exact.
		 * Same ring adoption as the gridded rescue
		 * below; no provisional phase, no catch-up
		 * burst. g_instant_rec clear = classic: skip
		 * the scan, shipped behavior bit-for-bit. */
		if (trigger && g_instant_rec && _pre_val) {
			uint64_t now_f = g_sample_clock + f;
			uint64_t backf = (g_arm_press_sclk &&
			                  now_f > g_arm_press_sclk)
			               ? (now_f - g_arm_press_sclk) : 0u;
			uint32_t cap = (uint32_t)
				((backf * g_cur_speed_q16) >> 16);
			if (cap > _pre_val) cap = _pre_val;
			uint32_t n = 0u;
			while (n + 64u <= cap) {
				int32_t pk = 0;
				for (uint32_t k = 1u; k <= 64u; k++) {
					int32_t sv = g_rring[(((_pre_w - n - k) >> 1) & RRING_MASK) * 2u]; /* CD-463: stored 24k, L */
					if (sv < 0) sv = -sv;
					if (sv > pk) pk = sv;
				}
				if (pk < SOUND_THRESHOLD) break;
				n += 64u;
			}
			pre_backfill = n;
		}
	}
	if (trigger) {
		/* CD-463: the pair grid is absolute — the take's flush
		 * origin (r_r = _pre_w - backfill) must be EVEN so stored
		 * pairs align with block boundaries. Shrink the backfill
		 * one frame (inaudible); if it reaches 0 the no-backfill
		 * branch resets r_w=r_r=0, which is even. */
		if (pre_backfill && ((_pre_w - pre_backfill) & 1u))
			pre_backfill--;
		if (g_loop_len == 0u) {
			/* first take: this sound is loop position 0
			 * (M20: with pre-roll, position 0 is the
			 * grid line we reached back to, so the
			 * playhead is already that far in) */
			_cpos = pre_backfill;
			g_midi_start_pending = 1; g_midi_cnt = 0;
			tempo_reset();
			rt->flush_blk = 0; rt->flush_mod = MAX_LOOP_BLOCKS;
			rt->rec_target = 0;
			rt->start_blk = 0;        /* the base take anchors the grid at 0 */
			rt->start_samps = 0;
			rt->len_samps = 0;   /* set at the stop */
			rt->len_blocks = 0;       /* set when the held length is known */
		} else {
			/* INDEPENDENT LOOPS: an overdub is an OPEN take
			 * exactly like the first — it records until the
			 * user taps the track again (or MAX), then loops
			 * at ITS OWN length on its own cycle. No
			 * quantization to the first track's grid, no
			 * silence padding while you hunt for the loop
			 * point. start_blk anchors playback to where
			 * recording began; length is set at stop.
			 * Linear flush, no wrap. */
			rt->flush_blk = 0; rt->flush_mod = MAX_LOOP_BLOCKS;
			rt->rec_target = 0;
			/* C-r3: arm the onset estimator for THIS
			 * take. It only ever armed on first takes,
			 * so an overdub's stop read a STALE
			 * first_onset from take 1 — the landmark
			 * collapsed into pure punch spacing, the
			 * convergence measured the grid against
			 * itself, railed at the cap and DIVERGED
			 * (bench: -60 then -143 ms; diag showed
			 * cnv=128/16 then 203/14 against a truth
			 * of -4). Each take now lands its own
			 * landmark; the leak fix still retires
			 * the estimator at every gridded stop. */
			tempo_reset();
			{	/* M20: an overdub anchors where
				 * recording BEGAN — the reached-back
				 * line, not the moment of the punch */
				uint32_t sp = _cpos;
				sp = (sp >= pre_backfill)
				   ? (sp - pre_backfill) : 0u;
				/* M22-A: NEAREST, not truncate — the
				 * old floor put every overdub 0-5.3 ms
				 * early, always early. Full sample
				 * anchors are Phase B. */
				rt->p16m = rt->p16m_next;   /* GS2-532: the toggled record mode */
				rt->start_blk = (sp + TSPB(rt) / 2u)
				              / TSPB(rt);
				rt->start_samps = sp;   /* M22-B: exact */
				rt->len_samps = 0;
			}
		}
		if (pre_backfill) {
			/* the pre-rolled tail IS the take's head:
			 * the ring already holds it, so simply
			 * adopt those indices (no copy). */
			rt->r_r = _pre_w - pre_backfill;
			rt->r_w = _pre_w;
			rt->rec_count = pre_backfill;
			{	/* SEAMX-727: those pairs never pass the recorder -- copy the head now */
				uint32_t _hn = pre_backfill >> 1; if (_hn > 64u) _hn = 64u;
				for (uint32_t _j = 0; _j < _hn; _j++) {
					const uint32_t _fi = ((((_pre_w - pre_backfill) >> 1) + _j) & RRING_MASK);
					g_seam_head[_j][0] = g_rring[_fi * 2u];
					g_seam_head[_j][1] = g_rring[_fi * 2u + 1u];
				}
			}
		} else {
			rt->r_w = 0; rt->r_r = 0; rt->rec_count = 0;
		}
		_pre_val = 0;   /* the ring belongs to the take now */
		rt->rec_silence = 0;
		if (g_grid_active && g_grid_beat_frames && g_grid_punch_at) {
			/* M8b: beat length in STORED samples at
			 * the punch-in tape speed (recording
			 * follows the tape). */
			/* GRIDCORE-733: with a loop the beat IS loop_len / n (exact); only the
			 * first take of a tapped song derives it from the clock. The punch line's
			 * beat-in-bar is kept: the first take's stop turns it into the "1". */
			if (g_loop_len && g_slot < NUM_SLOTS && g_grid_n[g_slot])
				g_gridrec_beat_samps = (g_loop_len + g_grid_n[g_slot] / 2u) / g_grid_n[g_slot];
			else
				g_gridrec_beat_samps = (uint32_t)
					(((uint64_t)g_grid_beat_frames *
					  g_cur_speed_q16) >> 16);
			g_grid_punch_k = (uint8_t)(((g_grid_punch_at - g_grid_anchor_e) / g_grid_beat_frames) & 3u);
			g_gridrec = 1;
			if (g_loop_len == 0u && !g_grid_fresh) {
				/* M8b-r2: your first loop IS the
				 * downbeat from here on (untapped
				 * grids only — M20 F1: a fresh
				 * TAPPED grid keeps its own phase;
				 * the punch already landed on it) */
				g_grid_anchor = g_grid_punch_at + (g_grid_anchor - g_grid_anchor_e);   /* STACKT-716: punch_at came from the effective anchor */
				g_grid_anchor_e = g_grid_punch_at;
				g_grid_punch_k = 0u;   /* GRIDCORE-733: your take IS the 1 */
			}
		} else {
			g_gridrec = 0;
		}
		g_grid_punch_at = 0;
		/* M90: publish the hoisted playhead BEFORE any state
		 * transition -- the streamer may observe the new state
		 * and must not then read a stale g_consume_pos. */
		{ g_pphase = _pphase; g_dec_acc = _dec_acc; g_dec_accR = _dec_accR;
		  g_frames_since = _fsince; g_pre_w = _pre_w;
		  g_pre_valid = _pre_val; g_consume_pos = _cpos; }
		/* P14S: every new take is raw. Shaper restarts at
		 * the punch; shadow machinery off for this track. */
		g_p14s_e1[0] = 0; g_p14s_e1[1] = 0; g_p14s_sh = 0u;
		g_p14s_prev[rt_i][0] = 0; g_p14s_prev[rt_i][1] = 0;
		g_p14s_mask |= (uint8_t)(1u << rt_i);
		rt->p16m = rt->p16m_next;   /* GS2-532 */
		rt->state = TS_REC;
	}
	c->cpos = _cpos; c->pre_val = _pre_val;
}

/* PASSA-726 / W328: no VFP registers in the mixer -- a 64-bit literal parked in a d-register
 * aligns the section to 8 and the linker sorts it to the front of flash (the 574/593 shape). */
static void looper_audio_block(int16_t *s) __attribute__((target("general-regs-only")));

/* O2FIX-591 (W282): the mixer carried __attribute__((optimize("O2"))) on the
 * line above its signature since cd416ba (2026-07-05). EFXM2-588 inserted
 * fx_chain_block() between that line and the signature, so from 588 to 590
 * PASS A/B and the lean loop compiled at the global -Os. Put it back, ON the
 * signature, where no future insertion can orphan it. */
static void __attribute__((optimize("O2"), noinline)) looper_audio_block(int16_t *s)
{
	int16_t *const tmp = g_live_blk;   /* INFX-672: file scope, the MIX slot needs the jack */
	if (g_xfer_mode) { memset(s, 0, BLK_BYTES); return; }   /* USB transfer: silence out */
	uint32_t _lt81 = DWT->CYCCNT;   /* M81 phase clock */
	/* M8X: one predicate per BLOCK, not per frame. Same gate as W4X. */
	const int _cx81 = (g_cur_speed_q16 >= CX_SPEED_MIN) && (g_rec_track >= 0);
	const int _dx81 = (g_cur_speed_q16 >= CX_SPEED_MIN) && (g_rec_track < 0);
	if (_cx81) g_cph_blk++;
	if (_dx81) g_dph_blk++;
	{ /* M46d: sprint flag with hysteresis — ON under 4096 smp (~85 ms),
	   * OFF at 4800 smp (~100 ms); steady-state fill is ~110 ms. */
	  uint32_t _sp_c = g_consume_pos; int _sp_low = 0;
	  for (int _sp_i = 0; _sp_i < NTRK; _sp_i++)
	    if (trk[_sp_i].state == TS_PLAY &&
	        (int32_t)(trk[_sp_i].p_w - _sp_c) <
	            (g_emmc_sprint ? (int32_t)4800 : (int32_t)4096)) {
	      _sp_low = 1; break;
	    }
	  { /* M87: the rec-ring flush needs the SAME storm immunity the
	     * play rings got in 2.7.2. Measured (M86-r2, raw indices):
	     * flush sustains ~160 blk/s under USB vs 171.4 demand; the
	     * 8,192-frame ring is lapped ~1 s into a take and capture
	     * corrupts. M55-r3 proved 376 blk/s at sprint priority under
	     * the same storm. Pressure = >2 pages (2x16 blocks) queued;
	     * the existing 150/15 duty cycle bounds the boost. */
	    int _rt87 = g_rec_track;
	    if (_rt87 >= 0 && trk[_rt87].state == TS_REC &&
	        (trk[_rt87].r_w - trk[_rt87].r_r) > 2u * 16u * TSPBI(_rt87))
		_sp_low = 1;
	  }
	  g_w4_tq++; if (_sp_low) g_w4_sq++;   /* W4P */
	  g_emmc_sprint = (uint8_t)_sp_low;
	}
	/* PREBUFFER: do not start draining a freshly-(re)enabled stream until the
	 * ring holds FB_SETPOINT frames — the feedback regulator over-delivers to
	 * fill it in ~20 ms. Without this gate the consumer races the empty ring and
	 * the first moments of every host play start dribble out as choppy fragments
	 * (this gate existed in the old direct path but was lost in the looper). */
	static bool primed;
	/* SCHED-LOCKED cluster: the mixer is PREEMPT(0) now, so the COOP USB
	 * threads can preempt it mid-ring_buf_get — and the terminal-toggle
	 * callback resets this ring (documented unsafe against a concurrent
	 * get). The lock (~tens of us) restores exactly the atomicity the old
	 * COOP(7) mixer had for this cluster; USB ISO service only needs to
	 * preempt the ~ms-scale MIX work below, never this copy. */
	k_sched_lock();
	if (!g_usb_streaming)
		primed = false;
	else if (!primed &&
		 ring_buf_size_get(&usb_audio_ring) >= FB_SETPOINT * USB_FRAME_BYTES)
		primed = true;
	if (primed) {
		/* diag: usb-in ring fill watermarks — the LIVE INPUT is the record
		 * source, and none of the eMMC counters can see it starve. */
		int32_t _uf = (int32_t)(ring_buf_size_get(&usb_audio_ring) / USB_FRAME_BYTES);
		if (_uf < g_usb_lowat) g_usb_lowat = _uf;
		if (_uf > (int32_t)g_usb_hiwat) g_usb_hiwat = (uint32_t)_uf;
		/* U3-471: same sample, cumulative (survives the blind run) */
		if ((uint32_t)_uf > g_u3_ring_hi) g_u3_ring_hi = (uint32_t)_uf;
		if ((uint32_t)_uf < g_u3_ring_lo) g_u3_ring_lo = (uint32_t)_uf;
	}
	uint32_t bytes = primed ?
		ring_buf_get(&usb_audio_ring, (uint8_t *)tmp, sizeof(g_live_blk)) : 0;   /* INFX-672: the whole block (1,024 B), not sizeof(pointer) */
	k_sched_unlock();
	uint32_t got = bytes / USB_FRAME_BYTES;
	g_live_got = got;   /* INFX-672 */
	if (g_in_on) in_slot_block(tmp, got, mix32, mix32R);   /* INFX-672: the input slot (W291 shape); skips itself on a bounce block */
	if (primed && got < BLK_FRAMES) {
		g_ring_underruns++;
		g_zero_pad += BLK_FRAMES - got;   /* silence frames injected (and recorded) */
	}

	/* FAILSAFE — exactly one recorder. Every block, find the single ARMED/REC
	 * track and make g_rec_track the one source of truth; if a second recorder
	 * somehow appeared, demote it back to play/empty. This guarantees recording
	 * can only ever touch one track at a time, no matter what races upstream. */
	{
		int only = -1;
		for (int i = 0; i < NTRK; i++) {
			uint8_t st = trk[i].state;
			if (st != TS_ARMED && st != TS_REC) continue;
			if (only < 0) only = i;
			else trk[i].state = (g_slot < NUM_SLOTS &&
					     g_meta.slot[g_slot].present[i]) ? TS_PLAY : TS_EMPTY;
		}
		g_rec_track = only;
	}

	/* PROVISIONAL AUTO-CONFIRM (engine-side, control-loop-independent): once
	 * a provisional take has captured ~150 ms of real material it is clearly
	 * not a transit graze (grazes abort within ~100 ms via the press-edge
	 * guard) — confirm it so the streamer starts flushing well inside the
	 * rec ring's ~341 ms horizon even if the control loop is frozen
	 * (FUNCTION page / USB transfer) before its own confirm could run.
	 * Without this a frozen control loop left a zombie RAM-only take that
	 * overflowed its ring and was discarded at the eventual stop tap. */
	/* DOUBLE-TAP DELETE: clear the track — abort any take it has in flight,
	 * drop it from the song, persist. If it was the song's only content, the
	 * song resets to empty (the next take sets a fresh loop length). Ring
	 * indices are NOT touched here: a mid-flush streamer pass may still be
	 * draining them (finite, writes land in the deleted track's own region);
	 * every take start re-zeroes them anyway. */
	for (int i = 0; i < NTRK; i++) {
		if (!g_del_req[i]) continue;
		g_del_req[i] = 0;
		if (g_rec_track == i) g_rec_track = -1;
		if (trk[i].state == TS_DONE)
			g_done_pending = 0;   /* M22-A: deleting a still-flushing take
			                       * must free the rec ring, or pre-roll is
			                       * silently OFF for the session (the only
			                       * other clear is the promotion, which
			                       * needs TS_DONE to still be true). Only
			                       * one take can flush at a time, so there
			                       * is never a second one to protect. */
		trk[i].state = TS_EMPTY;
		trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].rec_count = 0; trk[i].muted = 0;
		trk[i].len_blocks = 0; trk[i].start_blk = 0; trk[i].content_blocks = 0;  /* drop all its segments */
		trk[i].len_samps = 0; trk[i].start_samps = 0;
		if (g_slot < NUM_SLOTS) {
			g_meta.slot[g_slot].present[i] = 0;
			g_meta.slot[g_slot].trk_len[i] = 0;
			g_meta.slot[g_slot].trk_start[i] = 0;
			g_meta.trk_content[g_slot][i] = 0;   /* keep the on-flash block self-consistent */
			{	/* FX3-537: the x3 row dies with the take (the removed
				 * save-service refresh used to do this). rsv survives:
				 * the per-song preference outlives its takes. */
				struct x3_trk *_xe = &g_x3.t[g_slot][i];
				_xe->start_samps = 0; _xe->len_samps = 0;
				_xe->content_blocks = 0;
				_xe->codec_id = 0; _xe->flags = 0;   /* PLACE-715: pan survives, as rsv does */
			}
			g_meta.song_mode[g_slot] &= (uint8_t)~(uint8_t)(0x10u << i); /* M7-r4: unmute */
		}
		int any = 0;
		for (int k = 0; k < NTRK; k++)
			if (trk[k].state != TS_EMPTY ||
			    (g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[k]))
				any = 1;
		if (!any) {
			g_loop_len = 0; g_loop_blocks = 0; g_loop_active = 0;
			g_grid_base_beats = 0; g_grid_base_blocks = 0;   /* M20 F7 */
			if (g_slot < NUM_SLOTS) {
				g_meta.slot[g_slot].loop_len = 0;
				g_meta.song_mode[g_slot] = 0;   /* M7c: unstamp */
				g_meta.chop[g_slot][0] = 0;     /* M7a: unchop  */
				g_meta.chop[g_slot][1] = 0;
			}
			g_chop_div = 1; g_chop_off = 0;
			g_win_free = 0; g_win_rev = 0;          /* M16 */
			g_fixed_len = g_mode_pref;              /* rejoin global */
		}
		g_meta_save_req = 1;
	}

	/* HOLD-TO-RECORD. A track button held down records that track; releasing it
	 * stops. The FIRST take starts immediately and its hold duration sets the
	 * master length (snapped to whole bars on release); later tracks (overdubs)
	 * arm and begin on the next beat, in sync. Only one track records at a time. */

	/* RELEASE -> stop the current take */
	if (g_stop_req) {
		g_stop_req = 0;
		int i = g_rec_track;
		if (i >= 0 && i < NTRK) {
			if (trk[i].state == TS_ARMED) {
				/* cancelled before any sound — or while still PROVISIONAL
				 * (an empty-track instant arm whose press turned out to be
				 * a transit graze toward a higher button). A provisional
				 * take has only captured into RAM (PASS 1 skips its flush),
				 * so aborting leaves NO trace: no flash write, no junk
				 * take, no grid/BPM hijack on an empty song, and the
				 * transport state the arm forced is put back. */
				/* -> back to PLAY/EMPTY. */
				trk[i].state = (g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[i])
					       ? TS_PLAY : TS_EMPTY;
				trk[i].gsh = g_arm_gsh_prev;                          /* BNC3-605 */
				if (g_bnc_on) trk[i].p16m_next = g_bnc_p16m_prev;   /* BNC3-605 */
				g_rec_track = -1;
				g_grid_punch_at = 0;   /* M8b: cancel the scheduled punch */
				if (g_loop_len == 0u) {
					/* A sole-track re-record ARM reset the song grid and the
					 * playhead. A cancel must UNDO that damage: restore the
					 * saved grid, and re-anchor every playing ring to the
					 * (reset) playhead — without this the track replays one
					 * stale ~341 ms ring chunk for as long as the song had
					 * been running (PASS 2 believes the ring is pinned full)
					 * and the NEXT take hijacks the song grid. */
					if (g_slot < NUM_SLOTS && g_meta.slot[g_slot].loop_len) {
						g_loop_len = g_meta.slot[g_slot].loop_len;
						g_loop_blocks = (g_loop_len + SAMP_PER_BLK / 2u) / SAMP_PER_BLK;
					}
					int anyp = 0;
					for (int k = 0; k < NTRK; k++)
						if (trk[k].state == TS_PLAY) {
							anyp = 1;
							trk[k].p_w = g_consume_pos;  /* starve -> clean refill */
						}
					if (!anyp) g_loop_active = 0;
				}
			} else if (trk[i].state == TS_REC) {
				/* FREE-LENGTH stop (every take): the loop is EXACTLY what was
				 * recorded — no quantization to the first track's grid, no
				 * silence padding while you hunt for the loop point. Rounded
				 * only to the 256-sample storage block (~±2.7 ms, inaudible)
				 * so the eMMC streaming stays block-aligned. The FIRST take
				 * of a song additionally defines the beat grid + BPM (LEDs,
				 * MIDI clock); later tracks free-run on their own cycles. */
				/* CONTENT length = the audio actually recorded, rounded UP to a
				 * whole block so nothing is lost. rec_target is set to CONTENT,
				 * not the (possibly longer) loop length, so the recorder pads
				 * only this final <1 block and finalises INSTANTLY on the tap. */
				/* R2 (perfect-loop): backdate the stop by the constant
				 * gesture latency so the end lands on the finger, not
				 * on the pipeline. */
				/* M96: the gesture latency is not constant -- it stretches
				 * with main's CPU share, so a fixed 55 ms backdate leaves the
				 * take ending LATE under load. Use the measured value.
				 * FLOORED at the old constant so the healthy case cannot
				 * regress, CEILINGed at 8x so a pathological reading can
				 * never eat the take. 48 samples per ms at 48 kHz. */
				uint32_t _comp96 = g_stop_lat_ms * 48u;
				if (_comp96 < STOP_COMP_SAMPLES)      _comp96 = STOP_COMP_SAMPLES;
				if (_comp96 > STOP_COMP_SAMPLES * 8u) _comp96 = STOP_COMP_SAMPLES * 8u;
				uint32_t rc = trk[i].rec_count;
				if (rc > _comp96 + TSPBI(i))
					rc -= _comp96;
				uint32_t content = (rc + TSPBI(i) - 1u)
						   / TSPBI(i);
				if (content < 1u) content = 1u;
				else if (content > MAX_LOOP_BLOCKS) content = MAX_LOOP_BLOCKS;
				/* M8b QUANTIZED STOP: a grid-punched take rounds to the
				 * NEAREST grid beat. The beat is block-rounded ONCE and
				 * shared by every grid take -> all lengths are multiples
				 * of the same base and stay locked to each other. */
				uint32_t glen = 0, gbb = 0, gbeats = 0;
				if (g_gridrec && g_gridrec_beat_samps) {
					if (g_loop_len == 0u) {
						/* M20 F8: trust the recording over the
						 * tapping — refine the beat from the
						 * onsets this take just captured, and
						 * retune the grid to match. */
						uint32_t rf =
							tempo_refine(g_gridrec_beat_samps);
						/* M43: measured, recorded, NOT applied —
						 * the tapped/snapped grid is the clock. */
						if (SP1_GRID_FOLLOW && rf) {
							grid_retune(g_gridrec_beat_samps, rf);
							g_det_bpm = (int)(((uint64_t)LOOP_RATE *
								60u + rf / 2u) / rf);
							/* r7: beat_set already derived the rec beat
							 * from the grid. Re-assigning it here is
							 * exactly what let the two come apart. */
						}
					}
					gbb = (g_gridrec_beat_samps + TSPBI(i) / 2u)
					      / TSPBI(i);
					if (gbb < 1u) gbb = 1u;
					/* M20 F7: count beats from the PRECISE beat length,
					 * then ask for that many beats' worth of blocks —
					 * never beats-times-a-rounded-beat. */
					uint64_t csam = (uint64_t)content * TSPBI(i);
					uint32_t gm = (uint32_t)((csam +
						g_gridrec_beat_samps / 2u) / g_gridrec_beat_samps);
					if (gm < 1u) gm = 1u;
					uint32_t nearest = grid_len_blocks(gm, TSPBI(i));
					if (g_loop_len == 0u) {
						/* M8b-r3 FIRST TAKE: TRIM-BACK policy — the
						 * stop is instant and a loop can never
						 * contain silence. Run on only in the
						 * "nailed it" window (last ~15% of a beat);
						 * otherwise snap DOWN to the last whole
						 * beat (overhang stays on flash, unplayed).
						 * Tap-tempo drift made the old run-to-the-
						 * line wait long enough to read as "still
						 * recording" (user report). */
						uint32_t flb = (uint32_t)(csam /
							       g_gridrec_beat_samps);
						uint32_t fl = flb ? grid_len_blocks(flb, TSPBI(i)) : 0u;
						/* M20 F2: round to the NEAREST beat (window
						 * 16% -> 50%), matching the overdub policy.
						 * Bench: the trim-back cost marc a beat at
						 * his best (7 for an intended 8) and left a
						 * 7-vs-8 polymeter. Max run-on = half a beat
						 * with the double-blink cue. */
						uint32_t win = gbb / 2u;
						if (content < gbb) {
							glen = grid_len_blocks(1u, TSPBI(i));
							gbeats = 1u;    /* degenerate: complete 1 beat */
						} else if (nearest > fl &&
						         (nearest - content) <= win) {
							glen = nearest; /* tiny run-on to the line */
							gbeats = gm;
						} else {
							glen = fl;      /* trim back — instant */
							gbeats = flb;
						}
					} else {
						glen = nearest;         /* overdub: fixed-style */
						gbeats = gm;
					}
					if (glen > MAX_LOOP_BLOCKS) glen = 0;  /* fall back free */
				}
				if (g_loop_len == 0u) {
					uint32_t base = glen ? glen : content;
					/* M22-B: a gridded take's loop is EXACTLY its beats
					 * times the true beat — no longer forced onto a
					 * flash-block boundary. 8 beats at 128 BPM is
					 * 180000 samples now, not 179968: the loop and the
					 * source can no longer diverge, which is the whole
					 * mechanism behind "layers drift apart" (bench:
					 * -32 samples/lap = 10.7 ms/min at 128, and worse
					 * elsewhere; 120 BPM was one of only 12 tempos in
					 * 60-200 that could not show it). Blocks stay what
					 * the flash reads; samples are what the loop IS. */
					if (glen && gbeats && g_gridrec_beat_samps) {
						/* GRIDLAP-806 (W351): the song's loop is the lap the STREAMER SERVES -- whole blocks --
						 * not the sample-exact beat product, which is generally not a block multiple. The two
						 * differed by up to half a block on every gridded song (80 samples on marc's), and the
						 * metronome, the MIDI clock, the bar quantiser and every beat line used the wrong one.
						 * GRIDCORE-733: the tape is the clock. The gridded branch now ends as the ungridded does. */
						g_loop_len = base * TSPBI(i);
						grid_adopt_loop(gbeats, trk[i].start_samps, g_grid_punch_k);   /* GRIDCORE-733: adopts against the corrected L */
					} else {
						g_loop_len = base * TSPBI(i);
						if (g_slot < NUM_SLOTS) { g_grid_n[g_slot] = 0u; g_grid_o[g_slot] = 0u; }   /* GRIDCORE-733: an ungridded loop */
					}
					g_loop_blocks = base;
					if (glen && gbb) {
						/* the TAPPED grid defines the beat — exact
						 * stored-domain beat, not the estimator.
						 * The estimator is DONE here: this branch
						 * never called tempo_finish(), so it was
						 * left running for the rest of the session
						 * and kept sampling through every later
						 * overdub for an answer nobody reads. */
						g_tempo.active = 0;
						/* M22-A: THE GRID FOLLOWS THE LOOP. The loop
						 * wraps at a block-quantized length, so the
						 * beat it can actually honour is stored/beats
						 * — up to 128 samples per take away from the
						 * true beat. v2.5.0 kept the TRUE beat on the
						 * grid, so grid lines slid past the loop at
						 * up to ~9 samples a beat and later overdubs
						 * punched visibly off the layers already
						 * down (bench: +16 ms in 85 s at 128 BPM).
						 * The loop is the instrument; the grid now
						 * tunes itself to what the loop plays.
						 * (Residual: the song as a whole still runs
						 * at the quantized tempo vs an external
						 * source — that is M22 Phase B's job.) */
						if (gbeats) {
							/* M22-B: with sample-exact lengths the
							 * achievable beat IS the true beat, so
							 * this Phase-A retune self-disarms; it
							 * still guards the fallback paths. */
							uint32_t ach = (uint32_t)
								((g_loop_len + gbeats / 2u) / gbeats);
							if (ach && ach != g_gridrec_beat_samps) {
								grid_retune(g_gridrec_beat_samps, ach);
								g_det_bpm = (int)(((uint64_t)LOOP_RATE *
									60u + ach / 2u) / ach);   /* r7 */
							}
						}
						g_beat_samples = g_gridrec_beat_samps;
						g_midi_div = g_gridrec_beat_samps / 24u;
						if (gbeats) {   /* M20 F7: the base to lock to */
							g_grid_base_beats = gbeats;
							g_grid_base_blocks = glen;
						}
					} else
					tempo_finish();         /* set the detected beat grid + BPM */
					if (g_slot < NUM_SLOTS) {
						g_meta.slot[g_slot].loop_len = g_loop_len;
						g_meta.song_mode[g_slot] = (uint8_t)
							((g_meta.song_mode[g_slot] & 0xF0u) |
							 (g_fixed_len ? 2u : 1u)); /* M7c stamp */
						g_meta_save_req = 1;
					}
				}
				/* BNC2-603: a bounce stopped while the CHOP is on prints whole
				 * cycles, so the chop cuts the print the way it cuts its sources
				 * (a print shorter than the cycle is chopped relative to itself:
				 * marc heard the last part of the loop repeating). Cycle = the
				 * song base in fixed mode, the longest playing source otherwise,
				 * in THIS track's blocks. 0 = no rounding (chop off, or nothing
				 * to measure against). */
				uint32_t _bnc_cyc = bnc_cycle_blocks(i);   /* BNC3-606 (BNC2-603): 0 unless a bounce under chop */
				if (_bnc_cyc) { glen = 0; gbeats = 0; }   /* the cycle, not the beat, is the unit */
				uint32_t len = content;
				uint32_t tgt = content * TSPBI(i);   /* default: stop now */
				uint8_t  sil = 1;                        /* pad the final sub-block */
				if (trk[i].rec_target && !trk[i].rec_silence) {
					/* SECOND tap while a fixed-mode take is running on to
					 * the bar line (below): stop IMMEDIATELY — the loop
					 * keeps the already-snapped bar length; the unfilled
					 * remainder plays as silence (the old behavior, as an
					 * escape hatch). */
					len = trk[i].len_blocks;
					if (g_gridrec && g_gridrec_beat_samps) {
						/* M8b-r3: on a GRID take the escape trims to
						 * the last WHOLE beat instead of amputating
						 * mid-beat and leaving a silent tail. */
						uint32_t nb2 = (uint32_t)((uint64_t)trk[i].rec_count /
									  g_gridrec_beat_samps);
						uint32_t bb2 = grid_len_blocks(1u, TSPBI(i));
						if (nb2 >= 1u) {
							uint32_t fl2 = grid_len_blocks(nb2, TSPBI(i));
							if (fl2 >= bb2) {
								len = fl2;
								content = fl2;
								tgt = fl2 * TSPBI(i);
							}
						}
					}
				} else if (glen) {
					/* M8b: grid take — same early/late machinery as
					 * fixed mode, with the tapped beat as the base:
					 * EARLY -> run on to the grid line capturing live;
					 * LATE -> snap back (overhang never plays). */
					len = glen;
					if (glen * TSPBI(i) > trk[i].rec_count) {
						content = glen;
						tgt = glen * TSPBI(i);
						sil = 0;
					}
				} else if (_bnc_cyc || (g_fixed_len && TLOOPB(i))) {
					/* BNC3-607: a bounce under CHOP rounds to its chop cycle with
					 * exactly this machinery (BNC2-603); everything else is the
					 * fixed-mode bar rounding it always was. */
					const uint32_t _rbase = _bnc_cyc ? _bnc_cyc : TLOOPB(i);
					/* FIXED mode: round to the NEAREST whole multiple of
					 * the base — ceil-only rounding gapped BOTH ways
					 * (community: stop a hair early and the tail padded
					 * with silence; a hair late and nearly a whole extra
					 * bar of silence was appended).
					 *  - stopped EARLY (before the nearest bar): the tap
					 *    SCHEDULES the stop — recording runs on to the bar
					 *    line capturing live audio, so the loop ends ON
					 *    the bar with real sound in it (the emit path
					 *    fades the final ~2.7 ms into the seam). The track
					 *    LED stays on until the bar; tap again to force an
					 *    immediate stop.
					 *  - stopped LATE (past the nearest bar): snap BACK to
					 *    it — the overhang stays on flash but is never
					 *    played (promotion fades the new seam). */
					uint32_t mult = (content + _rbase / 2u) / _rbase;
					if (mult < 1u) mult = 1u;
					uint32_t nlen = mult * _rbase;
					if (nlen <= MAX_LOOP_BLOCKS) {
						len = nlen;
						if (nlen * TSPBI(i) > trk[i].rec_count) {
							/* EARLY: run on to the bar, capturing live */
							content = nlen;
							tgt = nlen * TSPBI(i);
							sil = 0;
						}
					}
					/* nlen over the region: len stays = content, stop now */
				}
				trk[i].content_blocks = content;     /* audio ends here */
				trk[i].len_blocks     = len;         /* loop length */
				/* M22-B: the sample-exact wrap. Gridded takes are whole
				 * beats of the TRUE beat; everything else keeps the
				 * block length exactly as before (their loop-vs-grid
				 * question does not exist). */
				trk[i].len_samps = (glen && gbeats && g_gridrec_beat_samps)
						 ? gbeats * g_gridrec_beat_samps
						 : len * TSPBI(i);
				/* DIAGDEL-733: the M22c convergence (dead under SP1_GRID_FOLLOW 0 since M43) and its diag are gone. */
				trk[i].rec_target     = bnc_bake_target(i, tgt, sil);   /* TRUE-621: a bake stops on a whole baked block */
				/* end the live phrase. When padding (immediate stops), the
				 * pad used to be hard zeros — a click baked into the seam;
				 * fade the first 128 pad samples (~2.7 ms) down instead. */
				trk[i].rec_silence = sil;
				g_gridrec = 0;
				if (sil) {
					trk[i].rec_fade = 128;
					/* the pad is only rec_target - rec_count samples
					 * (0..255); steepen the slope so the fade always
					 * COMPLETES inside it. */
					uint32_t pad = trk[i].rec_target - trk[i].rec_count;
					trk[i].rec_fstep = (pad && pad < 128u)
						? (uint8_t)((128u + pad - 1u) / pad) : 1u;
				}
			}
		}
	}

	/* PRESS -> start recording that track (if nothing else is recording) */
	for (int i = 0; i < NTRK; i++) {
		if (!g_arm_req[i]) continue;
		g_arm_req[i] = 0;
		const int _bnc_i = (g_bnc_arm == (int8_t)i);   /* BNC-597: this arm is a bounce */
		if (_bnc_i) g_bnc_arm = -1;
		if (!g_emmc_ready) continue;
		if (g_rec_track >= 0) continue;                       /* one at a time */
		/* ONE take in flight, device-wide: refuse while ANY track is armed,
		 * recording, or still flushing (TS_DONE). The rec ring is SHARED, so a
		 * second take during a drain would interleave into the same buffer; and
		 * two flushes would also exceed the eMMC write budget. The press becomes
		 * valid the moment the drain finishes (sub-second; LED solid meanwhile). */
		int busy = 0;
		for (int k = 0; k < NTRK; k++) {
			uint8_t st = trk[k].state;
			if (st == TS_REC || st == TS_ARMED || st == TS_DONE) busy = 1;
		}
		if (busy) continue;
		/* sole track in the song -> allow a fresh length (reset only the in-RAM
		 * master; the saved length is rewritten when this new take completes).
		 * ANY non-empty state on another track counts as "others" — including
		 * TS_DONE (a take still flushing to the card): resetting the length while
		 * another take is mid-flush would corrupt it. */
		int others = 0;
		for (int k = 0; k < NTRK; k++)
			if (k != i && (trk[k].state != TS_EMPTY ||
				       (g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[k])))
				others = 1;
		if (_bnc_i && !others) {
			/* BNC-597: a bounce needs something to bounce. main checks this
			 * too; this is the engine's own word on it. */
			g_led_shrug = 20;
			continue;
		}
		if (!others) { g_loop_len = 0; g_loop_blocks = 0; g_loop_active = 0;
			       g_grid_base_beats = 0; g_grid_base_blocks = 0; }

		if (g_loop_len == 0u) {
			/* FIRST take: start the transport NOW so the recorder can watch the
			 * input, but DON'T capture yet — recording begins at the first sound
			 * (auto-start), at which point the playhead is reset so that sound is
			 * loop position 0. Snap the tape speed so no spin-up ramp is baked in. */
			g_cur_speed_q16 = g_play_speed_q16;   /* snap to the set tape speed */
			g_loop_active = 1; g_consume_pos = 0;
			g_pphase = 0; g_frames_since = 0; g_dec_acc = 0; g_dec_accR = 0;
		}
		/* ARM (first take AND overdub): wait for the first sound, then the tick
		 * handler begins the capture so the loop starts exactly on the audio. */
		trk[i].r_w = 0; trk[i].r_r = 0; trk[i].flush_blk = 0;
		trk[i].flush_mod = MAX_LOOP_BLOCKS;
		trk[i].rec_count = 0; trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].muted = 0;
		if (g_slot < NUM_SLOTS)   /* M7-r4: a fresh take is audible — unmute */
			g_meta.song_mode[g_slot] &= (uint8_t)~(uint8_t)(0x10u << i);
		trk[i].wait_peak = 0; trk[i].wait_ticks = 0;
		if (g_grid_active && g_grid_beat_frames) {
			if (g_loop_len == 0u && !g_grid_fresh) {
				/* M8b-r5 (kept for UNTAPPED grids): the first take
				 * punches immediately and places the downbeat —
				 * your take IS the "1". The r2 contradiction (wait
				 * for a phase, then discard it) stays resolved this
				 * way HERE; on fresh-tapped grids it's resolved the
				 * other way below (M20 F1: keep the phase). */
				g_grid_punch_at = g_sample_clock ? g_sample_clock : 1u;
			} else {
				/* M20 F1+F3: overdubs — and FIRST takes on a
				 * FRESHLY TAPPED grid (the taps said "sync to
				 * this") — punch on the next BEAT line. F3: beat,
				 * not bar — the <=2 s bar count-in was the "way
				 * too late" report; the wait is <=1 beat now, and
				 * stops still snap to whole beats so every loop
				 * stays locked. The armed LED fast-blinks. */
				uint64_t unit = (uint64_t)g_grid_beat_frames;
				/* A-r2: the FIRST take on a fresh grid schedules
				 * from the PRESS — pressing before the line catches
				 * that line exactly (the trigger fires the moment
				 * the arm lands if the line just passed: bounded
				 * ~25-30 ms worst case, never a full-beat wait).
				 * Overdubs keep arm-time scheduling: their 180 ms
				 * hold is a deliberate content-track gesture filter,
				 * and arming early achieves line-exact there. */
				uint64_t ref = (g_loop_len == 0u && g_arm_press_sclk &&
						g_arm_press_sclk > g_grid_anchor_e)
					     ? g_arm_press_sclk : g_sample_clock;
				uint64_t off = ref - g_grid_anchor_e;
				g_grid_punch_at = g_grid_anchor_e +
					((off + unit - 1u) / unit) * unit;
			}
			g_arm_press_sclk = 0;
		} else {
			g_grid_punch_at = 0;
		}
		/* NOTE: len_blocks/start_blk are NOT reset here -- they are set when the
		 * first sound lands (TS_REC). Leaving them intact means a re-record that
		 * is cancelled (released before any sound) returns the track to PLAY with
		 * its ORIGINAL length, not a clobbered one. */
		if (_bnc_i) bnc_arm_prep(i);   /* BNC3-606: mode, pre-roll, block-exact punch */
		trk[i].state = TS_ARMED;
		g_rec_track = i;
		g_bnc_on = _bnc_i ? 1u : 0u;   /* BNC-597: every take start says what its source is */
		g_arm_gsh_prev = trk[i].gsh;   /* BNC3-605: a NEW take has no print gain (a cancel puts it back) */
		trk[i].gsh = 0u;
	}

	/* HOLD PLAY -> jump to the start of the song and play. Rewind the shared
	 * playhead to 0 and reset every track's read frontier so the streamer refills
	 * each loop from its first block; they all restart together, in sync. Ignored
	 * while recording (so a take isn't disrupted). */
	if (g_restart_req) {
		g_restart_req = 0;
		if (g_rec_track < 0 && g_loop_active) {
			g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0; g_dec_accR = 0; g_midi_cnt = 0;
			for (int i = 0; i < NTRK; i++) trk[i].p_w = 0;
			g_playing = 1;
			g_midi_start_pending = 1;
		}
	}

	/* CHOP CHANGE: drop the (old-window) read-ahead so the new window is
	 * audible within one refill round (~20-40 ms, boundary-faded by the
	 * starve machinery) instead of after ~341 ms of stale ring. */
	if (g_chop_req) {
		g_chop_req = 0;
		for (int i = 0; i < NTRK; i++)
			if (trk[i].state == TS_PLAY)
				trk[i].p_w = (g_consume_pos / TSPBI(i)) * TSPBI(i);
	}

	/* SONG SWITCH: reload the tracks for the newly-selected slot. Tracks that the
	 * slot already has recorded -> PLAY (streamer refills from that slot's eMMC
	 * region from block 0); empty tracks -> ready to record. Restart the loop. */
	if (g_slot_switch_req) {
		g_slot_switch_req = 0;
		g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0; g_dec_accR = 0; g_midi_cnt = 0;
		g_rec_track = -1;
		/* this song's remembered loop length (0 = empty, ready for a fresh take) */
		g_loop_len    = (g_slot < NUM_SLOTS) ? g_meta.slot[g_slot].loop_len : 0;
		g_loop_blocks = (g_loop_len + SAMP_PER_BLK / 2u) / SAMP_PER_BLK;
		int any = 0;
		for (int i = 0; i < NTRK; i++) {
			uint8_t pres = (g_slot < NUM_SLOTS) ? g_meta.slot[g_slot].present[i] : 0;
			trk[i].state = pres ? TS_PLAY : TS_EMPTY;
			trk[i].p_w = 0;
			trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].rec_count = 0;
			/* M7-r4: the song's saved mutes come back with it */
			trk[i].muted = (pres && g_slot < NUM_SLOTS &&
			                (g_meta.song_mode[g_slot] & (0x10u << i))) ? 1u : 0u;
			{	/* FX2-536: the per-song NEXT-mode preference loads for
				 * EVERY track -- the old latch sat inside the presence
				 * guard, so empty tracks (the main use case) never
				 * restored it (marc: "songs not saving the settings"). */
				uint8_t _rv = (g_x3_ok && g_slot < NUM_SLOTS)
				            ? g_x3.t[g_slot][i].rsv : 0u;
				if (_rv & 0x80u)
					trk[i].p16m_next = (uint8_t)(_rv & 1u);
				if (!pres) {
					trk[i].p16m = 0;   /* empty: no take, clean geometry */
					trk[i].gsh  = 0;   /* BNC2-604 */
					if (!(_rv & 0x80u)) trk[i].p16m_next = 0;
				}
			}
			/* SEGMENT: restore this track's own length + phase anchor (older saves
			 * with trk_len==0 fall back to the base length = one segment). */
			if (pres && g_slot < NUM_SLOTS) {
				trk[i].p16m = (g_x3_ok && g_x3.t[g_slot][i].codec_id
				               == X3_CODEC_P16M) ? 1u : 0u;   /* P16-522:
				               * BEFORE the TSPBI(i) length math below */
				trk[i].gsh  = g_x3_ok ? (uint8_t)((g_x3.t[g_slot][i].flags >> 1) & 3u) : 0u;   /* BNC2-604 */
				{	/* PS-535: per-song NEXT-mode preference. */
					uint8_t _rv = (g_x3_ok && g_slot < NUM_SLOTS)
					            ? g_x3.t[g_slot][i].rsv : 0u;
					trk[i].p16m_next = (_rv & 0x80u)
					                 ? (uint8_t)(_rv & 1u)
					                 : trk[i].p16m;   /* old cards: follow the take */
				}
				uint32_t L = g_meta.slot[g_slot].trk_len[i];
				trk[i].len_blocks = L ? L : TLOOPB(i);
				{	/* M22-B: rebuild the sample length from the stored
					 * master (loop_len is SAMPLES and now carries the
					 * true length). A track is a whole multiple of the
					 * base, so multiple x master = its exact samples.
					 * Anchors reload block-rounded (<=2.7 ms once per
					 * load) — the known Phase-B limit; live sessions
					 * are sample-exact. */
					uint32_t Lb = trk[i].len_blocks;
					uint32_t ms = g_loop_len;
					if (ms && Lb && (ms % TSPBI(i)) != 0u) {
						uint32_t mult = (uint32_t)
							(((uint64_t)Lb * TSPBI(i) +
							  ms / 2u) / ms);
						if (!mult) mult = 1u;
						trk[i].len_samps = mult * ms;
					} else {
						trk[i].len_samps = Lb * TSPBI(i);
					}
				}
				trk[i].start_blk  = g_meta.slot[g_slot].trk_start[i];
				/* M25 BUG FIX: this used to sit one line ABOVE the
				 * load, so it read the anchor belonging to the
				 * PREVIOUSLY loaded song (or 0 at boot) — not the
				 * block-rounded anchor the comment above claims, an
				 * arbitrary one. First takes anchor at block 0 and
				 * survived; overdubs came back at the wrong phase
				 * after a song switch or power-cycle. Introduced by
				 * M22-B, found while planning M25. */
				trk[i].start_samps = trk[i].start_blk * TSPBI(i);
				trk[i].content_blocks = g_meta.trk_content[g_slot][i]; /* 0 = whole track */
				/* M72: adopt the EXACT persisted values when the v3
				 * entry is consistent with the index (torn/stale ->
				 * keep the block-derived fallback above). */
				if (g_x3_ok && g_slot < NUM_SLOTS) {
					const struct x3_trk *xe = &g_x3.t[g_slot][i];
					uint32_t Lb2 = trk[i].len_blocks;
					if (xe->len_samps && Lb2 &&
					    /* r2: the INDEX rounds half-up, not ceil */
					    ((xe->len_samps + TSPBI(i) / 2u) /
					     TSPBI(i)) == Lb2 &&
					    (xe->start_samps / TSPBI(i)) ==
					     trk[i].start_blk) {
						trk[i].len_samps   = xe->len_samps;
						trk[i].start_samps = xe->start_samps;
					}
				}
			} else {
				trk[i].len_blocks = 0; trk[i].start_blk = 0;
				trk[i].len_samps = 0; trk[i].start_samps = 0;
				trk[i].content_blocks = 0;
			}
			if (pres) any = 1;
		}
		gridlap_load();   /* GRIDLAP-806: the loop becomes the lap the streamer will serve */
		g_loop_active = any && (g_loop_len > 0);
	}

	/* ---- TAPE-EFFECT speed smoothing (once per block, like the SP-1) ----
	 * target = the rocker speed when playing, 0 when stopped. A one-pole filter
	 * glides the actual speed toward the target by 2% per block, giving the tape
	 * ramp on play/pause AND on tempo changes. Recording does NOT force 1.0x any
	 * more: capture ticks in the loop-sample domain at the current speed, so an
	 * overdub lands exactly as heard at ANY speed — and there's no pitch JUMP
	 * when record starts/stops. The Q16 step feeds the resampler below. */
	uint32_t target_q16 = g_playing ? g_play_speed_q16 : 0u;
	int32_t sd = (int32_t)target_q16 - (int32_t)g_cur_speed_q16;
	if (sd > -64 && sd < 64) g_cur_speed_q16 = target_q16;                 /* snap when ~there */
	else g_cur_speed_q16 = (uint32_t)((int32_t)g_cur_speed_q16 + sd / 50); /* one-pole ~2%/block */
	/* At exact unity, drop any fractional-phase residue left by the spin-up
	 * ramp (one-time <=1/2-sample jump, inaudible): otherwise frac stays
	 * nonzero forever and every playing track pays the interpolation
	 * multiply per frame despite running at 1.0x. */
	if (g_cur_speed_q16 == 65536u && g_playing && (g_pphase & 0xFFFFu))
		g_pphase &= ~0xFFFFu;
	uint32_t step = g_cur_speed_q16 / DECIM;                              /* Q16 per I2S frame */

	/* Snapshot per-track fader volume once per block. vol_q8 is volatile (reloaded
	 * every frame at -Os), but its only writer is the lower-priority controls path
	 * and the mixer (PREEMPT 0) outranks it, so it is constant
	 * across the 256 frames -- the snapshot is bit-identical and drops ~1024 reloads. */
	uint16_t vol_s[NTRK];
	uint8_t  gs_s[NTRK];   /* BNC2-604: the print gain shift, per track, this block */
	for (int i = 0; i < NTRK; i++) {
		/* BNC2-604: the print gain rides the per-block volume hoist -- a
		 * head plays its SOURCE take, so it takes the source's gain. */
		const uint8_t _gs = head_active(i) ? trk[g_head_src].gsh : trk[i].gsh;
		gs_s[i]  = _gs;
		vol_s[i] = (uint16_t)((uint32_t)trk[i].vol_q8 << _gs);
	}

	M81_LAP(0);
	/* ==== M90: PASS A ran on VOLATILES. Every reference was a forced
	 * reload; ~60 of them per inner-loop iteration, which is why the
	 * loop cost 338 cycles (634 recording) for a decimate and one ring
	 * store. Hoist the audio-thread-private ones into locals for the
	 * duration of the block and publish once at the end.
	 *
	 * SAFE because the STREAMER CANNOT RUN INSIDE PASS A: audio is
	 * PREEMPT(0), streamer PREEMPT(5), and PASS A has no blocking call.
	 * The streamer already only ever saw whole-block snapshots.
	 *
	 * NOT hoisted, deliberately: trk[].r_w (g_rring[] is non-volatile,
	 * so hoisting r_w lets the compiler sink array stores past the
	 * publish -- needs a barrier), g_rec_track (needs a finalize
	 * publication point), and trk[].state / trk[].r_r / g_done_pending
	 * (the STREAMER writes those -- read-only, never written back).
	 *
	 * g_dec_acc stays 64-bit: paused with a loop present, step rounds
	 * to 0, the inner while never fires, and it accumulates unbounded
	 * at 48 kHz -- int32 would overflow in ~1.37 s. */
	uint32_t _pphase   = g_pphase;
	int64_t  _dec_acc  = g_dec_acc;
	int64_t  _dec_accR = g_dec_accR;
	uint32_t _fsince   = g_frames_since;
	uint32_t _pre_w    = g_pre_w;
	uint32_t _pre_val  = g_pre_valid;
	uint32_t _cpos     = g_consume_pos;
	const uint8_t _lactive = g_loop_active;
	/* BNC-597: this block records the BUS, not the jack. Tied to g_rec_track
	 * so a stale flag can never outlive its take. */
	const int _bnc = (g_bnc_on && g_rec_track >= 0);
	/* ==== PASS A: transport + record, stashing per-frame positions ====
	 * The old single loop interleaved all four tracks' mixing into every
	 * frame, paying loop + volatile-read overhead 4 x 48000 times a second
	 * even for empty tracks — measured with the kernel's thread stats at
	 * 31% CPU stopped / 40% playing, which starved the eMMC streamer below
	 * the refill rate it needed (the cut-outs while recording track 4).
	 * Restructured into per-block passes: A) advance transport + record
	 * exactly as before, stashing each frame's playhead position + phase;
	 * B) one tight accumulation loop per PLAYING track; C) master volume +
	 * limiter + stereo write-out. Arithmetic, ordering and per-frame starve
	 * semantics are unchanged — the loops are merely inverted so per-track
	 * invariants hoist out of the 48 kHz hot path. */
	static uint32_t posb[BLK_FRAMES];
	static uint16_t fracb[BLK_FRAMES];
	/* INFX-672: mix32 / mix32R are file-scope statics now (declared with the
	 * input-path globals); same storage, same code, reachable by the slot. */
	/* BNC3-605 THE SOURCE SWAP, OUTSIDE THE FRAME LOOP (W291). mix32/mix32R
	 * still hold the previous block's post-FX bus (the lean loop only reads
	 * them). On a bounce block: keep the jack, put the limited, 12 dB-down
	 * bus into tmp[] and let PASS A record it with the code it always had.
	 * The monitor gets the jack back right after PASS A (below). */
	uint32_t got_live = got;   /* BNC3-606 */
	if (_bnc) { bnc_prepass(tmp, mix32, mix32R); got = BLK_FRAMES; }
	/* WOBBUS-673: the wobble is on the bus (wob_tick_block + in_wobble_block in the chain) */
	/* BNC-570 B1: consume the PREVIOUS block's PASS C tap. This is exactly
	 * the handoff a real bounce needs -- PASS A runs before PASS C, so the
	 * print is one block (5.33 ms) late by construction. The counter
	 * advancing at all is the proof the plumbing works. */
	{
		uint32_t _bl = g_bt_lat;
		g_bt_acc = (g_bt_acc * 7u + _bl) >> 3;   /* slow mean, no divide */
		g_bt_n++;
	}
	if (_lactive && g_rec_track < 0 && !g_done_pending && step >= 4096u && _fsince < 0x8000u) {
		/* PASSA2-786: the idle transport (a loop playing, nothing recording) -- its own function */
		struct pa_idle _pi = { _pphase, _fsince, _pre_w, _pre_val, _cpos, _dec_acc, _dec_accR };
		pa_idle_block(tmp, got, step, posb, fracb, &_pi);
		_pphase = _pi.pphase; _fsince = _pi.fsince; _pre_w = _pi.pre_w; _pre_val = _pi.pre_val; _cpos = _pi.cpos;
		_dec_acc = _pi.dec_acc; _dec_accR = _pi.dec_accR;
	} else
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		/* The UAC2 input is a stereo pair and always has been; the
		 * engine simply summed it away. liveL/liveR feed the monitor;
		 * `live` keeps the EXACT former value so every record-path
		 * consumer stays bit-exact (asserted in this stage's gate). */
		int32_t liveL = (f < got) ? (int32_t)tmp[2 * f]      : 0;
		int32_t liveR = (f < got) ? (int32_t)tmp[2 * f + 1]  : 0;
		int32_t live  = (liveL + liveR) >> 1;

		/* (the first take is started immediately by the press handler above, and
		 * overdubs are started on the next beat by the wrap logic below) */
		mix32[f] = liveL; mix32R[f] = liveR;    /* the live monitor is STEREO.
		                                         * PASS B and PASS C were already
		                                         * two-channel (M63a stages 2, 3);
		                                         * only this fetch was still mono. */
		/* TAPE-569 r2: a fast path that is BIT-IDENTICAL when the offset is
		 * zero. The rc2 click came from JUMPING between two different read
		 * positions; skipping arithmetic whose result is provably 0 changes
		 * nothing and cannot click. Without this the wobble cost every block
		 * of every build, page closed and fader down.
		 * At depth 0 the ramp settles at exactly 0, so idle playback carries
		 * no added latency, and the engage/disengage transition is a glide. */
		posb[f]  = _cpos;   /* WOBBUS-673: the plain read position, always */
		fracb[f] = (uint16_t)(_pphase & 0xFFFFu);

		/* advance the playback phase; each integer step is one loop-sample tick */
		/* S2CAP: accumulate TRUE channels. `live` (the downmix) still
		 * feeds threshold/tempo consumers; the RECORDED signal is now
		 * the real L and the real R. */
		_dec_acc += liveL; _dec_accR += liveR; _fsince++;
		if (_lactive) {
			_pphase += step;
			while (_pphase >= 65536u) {
				_pphase -= 65536u;
				/* Decimate the live input to the current tape rate.
				 * >1x (frames_since==0, a 2nd+ emit in one input frame):
				 * HOLD the current sample instead of emitting a zero — the
				 * old zero-stuffing was the metallic aliasing/bitcrush. 1x:
				 * the one accumulated sample. <1x: average the frames. */
				int16_t lsamp, rsamp;
				if (_fsince == 0u)    { lsamp = (int16_t)liveL; rsamp = (int16_t)liveR; }
				else if (_fsince == 1u) { lsamp = (int16_t)_dec_acc; rsamp = (int16_t)_dec_accR; }
				else if (_fsince < 65536u)
				{ lsamp = (int16_t)((int32_t)_dec_acc /
							  (int32_t)_fsince); /* hw SDIV, bit-identical */
				  rsamp = (int16_t)((int32_t)_dec_accR /
							  (int32_t)_fsince); }
				else { lsamp = (int16_t)(_dec_acc / (int64_t)_fsince);
				       rsamp = (int16_t)(_dec_accR / (int64_t)_fsince); }
				_dec_acc = 0; _dec_accR = 0; _fsince = 0;

#if M82_PROBES
				uint32_t _r82 = DWT->CYCCNT;   /* M82 */
#endif
				int rt_i = g_rec_track;
				/* RP: ONE address computation for the whole iteration.
				 * rt_i is a local and fixed here, so &trk[rt_i] is
				 * invariant -- this replaces three movw #32840 + mul
				 * sequences. Every state READ below stays exactly where
				 * it was: the arm block changes state mid-iteration and
				 * the third read is what starts recording on the SAME
				 * sample the threshold is crossed. NULL when idle; every
				 * use is short-circuit guarded by rt_i >= 0. */
				struct looptrk *rt_p = (rt_i >= 0) ? &trk[rt_i] : (struct looptrk *)0;
				/* M20 PRE-ROLL: nothing capturing and nothing still
				 * flushing -> the ring is free, so keep the last
				 * PREROLL_MAX samples of input in it. */
				if (!g_done_pending &&
				    (rt_i < 0 || rt_p->state != TS_REC)) {
					{ /* CD-463: 24k pre-roll store (same absolute pair grid) */
					  if ((_pre_w & 1u) == 0u) { g_cd_holdL = lsamp; g_cd_holdR = rsamp; }
					  else { uint32_t _fi = ((_pre_w >> 1) & RRING_MASK);
					    g_rring[_fi * 2u]      = (int16_t)(((int32_t)g_cd_holdL + (int32_t)lsamp + 1) >> 1);
					    g_rring[_fi * 2u + 1u] = (int16_t)(((int32_t)g_cd_holdR + (int32_t)rsamp + 1) >> 1); } }
					_pre_w++;
					if (_pre_val < PREROLL_MAX) _pre_val++;
				}
				if (rt_i >= 0 && rt_p->state == TS_ARMED) {
					/* PASSA-726: the cold ARMED path lives in pa_armed() now */
					struct pa_ctx _pc = { _cpos, _pre_w, _pre_val, _pphase, _fsince, _dec_acc, _dec_accR };
					pa_armed(rt_i, lsamp, f, &_pc);
					_cpos = _pc.cpos; _pre_val = _pc.pre_val;
				}
#if M82_PROBES
				g_pa82[0] += DWT->CYCCNT - _r82; _r82 = DWT->CYCCNT;
#endif
				if (rt_i >= 0 && rt_p->state == TS_REC) {
#if M82_PROBES
					{ uint32_t _c86 = DWT->CYCCNT;
					  rec_write_sample(lsamp, rsamp);
					  uint32_t _d86 = DWT->CYCCNT - _c86;
					  if (_d86 < g_t1min) g_t1min = _d86; }
#else
					rec_write_sample(lsamp, rsamp);
#endif
				}

#if M82_PROBES
				g_pa82[1] += DWT->CYCCNT - _r82;
#endif
				_cpos++;
				/* MIDI 24-PPQN clock: a cheap per-sample COUNTER (the divisor
				 * g_midi_div is precomputed when the tempo is detected) -- no
				 * runtime divide here. The beat-phase display moved to once-per-
				 * block (after this loop); it only drives the LED + diag. */
				if (!g_grid_active && g_midi_div && ++g_midi_cnt >= g_midi_div) {
					g_midi_cnt = 0; g_midi_clk_produced++;
				}
			}
		} else if (!g_done_pending) {
			/* M20b-r3 IDLE PRE-ROLL: the transport only runs once a
			 * loop exists, so on an EMPTY song the ring stayed cold
			 * and the very first take had nothing to reach back to.
			 * But the I2S bus is clocked by the 3.072 MHz oscillator
			 * with the nRF a frame/bit SLAVE, so input frames arrive
			 * whether or not the looper is doing anything: decimate
			 * them at the speed a take WOULD use (arming snaps
			 * g_cur_speed to g_play_speed) and keep the same short
			 * memory. No new buffer, no eMMC, and this branch cannot
			 * touch a running transport — it is the else of one. */
			if (g_pre_speed != g_play_speed_q16) {
				/* rate changed: what is stored was sampled at a
				 * different tape speed, so its LENGTH would lie */
				g_pre_speed = g_play_speed_q16;
				_pre_val = 0;
				_dec_acc = 0; _fsince = 0;
			}
			g_pre_phase += g_play_speed_q16 / DECIM;
			while (g_pre_phase >= 65536u) {
				g_pre_phase -= 65536u;
				/* MUST MATCH the decimator above, sample for sample */
				int16_t psamp, psampR;
				if (_fsince == 0u)    { psamp = (int16_t)liveL; psampR = (int16_t)liveR; }
				else if (_fsince == 1u) { psamp = (int16_t)_dec_acc; psampR = (int16_t)_dec_accR; }
				else if (_fsince < 65536u)
				{ psamp = (int16_t)((int32_t)_dec_acc /
							  (int32_t)_fsince);
				  psampR = (int16_t)((int32_t)_dec_accR /
							  (int32_t)_fsince); }
				else { psamp = (int16_t)(_dec_acc / (int64_t)_fsince);
				       psampR = (int16_t)(_dec_accR / (int64_t)_fsince); }
				_dec_acc = 0; _dec_accR = 0; _fsince = 0;
				{ /* CD-463: 24k pre-roll store (pause path, same pair grid) */
				  if ((_pre_w & 1u) == 0u) { g_cd_holdL = psamp; g_cd_holdR = psampR; }
				  else { uint32_t _fi = ((_pre_w >> 1) & RRING_MASK);
				    g_rring[_fi * 2u]      = (int16_t)(((int32_t)g_cd_holdL + (int32_t)psamp + 1) >> 1);
				    g_rring[_fi * 2u + 1u] = (int16_t)(((int32_t)g_cd_holdR + (int32_t)psampR + 1) >> 1); } }
				_pre_w++;
				if (_pre_val < PREROLL_MAX) _pre_val++;
			}
		}
	}

	M81_LAP(1);
	/* M90: publish the block's work. One store per variable per block
	 * instead of one per sample. */
	g_pphase = _pphase; g_dec_acc = _dec_acc;
	g_frames_since = _fsince; g_pre_w = _pre_w;
	g_pre_valid = _pre_val; g_consume_pos = _cpos;

	if (_bnc) bnc_postpass(mix32, mix32R, tmp, got_live);   /* BNC3-606 */
	/* ==== PASS B: accumulate each playing track over the whole block ==== */
	/* CPU-780 M1: the HEALTHY fast path's tracks are deferred and mixed in PAIRS after this loop. */
	const int16_t *hp_pr[NTRK]; int32_t hp_vl[NTRK], hp_vr[NTRK]; int hn = 0;
	for (int i = 0; i < NTRK; i++) {
		if (trk[i].state != TS_PLAY && !head_active(i)) continue;
		/* GAIN SMOOTHING + CLICKLESS MUTE: the fader value used to be
		 * applied as a hard step once per 5 ms block (and mute as an
		 * instant gate) — fast fader rides audibly zipper-clicked and
		 * every mute/unmute popped (community: "fast up-and-down fader
		 * movement sounds a little clicky"). The applied gain now ramps
		 * linearly across the block toward the target (mute = target 0),
		 * spreading any change over 256 samples; a muted track is skipped
		 * entirely once its ramp settles at zero. */
		/* M14: a pending blip mutes this track for ~3 blocks (~16 ms),
		 * riding the existing ramp for clickless edges. */
		const int32_t vtar = (trk[i].muted || g_head_blip[i])
				   ? 0 : (int32_t)vol_s[i];
		if (g_head_blip[i]) g_head_blip[i]--;
		const int32_t vprev = (int32_t)trk[i].vol_now;
		int32_t vd = vtar - vprev;                   /* 0 in the common case */
		/* ADC DEADBAND: the fader ADC jitters +/-1 count between reads, so
		 * without this vd was nonzero on nearly every block for every
		 * track — which silently disqualified the mixer's healthy FAST
		 * PATH (it requires vd==0) and re-cost the CPU that path had
		 * freed. Measured on hardware as renewed starvation under load
		 * (stv 59/67 in one session vs ~0 on the release). A 1-count step
		 * is 0.03 dB — far below audibility and below any zipper — so
		 * snap it instantly; only real movement (>=2 counts) ramps. */
		{	/* BNC2-604: the deadband is ONE FADER COUNT, which is 1 << gsh
			 * after the print gain -- otherwise a print's ADC jitter would
			 * disqualify the fast path every block, the exact regression
			 * the deadband was added for. */
			const int32_t _dz = (int32_t)1 << gs_s[i];
			if (vd >= -_dz && vd <= _dz) vd = 0;
		}
		if (vtar == 0 && vd == 0 && vprev == 0) continue;  /* silent and settled */
		trk[i].vol_now = (uint16_t)vtar;
		const int16_t *const pr = trk[i].pring;
		const int32_t vol = vtar;
		/* PLACE-715: the place as a BALANCE -- above centre the LEFT side comes
		 * down, below centre the RIGHT, 120..136 = centre; (d - 8) * 551 >> 8
		 * reaches 255 (-48 dB) at the ends. The side attenuation slews <= 4/256 a
		 * block (~340 ms full travel), the volume slew above is untouched. */
		int32_t gL, gR;
		{
			const uint32_t _pn = (g_slot < NUM_SLOTS) ? (uint32_t)g_x3.t[g_slot][i].pan : 128u;
			int32_t tL = 0, tR = 0;
			if (_pn > 136u) { tL = (int32_t)(((_pn - 136u) * 551u) >> 8); if (tL > 255) tL = 255; }
			else if (_pn && _pn < 120u) { tR = (int32_t)(((120u - _pn) * 551u) >> 8); if (tR > 255) tR = 255; }
			int32_t aL = trk[i].pan_al, aR = trk[i].pan_ar;
			if (aL != tL) { aL += (tL > aL) ? ((tL - aL > 4) ? 4 : (tL - aL)) : ((aL - tL > 4) ? -4 : (tL - aL)); trk[i].pan_al = (uint8_t)aL; }
			if (aR != tR) { aR += (tR > aR) ? ((tR - aR > 4) ? 4 : (tR - aR)) : ((aR - tR > 4) ? -4 : (tR - aR)); trk[i].pan_ar = (uint8_t)aR; }
			gL = 256 - aL; gR = 256 - aR;
		}
		const int32_t volL = (vol * gL) >> 8, volR = (vol * gR) >> 8;   /* PLACE-715 */
		/* STOPPED fast path: the transport is frozen (no phase steps this
		 * block), so this track contributes ONE constant sample — compute
		 * it once instead of 256 times. Falls back to the exact per-frame
		 * loop whenever a starve or fade boundary is in flight so those
		 * transitions keep their per-frame behavior. */
		/* STOPFIX-579 (W267): the wobble breaks this path's premise. Stopped,
		 * _cpos is frozen but the wobble offset keeps ramping, so posb[] still
		 * varies across the block; holding posb[0] for 256 frames produced a
		 * 187.5 Hz sample-and-hold of the audio at a wandering position --
		 * marc: "rhythmic, bitcrushy, square-ish, moving around the pan".
		 * With the wobble on, take the exact per-frame path instead. */
		if (step == 0u && !trk[i].starved && trk[i].fade >= 256u && vd == 0) {   /* WOBBUS-673: no wobble term */
			int32_t avail = (int32_t)(trk[i].p_w - posb[0]);
			if (avail < 2) {
				trk[i].starved = 1; g_starve_cnt[i]++; STV_BUMP();
				continue;
			}
			/* M63a: one constant FRAME (L,R) instead of one sample */
			uint32_t _b0 = (posb[0] & RING_MASK) * 2u;
			int16_t aL = pr[_b0], aR = pr[_b0 + 1u];
			int16_t svL, svR;
			if (fracb[0] == 0u) {
				svL = aL; svR = aR;
			} else {
				uint32_t _b1 = ((posb[0] + 1) & RING_MASK) * 2u;
				int16_t bL = pr[_b1], bR = pr[_b1 + 1u];
				svL = (int16_t)((int32_t)aL +
					(int32_t)(((bL - aL) * (int32_t)((fracb[0]) >> 1)) >> 15));
				svR = (int16_t)((int32_t)aR +
					(int32_t)(((bR - aR) * (int32_t)((fracb[0]) >> 1)) >> 15));
			}
			if (avail < 256) {
				svL = (int16_t)(((int32_t)svL * avail) >> 8);
				svR = (int16_t)(((int32_t)svR * avail) >> 8);
			}
			int32_t addL = ((int32_t)svL * volL) >> 8;   /* PLACE-715 */
			int32_t addR = ((int32_t)svR * volR) >> 8;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				mix32[f] += addL; mix32R[f] += addR;
			}
			continue;
		}
		/* HEALTHY fast path: when no starve or fade boundary can possibly
		 * occur inside this block — not starved, no fade-in running, and
		 * the frontier is far enough ahead of the block's LAST frame that
		 * even with zero refills every frame has avail >= 258 (above both
		 * the <2 starve gate and the <256 fade-out) — the per-frame
		 * volatile p_w reload and the starve/fade branches are provably
		 * dead. Skip them: output is bit-identical (refills only ever
		 * RAISE avail). This is most of the mixer's remaining cost at
		 * 3-4 healthy tracks; tracks anywhere near their edge take the
		 * exact slow path below. Read demand scales with tape speed
		 * (1.5x = 1125 blk/s for 4 tracks), and the CPU this returns to
		 * the streamer is what lifts the refill ceiling past that. */
		if (vd == 0 && !trk[i].starved && trk[i].fade >= 256u &&
		    (int32_t)(trk[i].p_w - posb[BLK_FRAMES - 1u]) >= 258) {
			/* CPU-780 M1: deferred -- mixed in pairs below (int32 adds into the bus commute exactly) */
			hp_pr[hn] = pr; hp_vl[hn] = volL; hp_vr[hn] = volR; hn++;
			continue;
		}
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			uint32_t cpos = posb[f];
			/* underrun gate WITH HYSTERESIS (semantics unchanged): once a
			 * ring runs dry the track stays silent until half-refilled
			 * (recovering earlier re-dips and chatters — hardware-tested). */
			int32_t avail = (int32_t)(trk[i].p_w - cpos);
			if (trk[i].starved) {
				if (avail >= (int32_t)PLAY_REARM_FRAMES) {
					trk[i].starved = 0;
					trk[i].fade = 0;   /* ramp back in (~5 ms), no click */
					trk[i].held = 0u;   /* HOLDFADE-787 */
				} else {
					continue;
				}
			} else if (avail < 2) {
				if (trk[i].fade == 0u) {   /* STARVEDUCK-735: silent only once the gain reached 0 */
					trk[i].starved = 1; g_starve_cnt[i]++; STV_BUMP();
					continue;
				}
				cpos = trk[i].p_w - 1u; avail = 0;   /* dry with gain up: hold the last frame under the falling gain */
				trk[i].held = 1u; trk[i].hold_pos = cpos;   /* HOLDFADE-787 */
			} else if (trk[i].held) {
				/* HOLDFADE-787: the refill landed before the duck reached 0. Resuming here would
				 * jump the CONTENT (the held frame -> the real position) at the gain the hold left.
				 * Finish the duck on the held frame first (2 a frame to 0, below), then ramp the
				 * real position in from 0 (1 a frame, the re-arm's ramp). A dip, never a step. */
				if (trk[i].fade > 0u) { cpos = trk[i].hold_pos; avail = 0; }
				else { trk[i].held = 0u; g_stv_hr++; }
			}
			uint32_t frac = (avail > 0) ? fracb[f] : 0u;
			uint32_t _b0 = (cpos & RING_MASK) * 2u;               /* M63a frame */
			int16_t aL = pr[_b0], aR = pr[_b0 + 1u];
			int16_t sv, svR2;
			if (frac == 0u) {
				sv = aL; svR2 = aR;    /* unity speed: no interpolation */
			} else {
				uint32_t _b1 = ((cpos + 1) & RING_MASK) * 2u;
				int16_t bL = pr[_b1], bR = pr[_b1 + 1u];
				/* int64 product: (b-a)*frac can exceed INT32_MAX = signed-
				 * overflow UB; the cast keeps it defined (SMULL on M4). */
				sv = (int16_t)((int32_t)aL +
					(int32_t)(((bL - aL) * (int32_t)((frac) >> 1)) >> 15));
				svR2 = (int16_t)((int32_t)aR +
					(int32_t)(((bR - aR) * (int32_t)((frac) >> 1)) >> 15));
			}
			/* BOUNDARY FADE -- STARVEDUCK-735: the applied gain is a STATE (trk.fade,
			 * 0..256) with a bounded slew, never a function of the write pointer:
			 * down <= 2 a frame toward the ring's edge (a smooth drain is followed
			 * exactly), up 1 a frame (a refill mid-fade or a re-arm ramps back over
			 * ~5 ms instead of snapping to full -- the step that popped at the
			 * corner). Dropouts duck instead of clicking, in both directions. */
			{
				int32_t g = (int32_t)trk[i].fade;
				const int32_t tgt = (avail < 256) ? avail : 256;
				if (tgt < g) { g -= 2; if (g < tgt) g = tgt; }
				else if (g < 256) g++;
				trk[i].fade = (uint16_t)g;
				if (g < 256) {
					sv   = (int16_t)(((int32_t)sv * g) >> 8);
					svR2 = (int16_t)(((int32_t)svR2 * g) >> 8);
				}
			}
			int32_t vf = vd ? (vprev + ((vd * (int32_t)(f + 1)) >> 8)) : vol;
			const int32_t vfL = (vf * gL) >> 8, vfR = (vf * gR) >> 8;   /* PLACE-715 */
			mix32[f]  += ((int32_t)sv * vfL) >> 8;
			mix32R[f] += ((int32_t)svR2 * vfR) >> 8;
		}
	}

	/* CPU-780 M1 / M2: the healthy tracks, two at a time -- posb / fracb, both ring indices, the frac
	 * test and the bus load + store shared by the pair (the single loop repeated them per track);
	 * the interpolated sample stays int32 (a + ((b - a) * fr >> 15) lies inside [min(a,b), max(a,b)],
	 * the old (int16_t) cast was a no-op). A lone leftover takes the same shape alone. Host proof:
	 * 20,000 random blocks vs two sequential single passes, 0 differing samples. */
	for (int h = 0; h + 1 < hn; h += 2) {
		const int16_t *const pA = hp_pr[h], *const pB = hp_pr[h + 1];
		const int32_t vAL = hp_vl[h], vAR = hp_vr[h], vBL = hp_vl[h + 1], vBR = hp_vr[h + 1];
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const uint32_t cpos = posb[f];
			const uint32_t frac = fracb[f];
			const uint32_t _b0 = (cpos & RING_MASK) * 2u;
			int32_t aL0 = pA[_b0], aR0 = pA[_b0 + 1u], aL1 = pB[_b0], aR1 = pB[_b0 + 1u];
			if (frac != 0u) {
				const uint32_t _b1 = ((cpos + 1) & RING_MASK) * 2u;
				const int32_t fr = (int32_t)(frac >> 1);
				aL0 += ((pA[_b1] - aL0) * fr) >> 15; aR0 += ((pA[_b1 + 1u] - aR0) * fr) >> 15;
				aL1 += ((pB[_b1] - aL1) * fr) >> 15; aR1 += ((pB[_b1 + 1u] - aR1) * fr) >> 15;
			}
			mix32[f]  += ((aL0 * vAL) >> 8) + ((aL1 * vBL) >> 8);   /* PLACE-715 */
			mix32R[f] += ((aR0 * vAR) >> 8) + ((aR1 * vBR) >> 8);
		}
	}
	if (hn & 1) {
		const int16_t *const pA = hp_pr[hn - 1];
		const int32_t vAL = hp_vl[hn - 1], vAR = hp_vr[hn - 1];
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const uint32_t cpos = posb[f];
			const uint32_t frac = fracb[f];
			const uint32_t _b0 = (cpos & RING_MASK) * 2u;
			int32_t aL0 = pA[_b0], aR0 = pA[_b0 + 1u];
			if (frac != 0u) {
				const uint32_t _b1 = ((cpos + 1) & RING_MASK) * 2u;
				const int32_t fr = (int32_t)(frac >> 1);
				aL0 += ((pA[_b1] - aL0) * fr) >> 15; aR0 += ((pA[_b1 + 1u] - aR0) * fr) >> 15;
			}
			mix32[f]  += (aL0 * vAL) >> 8;
			mix32R[f] += (aR0 * vAR) >> 8;
		}
	}
	M81_LAP(2);
	/* ==== PASS C: master volume + soft limiter -> stereo out ==== */
	{
		/* the VOL buttons step ~3 dB at a time — ramp each step across the
		 * block instead of applying it as a hard gain jump (a click). */
		/* M10: a declick/fade ENVELOPE rides on the master gain. A chop
		 * edit dips it to 0 (the per-block interpolation below turns that
		 * into a ~5 ms ramp) and it recovers over ~27 ms — masking the
		 * window-jump discontinuity that used to click. Power-off fades
		 * it out over ~85 ms and HOLDS, so the codecs are shut down on
		 * silence. Envelope and master fold into ONE interpolated gain:
		 * the per-frame cost is unchanged. */
		static int32_t env_q8 = 256;
		if (g_off_fade) {
			env_q8 -= (env_q8 > 16) ? 16 : env_q8;
		} else if (g_dip_req) {
			g_dip_req = 0;
			env_q8 = 0;
		} else if (env_q8 < 256) {
			env_q8 += 48;
			if (env_q8 > 256) env_q8 = 256;
		}
		int32_t vu_pk = 0;   /* LED-549: this block's output peak */
		const int32_t mv = (int32_t)g_master_vol_q8;
		const int32_t ge = (mv * env_q8) >> 8;
		static int32_t ge_prev;
		const int32_t gd = ge - ge_prev;
		const int32_t g0 = ge_prev;
		ge_prev = ge;
		/* EFXM2-588 (W280): the effect chain lives in its own function so
		 * that nothing about it -- its constants, its loops, its register
		 * pressure -- can change the code GCC emits for PASS A/B. 586 moved
		 * no PASS A/B source and still re-rolled their allocation and their
		 * cache-line placement (W261 class, 2.7x on the hands-off corner).
		 * noinline is the whole point. */
		fx_chain_block(mix32, mix32R);
		/* the lean loop, unconditional (FXFAST-559) */
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				int32_t m = gd ? (g0 + ((gd * (int32_t)(f + 1)) >> 8)) : ge;
				int32_t oL = soft_limit((mix32[f]  * m) >> 8);
				int32_t oR = soft_limit((mix32R[f] * m) >> 8);
				s[2 * f]      = oL;
				s[2 * f + 1u] = oR;
				if ((f & 3u) == 0u) {
					int32_t aL = oL < 0 ? -oL : oL;
					int32_t aR = oR < 0 ? -oR : oR;
					int32_t a  = (aL > aR) ? aL : aR;
					if (a > vu_pk) vu_pk = a;
				}
			}
		{   /* LED-549 r4: a level ANCHORED AT FULL SCALE, so the
		     * bar can actually reach the top. 12 steps of 3 dB =
		     * 36 dB of travel, with 12 at 0 dBFS. r3 anchored at the
		     * floor instead and capped at 24/47 -- the meter was
		     * mathematically unable to leave the bottom half.
		     * BALLISTICS: instant attack (it is a peak), release one
		     * step per 8 blocks (~40 ms/step, ~0.5 s top to bottom) --
		     * slow enough to read as a level, not a twitch. */
			int lv = 0;
			if (vu_pk > 8) {
				int b = 31 - __builtin_clz((uint32_t)vu_pk);
				int hf = (int)(((uint32_t)vu_pk >> (b - 1)) & 1u);
				lv = b * 2 + hf - 13;   /* LED-549 r6: the TOP of the
				     * bar is now -12 dBFS, not 0 -- real loop material
				     * peaked around -18 and only ever reached the bottom
				     * two LEDs. 12 steps x 3 dB now spans -48..-12 dBFS,
				     * so normal playing lives in the upper half and loud
				     * moments peg the top, which is how a meter should
				     * read. */
				if (lv < 0) lv = 0;
				if (lv > 12) lv = 12;
			}
			/* BNC-570 B1: THE TAP. The plan says "post-limiter but
			 * pre-master-volume", which is not a single point on this chain --
			 * the order is master volume -> soft_limit -> store. So take the
			 * true output and divide the master gain back out, which is what
			 * the plan's own next sentence ("normalise by the master gain")
			 * asks for: the print lands at full level however loud you are
			 * monitoring. ONE divide per block, on the peak -- not per frame.
			 * Placed ABOVE the meter's peak-hold, never inside it. */
			{
				int32_t _bg = ge ? ge : 1;
				uint32_t _bp = (uint32_t)((vu_pk * 256) / _bg);
				if (_bp > 65535u) _bp = 65535u;
				g_bt_pk  = _bp;
				g_bt_lat = _bp;   /* PASS A of the NEXT block consumes this */
			}
			static uint8_t vu_hold;
			if ((uint8_t)lv >= g_vu) {
				g_vu = (uint8_t)lv; vu_hold = 0;
			} else if (++vu_hold >= 8u) {
				vu_hold = 0;
				if (g_vu) g_vu--;
			}
		}
	}
	g_sample_clock += BLK_FRAMES;
	/* GRIDCORE-733: the MIDI clock and the bar service moved to grid_follow_tape()
	 * (audio_thread, after the mixer) -- the ticks come from the tape position. */
	/* Beat-phase display computed ONCE per block now (was per loop-sample). It
	 * only feeds the LED + MIDI-grid diag, so block granularity (~5 ms) is plenty
	 * -- this lifts three runtime divides off the per-sample hot path. */
	if (g_loop_active) {
		uint32_t bs = g_beat_samples ? g_beat_samples : BEAT_SAMPLES_L;
		if (g_loop_len > 0u) {
			uint32_t lp = g_consume_pos % g_loop_len;
			g_beat_phase = lp % bs;
			g_dbg_beat = (int)(lp / bs);
		} else {
			g_beat_phase = g_consume_pos % bs;
			g_dbg_beat = (int)(g_consume_pos / bs);
		}
	} else {
		g_beat_phase = (uint32_t)((g_sample_clock % BEAT_SAMPLES_I2S) / DECIM);
	}

	/* diag WATERMARKS (once per block): how close each ring got to its cliff
	 * this window — shows near-misses even when no starve/overrun fired. */
	{
		uint32_t _cp = g_consume_pos;
		for (int i = 0; i < NTRK; i++) {
			if (trk[i].state != TS_PLAY) continue;
			int32_t _av = (int32_t)(trk[i].p_w - _cp);
			if (_av < g_play_lowat) g_play_lowat = _av;
		}
		int _rt = g_rec_track;
		if (_rt >= 0 && trk[_rt].state == TS_REC) {
			uint32_t _fill = trk[_rt].r_w - trk[_rt].r_r;
			if (_fill > g_rec_hiwat) g_rec_hiwat = _fill;
		}
	}
	M81_LAP(3);
}

/* eMMC busy-abort callback: polled ~1 kHz inside the driver's ABORTABLE R1b
 * waits (the idle cache flush), on the streamer thread. true = fire an HPI
 * and bail. Trips the moment a take is armed/recording/finalizing, shutdown
 * work is pending, or any playing ring has drained to half. */
static bool emmc_busy_abort_chk(void)
{
	if (g_stop_req || g_cache_flush_req)
		return true;
	for (int j = 0; j < NTRK; j++) {
		uint8_t sj = trk[j].state;
		if (sj == TS_ARMED || sj == TS_REC || sj == TS_DONE)
			return true;
		if ((sj == TS_PLAY || head_active(j)) &&
		    (int32_t)(trk[j].p_w - g_consume_pos) <
		    (int32_t)(RING_SAMPLES / 2u))
			return true;
	}
	return false;
}

/* ========================================================================
 *  eMMC STREAMER  —  PREEMPT-5 (below audio), the ONLY eMMC user. Each loop:
 *  PASS 1 flushes the record ring to flash (writes-first), PASS 2 reads each
 *  play track ahead into its RAM ring. A balanced ADAPTIVE FLUSH yields the
 *  bus between the two passes — playback wins unless a play ring is about to
 *  underrun, recording wins at true rec-ring overflow. Also loads/saves the
 *  slot metadata (block 0) and runs the power-off cache flush.
 * ======================================================================== */
/* ---- background eMMC streamer (the ONLY eMMC user) -------------------------
 * Preemptible priority BELOW the cooperative audio thread, so the audio thread
 * can always preempt the bit-bang busy-waits and keep the I2S DMA fed. Per
 * PLAY track: read-ahead into the play ring. Per REC/DONE track: flush the rec
 * ring to the card; on DONE, finish the tail then switch the track to PLAY. */
static K_THREAD_STACK_DEFINE(streamer_stack, 2048);  /* RD2-475: was 3072 (474), 4096 originally. 474's run RECORDED, so the write/flush chain was on this stack, and U4S STILL measured a 680 B peak -- identical to the read-only 473 figure. 3.0x margin, 1368 B free. */  /* 4096: the eMMC driver is -O2 here, so its read/send_command/crc chain inlines into a deeper frame on this thread */
static struct k_thread streamer_tcb;
static uint8_t g_streamer_started;   /* v1.2.3: streamer may start EARLY (standby) */
static void streamer_thread(void *a, void *b, void *c);
static void streamer_start(void)
{
	if (g_streamer_started) return;
	g_streamer_started = 1;
	k_thread_create(&streamer_tcb, streamer_stack, K_THREAD_STACK_SIZEOF(streamer_stack),
			streamer_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
}
static volatile uint8_t g_usb_up;    /* usb_audio_start() completed (gates xfer polling) */

#if SP1_XFER_ENABLE
/* ISR: drain the CDC RX FIFO into the ring buffer (host -> device bytes). */
static void cdc_rx_isr(const struct device *dev, void *u)
{
	ARG_UNUSED(u);
	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t b[64];
		int n = uart_fifo_read(dev, b, sizeof b);
		if (n > 0) (void)ring_buf_put(&g_cdc_rx, b, (uint32_t)n);
	}
}

/* Blocking byte send (matches how printk drives the console). */
static void cdc_tx(const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) uart_poll_out(cdc, p[i]);
}

/* Pull exactly n bytes from the RX ring, up to timeout_ms. */
static bool cdc_rx(uint8_t *p, uint32_t n, int timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;
	uint32_t got = 0;
	while (got < n) {
		got += ring_buf_get(&g_cdc_rx, p + got, n - got);
		if (got < n) {
			if (k_uptime_get() > end) return false;
			k_msleep(1);
		}
	}
	return true;
}

/* A block command's sub-read stalled mid-stream, so the RX ring may hold a partial
 * payload that would misframe every later command. Drain it back to a clean command
 * boundary (consumer-side get, safe vs the producing ISR) and send the host an error
 * byte so it aborts that block; the host's next ping then resyncs cleanly. */
static void xfer_resync(uint8_t err_byte)
{
	uint8_t dump;
	while (ring_buf_get(&g_cdc_rx, &dump, 1) == 1) {
	}
	cdc_tx(&err_byte, 1);
}

/* Which track regions the host actually wrote this transfer session. At commit,
 * only these take the host's trk_content (0 = whole track — correct for an
 * upload, which writes full-length audio); every other track keeps the device's
 * value. The website rebuilds block 0 from the legacy layout and writes the
 * appended fields as zeros, and zero is NOT a safe default here: it would
 * unmask never-written flash in the silence tail of fixed-mode takes and drop
 * the saved loop-length mode. */
static uint8_t g_xfer_dirty[NUM_SLOTS][NTRK];

/* Commit host writes durably. The host writes land in the eMMC's volatile write
 * cache (and never touch the in-RAM g_meta), so without this an upload is lost on
 * the next power cut and the stale in-RAM index can overwrite it. This (1) reads
 * block 0 back into g_meta (the write cache is read-coherent), (2) repairs the
 * appended fields the host doesn't manage and writes the repaired index straight
 * back, and (3) flushes the cache — host audio and repaired index become durable
 * TOGETHER. Deferring the repair via g_meta_save_req would be wrong: the streamer
 * only services it after transfer mode ends, so for a whole keepalive-extended
 * session the host's zeroed copy would be the durable one, and a battery death
 * mid-session would persist exactly the corruption this repairs. Runs from the
 * streamer while g_xfer_mode is still set (audio is silenced), so the
 * bus-blocking flush has nothing live to starve. */
#define FLUSH_BATCH 32u   /* 16KB bursts = 2 whole 8KB pages per CMD25 (reverted from 16: the interleave+16 experiment caused catastrophic rec-ring overflow + flash write errors) */
/* R2C-774: THE FLUSH BATCH AND THE DECODE SCRATCH SHARE ONE BUFFER. The streamer packs a
 * take into batch[] and writes it (synchronously: the aw2 chain blocks its caller; the W3-r4
 * pair waits on each burst before the next pass); it reads a play burst into batch[] and
 * decodes it block by block THROUGH the scratch into the ring. The scratch is live only
 * inside a decode (every decoder fills it before reading it), the batch only inside a pack
 * + write -- one thread, never interleaved. The read burst is capped at BATCH_RD_BLKS so
 * the blocks being decoded never underlie the scratch. xfer_commit's borrow (RAMR1A-738)
 * runs while the loop idles in xfer mode. 3,108 B back. */
#define BATCH_RD_BLKS 25u
static union {
	uint8_t batch[FLUSH_BATCH * EMMC_BLOCK_SIZE];
	struct {
		uint8_t rd[FLUSH_BATCH * EMMC_BLOCK_SIZE - 2u * 994u - 4u * 280u];   /* 13,276 B: >= BATCH_RD_BLKS blocks */
		int16_t p14s_scr[994];   /* P16-522: 2*496+2 (P14S uses [0..561]); IL-489 interleaved */
		int16_t a7_scrL[280], a7_scrR[280];   /* A7 SONGS */
	} scr;
} g_bb __aligned(4);
#define batchbuf (g_bb.batch)
#define p14s_scr (g_bb.scr.p14s_scr)
#define a7_scrL  (g_bb.scr.a7_scrL)
#define a7_scrR  (g_bb.scr.a7_scrR)
_Static_assert(sizeof(g_bb.scr.rd) >= BATCH_RD_BLKS * EMMC_BLOCK_SIZE, "R2C-774: the read burst reaches the scratch");
_Static_assert(sizeof(g_bb.scr) == sizeof(g_bb.batch), "R2C-774: the overlay is not exact");
_Static_assert((sizeof(g_bb.scr.rd) % 4u) == 0u, "R2C-774: p14s_scr must stay 4-aligned (P16-522)");
static void xfer_commit(void)
{
	uint8_t *const mblk = (uint8_t *)p14s_scr;   /* RAMR1A-738: the streamer's decode scratch (1,988 B >= 3 blocks), idle while g_xfer_mode holds the loop -- was a private 1,536 B static */
	if (g_emmc_ready && emmc_read_blocks(META_BLOCK, mblk, META_BLOCKS)) {
		struct meta_blk *m = (struct meta_blk *)mblk;
		if (m->magic == META_MAGIC && m->cur_slot < NUM_SLOTS) {
			uint32_t keep[NUM_SLOTS][NTRK];
			uint8_t keep_chop[NUM_SLOTS][2];
			uint8_t keep_mode[NUM_SLOTS];
			memcpy(keep, g_meta.trk_content, sizeof(keep));
			memcpy(keep_chop, g_meta.chop, sizeof(keep_chop));
			memcpy(keep_mode, g_meta.song_mode, sizeof(keep_mode));
			memcpy(&g_meta, m, sizeof(g_meta));
			g_slot = g_meta.cur_slot;
			/* the host only manages the legacy fields (see g_xfer_dirty):
			 * restore the mode setting and every untouched track's content
			 * length, then write the repaired index back (skipped when the
			 * host's copy already matches, e.g. a read-only session). */
			g_meta.fixed_len = g_mode_pref;      /* M7c: field = preference */
			{ g_led_dim = (g_meta.led_full & 1u) ? 0u : 1u; led_hw_refresh(); }   /* M8c: site owns it */
			g_instant_rec = (uint8_t)(((g_meta.led_full >> 1) & 1u) ? 0u : 1u);   /* M41-r5: bit 1 SET = classic */
			memcpy(g_meta.chop, keep_chop, sizeof(keep_chop));
			memcpy(g_meta.song_mode, keep_mode, sizeof(keep_mode));
			if (g_slot < NUM_SLOTS) {   /* reload effective for current song */
				uint32_t cd, co;
				chop_meta_decode(g_meta.chop[g_slot], &cd, &co);   /* CHOPCAP-690 */
				g_chop_div = cd; g_chop_off = co;
				g_fixed_len = (g_meta.song_mode[g_slot] & 0x0Fu)
					    ? ((g_meta.song_mode[g_slot] & 0x0Fu) == 2u ? 1u : 0u)
					    : g_mode_pref;
			}
			for (int s = 0; s < NUM_SLOTS; s++)
				for (int t = 0; t < NTRK; t++)
					if (!g_xfer_dirty[s][t])
						g_meta.trk_content[s][t] = keep[s][t];
					else   /* M7-r4: freshly uploaded audio is audible */
						g_meta.song_mode[s] &= (uint8_t)~(uint8_t)(0x10u << t);
			if (memcmp(mblk, &g_meta, sizeof(g_meta)) != 0) {
				memset(mblk, 0, X3_NBLK * EMMC_BLOCK_SIZE);
				memcpy(mblk, &g_meta, sizeof(g_meta));
				(void)meta_write_blocks(mblk);
			}
		}
	}
	/* X3RELOAD-677: the host may have rewritten blocks 3-5 (a 3.0 upload / delete). Pull them
	 * now, exactly as boot does, so the slot reload and the next save see the CARD's table and
	 * not the stale RAM copy. A table that does not validate is ignored (RAM stays). */
	if (g_emmc_ready && emmc_read_blocks(X3_BLK, mblk, X3_NBLK)) {
		const struct x3_tab *xt = (const struct x3_tab *)mblk;
		if (x3_valid(xt)) {
			memcpy(&g_x3, xt, sizeof(g_x3));
			g_x3_ok = 1u;
			/* the mask re-derivation, a private copy of p14s_mask_from_x3 (that one stays
			 * single-caller so the streamer's boot inline -- and its text -- do not move) */
			if (g_slot < NUM_SLOTS) {
				g_p14s_mask = 0;
				for (int xi = 0; xi < NTRK; xi++) {
					const struct x3_trk *xe = &g_x3.t[g_slot][xi];
					if (xe->codec_id == X3_CODEC_P14S || xe->codec_id == X3_CODEC_P16M) {
						g_p14s_mask |= (uint8_t)(1u << xi);
						trk[xi].p16m = (xe->codec_id == X3_CODEC_P16M) ? 1u : 0u;
						trk[xi].gsh  = (uint8_t)((xe->flags >> 1) & 3u);
						trk[xi].p16m_next = (xe->rsv & 0x80u) ? (uint8_t)(xe->rsv & 1u) : trk[xi].p16m;
					}
				}
			}
		}
	}
	if (g_cache_on) {
		(void)emmc_cache_flush();
	}
}

/* The block-transfer protocol, serviced from the streamer (the only eMMC user).
 * OUT of transfer mode: scan the RX stream for the 8-byte enter-magic.
 * IN transfer mode: run ONE command per call ('P'ing/'R'ead/'W'rite/'F'lush/e'X'it),
 * auto-committing + auto-exiting after 15 s with no command so a dropped page can't
 * wedge it or strand an upload in volatile cache. */
static void xfer_service(void)
{
	static const uint8_t MAGIC[8] = { 'S','P','1','X','F','E','R','!' };
	static uint8_t  m;
	static int64_t  last;

	if (!g_xfer_mode) {
		uint8_t b;
		while (ring_buf_get(&g_cdc_rx, &b, 1) == 1) {
			m = (b == MAGIC[m]) ? (uint8_t)(m + 1) : (b == MAGIC[0] ? 1u : 0u);
			if (m == 8u) {
				m = 0;
				/* Don't freeze the streamer mid-take: if a recording is still
				 * being captured or flushed, finalize it first (the audio thread
				 * promotes it + the streamer drains the ring and persists the
				 * index). Enter on a later magic -- the host's handshake retries,
				 * and a take finalizes in well under that window. */
				bool busy = (g_rec_track >= 0) || g_meta_save_req;
				for (int t = 0; t < NTRK; t++)
					if (trk[t].state == TS_REC || trk[t].state == TS_DONE) busy = 1;
				if (busy) {
					g_stop_req = 1;
					break;
				}
				memset(g_xfer_dirty, 0, sizeof(g_xfer_dirty));
				g_xfer_mode = 1;
				g_playing = 0;           /* pause the transport during transfer */
				last = k_uptime_get();
				break;
			}
		}
		return;
	}

	uint8_t cmd;
	if (ring_buf_get(&g_cdc_rx, &cmd, 1) != 1) {            /* idle: commit + exit on timeout */
		if (k_uptime_get() - last > 15000) {
			xfer_commit();                         /* don't strand an upload in cache */
			g_slot_switch_req = 1;                 /* reload tracks for the active song */
			g_br_shift = 0;                        /* LOOPHOLD-804: a transfer reloads the takes under the hold */
			g_xfer_mode = 0;
		}
		return;
	}
	last = k_uptime_get();

	if (cmd == 'P') {                                      /* ping -> magic + layout */
		uint8_t r[4 + 6 * 4];
		memcpy(r, "SP1!", 4);
		uint32_t info[6] = { EMMC_BLOCK_SIZE, NUM_SLOTS, NTRK,
				     SLOT0_BLOCK, TRACK_BLOCKS, META_MAGIC };
		memcpy(r + 4, info, sizeof info);
		cdc_tx(r, sizeof r);
	} else if (cmd == 'R' || cmd == 'W') {                 /* read / write one block */
		uint8_t a[4];
		if (!cdc_rx(a, 4, 1000)) { xfer_resync(cmd == 'R' ? 'e' : 'E'); return; }
		uint32_t blk = (uint32_t)a[0] | ((uint32_t)a[1] << 8) |
			       ((uint32_t)a[2] << 16) | ((uint32_t)a[3] << 24);
		uint32_t total = SLOT0_BLOCK + (uint32_t)NUM_SLOTS * NTRK * TRACK_BLOCKS;
		static uint8_t sec[EMMC_BLOCK_SIZE];
		if (cmd == 'R') {
			bool ok = (blk < total) && emmc_read_blocks(blk, sec, 1);
			uint8_t h = ok ? 'r' : 'e';
			cdc_tx(&h, 1);
			if (ok) cdc_tx(sec, EMMC_BLOCK_SIZE);
		} else {
			if (!cdc_rx(sec, EMMC_BLOCK_SIZE, 4000)) { xfer_resync('E'); return; }
			bool ok = (blk < total) && emmc_write_blocks(blk, sec, 1);
			if (ok && blk >= SLOT0_BLOCK) {
				uint32_t ti = (blk - SLOT0_BLOCK) / TRACK_BLOCKS;
				if (ti < (uint32_t)NUM_SLOTS * NTRK)
					g_xfer_dirty[ti / NTRK][ti % NTRK] = 1;
			}
			uint8_t h = ok ? 'w' : 'E';
			cdc_tx(&h, 1);
		}
	} else if (cmd == 'F') {                               /* flush: commit writes to NAND */
		xfer_commit();
		uint8_t h = 'f';
		cdc_tx(&h, 1);
	} else if (cmd == 'X') {                               /* commit, then exit transfer mode */
		xfer_commit();
		g_slot_switch_req = 1;                         /* reload tracks for the active song */
		g_br_shift = 0;                                /* LOOPHOLD-804: a transfer reloads the takes under the hold */
		g_xfer_mode = 0;
		uint8_t h = 'x';
		cdc_tx(&h, 1);
	}
}
#endif /* SP1_XFER_ENABLE */

/* =====================================================================
 * STORAGE CODEC pack/unpack
 * Place this entire block in main.c just BEFORE streamer_thread()
 * (above `static void streamer_thread(void *a,...)` at main.c:1523).
 * SP1_CODEC, SAMP_PER_BLK, EMMC_BLOCK_SIZE are all in scope there.
 *
 *  codec_pack  : int16 ring -> packed flash bytes (ENCODE), nblk*512 bytes out
 *  codec_unpack: packed flash bytes -> int16 ring (DECODE), nblk*512 bytes in
 *
 * Args:
 *   ring       : the int16 ring base (g_rring for write, trk[].pring for read)
 *   ring_mask  : RRING_MASK (write) or RING_MASK (read) — power of two, sample-domain
 *   start      : ring sample offset of the FIRST sample (already & ring_mask'd by caller)
 *   flash      : the linear 512*nblk-byte batch buffer (batchbuf)
 *   nblk       : number of 512-byte flash blocks
 * Each block holds exactly SAMP_PER_BLK int16 samples. The caller guarantees
 * `start` is block-aligned in the ring, so each block's run wraps the ring at
 * most once (same invariant the original memcpy pairs used).
 * ===================================================================== */

#if SP1_CODEC == SP1_CODEC_PCM
/* ---- M63a PCM: STEREO ring <-> MONO blocks. The on-flash format is
 * byte-identical to v2.7.2 (256 int16 samples per block): pack stores
 * the LEFT channel of each frame; unpack writes the sample to BOTH
 * channels. Index math replaces the old memcpy pair; SAMP_PER_BLK
 * counts FRAMES (== stored samples, mono blocks). ---- */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk)
{
	int16_t *out = (int16_t *)flash;
	uint32_t ntot = nblk * SAMP_PER_BLK;
	for (uint32_t i = 0; i < ntot; i++)
		out[i] = ring[((start + i) & ring_mask) * 2u];
}
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk)
{
	const int16_t *in = (const int16_t *)flash;
	uint32_t ntot = nblk * SAMP_PER_BLK;
	for (uint32_t i = 0; i < ntot; i++) {
		uint32_t fi = ((start + i) & ring_mask) * 2u;
		ring[fi] = in[i]; ring[fi + 1u] = in[i];
	}
}
/* M22-B: adopt an arbitrary FRAME run out of a whole-block read (the
 * sample-exact loop seam). Semantics identical to v2.7.2. */
static void codec_unpack_part(int16_t *ring, uint32_t ring_mask, uint32_t start,
                              const uint8_t *flash, uint32_t skip, uint32_t nsamp)
{
	const int16_t *in = (const int16_t *)flash + skip;
	for (uint32_t i = 0; i < nsamp; i++) {
		uint32_t fi = ((start + i) & ring_mask) * 2u;
		ring[fi] = in[i]; ring[fi + 1u] = in[i];
	}
}
#elif SP1_CODEC == SP1_CODEC_A7
/* ==== M63b: SP1-ADPCM7 — the FROZEN 3.0 codec. Core verified
 * bit-exact against SP1-ADPCM7-CODEC-REFERENCE.py by M70 on hardware.
 * 7-bit, damped 2nd-order predictor, full quantiser-error noise
 * shaping clamped at 4*step. 280 STEREO FRAMES per 512-byte block:
 *   [0:2] Lp1 [2:4] Lp2 [4] Lidx [5] flags(b0=stereo)
 *   [6:8] Rp1 [8:10] Rp2 [10] Ridx [11] rsv
 *   [12:14] frames u16  [14:16] sum16(payload)  [16:506] payload
 * Headers reseed the decoder, so every block decodes independently —
 * that is what keeps loop wrap, windows, chop and reverse free. ==== */
static const int16_t a7_step[89] = {
	7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
	73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
	408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,
	1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
	7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
	22385,24623,27086,29794,32767 };
static const int8_t a7_idx7[64] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,2,2,2,2,3,3,3,4,4,4,5,5,
	6,6,7,7,8,8,9,10,10,11,12,13,14,15,16,16 };
struct a7_enc { int32_t p1, p2; uint8_t idx; int32_t e; };
struct a7_dec { int32_t p1, p2; uint8_t idx; };
static inline int32_t a7_clip16(int32_t v)
{ return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline int32_t a7_pred(int32_t p1, int32_t p2)
{ return a7_clip16(p1 + ((p1 - p2) >> 1)); }
__attribute__((optimize("O2")))
static inline __attribute__((always_inline)) uint8_t a7_encode(struct a7_enc *st, int16_t s)
{
	int32_t pr = a7_pred(st->p1, st->p2);
	int32_t step = a7_step[st->idx];
	int32_t d = (int32_t)s - pr + st->e;      /* NS_NUM 8/8 => e as-is */
	uint8_t sign = 0; int32_t ad, q, ne, lim, ni; uint32_t mag;
	if (d < 0) { sign = 64u; ad = -d; } else ad = d;
	mag = (uint32_t)((ad * 32) / step);
	if (mag > 63u) mag = 63u;
	q = (int32_t)((mag * (uint32_t)step) / 32u) + step / 64;
	if (sign) q = -q;
	st->p2 = st->p1; st->p1 = a7_clip16(pr + q);
	ne = d - q; lim = 4 * step;
	st->e = ne > lim ? lim : (ne < -lim ? -lim : ne);
	ni = (int32_t)st->idx + a7_idx7[mag];
	st->idx = (uint8_t)(ni < 0 ? 0 : (ni > 88 ? 88 : ni));
	return (uint8_t)(sign | (uint8_t)mag);
}
__attribute__((optimize("O2")))
/* M74 bitstream helpers: 32-bit, LSB-first -- exactly the order the
 * format already uses (spec 4.1b: "V = c0 | c1<<7 | ... little-endian").
 * These replace uint64_t shifts, which the Cortex-M4 has no barrel
 * shifter for. */
#define A7_FILL(acc, nb, bi, p) do { \
	while ((nb) <= 24u && (bi) < 7u) { \
		(acc) |= (uint32_t)(p)[(bi)++] << (nb); (nb) += 8u; } \
	} while (0)
#define A7_TAKE(c, acc, nb) do { \
	(c) = (uint8_t)((acc) & 0x7Fu); (acc) >>= 7; (nb) -= 7u; \
	} while (0)
static inline __attribute__((always_inline)) int16_t a7_decode(struct a7_dec *st, uint8_t c)
{
	int32_t pr = a7_pred(st->p1, st->p2);
	int32_t step = a7_step[st->idx];
	uint32_t mag = c & 63u;
	int32_t q = (int32_t)((mag * (uint32_t)step) / 32u) + step / 64;
	int32_t rec = a7_clip16((c & 64u) ? (pr - q) : (pr + q));
	int32_t ni = (int32_t)st->idx + a7_idx7[mag];
	st->idx = (uint8_t)(ni < 0 ? 0 : (ni > 88 ? 88 : ni));
	st->p2 = st->p1; st->p1 = rec;
	return (int16_t)rec;
}
/* R2C-774: a7_scrL/R live in g_bb (defined with xfer_commit above) */
/* IL-489: PCM14S decodes into ONE INTERLEAVED buffer so the
 * scratch->ring copy becomes two straight 32-bit memcpy spans
 * instead of a 16-bit-at-a-time interleave. Frame f is at
 * [f*2] = L, [f*2+1] = R. 562 not 560: see the upsample note. */
/* R2C-774: p14s_scr lives in g_bb (defined with xfer_commit above) */
static uint8_t a7_codes[560];
static void a7_emit_block_i(const int16_t *ring, uint32_t ring_mask,
			  uint32_t start, struct a7_enc *eL,
			  struct a7_enc *eR, uint8_t *blk);
static void a7_emit_block(const int16_t *ring, uint32_t ring_mask,
			  uint32_t start, struct a7_enc *eL,
			  struct a7_enc *eR, uint8_t *blk)
{
	M73_T0();
	a7_emit_block_i(ring, ring_mask, start, eL, eR, blk);
	{ uint32_t _d88 = DWT->CYCCNT - _t73;
	  if (_d88 < g_enmin) g_enmin = _d88; }
	M73_ADD(g_t_en);
}
__attribute__((optimize("O2")))
static void a7_emit_block_i(const int16_t *ring, uint32_t ring_mask,
			  uint32_t start, struct a7_enc *eL,
			  struct a7_enc *eR, uint8_t *blk)
{
	uint32_t g, k, i2; uint16_t s16 = 0;
	blk[0] = (uint8_t)(eL->p1 & 0xFF); blk[1] = (uint8_t)((eL->p1 >> 8) & 0xFF);
	blk[2] = (uint8_t)(eL->p2 & 0xFF); blk[3] = (uint8_t)((eL->p2 >> 8) & 0xFF);
	blk[4] = eL->idx;
	blk[5] = g_cap_stereo ? 1u : 0u;  /* M78: mono takes -> mono blocks */
	blk[6] = (uint8_t)(eR->p1 & 0xFF); blk[7] = (uint8_t)((eR->p1 >> 8) & 0xFF);
	blk[8] = (uint8_t)(eR->p2 & 0xFF); blk[9] = (uint8_t)((eR->p2 >> 8) & 0xFF);
	blk[10] = eR->idx; blk[11] = 0;
	blk[12] = (uint8_t)(SAMP_PER_BLK & 0xFF); blk[13] = (uint8_t)(SAMP_PER_BLK >> 8);
	if (blk[5] & 1u) {
		for (i2 = 0; i2 < SAMP_PER_BLK; i2++) {
			/* M91: the rec ring is MONO; this encoder serves ONLY the
			 * rec ring. Stereo blocks (impossible until M63b-2) would
			 * encode L for both channels. */
			uint32_t fi = (start + i2) & ring_mask;
			a7_codes[i2 * 2u]      = a7_encode(eL, ring[fi * 2u]);
			a7_codes[i2 * 2u + 1u] = a7_encode(eR, ring[fi * 2u + 1u]);
		}
	} else {
		/* M78 MONO: one code per frame from L (capture writes the
		 * downmix to both channels, so L is the take). R seeds are
		 * zeroed; the mono decoder never reads them. */
		for (i2 = 0; i2 < SAMP_PER_BLK; i2++) {
			uint32_t fi = (start + i2) & ring_mask;   /* M91 mono ring */
			a7_codes[i2] = a7_encode(eL, ring[fi * 2u]);  /* S2CAP: L of frame */
		}
		blk[6] = 0; blk[7] = 0; blk[8] = 0; blk[9] = 0; blk[10] = 0;
	}
	{
	uint32_t _ng78 = (blk[5] & 1u) ? 70u : 35u;
	for (g = 0; g < _ng78; g++) {
		/* M74: 32-bit LSB-first pack (was a uint64_t V with a
		 * variable-distance shift per code). Identical order, so
		 * byte-for-byte identical output -- stage EQ proves it. */
		uint32_t acc74 = 0, nb74 = 0, bo74 = 0;
		for (k = 0; k < 8u; k++) {
			acc74 |= (uint32_t)(a7_codes[g * 8u + k] & 0x7Fu) << nb74;
			nb74 += 7u;
			while (nb74 >= 8u) {
				blk[16u + g * 7u + bo74++] = (uint8_t)acc74;
				acc74 >>= 8; nb74 -= 8u;
			}
		}
	}
	}
	if (!(blk[5] & 1u))
		for (g = 16u + 245u; g < 506u; g++) blk[g] = 0;
	for (g = 0; g < 490u; g++) s16 = (uint16_t)(s16 + blk[16u + g]);
	blk[14] = (uint8_t)(s16 & 0xFF); blk[15] = (uint8_t)(s16 >> 8);
	for (g = 506u; g < 512u; g++) blk[g] = 0;
}
static void a7_decode_block_i(const uint8_t *blk, int16_t *dstL, int16_t *dstR);
static void a7_decode_block(const uint8_t *blk, int16_t *dstL, int16_t *dstR)
{
	M73_T0();
	a7_decode_block_i(blk, dstL, dstR);
	M73_ADD(g_t_dc);
	g_dcc++;
}

/* M79: decode STRAIGHT INTO THE RING -- deletes the a7_scr scratch
 * store + reload + masked copy (~6-8 ops/frame). Plain fill path
 * only (100% of steady traffic, M76: dcu==dcc); the seam and reverse
 * paths keep the scratch route. The running pointer handles the wrap
 * with one compare per frame: RING_SAMPLES is NOT a multiple of
 * SAMP_PER_BLK (8192 % 280 != 0), so a block CAN wrap mid-decode.
 * Bit-exact: same sample codec, same code order, only the
 * destination changes. Carries its own M73 timing + g_dcc. */
__attribute__((optimize("O2"), noinline))
static void a7_decode_block_ring(const uint8_t *blk, int16_t *ring,
				 uint32_t ring_mask, uint32_t base)
{
	M73_T0();
	struct a7_dec dL, dR;
	uint32_t g, k, i2 = 0;
	int stereo = blk[5] & 1;
	int16_t *p = ring + (base & ring_mask) * 2u;
	int16_t * const rend = ring + (ring_mask + 1u) * 2u;
	dL.p1 = (int16_t)((uint16_t)blk[0] | ((uint16_t)blk[1] << 8));
	dL.p2 = (int16_t)((uint16_t)blk[2] | ((uint16_t)blk[3] << 8));
	dL.idx = blk[4] > 88u ? 88u : blk[4];
	if (!stereo) {
		for (g = 0; g < 35u && i2 < SAMP_PER_BLK; g++) {
			const uint8_t *p79 = blk + 16u + g * 7u;
			uint32_t acc = 0, nb = 0, bi = 0;
			A7_FILL(acc, nb, bi, p79);
			for (k = 0; k < 8u && i2 < SAMP_PER_BLK; k++, i2++) {
				uint8_t c79;
				A7_TAKE(c79, acc, nb); A7_FILL(acc, nb, bi, p79);
				int16_t s = a7_decode(&dL, c79);
				p[0] = s; p[1] = s; p += 2;
				if (p == rend) p = ring;
			}
		}
		M73_ADD(g_t_dc);
		g_dcc++;
		return;
	}
	dR.p1 = (int16_t)((uint16_t)blk[6] | ((uint16_t)blk[7] << 8));
	dR.p2 = (int16_t)((uint16_t)blk[8] | ((uint16_t)blk[9] << 8));
	dR.idx = blk[10] > 88u ? 88u : blk[10];
	for (g = 0; g < 70u && i2 < SAMP_PER_BLK; g++) {
		const uint8_t *p79 = blk + 16u + g * 7u;
		uint32_t acc = 0, nb = 0, bi = 0;
		A7_FILL(acc, nb, bi, p79);
		for (k = 0; k < 8u && i2 < SAMP_PER_BLK; k += 2u, i2++) {
			uint8_t cl79, cr79;
			A7_TAKE(cl79, acc, nb); A7_FILL(acc, nb, bi, p79);
			A7_TAKE(cr79, acc, nb); A7_FILL(acc, nb, bi, p79);
			int16_t sl = a7_decode(&dL, cl79);
			int16_t sr = a7_decode(&dR, cr79);
			p[0] = sl; p[1] = sr; p += 2;
			if (p == rend) p = ring;
		}
	}
	M73_ADD(g_t_dc);
	g_dcc++;
}
__attribute__((optimize("O2")))
static void a7_decode_block_i(const uint8_t *blk, int16_t *dstL, int16_t *dstR)
{
	struct a7_dec dL, dR;
	uint32_t g, k, i2 = 0;
	int stereo = blk[5] & 1;
	dL.p1 = (int16_t)((uint16_t)blk[0] | ((uint16_t)blk[1] << 8));
	dL.p2 = (int16_t)((uint16_t)blk[2] | ((uint16_t)blk[3] << 8));
	dL.idx = blk[4] > 88u ? 88u : blk[4];
	dR.p1 = (int16_t)((uint16_t)blk[6] | ((uint16_t)blk[7] << 8));
	dR.p2 = (int16_t)((uint16_t)blk[8] | ((uint16_t)blk[9] << 8));
	dR.idx = blk[10] > 88u ? 88u : blk[10];
	if (!stereo) {
		/* M78 MONO: 280 codes, 35 groups, one code per frame; the
		 * sample lands on BOTH ring channels. dR is never used. */
		for (g = 0; g < 35u && i2 < SAMP_PER_BLK; g++) {
			const uint8_t *p78 = blk + 16u + g * 7u;
			uint32_t acc = 0, nb = 0, bi = 0;
			A7_FILL(acc, nb, bi, p78);
			for (k = 0; k < 8u && i2 < SAMP_PER_BLK; k++, i2++) {
				uint8_t c78;
				A7_TAKE(c78, acc, nb); A7_FILL(acc, nb, bi, p78);
				int16_t s = a7_decode(&dL, c78);
				dstL[i2] = s; dstR[i2] = s;
			}
		}
		return;
	}
	for (g = 0; g < 70u && i2 < SAMP_PER_BLK; g++) {
		/* M74: 32-bit LSB-first bitstream. Was a uint64_t V with a
		 * variable-distance >> per code -- the M4 has no 64-bit
		 * barrel shifter, so each cost ~10-20 cycles. Same LSB-first
		 * order, so codes are BIT-IDENTICAL (stage EQ proves it on
		 * the host; M70 conformance proves it again on hardware).
		 * Refill keeps nb <= 24 before an OR, so every shift is
		 * in-range for a uint32_t. */
		const uint8_t *p74 = blk + 16u + g * 7u;
		uint32_t acc = 0, nb = 0, bi = 0;
		A7_FILL(acc, nb, bi, p74);
		for (k = 0; k < 8u && i2 < SAMP_PER_BLK; k += 2u, i2++) {
			uint8_t cl74, cr74;
			A7_TAKE(cl74, acc, nb); A7_FILL(acc, nb, bi, p74);
			A7_TAKE(cr74, acc, nb); A7_FILL(acc, nb, bi, p74);
			int16_t sl = a7_decode(&dL, cl74);
			int16_t sr = a7_decode(&dR, cr74);
			if (!stereo) sr = sl;
			dstL[i2] = sl; dstR[i2] = sr;
		}
	}
}
/* PACK: encoder state chains across SEQUENTIAL calls (a flush burst);
 * a non-sequential start resets it. Headers carry the pre-block state,
 * so decode never depends on that continuity. */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk)
{
	static struct a7_enc peL, peR;
	static uint32_t p_expect = 0xFFFFFFFFu;
	if (start != p_expect) {
		peL.p1 = 0; peL.p2 = 0; peL.idx = 0; peL.e = 0;
		peR = peL;
	}
	for (uint32_t b = 0; b < nblk; b++)
		a7_emit_block(ring, ring_mask,
			      (start + b * SAMP_PER_BLK) & ring_mask,
			      &peL, &peR, flash + b * EMMC_BLOCK_SIZE);
	p_expect = (start + nblk * SAMP_PER_BLK) & ring_mask;
}
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk)
{
	/* M79: straight to the ring; scratch route retired from this
	 * path (seam + reverse keep it). */
	for (uint32_t b = 0; b < nblk; b++) {
		a7_decode_block_ring(flash + b * EMMC_BLOCK_SIZE, ring,
				     ring_mask, start + b * SAMP_PER_BLK);
		g_dcu++;
	}
}
/* M22-B seam, with the M62-r2 MULTI-BLOCK walk: the caller's run can
 * span blocks (_ds = n*SAMP_PER_BLK - off). The r1 single-block version
 * of this is what caused the stutter; do not simplify it back. */
static void codec_unpack_part(int16_t *ring, uint32_t ring_mask, uint32_t start,
                              const uint8_t *flash, uint32_t skip, uint32_t nsamp)
{
	uint32_t pos = start, done = 0;
	uint32_t b = skip / SAMP_PER_BLK, off = skip % SAMP_PER_BLK;
	while (done < nsamp) {
		uint32_t take = SAMP_PER_BLK - off;
		if (take > nsamp - done) take = nsamp - done;
		a7_decode_block(flash + b * EMMC_BLOCK_SIZE, a7_scrL, a7_scrR);
		g_dcp++;
		for (uint32_t i2 = 0; i2 < take; i2++) {
			uint32_t fi = (pos & ring_mask) * 2u;
			ring[fi] = a7_scrL[off + i2];
			ring[fi + 1u] = a7_scrR[off + i2];
			pos++;
		}
		done += take; off = 0; b++;
	}
}
/* Reversed heads: coded blocks cannot be byte-flipped, so decode in
 * reverse block order and write each block's frames time-reversed.
 * Same audible result the M15-r2 batch flip had on PCM. */
static void codec_unpack_rev(int16_t *ring, uint32_t ring_mask, uint32_t start,
                             const uint8_t *flash, uint32_t nblk)
{
	uint32_t pos = start;
	for (uint32_t b = nblk; b-- > 0u; ) {
		a7_decode_block(flash + b * EMMC_BLOCK_SIZE, a7_scrL, a7_scrR);
		g_dcr++;
		for (uint32_t i2 = SAMP_PER_BLK; i2-- > 0u; ) {
			uint32_t fi = (pos & ring_mask) * 2u;
			ring[fi] = a7_scrL[i2]; ring[fi + 1u] = a7_scrR[i2];
			pos++;
		}
	}
}

#elif SP1_CODEC == SP1_CODEC_ULAW
/* ---- G.711 u-law, 8-bit, 2:1 --------------------------------------------- */
#define ULAW_BIAS 0x84
#define ULAW_CLIP 32635
static inline uint8_t ulaw_encode(int16_t pcm)
{
	int sign = (pcm >> 8) & 0x80;
	int s = pcm;
	if (sign) s = -s;
	if (s > ULAW_CLIP) s = ULAW_CLIP;
	s += ULAW_BIAS;
	int exp = 7;
	for (int em = 0x4000; (s & em) == 0 && exp > 0; exp--, em >>= 1) { }
	int mant = (s >> (exp + 3)) & 0x0F;
	return (uint8_t)(~(sign | (exp << 4) | mant));
}
static inline int16_t ulaw_decode(uint8_t u)
{
	u = ~u;
	int sign = u & 0x80;
	int exp  = (u >> 4) & 0x07;
	int mant = u & 0x0F;
	int s = ((mant << 3) + ULAW_BIAS) << exp;
	s -= ULAW_BIAS;
	return (int16_t)(sign ? -s : s);
}
/* one u-law byte per sample; SAMP_PER_BLK == 512 == EMMC_BLOCK_SIZE */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk)
{
	uint32_t ntot = nblk * SAMP_PER_BLK;
	uint32_t pos = start;
	for (uint32_t i = 0; i < ntot; i++) {
		flash[i] = ulaw_encode(ring[pos]);
		pos = (pos + 1u) & ring_mask;
	}
}
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk)
{
	uint32_t ntot = nblk * SAMP_PER_BLK;
	uint32_t pos = start;
	for (uint32_t i = 0; i < ntot; i++) {
		ring[pos] = ulaw_decode(flash[i]);
		pos = (pos + 1u) & ring_mask;
	}
}

#else /* SP1_CODEC == SP1_CODEC_ADPCM */
/* ---- IMA ADPCM, 4-bit, ~4:1, SELF-CONTAINED 512-byte blocks --------------
 * Each 512-byte flash block decodes STANDALONE (predictor + step index RESET at
 * the block start) so random-access loop seeks land on any block.
 *   byte 0..1 : int16 predictor seed (little-endian) = block's first sample
 *   byte 2    : uint8 step index seed (0..88)
 *   byte 3    : pad (0)
 *   byte 4..511 : 508 data bytes * 2 nibbles = 1016 samples (SAMP_PER_BLK).
 * Within a block, sample[k] is encoded as nibble[k] against the running
 * predictor seeded from the header (so nibble[0] re-encodes sample[0] against
 * predictor==sample[0]; round-trips to ~sample[0]). 508 bytes = exactly 1016
 * nibbles = SAMP_PER_BLK. Low nibble of each byte first, then high nibble. */
static const int8_t  ima_index_tab[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};
static const int16_t ima_step_tab[89] = {
	    7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
	   19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
	   50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
	  130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
	  337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
	  876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
	 2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
	 5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
#define ADPCM_HDR_BYTES   4u
#define ADPCM_DATA_BYTES  (EMMC_BLOCK_SIZE - ADPCM_HDR_BYTES)   /* 508 -> 1016 samples */

static inline uint8_t ima_enc_step(int16_t sample, int32_t *pred, int *idx)
{
	int step = ima_step_tab[*idx];
	int diff = sample - *pred;
	int code = 0;
	if (diff < 0) { code = 8; diff = -diff; }
	if (diff >= step)        { code |= 4; diff -= step; }
	if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; }
	if (diff >= (step >> 2)) { code |= 1; }
	/* reconstruct EXACTLY as the decoder will, to keep predictor in lockstep */
	int diffq = step >> 3;
	if (code & 4) diffq += step;
	if (code & 2) diffq += step >> 1;
	if (code & 1) diffq += step >> 2;
	if (code & 8) *pred -= diffq; else *pred += diffq;
	if (*pred >  32767) *pred =  32767;
	if (*pred < -32768) *pred = -32768;
	*idx += ima_index_tab[code & 7];
	if (*idx < 0)  *idx = 0;
	if (*idx > 88) *idx = 88;
	return (uint8_t)(code & 0x0F);
}
static inline int16_t ima_dec_step(uint8_t code, int32_t *pred, int *idx)
{
	int step = ima_step_tab[*idx];
	int diffq = step >> 3;
	if (code & 4) diffq += step;
	if (code & 2) diffq += step >> 1;
	if (code & 1) diffq += step >> 2;
	if (code & 8) *pred -= diffq; else *pred += diffq;
	if (*pred >  32767) *pred =  32767;
	if (*pred < -32768) *pred = -32768;
	*idx += ima_index_tab[code & 7];
	if (*idx < 0)  *idx = 0;
	if (*idx > 88) *idx = 88;
	return (int16_t)*pred;
}

/* Encode exactly ONE block (SAMP_PER_BLK==1016 samples) into one 512-byte block,
 * predictor + step index RESET at block start -> block is standalone. */
static void adpcm_pack_block(const int16_t *ring, uint32_t ring_mask,
                             uint32_t start, uint8_t *blk)
{
	uint32_t pos = start;
	int32_t pred = ring[pos];          /* seed predictor = first sample */
	int idx = 0;                        /* fixed reset step index */
	blk[0] = (uint8_t)(pred & 0xFF);
	blk[1] = (uint8_t)((pred >> 8) & 0xFF);
	blk[2] = (uint8_t)idx;
	blk[3] = 0;
	uint8_t *data = blk + ADPCM_HDR_BYTES;
	for (uint32_t i = 0; i < ADPCM_DATA_BYTES; i++) {
		int16_t s0 = ring[pos];  pos = (pos + 1u) & ring_mask;
		uint8_t n0 = ima_enc_step(s0, &pred, &idx);
		int16_t s1 = ring[pos];  pos = (pos + 1u) & ring_mask;
		uint8_t n1 = ima_enc_step(s1, &pred, &idx);
		data[i] = (uint8_t)(n0 | (n1 << 4));
	}
}
/* Decode exactly ONE block back into SAMP_PER_BLK==1016 ring samples. */
static void adpcm_unpack_block(int16_t *ring, uint32_t ring_mask,
                               uint32_t start, const uint8_t *blk)
{
	uint32_t pos = start;
	int32_t pred = (int16_t)((uint16_t)blk[0] | ((uint16_t)blk[1] << 8));
	int idx = blk[2];
	if (idx > 88) idx = 88;
	const uint8_t *data = blk + ADPCM_HDR_BYTES;
	for (uint32_t i = 0; i < ADPCM_DATA_BYTES; i++) {
		uint8_t b = data[i];
		ring[pos] = ima_dec_step(b & 0x0F, &pred, &idx);
		pos = (pos + 1u) & ring_mask;
		ring[pos] = ima_dec_step((b >> 4) & 0x0F, &pred, &idx);
		pos = (pos + 1u) & ring_mask;
	}
}
/* nblk blocks, each independent (fresh predictor) — REQUIRED for random-access
 * loop seeks: a play read can start at ANY block, so every block must decode
 * without history from the previous one. */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk)
{
	uint32_t pos = start;
	for (uint32_t b = 0; b < nblk; b++) {
		adpcm_pack_block(ring, ring_mask, pos, flash + b * EMMC_BLOCK_SIZE);
		pos = (pos + SAMP_PER_BLK) & ring_mask;
	}
}
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk)
{
	uint32_t pos = start;
	for (uint32_t b = 0; b < nblk; b++) {
		adpcm_unpack_block(ring, ring_mask, pos, flash + b * EMMC_BLOCK_SIZE);
		pos = (pos + SAMP_PER_BLK) & ring_mask;
	}
}
#endif /* SP1_CODEC */

/* ==== M71: verified async reads for the streamer. The port (M50) +
 * CRC tails (M54) + PSEL re-orient (M55r6) are the six-bench-proven
 * triple. <=16-block chunks (the tail stash caps at 16), 3 attempts,
 * then the SYNC path as courier fallback — never worse than before.
 * ONLY the streamer thread may call this (single storage owner). ==== */
extern void emmc_m50_setup(void);
extern int emmc_m50_read_async(uint32_t blk, uint8_t *buf, uint32_t n);
extern int emmc_m50_wait(int ms);
extern uint8_t m54_tails[32][2];
/* HDRCHK-789: the header canary + its latch; g_rd_p14s tells the unpack the burst it decodes was P14S */
static volatile uint32_t g_hdr_bad, g_hdr_fixed, g_hdr_alien; static uint8_t g_rd_p14s;
static volatile uint32_t g_hdr_zero; static uint8_t g_aln_hex[16];   /* ALIEN-790: the pads, and the first real alien's header at decode time */
static uint32_t g_hdr_blk; static uint8_t g_hdr_idx, g_hdr_hex[16];
static uint32_t __attribute__((noinline)) hdr_check(const uint8_t *buf, uint32_t c)
{	/* 0 = clean (or not a P14S burst); else 1 + the index of the first unmarked block */
	if (buf[11] != P14S_MARK) return 0u;
	for (uint32_t bi = 1u; bi < c; bi++) if (buf[bi * 512u + 11u] != P14S_MARK) return bi + 1u;
	return 0u;
}
extern void emmc_m54_crc_init(void);
extern uint16_t emmc_m54_crc16(const uint8_t *d, uint32_t n);
static volatile uint32_t g_m71_as, g_m71_rt, g_m71_fb;
/* CRCC-625 (W302): THE CRC CANARY. Every block is checked at <= CRCC_RATE_FULL
 * blk/s (4 tracks x 1x = 686) and for CRCC_FULL_BLOCKS blocks after boot or any
 * mismatch; above the rate, one block per read call at a rotating position.
 * A systemic fault re-arms full checking by itself and shows as rt>0. */
#define CRCC_RATE_FULL   720u
#define CRCC_FULL_BLOCKS 4096u
static uint32_t          g_crcc_full = CRCC_FULL_BLOCKS;   /* blocks still to check fully */
static uint32_t          g_crcc_pick;                      /* rotating sampled position */
static int64_t           g_crcc_w0;                        /* rate window start, ms */
static uint32_t          g_crcc_wn, g_crcc_rate;           /* blocks this window; blk/s */
static volatile uint32_t g_m71_vf, g_m71_sk;               /* blocks checked / left unchecked */
static volatile uint32_t g_m71_ca, g_m71_rq, g_m71_rd, g_m71_wu;
/* RRT-630 (W306): THE REC-RING TRACE -- what the streamer was doing while the
 * rec ring filled. A 24-event ring (R read / W write / P pass / S sleep, with
 * wall us), recorded while a take is on the ring and frozen the first time
 * the backlog crosses 3/4; per-boot totals; TKD = takes whose ring overran.
 * Diag only: no behaviour change, no mixer text. */
#define RRT_N 24u
#define RRT_R 1u
#define RRT_W 2u
#define RRT_P 3u
#define RRT_S 4u
struct rrt_ev { uint8_t t, n; uint16_t us; };
static struct rrt_ev g_rrt[RRT_N];
static uint8_t  g_rrt_i, g_rrt_frz, g_rrt_on;
static uint32_t g_rrt_frz_fill, g_rrt_frz_ms;
static uint32_t g_rrt_p1, g_rrt_gap_us, g_rrt_gap_fill, g_rrt_p1_last;
static uint32_t g_rrt_wr_n, g_rrt_wr_us, g_rrt_wr_max, g_rrt_rd_n, g_rrt_rd_us, g_rrt_sl_us;
static uint32_t g_rrt_ovr0, g_tkd_n, g_tkd_ovr;
static uint32_t g_p1spr_n, g_p1spr_cut;   /* SPRINT-P1-631: passes whose flush ran boosted; 150 ms bound hits */
static struct rrt_ev g_rrt_hw[RRT_N];      /* RETRY-633: the ring as it was at the high-water moment */
static uint8_t  g_rrt_hw_i;
static uint32_t g_rrt_hw_fill, g_rrt_hw_ms, g_rrt_wretry;
extern volatile uint32_t g_aw2_werr;        /* the write chain's CRC-status rejections (sp1_emmc.c) */
/* TLT-649: THE TAIL TRACE -- the window between a take's finalize (TS_DONE
 * first seen by PASS 1) and its promotion, whose starves STV counts as pf.
 * Per boot: the last tail's numbers and the event ring as it stood at the
 * promotion; the prime and the meta save that follow it are timed too. */
static uint8_t  g_tl_on;
static uint32_t g_tl_n, g_tl_t0, g_tl_ms, g_tl_fill0, g_tl_p1, g_tl_wr, g_tl_wus, g_tl_rd, g_tl_rus, g_tl_sl, g_tl_pf;
static uint32_t g_tl_p1_0, g_tl_wr_0, g_tl_wus_0, g_tl_rd_0, g_tl_rus_0, g_tl_sl_0, g_tl_pf_0;
static uint32_t g_tl_prime_us, g_tl_meta_us, g_tl_meta_n;
static struct rrt_ev g_rrt_tl[RRT_N];
static uint8_t  g_rrt_tl_i;
/* the rec backlog in emissions, whichever track owns the ring (TS_REC or its tail flush) */
static uint32_t rrt_fill(int *who)
{
	uint32_t fill = 0u; int w = -1;
	for (int j = 0; j < NTRK; j++) {
		uint8_t sj = trk[j].state;
		if (sj != TS_REC && sj != TS_DONE) continue;
		uint32_t f = trk[j].r_w - trk[j].r_r;
		if (f >= fill) { fill = f; w = j; }
	}
	if (who) *who = w;
	return fill;
}
static void rrt_ev(uint8_t t, uint32_t n, uint32_t us)
{
	if (!g_rrt_on) return;
	if (t == RRT_R) { g_rrt_rd_n += n; g_rrt_rd_us += us; }
	else if (t == RRT_W) { g_rrt_wr_n += n; g_rrt_wr_us += us; if (us > g_rrt_wr_max) g_rrt_wr_max = us; }
	else if (t == RRT_S) g_rrt_sl_us += us;
	struct rrt_ev *e = &g_rrt[g_rrt_i];
	e->t = t; e->n = (uint8_t)(n > 255u ? 255u : n); e->us = (uint16_t)(us > 65535u ? 65535u : us);
	g_rrt_i = (uint8_t)((g_rrt_i + 1u) % RRT_N);
	uint32_t fill = rrt_fill(NULL);
	if (!g_rrt_frz && fill >= (RRING_SAMPLES * 2u) / 4u * 3u) { g_rrt_frz = 1u; g_rrt_frz_fill = fill; g_rrt_frz_ms = k_uptime_get_32(); }
	if (fill > g_rrt_hw_fill) {   /* RETRY-633: keep the ring as it stood at every new high-water mark */
		g_rrt_hw_fill = fill; g_rrt_hw_ms = k_uptime_get_32(); g_rrt_hw_i = g_rrt_i;
		for (uint32_t k = 0; k < RRT_N; k++) g_rrt_hw[k] = g_rrt[k];
	}
}
/* once per streamer pass, at the top of PASS 1 */
static void __attribute__((noinline)) rrt_pass1(void)
{
	int who = -1;
	uint32_t fill = rrt_fill(&who);
	uint32_t now = DWT->CYCCNT;
	if (who >= 0 && !g_rrt_on) { g_rrt_ovr0 = g_rec_overruns; g_rrt_p1_last = now; }   /* a take just landed on the ring */
	g_rrt_on = (who >= 0) ? 1u : 0u;
	if (who < 0) return;
	g_rrt_p1++;
	if (!g_tl_on && trk[who].state == TS_DONE) {   /* TLT-649: the tail begins */
		g_tl_on = 1u; g_tl_n++; g_tl_t0 = k_uptime_get_32();
		g_tl_fill0 = fill / TSPBI(who);
		g_tl_p1_0 = g_rrt_p1; g_tl_wr_0 = g_rrt_wr_n; g_tl_wus_0 = g_rrt_wr_us;
		g_tl_rd_0 = g_rrt_rd_n; g_tl_rus_0 = g_rrt_rd_us; g_tl_sl_0 = g_rrt_sl_us; g_tl_pf_0 = g_stv_pf;
	}
	uint32_t gap = (uint32_t)(now - g_rrt_p1_last) / 64u; g_rrt_p1_last = now;
	if (gap > g_rrt_gap_us) { g_rrt_gap_us = gap; g_rrt_gap_fill = fill; }
	rrt_ev(RRT_P, fill / TSPBI(who), gap / 1000u);   /* RETRY-633: P carries its gap in ms */
}
/* at a take's promotion: did its ring overrun while it was on the ring? */
static void rrt_take_done(void)
{
	if (g_tl_on) {   /* TLT-649: the tail ends here; keep its numbers and its ring */
		g_tl_on = 0u; g_tl_ms = k_uptime_get_32() - g_tl_t0;
		g_tl_p1 = g_rrt_p1 - g_tl_p1_0; g_tl_wr = g_rrt_wr_n - g_tl_wr_0; g_tl_wus = g_rrt_wr_us - g_tl_wus_0;
		g_tl_rd = g_rrt_rd_n - g_tl_rd_0; g_tl_rus = g_rrt_rd_us - g_tl_rus_0; g_tl_sl = g_rrt_sl_us - g_tl_sl_0;
		g_tl_pf = g_stv_pf - g_tl_pf_0;
		g_rrt_tl_i = g_rrt_i;
		for (uint32_t k = 0; k < RRT_N; k++) g_rrt_tl[k] = g_rrt[k];
	}
	uint32_t d = g_rec_overruns - g_rrt_ovr0;
	if (d) { g_tkd_n++; g_tkd_ovr = d; }
	g_rrt_ovr0 = g_rec_overruns;
}
/* ==== PCM14S: the two-tier TAKE codec (design doc + script 459) ====
 * 24 kHz stereo 14-bit shaped. 512 B = 16 B hdr + 140 stored frames
 * x 3.5 B. ONE BLOCK = 280 ENGINE frames: the decoder upsamples 2:1
 * (linear), so the transport/region/bus math is identical to A7.
 * Marker blk[11] = 0x5B -> per-block dispatch; mixed tracks decode.
 * Shaper e1 is CARRIED across blocks (encode-side; decode stateless);
 * reset at punch-in. Continuity: the sequential fill heals the odd
 * frame before each block against the previous block's last stored
 * frame (per-track state); seam/reverse paths duplicate one frame.
 * 64-bit pack shifts (4 x 14 = 56 bits) -- see 459's C-port note. */

/* PK32-484: the last PCM14S hot function still at -Os. Stage K gave
 * the A7 encoder -O2 and 479 gave the decoder -O2; this is the
 * encode side of marc's record corner. */
__attribute__((optimize("O2")))
static void p14s_pack_blocks(const int16_t *ring, uint32_t ring_mask,
			     uint32_t start, uint8_t *out, uint32_t nblk)
{
	uint32_t _tep = DWT->CYCCNT;   /* EP-484 */
	for (uint32_t b = 0; b < nblk; b++) {
		uint8_t *blk = out + b * EMMC_BLOCK_SIZE;
		uint32_t f0 = start + b * 140u;   /* CD-463: STORED (24k) frames */
		blk[0] = 0x50u; blk[1] = 0x31u; blk[2] = 0x34u; blk[3] = 0x53u;
		blk[4] = 0; blk[5] = 1u; blk[6] = 0; blk[7] = 0;
		blk[8] = 0; blk[9] = 0; blk[10] = 0; blk[11] = P14S_MARK;
		blk[12] = (uint8_t)(SAMP_PER_BLK & 0xFFu);
		blk[13] = (uint8_t)(SAMP_PER_BLK >> 8);
		/* BG-470: BLOCK FLOATING POINT. Pass 1 -- peak of the 140 stored
		 * frames (both channels). Pass 2 -- encode with the samples
		 * scaled up by `sh`, so the effective quantizer step is 4 >> sh.
		 * At a typical playing level (peak ~ -14 dBFS) sh lands on 2 and
		 * the step becomes 1: bit-exact 16-bit storage, matching v2.7.2
		 * (marc's clean reference). Headroom check leaves room for the
		 * shaper error (clamped +-16) plus one step. */
		int32_t _pk = 0;
		for (uint32_t i = 0; i < 140u; i++) {
			uint32_t fa = ((f0 + i) & ring_mask) * 2u;
			int32_t a = (int32_t)ring[fa];      if (a < 0) a = -a;
			int32_t d = (int32_t)ring[fa + 1u]; if (d < 0) d = -d;
			if (a > _pk) _pk = a;
			if (d > _pk) _pk = d;
		}
		uint32_t sh = 0u;
		while (sh < 3u && (_pk << (sh + 1u)) < 32000) sh++;
		blk[14] = (uint8_t)sh; blk[15] = 0;
		/* S8/Q RULE: the shaper error is invalid across a step-size
		 * change -- reset e1 whenever the shift moves (this exact bug
		 * bit the S8/Q build three times). */
		if (sh != g_p14s_sh) { g_p14s_e1[0] = 0; g_p14s_e1[1] = 0; g_p14s_sh = sh; }
		uint32_t o = 16u;
		for (uint32_t i = 0; i < 140u; i += 2u) {
			int32_t q4[4];
			for (uint32_t k = 0; k < 2u; k++) {
				uint32_t fa = ((f0 + i + k) & ring_mask) * 2u;
				for (uint32_t c = 0; c < 2u; c++) {
					/* CD-463: boxcar moved to capture; ring is already 24k */
					int32_t s24 = (int32_t)ring[fa + c] << sh;   /* BG-470 */
					int32_t v = s24 - g_p14s_e1[c];
					if (v > 32767) v = 32767;
					else if (v < -32768) v = -32768;
					int32_t q = (v + 2) >> 2;
					if (q > 8191) q = 8191;
					else if (q < -8192) q = -8192;
					g_p14s_e1[c] = (q << 2) - v;
					if (g_p14s_e1[c] > 16) g_p14s_e1[c] = 16;
					else if (g_p14s_e1[c] < -16) g_p14s_e1[c] = -16;
					q4[k * 2u + c] = q;
				}
			}
			{	/* PK32-484: two 32-bit words, no 64-bit arithmetic.
				 * Exact inverse of the 477 decode. */
				uint32_t _p0 = (uint32_t)q4[0] & 0x3FFFu;
				uint32_t _p1 = (uint32_t)q4[1] & 0x3FFFu;
				uint32_t _p2 = (uint32_t)q4[2] & 0x3FFFu;
				uint32_t _p3 = (uint32_t)q4[3] & 0x3FFFu;
				uint32_t _pa = (_p0 << 18) | (_p1 << 4) | (_p2 >> 10);
				uint32_t _pb = (_p2 << 14) | _p3;
				blk[o     ] = (uint8_t)(_pa >> 24);
				blk[o + 1u] = (uint8_t)(_pa >> 16);
				blk[o + 2u] = (uint8_t)(_pa >>  8);
				blk[o + 3u] = (uint8_t)(_pa);
				blk[o + 4u] = (uint8_t)(_pb >> 16);
				blk[o + 5u] = (uint8_t)(_pb >>  8);
				blk[o + 6u] = (uint8_t)(_pb);
			}
			o += 7u;
		}
	}
	if (g_rec_track >= 0) {   /* EP-484: record corner, any speed */
		g_t_ep += (uint32_t)(DWT->CYCCNT - _tep);
		g_t_epn++;
		g_ep_blk += nblk;
	}
}

/* decode ONE P14S block into a7_scrL/R (280 engine frames). The last
 * odd frame duplicates (healed by the sequential path via prev). */
/* O2P-479: the firmware is built -Os (CONFIG_SIZE_OPTIMIZATIONS=y).
 * Stage K gave the A7 codec per-function -O2; this decoder was written
 * later (two-tier, 460) and never got it -- while becoming the hottest
 * function in the build (476: 40.9%% of the corner's wall time). The 478
 * disassembly showed -Os spilling to the stack inside the inner loop. */
__attribute__((optimize("O2")))
static void p14s_dec_scr(const uint8_t *blk)
{
	M73_T0();   /* UP-476 */
	/* BG-470: block-gain shift; 0 in every pre-470 take -> identical decode */
	const uint32_t _sh = (uint32_t)(blk[14] & 3u);
	const uint8_t *o = blk + 16u;
	for (uint32_t i = 0; i < 140u; i += 2u) {
		/* X32-477: two overlapping big-endian 32-bit words replace the
		 * uint64_t. A = v64 bits 55..24, B = v64 bits 31..0. */
		uint32_t _a32, _b32;
		memcpy(&_a32, o, 4);
		memcpy(&_b32, o + 3, 4);
		_a32 = __builtin_bswap32(_a32);
		_b32 = __builtin_bswap32(_b32);
		o += 7u;
		int32_t s0 = (int32_t)((_a32 >> 18) & 0x3FFFu);
		int32_t s1 = (int32_t)((_a32 >>  4) & 0x3FFFu);
		int32_t s2 = (int32_t)((_b32 >> 14) & 0x3FFFu);
		int32_t s3 = (int32_t)( _b32        & 0x3FFFu);
		if (s0 > 8191) s0 -= 16384; if (s1 > 8191) s1 -= 16384;
		if (s2 > 8191) s2 -= 16384; if (s3 > 8191) s3 -= 16384;
		p14s_scr[i * 4u]        = (int16_t)((s0 << 2) >> _sh);  /* BG-470 */
		p14s_scr[i * 4u + 1u]   = (int16_t)((s1 << 2) >> _sh);
		p14s_scr[i * 4u + 4u]   = (int16_t)((s2 << 2) >> _sh);
		p14s_scr[i * 4u + 5u]   = (int16_t)((s3 << 2) >> _sh);
	}
	for (uint32_t j = 0; j < 277u; j += 2u) {   /* IL-489: was 279u, OOB */
		p14s_scr[j * 2u + 2u] = (int16_t)(((int32_t)p14s_scr[j * 2u]
						  + p14s_scr[j * 2u + 4u]) >> 1);
		p14s_scr[j * 2u + 3u] = (int16_t)(((int32_t)p14s_scr[j * 2u + 1u]
						  + p14s_scr[j * 2u + 5u]) >> 1);
	}
	p14s_scr[558] = p14s_scr[556]; p14s_scr[559] = p14s_scr[557];
	M73_ADD(g_t_ps);   /* UP-476 */
}

/* sequential fill dispatch: per-block marker branch; P14S blocks copy
 * scr -> stereo pring and HEAL the previous odd frame via per-track
 * continuity. A7 blocks fall through to codec_unpack one at a time. */
/* O2P-479: same reasoning -- this carries the scratch->ring copy, which
 * 476 measured at pk - ps = 15.9%% of the corner. */
__attribute__((optimize("O2")))
/* ==== P16-522: the PCM16-mono take codec (the spike's PROVEN arms) =====
 * Storage: 512-B block = 16-B header ('P16M' sig, blk[11]=P14S_MARK kept
 * so the block-level dispatch is untouched, blk[5]=0 mono, blk[12..13]=496)
 * + 248 little-endian int16 stored samples (24 kHz mono). Unit map (W136):
 * pack-internal f0 = STORED frames; decode s0 = ENGINE frames. */
static void p16m_dec_scr(const uint8_t *blk)
{
	/* r2 interp upsample, ear-proven: frame 2i = v[i] (both channels),
	 * frame 2i+1 = (v[i]+v[i+1]+1)>>1; the final odd frame holds. */
	for (uint32_t i = 0; i < 248u; i++) {
		int32_t v  = (int16_t)((uint16_t)blk[16u + i * 2u] |
		                       ((uint16_t)blk[16u + i * 2u + 1u] << 8));
		int32_t vn = (i < 247u)
		           ? (int16_t)((uint16_t)blk[18u + i * 2u] |
		                       ((uint16_t)blk[19u + i * 2u] << 8))
		           : v;
		int32_t h = (v + vn + 1) >> 1;
		p14s_scr[i * 4u]      = (int16_t)v; p14s_scr[i * 4u + 1u] = (int16_t)v;
		p14s_scr[i * 4u + 2u] = (int16_t)h; p14s_scr[i * 4u + 3u] = (int16_t)h;
	}
}

static void p16m_pack_blocks(const int16_t *ring, uint32_t ring_mask,
			     uint32_t start, uint8_t *out, uint32_t nblk)
{
	/* the spike r4 arm: 248 CONSECUTIVE stored frames per block, stride 1,
	 * downmix (L+R+1)>>1. start is in STORED (24k) frames -- W136. */
	for (uint32_t b = 0; b < nblk; b++) {
		uint8_t *blk = out + b * EMMC_BLOCK_SIZE;
		uint32_t f0 = start + b * 248u;   /* STORED (24k) frames */
		blk[0] = 0x50u; blk[1] = 0x31u; blk[2] = 0x36u; blk[3] = 0x4Du;
		blk[4] = 0; blk[5] = 0; blk[6] = 0; blk[7] = 0;
		blk[8] = 0; blk[9] = 0; blk[10] = 0; blk[11] = P14S_MARK;
		blk[12] = (uint8_t)(496u & 0xFFu);
		blk[13] = (uint8_t)(496u >> 8);
		blk[14] = 0; blk[15] = 0;
		uint32_t o = 16u;
		for (uint32_t i = 0; i < 248u; i++) {
			uint32_t fa = ((f0 + i) & ring_mask) * 2u;
			int32_t m = ((int32_t)ring[fa] + (int32_t)ring[fa + 1u] + 1) >> 1;
			blk[o]      = (uint8_t)((uint16_t)m & 0xFFu);
			blk[o + 1u] = (uint8_t)((uint16_t)m >> 8);
			o += 2u;
		}
	}
}

static void takes_pack_blocks(struct looptrk *t, const int16_t *ring,
			      uint32_t ring_mask, uint32_t start,
			      uint8_t *out, uint32_t nblk)
{
	if (t->p16m) p16m_pack_blocks(ring, ring_mask, start, out, nblk);
	else         p14s_pack_blocks(ring, ring_mask, start, out, nblk);
}

/* P16-522: the unpack family parameter is named `trk`, which SHADOWS
 * the global track array -- TSPBI(trk) would expand to trk[trk].p16m.
 * This helper lives OUTSIDE the shadowed scope. */
static uint32_t p16m_trk_spb(int ti)
{
	return TSPBI(ti);
}

/* ==== UNPK-616: decode STRAIGHT INTO THE RING (audit tier 2 row 12) ====
 * The scratch round trip -- decode 280 frames into p14s_scr, run the
 * midpoint pass over it, then copy 280 words into the ring and heal the
 * previous odd frame -- was ~1,400 memory ops per block that the ring
 * write can absorb: the midpoints are formed from the two exact frames in
 * registers and every frame is stored ONCE, as one aligned 32-bit word
 * (L low half, R high half). The ring wraps at most once per block, so a
 * countdown replaces the per-frame mask. Bit-exact with the scratch path
 * by construction (same expressions, same rounding) and proven on the host
 * over random blocks / wrap positions / continuity before every build.
 * The scratch decoders stay for the reverse path (p14s_unpack_rev). */
#define UNPK_EMIT(l, r) do { \
		*_d++ = (uint32_t)(uint16_t)(l) | ((uint32_t)(uint16_t)(r) << 16); \
		if (--_left == 0u) { _d = (uint32_t *)(void *)ring; _left = 0xFFFFFFFFu; } \
	} while (0)

/* O2P-479's intent, made explicit: the decode+store loop at -O2 (the old
 * copy loop had it; p16m_dec_scr had it by placement). A different optimize
 * level also keeps GCC from inlining these into the -Os unpack, so the
 * unpack's pinned 32-byte line and its own text stay small and stable. */
static void __attribute__((optimize("O2"))) p14s_dec_ring(const uint8_t *blk, int16_t *ring, uint32_t ring_mask, uint32_t s0, int trk)
{
	M73_T0();   /* UP-476 */
	const uint32_t _sh = (uint32_t)(blk[14] & 3u);   /* BG-470 block gain */
	const uint8_t *o = blk + 16u;
	const uint32_t _base = s0 & ring_mask;
	uint32_t  _left = ring_mask + 1u - _base;          /* frames before the wrap */
	uint32_t *_d    = (uint32_t *)(void *)(ring + (size_t)_base * 2u);
	int32_t pl = g_p14s_prev[trk][0], pr = g_p14s_prev[trk][1];
	for (uint32_t g = 0; g < 70u; g++) {
		/* X32-477: two overlapping big-endian 32-bit words replace the
		 * uint64_t. A = v64 bits 55..24, B = v64 bits 31..0. */
		uint32_t _a32, _b32;
		memcpy(&_a32, o, 4);
		memcpy(&_b32, o + 3, 4);
		_a32 = __builtin_bswap32(_a32);
		_b32 = __builtin_bswap32(_b32);
		o += 7u;
		int32_t s0v = (int32_t)((_a32 >> 18) & 0x3FFFu);
		int32_t s1v = (int32_t)((_a32 >>  4) & 0x3FFFu);
		int32_t s2v = (int32_t)((_b32 >> 14) & 0x3FFFu);
		int32_t s3v = (int32_t)( _b32        & 0x3FFFu);
		if (s0v > 8191) s0v -= 16384; if (s1v > 8191) s1v -= 16384;
		if (s2v > 8191) s2v -= 16384; if (s3v > 8191) s3v -= 16384;
		const int32_t l0 = (int16_t)((s0v << 2) >> _sh), r0 = (int16_t)((s1v << 2) >> _sh);
		const int32_t l1 = (int16_t)((s2v << 2) >> _sh), r1 = (int16_t)((s3v << 2) >> _sh);
		if (g == 0u) {
			/* the HEAL: frame s0-1 = midpoint of the previous block's last
			 * exact frame and this block's first (the copy path wrote it
			 * after the copy; same value, same place) */
			uint32_t hp = ((s0 - 1u) & ring_mask) * 2u;
			ring[hp]      = (int16_t)((pl + l0) >> 1);
			ring[hp + 1u] = (int16_t)((pr + r0) >> 1);
		} else {
			UNPK_EMIT((int16_t)((pl + l0) >> 1), (int16_t)((pr + r0) >> 1));   /* frame 2k-1 */
		}
		UNPK_EMIT((int16_t)l0, (int16_t)r0);                                       /* frame 2k   */
		UNPK_EMIT((int16_t)((l0 + l1) >> 1), (int16_t)((r0 + r1) >> 1));          /* frame 2k+1 */
		UNPK_EMIT((int16_t)l1, (int16_t)r1);                                       /* frame 2k+2 */
		pl = l1; pr = r1;
	}
	UNPK_EMIT((int16_t)pl, (int16_t)pr);   /* frame 279: the hold, healed by the next block */
	g_p14s_prev[trk][0] = (int16_t)pl;     /* last EXACT frame (278) */
	g_p14s_prev[trk][1] = (int16_t)pr;
	M73_ADD(g_t_ps);   /* UP-476 */
}

static void __attribute__((optimize("O2"))) p16m_dec_ring(const uint8_t *blk, int16_t *ring, uint32_t ring_mask, uint32_t s0, int trk)
{
	/* P16-522 r2 interp upsample: frame 2i = v[i] (both channels), frame
	 * 2i+1 = (v[i]+v[i+1]+1)>>1; the final odd frame holds. */
	const uint32_t _base = s0 & ring_mask;
	uint32_t  _left = ring_mask + 1u - _base;
	uint32_t *_d    = (uint32_t *)(void *)(ring + (size_t)_base * 2u);
	int32_t v = (int16_t)((uint16_t)blk[16u] | ((uint16_t)blk[17u] << 8));
	{	/* the HEAL, exactly as the generic tail did it from scratch[0..1] */
		uint32_t hp = ((s0 - 1u) & ring_mask) * 2u;
		ring[hp]      = (int16_t)(((int32_t)g_p14s_prev[trk][0] + v) >> 1);
		ring[hp + 1u] = (int16_t)(((int32_t)g_p14s_prev[trk][1] + v) >> 1);
	}
	for (uint32_t i = 0; i < 248u; i++) {
		int32_t vn = (i < 247u)
		           ? (int16_t)((uint16_t)blk[18u + i * 2u] | ((uint16_t)blk[19u + i * 2u] << 8))
		           : v;
		int32_t h = (v + vn + 1) >> 1;
		UNPK_EMIT((int16_t)v, (int16_t)v);
		UNPK_EMIT((int16_t)h, (int16_t)h);
		v = vn;
	}
	g_p14s_prev[trk][0] = (int16_t)v;     /* frame 494 = v[247] */
	g_p14s_prev[trk][1] = (int16_t)v;
}

/* ALN2-573 (W256): PIN THE UNPACK TO A 32-BYTE ICACHE LINE.
 * 572's at-rest corner regressed 56%% on pk/call with every feature OFF.
 * The added instructions total ~137 cyc/block (0.04%%). The UNPACK -- code
 * nobody edited -- lost 2,121 cyc/block (+12.7%%), because 2.3 KB of new
 * flash upstream moved it +88 B: from mod32=0 to mod32=24. The nRF52840
 * ICACHE line is 32 B. W143 pinned streamer_thread for exactly this class
 * of bug and the pin held; the tripwire was simply watching the wrong
 * door -- the unpack is its own symbol (p14s_unpack.constprop.0).
 * 32, NOT 2048: W145 showed 2048-scatter (all-pin) and RAM-exec both made
 * the contention cliff WORSE. This is the minimal pin, one function.
 * If this build regresses, iterate the offset -- do not unpin. */
static void __attribute__((aligned(32))) p14s_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
			const uint8_t *flash, uint32_t nblk, int trk, uint32_t pad_spb)
{
	M73_T0();   /* UP-476 */
	rl_add(trk, start, nblk * pad_spb, 1u);   /* RUNLOG-785 */
	uint32_t s0 = start;
	for (uint32_t b = 0; b < nblk; b++) {
		const uint8_t *blk = flash + b * EMMC_BLOCK_SIZE;
		uint32_t _spb;
		if (blk[11] == P14S_MARK) {
			/* HG-646 (W185): the STRIDE comes from the block's own header,
			 * exactly as the decoder choice already did -- never from the
			 * ring's track. Under a head the ring's track is not the owner
			 * of these bytes, and striding by ITS geometry left 216 stale
			 * frames per block ("half-plugged cable"). trk stays the
			 * continuity index: the heal is per destination ring. */
			_spb = (blk[2] == 0x36u) ? 496u : SAMP_PER_BLK;
			/* UNPK-616: decode straight into the ring; the heal and the
			 * per-track continuity live inside the decoders now. */
			if      (blk[2] == 0x36u) p16m_dec_ring(blk, ring, ring_mask, s0, trk);   /* P16-522 */
			else if (blk[2] == 0x34u) p14s_dec_ring(blk, ring, ring_mask, s0, trk);
			else {	/* GS-531 (W144): mark present but FOREIGN sig -- a
				 * future codec or cross-version content. SILENCE,
				 * never garbage -- with the continuity the scratch path
				 * gave it (healed against 0, prev = 0). */
				for (uint32_t f = 0; f < _spb; f++) {
					uint32_t fi = ((s0 + f) & ring_mask) * 2u;
					ring[fi] = 0; ring[fi + 1u] = 0;
				}
				uint32_t hp = ((s0 - 1u) & ring_mask) * 2u;
				ring[hp]      = (int16_t)((int32_t)g_p14s_prev[trk][0] >> 1);
				ring[hp + 1u] = (int16_t)((int32_t)g_p14s_prev[trk][1] >> 1);
				g_p14s_prev[trk][0] = 0; g_p14s_prev[trk][1] = 0;
			}
		} else if (pad_spb == 496u) {
			/* P16-522: a non-P16M block under a P16M OWNER = the SILENCE
			 * PAD -- true silence at the owner's stride (HG-646: the
			 * caller names the owner; under a head that is the source). */
			_spb = 496u;
			for (uint32_t f = 0; f < _spb; f++) {
				uint32_t fi = ((s0 + f) & ring_mask) * 2u;
				ring[fi] = 0; ring[fi + 1u] = 0;
			}
		} else if (g_rd_p14s) {
			/* HDRCHK-789: an unmarked block inside a P14S burst -- not the card's block.
			 * A7-decoding it is rail garbage (the 20:30 click). Silence with continuity. */
			_spb = SAMP_PER_BLK;
			{	/* ALIEN-790: a silence-pad block is all zeros -- count it apart; latch the first real alien's header */
				uint32_t _nz = 0u; for (uint32_t _q = 0u; _q < 16u; _q++) _nz |= blk[_q];
				if (!_nz) g_hdr_zero++;
				else { if (g_hdr_alien == 0u) memcpy(g_aln_hex, blk, 16u); g_hdr_alien++; }
			}
			for (uint32_t f = 0; f < _spb; f++) {
				uint32_t fi = ((s0 + f) & ring_mask) * 2u;
				ring[fi] = 0; ring[fi + 1u] = 0;
			}
			{	uint32_t hp = ((s0 - 1u) & ring_mask) * 2u;
				ring[hp]      = (int16_t)((int32_t)g_p14s_prev[trk][0] >> 1);
				ring[hp + 1u] = (int16_t)((int32_t)g_p14s_prev[trk][1] >> 1);
				g_p14s_prev[trk][0] = 0; g_p14s_prev[trk][1] = 0; }
		} else {
			_spb = SAMP_PER_BLK;
			codec_unpack(ring, ring_mask, s0, blk, 1u);
		}
		s0 += _spb;
	}
	if (M73_CY_NOW()) g_pk_blk += nblk;   /* UP-476 */
	M73_ADD(g_t_pk);   /* UP-476 */
}

static void p14s_unpack_part(int16_t *ring, uint32_t ring_mask, uint32_t start,
			     const uint8_t *flash, uint32_t skip, uint32_t nsamp, int trk)
{
	rl_add(trk, start, nsamp, 2u);   /* RUNLOG-785 */
	/* FZ-464: this is NOT seam-rate -- the plain fill and the prime path
	 * both decode through here, so it needs the SAME cross-block
	 * continuity as the full-path decoder. Without it, every block's
	 * provisional last frame stayed a DUPLICATE: a step glitch up to
	 * 171x/s = the block-edge zipper marc's ear caught twice (E render;
	 * then on hardware as "fuzzy bass", 2026-08-26). Conformance: 12,691
	 * unhealed edges vs the reference, ALL at position 279, max 13,406.
	 * A7 blocks in the span fall through to codec_unpack_part. */
	uint32_t f = skip;
	uint32_t done = 0;
	const uint32_t _spb = p16m_trk_spb(trk);   /* P16-522: TRACK-driven, so the
	                                     * zeroed silence pad stays correct */
	while (done < nsamp) {
		uint32_t blkno = f / _spb;
		uint32_t off   = f - blkno * _spb;
		uint32_t run   = _spb - off;
		if (run > nsamp - done) run = nsamp - done;
		const uint8_t *blk = flash + blkno * EMMC_BLOCK_SIZE;
		if (blk[11] == P14S_MARK) {
			if      (blk[2] == 0x36u) p16m_dec_scr(blk);   /* P16-522 */
			else if (blk[2] == 0x34u) p14s_dec_scr(blk);
			else    /* GS-531 (W144): mark present but FOREIGN sig -- a
			         * future codec or cross-version content. SILENCE,
			         * never garbage. */
				memset(p14s_scr, 0, (size_t)_spb * 4u);
			for (uint32_t k = 0; k < run; k++) {
				uint32_t fi = ((start + done + k) & ring_mask) * 2u;
				ring[fi]      = p14s_scr[(off + k) * 2u];
				ring[fi + 1u] = p14s_scr[(off + k) * 2u + 1u];
			}
			if (off == 0u) {
				/* block ENTRY: heal the previous engine frame
				 * (written by an earlier pass or the loop seam)
				 * exactly like the full path. */
				uint32_t hp = ((start + done - 1u) & ring_mask) * 2u;
				ring[hp]      = (int16_t)(((int32_t)g_p14s_prev[trk][0] + p14s_scr[0]) >> 1);
				ring[hp + 1u] = (int16_t)(((int32_t)g_p14s_prev[trk][1] + p14s_scr[1]) >> 1);
			}
			if (off + run >= _spb - 1u) {   /* P16-522: was 279u = 280-1 */
				/* this pass decoded through the block's last stored
				 * frame -> it becomes the prev for the next entry. */
				g_p14s_prev[trk][0] = p14s_scr[_spb * 2u - 4u];   /* P16-522 */
				g_p14s_prev[trk][1] = p14s_scr[_spb * 2u - 3u];
			}
		} else if (_spb == 496u) {
			/* P16-522: a non-P16M block under a P16M take = the SILENCE
			 * PAD. Write true silence; the A7 fallthrough would page a
			 * zeroed buffer at the wrong geometry. */
			for (uint32_t k = 0; k < run; k++) {
				uint32_t fi = ((start + done + k) & ring_mask) * 2u;
				ring[fi] = 0; ring[fi + 1u] = 0;
			}
		} else {
			codec_unpack_part(ring, ring_mask, start + done, flash, f, run);
		}
		f += run; done += run;
	}
}

static void p14s_unpack_rev(int16_t *ring, uint32_t ring_mask, uint32_t start,
			    const uint8_t *flash, uint32_t nblk, int trk, uint32_t pad_spb)
{
	rl_add(trk, start, nblk * pad_spb, 3u);   /* RUNLOG-785 */
	/* REV-638 (W308): a burst read as [lo .. hi] plays BACKWARD, so the ring
	 * gets block hi first -- descending, as codec_unpack_rev always did. Each
	 * block is emitted time-reversed; its FIRST ring frame is the healed edge:
	 * the midpoint of this block's last exact frame and the previously emitted
	 * block's first exact frame (the next block in source order), carried per
	 * destination ring in g_p14s_prev exactly like the forward heal, same
	 * rounding. Geometry comes from the block header; trk is continuity only. */
	uint32_t _s0 = start;
	int32_t pl = g_p14s_prev[trk][0], pr = g_p14s_prev[trk][1];
	for (uint32_t b = nblk; b-- > 0u; ) {
		const uint8_t *blk = flash + b * EMMC_BLOCK_SIZE;
		if (blk[11] == P14S_MARK) {
			const uint32_t _spb = (blk[2] == 0x36u) ? 496u : SAMP_PER_BLK;
			if      (blk[2] == 0x36u) p16m_dec_scr(blk);   /* P16-522 */
			else if (blk[2] == 0x34u) p14s_dec_scr(blk);
			else    /* GS-531 (W144): mark present but FOREIGN sig -- a
			         * future codec or cross-version content. SILENCE,
			         * never garbage. */
				memset(p14s_scr, 0, (size_t)_spb * 4u);
			{	/* frame 0 of the reversed block = the healed edge */
				uint32_t fi = (_s0 & ring_mask) * 2u;
				ring[fi]      = (int16_t)(((int32_t)p14s_scr[(_spb - 2u) * 2u]      + pl) >> 1);
				ring[fi + 1u] = (int16_t)(((int32_t)p14s_scr[(_spb - 2u) * 2u + 1u] + pr) >> 1);
			}
			for (uint32_t f = 1; f < _spb; f++) {   /* then frames spb-2 .. 0 */
				uint32_t fi = ((_s0 + f) & ring_mask) * 2u;
				ring[fi]      = p14s_scr[(_spb - 1u - f) * 2u];
				ring[fi + 1u] = p14s_scr[(_spb - 1u - f) * 2u + 1u];
			}
			pl = p14s_scr[0]; pr = p14s_scr[1];   /* this block's first exact frame heals the next emitted block */
			_s0 += _spb;
		} else if (pad_spb == 496u) {
			/* HG-646: the OWNER's silence pad, at the owner's stride */
			for (uint32_t f = 0; f < 496u; f++) {
				uint32_t fi = ((_s0 + f) & ring_mask) * 2u;
				ring[fi] = 0; ring[fi + 1u] = 0;
			}
			_s0 += 496u;
		} else {
			codec_unpack_rev(ring, ring_mask, _s0, blk, 1u);
			_s0 += SAMP_PER_BLK;
		}
	}
	g_p14s_prev[trk][0] = (int16_t)pl;
	g_p14s_prev[trk][1] = (int16_t)pr;
}

static void p14s_mask_from_x3(uint32_t slot)
{
	g_p14s_mask = 0;
	if (g_x3_ok && slot < NUM_SLOTS)
		for (int xi = 0; xi < NTRK; xi++)
			if (g_x3.t[slot][xi].codec_id == X3_CODEC_P14S ||
			    g_x3.t[slot][xi].codec_id == X3_CODEC_P16M) {
				g_p14s_mask |= (uint8_t)(1u << xi);
				trk[xi].p16m = (g_x3.t[slot][xi].codec_id
				                == X3_CODEC_P16M) ? 1u : 0u;   /* P16-522 */
				trk[xi].gsh  = (uint8_t)((g_x3.t[slot][xi].flags >> 1) & 3u);   /* BNC2-604 */
				{	/* PS-535, as in the restore path */
					uint8_t _rv = g_x3.t[slot][xi].rsv;
					trk[xi].p16m_next = (_rv & 0x80u)
					                  ? (uint8_t)(_rv & 1u)
					                  : trk[xi].p16m;
				}
			}
}

static bool emmc_read_blocks_fast(uint32_t blk, uint8_t *buf, uint32_t n)
{
	M73_T0();
	int64_t _t0 = k_uptime_get();
	uint32_t _rrt0 = DWT->CYCCNT;   /* RRT-630 */
	g_m71_ca++; g_m71_rq += n;
	{	/* CRCC-625: the read rate over ~half-second windows (one divide per window) */
		g_crcc_wn += n;
		int64_t _dtw = _t0 - g_crcc_w0;
		if (_dtw >= 512) {
			g_crcc_rate = (_dtw < 60000) ? (uint32_t)(((uint64_t)g_crcc_wn * 1000u) / (uint64_t)_dtw) : 0u;
			g_crcc_wn = 0u; g_crcc_w0 = _t0;
		}
	}
	static uint8_t m71_init;   /* 0 = not yet, 1 = ok */
	if (!m71_init) {
		emmc_m50_setup();
		emmc_m54_crc_init();
		m71_init = 1;
	}
	uint32_t done = 0;
	while (done < n) {
		uint32_t c = n - done;
		if (c > 32u) c = 32u;   /* M71r5: whole turn in one arm */
		uint8_t *dst = buf + done * EMMC_BLOCK_SIZE;
		int good = 0;
		for (int attempt = 0; attempt < 3 && !good; attempt++) {
			int bad, r;
			if (attempt) g_m71_rt++;
			if (!emmc_m50_read_async(blk + done, dst, c)) {
				k_msleep(1);
				continue;
			}
			r = emmc_m50_wait(300);
			if (r != 1) continue;
			bad = -1;
			{	/* CRCC-625 (W302): the canary -- every block below the rate or while
				 * re-armed, else one rotating block per call; a mismatch retries the
				 * turn fully checked (the attempt loop) and re-arms full checking */
				const bool _full = (g_crcc_full != 0u) || (g_crcc_rate <= CRCC_RATE_FULL);
				const uint32_t _pick = g_crcc_pick++ % c;
				for (uint32_t bi = 0; bi < c; bi++) {
					if (!_full && bi != _pick) { g_m71_sk++; continue; }
					uint16_t cc = emmc_m54_crc16(dst + bi * 512u, 512u);
					uint16_t tb = (uint16_t)((m54_tails[bi][0] << 8) |
								  m54_tails[bi][1]);
					if (cc != tb) { bad = (int)bi; break; }
					g_m71_vf++;
				}
				if (bad >= 0) g_crcc_full = CRCC_FULL_BLOCKS;
				else if (g_crcc_full) g_crcc_full = (g_crcc_full > c) ? (g_crcc_full - c) : 0u;
			}
			if (bad < 0) {	/* HDRCHK-789: the header canary -- a P14S burst with an unmarked block is a suspect read */
				const uint32_t hb = hdr_check(dst, c);
				if (hb) {
					g_hdr_bad++;
					if (g_hdr_bad == 1u) { g_hdr_blk = blk + done + hb - 1u; g_hdr_idx = (uint8_t)(hb - 1u); memcpy(g_hdr_hex, dst + (hb - 1u) * 512u, 16u); }
					continue;   /* the attempt loop re-reads the chunk */
				}
				if (g_hdr_bad && attempt) g_hdr_fixed++;   /* a retry after a suspect read came back clean */
			}
			if (bad < 0) good = 1;
		}
		if (!good) {
			/* courier: the proven sync path takes this chunk */
			g_m71_fb++;
			if (!emmc_read_blocks(blk + done, dst, c))
				return false;
		} else {
			g_m71_as += c;
		}
		done += c;
	}
	g_rd_p14s = (buf[11] == P14S_MARK) ? 1u : 0u;   /* HDRCHK-789 */
	g_m71_wu += (uint32_t)(k_uptime_get() - _t0);
	rrt_ev(RRT_R, n, (uint32_t)(DWT->CYCCNT - _rrt0) / 64u);   /* RRT-630 */
	M73_ADD(g_t_rd);
	return true;
}


/* BAKE-619: Lanczos-3, 128 phases, Q14, every row sums to 16384 exactly
 * (generated by the stage; the host proof compiles this same text). */
static const int16_t s_bk_lz[128][6] = {
	{      0,      0,  16384,      0,      0,      0 },
	{     26,   -105,  16383,    107,    -27,      0 },
	{     52,   -207,  16377,    216,    -54,      0 },
	{     76,   -307,  16368,    328,    -82,      1 },
	{    100,   -405,  16356,    442,   -111,      2 },
	{    124,   -500,  16339,    558,   -140,      3 },
	{    147,   -593,  16320,    676,   -170,      4 },
	{    169,   -684,  16297,    797,   -201,      6 },
	{    190,   -772,  16272,    919,   -232,      7 },
	{    211,   -858,  16242,   1044,   -264,      9 },
	{    230,   -941,  16208,   1171,   -296,     12 },
	{    250,  -1023,  16172,   1300,   -329,     14 },
	{    268,  -1101,  16131,   1432,   -363,     17 },
	{    286,  -1178,  16088,   1565,   -397,     20 },
	{    303,  -1252,  16041,   1700,   -431,     23 },
	{    319,  -1323,  15991,   1837,   -466,     26 },
	{    335,  -1393,  15936,   1977,   -501,     30 },
	{    350,  -1459,  15878,   2118,   -537,     34 },
	{    364,  -1524,  15818,   2261,   -573,     38 },
	{    378,  -1586,  15753,   2406,   -610,     43 },
	{    391,  -1646,  15686,   2553,   -647,     47 },
	{    403,  -1703,  15616,   2701,   -685,     52 },
	{    415,  -1758,  15541,   2852,   -723,     57 },
	{    425,  -1811,  15464,   3004,   -761,     63 },
	{    436,  -1862,  15384,   3157,   -799,     68 },
	{    445,  -1910,  15301,   3312,   -838,     74 },
	{    454,  -1956,  15214,   3469,   -877,     80 },
	{    462,  -1999,  15122,   3628,   -916,     87 },
	{    470,  -2040,  15029,   3787,   -955,     93 },
	{    476,  -2080,  14934,   3949,   -995,    100 },
	{    483,  -2116,  14833,   4111,  -1034,    107 },
	{    488,  -2151,  14732,   4275,  -1074,    114 },
	{    493,  -2184,  14628,   4440,  -1114,    121 },
	{    498,  -2214,  14519,   4607,  -1154,    128 },
	{    502,  -2242,  14408,   4774,  -1194,    136 },
	{    505,  -2268,  14294,   4943,  -1234,    144 },
	{    507,  -2292,  14179,   5112,  -1274,    152 },
	{    510,  -2314,  14058,   5283,  -1313,    160 },
	{    511,  -2334,  13938,   5454,  -1353,    168 },
	{    512,  -2351,  13812,   5627,  -1393,    177 },
	{    513,  -2367,  13685,   5800,  -1432,    185 },
	{    513,  -2381,  13555,   5974,  -1471,    194 },
	{    512,  -2393,  13425,   6148,  -1510,    202 },
	{    511,  -2403,  13290,   6324,  -1549,    211 },
	{    510,  -2411,  13153,   6499,  -1587,    220 },
	{    508,  -2417,  13014,   6675,  -1625,    229 },
	{    505,  -2421,  12872,   6852,  -1662,    238 },
	{    502,  -2424,  12728,   7029,  -1699,    248 },
	{    499,  -2425,  12583,   7206,  -1736,    257 },
	{    495,  -2424,  12436,   7383,  -1772,    266 },
	{    491,  -2421,  12286,   7561,  -1808,    275 },
	{    487,  -2417,  12134,   7738,  -1843,    285 },
	{    482,  -2411,  11980,   7916,  -1877,    294 },
	{    477,  -2403,  11825,   8093,  -1911,    303 },
	{    471,  -2394,  11669,   8270,  -1944,    312 },
	{    465,  -2384,  11510,   8447,  -1976,    322 },
	{    459,  -2372,  11349,   8624,  -2007,    331 },
	{    453,  -2358,  11187,   8800,  -2038,    340 },
	{    446,  -2343,  11024,   8976,  -2068,    349 },
	{    439,  -2327,  10860,   9151,  -2097,    358 },
	{    432,  -2309,  10693,   9326,  -2125,    367 },
	{    424,  -2290,  10527,   9500,  -2152,    375 },
	{    417,  -2270,  10357,   9673,  -2177,    384 },
	{    409,  -2249,  10188,   9846,  -2202,    392 },
	{    401,  -2226,  10017,  10017,  -2226,    401 },
	{    392,  -2202,   9846,  10188,  -2249,    409 },
	{    384,  -2177,   9672,  10358,  -2270,    417 },
	{    375,  -2152,   9501,  10526,  -2290,    424 },
	{    367,  -2125,   9326,  10693,  -2309,    432 },
	{    358,  -2097,   9152,  10859,  -2327,    439 },
	{    349,  -2068,   8976,  11024,  -2343,    446 },
	{    340,  -2038,   8800,  11187,  -2358,    453 },
	{    331,  -2007,   8624,  11349,  -2372,    459 },
	{    322,  -1976,   8448,  11509,  -2384,    465 },
	{    312,  -1944,   8271,  11668,  -2394,    471 },
	{    303,  -1911,   8093,  11825,  -2403,    477 },
	{    294,  -1877,   7916,  11980,  -2411,    482 },
	{    285,  -1843,   7738,  12134,  -2417,    487 },
	{    275,  -1808,   7561,  12286,  -2421,    491 },
	{    266,  -1772,   7384,  12435,  -2424,    495 },
	{    257,  -1736,   7206,  12583,  -2425,    499 },
	{    248,  -1699,   7028,  12729,  -2424,    502 },
	{    238,  -1662,   6852,  12872,  -2421,    505 },
	{    229,  -1625,   6675,  13014,  -2417,    508 },
	{    220,  -1587,   6499,  13153,  -2411,    510 },
	{    211,  -1549,   6325,  13289,  -2403,    511 },
	{    202,  -1510,   6149,  13424,  -2393,    512 },
	{    194,  -1471,   5973,  13556,  -2381,    513 },
	{    185,  -1432,   5799,  13686,  -2367,    513 },
	{    177,  -1393,   5626,  13813,  -2351,    512 },
	{    168,  -1353,   5455,  13937,  -2334,    511 },
	{    160,  -1313,   5282,  14059,  -2314,    510 },
	{    152,  -1274,   5113,  14178,  -2292,    507 },
	{    144,  -1234,   4942,  14295,  -2268,    505 },
	{    136,  -1194,   4774,  14408,  -2242,    502 },
	{    128,  -1154,   4607,  14519,  -2214,    498 },
	{    121,  -1114,   4441,  14627,  -2184,    493 },
	{    114,  -1074,   4275,  14732,  -2151,    488 },
	{    107,  -1034,   4110,  14834,  -2116,    483 },
	{    100,   -995,   3949,  14934,  -2080,    476 },
	{     93,   -955,   3786,  15030,  -2040,    470 },
	{     87,   -916,   3627,  15123,  -1999,    462 },
	{     80,   -877,   3470,  15213,  -1956,    454 },
	{     74,   -838,   3313,  15300,  -1910,    445 },
	{     68,   -799,   3158,  15383,  -1862,    436 },
	{     63,   -761,   3004,  15464,  -1811,    425 },
	{     57,   -723,   2852,  15541,  -1758,    415 },
	{     52,   -685,   2702,  15615,  -1703,    403 },
	{     47,   -647,   2553,  15686,  -1646,    391 },
	{     43,   -610,   2405,  15754,  -1586,    378 },
	{     38,   -573,   2261,  15818,  -1524,    364 },
	{     34,   -537,   2118,  15878,  -1459,    350 },
	{     30,   -501,   1977,  15936,  -1393,    335 },
	{     26,   -466,   1838,  15990,  -1323,    319 },
	{     23,   -431,   1701,  16040,  -1252,    303 },
	{     20,   -397,   1565,  16088,  -1178,    286 },
	{     17,   -363,   1432,  16131,  -1101,    268 },
	{     14,   -329,   1300,  16172,  -1023,    250 },
	{     12,   -296,   1171,  16208,   -941,    230 },
	{      9,   -264,   1044,  16242,   -858,    211 },
	{      7,   -232,    919,  16272,   -772,    190 },
	{      6,   -201,    796,  16298,   -684,    169 },
	{      4,   -170,    675,  16321,   -593,    147 },
	{      3,   -140,    557,  16340,   -500,    124 },
	{      2,   -111,    442,  16356,   -405,    100 },
	{      1,    -82,    328,  16368,   -307,     76 },
	{      0,    -54,    216,  16377,   -207,     52 },
	{      0,    -27,    108,  16382,   -105,     26 },
};

/* ===== BAKE-619 (W298): THE SPEED-BAKE KERNEL (flush side) =====
 * A bounce armed at tape speed s records the bus through the recorder's
 * tape tick, so the take holds 24k*s stored frames per second of what was
 * heard, and at 1x it plays the sources back at 1x again -- the speed is
 * undone, not printed (marc, 09-05). The bake resamples the rec ring by s
 * as the streamer packs it: the print becomes a 1x recording of what was
 * heard, and the tape speed applies to it like to any other track.
 *   input  : g_rring frames from p0 = r_r>>1, the recorder's 24k*s stream
 *   output : fpb frames per baked block; Lanczos-3 (6 taps, 128 phases,
 *            Q14, unity gain per phase), exact Q16 phase accumulation
 *   1.0x   : g_bk_spd stays 0 and the flush is the unchanged 616 code
 * Host-measured (sine SNR vs the ideal 24k signal): 1.37x 8 kHz 35 dB /
 * 10 kHz 33 dB; 0.73x 5 kHz 42 dB -- Catmull-Rom was 22 / 15 / 17, and
 * the player's own 2-tap read is worse still. The close machinery is
 * untouched (recorder units); promotion converts len/content to baked
 * blocks (nearest). Seam residual <= half a baked block, the class of
 * today's immediate-stop pad. Stage = batchbuf's tail (blocks 30-31; the
 * live flush never packs more than 16). ~60 instructions per baked frame
 * on the streamer, during a bounce at != 1.0x only. */
static inline uint32_t bk_fpb(const struct looptrk *t) { return t->p16m ? 248u : 140u; }

/* Is this track's flush a bake? The print's gain shift is set at the same
 * anchor that captures the speed, so a stale speed can never reach a
 * normal take (a cancel restores gsh; every new take arms with gsh 0). */
static inline bool bk_flush_on(const struct looptrk *t, int i)
{
	return g_bk_spd != 0u && g_bk_trk == (int8_t)i && t->gsh == (uint8_t)BNC_GSH;
}

/* Whole baked blocks the input on hand can produce from the current phase:
 * block k's last output sits at p0-relative frame (ph + (k*fpb-1)*spd)>>16
 * and the kernel reads three frames past it. TS_DONE with a remainder: one
 * tail block, end-clamped. Capped at the region. */
static uint32_t bk_navail(const struct looptrk *t)
{
	uint32_t in = (t->r_w - t->r_r) >> 1;
	uint32_t fpb = bk_fpb(t), spd = g_bk_spd, k = 0u;
	if (in >= 4u) {
		uint64_t room = ((uint64_t)(in - 3u) << 16);   /* need ph + (k*fpb-1)*spd < room */
		if (room > g_bk_ph) {
			uint64_t q = (room - g_bk_ph - 1u) / spd;   /* max k*fpb-1 */
			k = (uint32_t)((q + 1u) / fpb);
		}
	}
	if (k == 0u && t->state == TS_DONE && in >= 1u) k = 1u;   /* the tail */
	if (g_bk_blocks + k > MAX_LOOP_BLOCKS) {
		k = MAX_LOOP_BLOCKS - g_bk_blocks;
		if (t->state == TS_DONE) g_bk_capped = 1u;
	}
	return k;
}

/* SPEEDBAKE-768: the bake reads the tape speed NOW, once per flush iteration --
 * the flush trails the recorder by ~100-170 ms, so a sweep is smeared by that
 * and no more. Floor 0.25x (RANGE-655); a stopped tape keeps the last speed. */
/* SPDLOG-777: the speed the frames at recorder position `pos` (samples since the take's
 * start) were recorded at, relative to the print's reference. Newest entry back: the
 * block containing pos is the LAST entry whose end is past pos. Older than the log = the
 * oldest logged speed; an empty log = the speed now. */
static uint32_t __attribute__((noinline)) bk_spd_at(uint32_t pos)
{
	const uint8_t w = g_sl_wr;
	uint32_t sp = 0u;
	for (uint32_t k = 1u; k <= SL_N; k++) {
		const uint32_t i = (uint32_t)(w - k) & (SL_N - 1u);
		const uint32_t rc = g_sl_rc[i];
		if (!rc) continue;
		if (rc > pos) sp = g_sl_sp[i];
		else break;
	}
	return bk_rel(sp ? sp : *(volatile const uint32_t *)&g_cur_speed_q16);
}

static inline int bk_spd_live(const struct looptrk *t, int i)
{
	if (bk_flush_on(t, i)) {
		g_bk_spd = bk_spd_at(t->r_r - g_sl_base);   /* SPDLOG-777: the speed at r_r -- bk_navail's bound; each block looks up its own */
	}
	return 1;
}

/* The flush's unit of work, in whichever units this take is in. */
static inline uint32_t bk_flush_navail(const struct looptrk *t, int i)
{
	/* TAPECOPY-684: the tap/hold verdict (<= BK_DECIDE_MS after the chord) decides bake or
	 * not; until it lands, a bounce that COULD bake packs nothing -- the ring holds it.
	 * A print that reached TS_DONE undecided is a tape copy (the release is what stops it). */
	if (g_bk_spd != 0u && g_bk_trk == (int8_t)i && g_bk_mode == 0u) {
		if (t->state != TS_DONE) return 0u;
		g_bk_mode = 1u; g_bk_spd = 0u;
	}
	return bk_flush_on(t, i) ? bk_navail(t) : (t->r_w - t->r_r) / TSPB(t);
}

/* One baked block: fpb stereo frames into dst, from phase ph with p0 at
 * absolute input frame p0a and `in` frames on hand. Taps p0-2..p0+3 around
 * the interval; the two frames before p0 are still in the ring (the writer
 * is < 8191 ahead) except at the take's start, where they clamp to frame 0;
 * the end clamps to the last frame (the tail block only). Returns the phase
 * after the block; *cons = input frames consumed (all of them at the tail). */
static uint32_t __attribute__((optimize("O2"), noinline))
bk_resample_block(const struct looptrk *t, int16_t *dst, uint32_t ph, uint32_t p0a, uint32_t in, uint32_t *cons, uint32_t spd)   /* SPDLOG-777: the block's own speed */
{
	const uint32_t fpb = bk_fpb(t);
	const uint32_t last = in ? in - 1u : 0u;
	const int16_t *ring = g_rring;
	for (uint32_t j = 0; j < fpb; j++) {
		const uint32_t k = ph >> 16;
		const int16_t *w = s_bk_lz[(ph & 0xFFFFu) >> 9];
		int32_t yl = 8192, yr = 8192;
		const uint32_t f0 = (p0a + k - 2u) & RRING_MASK;   /* ring index of tap 0 (p0-2) */
		if (k >= 2u && k + 3u <= last && f0 + 5u < RRING_SAMPLES) {
			/* INTERIOR (all but a few frames per print): six consecutive
			 * ring frames, no clamp, no wrap -- straight-line MACs. */
			const int16_t *p = ring + f0 * 2u;
			yl += (int32_t)w[0] * p[0]  + (int32_t)w[1] * p[2]  + (int32_t)w[2] * p[4]
			    + (int32_t)w[3] * p[6]  + (int32_t)w[4] * p[8]  + (int32_t)w[5] * p[10];
			yr += (int32_t)w[0] * p[1]  + (int32_t)w[1] * p[3]  + (int32_t)w[2] * p[5]
			    + (int32_t)w[3] * p[7]  + (int32_t)w[4] * p[9]  + (int32_t)w[5] * p[11];
		} else {
			for (uint32_t tp = 0; tp < 6u; tp++) {
				uint32_t i;
				if (k + tp >= 2u) { i = k + tp - 2u; if (i > last) i = last; }
				else i = (p0a + k + tp >= 2u) ? (uint32_t)(k + tp - 2u) : 0u;   /* before p0: back into the ring, or clamp at the take start */
				const int16_t *p = ring + (((p0a + i) & RRING_MASK) * 2u);
				yl += (int32_t)w[tp] * p[0];
				yr += (int32_t)w[tp] * p[1];
			}
		}
		yl >>= 14; yr >>= 14;
		if (yl > 32767) yl = 32767; else if (yl < -32768) yl = -32768;
		if (yr > 32767) yr = 32767; else if (yr < -32768) yr = -32768;
		dst[2u * j] = (int16_t)yl; dst[2u * j + 1u] = (int16_t)yr;
		ph += spd;
	}
	*cons = ph >> 16;
	if (*cons > in) *cons = in;
	if (t->state == TS_DONE && *cons + 3u >= in) *cons = in;   /* the tail: <=3 frames would be lookahead only */
	return ph & 0xFFFFu;
}

/* Pack n baked blocks into out[] via the stage, WITHOUT committing: the
 * commit (r_r, phase, count) follows a successful write, exactly where the
 * plain path advances r_r. */
static uint32_t __attribute__((noinline)) bk_pack_blocks(struct looptrk *t, uint8_t *out, uint32_t n, int16_t *stage)
{
	uint32_t ph = g_bk_ph, off = 0u, b;
	const uint32_t p0a = t->r_r >> 1, in = (t->r_w - t->r_r) >> 1, fpb = bk_fpb(t);
	if (n > 28u) n = 28u;   /* the stage lives in blocks 30-31 of the same buffer */
	for (b = 0; b < n; b++) {
		uint32_t cons = 0u;
		/* SPDLOG-777: this block's input starts at p0a + off -- the speed its frames were recorded at */
		const uint32_t spd = bk_spd_at(((p0a + off) << 1) - g_sl_base);
		{	/* enough input for a whole block at THIS speed? (bk_navail bounded the count at r_r's speed) */
			const uint32_t rem = in - off;
			uint32_t k = 0u;
			if (rem >= 4u) {
				const uint64_t room = ((uint64_t)(rem - 3u) << 16);
				if (room > ph) k = (uint32_t)(((room - ph - 1u) / spd + 1u) / fpb);
			}
			if (k == 0u && !(t->state == TS_DONE && rem >= 1u)) break;
		}
		ph = bk_resample_block(t, stage, ph, p0a + off, in - off, &cons, spd);
		takes_pack_blocks(t, stage, 0xFFFFFFFFu, 0u, out + b * EMMC_BLOCK_SIZE, 1u);
		off += cons;
	}
	s_bk_ph_next = ph; s_bk_cons_next = off;
	return b;   /* the blocks actually produced */
}

/* Promotion: the close machinery declared the loop in RECORDER blocks;
 * the flash holds BAKED blocks. Nearest whole baked block; content never
 * beyond what was written; the recorder's leftovers are not packable in
 * their own units any more. Clears the bake. */
static void __attribute__((noinline)) bk_promote(struct looptrk *t)
{
	const uint32_t spd = g_bk_spd;
	uint32_t lb = g_bk_len ? g_bk_len   /* TRUE-621: the stop chose it */
	            : g_bk_lens ? (g_bk_lens + TSPB(t) / 2u) / TSPB(t)   /* BAKELATE-776: a snapped-back loop, from the integral */
	            : (uint32_t)((((uint64_t)t->len_blocks << 16) + spd / 2u) / spd);
	uint32_t cb = g_bk_blocks;   /* SPEEDBAKE-768: every baked block is content (the speed-derived count is not defined through a sweep) */
	if (lb < 1u) lb = 1u;
	if (lb > MAX_LOOP_BLOCKS) lb = MAX_LOOP_BLOCKS;
	if (lb > g_bk_blocks && g_bk_blocks) lb = g_bk_blocks;   /* SPEEDBAKE-768 / BAKELATE-776: never a loop past what was baked, in any case */
	if (cb < lb) cb = lb;                 /* TRUE-621: the loop's last block holds the fade, not silence */
	if (g_bk_len && cb > lb) cb = lb;     /* SPEEDBAKE-768: the recorded length IS the loop */
	if (cb > g_bk_blocks) cb = g_bk_blocks;
	if (cb < 1u) cb = 1u;
	const uint32_t _src = t->len_samps;   /* BAKELEN-727: the recorded length, sample-exact (whole beats of the true beat) */
	t->len_blocks = lb; t->content_blocks = cb;
	t->len_samps = lb * TSPB(t);
	if (_src) {   /* BAKELEN-727: the print's loop is the sources' length at the baked speed, not whole blocks */
		uint32_t _ls = g_bk_lens ? g_bk_lens   /* SPEEDBAKE-768: the integral at the loop's frames */
		             : (uint32_t)((((uint64_t)_src << 16) + spd / 2u) / spd);
		const uint32_t _hi = lb * TSPB(t), _lo = (lb - 1u) * TSPB(t) + 1u;
		if (_ls > _hi) _ls = _hi;
		if (_ls < _lo) _ls = _lo;
		t->len_samps = _ls;
	}
	if (g_bk_mode == 2u) {   /* SMPSTART-687: a held (sampler) print plays from its beginning at finger-up */
		uint32_t sb = (g_bk_stop_pos + TSPB(t) / 2u) / TSPB(t);
		t->start_blk = sb; t->start_samps = sb * TSPB(t);
	}
	t->r_r = t->r_w;
	g_bk_last_spd = spd;
	g_bk_spd = 0u; g_bk_trk = -1; g_bk_mode = 0u;   /* TAPECOPY-684 */
}
/* ===== end BAKE-619 kernel ===== */

/* ALN-525 (W143): PIN the streamer to a 2048-byte flash boundary.
 * The night of 2026-08-30 proved the max+stream corner regresses when
 * ANY upstream code growth shifts this function's address (0x25aac
 * clean everywhere, 0x25b34 regressed; the PAD probe -- dead bytes,
 * code unmoved -- ran clean). Pinning makes every future stage
 * placement-immune. If THIS build regresses, phase 0 is unlucky:
 * iterate the alignment offset, do not unpin. */
/* PAGE7V-718: set track t's nudge (1..255, 128 = centre) with 717's law -- refused while the
 * track is taking, the M14 dip when it is playing, the tail marked dirty. Controls thread only. */
static void nudge_set(int t, uint8_t v)
{
	if (t < 0 || t >= NTRK || g_slot >= NUM_SLOTS || v == 0u) return;
	if (v >= 120u && v <= 136u) v = 128u;
	const uint8_t _st = trk[t].state;
	if (_st == TS_ARMED || _st == TS_REC || _st == TS_DONE) return;
	const uint8_t _cur = g_trk_nudge[g_slot][t] ? g_trk_nudge[g_slot][t] : 128u;
	if (_cur == v) return;
	g_trk_nudge[g_slot][t] = v;
	g_grid_dirty_ms = k_uptime_get_32() | 1u;
	if (_st == TS_PLAY || head_active(t)) {
		g_head_blip[t] = 3;
		trk[t].p_w = (g_consume_pos / TSPB_SRC(t)) * TSPB_SRC(t);
	}
}
/* NUDGE-717: the anchor as the phase reads it -- (start + nudge) reduced into [0, mod). `start_mod`
 * is the caller's start % mod; `div` = 1 for the sample-exact paths, TSPB for the block paths. */
static uint32_t __attribute__((noinline)) nudge_anchor(int i, uint32_t start_mod, uint32_t mod, uint32_t div)
{
	uint32_t base = start_mod;
	if (g_br_shift && mod && div) base = (base + (g_br_shift / div)) % mod;   /* LOOPHOLD-804: the held tape, in this reader's units */
	if (i < 0 || i >= NTRK || g_slot >= NUM_SLOTS || !mod || !div) return base;
	const uint32_t nq = g_trk_nudge[g_slot][i];
	if (!nq || nq == 128u) return base;
	const struct looptrk *t = &trk[i];
	const uint32_t beat = (g_grid_active && g_grid_beat_frames)
	                    ? (uint32_t)(((uint64_t)g_grid_beat_frames * g_cur_speed_q16) >> 16)
	                    : ((t->len_samps ? t->len_samps : 1u) / 8u);
	const uint32_t q = (nq > 128u) ? (nq - 128u) : (128u - nq);
	uint32_t m = (uint32_t)((((uint64_t)q * beat) >> 8) / div) % mod;
	return (nq > 128u) ? (base + m) % mod : (base + mod - m) % mod;   /* LOOPHOLD-804: on top of the held tape */
}
/* CHOPCORE-805: THE single definition of the chop's geometry, and of a track's position inside it. Five sites used to
 * carry a copy of the phase and four a copy of the tile (PASS 2, the prime fill, br_geom, br_release, the LED pulse);
 * they drifted, and a fix applied to one of them is a bug in the other four (W350). Everything calls these now.
 * The tile math is 804's, verbatim. `base` is the song's loop in THIS TRACK's blocks (TLOOPB), so fixed mode takes the
 * same branch in every caller -- PASS 2 used to measure it in 280-sample blocks, which is wrong for a mono take. */
static void __attribute__((noinline)) chop_tile(uint32_t gb, uint32_t spb,
                                                uint32_t *pwper, uint32_t *pwin, uint32_t *pwbase, uint32_t *pcyc)
{
	const uint32_t cdiv = g_chop_div ? g_chop_div : 1u, coff = g_chop_off;
	const uint32_t _ll = g_loop_len;
	const uint32_t base = (_ll && spb) ? ((_ll + spb / 2u) / spb) : 0u;   /* TLOOPB, from the track's own geometry */
	uint32_t cyc, win, wbase, wper;
	if (g_fixed_len && base && gb >= base && (gb % base) == 0u) {
		wper = base;
		win = (wper + cdiv / 2u) / cdiv; if (win == 0u) win = 1u;   /* CHOPROUND-811: a musical division, rounded -- not truncated onto the storage grid (W348) */
		if (win > wper) win = wper;
		wbase = (coff * wper + cdiv / 2u) / cdiv;   /* CHOPROUND-811: each offset lands on the NEAREST block to its musical position */
		if (wbase + win > wper) wbase = wper - win;
		cyc = (gb / wper) * win;
	} else {
		wper = gb;
		win = (gb + cdiv / 2u) / cdiv; if (win == 0u) win = 1u;   /* CHOPROUND-811 (W348) */
		if (win > gb) win = gb;
		wbase = (coff * gb + cdiv / 2u) / cdiv;   /* CHOPROUND-811 */
		if (wbase + win > gb) wbase = gb - win;
		cyc = win;
	}
	if (g_win_free) {   /* CHOPNEST-813: the free window is the REGION; the rocker's chop SUBDIVIDES it.
	                     * Read the pair defensively (torn store). With cdiv == 1 / coff == 0 this reduces
	                     * exactly to M16's old override, so a region with no chop is unchanged. */
		uint32_t ws = g_win_s8, we = g_win_e8;
		if (we < ws) { uint32_t t2 = ws; ws = we; we = t2; }
		uint32_t rbase = (ws * wper) >> 8;
		uint32_t rlen  = ((we - ws + 1u) * wper) >> 8;
		if (rlen == 0u) rlen = 1u;
		if (rbase >= wper) rbase = wper - 1u;
		if (rbase + rlen > wper) rlen = wper - rbase;
		win = (rlen + cdiv / 2u) / cdiv; if (win == 0u) win = 1u;   /* CHOPROUND-811's law, on the region */
		if (win > rlen) win = rlen;
		wbase = rbase + (coff * rlen + cdiv / 2u) / cdiv;
		if (wbase + win > rbase + rlen) wbase = rbase + rlen - win;
		cyc = (gb / wper) * win;
	}
	*pwper = wper ? wper : 1u; *pwin = win ? win : 1u;
	*pwbase = wbase; *pcyc = cyc ? cyc : 1u;
}
/* CHOPANCHOR-805: a track's phase in the audible chop cycle. THE LOOP POSITION FIRST, then the window: `start_blk` is
 * a TRANSPORT block (where this take punched in), and `start_blk % cyc` reduced it by a musical window -- unrelated
 * quantities, so every take got an arbitrary phase the moment cyc != gb. `(start_blk % gb) % cyc` is the take's own
 * loop position reduced to the window, which is what the anchor always meant. When cyc == gb the two are the same
 * expression, so un-chopped playback is unchanged; `mod` stays cyc, so the nudge and LOOPHOLD-804's shift are too. */
static inline __attribute__((always_inline)) uint32_t chop_phase(int ni, uint32_t start_blk, uint32_t blk,
                                                                 uint32_t gb, uint32_t cyc, uint32_t spb)
{
	if (!gb) gb = 1u; if (!cyc) cyc = 1u;
	return ((blk % cyc) + cyc - nudge_anchor(ni, (start_blk % gb) % cyc, cyc, spb)) % cyc;
}
static void __attribute__((aligned(2048))) streamer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t blk[EMMC_BLOCK_SIZE];
	static uint8_t metabuf[META_BLOCKS * EMMC_BLOCK_SIZE];  /* 2-block song index */
	/* Flush the rec ring in MULTI-BLOCK (CMD25) bursts: the card pipelines the
	 * programming across the burst instead of fully programming each block (~30 ms
	 * single-block), so the sustained write keeps up with live recording. */
	/* R2C-774: batchbuf = g_bb.batch (file scope, shared with the decode scratch) */

	(void)emmc_init();
	/* AFTER init: emmc_init() resets the clock to the slow safe value — the
	 * old code zeroed it BEFORE init, so every bit-bang phase (start-bit
	 * hunts, CRC tokens, busy polls) has been running ~4x slower than
	 * intended this whole time. Zero it here so it actually sticks. */
	g_emmc_clk_half_us = 0u;
	g_emmc_ready = emmc_is_ready() ? 1 : 0;
	{ /* CX: start the 1 ms census HERE, at boot, not at the first
	   * DTR tick. The old placement meant the census only ever ran
	   * while 328 was attached, so under Protocol A it started
	   * AFTER the corner was over and every number it produced
	   * described a monitored machine. */
	  g_w4c_on = 1;
	  k_timer_init(&g_w4c_tmr, w4c_tick, NULL);
	  k_timer_start(&g_w4c_tmr, K_MSEC(1), K_MSEC(1));
	}

	/* Enable the card's internal write cache if it has one. Read EXT_CSD (CMD8) to
	 * check CACHE_SIZE and the spec revision; if present, turn the cache on. It
	 * absorbs the record write-bursts so an overdub acks from the card's SRAM
	 * instead of stalling the bus -- without it the 4th simultaneous track
	 * overflows the rec ring. There is deliberately NO flush during play (that
	 * freezes the bus and starves playback); the card flushes in the background,
	 * and we force a single flush at power-off (see stop_and_flush) so loops are
	 * durable. eMMC is streamer-only, so this boot-time read is safe here. */
	/* The write cache absorbs each record burst so the write returns immediately
	 * instead of programming NAND on the bus and starving the playing tracks
	 * (which is what crackles). Both builds use it; the 24 kHz build pairs it with
	 * the in-spec 16 MHz bus (the overclock, not the cache, was its white-noise). */
	if (g_emmc_ready && emmc_read_ext_csd(blk)) {
		uint32_t cache_kb = (uint32_t)blk[249] | ((uint32_t)blk[250] << 8) |
				    ((uint32_t)blk[251] << 16) | ((uint32_t)blk[252] << 24);
		g_cache_kb = cache_kb;
		/* diag snapshot: WR_REL_SET, WR_REL_PARAM, SEC_FEATURE_SUPPORT,
		 * BKOPS_SUPPORT, HPI_FEATURES, OUT_OF_INTERRUPT_TIME, BKOPS_STATUS,
		 * EXT_CSD_REV, ERASE_GROUP_DEF — confirms on the REAL unit which
		 * FTL-management features (TRIM/BKOPS/HPI) the card supports. */
		g_extcsd_dump[0] = blk[167]; g_extcsd_dump[1] = blk[166];
		g_extcsd_dump[2] = blk[231]; g_extcsd_dump[3] = blk[502];
		g_extcsd_dump[4] = blk[503]; g_extcsd_dump[5] = blk[198];
		g_extcsd_dump[6] = blk[246]; g_extcsd_dump[7] = blk[192];
		g_extcsd_dump[8] = blk[175];
		if (cache_kb > 0u && blk[192] >= 6u)   /* CACHE_SIZE>0, EXT_CSD_REV>=6 (v4.5+) */
			g_cache_on = emmc_cache_enable() ? 1u : 0u;
		if (blk[503] & 0x01) {                 /* HPI: abort lever for the idle flush */
			g_hpi_on = emmc_hpi_enable() ? 1u : 0u;
			if (g_hpi_on)
				emmc_set_abort_cb(emmc_busy_abort_chk);
		}
	}

	/* Load the slot metadata (block 0). If absent/invalid, format fresh — this
	 * overwrites the old TE album index, deleting the original songs + reclaiming
	 * the space (they couldn't be played on this hardware anyway). */
	memset(&g_meta, 0, sizeof(g_meta));
	g_meta.magic = META_MAGIC;
	for (uint32_t s = 0; s < NUM_SLOTS; s++) g_meta.slot[s].speed_q16 = 65536u;
	if (g_emmc_ready && emmc_read_blocks(META_BLOCK, metabuf, META_BLOCKS)) {
		struct meta_blk *m = (struct meta_blk *)metabuf;
		if (m->magic == META_MAGIC && m->cur_slot < NUM_SLOTS) {
			memcpy(&g_meta, m, sizeof(g_meta));     /* resume saved songs */
		} else {
			/* Unknown/older index (incl. 'SE4A'/'SE8A': their track
			 * regions were sized for 800-beat takes and don't line up
			 * with the 400-beat layout) -> one-time format-fresh. */
			memset(metabuf, 0, sizeof(metabuf));
			memcpy(metabuf, &g_meta, sizeof(g_meta));
			(void)meta_write_blocks(metabuf);
			/* GX-509: clear EVERY side table, not just the one whose
			 * magic changed (W111). GRID_EXT_MAGIC 'GRD1' is UNCHANGED
			 * between 2.x and 3.0 -- confirmed present in both shipped
			 * binaries -- and block 2 is read INDEPENDENTLY just below,
			 * so a 2.7.2 card's per-song grid tempos would survive onto
			 * the songs we have just cleared. The user would get 16 empty
			 * songs that already carry tempos, and ungridded first-take
			 * DETECTION would silently never fire, because a grid already
			 * exists -- and by M43 a grid, once set, does not move.
			 * The v3 table is self-validating, so a 2.x card reads as "no
			 * data" today; but a 3.0 -> 2.x -> 3.0 round trip can leave a
			 * VALID stale table describing songs that were cleared.
			 * BUFFER SAFETY (W92): metabuf is META_BLOCKS*512 = 1024 B.
			 * Never hand a 1024 B buffer to a 3-block write -- one block
			 * at a time. */
			memset(metabuf, 0, sizeof(metabuf));
			(void)emmc_write_blocks(GRID_EXT_BLOCK, metabuf, 1u);
			for (uint32_t _gb = 0; _gb < X3_NBLK; _gb++)
				(void)emmc_write_blocks(X3_BLK + _gb, metabuf, 1u);
		}
	}
	/* M8a: grid extension (block 2). Bad tag/sum -> all zeros = no grids. */
	if (g_emmc_ready && emmc_read_blocks(GRID_EXT_BLOCK, metabuf, 1)) {
		struct grid_ext *ge = (struct grid_ext *)metabuf;
		uint16_t gsum = 0;
		for (uint32_t gi = 0; gi < NUM_SLOTS; gi++)
			gsum = (uint16_t)(gsum + ge->bpm_q8[gi]);
		if (ge->magic == GRID_EXT_MAGIC && gsum == ge->sum)
			for (uint32_t gi = 0; gi < NUM_SLOTS; gi++)
				g_grid_bpm_q8[gi] = ge->bpm_q8[gi];
		grid_ext2_load(metabuf);   /* STACKT-716: the tail (own magic + sum) */
	}
	g_slot = g_meta.cur_slot;
	g_mode_pref = g_meta.fixed_len ? 1u : 0u;   /* M7c: global mode preference */
	g_fixed_len = g_mode_pref;                  /* effective refined when the
	                                             * current song loads (main) */
	/* M72: pull the v3 extended table (blocks 3-5). Invalid/stale ->
	 * g_x3_ok stays 0 and reloads use the block-derived values. */
	if (g_emmc_ready && emmc_read_blocks(X3_BLK, batchbuf, X3_NBLK)) {
		memcpy(&g_x3, batchbuf, sizeof(g_x3));
		g_x3_ok = x3_valid(&g_x3) ? 1u : 0u;
		p14s_mask_from_x3(g_slot);   /* P14S: shadow gates follow the table */
	}
	if (!g_x3_ok) memset(&g_x3, 0, sizeof(g_x3));
	g_meta_loaded = 1;

	while (1) {
		{ /* M46d: identify the streamer for the read wrapper's boost */
		  if (!g_str_tid) { g_str_tid = k_current_get();
		    g_pb_orig = k_thread_priority_get(k_current_get()); }
		}
#if SP1_XFER_ENABLE
		/* Website loop transfer: scan for the connect-magic / run one command
		 * per pass. While a transfer is active the transport is paused and
		 * the streamer serves ONLY the transfer (audio is silent anyway).
		 * v1.2.3: gated on USB being up — the streamer can now run during
		 * charge-standby, before usb_audio_start(). */
		if (g_usb_up)
			xfer_service();
#endif
		if (g_xfer_mode) { k_msleep(1); continue; }

		/* Power-off cache flush: program the volatile write cache to NAND so the
		 * last take + slot index survive a power cut. Requested by stop_and_flush
		 * AFTER recording is finalized + while shutting down, so this bus-blocking
		 * flush has nothing live to starve. Done here because the streamer is the
		 * only eMMC user. */
		if (g_emmc_quiesce) {                   /* shutting down: bus parked */
			k_msleep(10);
			continue;
		}
		if (g_cache_flush_req) {
			(void)emmc_cache_flush();
			g_emmc_quiesce = 1;   /* no further eMMC traffic after the final flush */
			g_cache_flush_req = 0;
			continue;
		}

		bool work = false;
		uint32_t cpos = g_consume_pos;
		uint32_t slot = g_slot;
		{ /* ==== DMP-466: ONE-SHOT TAKE-BLOCK DUMP (diagnostic; READS ONLY).
		   * Streams track-1 blocks as hex so the host reference decoder
		   * can rule on the actual card bytes. Runs only when: capture
		   * connected (armed by main's DTR tick), nothing recording or
		   * draining, playback stopped (consume frozen). Reads into its
		   * own g_dmp_buf (R1-597; it used to borrow the bounce
		   * accumulator). */
		  if (g_dmp_arm && g_dmp_state < 2u) {
			bool _di = true;
			for (int _dk = 0; _dk < NTRK; _dk++)
				if (trk[_dk].state == TS_REC || trk[_dk].state == TS_DONE)
					_di = false;
			static uint32_t _dcp;
			if (g_consume_pos != _dcp) { _dcp = g_consume_pos; _di = false; }
			if (_di) {
				uint8_t *_db = metabuf;   /* STACKT-716: the song-index buffer, idle in this pass */
				if (!g_dmp_state) {
					g_dmp_n = trk[0].len_blocks ? trk[0].len_blocks : 344u;
					if (g_dmp_n > 344u) g_dmp_n = 344u;
					printk("DMP,BEGIN,slot=%u,cid=%u,len=%u,base=%u\n",
					       (unsigned)g_slot,
					       (unsigned)((g_slot < NUM_SLOTS) ? g_x3.t[g_slot][0].codec_id : 255u),
					       (unsigned)g_dmp_n, (unsigned)trk_blk(g_slot, 0u));
					g_dmp_state = 1u; g_dmp_blk = 0u;
				}
				for (uint32_t _bi = 0u; _bi < 12u && g_dmp_blk < g_dmp_n; _bi++, g_dmp_blk++) {
					if (!emmc_read_blocks_fast(trk_blk(g_slot, 0u) + g_dmp_blk, _db, 1u)) {
						printk("DMP,RDERR,%u\n", (unsigned)g_dmp_blk);
						continue;
					}
					for (uint32_t _li = 0u; _li < 8u; _li++) {
						static const char _hx[] = "0123456789abcdef";
						char _ob[132];
						for (uint32_t _bb = 0u; _bb < 64u; _bb++) {
							uint8_t _v = _db[_li * 64u + _bb];
							_ob[_bb * 2u]      = _hx[_v >> 4];
							_ob[_bb * 2u + 1u] = _hx[_v & 0xFu];
						}
						_ob[128] = 0;
						printk("DMP,%u,%u,%s\n", (unsigned)g_dmp_blk, (unsigned)_li, _ob);
					}
					k_msleep(8);
				}
				if (g_dmp_blk >= g_dmp_n) {
					printk("DMP,END,%u\n", (unsigned)g_dmp_n);
					g_dmp_state = 2u;
				}
			}
		  }
		}

		if (g_meta_save_req) {                       /* persist songs + BPMs */
			g_meta_save_req = 0;
			if (g_emmc_ready) {
				uint32_t _tl_mt = DWT->CYCCNT;   /* TLT-649 */
				memset(metabuf, 0, sizeof(metabuf));
				memcpy(metabuf, &g_meta, sizeof(g_meta));
				(void)meta_write_blocks(metabuf);
				work = true;
				/* FX3-537: the M72 refresh is GONE. It re-authored this
				 * slot's rows from live trk[], and a song jump could
				 * slip between g_slot flipping (gesture thread) and the
				 * trk[] restore (audio thread) -- this pass then stamped
				 * the DESTINATION song's rows with the OLD song's state
				 * (marc's no-recording corruption). The cross-check that
				 * once made write order irrelevant never covered
				 * codec_id (522), which has no g_meta fallback. Rows are
				 * now authored ONLY at their events: boot load, take
				 * promotion (FX2), page toggle (FX2), delete (below).
				 * This service just writes RAM to the card. */
				if (slot < NUM_SLOTS) {
					g_x3.magic = X3_MAGIC; g_x3.ver = X3_VER;
					g_x3.sum = x3_sum(&g_x3);
					memset(batchbuf, 0, X3_NBLK * EMMC_BLOCK_SIZE);
					memcpy(batchbuf, &g_x3, sizeof(g_x3));
					if (emmc_write_blocks(X3_BLK, batchbuf, X3_NBLK))
						g_x3_ok = 1u;
				}
				g_tl_meta_us = (uint32_t)(DWT->CYCCNT - _tl_mt) / 64u; g_tl_meta_n++;   /* TLT-649 */
			}
		}
		if (g_grid_save_req) {                       /* persist grids (block 2) */
			g_grid_save_req = 0;
			if (g_emmc_ready) {
				memset(metabuf, 0, 512);
				struct grid_ext *ge = (struct grid_ext *)metabuf;
				ge->magic = GRID_EXT_MAGIC;
				uint16_t gsum = 0;
				for (uint32_t gi = 0; gi < NUM_SLOTS; gi++) {
					ge->bpm_q8[gi] = g_grid_bpm_q8[gi];
					gsum = (uint16_t)(gsum + ge->bpm_q8[gi]);
				}
				ge->sum = gsum;
				grid_ext2_store(metabuf);   /* STACKT-716: the tail rides along */
				(void)emmc_write_blocks(GRID_EXT_BLOCK, metabuf, 1);
				work = true;
			}
		}

		rrt_pass1();   /* RRT-630: the trace's pass marker + the flush-gap meter */
		/* SPRINT-P1-631 (W307): M87 raises g_emmc_sprint under rec-ring pressure so
		 * the flush gets the storm immunity the play rings have -- but the boost was
		 * only ever applied inside PASS 2's rounds; the pack and the page writes ran
		 * at PREEMPT(5) under USB and main (630's trace: ~200 ms per pass at the
		 * overdub corner). Sprint PASS 1 too, bounded to 150 ms per pass. */
		int _p1spr = 0; uint32_t _p1t0 = 0u;
		if (g_emmc_sprint) {
			_p1spr = 1; _p1t0 = k_uptime_get_32(); g_p1spr_n++;
			k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(1));
		}
		/* PASS 1 — WRITES FIRST. Flushing the rec ring always outranks play
		 * read-ahead: a rec-ring overflow corrupts the take permanently, while a
		 * play-ring underrun is only a brief, recoverable dropout. */
		for (int i = 0; i < NTRK; i++) {
			struct looptrk *t = &trk[i];
			uint8_t st = t->state;

			if (st == TS_REC || st == TS_DONE ||
			    (t->r_w - t->r_r) >= TSPB(t)) {
				/* M89: the wrap can flip a finished take to TS_PLAY
				 * before its tail is flushed (measured: 8,781-29,118
				 * frames stranded, frozen between takes -- every take
				 * stored missing its last stretch). Serve ANY backlog:
				 * the frames are real audio with fixed destinations;
				 * a late flush self-heals on the next loop pass. */
				uint32_t _wretry = 0u;   /* RETRY-633: failed-write retries this pass */
				while (bk_spd_live(t, i) && bk_flush_navail(t, i) > 0u) {   /* BAKE-619: in this take's units; SPEEDBAKE-768: at the speed now */
					if (_p1spr && (k_uptime_get_32() - _p1t0) > 150u) {   /* SPRINT-P1-631: the duty bound */
						_p1spr = 0; g_p1spr_cut++;
						k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(5));
					}
					uint32_t fm = t->flush_mod ? t->flush_mod : MAX_LOOP_BLOCKS;
					const bool _bkon = bk_flush_on(t, i);   /* SPEEDBAKE-768: ONE reading per iteration -- pack and commit agree */
					/* batch as many contiguous blocks as are ready, up to the
					 * buffer size and the loop-wrap boundary, into one CMD25 write */
					uint32_t navail = _bkon ? bk_navail(t) : (t->r_w - t->r_r) / TSPB(t);   /* BAKE-619 */
					uint32_t n = navail < FLUSH_BATCH ? navail : FLUSH_BATCH;
					if (!n) break;   /* SPEEDBAKE-768: a join between the two readings can leave nothing whole */
					uint32_t to_wrap = fm - (t->flush_blk % fm);
					if (n > to_wrap) n = to_wrap;
					uint32_t blkno = trk_blk(slot, (uint32_t)i) +
							 (t->flush_blk % fm);
					/* PAGE RULE: never let a burst straddle an 8KB (16-block)
					 * page — straddling forces the card into a slow read-
					 * modify-write; page-aligned bursts are fast. Misaligned
					 * start (overdub begun mid-loop):
					 * one short burst up to the boundary, aligned after.
					 * CRITICAL: while recording, WAIT for a full page before
					 * writing — draining the ring in dribbles makes every
					 * write a partial page = RMW = the slow path (this is
					 * what made the first 24 kHz build unable to record).
					 * Partial writes only at: overdub start, loop wrap, and
					 * the final tail after the take ends.
					 * Three cases below: (1) misaligned start -> trim to the next
					 * 8KB (16-block) page boundary; (2) >=1 whole page ready ->
					 * write whole pages only; (3) recording mid-loop with <1 page
					 * ready -> wait (the loop-wrap tail is exempt via n<to_wrap). */
					uint32_t mis = blkno % 16u;
					if (mis) {
						uint32_t to_page = 16u - mis;
						if (n > to_page) n = to_page;
					} else if (n >= 16u) {
						/* SINGLE whole pages deliberately: a 32-block
						 * double-burst experiment saved command overhead
						 * but each burst held the bus ~9 ms uninterrupted
						 * — at high tape speed the playing tracks can't
						 * ride out blackouts that long (hardware-measured:
						 * rec ring peaked 78%, a track fell 209 ms behind,
						 * MORE starves). Frequent small write bursts keep
						 * read latency bounded; total overhead matters
						 * less than its distribution here. */
						n &= ~15u;        /* whole pages only */
						if (n > 16u) n = 16u; /* CD-463: keep 460's burst size; drain ~800 blk/s vs 257 demand */
					} else if (t->state == TS_REC && n < to_wrap) {
						break;            /* let a full page accumulate */
					}
					/* ENCODE: rec ring (int16, wraps at RRING_MASK, r_r is
					 * block-aligned) -> packed flash bytes for n blocks. PCM is
					 * memcpy-equivalent; ULAW/ADPCM compress 2x/4x so this CMD25
					 * moves half/quarter the bytes the card must program. */
					if (0 /* CD-463: W3-r4 pair path FROZEN — it becomes reachable for
					       * the first time with the doubled ring; re-enable DELIBERATELY
					       * in its own build (one variable per build). */
					    && n == 16u && t->state == TS_REC &&
					    (blkno % 16u) == 0u && navail >= 32u &&
					    to_wrap >= 32u) {
						/* W3-r4 PIPELINE: pack the NEXT page while this one
						 * is in the card. Encode wall hides inside program
						 * time (the async port's overlap). Pair-granular
						 * play-crit break below; failures leave data in the
						 * ring for the single-burst path's streak logic. */
						bool aw3_burst_start(uint32_t, const uint8_t *, uint32_t);
						bool aw3_burst_wait(void);
						uint32_t _tw4 = DWT->CYCCNT;
						g_w4_pk++; g_w4_pb += 16u;
						takes_pack_blocks(t, g_rring, RRING_MASK, (t->r_r >> 1) & RRING_MASK,
						           batchbuf, 16u);
						bool _ok1 = aw3_burst_start(blkno, batchbuf, 16u);
						g_w4_pk++; g_w4_pb += 16u;
						takes_pack_blocks(t, g_rring, RRING_MASK,
						           ((t->r_r >> 1) + 16u * (t->p16m ? 248u : 140u)) & RRING_MASK,
						           batchbuf + 16u * EMMC_BLOCK_SIZE, 16u);
						if (_ok1) _ok1 = aw3_burst_wait();
						if (_ok1) {
							t->r_r += 16u * TSPB(t);
							t->flush_blk += 16u;
							bool _ok2 = aw3_burst_start(blkno + 16u,
							            batchbuf + 16u * EMMC_BLOCK_SIZE, 16u);
							if (_ok2) _ok2 = aw3_burst_wait();
							if (_ok2) {
								t->r_r += 16u * TSPB(t);
								t->flush_blk += 16u;
							}
						}
						{ uint32_t _dcx73 = (uint32_t)(DWT->CYCCNT - _tw4); g_t_wr += _dcx73; if (M73_CX_NOW()) { g_t_wr_cx += _dcx73; g_t_wr_cxn++; } if (M73_CY_NOW()) { g_t_wr_cy += _dcx73; g_t_wr_cyn++; } }
						work = true;
						if ((t->r_w - t->r_r) <
						    RRING_SAMPLES) {   /* CD-463: half of 2x-engine capacity */
							bool _pc4 = false;
							for (int j = 0; j < NTRK; j++)
								if ((trk[j].state == TS_PLAY ||
								     head_active(j)) &&
								    (int32_t)(trk[j].p_w - g_consume_pos) <
								    (int32_t)PLAY_CRIT_SAMPLES)
									_pc4 = true;
							if (_pc4)
								break;
						}
						continue;
					}
					g_w4_pk++; g_w4_pb += n;   /* W4P */
					if (_bkon)   /* BAKE-619: resample by the bounce speed on the way to flash */
					{	n = bk_pack_blocks(t, batchbuf, n, (int16_t *)(void *)(batchbuf + 30u * EMMC_BLOCK_SIZE));   /* SPDLOG-777: the count it could produce at each block's own speed */
						if (!n) break; }
					else
					takes_pack_blocks(t, g_rring, RRING_MASK, (t->r_r >> 1) & RRING_MASK,
					           batchbuf, n);
					static uint32_t wfail_start;   /* 0 = no failure streak */
					static uint32_t wfail_key;     /* streak identity (track|flush pos) */
					static uint8_t  wfail_ready1;  /* card seen READY once this streak */
					uint32_t _wkey = ((uint32_t)i << 28) ^ t->flush_blk;
					uint32_t _tw = DWT->CYCCNT;
					bool _wok = emmc_write_blocks(blkno, batchbuf, n);
					{ uint32_t _dcx73 = (uint32_t)(DWT->CYCCNT - _tw); g_t_wr += _dcx73; rrt_ev(RRT_W, n, _dcx73 / 64u);   /* RRT-630 */ if (M73_CX_NOW()) { g_t_wr_cx += _dcx73; g_t_wr_cxn++; } if (M73_CY_NOW()) { g_t_wr_cy += _dcx73; g_t_wr_cyn++; } }
					if (!_wok) {
						/* write failed (bus CRC or busy timeout): data is
						 * still in the ring — retry next pass. Give up and
						 * advance anyway (storing a glitch) ONLY after the
						 * card has been failing >400 ms of WALL TIME and
						 * reports READY_FOR_DATA via CMD13 (recovered yet
						 * genuinely rejecting). The old 8-fast-fails counter
						 * elapsed in <50 ms mid-stall and stored a glitch
						 * that REPLAYED at the same spot every loop pass. */
						uint32_t _now = k_uptime_get_32();
						/* STREAK IDENTITY: a streak abandoned mid-take
						 * (e.g. the track was deleted while flushing)
						 * must not leak its stale timestamp into the
						 * NEXT take — that made a single routine CRC
						 * blip give up instantly and silently drop a
						 * whole burst. */
						if (!wfail_start || wfail_key != _wkey) {
							wfail_start = _now | 1u;
							wfail_key = _wkey;
							wfail_ready1 = 0;
						}
						bool _giveup = false;
						if ((_now - wfail_start) > 400u) {
							uint8_t _r1[6];
							if (emmc_cmd13(_r1) && (_r1[3] & 0x01)) {
								/* READY often means the stall just
								 * ended and THIS attempt was its tail
								 * casualty — give the card ONE clean
								 * retry before declaring the data
								 * rejected for good. */
								if (wfail_ready1)
									_giveup = true;
								else
									wfail_ready1 = 1;
							}
						}
						if (!_giveup) {
							/* BACKOFF: the card is mid-stall; an
							 * immediate CMD25 retry is a zero-yield
							 * spin that starves MIDI/main (the WDT
							 * feeder). 2 ms costs nothing here. */
							rrt_ev(RRT_S, 2u, 2000u);   /* RRT-630 */
							k_msleep(2);
							work = true;
							/* RETRY-633 (W307): the same page again, at most twice, before
							 * yielding to PASS 2 -- a yield costs a turn (~8-12 blocks of
							 * input at 1.5x), a retry costs 2 ms + the write. The streak /
							 * give-up logic above sees every attempt as before. */
							if (++_wretry <= 2u) { g_rrt_wretry++; continue; }
							break;
						}
						g_stored_glitch_cnt++;  /* audible as a REPEATING artifact */
					}
					wfail_start = 0;
					wfail_ready1 = 0;
					if (_bkon) { t->r_r += 2u * s_bk_cons_next; g_bk_ph = s_bk_ph_next; g_bk_blocks += n; }   /* BAKE-619: commit */
					else { t->r_r += n * TSPB(t); if (g_bk_trk == (int8_t)i) g_bk_blocks += n; }   /* SPEEDBAKE-768: a 1x print's blocks are baked blocks 1:1 (a join counts from here) */
					t->flush_blk += n;
					work = true;
					/* FB-529 (W137): PROACTIVE page pacing at high tape
					 * speed. The tail flush's back-to-back 16-block bursts
					 * monopolize the bus exactly when the play rings drain
					 * 1.5x faster (STV pf = 148-259 at the corner, all with
					 * g_done_pending). One page per streamer pass, then
					 * PASS 2 serves; full writes-first priority returns the
					 * moment the backlog crosses half capacity, so data
					 * safety is unchanged (the ring holds ~341 ms). */
					if (g_cur_speed_q16 >= CX_SPEED_MIN &&
					    (t->r_w - t->r_r) < RRING_SAMPLES / 2u)   /* QUARTER-632 (W307): a quarter ring, not half */
						break;
					/* POST-STALL DRAIN ORDER: a big rec backlog must not
					 * starve the playing rings at their emptiest moment.
					 * After each burst, if any playing ring is inside its
					 * critical margin and the rec ring is NOT at the 7/8
					 * overflow emergency, break to PASS 2 to feed the
					 * emptiest ring one chunk, then resume flushing here.
					 * Burst-granular alternation only (one fully-terminated
					 * CMD25, then reads) — NOT the sub-page interleave that
					 * broke writes in an earlier experiment. */
					/* W3-r2: the polite play-first break now yields only
					 * below HALF a ring of backlog. The old 7/8 line left
					 * 43 ms of emergency headroom -- thinner than one
					 * write-cache stall -- and takes lapped inside it
					 * (W3-r1, campaign S43). Above half a ring the flush
					 * outranks play, as this pass's own preamble says. */
					if ((t->r_w - t->r_r) <
					    RRING_SAMPLES) {   /* CD-463: half of 2x-engine capacity */
						bool _pcrit = false;
						for (int j = 0; j < NTRK; j++)
							if ((trk[j].state == TS_PLAY ||
							     head_active(j)) &&
							    (int32_t)(trk[j].p_w - g_consume_pos) <
							    (int32_t)PLAY_CRIT_SAMPLES)
								_pcrit = true;
						if (_pcrit)
							break;  /* rec ring holds; feed play first */
					}
				}
				/* Promotion re-reads the LIVE state (not the pass-start snapshot)
				 * so an engine transition during the flush can't be overwritten.
				 * Order matters: request the meta save BEFORE publishing TS_PLAY,
				 * or stop_and_flush() (power-off/DFU) can observe "idle" between
				 * the two stores and sleep with the new recording unsaved. */
				if (t->state == TS_DONE && bk_flush_navail(t, i) == 0u) {   /* BAKE-619: in this take's units */
					if (bk_flush_on(t, i)) bk_promote(t);   /* BAKE-619: recorder blocks -> baked blocks */
					rrt_take_done();   /* RRT-630: TKD if the ring overran during this take */
					g_done_pending = 0;   /* M20: ring free again */
					/* Start playback BLOCK-ALIGNED at the live playhead. p_w must be a
					 * multiple of SAMP_PER_BLK or the streamer writes each eMMC block at
					 * a misaligned ring offset and the track plays ~16 ms out of sync. */
					if (slot < NUM_SLOTS) {
						g_meta.slot[slot].present[i]   = 1;
						g_meta.slot[slot].trk_len[i]   = t->len_blocks;  /* SEGMENT: per-track length */
						g_meta.slot[slot].trk_start[i] = t->start_blk;   /* + phase anchor */
						g_meta.trk_content[slot][i]    = t->content_blocks; /* silence-pad boundary */
						{	/* FX2-536: the x3 entry, stamped IN RAM at
							 * promotion. A deferred stamp raced song
							 * switches: the new take's codec_id was
							 * never written for THIS slot, and the
							 * stale id mis-strided the track-driven
							 * reader on return (the aux-cable sound). */
							struct x3_trk *_xe = &g_x3.t[slot][i];
							_xe->start_samps    = t->start_samps;
							_xe->len_samps      = t->len_samps;
							_xe->content_blocks = t->content_blocks;
							_xe->codec_id       = t->p16m ? X3_CODEC_P16M
							                              : X3_CODEC_P14S;
							_xe->flags          = (uint8_t)((g_cap_stereo ? 1u : 0u) |
							                      ((t->gsh & 3u) << 1));   /* BNC2-604: bits 1-2 */
							/* PLACE-715: pan untouched -- the place survives the take (0 reads as centre; a store here cost the streamer a spill, 206 > 205) */
							_xe->rsv            = (uint8_t)(0x80u |
							                      (t->p16m_next & 1u));
						}
					}
					g_meta_save_req = 1;             /* persist the new recording */
					/* SEAMX-727: the 2.x flash-side seam fade + head ramp lived here under
					 * #if SP1_CODEC == SP1_CODEC_PCM -- dead since the A7 build (W334); the
					 * seam is crossfaded by the recorder now (g_seam_head). */
					/* PRIME the play ring before publishing TS_PLAY: read ~half-ring of
					 * the loop into pring so a freshly-promoted track starts with read-
					 * ahead cushion instead of avail=0. Empty promotion made the last-
					 * recorded track starve -> silent until half-refill -> resume at the
					 * live playhead (a forward time-skip) = the 'last track clock wrong'.
					 * Runs on the streamer thread while the ring is still private (state
					 * != PLAY) so it can't race the audio read; the other rings hold
					 * ~341 ms, so this one-time ~20 ms prime burst can't starve them. */
					/* Block-align the prime start to a SAMP_PER_BLK boundary.
					 * MUST be DIVISION-based (not & ~(SAMP_PER_BLK-1)): for ADPCM
					 * SAMP_PER_BLK=1016 is NOT a power of two, so the bitmask
					 * would corrupt the address. Division is exact for every codec
					 * (256/512/1016) and identical to the mask for power-of-two. */
					uint32_t _tl_pt = DWT->CYCCNT;   /* TLT-649 */
					uint32_t _pw_snap = t->p_w;   /* detect restart/reset mid-prime */
					uint32_t _pw   = (g_consume_pos / TSPB(t)) * TSPB(t);
					uint32_t _gb   = t->len_blocks ? t->len_blocks
					               : (TLOOPB(i) ? TLOOPB(i) : 1u);
					uint32_t _cyc, _win, _wb, _wper;   /* CHOPCORE-805: the prime fills from the SAME tile PASS 2 serves */
					chop_tile(_gb, TSPB(t), &_wper, &_win, &_wb, &_cyc);
					uint32_t _want = (RING_SAMPLES / 2u) + 16u * TSPB(t);
					if (_want > RING_SAMPLES) _want = (RING_SAMPLES - WOB_RING_RSV) - TSPB(t);
					if (g_win_rev)
						_want = 0;   /* M16: reversed window — skip the
						              * forward prime; the starve fade-in
						              * covers the first fill (heads rule) */
#if SP1_CODEC == SP1_CODEC_PCM
					/* M22-B PLAIN PATH: no chop, no free window, not a
					 * head — the loop wraps at its SAMPLE length. The
					 * block machinery below still chooses what to read;
					 * this path only decides how much of the final
					 * block belongs to the lap. */
					bool _plain = (_cdiv == 1u && !g_win_free &&
						       !g_win_rev &&
						       !g_head_rev[i] &&   /* M50a / REV2-641: a reversed track must not take the express lane */
						       !head_active(i) && t->len_samps &&
						       _win == _wper && _wper == _gb);
#else
					bool _plain = false;
#endif
					for (uint32_t _got = 0; _got < _want; ) {
#if SP1_CODEC == SP1_CODEC_PCM
						if (_plain) {
							uint32_t _Ls  = t->len_samps;
							uint32_t _lp  = ((_pw % _Ls) + _Ls -
							              nudge_anchor(i, t->start_samps % _Ls, _Ls, 1u)) % _Ls;   /* NUDGE-717 */
							uint32_t _off = _lp % TSPB(t);
							uint32_t _lb2 = _lp / TSPB(t);
							uint32_t _n2  = 32u;
							{  /* M63b-r2: the prime loop may run MORE THAN ONCE
							    * when blocks/iteration < _want (true at 280
							    * frames/block, false at 256) — without a
							    * CUMULATIVE room clip the second pass wraps the
							    * ring and overwrites the first. Same clip PASS2
							    * has always had. */
							   int32_t _rm = (int32_t)((RING_SAMPLES - WOB_RING_RSV) - TSPB(t))
							               - (int32_t)_got;
							   uint32_t _rb = _rm > 0 ? (uint32_t)_rm / TSPB(t) : 0u;
							   if (_n2 > _rb) { _n2 = _rb; g_prime_ovf++; }
							   if (_n2 > BATCH_RD_BLKS) _n2 = BATCH_RD_BLKS;   /* R2C-774 */
							   if (!_n2) break;
							}
							{	/* clip to the lap end (whole blocks;
								 * the decode below trims the tail) */
								uint32_t _lapb = ((_Ls - 1u) / TSPB(t)) + 1u;
								if (_lb2 + _n2 > _lapb) _n2 = _lapb - _lb2;
							}
							uint32_t _ct = t->content_blocks ? t->content_blocks
							                                 : ((_Ls - 1u) / TSPB(t)) + 1u;
							bool _ps = (_lb2 >= _ct);
							if (!_ps && _lb2 + _n2 > _ct) _n2 = _ct - _lb2;
							if (!_n2) _n2 = 1u, _ps = true;
							if (_ps) {
								memset(batchbuf, 0, (size_t)_n2 * EMMC_BLOCK_SIZE);
							} else if (!emmc_read_blocks_fast(trk_blk(slot, (uint32_t)i) + _lb2, batchbuf, _n2)) { /* M71 */
								break;
							}
							uint32_t _ds = _n2 * TSPB(t) - _off;
							if (_ds > _Ls - _lp) _ds = _Ls - _lp;
							p14s_unpack_part(t->pring, RING_MASK,
									  _pw & RING_MASK,
									  batchbuf, _off, _ds, i);
							_pw  += _ds;
							_got += _ds;
							continue;
						}
#endif
						uint32_t _pwb = _pw / TSPB(t);
						uint32_t _c   = chop_phase(i, t->start_blk, _pwb, _gb, _cyc, TSPB(t));   /* NUDGE-717 / CHOPANCHOR-805 */
						uint32_t _lb  = (_c / _win) * _wper + _wb + (_c % _win);
						uint32_t _n   = 32u;
						if (_n > (RING_SAMPLES / TSPB(t)) - 1u) _n = (RING_SAMPLES / TSPB(t)) - 1u;
						if (_n > BATCH_RD_BLKS) _n = BATCH_RD_BLKS;   /* R2C-774 */
						{  /* M63b-r2: cumulative room clip (see the plain path) */
						   int32_t _rm = (int32_t)((RING_SAMPLES - WOB_RING_RSV) - TSPB(t))
						               - (int32_t)_got;
						   uint32_t _rb = _rm > 0 ? (uint32_t)_rm / TSPB(t) : 0u;
						   if (_n > _rb) { _n = _rb; g_prime_ovf++; }
						   if (!_n) break;
						}
						{
							uint32_t _we = (_c / _win) * _wper + _wb + _win;
							if (_lb + _n > _we) _n = _we - _lb;
						}
						/* SILENCE PAD (see PASS 2): [content, _gb) is synthesised
						 * zeros, never read from flash. */
						uint32_t _content = t->content_blocks ? t->content_blocks : _gb;
						bool _psil = (_lb >= _content);
						if (!_psil && _lb + _n > _content) _n = _content - _lb;
						if (_psil) {
							memset(batchbuf, 0, (size_t)_n * EMMC_BLOCK_SIZE);
						} else if (!emmc_read_blocks_fast(trk_blk(slot, (uint32_t)i) + _lb, batchbuf, _n)) { /* M71 */
							break;
						}
						/* DECODE the prime burst (_n blocks) into the play ring. */
						uint32_t _ntot = _n * TSPB(t);
						p14s_unpack(t->pring, RING_MASK, _pw & RING_MASK,
						             batchbuf, _n, i, TSPB(t));   /* HG-646: own bytes, own pad */
						_pw  += _ntot;
						_got += _ntot;
					}
					if (t->p_w == _pw_snap)
						t->p_w = _pw;   /* publish: ring now has ~170 ms cushion */
					/* else a restart/song-switch reset p_w mid-prime: keep the
					 * reset value (int16 ring zeros = silence); PASS 2 refills
					 * from the new playhead. */
					g_tl_prime_us = (uint32_t)(DWT->CYCCNT - _tl_pt) / 64u;   /* TLT-649 */
					t->state = TS_PLAY;    /* publish AFTER priming -> no entry starve */
					work = true;
				}
			}
		}

		if (_p1spr) k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(5));   /* SPRINT-P1-631: PASS 2 decides its own */
		/* PASS 2 — play read-ahead, only after all pending writes are flushed.
		 * Skip refills entirely while a big rec backlog exists so the recorder
		 * always wins the bus (the play rings hold ~1.2 s and can coast). */
		/* PASS 2 — ONE SWEEP PER PASS, ROTATING START, ONE CHUNK PER TRACK.
		 * Every priority heuristic tried here (emptiest-first, audible-first
		 * + starved-last, mid-round yields on rec backlog or read failure)
		 * produced the same measured pathology from a different corner: the
		 * track that sorted LAST got locked out entirely whenever the round
		 * kept terminating early, and one track would sit at ZERO delivered
		 * blocks for whole takes while its siblings stayed fat. Demand is
		 * ~750 blk/s of a ~1300 blk/s bus — there is no capacity problem,
		 * only fairness. So: serve every playing track AT MOST one chunk
		 * per sweep, starting from a rotating index so early-abort cost is
		 * shared; PASS 1 (writes) runs between sweeps EVERY pass, i.e. at
		 * least once per ~4 chunks (~15 ms) BY CONSTRUCTION, which bounds
		 * the rec backlog far below danger without any mid-sweep yield.
		 * Only the true 7/8 rec-ring emergency may abort a sweep. */
		{
			/* ROUNDS: repeat the fair sweep until every ring is topped up —
			 * one pass can deliver MANY chunks (amortizing the pass's fixed
			 * cost, which matters because the audio thread owns most of the
			 * CPU: one-chunk-per-pass measured out at only ~18 passes/s,
			 * pinning refill throughput to exactly consumption with zero
			 * surplus to rebuild margins). Fairness is per ROUND, so no
			 * track can be locked out; writes stay bounded because a round
			 * breaks out the moment a whole write page is waiting. */
			/* M71r4 SPRINT: the async benches earned 1,689-3,024 blk/s at
			 * prio 1; at PREEMPT(5) the storm load (~71% above us) parks
			 * this loop between arms (r2/r3 measured). Boost the WHOLE
			 * rounds loop while the audio thread says a ring is low; the
			 * 64-pass 0.5 ms breather below still protects main/WDT. */
			/* M63b-r4 DUTY CYCLE (M46d semantics restored): main runs at
			 * PREEMPT(1) too and there is no timeslicing, so an
			 * unbounded boost starves buttons/LEDs/the WDT feed. Sprint
			 * at most SPRINT_ON_MS, then hand the level back for
			 * SPRINT_OFF_MS. */
			static int64_t _spr_t0;
			int _m71spr = 0;
			if (g_emmc_sprint) {
				_m71spr = 1; _spr_t0 = k_uptime_get();
				k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(1));
			}
			static uint32_t rr;
			bool more = true;
			while (more && g_slot == slot) {
				more = false;
			rr = (rr + 1u) & 3u; g_m71_rd++;   /* M71r2 rounds/s */
			if (!_m71spr && g_emmc_sprint) {   /* went low mid-loop */
				_m71spr = 1; _spr_t0 = k_uptime_get();
				k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(1));
			} else if (_m71spr &&
			           k_uptime_get() - _spr_t0 >= 150) {
				/* M63b-r4: 150 ms sprinted -> give main the level back
				 * for 15 ms, then resume if still low (M46d shape). */
				k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(5));
				_m71spr = 0;
				rrt_ev(RRT_S, 15u, 15000u);   /* RRT-630 */
				k_msleep(15);
				if (g_emmc_sprint) {
					_m71spr = 1; _spr_t0 = k_uptime_get();
					k_thread_priority_set(k_current_get(),
					                      K_PRIO_PREEMPT(1));
				}
			}
			cpos = g_consume_pos;    /* fresh playhead for this round */
			for (int k = 0; k < NTRK; k++) {
				int i = (int)((rr + (uint32_t)k) & 3u);
				if (g_slot != slot) break;
				struct looptrk *t = &trk[i];
				if (t->state != TS_PLAY && !head_active(i)) continue;
				/* HG-646 (W185): the geometry of the bytes this ring plays -- the
				 * SOURCE's under a head. Every block<->position step below uses
				 * it; TSPB(t) is the ring's own track, wrong for a head. */
				const uint32_t _spb = TSPB_SRC(i);
				/* ===== M29: do not read what nobody can hear =================
				 * A muted track is still TS_PLAY, so its ring was filled at full
				 * rate from flash and the mixer discarded every sample. During a
				 * take that waste is the difference between over-budget and 84%
				 * of the bus. Scoped to takes only; heads sources never skipped. */
				if (g_rec_track >= 0 && t->muted && !head_active(i)) continue;
				int32_t avail = (int32_t)(t->p_w - cpos);
				/* DEAD-HISTORY SNAP: a frontier BEHIND the playhead is pure
				 * waste — the mixer reads exactly pring[cpos], so every
				 * sample in [p_w, cpos) can never be played, yet the old
				 * code ground through it sequentially. During an overdub
				 * the three playing tracks live just below zero (each
				 * write burst dips them), so nearly the WHOLE read budget
				 * went on never-played history, which is what actually cut
				 * the other tracks out while recording the 4th (measured
				 * live: margins oscillating 0..-350 ms for the entire
				 * take, full-rate reads, zero audible progress). Snap the
				 * frontier to the live playhead the moment it falls more
				 * than a block behind; loop_blk below is fully modular, so
				 * the loop phase is untouched — the track simply rejoins
				 * the transport where it is NOW, and every read from here
				 * on buys audible audio. */
				if (avail < -(int32_t)_spb ||
				    avail > (int32_t)RING_SAMPLES) {
					/* Test against the LIVE playhead, not the round's cpos
					 * snapshot: a restart/slot-switch during an earlier
					 * CMD18 in this round resets BOTH cpos and p_w to 0,
					 * and snapping against the stale snapshot would clobber
					 * that reset (p_w lands far AHEAD -> ring reads as
					 * pinned-full -> the mixer replays stale ring content).
					 * The upper bound is impossible in any healthy state
					 * (refill never runs more than one ring ahead), so it
					 * uniquely fingerprints such a clobber and self-heals
					 * it within one streamer pass. */
					uint32_t cnow = g_consume_pos;
					int32_t a2 = (int32_t)(t->p_w - cnow);
					if (a2 < -(int32_t)_spb ||
					    a2 > (int32_t)RING_SAMPLES) {
						uint32_t anchor = (cnow / _spb) * _spb;
						t->p_w = anchor;   /* audio thread sees starved either way */
						a2 = (int32_t)(anchor - cnow);
						g_p2snap[i]++;
					}
					avail = a2;
				}
				if (avail > (int32_t)(RING_SAMPLES - 8u * _spb))
					continue;          /* ring PINNED ~full (<=8 blocks of headroom):
					                    * the cushion is real at stall onset instead of
					                    * sawtoothing between half and full. 8 blocks
					                    * (not 4) so steady-state top-ups are >=7-block
					                    * bursts, not 3-block CMD18 spam. */
				/* SEGMENT: this track loops at ITS OWN length (a whole multiple
				 * of the base), not the shared g_loop_blocks. */
				/* M13: a HEAD sources track 1's loop instead of its own —
				 * geometry (length/start/content/region) comes from track
				 * 1; ring bookkeeping stays this track's. */
				struct looptrk *hsrc = head_active(i) ? &trk[g_head_src] : t;
				uint32_t gb = hsrc->len_blocks ? hsrc->len_blocks
					    : (g_loop_blocks ? g_loop_blocks : 1u);
				/* CHOP window (M7b, mode-aware). VARIABLE: slice this
				 * track's OWN length (M5 behavior). FIXED (base known,
				 * track a whole multiple of it): slice THE BAR — every
				 * layer plays the same base/div slice OF EACH OF ITS
				 * BARS, uniform and phase-locked, multi-bar variation
				 * preserved. div=1 reduces to the original math. */
				uint32_t cyc, win, wbase, wper;   /* CHOPCORE-805: ONE tile definition, shared with the prime, br_tile and the LEDs */
				chop_tile(gb, TSPB(hsrc), &wper, &win, &wbase, &cyc);
				/* BOUNDARY BUDGET: a chunk clipped by the loop wrap or the
				 * content/silence boundary used to consume this track's
				 * WHOLE turn in the round — so the only track with a
				 * mid-loop boundary (a fixed-mode silence tail) lost ~85 ms
				 * of refill every lap and was measurably the only one still
				 * starving (stv=[2 2 35 0] while its siblings sat at 2).
				 * The turn now keeps reading until its full 32-block quota
				 * has moved; a boundary merely splits it into 2-3 shorter
				 * bursts. Fairness is unchanged (same per-round quota). */
				bool round_abort = false;
				for (uint32_t budget = 32u; budget; ) {
					/* Snapshot the frontier: the (higher-priority) audio
					 * thread can reset p_w mid-eMMC-read on a song switch /
					 * restart. Fill from the snapshot, COMMIT only if
					 * unchanged. */
					uint32_t pw = t->p_w;
#if SP1_CODEC == SP1_CODEC_PCM
					/* M22-B PLAIN PATH (see the prime site): the loop
					 * wraps at its SAMPLE length. Mirrors the block
					 * path's room/content/race discipline exactly. */
					if (cdiv == 1u && !g_win_free && !head_active(i) &&
					    t->len_samps && win == wper && wper == gb &&
					    !g_win_rev &&
					    !g_head_rev[i]) {   /* M50a / REV2-641: reversed -> the block path */
						uint32_t Ls  = t->len_samps;
						uint32_t lp  = ((pw % Ls) + Ls -
						              nudge_anchor(i, t->start_samps % Ls, Ls, 1u)) % Ls;   /* NUDGE-717 */
						uint32_t off = lp % TSPB(t);
						uint32_t lb  = lp / TSPB(t);
						uint32_t n   = budget;
						if (n > (RING_SAMPLES / TSPB(t)) - 1u)
							n = (RING_SAMPLES / TSPB(t)) - 1u;
						if (n > BATCH_RD_BLKS) n = BATCH_RD_BLKS;   /* R2C-774 */
						{	/* fill to ~full, 1-block gap (as below) */
							int32_t av = (int32_t)(pw - cpos);
							int32_t room = (int32_t)((RING_SAMPLES - WOB_RING_RSV) - TSPB(t)) - av;
							uint32_t rb = room > 0 ? (uint32_t)room / TSPB(t) : 0u;
							if (n > rb) n = rb;
						}
						if (!n) break;
						{	/* clip to the lap end in whole blocks */
							uint32_t lapb = ((Ls - 1u) / TSPB(t)) + 1u;
							if (lb + n > lapb) n = lapb - lb;
						}
						uint32_t ct = t->content_blocks ? t->content_blocks
						                                : ((Ls - 1u) / TSPB(t)) + 1u;
						bool sil = (lb >= ct);
						if (!sil && lb + n > ct) n = ct - lb;
						if (!n) { n = 1u; sil = true; }
						bool rok;
						if (sil) { memset(batchbuf, 0, (size_t)n * EMMC_BLOCK_SIZE); rok = true; }
						else     { rok = emmc_read_blocks_fast(trk_blk(slot, (uint32_t)i) + lb, batchbuf, n); } /* M71 */
						if (!rok) {
							work = true;
							g_p2rfail++;
							bool rp = false;
							for (int j = 0; j < NTRK; j++) {
								uint8_t sj = trk[j].state;
								if (sj != TS_REC && sj != TS_DONE) continue;
								if ((trk[j].r_w - trk[j].r_r) >=
								    ((RRING_SAMPLES * 2u) - RRING_SAMPLES / 2u))   /* CD-463: 3/4 of 2x */
									rp = true;
							}
							if (rp) round_abort = true;
							break;
						}
						if (t->p_w != pw) { work = true; break; } /* reset raced us */
						uint32_t ds = n * TSPB(t) - off;
						if (ds > Ls - lp) ds = Ls - lp;
						p14s_unpack_part(t->pring, RING_MASK, pw & RING_MASK,
								  batchbuf, off, ds, i);
						t->p_w = pw + ds;
						g_p2blk[i] += n;
						work = true;
						more = true;
						budget -= n;
						continue;
					}
#endif
					/* phase-anchored loop position: (pw_block - start_blk)
					 * mod gb, safe when pw_block < start_blk (restart). */
					uint32_t pwb = pw / _spb;
					/* phase-anchored position along the audible chop
					 * cycle, tiled onto the region (variable mode:
					 * wper=gb, cyc=win -> identical to M5). */
					uint32_t hoff = heads_engaged()
						      ? (((uint32_t)g_head_pos[i] * cyc) >> 8) : 0u;
					uint32_t c = (chop_phase(head_active(i) ? (int)g_head_src : i, hsrc->start_blk,
								 pwb, gb, cyc, TSPB(hsrc)) + hoff) % cyc;   /* NUDGE-717 / CHOPANCHOR-805: the source's */
					/* M15 REVERSE: mirror the phase — consecutive ring
					 * blocks then walk the source BACKWARD, and each
					 * block's samples are flipped after decode below:
					 * together a continuous time-reversed stream. */
					bool hrev = (bool)g_head_rev[i] ^   /* REV2-641: per-track direction in ANY mode (W308) */
						    (bool)g_win_rev;
					if (hrev) c = (cyc - 1u) - c;
					uint32_t loop_blk = (c / win) * wper + wbase + (c % win);
					uint32_t brph = 0u, brw = 0u;
					if (g_br_live && !hrev && !head_active(i) && g_br_w[i]) {   /* BRWIN-754 / BRCHOP-800: the beat repeat = a window INSIDE the audible cycle; the chop's tile stays */
						brw = g_br_w[i];   /* BRCLAMP-804: ONE read -- a resize between two reads underflowed the clamp below */
						brph = (pwb + brw - (g_br_a[i] % brw)) % brw;   /* BRSTICK-763: the phase anchor (0 at the engage) */
						c = (g_br_b[i] + brph) % cyc;
						loop_blk = (c / win) * wper + wbase + (c % win);
					}
					uint32_t n = budget;
					if (n > (RING_SAMPLES / _spb) - 1u) n = (RING_SAMPLES / _spb) - 1u;
					if (n > BATCH_RD_BLKS) n = BATCH_RD_BLKS;   /* R2C-774 */
					/* VARIABLE TOP-UP: fill to ~full (keep a 1-block
					 * producer/consumer gap) so rings park at ~100%. */
					{
						int32_t av = (int32_t)(pw - cpos);
						int32_t _room = (int32_t)(RING_SAMPLES - _spb) - av;
						uint32_t _rb = _room > 0 ? (uint32_t)_room / _spb : 0u;
						if (n > _rb) n = _rb;
					}
					if (!n) break;
					{	/* contiguous run ends at this tile's window edge
						 * (M15-r2: a reversed head's run walks BACKWARD,
						 * so its edge is the tile START) */
						if (!hrev) {
							uint32_t wend = (c / win) * wper + wbase + win;
							if (loop_blk + n > wend) n = wend - loop_blk;
						} else {
							uint32_t wstart = (c / win) * wper + wbase;
							if (n > loop_blk - wstart + 1u)
								n = loop_blk - wstart + 1u;
						}
					}
					if (brw && n > brw - brph) n = brw - brph;   /* BRWIN-754 / BRCHOP-800 / BRCLAMP-804: the repeat's edge, from the SAME read (the tile edge is clipped above) */
					/* SILENCE PAD: the loop length can exceed the recorded
					 * content (fixed mode). [content, gb) was never written
					 * to flash — read it as synthesised zeros instead of
					 * stale flash data. NOTE: memset(0) is true silence ONLY
					 * for PCM. A compressed codec (u-law/ADPCM) would need
					 * its own encoded-silence bytes here, not zeros (u-law
					 * 0x00 decodes to a loud tone). */
					uint32_t content = hsrc->content_blocks ? hsrc->content_blocks : gb;
					bool _sil = (loop_blk >= content);
					if (!hrev) {
						if (!_sil && loop_blk + n > content)
							n = content - loop_blk;
					} else if (_sil && n > loop_blk - content + 1u) {
						/* backward silence run stays silence */
						n = loop_blk - content + 1u;
					}
					uint32_t blkno = trk_blk(slot, head_active(i)
					                              ? (uint32_t)g_head_src
					                              : (uint32_t)i)
						       + (hrev ? (loop_blk - n + 1u) : loop_blk);
					bool _rok;
					if (_sil) { memset(batchbuf, 0, (size_t)n * EMMC_BLOCK_SIZE); _rok = true; }
					else      {
						{	/* RB-475: bucket the burst BEFORE the read.
							 * Counters only -- n is not modified. */
							uint32_t _rbn = n;
							int _rbi = (_rbn <= 2u) ? 0 : (_rbn <= 4u) ? 1 :
							           (_rbn <= 8u) ? 2 : (_rbn <= 16u) ? 3 : 4;
							if (M73_CY_NOW()) {
								g_rb_n[_rbi]++;
								g_rb_blk += _rbn;
								g_rb_cnt++;
							}
						}
						_rok = emmc_read_blocks_fast(blkno, batchbuf, n);
					} /* M71 */
					if (!_rok) {
						work = true;       /* read failed: retry in a few ms */
						g_p2rfail++;
						/* Fast command-phase failures must not abort the
						 * whole round (that lockout was the measured
						 * cut-out mechanism); only genuine rec-ring
						 * pressure may. Otherwise skip this track — the
						 * next round retries a few ms later, after the
						 * card's busy window has passed. */
						bool _rec_press = false;
						for (int j = 0; j < NTRK; j++) {
							uint8_t sj = trk[j].state;
							if (sj != TS_REC && sj != TS_DONE) continue;
							if ((trk[j].r_w - trk[j].r_r) >=
							    ((RRING_SAMPLES * 2u) - RRING_SAMPLES / 2u))   /* CD-463: 3/4 of 2x */
								_rec_press = true;
						}
						if (_rec_press) round_abort = true;
						break;
					}
					if (t->p_w != pw) { work = true; break; } /* reset raced us */
					/* DECODE: packed flash bytes (n blocks just read) -> play
					 * ring (int16, wraps at RING_MASK, pw is block-aligned).
					 * PCM is memcpy-equivalent. */
					/* M63b: coded blocks cannot be byte-flipped — decode
					 * in reverse block order instead. */
					if (hrev)
						p14s_unpack_rev(t->pring, RING_MASK, pw & RING_MASK,
						                 batchbuf, n, i, _spb);   /* REV-638: i = continuity only; HG-646: the owner's pad */
					else
						p14s_unpack(t->pring, RING_MASK, pw & RING_MASK,
						             batchbuf, n, i, _spb);   /* HG-646: the owner's pad */
					t->p_w = pw + n * _spb;   /* HG-646: the source's stride */
					g_p2blk[i] += n;
					work = true;
					more = true;             /* served: worth another round */
					budget -= n;
				}
				if (round_abort) { more = false; break; }
				/* WRITE-PAGE BREAK: the recorder fills a whole 16-block
				 * page every ~85 ms; the moment one is ready, finish the
				 * round early so PASS 1 can write it — write latency is
				 * bounded to ~one chunk (~5 ms) without any of the old
				 * mid-round yield heuristics that locked tracks out. */
				bool page_ready = false;
				for (int j = 0; j < NTRK; j++) {
					uint8_t sj = trk[j].state;
					if (sj != TS_REC && sj != TS_DONE) continue;
					if ((trk[j].r_w - trk[j].r_r) >=
					    16u * TSPBI(j)) page_ready = true;
				}
				if (page_ready) {
					g_p2yield++;
					more = false;
					break;
				}
			}
			}
			if (_m71spr)
				k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(5));
		}
		if (!work) {
			/* IDLE WINDOW: drain the card's write cache in the background.
			 * emmc_cache_flush_try() was built for exactly this (abortable:
			 * the busy-abort hook fires an HPI the moment a take arms or a
			 * play ring drains toward half) but was NEVER WIRED IN — the
			 * cache only flushed at power-off, so it silently filled across
			 * a session and later takes paid internal-eviction busy on
			 * every write burst. That is the "gets worse and worse",
			 * worst-on-the-4th-track cut-out: the first takes write into
			 * an empty cache, the last ones fight the card's housekeeping
			 * for the bus. Keeping the cache drained between takes gives
			 * every take a fresh, absorbent cache. */
			bool quiet = (g_rec_track < 0) && !g_xfer_mode &&
				     g_hpi_on && g_emmc_ready && !g_emmc_quiesce &&
				     !g_meta_save_req && !g_cache_flush_req;
			if (quiet)
				for (int j = 0; j < NTRK; j++) {
					uint8_t sj = trk[j].state;
					if (sj == TS_ARMED || sj == TS_REC || sj == TS_DONE)
						quiet = false;
				}
			/* HK-613 (audit §2.3): only while the write cache is DIRTY, and at
			 * most once a second. The flush used to run every 50 ms of idle --
			 * CMD6 + busy + CMD13, ~1-1.7 ms -- against a cache nothing had
			 * written to. The take-fills-the-cache case it was built for is
			 * still covered: the first idle window after a write drains it. */
			static int64_t flush_last;
			int64_t nowms = k_uptime_get();
			if (quiet && emmc_cache_dirty() && nowms - flush_last >= 1000) {
				flush_last = nowms;
				(void)emmc_cache_flush_try();
			}
			k_msleep(2);
		} else {
			/* ANTI-STARVATION: the streamer at PREEMPT(5) outranks main(8),
			 * the WDT feeder. A long stretch of back-to-back work (or any
			 * future livelock in this loop) must NEVER be able to hold main
			 * off the CPU for the 4 s watchdog window — one 0.5 ms breather
			 * per 64 working passes costs <1% and guarantees it. */
			static uint32_t workpass;
			if ((++workpass & 0x3Fu) == 0u)
				k_usleep(500);
		}
	}
}

/* ========================================================================
 *  MIDI  —  timer-driven 24-PPQN clock + Start/Stop out over the SYNC jack.
 *  A free hardware timer clocks the UART bits one per ISR with interrupts
 *  left ON, so it never masks the eMMC/I2S ISRs (the fix for the >3-track
 *  crackle the old irq-locked bit-bang caused).
 * ======================================================================== */
/* ---- MIDI clock + Pocket-Operator sync out over the SYNC jack --------------
 * Pins from TimK's sync-jack schematic:
 *   MIDI  : BC807_BASE = P0.23 -> a PNP transistor that drives SYNC_RING. The
 *           PNP INVERTS: P0.23 LOW -> ring HIGH (MIDI idle/mark), P0.23 HIGH ->
 *           ring LOW (start bit/space). So we bit-bang the MIDI waveform, and
 *           midi_line() flips it for the transistor (set MIDI_INVERT 0 to undo
 *           if a receiver sees it inverted).
 *   PO sync: PO_A = P0.20 -> SYNC_TIP. A short pulse per 1/8 note (2 PPQN),
 *           the Korg/Volca/Pocket-Operator convention.
 * MIDI is 31250 baud, 8N1 = 32 us/bit. Each byte is sent with interrupts locked
 * so its 10 bits keep accurate spacing (~320 us, well within one I2S block of
 * DMA cushion). Driven from the low-priority midi_thread off the engine's
 * 24-PPQN clock counter — no UART peripheral needed.
 *
 * NOTE: untested on real gear yet — verify on a MIDI/PO device; if MIDI is
 * silent/garbled, try flipping MIDI_INVERT. */
#define MIDI_PIN      23u    /* P0.23 BC807_BASE -> SYNC_RING (MIDI)          */
#define POSYNC_PIN    20u    /* P0.20 PO_A       -> SYNC_TIP  (PO/Volca sync) */
#define POSYNC_PIN_B  17u    /* P0.17 PO_B       -> SYNC_TIP (paralleled)     */
#define MIDI_INVERT   1      /* PNP stage inverts; 1 = compensate             */
#define MIDI_BIT_US   32u    /* 31250 baud                                    */
#define PO_PULSE_MS   5      /* sync pulse width                              */
#define PO_DIV        12u    /* 24-PPQN clock / 12 = 2 PPQN (1/8-note pulses) */
/* MIDI/PO SYNC OUT — ENABLED, streaming-safe. The OLD bit-bang held irq_lock()
 * ~320us per byte (10 bits x 32us), masking the eMMC SPIM + I2S DMA ISRs ~32x/s
 * while playing -> stole the streamer's worst-case margin = the >3-track crackle
 * (v1/v2 had no MIDI thread). NOW the 10 UART bits are clocked out by a hardware
 * TIMER, one bit per tiny (~0.5us) ISR, with interrupts LEFT ON the whole time,
 * so the streamer is never starved. The PNP inverts the line, which a hardware
 * UARTE cannot compensate for -- the timer's ISR drives the bit via midi_line()
 * which applies MIDI_INVERT, so the timing is hardware-accurate AND the polarity
 * is right. Set to 0 to compile MIDI out entirely. */
/* MIDI is ON here (timer-driven) alongside the segment looper. Set to 0 to
 * compile the MIDI clock/Start-Stop output out entirely (the line stays idle). */
#define MIDI_SYNC_ENABLE 1

static K_THREAD_STACK_DEFINE(midi_stack, 512);  /* RD-474: was 768. 473 U4S measured 208 B peak -> 2.5x margin. */
static struct k_thread   midi_tcb;

static void midi_pins_init(void)
{
	NRF_P0->PIN_CNF[MIDI_PIN]   =
		(GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
		(GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos);
	NRF_P0->PIN_CNF[POSYNC_PIN] =
		(GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
		(GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos);
	NRF_P0->PIN_CNF[POSYNC_PIN_B] =
		(GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
		(GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos);
	NRF_P0->OUTCLR = (1u << POSYNC_PIN) | (1u << POSYNC_PIN_B);
	/* idle the MIDI line at MARK (ring high -> P0.23 low after inversion) */
	if (MIDI_INVERT) NRF_P0->OUTCLR = (1u << MIDI_PIN);
	else             NRF_P0->OUTSET = (1u << MIDI_PIN);
}

static inline void midi_line(int mark)   /* drive the MIDI line; mark=1 is idle/high */
{
	int p = MIDI_INVERT ? !mark : mark;
	if (p) NRF_P0->OUTSET = (1u << MIDI_PIN);
	else   NRF_P0->OUTCLR = (1u << MIDI_PIN);
}

/* Streaming-safe MIDI byte TX: a free hardware timer (TIMER2 — the board binds
 * no TIMER) clocks out the UART bits one per ISR. The start bit is driven when
 * the byte is queued; the timer then drives the 8 data bits (LSB first) + stop
 * bit at MIDI_BIT_US spacing. Interrupts stay ON throughout, so the eMMC/I2S
 * ISRs are never masked (the fix for the >3-track crackle). Only midi_thread
 * calls midi_send, sequentially, and MIDI bytes are >=31ms apart in practice,
 * so the single-byte-in-flight guard (midi_tx_done) never actually contends. */
#define MIDI_TIMER       NRF_TIMER2
#define MIDI_TIMER_IRQn  TIMER2_IRQn
static volatile uint16_t midi_tx_bits;     /* remaining frame, LSB = next bit out */
static volatile uint8_t  midi_tx_left;     /* bits still to clock (0 = done) */
static struct k_sem      midi_tx_done;     /* 1 = line free for the next byte */

static void midi_timer_isr(const void *arg)
{
	ARG_UNUSED(arg);
	MIDI_TIMER->EVENTS_COMPARE[0] = 0;
	(void)MIDI_TIMER->EVENTS_COMPARE[0];        /* flush the clear (nRF anomaly) */
	if (midi_tx_left) {
		midi_line(midi_tx_bits & 1u);       /* drive this bit (PNP-inverted) */
		midi_tx_bits >>= 1;
		midi_tx_left--;
	} else {
		MIDI_TIMER->TASKS_STOP = 1;
		midi_line(1);                       /* leave the line idle at mark */
		k_sem_give(&midi_tx_done);
	}
}

static void midi_timer_init(void)
{
	MIDI_TIMER->MODE      = TIMER_MODE_MODE_Timer;
	MIDI_TIMER->BITMODE   = TIMER_BITMODE_BITMODE_16Bit;
	MIDI_TIMER->PRESCALER = 4;                          /* 16MHz/16 = 1us tick */
	MIDI_TIMER->CC[0]     = MIDI_BIT_US;                /* fire every 32us = 1 bit */
	MIDI_TIMER->SHORTS    = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
	MIDI_TIMER->INTENSET  = TIMER_INTENSET_COMPARE0_Msk;
	k_sem_init(&midi_tx_done, 1, 1);                    /* start with the line free */
	IRQ_CONNECT(MIDI_TIMER_IRQn, 2, midi_timer_isr, NULL, 0);
	irq_enable(MIDI_TIMER_IRQn);
}

static void midi_send(uint8_t b)
{
	/* wait for any in-flight byte to finish (in practice it always has) */
	if (k_sem_take(&midi_tx_done, K_MSEC(5)) != 0) return;   /* stuck -> skip byte */
	/* The ENTIRE 10-bit frame is timer-clocked -- start(0), d0..d7 (LSB first),
	 * stop(1). The START bit is the timer's FIRST event, NOT driven here, so every
	 * edge is timer-paced; a thread preemption between here and TASKS_START can no
	 * longer stretch the start bit and corrupt the framing. */
	midi_tx_bits = ((uint16_t)b << 1) | (1u << 9);   /* bit0=start(0), d0..d7 @1..8, stop @9 */
	midi_tx_left = 10;                                /* start + 8 data + stop */
	midi_line(1);                                     /* hold idle/mark until the 1st ISR */
	MIDI_TIMER->TASKS_CLEAR = 1;
	MIDI_TIMER->TASKS_START = 1;                      /* 1st ISR (+32us) emits the START bit */
}

/* BASIC MIDI ONLY: just Start/Stop + 24-PPQN clock on the MIDI line. The
 * Pocket-Operator / Volca 2-PPQN sync (the POSYNC GPIO pulses + k_uptime polling)
 * has been removed to keep this thread minimal. */
/* HK-613: with CONFIG_SCHED_THREAD_USAGE=n the runtime-stats struct has no
 * execution_cycles member (kernel/thread.h). The two diag readers below go
 * through this macro and read 0 -- the CPU= line goes quiet, M80 res/all = 0.
 * The W4Y 1 ms census is the CPU instrument (473 onward). */
#if IS_ENABLED(CONFIG_SCHED_THREAD_USAGE)
#define HK_RS_CYC(rs) ((rs).execution_cycles)
#else
#define HK_RS_CYC(rs) ((void)(rs), 0ull)
#endif

static void midi_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t consumed = 0;
	while (1) {
		if (g_midi_start_pending) { g_midi_start_pending = 0; midi_send(0xFA); }
		if (g_midi_stop_pending)  { g_midi_stop_pending  = 0; midi_send(0xFC); }
		uint32_t prod = g_midi_clk_produced;
		if (consumed != prod) {
			if ((uint32_t)(prod - consumed) > 96u) {
				/* absurd backlog (>4 beats — a stall or a counter
				 * glitch): RESYNC instead of blasting the difference,
				 * because each clock byte locks IRQs ~320 us and a huge
				 * catch-up burst starves everything below PREEMPT(6). */
				consumed = prod;
			} else {
				consumed++;
				midi_send(0xF8);               /* MIDI clock, 24 PPQN */
			}
		} else {
			k_msleep(1);
		}
	}
}

/* GRIDCORE-733: THE GRID SERVICE. Once a block, after the mixer (never inside it, W291).
 *
 * A song with a loop: the tape is the clock.
 *   L = loop_len, P = consume_pos (64-bit shadow), n = beats in the loop, O = the "1"
 *   (+ the page-7 offset, in samples):  q = ((P - Oe) mod L) * n
 *   beat = q / L, bar = beat / 4, phase = (q mod L) / L, MIDI tick = q * 24 / L.
 * Published for the readers: g_grid_beat_frames = the wall beat at this block's speed
 * (L * 65536 / (n * s)), g_grid_anchor_e = now - (beat_in_bar * bf + phase_frames), in
 * modular uint64 so every "(now - anchor_e)" idiom reads the phase exactly. Nothing is
 * integrated block to block, so nothing can drift: the phase is recomputed from P.
 * The MIDI ticks are counted from q: exactly 24n a lap. With the tape stopped P is
 * frozen and the phase with it (the LEDs hold); the ticks then run on the wall clock
 * at the set speed's tempo (marc 09-10: keep ticking) and re-derive on PLAY.
 *
 * An empty song: the tapped clock (g_grid_anchor, nf) as before; ticks on the wall
 * scheduler (M22-A's exact index schedule, moved here from the mixer). */
static uint32_t grid_o_eff(uint32_t L, uint32_t n)
{
	/* the "1" plus the page-7 offset (+-1/2 beat, in samples), mod L */
	uint32_t o = (g_slot < NUM_SLOTS) ? g_grid_o[g_slot] : 0u;
	const int32_t q = (g_slot < NUM_SLOTS) ? (int32_t)g_grid_off_q8[g_slot] : 0;
	if (q && n) {
		const int64_t d = ((int64_t)q * (int64_t)(L / n)) >> 8;
		o = (uint32_t)(((int64_t)o + d + (int64_t)L) % (int64_t)L);
	}
	return (L && o >= L) ? o % L : o;
}
static void __attribute__((noinline)) grid_adopt_loop(uint32_t beats, uint32_t start_samps, uint32_t k)
{
	/* the first take stopped: the loop IS the grid from here. O = the take's start
	 * minus k beats (the punch line was beat k of its bar), n = the take's beats.
	 * Persisted (GRD3); bpm_q8 is rewritten as the loop's 1x tempo (the hint an
	 * old firmware or a migration reads). */
	if (g_slot >= NUM_SLOTS || !g_loop_len || !beats || beats > 255u) return;
	const uint32_t L = g_loop_len, B = (L + beats / 2u) / beats;
	const uint32_t back = (uint32_t)(((uint64_t)(k & 3u) * B) % L);
	g_grid_n[g_slot] = (uint8_t)beats;
	g_grid_o[g_slot] = (start_samps % L + L - back) % L;
	g_grid_bpm_q8[g_slot] = (uint16_t)((48000ULL * 60u * 256u * beats) / L);
	g_grid_active = 1;
	g_grid_save_req = 1;
}
static void __attribute__((noinline)) grid_follow_tape(void)
{
	static uint32_t g_gf_bf, g_gf_bf_L; static uint64_t g_gf_bf_ns;   /* CPU-780 O3: the beat-frames cache (function-local so the 24-h proof carries it) */
	const uint32_t L = g_loop_len;
	const uint32_t n = (g_slot < NUM_SLOTS) ? g_grid_n[g_slot] : 0u;
	uint32_t moved;
	{	/* the 64-bit shadow of the playhead: the 2^32 wrap (24.8 h) is a small
		 * forward step and disappears; a BACKWARD step is a reset (restart, first
		 * take, song switch -- all to 0) and the shadow restarts with it, so
		 * (shadow mod L) == (P mod L) always. */
		const uint32_t pl = g_consume_pos;
		const int32_t d = (int32_t)(pl - g_grid_p_lo);
		if (d >= 0) g_grid_p64 += (uint32_t)d; else g_grid_p64 = pl;
		moved = (pl != g_grid_p_lo) ? 1u : 0u;
		g_grid_p_lo = pl;
	}
	if (!g_grid_active) { g_grid_src = 0u; g_grid_tick_ok = 0u; g_grid_o_req = 0u; return; }
	const uint32_t s_now = (g_playing && g_loop_active && g_cur_speed_q16 >= 12288u)   /* RANGE-655: the 0.25x floor */
	                     ? g_cur_speed_q16 : g_play_speed_q16;
	if (L && n && s_now) {
		/* ---- the tape is the clock (stopped: P is frozen, so is the phase) ---- */
		if (g_grid_o_req && g_slot < NUM_SLOTS) {
			/* a tap said "the 1 is at wall frame W": convert with THIS block's
			 * coherent (W, P) -- the taps happened at the speed still in
			 * g_cur_speed_q16 (the retune is a ramp). */
			const uint64_t ago = g_sample_clock - g_grid_o_req_w;   /* frames since the tap */
			const uint32_t back = moved ? (uint32_t)(((ago * g_cur_speed_q16) >> 16) % L) : 0u;   /* a stopped tape: the 1 is where it resumes */
			g_grid_o[g_slot] = ((g_consume_pos % L) + L - back) % L;
			g_grid_o_req = 0u;
			g_grid_save_req = 1;
		}
		const uint32_t Oe = grid_o_eff(L, n);
		const uint32_t pl = (uint32_t)((g_grid_p64 + (uint64_t)L - Oe) % L);   /* (P - Oe) mod L */
		const uint64_t q = (uint64_t)pl * n;
		const uint32_t beat = (uint32_t)(q / L);
		const uint64_t frac = q - (uint64_t)beat * L;                   /* /L = the phase in the beat (CPU-780 O3: the remainder from the quotient, no second divide) */
		const uint64_t ns64 = (uint64_t)n * s_now;
		if (g_gf_bf_L != L || g_gf_bf_ns != ns64) {   /* CPU-780 O3: bf depends on (L, n, s_now) only -- one divide per change, not per block */
			g_gf_bf_L = L; g_gf_bf_ns = ns64;
			g_gf_bf = (uint32_t)(((uint64_t)L * 65536u + ns64 / 2u) / ns64);
		}
		const uint32_t bf = g_gf_bf;                                    /* wall frames a beat at this speed */
		const uint32_t phf = (uint32_t)((frac * bf) / L);                /* wall frames into the beat */
		g_grid_beat_frames = bf;
		g_grid_anchor_e = g_sample_clock - ((uint64_t)(beat & 3u) * bf + phf);   /* modular: (now - anchor_e) = the bar phase */
		g_gridrec_beat_samps = (L + n / 2u) / n;
		g_beat_samples = g_gridrec_beat_samps;
		g_midi_div = g_gridrec_beat_samps / 24u;
		/* MIDI: tick index from q, exactly 24n a lap; a jump (restart, song
		 * switch, regime change) re-bases silently -- no burst, the Start
		 * message carries the transport. Stopped: the wall scheduler below. */
		const uint32_t tk = (L < (1u << 27)) ? beat * 24u + ((uint32_t)frac * 24u) / L   /* CPU-780 O3: = floor(24q / L) exactly, 32-bit while L < 2^27 (62 min) */
		                                       : (uint32_t)((q * 24u) / L);
		const uint32_t tkn = 24u * n;
		g_grid_src = 1u;
		if (g_grid_tick_ok) {
			/* every tick the tape crossed since the last block, whatever the
			 * transport did in between (a stop freezes P: d = 0); a jump of more
			 * than a few ticks is a reset and re-bases silently */
			const uint32_t d = (tk + tkn - g_grid_tick_prev) % tkn;
			if (d <= 4u) {
				g_midi_clk_produced += d; g_grid_lap_acc += d;
				if (d && tk < g_grid_tick_prev) { g_grid_lap_tk = g_grid_lap_acc - (tk + 1u); g_grid_lap_acc = tk + 1u; }   /* diag: the lap just closed (ticks 0..tk are the new lap's) */
			} else {
				g_grid_lap_acc = tk + 1u;
			}
		}
		g_grid_tick_prev = tk; g_grid_tick_ok = 1u;
		if (moved || (g_playing && g_cur_speed_q16 >= 12288u)) {
			g_grid_next_tick = g_sample_clock;   /* the wall scheduler below re-bases here when the tape sits still */
			return;
		}
		goto wall_ticks;   /* the tape sits still: the clock keeps ticking at the set tempo (marc 09-10) */
	}
	/* ---- the tapped clock (an empty song, or a loop without a beat count) ---- */
	if (g_grid_src == 1u) {
		/* the loop went away under a tape grid (all tracks deleted): carry the
		 * phase into the clock so the metronome does not jump */
		g_grid_anchor = g_grid_anchor_e - (uint64_t)((((int64_t)((g_slot < NUM_SLOTS) ? g_grid_off_q8[g_slot] : 0)) * (int64_t)g_grid_beat_frames) >> 8);
		g_grid_tick_ok = 0u;
	}
	g_grid_o_req = 0u;
	g_grid_src = 2u;
	g_grid_anchor_e = grid_anchor_eff();   /* STACKT-716: once a block, for every reader */
	if (!g_grid_beat_frames) return;
wall_ticks:
	{	/* M22-A: EXACT tick schedule. beat/24 truncates (937.5 -> 937 at 128 BPM) and
		 * a += would accumulate that forever (~32 ms/min at 128 -- marc heard it);
		 * ticks come from an index against a fixed base, error bounded at +-1 frame.
		 * The base re-arms whenever anything else wrote next_tick (tap, load, the
		 * tape path above): the first tick of a fresh base fires AT the base. */
		const uint32_t bf = g_grid_beat_frames;
		if (g_grid_next_tick != g_grid_tick_base_sync) {
			g_grid_tick_base = g_grid_next_tick;
			g_grid_tick_base_sync = g_grid_next_tick;
			g_grid_tick_idx = 0;
		}
		while (g_sample_clock >= g_grid_next_tick) {
			g_grid_tick_idx++;
			g_grid_next_tick = g_grid_tick_base + (uint64_t)(((uint64_t)g_grid_tick_idx * bf) / 24u);
			g_grid_tick_base_sync = g_grid_next_tick;
			g_midi_clk_produced++;
		}
	}
}

/* POPTRAP-728 (W335): watch the FINISHED block for a discontinuity. Runs in
 * audio_thread after the mixer returns (never inside it, W291). One pass over
 * the 256 frames: the largest |s[n] - s[n-1]| on either channel, carried
 * across the block edge by g_pop_prev, and the block peak. An event counts
 * always; its context is latched only while nothing is waiting for the diag,
 * so the FIRST pop of a burst is the one described. */
/* Packed helpers: both channels of a frame in one 32-bit word (L low, R high).
 * On the M4 the SIMD instructions do both halves per instruction; the scalar
 * form is the host proof and the fallback. */
#if defined(__ARM_FEATURE_DSP)
static inline uint32_t pop_abs2(uint32_t d)
{
	uint32_t n = __QSUB16(0u, d);
	(void)__SSUB16(d, 0u);           /* GE per half: d >= 0 */
	return __SEL(d, n);
}
static inline uint32_t pop_max2(uint32_t a, uint32_t b)
{
	(void)__USUB16(a, b);            /* GE per half: a >= b (unsigned) */
	return __SEL(a, b);
}
#define pop_dif2(a, b) __QSUB16((a), (b))
static inline uint32_t pop_min2(uint32_t a, uint32_t b)
{
	(void)__USUB16(a, b);            /* GE per half: a >= b */
	return __SEL(b, a);
}
#define pop_usub2(a, b) __UQSUB16((a), (b))
#define pop_uadd2(a, b) __UQADD16((a), (b))
#else
static inline uint32_t pop_abs2(uint32_t d)
{
	int32_t lo = (int16_t)(d & 0xFFFFu), hi = (int16_t)(d >> 16);
	if (lo < 0) lo = -lo;
	if (hi < 0) hi = -hi;
	if (lo > 32767) lo = 32767;   /* QSUB16 saturates: |-32768| = 32767 */
	if (hi > 32767) hi = 32767;
	return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFFu);
}
static inline uint32_t pop_max2(uint32_t a, uint32_t b)
{
	uint32_t lo = (a & 0xFFFFu) > (b & 0xFFFFu) ? (a & 0xFFFFu) : (b & 0xFFFFu);
	uint32_t hi = (a >> 16) > (b >> 16) ? (a >> 16) : (b >> 16);
	return (hi << 16) | lo;
}
static inline uint32_t pop_min2(uint32_t a, uint32_t b)
{
	uint32_t lo = (a & 0xFFFFu) < (b & 0xFFFFu) ? (a & 0xFFFFu) : (b & 0xFFFFu);
	uint32_t hi = (a >> 16) < (b >> 16) ? (a >> 16) : (b >> 16);
	return (hi << 16) | lo;
}
static inline uint32_t pop_usub2(uint32_t a, uint32_t b)
{
	uint32_t lo = (a & 0xFFFFu) > (b & 0xFFFFu) ? (a & 0xFFFFu) - (b & 0xFFFFu) : 0u;
	uint32_t hi = (a >> 16) > (b >> 16) ? (a >> 16) - (b >> 16) : 0u;
	return (hi << 16) | lo;
}
static inline uint32_t pop_uadd2(uint32_t a, uint32_t b)
{
	uint32_t lo = (a & 0xFFFFu) + (b & 0xFFFFu); if (lo > 0xFFFFu) lo = 0xFFFFu;
	uint32_t hi = (a >> 16) + (b >> 16); if (hi > 0xFFFFu) hi = 0xFFFFu;
	return (hi << 16) | lo;
}
static inline uint32_t pop_dif2(uint32_t a, uint32_t b)
{
	int32_t lo = (int16_t)(a & 0xFFFFu) - (int16_t)(b & 0xFFFFu), hi = (int16_t)(a >> 16) - (int16_t)(b >> 16);
	if (lo > 32767) lo = 32767;
	if (lo < -32768) lo = -32768;
	if (hi > 32767) hi = 32767;
	if (hi < -32768) hi = -32768;
	return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFFu);
}
#endif
/* POPLOG-730: the machine state the trap can see from outside the mixer, packed. */
static void pop_snapshot(struct pop_snap *o)
{
	uint32_t w0 = (g_slot & 15u) | ((g_live_got ? 1u : 0u) << 4);
	uint32_t w1 = (g_pg_open ? 1u : 0u) | ((uint32_t)(g_pg_id & 15u) << 1) | ((g_heads_mode ? 1u : 0u) << 5)
	            | ((uint32_t)(g_head_src & 3u) << 6) | ((g_playing ? 1u : 0u) << 8) | ((g_bnc_on ? 1u : 0u) << 9)
	            | ((uint32_t)((g_rec_track + 1) & 7) << 10) | ((g_mon_mute ? 1u : 0u) << 13) | (((trk[0].starved | trk[1].starved | trk[2].starved | trk[3].starved) ? 1u : 0u) << 14)   /* POPTRAP2-734: any track starved (was RESYNC) */
	            | ((g_win_rev ? 1u : 0u) << 15) | ((((g_chop_div * 31u) + g_chop_off) & 0x3FFu) << 16);
	uint32_t sn = g_p2snap[0] + g_p2snap[1] + g_p2snap[2] + g_p2snap[3];
	for (int i = 0; i < NTRK; i++) {
		w0 |= ((uint32_t)(trk[i].state & 7u)) << (5 + 3 * i);      /* bits 5..16 */
		w0 |= ((uint32_t)(trk[i].muted ? 1u : 0u)) << (17 + i);     /* 17..20 */
		w0 |= ((uint32_t)(g_pg_route[i + 1] & 3u)) << (21 + 2 * i);  /* 21..28 */
		w1 |= ((uint32_t)(g_head_rev[i] ? 1u : 0u)) << (26 + i);     /* 26..29 */
		if (g_head_blip[i]) w1 |= 1u << 30;
		o->vol[i] = trk[i].vol_now;
	}
	w0 |= ((g_ec2_live ? 1u : 0u) << 29) | ((g_rv_live ? 1u : 0u) << 30);
	o->cpos = g_consume_pos; o->spd = g_cur_speed_q16; o->w0 = w0; o->w1 = w1; o->snap = sn;
}
/* tag bits: 0 CPOS jump  1 SLOT  2 LIVE edge  3 track STATE  4 MUTE  5 ROUTE  6 ECHO/REVERB owner
 * 7 REV  8 CHOP  9 PAGE  10 HEADS  11 SNAP  12 BLIP  13 PLAY  14 SPEED  15 VOL step  16 BOUNCE
 * 17 REC track  18 MONITOR mute  19 STARVE edge (POPTRAP2-734; was RESYNC) */
static uint32_t pop_tags(const struct pop_snap *a, const struct pop_snap *b)   /* what changed a -> b */
{
	uint32_t t = 0u;
	{
		const uint32_t exp = (a->w1 & (1u << 8)) ? (uint32_t)(((uint64_t)BLK_FRAMES * a->spd) >> 16) : 0u;
		int32_t d = (int32_t)(b->cpos - a->cpos) - (int32_t)exp;
		if (d > 8 || d < -8) t |= 1u << 0;
	}
	const uint32_t x0 = a->w0 ^ b->w0, x1 = a->w1 ^ b->w1;
	if (x0 & 15u) t |= 1u << 1;
	if (x0 & (1u << 4)) t |= 1u << 2;
	if (x0 & (0xFFFu << 5)) t |= 1u << 3;
	if (x0 & (15u << 17)) t |= 1u << 4;
	if (x0 & (0xFFu << 21)) t |= 1u << 5;
	if (x0 & (3u << 29)) t |= 1u << 6;
	if ((x1 & (15u << 26)) || (x1 & (1u << 15))) t |= 1u << 7;
	if (x1 & (0x3FFu << 16)) t |= 1u << 8;
	if (x1 & 31u) t |= 1u << 9;
	if (x1 & (7u << 5)) t |= 1u << 10;
	if (a->snap != b->snap) t |= 1u << 11;
	if (x1 & (1u << 30)) t |= 1u << 12;
	if (x1 & (1u << 8)) t |= 1u << 13;
	{ int32_t ds = (int32_t)(b->spd - a->spd); if (ds > 512 || ds < -512) t |= 1u << 14; }
	for (int i = 0; i < NTRK; i++) { int32_t dv = (int32_t)b->vol[i] - (int32_t)a->vol[i]; if (dv > 32 || dv < -32) t |= 1u << 15; }
	if (x1 & (1u << 9)) t |= 1u << 16;
	if (x1 & (7u << 10)) t |= 1u << 17;
	if (x1 & (1u << 13)) t |= 1u << 18;
	if (x1 & (1u << 14)) t |= 1u << 19;
	return t;
}
/* POPTRAP2-734: the click's position, found only on an event (cold). d[k] = |x[k]-x[k-1]|;
 * the click is the first k with d[k] >= TH and d[k] >= 4*max(d[k-1], d[k+1]); k = -1
 * (the block edge) reports as frame 0. */
static void pop_find_click(const int16_t *s, uint32_t prev0, uint32_t *at, uint8_t *ch)
{
	for (uint32_t c = 0; c < 2u; c++) {
		int32_t x_1 = (int16_t)((c ? (prev0 >> 16) : prev0) & 0xFFFFu);
		uint32_t dpp = (c ? (g_pop_prev_dpp >> 16) : g_pop_prev_dpp) & 0xFFFFu;
		uint32_t dp  = (c ? (g_pop_dp2 >> 16) : g_pop_dp2) & 0xFFFFu;
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			const int32_t x = s[2u * f + c];
			int32_t dd = x - x_1; if (dd < 0) dd = -dd; if (dd > 32767) dd = 32767;
			const uint32_t d = (uint32_t)dd, nb = (dpp > d) ? dpp : d;
			if (dp >= (uint32_t)POP_TH && dp >= (nb << POP_ISO_SHIFT)) { *at = f ? f - 1u : 0u; *ch = (uint8_t)c; return; }
			dpp = dp; dp = d; x_1 = x;
		}
	}
	*at = 0u; *ch = 0u;
}
static void __attribute__((optimize("O2"), noinline)) pop_trap(const int16_t *s, uint32_t cus)
{
	const uint32_t *w = (const uint32_t *)s;   /* BLK_FRAMES packed frames (the I2S block is 4-aligned) */
	uint32_t prev = ((uint32_t)(uint16_t)g_pop_prev[1] << 16) | (uint16_t)g_pop_prev[0];
	const uint32_t prev0 = prev;
	uint32_t jmp2 = 0u, pk2 = 0u, clk2 = 0u;
	uint32_t dpp = g_pop_prev_dpp, dp = g_pop_dp2;   /* POPTRAP2-734: d[f-2], d[f-1] carried across the block edge */
	const uint32_t th2 = ((uint32_t)(POP_TH - 1) << 16) | (uint32_t)(POP_TH - 1);   /* >= TH  <=>  d - (TH-1) > 0 */
	const uint32_t one2 = 0x00010001u;
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		const uint32_t cur = w[f];
		const uint32_t d = pop_abs2(pop_dif2(cur, prev));
		jmp2 = pop_max2(jmp2, d);   /* CPU-780 O1: the block peak left the hot loop (event branch below) */
		{	/* POPTRAP2-734: is d[f-1] an ISOLATED step? >= TH and >= 4x max(d[f-2], d[f]) */
			uint32_t nb = pop_max2(dpp, d);
			nb = pop_uadd2(nb, nb); nb = pop_uadd2(nb, nb);
			clk2 = pop_max2(clk2, pop_min2(pop_usub2(dp, th2), pop_usub2(dp, pop_usub2(nb, one2))));   /* dp >= TH && dp >= 4nb, both inclusive */
		}
		dpp = dp; dp = d;
		prev = cur;
	}
	const uint32_t dpp_out = dpp, dp_out = dp;   /* published at the end: the finder needs the values from before this block */
	g_pop_prev[0] = (int16_t)(prev & 0xFFFFu); g_pop_prev[1] = (int16_t)(prev >> 16);
	g_pop_blkn++;
	const uint32_t jl = jmp2 & 0xFFFFu, jh = jmp2 >> 16;
	const uint32_t jmp = (jl > jh) ? jl : jh;
	const uint32_t cl = clk2 & 0xFFFFu, ch2 = clk2 >> 16;
	const uint32_t clk = ((cl > ch2) ? cl : ch2) ? 1u : 0u;   /* POPTRAP2-734: a click somewhere in the block (or its first frame) */
	if (jmp >= (uint32_t)POP_TH) g_pop_n++;
	{	/* POPLOG-730: the state now; the last two blocks stay for the tags */
		struct pop_snap now; pop_snapshot(&now);
		if (clk) {   /* POPTRAP2-734: clicks only; POPSTICKY-788: a rolling ring of the last POPL_N */
			struct pop_ev *e = &g_pop_log[g_pop_logi];
			g_pop_logi = (uint8_t)((g_pop_logi + 1u) % POPL_N);
			if (g_pop_logn < POPL_N) g_pop_logn++;
			const uint32_t ms = k_uptime_get_32();
			uint32_t at = 0u; uint8_t ch = 0u;
			{	int32_t pl = (int16_t)(prev0 & 0xFFFFu), pr = (int16_t)(prev0 >> 16);
				pop_find_click(s, prev0, &at, &ch);   /* POPTRAP2-734 */
			}
			e->ms = ms; e->jmp = (uint16_t)jmp; e->at = (uint8_t)at; e->ch = ch;
			e->tags = pop_tags(&g_pop_ps[0], &now) | pop_tags(&g_pop_ps[1], &g_pop_ps[0]);
			e->ctl = g_pop_ctl;
			{ uint32_t age = ms - g_pop_ctl_ms; e->age = (uint8_t)(g_pop_ctl ? ((age > 250u) ? 250u : age) : 255u); }
		}
		g_pop_ps[1] = g_pop_ps[0]; g_pop_ps[0] = now;
	}
	if (!clk) { g_pop_prev_dpp = dpp_out; g_pop_dp2 = dp_out; return; }
	g_pop_c++;
	/* POPSTICKY-788: a new click overwrites the latched one (the print never clears it) */
	/* an event: find its first frame + channel (only now -- the hot loop above tracks no position) */
	{
		int32_t pl = (int16_t)(prev0 & 0xFFFFu), pr = (int16_t)(prev0 >> 16);
		uint32_t at = 0u; uint8_t ch = 0u;
		pop_find_click(s, prev0, &at, &ch);   /* POPTRAP2-734 */
		g_pop_at = (uint16_t)at; g_pop_ch = ch;
	}
	for (uint32_t f = 0; f < BLK_FRAMES; f++) pk2 = pop_max2(pk2, pop_abs2(w[f]));   /* CPU-780 O1: on an event only */
	const uint32_t pl2 = pk2 & 0xFFFFu, ph2 = pk2 >> 16;
	g_pop_ms = (uint32_t)k_uptime_get(); g_pop_cus = cus;
	g_pop_jmp = (int16_t)jmp; g_pop_pk = (int16_t)((pl2 > ph2) ? pl2 : ph2);
	{
		uint8_t sb = 0u;
		for (int i = 0; i < NTRK; i++)
			if (trk[i].starved) sb |= (uint8_t)(1u << i);
		g_pop_stv = sb;
	}
	g_pop_pl = g_playing ? 1u : 0u; g_pop_spd = g_cur_speed_q16;
	g_pop_usb = g_usb_streaming ? 1u : 0u; g_pop_got = g_live_got;
	g_pop_pend = 1u; g_pw_done = 0u;   /* POPSTICKY-788: the readback runs again for this event */
	g_pop_prev_dpp = dpp_out; g_pop_dp2 = dp_out;
}

/* POPWHO-779: WHO clicked? Runs once per latched event, after the trap, off the hot path. */
static uint16_t pop_who_ring(const int16_t *pr, uint32_t est)
{
	uint32_t worst = 0u;
	for (uint32_t k = 0u; k < 7u; k++) {   /* frames est-4 .. est+3: 7 steps */
		const uint32_t p0 = ((est - 4u + k) & RING_MASK) * 2u, p1 = ((est - 3u + k) & RING_MASK) * 2u;
		const int32_t dl = (int32_t)pr[p1] - pr[p0], dr = (int32_t)pr[p1 + 1u] - pr[p0 + 1u];
		const uint32_t al = (uint32_t)(dl < 0 ? -dl : dl), ar = (uint32_t)(dr < 0 ? -dr : dr);
		if (al > worst) worst = al;
		if (ar > worst) worst = ar;
	}
	return (uint16_t)worst;
}
static void __attribute__((noinline)) pop_who(void)
{
	const uint32_t at = g_pop_at;
	const uint32_t spd = (g_cur_speed_q16 >= 12288u) ? g_cur_speed_q16 : 65536u;
	const uint32_t back = (uint32_t)(((uint64_t)(BLK_FRAMES - at) * spd) >> 16);   /* loop samples from the click to the block's end */
	const uint32_t est = g_consume_pos - back;   /* the transport's read position at the click (a plain forward track) */
	uint8_t st = 0u;
	for (int i = 0; i < NTRK; i++) {
		g_pw_t[i] = pop_who_ring(trk[i].pring, est);
		const int32_t av = (int32_t)(trk[i].p_w - est);
		g_pw_av[i] = (uint16_t)((av < 0) ? 0 : (av > 65535) ? 65535 : av);
		g_pw_fd[i] = trk[i].fade;
		if (trk[i].starved) st |= (uint8_t)(1u << i);
		{	/* BLKOFF-784: where in the take's geometry the click frame sits (the live fill's block index is est / spb) */
			const uint32_t spb = TSPBI(i), lb = trk[i].len_blocks ? trk[i].len_blocks : 1u;
			g_pw_bo[i] = (uint16_t)(est % spb);
			g_pw_so[i] = (est / spb) % lb;
		}
	}
	g_pw_st = st;
	{	/* RUNLOG-785: the worst ring's shape, the run's ends, the writes into it */
		int wt = 0;
		for (int i = 1; i < NTRK; i++) if (g_pw_t[i] > g_pw_t[wt]) wt = i;
		g_rl_wt = (uint8_t)wt;
		const int16_t *pr = trk[wt].pring;
		for (uint32_t k = 0u; k < 24u; k++) g_rl_rv[k] = pr[((est - 8u + k) & RING_MASK) * 2u];   /* ALIEN-790: est-8 .. est+15 */
		g_rl_rl = 0u; g_rl_rls = 0u; g_rl_rb = 0u; g_rl_rbs = 0u;
		for (uint32_t d = 4u; d < 600u; d++) {
			const int32_t a = pr[((est + d - 1u) & RING_MASK) * 2u], b = pr[((est + d) & RING_MASK) * 2u];
			const int32_t dd = (b > a) ? b - a : a - b;
			if (dd >= 4000) { g_rl_rl = (uint16_t)d; g_rl_rls = (uint16_t)dd; break; }
		}
		for (uint32_t d = 5u; d < 600u; d++) {
			const int32_t a = pr[((est - d) & RING_MASK) * 2u], b = pr[((est - d + 1u) & RING_MASK) * 2u];
			const int32_t dd = (b > a) ? b - a : a - b;
			if (dd >= 4000) { g_rl_rb = (uint16_t)d; g_rl_rbs = (uint16_t)dd; break; }
		}
		for (uint32_t k = 0u; k < RL_N; k++)   /* newest first */
			g_rl_out[k] = g_rl_log[wt][(g_rl_idx[wt] + RL_N - 1u - k) % RL_N];
	}
	{	/* the jack block around the frame (what the monitor mixed) */
		uint32_t worst = 0u;
		const uint32_t f0 = (at > 4u) ? at - 4u : 0u;
		for (uint32_t f = f0; f + 1u < BLK_FRAMES && f < at + 3u; f++) {
			const int32_t dl = (int32_t)g_live_blk[2u * f + 2u] - g_live_blk[2u * f], dr = (int32_t)g_live_blk[2u * f + 3u] - g_live_blk[2u * f + 1u];
			const uint32_t al = (uint32_t)(dl < 0 ? -dl : dl), ar = (uint32_t)(dr < 0 ? -dr : dr);
			if (al > worst) worst = al;
			if (ar > worst) worst = ar;
		}
		g_pw_mon = (uint16_t)worst;
	}
	{	const uint32_t d = g_ec_dly; g_pw_ecd_step = (d > g_pw_ecd) ? d - g_pw_ecd : g_pw_ecd - d; }
}
static void audio_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	struct i2s_config cfg = {
		/* SLAVE on both clocks (TE native topology): the 3.072 MHz oscillator
		 * drives SCLK, the CS42L42 masters LRCK at exactly 48 kHz (64 SCLK per
		 * frame). We still send 16-bit samples — the nRF shifts the 16 MSBs of
		 * each 32-SCLK half-frame, which both codecs (set for MSB-first slots)
		 * decode correctly; the remaining LSBs are below the 16-bit noise floor. */
		.word_size      = 16,
		.channels       = 2,
		.format         = I2S_FMT_DATA_FORMAT_I2S,
		.options        = I2S_OPT_FRAME_CLK_SLAVE | I2S_OPT_BIT_CLK_SLAVE,
		.frame_clk_freq = I2S_SR,
		.mem_slab       = &tx_slab,
		.block_size     = BLK_BYTES,
		.timeout        = 2000,
	};

	if (!device_is_ready(i2s_dev)) { audio_cfg_rc = -100; return; }

	audio_cfg_rc = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (audio_cfg_rc != 0) return;

	/* Prime a few silent blocks, then START. After this the loop refills the
	 * DMA continuously with NO long gap, so the TX stream never underruns.
	 * The codec is configured separately on the main thread (it needs BCLK,
	 * which is live the moment we signal audio_started). */
	for (int i = 0; i < 4; i++) {
		void *blk;
		if (k_mem_slab_alloc(&tx_slab, &blk, K_FOREVER) != 0)
			continue;
		fill_block(blk);
		if (i2s_write(i2s_dev, blk, BLK_BYTES) != 0)
			k_mem_slab_free(&tx_slab, blk);
	}
	i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	audio_started = true;

	int wfail = 0;                       /* consecutive i2s_write failures */
	while (1) {
		void *blk;
		if (k_mem_slab_alloc(&tx_slab, &blk, K_FOREVER) != 0)
			continue;
		{	/* U3-471: peak tx_slab occupancy. 8 blocks are allocated (TXSLAB-773;
			 * 10,240 B before); the structural peak is 7 (queue 4 + DMA 2 + this one).
			 * This is the evidence the RAM audit demands before any
			 * cut -- the sizes are certain, "oversized" is a hypothesis. */
			uint32_t _u3u = (uint32_t)k_mem_slab_num_used_get(&tx_slab);
			if (_u3u > g_u3_tx_hi) g_u3_tx_hi = _u3u;
		}

		/* Looper engine: drains the live USB input (prebuffer-gated inside;
		 * silence if the host isn't streaming) and mixes the 4 tracks on top.
		 * DWT-timed: worst-case exec must stay far below the 5.33 ms block
		 * budget — aus= in the diag definitively exonerates (or convicts)
		 * the CPU path for the crackle. */
		uint32_t _c0 = DWT->CYCCNT;
		looper_audio_block(blk);
		uint32_t _cus = (DWT->CYCCNT - _c0) / 64u;   /* 64 MHz -> us */
		if (_cus > g_audio_us_max) g_audio_us_max = _cus;
		grid_follow_tape();   /* GRIDSPD-622 (W301): the tapped grid follows the tape speed */
		if (g_dmp_arm) pop_trap((const int16_t *)blk, _cus);   /* POPTRAP-728 (W335): the finished block, every block -- TRAPGATE-792: only once a capture has connected */
		if (g_pop_pend && !g_pw_done) { pop_who(); g_pw_done = 1u; }   /* POPWHO-779: once per latched event */
		g_pw_ecd = g_ec_dly;   /* POPWHO-779: the echo delay this block, for the next block's step */
		{	/* FXSTAT2-585: which ONE effect is up this block? Threshold 32
			 * skips the pickup-law zone while a fader is still being swept. */
			uint32_t _n = 0u, _c = 0u;
			#define _FXS(cond, id) do { if (cond) { _n++; _c = (id); } } while (0)
			_FXS(g_flt_pos >= 160u || g_flt_pos <= 96u, 1u);
			_FXS(g_chr_mix  >= 32u, 2u);  _FXS(g_dst_amt >= 32u, 3u);
			_FXS(g_gat_amt  >= 32u, 4u);  _FXS(g_bcr_amt >= 32u, 5u);
			_FXS(g_rng_amt  >= 32u, 6u);  _FXS(g_awh_amt >= 32u, 7u);
			_FXS(g_ec_mix   >= 32u, 8u);  _FXS(g_phs_amt >= 32u, 9u);
			_FXS(g_swp_amt  >= 32u, 10u); _FXS(g_trm_amt >= 32u, 11u);
			_FXS(g_tp_drive >= 32u, 12u); _FXS(g_tp_tone >= 160u || g_tp_tone <= 96u, 13u);
			_FXS(g_tp_hiss >= 160u || g_tp_hiss <= 96u, 14u); _FXS(g_tp_wob >= 32u, 15u);   /* HISS2-701: bipolar */
			_FXS(g_rv_mix   >= 32u, 16u);   /* REVERB-676 */
			_FXS(g_eq_g[0] >= 160u || g_eq_g[0] <= 96u || g_eq_g[1] >= 160u || g_eq_g[1] <= 96u ||
			     g_eq_g[2] >= 160u || g_eq_g[2] <= 96u || g_eq_g[3] >= 160u || g_eq_g[3] <= 96u, 17u);   /* EQ-691 */
			#undef _FXS
			uint32_t _k = (_n == 0u) ? 0u : (_n == 1u) ? _c : 18u;
			uint32_t _s = g_starve_cnt[0] + g_starve_cnt[1] + g_starve_cnt[2] + g_starve_cnt[3];
			g_fxs_blk[_k]++;
			if (_cus > g_fxs_aus[_k]) g_fxs_aus[_k] = _cus;   /* FXA */
			if (_cus > 5333u) g_fxs_over[_k]++;   /* POPS-700 FXO: an output click */
			g_fxs_stv[_k] += _s - g_fxs_prev;  g_fxs_prev = _s;
			if (g_playing)   /* GRIDFIX-626 (W303): a starved flag parked across a STOP is not a dropout */
				g_fxs_dry[_k] += (uint32_t)trk[0].starved + trk[1].starved + trk[2].starved + trk[3].starved;
		}

		int wrc = i2s_write(i2s_dev, blk, BLK_BYTES);
		if (wrc != 0) {
			g_i2s_wfail_cnt++;   /* diag: I2S path failure counter */
			k_mem_slab_free(&tx_slab, blk);
			/* FAILSAFE: if the I2S TX ever errors into the stopped state, every
			 * write fails forever and the device latches SILENT until reboot.
			 * After a burst of consecutive failures, drop + re-prime + restart
			 * the stream instead of staying mute. */
			if (++wfail >= 8) {
				wfail = 0;
				(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
				for (int i = 0; i < 4; i++) {
					void *pb;
					if (k_mem_slab_alloc(&tx_slab, &pb, K_NO_WAIT) != 0)
						break;
					fill_block(pb);
					if (i2s_write(i2s_dev, pb, BLK_BYTES) != 0)
						k_mem_slab_free(&tx_slab, pb);
				}
				(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
			}
			continue;
		}
		wfail = 0;
	}
}

/* Bring up the audio output path: osc on, codec configured, stream started. */
static void audio_init(void)
{
	/* The 3.072 MHz oscillator IS the bus bit-clock in TE's topology — turn it
	 * ON. (The old crackle when enabling it came from the nRF ALSO mastering
	 * SCLK = two drivers on one line; the nRF is a clock slave now.) */
	gpio_drive_high(OSC_EN_PORT, OSC_EN_PIN);
	k_msleep(5);
	k_thread_create(&audio_tcb, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack),
			audio_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	/* PREEMPT(0), not COOP(7): still outranks every other app thread (main 8,
	 * streamer 5, MIDI 6 — none can preempt it), but the COOP USB/UDC stack
	 * threads can now interrupt the mixer for their ~100 us ISO service.
	 * MEASURED on hardware: with the mixer non-preemptible, the USB
	 * controller lost ~600 incoming audio frames/s ONLY while recording
	 * (SOF heartbeat perfect, rx pool untouched) — silence stitched into
	 * every take = THE 4-track crackle. Shared state with the USB threads is
	 * one SPSC ring buffer and one mem-slab, both preemption-safe. */

	/* eMMC streamer: preemptible + below the audio thread so audio always
	 * wins. Guarded: in the charge-standby path it was already started
	 * early so the gauge can read the saved brightness (v1.2.3). */
	streamer_start();

	/* MIDI clock + PO sync out over the SYNC jack (TimK pins). The MIDI byte TX
	 * is now clocked by a hardware timer (midi_timer_init) so it no longer masks
	 * interrupts -- the >3-track crackle fix. Thread is low priority; it just
	 * flags bytes + drives the PO-sync GPIO pulse. */
#if MIDI_SYNC_ENABLE
	midi_pins_init();
	midi_timer_init();
	k_thread_create(&midi_tcb, midi_stack, K_THREAD_STACK_SIZEOF(midi_stack),
			midi_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
#endif

	/* Wait until the audio thread has the I2S stream running (BCLK live), then
	 * configure the codec here on the main thread. The audio thread keeps the
	 * DMA fed throughout, so its config sleeps never starve the I2S. */
	for (int i = 0; i < 100 && !audio_started; i++)
		k_msleep(2);
	tas2505_configure();
}

/* ---- UAC2 explicit feedback: software regulator (v1) ------------------------
 * The host needs to know how fast the SP-1 actually consumes samples. The SP-1
 * I2S bus runs at exactly 48000 Hz (codec-mastered); reporting the nominal rate
 * would make the host over-deliver and overflow the ring. Nordic only ships a
 * hardware feedback measurement for the nRF5340 (it needs an I2S FRAMESTART
 * event the nRF52840 lacks), so we regulate in software, reporting a USB Q10.14
 * "samples per SOF" value (1.0 sample = 1<<14 in the low 24 bits).
 *
 * CRITICAL: the reported value must be SMOOTH. The raw ring fill carries a large
 * ~187 Hz sawtooth (audio_thread drains in 256-frame blocks) plus per-packet USB
 * jitter; feeding that straight into the feedback warbles the host's asynchronous
 * resampler (audible pitch wobble) and makes the buffer hunt (crackle). So we
 *   1) low-pass the fill with an EMA, and
 *   2) apply only a GENTLE proportional gain to the smoothed fill error.
 * No separate integrator: the ring level is ITSELF the integral of the rate
 * mismatch, so a proportional law already drives the steady-state RATE error to
 * zero; the earlier extra integrator made it a double integrator that hunted.
 * feedback_update() runs once per SOF (USB thread); feedback_cb() returns the
 * atomic snapshot. Tuning knobs: FB_KP (authority) and FILL_EMA_SHIFT (smoothing). */
#define FB_FRAC        14
/* I2S_TRUE_HZ (48000) is defined up top near I2S_SR. FB_TRUE is the Q10.14
 * "samples per USB SOF" we report back to the host so it delivers at the rate
 * the I2S bus actually consumes, keeping the ring balanced. */
#define FB_TRUE        ((uint32_t)(((uint64_t)I2S_TRUE_HZ << FB_FRAC) / 1000u))
/* Clamp window centered on the true rate — safety rails only, not hit in normal
 * operation. FB_SETPOINT is defined up by the ring buffer (shared with prebuffer). */
#define FB_MIN         (FB_TRUE - (1u << FB_FRAC))  /* ~43.4 samples/SOF */
#define FB_MAX         (FB_TRUE + (1u << FB_FRAC))  /* ~45.4 samples/SOF */
#define FILL_Q         8                      /* fixed-point bits for the fill EMA */
#define FILL_EMA_SHIFT 6                      /* EMA tau ~64 SOFs (~64 ms): kills the
						* ~187 Hz block-drain sawtooth, far below
						* audio. Raise to smooth more. */
#define FB_KP          3                      /* gentle: fb-LSB per frame of smoothed err */

static atomic_t g_fb_value = ATOMIC_INIT(FB_TRUE);  /* Q10.14 snapshot for the host */
static int32_t  g_fill_avg;                         /* smoothed fill, frames << FILL_Q */
static volatile bool g_fb_running;

static void feedback_reset(void)
{
	g_fill_avg = 0;                       /* ring was just reset to empty */
	atomic_set(&g_fb_value, (atomic_val_t)FB_TRUE);
}

/* Called every USB SOF (USB thread) while the terminal is streaming. */
static void feedback_update(void)
{
	g_sof_cnt++;                        /* diag: SOF heartbeat (1000/s) */
	int frames = (int)(ring_buf_size_get(&usb_audio_ring) / USB_FRAME_BYTES);

	/* EMA low-pass of the fill (Q=FILL_Q fixed point) to strip the block-drain
	 * sawtooth before it can reach the host's resampler. */
	g_fill_avg += (((int32_t)frames << FILL_Q) - g_fill_avg) >> FILL_EMA_SHIFT;
	int err = (g_fill_avg >> FILL_Q) - FB_SETPOINT;   /* smoothed fill error (frames) */

	int32_t fb = (int32_t)FB_TRUE - err * FB_KP;      /* >0 err: ring full -> ask less */
	if (fb > (int32_t)FB_MAX) {
		fb = (int32_t)FB_MAX;
	} else if (fb < (int32_t)FB_MIN) {
		fb = (int32_t)FB_MIN;
	}

	atomic_set(&g_fb_value, (atomic_val_t)fb);
}

/* ---- UAC2 application callbacks --------------------------------------------
 * UDC-aligned pool the USB stack writes incoming audio into before handing it
 * to data_recv_cb. One SOF of FS audio is 48 frames; allow +1 for feedback
 * over-speed packets.
 * POOL DEPTH IS LOAD-BEARING: if uac2_get_recv_buf has no buffer for an
 * isochronous OUT interval, that packet is LOST FOREVER (ISO never retries) —
 * a 1 ms hole in the live input that gets RECORDED into a take. The audio
 * thread is COOP(7) and non-preemptible, so the COOP(8) USB threads can be
 * held off for several ms under recording load; 6 buffers (~6 ms) was NOT
 * enough — measured live: the input ring pinned at its floor with ~16 silence
 * frames padded into every block, 187x/s, for entire takes = THE crackle
 * (the eMMC was never the cause). 32 buffers = ~32 ms of cushion. */
#define UAC2_IN_TERMINAL_ID  UAC2_ENTITY_ID(DT_NODELABEL(in_terminal))
#define UAC2_MAX_PKT         ((48 + 1) * USB_FRAME_BYTES)
K_MEM_SLAB_DEFINE_STATIC(uac2_rx_slab, ROUND_UP(UAC2_MAX_PKT, UDC_BUF_GRANULARITY),
			 16, UDC_BUF_ALIGN);   /* R5-610 (553): was 32. Peak use measured 1 of 32
			                       * (rxlo=31, nb=0) every packet at the corner; 16
			                       * keeps 16x the observed peak. 3,136 B -> the echo line. */

static const struct device *const uac2_dev =
	DEVICE_DT_GET(DT_NODELABEL(uac2_speaker));

static void uac2_terminal_update_cb(const struct device *dev, uint8_t terminal,
				    bool enabled, bool microframes, void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(microframes); ARG_UNUSED(user_data);

	if (terminal != UAC2_IN_TERMINAL_ID) {
		return;
	}

	if (enabled) {
		/* Reset must be atomic vs the audio thread's ring_buf_get (reset is
		 * neither the producer nor the consumer role, so it is NOT safe against
		 * a concurrent get — a half-reset index pair can hand the consumer a
		 * block of garbage right at stream start). Briefly lock the scheduler. */
		k_sched_lock();
		ring_buf_reset(&usb_audio_ring);
		k_sched_unlock();
		feedback_reset();
		g_fb_running = true;
		g_usb_streaming = true;        /* audio_thread switches to the ring */
	} else {
		g_usb_streaming = false;       /* audio_thread falls back to silence/tone */
		g_fb_running = false;
	}
}

static void *uac2_get_recv_buf(const struct device *dev, uint8_t terminal,
			       uint16_t size, void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(user_data);
	void *buf = NULL;

	if (terminal == UAC2_IN_TERMINAL_ID && g_usb_streaming) {
		__ASSERT_NO_MSG(size <= UAC2_MAX_PKT);
		uint32_t _free = k_mem_slab_num_free_get(&uac2_rx_slab);
		if (_free < g_rx_slab_min) g_rx_slab_min = _free;
		if (_free < g_u3_rx_lo) g_u3_rx_lo = _free;   /* U3-471 cumulative */
		if (k_mem_slab_alloc(&uac2_rx_slab, &buf, K_NO_WAIT) != 0) {
			buf = NULL;            /* NO buffer for an ISO interval = the
			                        * packet is DROPPED (ISO never retries):
			                        * counted — this is the crackle source. */
			g_rx_nobuf++;
		}
	}

	return buf;
}

static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal,
			      void *buf, uint16_t size, void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(terminal); ARG_UNUSED(user_data);
	uint32_t _t80 = DWT->CYCCNT;

	if (g_usb_streaming && size) {
		g_usb_pkts++;                /* diag: ~1000/s expected while streaming */
		g_usb_frames += size / USB_FRAME_BYTES;
		/* Push the 16-bit stereo frames into the elastic ring. If the whole
		 * packet doesn't fit, drop the WHOLE packet (one clean 1 ms gap) rather
		 * than a partial put — with a feedback-deaf host the ring pegs full and
		 * per-packet shaving would otherwise crackle continuously. */
		if (ring_buf_space_get(&usb_audio_ring) >= size) {
			(void)ring_buf_put(&usb_audio_ring, (const uint8_t *)buf, size);
		} else {
			g_ring_overflows++;  /* ring full: host out-delivering the feedback */
		}
	}

	k_mem_slab_free(&uac2_rx_slab, buf);
	/* U1: the feedback regulator, relocated off the SOF path. Same
	 * 1 ms cadence (one ISO OUT completion per frame while streaming)
	 * on a wake-cascade that is already paid for. Runs AFTER the
	 * ring_buf_put above, so it sees the fill this frame produced --
	 * a fixed one-frame phase shift versus the old SOF timing, which
	 * the FILL_EMA_SHIFT=6 (~64 ms) filter renders irrelevant. */
	if (g_fb_running) {
		feedback_update();
	}
	g_t_cb += (uint32_t)(DWT->CYCCNT - _t80);
}

static void uac2_buf_release_cb(const struct device *dev, uint8_t terminal,
				void *buf, void *user_data)
{
	/* The SP-1 never sends audio to the host, so this is never called. */
	ARG_UNUSED(dev); ARG_UNUSED(terminal); ARG_UNUSED(buf); ARG_UNUSED(user_data);
}

static uint32_t uac2_feedback_cb(const struct device *dev, uint8_t terminal,
				 void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(terminal); ARG_UNUSED(user_data);
	return (uint32_t)atomic_get(&g_fb_value);
}

static void uac2_sof_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(user_data);
	uint32_t _t80 = DWT->CYCCNT;
	/* U1: feedback_update() moved to uac2_data_recv_cb(). With
	 * CONFIG_UDC_ENABLE_SOF=n this callback no longer fires at all;
	 * it is kept so the ops struct and the g_t_sof meter stay valid
	 * (g_t_sof going to zero is itself the confirmation that SOF is
	 * off). Set SP1_U1_SOF_OFF=0 and CONFIG_UDC_ENABLE_SOF=y to
	 * restore the old path. */
	g_t_sof += (uint32_t)(DWT->CYCCNT - _t80);
}

static struct uac2_ops sp1_uac2_ops = {
	.sof_cb             = uac2_sof_cb,
	.terminal_update_cb = uac2_terminal_update_cb,
	.get_recv_buf       = uac2_get_recv_buf,
	.data_recv_cb       = uac2_data_recv_cb,
	.buf_release_cb     = uac2_buf_release_cb,
	.feedback_cb        = uac2_feedback_cb,
};

/* Bring up the composite USB device (UAC2 audio + CDC console) on device_next.
 * set_ops MUST precede usbd_enable or the UAC2 class init fails. */
static void usb_audio_start(void)
{
	struct usbd_context *usbd;

	if (!device_is_ready(uac2_dev)) {
		printk("uac2 device not ready\n");
		return;
	}

	usbd_uac2_set_ops(uac2_dev, &sp1_uac2_ops, NULL);

	usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL) {
		printk("usbd init failed\n");
		return;
	}

	/* Pin bcdDevice to a new release number. Windows caches USB descriptors
	 * per VID/PID/version — without a version bump a PC that saw the old
	 * (Code-10) audio descriptor keeps judging a re-flashed SP-1 by the
	 * cached copy and can stay broken even after the fix. */
	(void)usbd_device_set_bcd_device(usbd, 0x0200);

	if (usbd_enable(usbd) != 0) {
		printk("usbd enable failed\n");
	}

#if SP1_XFER_ENABLE
	/* Register the CDC RX callback AND enable RX now. On this USB stack the
	 * CDC-ACM class only queues its FIRST receive transfer from
	 * uart_irq_rx_enable() — with it off the endpoint never accepts a single
	 * byte and the transfer site can never connect (GitHub issue #1). The ISR
	 * just moves bytes into a ring; while looping its cost is zero unless the
	 * host actually sends something. */
	uart_irq_callback_user_data_set(cdc, cdc_rx_isr, NULL);
	uart_irq_rx_enable(cdc);
#endif
}

/* Stream the raw ladder codes, but ONLY when a host has opened the port
 * (DTR asserted). That keeps us from ever stalling the watchdog loop when
 * nothing is listening. Throttled by the caller. */
/* =====================================================================
 * SEMITONE grid for the tempo rocker's DOUBLE-CLICK: 2^(k/12) in Q16 for
 * k = -24..+12 (0.25x..2.0x, RANGE-655; the BPM clamp bounds the usable range). A
 * double-click jumps the speed to the next exact equal-tempered semitone
 * relative to 1.0x (= 80 BPM) — one musical pitch step instead of forty
 * 1-BPM clicks — and a detuned speed SNAPS ONTO the grid rather than
 * drifting off it. Integer-only; the exact Q16 speed is what the song
 * saves, so semitone speeds survive power-off bit-exact. */
static const uint32_t k_semi_q16[37] = {   /* RANGE-655: k = -24..+12, 0.25x..2x */
	16384u,  17358u,  18390u,  19484u,  20643u,  21870u,  23170u,
	24548u,  26008u,  27554u,  29193u,  30929u,
	32768u,  34716u,  36781u,  38968u,  41285u,  43740u,  46341u,
	49097u,  52016u,  55109u,  58386u,  61858u,  65536u,  69433u,
	73562u,  77936u,  82570u,  87480u,  92682u,  98193u,  104032u,
	110218u, 116772u, 123715u, 131072u,
};

static uint32_t semitone_next(uint32_t sp, int dir)
{
	/* within ~0.4% of a grid point counts as ON it (absorbs BPM-integer
	 * rounding; far below the 5.9% semitone spacing) */
	if (dir > 0) {
		for (int k = 0; k < 37; k++)
			if (k_semi_q16[k] > sp + sp / 250u)
				return k_semi_q16[k];
		return k_semi_q16[36];
	}
	for (int k = 36; k >= 0; k--)
		if (k_semi_q16[k] < sp - sp / 250u)
			return k_semi_q16[k];
	return k_semi_q16[0];
}

static void controls_diag(void)
{
	{ /* BF: bug #116 + #117 evidence, one line.
	   *   cid=   codec id stamped into the v3 table (4 = SP1-ADPCM7)
	   *   flg=   v3 flags byte, bit0 = stereo content */
	  uint32_t _dbf = 0;
	  (void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &_dbf);
	  if (_dbf) {
		printk("BF,cid=%u,flg=%u\n",   /* SHWDEL-649: inv/stale gone with the shadow */
		       (unsigned)g_x3.t[g_slot][0].codec_id,
		       (unsigned)g_x3.t[g_slot][0].flags);
	  }
	}
	{ /* M80: USB path meters, CUMULATIVE ms (Protocol A applies).
	   * res = all-thread runtime minus our four threads = USB
	   * cluster + idle; only meaningful under load (M35: idle=0
	   * at the max corner). */
	  uint32_t _d80 = 0;
	  (void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &_d80);
	  if (_d80) {
		k_thread_runtime_stats_t _rs;
		uint64_t _all = 0, _kn = 0;
		if (!k_thread_runtime_stats_all_get(&_rs)) _all = HK_RS_CYC(_rs);
		if (!k_thread_runtime_stats_get(&audio_tcb, &_rs))    _kn += HK_RS_CYC(_rs);
		if (!k_thread_runtime_stats_get(&streamer_tcb, &_rs)) _kn += HK_RS_CYC(_rs);
		if (!k_thread_runtime_stats_get(&midi_tcb, &_rs))     _kn += HK_RS_CYC(_rs);
		if (!k_thread_runtime_stats_get(k_current_get(), &_rs)) _kn += HK_RS_CYC(_rs);
		/* M80-r2: cb/sof are DWT (64 MHz -> /64000 = ms). res/all are
		 * RUNTIME-STATS cycles -- the 32768 Hz RTC on this config, NOT
		 * DWT. r1 divided them by 64000 and printed 0 forever (time-base
		 * artifact #6). Print them RAW; the host divides by 32.768/ms. */
		{ /* M90: live snapshot, any state (artifact #9 fix) */
		  uint32_t _bg90 = 0;
		  for (int _i90 = 0; _i90 < NTRK; _i90++) {
			uint32_t _g90 = trk[_i90].r_w - trk[_i90].r_r;
			if (_g90 >= _bg90) { _bg90 = _g90;
			  { uint32_t _g4 = 0;
		  for (int _i4 = 0; _i4 < NTRK; _i4++) {
			uint32_t _d4 = trk[_i4].r_w - trk[_i4].r_r;
			if (_d4 > _g4) _g4 = _d4;
		  }
		  if (_g4 > g_gap_max) g_gap_max = _g4; }
		g_dbg_rw = trk[_i90].r_w; g_dbg_rr = trk[_i90].r_r;
			  g_dbg_rc = trk[_i90].rec_count; }
		  }
		}
		printk("M81,d=%u,a=%u,b=%u,c=%u,dm=%u,am=%u,bm=%u,cm=%u,r=%u,t=%u,tm=%u,rw=%u,rr=%u,rc=%u,em=%u\n",
		       (unsigned)(g_ph[0] / 64000u), (unsigned)(g_ph[1] / 64000u),
		       (unsigned)(g_ph[2] / 64000u), (unsigned)(g_ph[3] / 64000u),
		       (unsigned)g_phmax[0], (unsigned)g_phmax[1],
		       (unsigned)g_phmax[2], (unsigned)g_phmax[3],
		       0u, 0u,   /* PLACE-715: the M82 probes are compiled out */
		       0u, (unsigned)g_dbg_rw,
		       (unsigned)g_dbg_rr, (unsigned)g_dbg_rc,
		       (unsigned)g_enmin);
		printk("M8X,d=%u,a=%u,b=%u,c=%u,blk=%u\n",
		       (unsigned)(g_cph[0] / 64u), (unsigned)(g_cph[1] / 64u),
		       (unsigned)(g_cph[2] / 64u), (unsigned)(g_cph[3] / 64u),
		       (unsigned)g_cph_blk);
		printk("M8Y,d=%u,a=%u,b=%u,c=%u,blk=%u\n",
		       (unsigned)(g_dph[0] / 64u), (unsigned)(g_dph[1] / 64u),
		       (unsigned)(g_dph[2] / 64u), (unsigned)(g_dph[3] / 64u),
		       (unsigned)g_dph_blk);
		printk("M80,up=%u,cb=%u,sof=%u,res=%u,all=%u,pk=%u,sf=%u\n",
		       (unsigned)k_uptime_get_32(),
		       (unsigned)(g_t_cb / 64000u), (unsigned)(g_t_sof / 64000u),
		       (unsigned)(_all > _kn ? _all - _kn : 0u),
		       (unsigned)_all,
		       (unsigned)g_usb_pkts, (unsigned)g_sof_cnt);
	  }
	}
	{ /* M73: CUMULATIVE ms per streamer phase since boot.
	   * CUMULATIVE, not per-second deltas, so PROTOCOL A applies:
	   * run the corner with NO monitor, connect AFTERWARDS, and this
	   * one line carries the whole run. M71-r5 proved the diag
	   * printing is itself part of the storm load, so a per-second
	   * phase readout would have measured its own weight. */
	  uint32_t _d73 = 0;
	  (void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &_d73);
	  if (_d73) {
		g_dmp_arm = 1u;   /* DMP-466: capture connected -> dump may run */
		/* DWT->CYCCNT is the 64 MHz core counter, so 64000 cycles = 1 ms
		 * exactly -- no rounding constant to get wrong. */
		printk("M73,up=%u,rd=%u,dc=%u,en=%u,wr=%u,dcc=%u,"
		       "dcu=%u,dcp=%u,dcr=%u\n",
		       (unsigned)k_uptime_get_32(),
		       (unsigned)(g_t_rd / 64000u), (unsigned)(g_t_dc / 64000u),
		       (unsigned)(g_t_en / 64000u), (unsigned)(g_t_wr / 64000u),
		       (unsigned)g_dcc, (unsigned)g_dcu,
		       (unsigned)g_dcp, (unsigned)g_dcr);
		printk("M73CX,rd=%llu/%u,dc=%llu/%u,en=%llu/%u,wr=%llu/%u\n",
		       (unsigned long long)g_t_rd_cx, g_t_rd_cxn,
		       (unsigned long long)g_t_dc_cx, g_t_dc_cxn,
		       (unsigned long long)g_t_en_cx, g_t_en_cxn,
		       (unsigned long long)g_t_wr_cx, g_t_wr_cxn);
		printk("M73CY,rd=%llu/%u,dc=%llu/%u,en=%llu/%u,wr=%llu/%u\n",
		       (unsigned long long)g_t_rd_cy, g_t_rd_cyn,
		       (unsigned long long)g_t_dc_cy, g_t_dc_cyn,
		       (unsigned long long)g_t_en_cy, g_t_en_cyn,
		       (unsigned long long)g_t_wr_cy, g_t_wr_cyn);
		printk("RB,b1_2=%u,b3_4=%u,b5_8=%u,b9_16=%u,b17_32=%u,blk=%u,cnt=%u\n",
		       (unsigned)g_rb_n[0], (unsigned)g_rb_n[1], (unsigned)g_rb_n[2],
		       (unsigned)g_rb_n[3], (unsigned)g_rb_n[4],
		       (unsigned)g_rb_blk, (unsigned)g_rb_cnt);
		printk("M73CZ,pk=%llu/%u,ps=%llu/%u,blk=%u\n",
		       (unsigned long long)g_t_pk_cy, g_t_pk_cyn,
		       (unsigned long long)g_t_ps_cy, g_t_ps_cyn,
		       (unsigned)g_pk_blk);
		printk("EP,cyc=%llu,n=%u,blk=%u\n",
		       (unsigned long long)g_t_ep,
		       (unsigned)g_t_epn, (unsigned)g_ep_blk);



		printk("PF,v=5,pg=%u,id=%u,flt=%u,dst=%u,chr=%u,gat=%u,rate=%u,pat=%u,step=%u,gg=%u,vu=%u,fn=%u,bcr=%u,rng=%u,awh=%u,phs=%u,swp=%u,trm=%u,awe=%d,btpk=%u,btacc=%u,btn=%u,bnc=%u,bon=%u,tdr=%u,ttn=%u,ths=%u,twb=%u,wbj=%u,wbx=%u,ec=%u,ecdiv=%u,ecdly=%u,ecv=%u,ecw=%u,rt1=%u,rt2=%u,rt3=%u,rt4=%u,mm=%u,inb=%u,inw=%u\n",
		       (unsigned)g_pg_open, (unsigned)g_pg_id, (unsigned)g_flt_pos,
		       (unsigned)g_dst_amt, (unsigned)g_chr_mix, (unsigned)g_gat_amt,
		       (unsigned)g_gat_amt, (unsigned)g_gat_pat, (unsigned)g_tg_idx,
		       (unsigned)g_gat_g,
		       (unsigned)g_vu, (unsigned)g_fn_held,
		       (unsigned)g_bcr_amt, (unsigned)g_rng_amt,
		       (unsigned)g_awh_amt, (unsigned)g_phs_amt,
		       (unsigned)g_swp_amt, (unsigned)g_trm_amt,
		       (int)g_awh_env,
		       (unsigned)g_bt_pk, (unsigned)g_bt_acc,
		       (unsigned)g_bt_n,
		       (unsigned)g_bnc_prints, (unsigned)g_bnc_on,
		       (unsigned)g_tp_drive, (unsigned)g_tp_tone,
		       (unsigned)g_tp_hiss,  (unsigned)g_tp_wob,
		       (unsigned)g_wbj, (unsigned)g_wbx,
		       (unsigned)g_ec_mix, (unsigned)g_ec_div, (unsigned)g_ec_dly,
		       (unsigned)(((uint32_t)g_ec_mix * 166u) >> 8), (unsigned)g_ec2_w,   /* ECHO2-610: fb q8 + line index */
		       (unsigned)g_pg_route[1], (unsigned)g_pg_route[2], (unsigned)g_pg_route[3], (unsigned)g_pg_route[4],
		       (unsigned)g_mon_mute, (unsigned)g_in_blk, (unsigned)g_inw_blk);   /* INFX-672 */
		printk("GR,src=%u,n=%u,o=%u,L=%u,bf=%u,tk=%u,lap=%u\n",   /* GRIDCORE-733: 1 = the tape is the clock, 2 = the tapped clock; beats in the loop; the 1; loop_len; the wall beat now; tick index; ticks in the last lap (= 24n) */
		       (unsigned)g_grid_src, (unsigned)((g_slot < NUM_SLOTS) ? g_grid_n[g_slot] : 0u), (unsigned)((g_slot < NUM_SLOTS) ? g_grid_o[g_slot] : 0u),
		       (unsigned)g_loop_len, (unsigned)g_grid_beat_frames, (unsigned)g_grid_tick_prev, (unsigned)g_grid_lap_tk);
		printk("BK,p=%u,s=%u,l=%u,c=%u,b=%u,n=%u\n",   /* BAKE-619/TRUE-621: prints baked, last speed q16, last len (baked blocks), capped, blocks this print, loop chosen at the stop */
		       (unsigned)g_bk_prints, (unsigned)g_bk_last_spd, (unsigned)g_bk_len,
		       (unsigned)g_bk_capped, (unsigned)g_bk_blocks, (unsigned)g_bk_len);
		printk("P16,m=%u%u%u%u,n=%u%u%u%u\n",
		       (unsigned)trk[0].p16m, (unsigned)trk[1].p16m,
		       (unsigned)trk[2].p16m, (unsigned)trk[3].p16m,
		       (unsigned)trk[0].p16m_next, (unsigned)trk[1].p16m_next,
		       (unsigned)trk[2].p16m_next, (unsigned)trk[3].p16m_next);
		/* STVFIX-650 (W312): the arguments in the order the format names them.
		 * v=2 marks a truthful line; older captures are remapped by 571. */
		printk("STV,v=2,lo=%u,up=%u,cx=%u,pf=%u,re=%u,rhw=%u,hr=%u,hb=%u,hf=%u,ha=%u,hz=%u\n",
		       (unsigned)g_stv_lo, (unsigned)g_stv_up,
		       (unsigned)g_stv_cx, (unsigned)g_stv_pf, (unsigned)g_stv_re,
		       (unsigned)g_rw_hw, (unsigned)g_stv_hr, (unsigned)g_hdr_bad, (unsigned)g_hdr_fixed, (unsigned)g_hdr_alien, (unsigned)g_hdr_zero);   /* HOLDFADE-787, HDRCHK-789, ALIEN-790 */
		if (g_hdr_alien) {   /* ALIEN-790: the first real alien's header, as the decoder saw it */
			printk("ALN,h=");
			for (uint32_t _k = 0u; _k < 16u; _k++) printk("%02x", (unsigned)g_aln_hex[_k]);
			printk("\n");
		}
		if (g_hdr_bad) {   /* HDRCHK-789: the first suspect block, card address + index in its burst + 16 header bytes */
			printk("HDR,blk=%u,i=%u,h=", (unsigned)g_hdr_blk, (unsigned)g_hdr_idx);
			for (uint32_t _k = 0u; _k < 16u; _k++) printk("%02x", (unsigned)g_hdr_hex[_k]);
			printk("\n");
		}
		/* FXSTAT2-585: 19 x starves/dry/blocks, one class per field (676: 16 = reverb; 691: 17 = eq, 18 = MULTI) */
		printk("FXS2");
		for (uint32_t _k = 0; _k < FXS_N; _k++)
			printk(",%u/%u/%u", (unsigned)g_fxs_stv[_k], (unsigned)g_fxs_dry[_k], (unsigned)g_fxs_blk[_k]);
		printk("\n");
		printk("FXA");   /* WOBCLAMP-681: worst block us per class (the pop instrument, W324) */
		for (uint32_t _k = 0; _k < FXS_N; _k++)
			printk(",%u", (unsigned)g_fxs_aus[_k]);
		printk("\n");
		printk("FXO,rvclip=%u", (unsigned)g_rv_clip);   /* POPS-700: over-period blocks per class + the reverb clamp count */
		for (uint32_t _k = 0; _k < FXS_N; _k++)
			printk(",%u", (unsigned)g_fxs_over[_k]);
		printk("\n");
		/* POPTRAP-728 (W335): events / blocks watched, then the first latched event since the last print */
		printk("POP,n=%u,c=%u,blk=%u", (unsigned)g_pop_n, (unsigned)g_pop_c, (unsigned)g_pop_blkn);
		if (g_pop_pend) {
			printk(",ms=%u,jmp=%d,pk=%d,at=%u,ch=%u,cus=%u,stv=%x,pl=%u,spd=%u,usb=%u,got=%u",
			       (unsigned)g_pop_ms, (int)g_pop_jmp, (int)g_pop_pk, (unsigned)g_pop_at, (unsigned)g_pop_ch, (unsigned)g_pop_cus,
			       (unsigned)g_pop_stv, (unsigned)g_pop_pl, (unsigned)g_pop_spd, (unsigned)g_pop_usb, (unsigned)g_pop_got);
			if (g_pw_done)   /* POPWHO-779: the readback */
				printk(",who=%u/%u/%u/%u,mon=%u,av=%u/%u/%u/%u,fd=%u/%u/%u/%u,st=%x,ecd=%u,bo=%u/%u/%u/%u,so=%u/%u/%u/%u",
				       (unsigned)g_pw_t[0], (unsigned)g_pw_t[1], (unsigned)g_pw_t[2], (unsigned)g_pw_t[3], (unsigned)g_pw_mon,
				       (unsigned)g_pw_av[0], (unsigned)g_pw_av[1], (unsigned)g_pw_av[2], (unsigned)g_pw_av[3],
				       (unsigned)g_pw_fd[0], (unsigned)g_pw_fd[1], (unsigned)g_pw_fd[2], (unsigned)g_pw_fd[3],
				       (unsigned)g_pw_st, (unsigned)g_pw_ecd_step,
				       (unsigned)g_pw_bo[0], (unsigned)g_pw_bo[1], (unsigned)g_pw_bo[2], (unsigned)g_pw_bo[3],   /* BLKOFF-784 */
				       (unsigned)g_pw_so[0], (unsigned)g_pw_so[1], (unsigned)g_pw_so[2], (unsigned)g_pw_so[3]);
			if (g_pw_done) {   /* RUNLOG-785 */
				printk(",wt=%u,rv=", (unsigned)g_rl_wt);
				for (uint32_t _k = 0u; _k < 24u; _k++) printk("%s%d", _k ? "/" : "", (int)g_rl_rv[_k]);   /* ALIEN-790: 24 frames, est-8 first */
				printk(",rl=%u:%u,rb=%u:%u,wl=", (unsigned)g_rl_rl, (unsigned)g_rl_rls, (unsigned)g_rl_rb, (unsigned)g_rl_rbs);
				for (uint32_t _k = 0u; _k < RL_N; _k++)
					printk("%s%d:%u:%u:%u", _k ? "/" : "", (int)(g_pop_ms - g_rl_out[_k].ms),
					       (unsigned)g_rl_out[_k].s, (unsigned)g_rl_out[_k].n, (unsigned)g_rl_out[_k].p);
			}
			/* POPSTICKY-788: the latch persists; a new click overwrites it */
		}
		printk("\n");
		/* POPLOG-730: the census ring -- up to 4 events since the last print: ms:jump:frame:ch:tags(hex):ctl:age */
		printk("POPL,n=%u", (unsigned)g_pop_logn);
		for (uint32_t _k = 0; _k < g_pop_logn && _k < POPL_N; _k++) {   /* POPSTICKY-788: oldest first, never cleared */
			const uint32_t _j = (g_pop_logn < POPL_N) ? _k : ((g_pop_logi + _k) % POPL_N);
			printk(",%u:%u:%u:%u:%x:%u:%u", (unsigned)g_pop_log[_j].ms, (unsigned)g_pop_log[_j].jmp, (unsigned)g_pop_log[_j].at,
			       (unsigned)g_pop_log[_j].ch, (unsigned)g_pop_log[_j].tags, (unsigned)g_pop_log[_j].ctl, (unsigned)g_pop_log[_j].age);
		}
		printk("\n");

		printk("BTN,lat=%u,max=%u\n",
		       (unsigned)g_stop_lat_ms, (unsigned)g_stop_lat_max);
		printk("MT,b=815,mute=%u,tog=%u,rel=%u,tap=%u,rst=%u,sp=%u,last=%u,play=%u,cmb=%u,tr=%u,br=%u,brd=%u\n",   /* MTDIAG-742 / MUTEFIX4-743 / BEATREP-749: the PLAY-release trap, the chord-release mutes, the sagged PLAY reading, the repeat's engages + live div */
		       (unsigned)g_mon_mute, (unsigned)g_mt_tog, (unsigned)g_mt_rel, (unsigned)g_mt_tap, (unsigned)g_mt_rst, (unsigned)g_mt_sp, (unsigned)g_mt_last, (unsigned)g_playing, (unsigned)g_mt_cmb, (unsigned)g_mt_tr, (unsigned)g_br_n, (unsigned)(g_br_on ? g_chop_div : 0u));


		printk("BR,sh=%u,on=%u,cd=%u,co=%u,fw=%u,w=%u/%u/%u/%u,b=%u/%u/%u/%u\n",   /* BRDIAG-804 */
		       (unsigned)g_br_shift, (unsigned)g_br_on, (unsigned)g_chop_div, (unsigned)g_chop_off, (unsigned)g_win_free,
		       (unsigned)g_br_w[0], (unsigned)g_br_w[1], (unsigned)g_br_w[2], (unsigned)g_br_w[3],
		       (unsigned)g_br_b[0], (unsigned)g_br_b[1], (unsigned)g_br_b[2], (unsigned)g_br_b[3]);
		printk("W4P,pk=%u,pb=%u,sq=%u,tq=%u\n",
		       (unsigned)g_w4_pk, (unsigned)g_w4_pb,
		       (unsigned)g_w4_sq, (unsigned)g_w4_tq);
		{   /* CX-r2: EVERY tick. The once-per-boot guard cost the
		     * 435 run its thread map. */
			g_w4n_once = 1;
			extern struct k_thread z_main_thread;
			extern struct k_work_q k_sys_work_q;
			printk("W4N,aud=%p,str=%p,midi=%p,main=%p,sysq=%p\n",
			       (void *)&audio_tcb, (void *)&streamer_tcb,
			       (void *)&midi_tcb, (void *)&z_main_thread,
			       (void *)&k_sys_work_q.thread);
		}
		{	/* U3-471: name the census threads by PRIORITY. Zephyr coop
			 * priorities are NEGATIVE, so the USB cluster (K_PRIO_COOP(8))
			 * is unmistakable against idle (lowest) and the app threads
			 * (aud 0 / main 2 / streamer 5 or 1 sprinting / midi 6).
			 * Needs no Zephyr-internal symbols. */
			printk("U3P");
			for (int _u3i = 0; _u3i < 8; _u3i++) {
				void *_t = g_w4c_tab[_u3i].tid;
				if (!_t) { printk(",-"); continue; }
				printk(",%p:%d:%u", _t,
				       (int)k_thread_priority_get((k_tid_t)_t),
				       (unsigned)g_w4c_tab[_u3i].n);
			}
			printk("\n");
			printk("U3B,ringhi=%u,ringlo=%u,ringcap=%u,txhi=%u,txcap=%u,rxlo=%u\n",
			       (unsigned)g_u3_ring_hi,
			       (unsigned)(g_u3_ring_lo == 0xFFFFFFFFu ? 0u : g_u3_ring_lo),
			       (unsigned)USB_RING_FRAMES,
			       (unsigned)g_u3_tx_hi, 8u,   /* TXSLAB-773 */
			       (unsigned)(g_u3_rx_lo == 0xFFFFu ? 9999u : g_u3_rx_lo));
		}
		{	/* SS-473: stack high-water. k_thread_stack_space_get walks the
			 * 0xAA fill (CONFIG_INIT_STACKS) and reports bytes NEVER
			 * touched since thread creation -- a true peak, not a
			 * sample, so Protocol A cannot sleep through it.
			 * rc != 0 means the config did not take; treat as no data. */
			printk("U4S");
			for (int _ssi = 0; _ssi < 8; _ssi++) {
				struct k_thread *_kt = (struct k_thread *)g_w4c_tab[_ssi].tid;
				size_t _un = 0;
				int _rc;
				if (!_kt) { printk(",-"); continue; }
				_rc = k_thread_stack_space_get((k_tid_t)_kt, &_un);
				printk(",%p:%u:%u:%d", (void *)_kt->stack_info.start,
				       (unsigned)_kt->stack_info.size,
				       (unsigned)_un, _rc);
			}
			printk("\n");
		}
		printk("W4G,gmax=%u\n", (unsigned)g_gap_max);
		printk("W4C,%p:%u,%p:%u,%p:%u,%p:%u,%p:%u,%p:%u,%p:%u,%p:%u,m=%u,str=%p\n",
		       g_w4c_tab[0].tid, (unsigned)g_w4c_tab[0].n,
		       g_w4c_tab[1].tid, (unsigned)g_w4c_tab[1].n,
		       g_w4c_tab[2].tid, (unsigned)g_w4c_tab[2].n,
		       g_w4c_tab[3].tid, (unsigned)g_w4c_tab[3].n,
		       g_w4c_tab[4].tid, (unsigned)g_w4c_tab[4].n,
		       g_w4c_tab[5].tid, (unsigned)g_w4c_tab[5].n,
		       g_w4c_tab[6].tid, (unsigned)g_w4c_tab[6].n,
		       g_w4c_tab[7].tid, (unsigned)g_w4c_tab[7].n,
		       (unsigned)g_w4c_miss, (void *)g_str_tid);
		printk("W4X,%u,%u,%u,%u,%u,%u,%u,%u,tot=%u,hs=%u\n",
		       (unsigned)g_cx_n[0], (unsigned)g_cx_n[1],
		       (unsigned)g_cx_n[2], (unsigned)g_cx_n[3],
		       (unsigned)g_cx_n[4], (unsigned)g_cx_n[5],
		       (unsigned)g_cx_n[6], (unsigned)g_cx_n[7],
		       (unsigned)g_cx_tot, (unsigned)g_cx_hs);
		printk("W4Y,%u,%u,%u,%u,%u,%u,%u,%u,tot=%u,hs=%u\n",
		       (unsigned)g_cy_n[0], (unsigned)g_cy_n[1],
		       (unsigned)g_cy_n[2], (unsigned)g_cy_n[3],
		       (unsigned)g_cy_n[4], (unsigned)g_cy_n[5],
		       (unsigned)g_cy_n[6], (unsigned)g_cy_n[7],
		       (unsigned)g_cy_tot, (unsigned)g_cx_hs);
		printk("W4Z,%u,%u,%u,%u,%u,%u,%u,%u,tot=%u\n",
		       (unsigned)g_cz_n[0], (unsigned)g_cz_n[1],
		       (unsigned)g_cz_n[2], (unsigned)g_cz_n[3],
		       (unsigned)g_cz_n[4], (unsigned)g_cz_n[5],
		       (unsigned)g_cz_n[6], (unsigned)g_cz_n[7],
		       (unsigned)g_cz_tot);
	  }
	}
	{ /* M72: table health + track-1 exact length (a non-multiple of
	   * 256 surviving a power cycle is the PASS) */
	  uint32_t _d72 = 0;
	  (void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &_d72);
	  if (_d72)
		printk("M72,x3ok=%u,l0=%u,s0=%u\n", (unsigned)g_x3_ok,
		       (unsigned)trk[0].len_samps, (unsigned)trk[0].start_samps);
	}
	{ /* M71: async-read health, once per diag cycle (deltas) */
	  static uint32_t _p_as, _p_rt, _p_fb, _p_vf, _p_sk; uint32_t _d = 0;
	  (void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &_d);
	  if (_d) {
		static uint32_t _p_ca, _p_rq, _p_rd, _p_wu;
		printk("M71,as=%u,rt=%u,fb=%u,ca=%u,rq=%u,rd=%u,wu=%u,ovf=%u,vf=%u,sk=%u,cr=%u\n",   /* CRCC-625: checked, unchecked, read rate */
		       (unsigned)(g_m71_as - _p_as),
		       (unsigned)(g_m71_rt - _p_rt),
		       (unsigned)(g_m71_fb - _p_fb),
		       (unsigned)(g_m71_ca - _p_ca),
		       (unsigned)(g_m71_rq - _p_rq),
		       (unsigned)(g_m71_rd - _p_rd),
		       (unsigned)(g_m71_wu - _p_wu), (unsigned)g_prime_ovf,
		       (unsigned)(g_m71_vf - _p_vf), (unsigned)(g_m71_sk - _p_sk), (unsigned)g_crcc_rate);
		_p_as = g_m71_as; _p_rt = g_m71_rt; _p_fb = g_m71_fb; _p_vf = g_m71_vf; _p_sk = g_m71_sk;
		_p_ca = g_m71_ca; _p_rq = g_m71_rq; _p_rd = g_m71_rd; _p_wu = g_m71_wu;
		/* RRT-630: totals, the frozen trace (oldest first; the last event is the one
		 * during which the backlog crossed 3/4), TKD */
		printk("RRT,on=%u,p1=%u,gap=%u@%u,wr=%u/%u/%u,rd=%u/%u,sl=%u,frz=%u/%u/%u,tkd=%u/%u,p1s=%u/%u,werr=%u/%u,hw=%u/%u,ev=",
		       (unsigned)g_rrt_on, (unsigned)g_rrt_p1, (unsigned)g_rrt_gap_us, (unsigned)g_rrt_gap_fill,
		       (unsigned)g_rrt_wr_n, (unsigned)g_rrt_wr_us, (unsigned)g_rrt_wr_max,
		       (unsigned)g_rrt_rd_n, (unsigned)g_rrt_rd_us, (unsigned)g_rrt_sl_us,
		       (unsigned)g_rrt_frz, (unsigned)g_rrt_frz_fill, (unsigned)g_rrt_frz_ms,
		       (unsigned)g_tkd_n, (unsigned)g_tkd_ovr, (unsigned)g_p1spr_n, (unsigned)g_p1spr_cut,
		       (unsigned)g_aw2_werr, (unsigned)g_rrt_wretry, (unsigned)g_rrt_hw_fill, (unsigned)g_rrt_hw_ms);
		for (uint32_t _k = 0; _k < RRT_N; _k++) {   /* RETRY-633: the high-water snapshot, oldest first */
			const struct rrt_ev *_e = &g_rrt_hw[(g_rrt_hw_i + _k) % RRT_N];
			if (!_e->t) continue;
			printk("%c%u:%u ", "?RWPS"[_e->t], (unsigned)_e->n, (unsigned)_e->us);
		}
		printk("\n");
		/* TLT-649: the last tail, and the ring as it stood at the promotion */
		printk("TL,n=%u,ms=%u,fill0=%u,p1=%u,wr=%u/%u,rd=%u/%u,sl=%u,pf=%u,prime=%u,meta=%u/%u,ev=",
		       (unsigned)g_tl_n, (unsigned)g_tl_ms, (unsigned)g_tl_fill0, (unsigned)g_tl_p1,
		       (unsigned)g_tl_wr, (unsigned)g_tl_wus, (unsigned)g_tl_rd, (unsigned)g_tl_rus, (unsigned)g_tl_sl,
		       (unsigned)g_tl_pf, (unsigned)g_tl_prime_us, (unsigned)g_tl_meta_us, (unsigned)g_tl_meta_n);
		for (uint32_t _k = 0; _k < RRT_N; _k++) {
			const struct rrt_ev *_e = &g_rrt_tl[(g_rrt_tl_i + _k) % RRT_N];
			if (!_e->t) continue;
			printk("%c%u:%u ", "?RWPS"[_e->t], (unsigned)_e->n, (unsigned)_e->us);
		}
		printk("\n");
	  }
	}
	/* Stream one status line over USB-serial, but ONLY when a host has opened
	 * the port (DTR asserted) — otherwise printk could stall the control loop.
	 * Throttled by the caller. Healthy: tracks PLAY, ovr=0 (no record-buffer
	 * overflow), rerr=0/werr=0 (clean storage bus). */
	uint32_t dtr = 0;
	(void)uart_line_ctrl_get(cdc, UART_LINE_CTRL_DTR, &dtr);
	if (!dtr)
		return;

	static const char *const tsn[] = { "---", "ARM", "REC", "DON", "PLY" };
	int batt = ladder_read(&adc_ladder[LAD_BATT]);   /* raw 12-bit, battery divider */
	uint32_t cpos = g_consume_pos;
	int mg[NTRK];
	for (int _i = 0; _i < NTRK; _i++)
		mg[_i] = (int)((int32_t)(trk[_i].p_w - cpos) / (int)(LOOP_RATE / 1000u));
	/* M25-r4 INSTRUMENTATION (diag only): each track's phase INSIDE its own
	 * loop, start_samps mod len_samps. This number is the one thing a
	 * convergence retune must NOT change — it rescales len_samps for every
	 * track but never touches start_samps, so if the hypothesis is right
	 * this jumps at the retune for any track whose anchor is non-zero.
	 * Track 1 anchors at 0 and is immune, which is why the symptom only
	 * ever showed on an overdub. */
	uint32_t phz[NTRK];
	for (int _i = 0; _i < NTRK; _i++)
		phz[_i] = trk[_i].len_samps
			? (trk[_i].start_samps % trk[_i].len_samps) : 0u;
	printk("LOOPER %dHz song=%d %s hp=%d hpin=%d usb=%d chg=%d batt=%d bpm=%d detbpm=%d vol=%d "
	       "trk[%s %s %s %s] rec=%d mut=%u%u%u%u ovr=%u rerr=%u werr=%u marg=[%d %d %d %d]ms stv=[%u %u %u %u] len=[%u %u %u %u] st=[%u %u %u %u] spim=%d cache=%d ckb=%u wbi=%u chop=%u/%u ph=[%u %u %u %u] snap=%u/%u\n",
	       (int)LOOP_RATE, (int)g_slot, g_playing ? "PLAY" : "STOP", g_hp_on, g_hp_in,
	       usb_present() ? 1 : 0, charging() ? 1 : 0, batt,
	       g_play_bpm, g_det_bpm, g_master_vol_q8,
	       tsn[trk[0].state % 5], tsn[trk[1].state % 5],
	       tsn[trk[2].state % 5], tsn[trk[3].state % 5],
	       g_rec_track,
	       (unsigned)trk[0].muted, (unsigned)trk[1].muted,
	       (unsigned)trk[2].muted, (unsigned)trk[3].muted,
	       (unsigned)g_rec_overruns,
	       (unsigned)emmc_crc_rd_errs, (unsigned)emmc_crc_wr_errs,
	       mg[0], mg[1], mg[2], mg[3],
	       (unsigned)g_starve_cnt[0], (unsigned)g_starve_cnt[1], (unsigned)g_starve_cnt[2], (unsigned)g_starve_cnt[3],
	       (unsigned)trk[0].len_blocks, (unsigned)trk[1].len_blocks, (unsigned)trk[2].len_blocks, (unsigned)trk[3].len_blocks,
	       (unsigned)trk[0].start_blk, (unsigned)trk[1].start_blk, (unsigned)trk[2].start_blk, (unsigned)trk[3].start_blk,
	       emmc_spim_active() ? 1 : 0, g_cache_on ? 1 : 0, (unsigned)g_cache_kb, (unsigned)emmc_dbg_wr_busy_max,
	       (unsigned)g_chop_div, (unsigned)g_chop_off,
	       (unsigned)phz[0], (unsigned)phz[1], (unsigned)phz[2], (unsigned)phz[3],
	       (unsigned)g_snap_took, (unsigned)g_snap_took);
	{
		/* CPU= per-thread share of the last window, in percent: audio,
		 * streamer, midi, main, everything-else(usb/idle/isr). Answers
		 * WHERE the cycles actually go when refill can't build surplus. */
		static uint64_t l_aud, l_str, l_mid, l_mai, l_all;
		k_thread_runtime_stats_t rs;
		uint64_t aud = 0, str = 0, mid = 0, mai = 0, all = 0;
		if (!k_thread_runtime_stats_get(&audio_tcb, &rs))    aud = HK_RS_CYC(rs);
		if (!k_thread_runtime_stats_get(&streamer_tcb, &rs)) str = HK_RS_CYC(rs);
		if (!k_thread_runtime_stats_get(&midi_tcb, &rs))     mid = HK_RS_CYC(rs);
		if (!k_thread_runtime_stats_get(k_current_get(), &rs)) mai = HK_RS_CYC(rs);
		if (!k_thread_runtime_stats_all_get(&rs))            all = HK_RS_CYC(rs);
		uint64_t d_all = all - l_all;
		if (d_all) {
			printk("CPU aud=%u%% str=%u%% midi=%u%% main=%u%%\n",
			       (unsigned)((aud - l_aud) * 100u / d_all),
			       (unsigned)((str - l_str) * 100u / d_all),
			       (unsigned)((mid - l_mid) * 100u / d_all),
			       (unsigned)((mai - l_mai) * 100u / d_all));
		}
		l_aud = aud; l_str = str; l_mid = mid; l_mai = mai; l_all = all;
	}
	extern volatile uint32_t emmc_dbg_cmd_retries;
	printk("PASS2 p2=[%u %u %u %u] sn=[%u %u %u %u] ab=%u,%u rt=%u cn=[%u %u %u %u]\n",
	       (unsigned)g_p2blk[0], (unsigned)g_p2blk[1], (unsigned)g_p2blk[2], (unsigned)g_p2blk[3],
	       (unsigned)g_p2snap[0], (unsigned)g_p2snap[1], (unsigned)g_p2snap[2], (unsigned)g_p2snap[3],
	       (unsigned)g_p2yield, (unsigned)g_p2rfail,
	       (unsigned)emmc_dbg_cmd_retries,
	       (unsigned)trk[0].content_blocks, (unsigned)trk[1].content_blocks,
	       (unsigned)trk[2].content_blocks, (unsigned)trk[3].content_blocks);
	for (int _k = 0; _k < 4; _k++) { g_p2blk[_k] = 0; g_p2snap[_k] = 0; }
	g_p2yield = 0; g_p2rfail = 0;
	{
		/* THE stall numbers, finally wall-clock: wus=write-busy window/session
		 * max (us), rus=read-access wait, sus=CMD6 busy (cache flush / future
		 * TRIM+BKOPS), bto=busy-poll expiries, low=worst play margin this
		 * window (ms), hiw=worst rec fill (ms), gl=stored glitches (REPEATING
		 * artifacts), iwf=i2s failures, aus=worst audio-block exec us,
		 * ec=EXT_CSD[167,166,231,502,503,198,246,192,175]. */
		int32_t _lwv = g_play_lowat;
		int _lw = (_lwv == 0x7FFFFFFF) ? -1
			  : (int)(_lwv / (int32_t)(LOOP_RATE / 1000u));
		/* USB live-input health: uu=drain underruns (ring dry at the mixer),
		 * uo=receive overflows (whole 1 ms packet dropped: host over-
		 * delivering), up=ISO packets this window (~2x window-ms expected...
		 * i.e. ~1000/s), ufl=ring fill low,high watermarks in frames
		 * (setpoint ~1024 of 4096), fb=feedback delta from the true rate
		 * (Q10.14 LSBs; 0 = asking exactly for 48000 Hz). */
		static uint32_t _uplast;
		uint32_t _upnow = g_usb_pkts;
		unsigned _updelta = (unsigned)(_upnow - _uplast);
		_uplast = _upnow;
		int32_t _ulw = g_usb_lowat;
		if (_ulw == 0x7FFFFFFF) _ulw = -1;
		int _fbd = (int)((int32_t)atomic_get(&g_fb_value) - (int32_t)FB_TRUE);
		printk("EMMC48 wus=%u/%u rus=%u sus=%u bto=%u low=%dms hiw=%ums gl=%u iwf=%u aus=%u rr=%x flt=%x@%x hi=%u,%u cf=%u,%u uu=%u uo=%u up=%u ufl=%d,%u fb=%d ec=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
		       (unsigned)emmc_dbg_wr_busy_us_max, (unsigned)emmc_dbg_wr_busy_us_peak,
		       (unsigned)emmc_dbg_rd_wait_us_max, (unsigned)emmc_dbg_switch_busy_us_max,
		       (unsigned)emmc_dbg_busy_timeouts, _lw,
		       (unsigned)(g_rec_hiwat / (LOOP_RATE / 1000u)),
		       (unsigned)g_stored_glitch_cnt, (unsigned)g_i2s_wfail_cnt,
		       (unsigned)g_audio_us_max,
		       (unsigned)g_resetreas,
		       (unsigned)g_last_fault_reason, (unsigned)g_last_fault_pc,
		       (unsigned)g_hpi_on, (unsigned)emmc_dbg_hpi_fires,
		       (unsigned)emmc_dbg_cf_fires, (unsigned)emmc_dbg_cf_ok,   /* HK-613 */
		       (unsigned)g_ring_underruns, (unsigned)g_ring_overflows,
		       _updelta, _ulw, (unsigned)g_usb_hiwat, _fbd,
		       g_extcsd_dump[0], g_extcsd_dump[1], g_extcsd_dump[2],
		       g_extcsd_dump[3], g_extcsd_dump[4], g_extcsd_dump[5],
		       g_extcsd_dump[6], g_extcsd_dump[7], g_extcsd_dump[8]);
	}
	{
		/* USBIN: exact-rate splits for the input path. dt=window ms;
		 * sof/pk/fr = SOF heartbeats, ISO packets, audio frames received
		 * this window (expect dt, dt, 48*dt); nb=packets DROPPED because
		 * the rx pool was empty (MUST stay 0 after the 32-buffer fix);
		 * sl=min free rx buffers (headroom left); zp=silence frames padded
		 * into the live/record path this window (MUST stay 0). */
		static uint32_t _lms, _lsof, _lpk, _lfr, _lnb, _lzp;
		uint32_t _now2 = k_uptime_get_32();
		uint32_t _sof = g_sof_cnt, _pk = g_usb_pkts, _fr = g_usb_frames;
		uint32_t _nb = g_rx_nobuf, _zp = g_zero_pad;
		printk("USBIN dt=%u sof=%u pk=%u fr=%u nb=%u sl=%u zp=%u\n",
		       (unsigned)(_now2 - _lms), (unsigned)(_sof - _lsof),
		       (unsigned)(_pk - _lpk), (unsigned)(_fr - _lfr),
		       (unsigned)(_nb - _lnb), (unsigned)g_rx_slab_min,
		       (unsigned)(_zp - _lzp));
		_lms = _now2; _lsof = _sof; _lpk = _pk; _lfr = _fr;
		_lnb = _nb; _lzp = _zp;
		g_rx_slab_min = 0xFFFF;
	}
	emmc_dbg_wr_busy_max = 0u;   /* per-window worst, reset each print */
	emmc_dbg_wr_busy_us_max = 0u;
	emmc_dbg_rd_wait_us_max = 0u;
	g_play_lowat = 0x7FFFFFFF;
	g_rec_hiwat = 0u;
	g_usb_lowat = 0x7FFFFFFF;
	g_usb_hiwat = 0u;
}

/* ---- decode the ladders into named buttons (verified thresholds) ---- */
enum trk_btn { TRK_NONE = -1, TRK_1, TRK_2, TRK_3, TRK_4, TRK_PLAY };
enum vol_btn { VOL_NONE = -1, VOL_TEMPO_DOWN, VOL_DOWN, VOL_TEMPO_UP, VOL_UP, VOL_BOTH };   /* INFX-672: both volume buttons */

static enum trk_btn decode_tracks(int v)
{
	if (v <  110) return TRK_NONE;
	if (v <  300) return TRK_1;     /* ~213  */
	if (v <  560) return TRK_2;     /* ~403  */
	if (v <  950) return TRK_3;     /* ~733  */
	if (v < 1500) return TRK_4;     /* ~1220 */
	return TRK_PLAY;                /* ~1823 */
}

/* BEATREP-749: the beat repeat = a window pointed at one beat. Controls thread only.
 * BRCHOP-800: the window lives INSIDE THE AUDIBLE CYCLE (the chop's tile), never in storage: the streamer keeps its
 * ordinary chop mapping and only narrows the phase to [C0, C0 + W). br_tile() mirrors the streamer's tile math. */
static void __attribute__((noinline)) br_tile(const struct looptrk *t, uint32_t gb, uint32_t *pwin, uint32_t *pcyc)
{
	uint32_t wper, wbase;   /* CHOPCORE-805: the beat repeat reads the streamer's tile, not a copy of it */
	chop_tile(gb, TSPB(t), &wper, pwin, &wbase, pcyc);
}
/* BRCHOP-800: publish the FN layer's Q8 view from the first repeated track. */
static void br_publish8(void)
{
	for (int i = 0; i < NTRK; i++) {
		if (!g_br_w[i]) continue;
		uint32_t win, cyc; br_tile(&trk[i], trk[i].len_blocks, &win, &cyc);
		uint32_t l8 = (uint32_t)(((uint64_t)g_br_w[i] * 256u) / cyc); if (l8 < 1u) l8 = 1u; if (l8 > 256u) l8 = 256u;
		g_br_s8 = (uint16_t)((((uint64_t)g_br_b[i] * 256u) / cyc) & 255u); g_br_len8 = (uint16_t)l8;
		return;
	}
}
/* BRWIN-754: the per-track geometry of a window `div` of the loop, ONE back from the playhead's last beat line. */
/* BRSUB-807: `den` is what the ROCKER asked for when the musical window does not fit inside the chop --
 * 4 (UP, short) or 2 (DOWN, long). It is ignored whenever the window fits, which is every un-chopped song. */
static void __attribute__((noinline)) br_geom(uint32_t div, uint32_t den)
{
	g_br_live = 0;
	for (int i = 0; i < NTRK; i++) {
		struct looptrk *t = &trk[i];
		g_br_w[i] = 0u;
		if (t->state != TS_PLAY || head_active(i) || !t->len_blocks || !div) continue;
		const uint32_t spb = TSPB(t), gb = t->len_blocks;
		uint32_t win, cyc; br_tile(t, gb, &win, &cyc);
		uint32_t w = (gb + div / 2u) / div; if (w < 1u) w = 1u;   /* BRGRID-764: rounded, not truncated (87.875 -> 88, not 87) */
		{	/* BRSUB-807: a window INSIDE what is audible is never wider than HALF of it. 800 clamped to the whole
			 * cycle, which made the repeat a NO-OP on a chop shorter than a beat. This only fires when the musical
			 * window does not fit -- un-chopped, one beat of an n-beat loop is already <= half the loop. */
			if (w >= cyc) { w = (den >= 4u) ? (cyc / 4u) : (cyc / 2u); if (w < 1u) w = 1u; }
		}
		const uint32_t P = g_consume_pos, pwbc = P / spb;
		uint32_t line_blk;   /* the last window boundary at or before the playhead, in this track's blocks (free-running) */
		{	/* BRGRID-764: on a gridded song the boundaries are the BEAT LINES (GRIDCORE: O + k * L / n), subdivided by the
			 * window; ungridded: multiples of the window in the free-running count, as before. */
			const uint32_t n = (g_slot < NUM_SLOTS) ? (uint32_t)g_grid_n[g_slot] : 0u;
			if (n && g_loop_len) {   /* windows of L / div from the 1 (O): every beat line when div is a multiple of n, every other beat for the 2-beat window */
				const uint32_t O = (g_slot < NUM_SLOTS) ? g_grid_o[g_slot] : 0u;
				const uint32_t off = (uint32_t)((((uint64_t)((P + g_loop_len - (O % g_loop_len)) % g_loop_len) * div) % g_loop_len) / div);   /* samples into the current window */
				line_blk = (P - off) / spb;
			} else {
				line_blk = pwbc - (pwbc % w);
			}
		}
		/* BRCHOP-800: the line's phase in the AUDIBLE cycle, exactly as the streamer computes it (the nudge included) */
		const uint32_t c_line = chop_phase(i, t->start_blk, line_blk, gb, cyc, spb);   /* CHOPANCHOR-805: the streamer's own law */
		g_br_b[i] = (c_line + cyc - w) % cyc; g_br_w[i] = w; g_br_a[i] = line_blk % w;   /* BRSTICK-763 / BRGRID-764: phase-continuous, one window back from the line */
	}
	br_publish8();
	g_br_live = 1;
}
/* BRFN-765 / BRCHOP-800: set the repeat's window to [s8, s8 + len8) as Q8 fractions of the AUDIBLE cycle, per track,
 * keeping each track's phase inside the window (no retrigger). Used by the FN shift and the FN faders. */
static void __attribute__((noinline)) br_setwin(uint32_t s8, uint32_t len8)
{
	if (len8 < 1u) len8 = 1u; if (len8 > 256u) len8 = 256u;
	s8 &= 255u;
	g_br_live = 0;
	const uint32_t P = g_consume_pos;
	for (int i = 0; i < NTRK; i++) {
		struct looptrk *t = &trk[i];
		if (!g_br_w[i] || t->state != TS_PLAY || head_active(i) || !t->len_blocks) { g_br_w[i] = 0u; continue; }
		const uint32_t spb = TSPB(t), gb = t->len_blocks;
		uint32_t win, cyc; br_tile(t, gb, &win, &cyc);
		uint32_t w = (uint32_t)(((uint64_t)len8 * cyc) >> 8); if (w < 1u) w = 1u;
		if (w >= cyc) { w = cyc / 2u; if (w < 1u) w = 1u; }   /* BRSUB-807: the FN faders stop short of the whole cycle -- the whole cycle IS the chop */
		const uint32_t pwbc = P / spb, w0 = g_br_w[i];
		const uint32_t phi = (pwbc + w0 - (g_br_a[i] % w0)) % w0;      /* the phase inside the old window now */
		g_br_b[i] = (uint32_t)(((uint64_t)s8 * cyc) >> 8) % cyc; g_br_w[i] = w;
		g_br_a[i] = ((pwbc % w) + w - (phi % w)) % w;                  /* keep the phase */
	}
	g_br_s8 = (uint16_t)s8; g_br_len8 = (uint16_t)len8;
	g_br_live = 1;
}
/* BRSTICK-763 / BRCHOP-800: a resize keeps the CAPTURED start and changes only the length (the phase kept), per track
 * from the division exactly (no Q8 round trip), clamped to the audible cycle. */
static void __attribute__((noinline)) br_resize(uint32_t div)
{
	if (!div) return;
	g_br_live = 0;
	const uint32_t P = g_consume_pos;
	for (int i = 0; i < NTRK; i++) {
		struct looptrk *t = &trk[i];
		if (!g_br_w[i] || t->state != TS_PLAY || head_active(i) || !t->len_blocks) { g_br_w[i] = 0u; continue; }
		const uint32_t spb = TSPB(t), gb = t->len_blocks;
		uint32_t win, cyc; br_tile(t, gb, &win, &cyc);
		uint32_t w = (gb + div / 2u) / div; if (w < 1u) w = 1u;
		if (w >= cyc) { w = cyc / 2u; if (w < 1u) w = 1u; }   /* BRSUB-807: a resize can never climb back into the no-op */
		const uint32_t pwbc = P / spb, w0 = g_br_w[i];
		const uint32_t phi = (pwbc + w0 - (g_br_a[i] % w0)) % w0;
		g_br_w[i] = w;
		g_br_a[i] = ((pwbc % w) + w - (phi % w)) % w;
	}
	br_publish8();
	g_br_live = 1;
}
static void __attribute__((noinline)) br_click(int down)   /* BRDIR-757: `down` = SHRINK = the rocker UP (the chop's convention) */
{
	if (!g_br_on) {
		if (!g_playing || (g_rec_track >= 0 && !g_bnc_on) || !g_loop_active || !g_loop_len) return;   /* BRBNC-758: a repeat may start during a bounce (not during a plain take) */
		uint32_t n = (g_slot < NUM_SLOTS) ? (uint32_t)g_grid_n[g_slot] : 0u;
		if (n == 0u || n > CHOP_DIV_MAX / 2u) n = 8u;   /* ungridded: eighths of the loop */
		if (!down && n > 1u && (n & 1u) == 0u) n /= 2u;   /* BRBEAT-755 / BRDIR-757: UP first = ONE beat; DOWN first = TWO beats */
		g_br_div = n;
		br_geom(n, down ? 4u : 2u);   /* BRWIN-754 / BRCHOP-800: inside the chop, the chop untouched. BRSUB-807: UP (down) asks SHORT -> a quarter of the chop, DOWN asks LONG -> half */
		g_br_on = 1; g_br_n++;
	} else {
		uint32_t d = g_br_div;
		if (down) { if (d * 2u <= CHOP_DIV_MAX) d *= 2u; }          /* half the window */
		else      { if (d > 1u && (d & 1u) == 0u) d /= 2u; }         /* double it (clamped to the audible cycle per track) */
		g_br_div = d;
		br_resize(d);   /* BRSTICK-763: the same captured clip, resized from its start */
	}
	g_chop_req = 1; g_dip_req = 1;
}
/* LOOPHOLD-804: `roll` = land where the tape would be (792's behaviour, now the FN variant). The default HOLDS the
 * tape: each track lands where the repeat left off, the shift rounded to a bar so the downbeats stay on the grid. */
static void __attribute__((noinline)) br_release(int roll)
{
	if (!g_br_on) return;
	if (!roll && !g_bnc_on && g_loop_len && g_slot < NUM_SLOTS && g_grid_n[g_slot]) {
		for (int i = 0; i < NTRK; i++) {   /* one reference track: the hold is one musical amount for the whole tape */
			struct looptrk *t = &trk[i];
			if (!g_br_w[i] || t->state != TS_PLAY || head_active(i) || !t->len_blocks) continue;
			const uint32_t spb = TSPB(t), gb = t->len_blocks;
			uint32_t win, cyc; br_tile(t, gb, &win, &cyc);
			const uint32_t W = g_br_w[i], pwbc = g_consume_pos / spb;
			const uint32_t brph = (pwbc + W - (g_br_a[i] % W)) % W;
			const uint32_t c_rep = (g_br_b[i] + brph) % cyc;                    /* where the repeat is */
			const uint32_t c_rol = chop_phase(i, t->start_blk, pwbc, gb, cyc, spb);   /* where the tape is (CHOPANCHOR-805) */
			uint32_t raw = ((c_rol + cyc - c_rep) % cyc) * spb;                 /* the hold, in loop samples */
			const uint32_t n = (uint32_t)g_grid_n[g_slot];
			const uint32_t u = (n % 4u == 0u) ? 4u : (n % 3u == 0u) ? 3u : (n % 2u == 0u) ? 2u : 1u;   /* a bar, in beats */
			const uint32_t unit = (uint32_t)(((uint64_t)g_loop_len * u) / n);
			if (unit) raw = ((raw + unit / 2u) / unit) * unit;   /* NEAREST bar: a short stutter rounds to 0 and lands in time */
			if (raw) g_br_shift = (uint32_t)(((uint64_t)g_br_shift + raw) % g_loop_len);
			break;
		}
	}
	g_br_live = 0;   /* BRWIN-754: the streamer's ordinary mapping is back before the chop request */
	g_br_on = 0;
	g_chop_req = 1; g_dip_req = 1;   /* BRCHOP-800: nothing to restore -- the chop was never touched */
}

static enum vol_btn decode_vol(int v)
{
	if (v <  200) return VOL_NONE;
	if (v <  560) return VOL_TEMPO_DOWN; /* ~404  */
	if (v <  950) return VOL_DOWN;       /* ~729  */
	if (v < 1500) return VOL_TEMPO_UP;   /* ~1220 */
	if (v < 1910) return VOL_UP;         /* ~1820 */
	return VOL_BOTH;                     /* ~1998 (W115) -- INFX-672 */
}

/* ================= ALWAYS-DIM LEDs (soft PWM) =========================
 * Adapted unchanged from TechnicsOP's dimmed-LED build (shared on the SP-1
 * Discord 2026-07-15, MIT) — merged into this fork as ALWAYS-ON dimming.
 * The panel LEDs are plain on/off GPIO with no current control, so "dim" =
 * software PWM: every LED write (led_service, sweeps, gauges, our two-light
 * song display) goes into a shadow mask; a tiny TIMER3 ISR renders that
 * shadow at a low duty cycle. Single writer (control thread), ISR only
 * reads. ~1 kHz frame = flicker-free. LED_PWM_ON_US is the brightness. */
#define LED_PWM_PERIOD_US 1000u    /* 1 kHz frame */
#define LED_PWM_ON_US       52u    /* ~5.2% duty — v1.2.2: a hair dimmer than
                                    * the old 60 on the track row. Floor: at
                                    * 6 us, IRQ-entry jitter of +/-3 us is a
                                    * 10-80x brightness swing = flicker; at
                                    * 36 us it is +/-8% before the eye's ~10-
                                    * frame averaging — invisible. */
#define LED_STATUS_ON_US    66u    /* the SONG/status row runs a longer window
                                    * than the track row: slightly brighter
                                    * side lights relative to the tracks. CC2
                                    * mechanism, wide-window = jitter-immune. */
#define LED_GHOST_FRAME_DIV  5u    /* GHOST class: muted-but-loaded tracks lit
                                    * ONE frame in five using the SAME proven
                                    * 60 us window as normal dim -> 1/5 of dim
                                    * brightness (~1.2% of solid), refresh 200 Hz
                                    * (still far above flicker perception), ZERO
                                    * new edge timing. History: an 8 us second
                                    * CC window flickered (two independent IRQ
                                    * entry jitters on a narrow width) and a
                                    * 20 us in-ISR capture-spin failed to boot
                                    * on hardware — this design reuses only
                                    * field-proven mechanisms. */
/* every LED pin on each port (leds[]+track_leds[]) — for the OFF phase */
#define LED_ALL_P0 ((1u<<0)|(1u<<1)|(1u<<29)|(1u<<26))
#define LED_ALL_P1 ((1u<<13)|(1u<<12)|(1u<<15)|(1u<<14))
static volatile uint32_t g_led_p0_on;   /* P0 LED pins logically lit */
static volatile uint32_t g_led_p1_on;   /* P1 LED pins logically lit */
static volatile uint32_t g_led_p0_ghost; /* P0 pins lit at GHOST duty */
static volatile uint32_t g_led_p1_ghost; /* P1 pins lit at GHOST duty */
static volatile uint8_t  g_led_trk_lvl[4] = { 255u, 255u, 255u, 255u };   /* STACKA-664 A6: per-pin page level, 255 = the row */
static uint32_t g_led_sta_p0, g_led_sta_p1;   /* status-row pins (init-computed) */
static uint32_t g_led_trk_p0, g_led_trk_p1;   /* track-row pins  (init-computed) */

/* LEDPWM-710: hardware PWM. PWM2 = the status row, PWM3 = the track row. */
#define LED_PWM_STA   NRF_PWM2
#define LED_PWM_TRK   NRF_PWM3
#define LED_PWM_POL   0x8000u   /* high for `duty` ticks, then low (active-high LEDs) */
#define LED_PWM_FULL  0x7fffu   /* >= COUNTERTOP: solid high, no edge (what Zephyr's pwm_nrfx uses for 100 %) */
static uint16_t g_led_sta_duty[4] __attribute__((aligned(4)));   /* EasyDMA: RAM, 16-bit */
static uint16_t g_led_trk_duty[4] __attribute__((aligned(4)));
static uint32_t g_led_sta_cc = LED_STATUS_ON_US;                 /* status_level() writes it */
static uint32_t g_led_trk_cc[4] = { LED_PWM_ON_US, LED_PWM_ON_US, LED_PWM_ON_US, LED_PWM_ON_US };   /* track_level() */
static void led_hw_refresh(void)
{
	const uint32_t dim = g_led_dim;
	for (int i = 0; i < 4; i++) {
		const uint32_t m = 1u << leds[i].pin;
		const uint32_t on = (leds[i].port == NRF_P0) ? (g_led_p0_on & m) : (g_led_p1_on & m);
		const uint32_t gh = (leds[i].port == NRF_P0) ? (g_led_p0_ghost & m) : (g_led_p1_ghost & m);
		uint32_t d = 0u;
		if (on)      d = dim ? g_led_sta_cc : LED_PWM_FULL;
		else if (gh) d = dim ? (g_led_sta_cc >> 3) : (LED_PWM_PERIOD_US / LED_GHOST_FRAME_DIV);
		g_led_sta_duty[i] = (uint16_t)(LED_PWM_POL | d);
	}
	for (int i = 0; i < 4; i++) {
		const uint32_t m = 1u << track_leds[i].pin;
		const uint32_t on = (track_leds[i].port == NRF_P0) ? (g_led_p0_on & m) : (g_led_p1_on & m);
		const uint32_t gh = (track_leds[i].port == NRF_P0) ? (g_led_p0_ghost & m) : (g_led_p1_ghost & m);
		const uint32_t cc = g_led_trk_cc[i];
		uint32_t d = 0u;
		if (on)      d = (dim || g_led_trk_lvl[i] < 255u) ? cc : LED_PWM_FULL;
		else if (gh) d = dim ? (cc >> 3) : (cc / LED_GHOST_FRAME_DIV);
		g_led_trk_duty[i] = (uint16_t)(LED_PWM_POL | d);
	}
}
static void led_pwm_one(NRF_PWM_Type *pwm, const struct led *l, uint16_t *duty)
{
	pwm->ENABLE = 0;
	for (int i = 0; i < 4; i++)
		pwm->PSEL.OUT[i] = (l[i].pin & 31u) | ((l[i].port == NRF_P1) ? (1u << 5) : 0u);   /* port bit 5 */
	pwm->MODE       = PWM_MODE_UPDOWN_Up << PWM_MODE_UPDOWN_Pos;
	pwm->PRESCALER  = PWM_PRESCALER_PRESCALER_DIV_16 << PWM_PRESCALER_PRESCALER_Pos;   /* 1 MHz: 1 us ticks */
	pwm->COUNTERTOP = LED_PWM_PERIOD_US;                                                 /* 1 kHz frame, as before */
	pwm->LOOP       = 1u;
	pwm->DECODER    = (PWM_DECODER_LOAD_Individual << PWM_DECODER_LOAD_Pos) |
	                  (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
	pwm->SEQ[0].PTR = (uint32_t)duty; pwm->SEQ[0].CNT = 4; pwm->SEQ[0].REFRESH = 0; pwm->SEQ[0].ENDDELAY = 0;
	pwm->SEQ[1].PTR = (uint32_t)duty; pwm->SEQ[1].CNT = 4; pwm->SEQ[1].REFRESH = 0; pwm->SEQ[1].ENDDELAY = 0;
	pwm->SHORTS     = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;   /* forever: the buffer is re-read every period */
	pwm->ENABLE     = 1;
	pwm->TASKS_SEQSTART[0] = 1;
}
static void led_pwm_init(void)
{
	for (int li = 0; li < NUM_LEDS; li++) {
		if (leds[li].port == NRF_P0) g_led_sta_p0 |= (1u << leds[li].pin);
		else                         g_led_sta_p1 |= (1u << leds[li].pin);
	}
	for (int li = 0; li < NUM_TRACK_LEDS; li++) {
		if (track_leds[li].port == NRF_P0) g_led_trk_p0 |= (1u << track_leds[li].pin);
		else                               g_led_trk_p1 |= (1u << track_leds[li].pin);
	}
	led_hw_refresh();
	led_pwm_one(LED_PWM_STA, leds, g_led_sta_duty);
	led_pwm_one(LED_PWM_TRK, track_leds, g_led_trk_duty);
}

/* ---------- LED helpers ---------- */
static void led_cfg_output(const struct led *l)
{
	l->port->PIN_CNF[l->pin] =
		(GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)   |
		(GPIO_PIN_CNF_DRIVE_S0S1    << GPIO_PIN_CNF_DRIVE_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
}
static void led_on(int i)
{
	if (leds[i].port == NRF_P0) g_led_p0_on |= (1u << leds[i].pin);
	else                        g_led_p1_on |= (1u << leds[i].pin);
	led_hw_refresh();   /* LEDPWM-710 */
}
static void led_off(int i)
{
	if (leds[i].port == NRF_P0) g_led_p0_on &= ~(1u << leds[i].pin);
	else                        g_led_p1_on &= ~(1u << leds[i].pin);
	led_hw_refresh();
}
/* TG-551: set the STATUS row's PWM on-time directly. b is 0..255 of the
 * nominal LED_STATUS_ON_US window. SQUARED on the way in because
 * perceived brightness goes roughly as duty^(1/2.2) -- a linear ramp
 * appears to hang at the top and jump at the bottom. Clamped to at least
 * 1 us so a lit LED never goes fully dark mid-breath. */
static void status_level(uint32_t b)
{
	if (b > 255u) b = 255u;
	uint32_t cc = (LED_STATUS_ON_US * b * b) / (255u * 255u);
	if (cc < 1u) cc = 1u;
	if (cc > LED_PWM_PERIOD_US - 1u) cc = LED_PWM_PERIOD_US - 1u;
	g_led_sta_cc = cc;   /* LEDPWM-710 */
	led_hw_refresh();
}

/* STACKA-664 A6: a track LED's page LEVEL, 0..255 of the row's window (dim: the
 * 52 us window; full: the whole period). status_level()'s law -- SQUARED for
 * perceived brightness, clamped to >= 1 us so a lit pin never goes fully dark.
 * 255 = the row's normal behaviour. The ISR reads g_led_trk_lvl to decide
 * whether a full-mode pin may go dark at its compare. */
static void track_level(int i, uint32_t b)
{
	if (b > 255u) b = 255u;
	const uint32_t win = g_led_dim ? LED_PWM_ON_US : (LED_PWM_PERIOD_US - 1u);
	uint32_t cc = (win * b * b) / (255u * 255u);
	if (cc < 1u) cc = 1u;
	if (cc > LED_PWM_PERIOD_US - 1u) cc = LED_PWM_PERIOD_US - 1u;
	g_led_trk_lvl[i] = (uint8_t)b;
	g_led_trk_cc[i] = cc;   /* LEDPWM-710 */
	led_hw_refresh();
}
/* the page views set levels; this hands the row back once no page is showing */
static void track_level_rest(void)
{
	for (int i = 0; i < 4; i++)
		if (g_led_trk_lvl[i] != 255u) track_level(i, 255u);
}

static void led_ghost(int i)
{
	/* LED-549: the status row gains the GHOST (dim) duty the track
	 * row already had -- the third brightness the VU needs to GLOW
	 * rather than merely switch. */
	if (leds[i].port == NRF_P0) { g_led_p0_ghost |= (1u << leds[i].pin);
	                              g_led_p0_on &= ~(1u << leds[i].pin); }
	else                        { g_led_p1_ghost |= (1u << leds[i].pin);
	                              g_led_p1_on &= ~(1u << leds[i].pin); }
	led_hw_refresh();
}
static void led_clear(int i)
{
	/* LED-549: clears BOTH duties. led_off() leaves the ghost bit set,
	 * which is why the VU's dim bits survived into the song display. */
	if (leds[i].port == NRF_P0) { g_led_p0_on &= ~(1u << leds[i].pin);
	                              g_led_p0_ghost &= ~(1u << leds[i].pin); }
	else                        { g_led_p1_on &= ~(1u << leds[i].pin);
	                              g_led_p1_ghost &= ~(1u << leds[i].pin); }
	led_hw_refresh();
}
static void all_off(void)  { for (int i = 0; i < NUM_LEDS; i++) led_clear(i); }
/* Status row = song indicator, 16 songs via TWO LIGHTS ("scheme E", chosen
 * in the LED lab): the POSITION LED (song % 4) is SOLID, and the BANK LED
 * (song / 4) BLINKS ~2 Hz (250 ms on/off). When position == bank — songs 1,
 * 6, 11 and 16 — one LED carries both roles and simply BLINKS ~2 Hz: "only
 * one light, and it blinks" reads as position-and-bank-agree.
 * Read it as: "the steady light says where in the bank, the blinking light
 * says which bank." Pure function of (g_slot, uptime): no state, no
 * blocking, ~8 ms resolution. (Same LEDs the power on/off sweep uses.) */
static void show_song_leds(void)
{
	status_level(255u);   /* TG-551: restore the nominal row brightness */
	uint32_t slot = g_slot;                 /* volatile: read once */
	uint32_t pos  = slot & 3u;              /* slot % 4 */
	uint32_t bank = slot >> 2;              /* 0..3 */
	uint32_t t    = k_uptime_get_32();
	/* Bank-blink phase: fixed ~2 Hz normally; with a TAPPED GRID the blink
	 * locks to the beat (on for the first half of each beat, off for the
	 * second — 50% duty keeps "which bank" as readable as the 2 Hz square,
	 * unlike the brief 1/8-beat track pulses). The whole face keeps time. */
	int blink = ((t / 250u) & 1u) == 0u;
	if (g_grid_active && g_grid_beat_frames && g_playing && g_cur_speed_q16 >= 12288u) {
		/* LEDS-725 (marc 09-08): beat-locked only while the tape RUNS -- since
		 * GRIDLOCK-720 froze the grid on a pause the blink froze with it, and a
		 * song LED stuck on read as "two solid dots". Stopped = the 2 Hz square. */
		uint64_t ph = g_sample_clock - g_grid_anchor_e;
		blink = ((uint32_t)(ph % g_grid_beat_frames) <
		         g_grid_beat_frames / 2u);
	}
	for (int i = 0; i < NUM_LEDS; i++) {
		int on;
		/* LED-549 (marc): SOLID = BANK, BLINK = SONG. Blinking reads
		 * as the lighter of the two and a song is the lighter thing;
		 * the bank is the heavier, so it gets the steady light. When
		 * both land on one LED (bank 2, song 2) it BLINKS. */
		if ((uint32_t)i == pos && pos == bank)
			on = blink;                     /* both roles: BLINK */
		else if ((uint32_t)i == bank)
			on = 1;                         /* bank: SOLID */
		else if ((uint32_t)i == pos)
			on = blink;                     /* song: BLINK */
		else
			on = 0;
		on ? led_on(i) : led_clear(i);   /* LED-549 r6: clear the GHOST
		 * bit too. led_off() only clears "on", so after the VU had the
		 * row its dim bits survived and the bottom LEDs glowed faintly
		 * under the song display (marc saw it while paused). */
	}
}

static void show_vu_leds(void)
{
	status_level(255u);   /* TG-551: restore the nominal row brightness */
	/* LED-549 r4 (row 100, petercolombo; shape by marc): a stock-style
	 * VU on the status row while playing, filling BOTTOM-UP and glowing
	 * through the ghost duty. HOLD FUNCTION for the song indicators.
	 * r4 fixed two faults: the level index was anchored at the FLOOR
	 * and capped at half the bar (full-scale audio could not light the
	 * top LEDs -- an arithmetic ceiling, not a taste problem), and a
	 * 16 Hz ghost/on dither read as FLICKER. g_vu is now 0..12 with 12
	 * at 0 dBFS; each LED owns 3 steps: dark, GHOST, SOLID. */
	int v = (int)g_vu;
	for (int i = 0; i < NUM_LEDS; i++) {
		int li = NUM_LEDS - 1 - i;      /* BOTTOM-UP: last LED first */
		int t = v - i * 3;
		if (t <= 0)      led_clear(li);
		else if (t == 1) led_ghost(li);
		else             led_on(li);
	}
}

static void show_page_sweep(void)
{
	/* LED-549 r9 (marc): pages ANNOUNCE, then get out of the way. A
	 * persistent page light cost the song indicators for as long as the
	 * page stayed open -- which is most of the time. This borrows the
	 * M23 snap-sweep's vocabulary instead, and (r9, marc) it runs on the
	 * TRACK ROW: the row WALKS UP, BOUNCES BACK, and LANDS ON THE TRACK
	 * BUTTON THAT OPENED THE PAGE -- T2 for FX, T4 for MODE -- and then
	 * the page's own content takes the row over. The STATUS row is never
	 * touched, so the song indicators and the VU are never lost. The
	 * LANDING names the page, right where your finger already is.
	 * Two-phase counter so the landing cannot disturb the walk:
	 * 9..24 = walking (2 frames per LED: 1-2-3-4-3-2-1), 1..8 = landed
	 * and holding the page's LED, then done. */
	uint32_t tgt = ((g_pg_id >= 1u && g_pg_id <= NUM_TRACK_LEDS) ? g_pg_id : 1u) - 1u;
	uint32_t pos;
	if (g_pg_swmode) {
		/* LEDS-725 (marc 09-08): the COUNT sweep for a page reached by FN + VOL.
		 * 2 frames an LED: round one 1-2-3-4 (pages > 4) or 1..n, a 2-frame gap,
		 * round two 1..(n-4), and it ENDS on the last one -- no bounce, no landing. */
		uint32_t n = (g_pg_id >= 1u) ? (uint32_t)g_pg_id : 1u;
		uint32_t r1 = (n > 4u) ? 4u : n, r2 = (n > 4u) ? (n - 4u) : 0u;
		uint32_t total = 2u * r1 + (r2 ? 2u + 2u * r2 : 0u);
		uint32_t f = total - (uint32_t)g_pg_sweep;   /* frames elapsed */
		int p = -1;
		if (f < 2u * r1) p = (int)(f / 2u);
		else if (r2 && f >= 2u * r1 + 2u) p = (int)((f - 2u * r1 - 2u) / 2u);
		for (int i = 0; i < NUM_TRACK_LEDS; i++)
			(i == p) ? track_led_on(i) : track_led_off(i);
		g_pg_sweep--;
		return;
	}
	if (g_pg_sweep <= 8u) {
		pos = tgt;                        /* LANDED: hold, then release */
	} else {
		uint32_t step = (24u - (uint32_t)g_pg_sweep) / 2u;
		pos = (step <= 3u) ? step : ((step >= 6u) ? 0u : (6u - step));
		if (step >= 3u && pos == tgt) g_pg_sweep = 9u;   /* land next frame */
	}
	for (int i = 0; i < NUM_TRACK_LEDS; i++)
		((uint32_t)i == pos) ? track_led_on(i) : track_led_off(i);
	g_pg_sweep--;
}

/* LED-549 r11: the walk table the entry sweep uses, so the EXIT can replay
 * it. Step s = 0..6 -> LED 0,1,2,3,2,1,0; the entry LANDS on the first step
 * >= 3 whose LED is the page's, i.e. step (6 - tgt). */
static uint32_t pg_walk_pos(uint32_t s)
{
	return (s <= 3u) ? s : ((s >= 6u) ? 0u : (6u - s));
}

static void show_page_exit(void)
{
	/* LED-549 r11 (marc: "lets also do an exit animation"). The entry
	 * sweep PLAYED BACKWARDS: the row holds on the page's own button for
	 * a beat, then walks back out the way it came in and goes dark. Same
	 * vocabulary, mirrored -- closing costs nothing new to learn, and the
	 * track row is visibly HANDED BACK rather than just blinking off. */
	uint32_t tgt  = ((g_pg_id >= 1u && g_pg_id <= NUM_TRACK_LEDS) ? g_pg_id : 1u) - 1u;
	uint32_t land = 6u - tgt;                    /* the step the entry landed on */
	uint32_t f    = 24u - (uint32_t)g_pg_exit;   /* frames elapsed since the close */
	int pos;
	if (f < 8u) {
		pos = (int)tgt;                          /* HOLD where the entry landed */
	} else {
		uint32_t back = (f - 8u) / 2u;           /* 0..land, then done */
		pos = (back > land) ? -1 : (int)pg_walk_pos(land - back);
	}
	for (int i = 0; i < NUM_TRACK_LEDS; i++)
		(i == pos) ? track_led_on(i) : track_led_off(i);
	g_pg_exit--;
	if (pos < 0) g_pg_exit = 0;                  /* walked off the end: done */
}

/* Page-number count, in frames of the ~8 ms LED tick. */
/* r2 (marc: "dont fade too dark with the breathing and speed it up"). */
#define TG_BREATH    275u   /* ~2.2 s at the 8 ms LED tick (was 3.5) */
#define TG_BR_FLOOR  150u   /* of 255. After the gamma squaring this
                             * is ~62%% of perceived full brightness at
                             * the dimmest point, where 64 was ~28%%
                             * -- a breath, not a blink. */
#define PGN_HOLD       62u   /* a COUNTING group sits solid ~0.5 s */
#define PGN_LAST_HOLD 125u   /* LEDS-725 (marc 09-08): the LAST group sits solid ~1 s (LEDCYC-729: was ~2 s, "still hanging too long") ... */
#define PGN_LAST_FADE 100u   /* ... then FADES OUT over ~0.8 s (the row level, 255 -> 0) */
#define PGN_FADE        8u   /* fade OFF between groups, ~64 ms (marc: the
                              * fade-off speed is right as it is) */
#define PGN_RESET       0u   /* LEDCYC-729 (marc): the count loops the moment the row is dark (was ~1 s dark) */

static void show_page_number(void)
{
	/* LED-549 r11 (marc): while a page is open the status row COUNTS THE
	 * PAGE NUMBER instead of running the VU. Pages 5+ are real -- the four
	 * track buttons are BOOKMARKS into an ordered list and FN + VOL-/VOL+
	 * walks the tail (ROADMAP §1.4, request row 88) -- so four LEDs have to
	 * be able to say more than four.
	 *
	 * They count in GROUPS OF FOUR: page 6 = a group of 4, then a group of
	 * 2; page 10 = 4, 4, 2. Full groups pulse all the way to FULL
	 * brightness and go completely dark between them, so the groups are
	 * unmistakably separate events rather than one long shape.
	 *
	 * The LAST group only ever reaches GHOST and holds far longer: a
	 * PARTIAL fade means "this is the end of the count", whether that
	 * group holds four lights or two. It then BREATHES there -- the
	 * resting state, so a glance at any moment says which page you are in
	 * -- and after a few breaths the whole count LOOPS.
	 *
	 * Three honest brightness levels only (dark / ghost / solid). r5
	 * proved that dithering between ghost and solid to fake intermediate
	 * steps reads as FLICKER, so the "fade" is a slow walk through the
	 * three the hardware actually has. */
	uint32_t n     = (g_pg_id >= 1u) ? (uint32_t)g_pg_id : 1u;
	uint32_t last  = n % 4u;  if (last == 0u) last = 4u;
	uint32_t nfull = (n - last) / 4u;   /* whole groups of four before the last */

	uint32_t lit = 0u, bright = 0u;     /* bright: 0 dark, 1 ghost, 2 solid */

	if (nfull == 0u) {
		/* FOUR OR FEWER (r14, marc): no animation at all. The number fits
		 * the row, so the row simply IS the number. A light that does not
		 * move is the calmest thing to put above four faders, and it can
		 * never be mistaken for the VU or for a song blink. */
		lit = last; bright = 2u;
		/* TG-551: four or fewer still means "the row IS the number", but it
		 * now BREATHES instead of sitting flat. Phase 0 is MAXIMUM, so
		 * opening a page starts bright and breathes out, then in. ~3.5 s
		 * period; the floor keeps it clearly lit at its dimmest. */
		{
			if (g_mon_mute) {   /* INFX-672 / MUTEANY-738: MUTED -- inside a page the number BLINKS (~2 Hz) instead of breathing */
				status_level((((uint32_t)g_pg_cnt / 31u) & 1u) ? 255u : 40u);
				g_pg_cnt++;
			} else {
			uint32_t _t = (uint32_t)g_pg_cnt % TG_BREATH;
			uint32_t _h = TG_BREATH / 2u;
			uint32_t _tri = (_t < _h) ? (_h - _t) : (_t - _h);  /* max at 0 */
			status_level(TG_BR_FLOOR +
			             ((255u - TG_BR_FLOOR) * _tri) / _h);
			g_pg_cnt++;
			}
		}
	} else {
		/* MORE THAN FOUR: count it out. Each group of four sits solid for
		 * half a second and FADES OFF; the last group sits solid for five,
		 * fades off, and the row stays dark two seconds before the count
		 * starts again. The long hold and the long dark are what separate
		 * "the answer" from "still counting" -- no dimmer level, no pulse,
		 * nothing that competes with the FX page under your hands. */
		status_level(255u);   /* TG-551: the COUNT is read, not admired */
		uint32_t slot_n = PGN_HOLD + PGN_FADE;
		uint32_t cnt_len = nfull * slot_n;
		uint32_t cycle  = cnt_len + PGN_LAST_HOLD + PGN_LAST_FADE + PGN_RESET;
		uint32_t f      = (uint32_t)g_pg_cnt % cycle;

		if (f < cnt_len) {
			uint32_t k = f % slot_n;
			lit = 4u;
			bright = (k < PGN_HOLD) ? 2u
			       : ((k < PGN_HOLD + PGN_FADE / 2u) ? 1u : 0u);
		} else {
			uint32_t k = f - cnt_len;
			if (k < PGN_LAST_HOLD + PGN_LAST_FADE) {
				lit = last; bright = 2u;
				if (k >= PGN_LAST_HOLD)   /* LEDS-725: the row level walks 255 -> 0 over the fade */
					status_level(255u - (255u * (k - PGN_LAST_HOLD)) / PGN_LAST_FADE);
			}
			/* else: the reset gap -- everything stays dark */
		}
		g_pg_cnt++;
	}

	for (int i = 0; i < NUM_LEDS; i++) {
		/* r12 (marc): count from the TOP DOWN, the same direction the song
		 * indicators read. The VU fills bottom-up because it is a meter;
		 * this is a NUMBER, so it reads like the other numbers. */
		if ((uint32_t)i >= lit || bright == 0u) led_clear(i);
		else if (bright == 1u)                  led_ghost(i);
		else                                    led_on(i);
	}
}

static void track_led_on(int i)
{
	if (track_leds[i].port == NRF_P0) { g_led_p0_on |= (1u << track_leds[i].pin);
	                                    g_led_p0_ghost &= ~(1u << track_leds[i].pin); }
	else                              { g_led_p1_on |= (1u << track_leds[i].pin);
	                                    g_led_p1_ghost &= ~(1u << track_leds[i].pin); }
	led_hw_refresh();   /* LEDPWM-710 */
}
static void track_led_off(int i)
{
	if (track_leds[i].port == NRF_P0) { g_led_p0_on &= ~(1u << track_leds[i].pin);
	                                    g_led_p0_ghost &= ~(1u << track_leds[i].pin); }
	else                              { g_led_p1_on &= ~(1u << track_leds[i].pin);
	                                    g_led_p1_ghost &= ~(1u << track_leds[i].pin); }
	led_hw_refresh();
}
/* GHOST: barely-lit = this track HAS content but is muted (sleeping). The
 * fix for "muted and empty look identical" — community request. */
static void track_led_ghost(int i)
{
	if (track_leds[i].port == NRF_P0) { g_led_p0_ghost |= (1u << track_leds[i].pin);
	                                    g_led_p0_on &= ~(1u << track_leds[i].pin); }
	else                              { g_led_p1_ghost |= (1u << track_leds[i].pin);
	                                    g_led_p1_on &= ~(1u << track_leds[i].pin); }
	led_hw_refresh();
}
static void track_all_off(void)  { for (int i = 0; i < NUM_TRACK_LEDS; i++) track_led_off(i); }
/* STACKA-664 A6: a page lane's LED -- dark at 0, else lit at a brightness that
 * follows the amount with a 25 % floor (a barely-engaged effect is still
 * visibly ON). Bipolar lanes pass their distance from centre. */
static uint32_t bipolar_depth(uint32_t v)
{
	if (v >= 120u && v <= 136u) return 0u;
	return (v > 128u) ? (v - 128u) * 2u : (128u - v) * 2u;
}
/* STACKA-664 (marc): a track-button CYCLE answers with quick FLASHES that count the
 * new index (1 = the first type, 4 = OFF), then the LED goes back to showing the
 * lane's depth. ~80 ms on / ~80 ms off at the 8 ms LED tick; only the page views
 * read it, so it costs nothing anywhere else. */
static uint8_t g_led_fl_n[4], g_led_fl_t[4];   /* flashes left; ticks into the current phase */
static uint8_t g_led_fl_long[4];               /* LEDFLASH-791: this sequence is the OFF sign, one long blink */
#define LED_FL_TICKS 5u                         /* LEDFLASH-791: 40 ms phases (was 10 = 80 ms): a 3-count in ~240 ms */
#define LED_FL_LONG_TICKS 25u                   /* LEDFLASH-791: the OFF blink, 200 ms on */
static void led_flash_lane(int i, uint32_t count)
{
	if (i < 0 || i > 3) return;
	g_led_fl_n[i] = (uint8_t)((count ? count : 1u) * 2u);   /* on + off per flash; LEDFLASH-791: 0 = OFF = one long blink */
	g_led_fl_long[i] = count ? 0u : 1u;
	g_led_fl_t[i] = 0u;
}
static void page_led_depth(int i, uint32_t amt)
{
	if (g_led_fl_n[i]) {   /* the count overlay owns the LED until it is done */
		const int _on = (g_led_fl_n[i] & 1u) == 0u;   /* even = on phase first */
		if (_on) { track_led_on(i); track_level(i, 255u); } else { track_led_off(i); }
		if (++g_led_fl_t[i] >= ((g_led_fl_long[i] && _on) ? LED_FL_LONG_TICKS : LED_FL_TICKS)) { g_led_fl_t[i] = 0u; g_led_fl_n[i]--; }   /* LEDFLASH-791 */
		return;
	}
	if (amt > 255u) amt = 255u;
	if (amt == 0u) { track_led_off(i); track_level(i, 255u); return; }
	track_led_on(i);
	track_level(i, 64u + (amt * 191u) / 255u);
}

/* Clear BOTH LED rows. Used on power-off so nothing is left lit when SYSTEM_OFF
 * freezes the GPIO levels (the old power_off cleared only the status row, which
 * is exactly why the track/fader lights stayed on after powering down). */
static void shutdown_leds(void)
{
	all_off(); track_all_off();          /* clear the shadow */
	LED_PWM_STA->TASKS_STOP = 1; LED_PWM_TRK->TASKS_STOP = 1;   /* LEDPWM-710: stop the dimmers */
	LED_PWM_STA->ENABLE = 0;     LED_PWM_TRK->ENABLE = 0;       /* the pins are GPIO again */
	NRF_P0->OUTCLR = LED_ALL_P0;         /* force every LED pin low */
	NRF_P1->OUTCLR = LED_ALL_P1;
}

/* The single owner of the LEDs in normal running. Status row = song indicator.
 * Track row = per-track looper state (rec solid / armed blink / playing pulse),
 * OR — when no host audio is streaming AND nothing is recorded — a calm "standby"
 * chase so the device clearly reads as on-and-waiting instead of four dead LEDs.
 * As soon as a host streams audio or a loop exists, it falls through to state. */
static void led_service(void)
{
	/* INFX-672: does the input slot have work this pass? (routes change only
	 * by gesture, the wobble by a fader -- both land here within one pass) */
	g_in_on = (uint8_t)((g_pg_route[1] == RT_IN || g_pg_route[2] == RT_IN ||
	                     g_pg_route[3] == RT_IN || g_pg_route[4] == RT_IN) ? 1u : 0u);   /* WOBBUS-673: the wobble is page 4's, no slot term */
	/* The standby chase means "never used yet": it shows until the FIRST time a
	 * host streams audio (or anything is recorded) and then never returns. A
	 * live host-presence gate flickered the chase mid-session whenever the
	 * player closed the stream between songs / on pause. */

	/* LED-549: the status row has four jobs now, in priority order:
	 * a PAGE is open -> which page (+ announce) · FUNCTION held ->
	 * the song indicators (row 100's escape hatch) · PLAYING -> the
	 * VU meter · otherwise -> the song indicators. */
	/* INFX-672: a routing choice just made flashes the whole status row N times
	 * (1 INPUT, 2 TRACKS, 3 BOTH), page open or not; then the row returns. */
	int _has_trk = g_loop_active;                     /* LEDSONG-674: any loaded/armed/taking track */
	for (int _i = 0; _i < NTRK; _i++) if (trk[_i].state != TS_EMPTY) _has_trk = 1;
	if (g_usb_streaming) _has_trk = 1;   /* LEDSONG2-679: a live input is content for the VU */
	if (g_rt_flash) {
		uint32_t _ph = g_rt_tick / 8u;              /* ~64 ms on, ~64 ms off */
		int _on = (_ph & 1u) == 0u;
		if (++g_rt_tick >= 16u) { g_rt_tick = 0u; g_rt_flash--; }
		status_level(255u);
		for (int i = 0; i < NUM_LEDS; i++) { if (_on) led_on(i); else led_clear(i); }
	} else if (g_mt_flash) {   /* MUTEFIX-739: the mute's word is a SWEEP, one LED at a time (never the row dark):
		                            * on (1) = down 4-3-2-1, off (2) = up 1-2-3-4, ~100 ms a step */
		uint32_t _st = g_mt_tick / 12u;             /* 0..3 */
		int _lit = (g_mt_flash == 1u) ? (int)(NUM_LEDS - 1u - _st) : (int)_st;
		if (++g_mt_tick >= 12u * NUM_LEDS) { g_mt_tick = 0u; g_mt_flash = 0u; }
		status_level(255u);
		for (int i = 0; i < NUM_LEDS; i++) { if (i == _lit) led_on(i); else led_clear(i); }
	} else
	if (g_pg_open)            show_page_number();   /* LED-549 r11; LEDS-725: at once, FN held or not (a page walked with FN + VOL showed its number only on the release) */
	else if (g_fn_held)       show_song_leds();
	else if (g_playing && _has_trk) show_vu_leds();   /* LEDSONG-674: an EMPTY song playing shows the song, not an empty VU */
	else                      show_song_leds();

	int active = g_loop_active;
	for (int i = 0; i < NTRK; i++)
		if (trk[i].state != TS_EMPTY) active = 1;

	if (!(g_pg_open && g_pg_id >= 1u && g_pg_id <= 7u) || g_pg_sweep || g_pg_exit)
		track_level_rest();   /* STACKA-664 A6: only the FX pages set levels (EQ-691: and page 5) */
	if (g_pg_sweep) {
		show_page_sweep();   /* LED-549 r9: the activation sweep owns the
		                      * track row until it lands on the page's own
		                      * button; then the page content takes over. */
	} else if (g_pg_exit) {
		show_page_exit();    /* LED-549 r11: and hands it back on close */
	} else if (g_pg_open && g_pg_id == 2u) {
		/* LAYOUT-562: PAGE 2 VIEW -- bitcrush / ring mod / auto-wah / [echo]. */
		const uint32_t _lv[4] = { g_bcr_amt, g_rng_amt, g_awh_amt, (g_ec_div >= 3u) ? 0u : (uint32_t)g_ec_mix };   /* A6: depth; OFF dark */
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, (g_sec_led == (uint8_t)(i + 1)) ? (uint32_t)(g_sec_led2 ? g_sec2[(g_pg_id - 1u) & 3u][i] : g_sec[(g_pg_id - 1u) & 3u][i]) : _lv[i]);   /* SEC-695 / SHAPE-696 */
	} else if (g_pg_open && g_pg_id == 1u) {
		/* FXP-547: THE FX PAGE VIEW -- one LED per effect, lit when
		 * that effect is ENGAGED (away from neutral). Filter only
		 * this rung; T2-T4 stay dark until their kernels land. */
		/* A6: depth. The filter is bipolar with the 112..143 bypass band; the gate
		 * (r21) is dark unless a pattern is selected -- a lit LED for a bypassed
		 * effect is a lie. */
		const uint32_t _fl = (g_flt_pos < 112u) ? (uint32_t)(128u - g_flt_pos) * 2u
		                   : (g_flt_pos > 143u) ? (uint32_t)(g_flt_pos - 128u) * 2u : 0u;
		const uint32_t _lv[4] = { _fl, g_chr_mix, (g_dst_typ >= 3u) ? 0u : (uint32_t)g_dst_amt, (g_gat_pat < 2u) ? (uint32_t)g_gat_amt : 0u };
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, (g_sec_led == (uint8_t)(i + 1)) ? (uint32_t)(g_sec_led2 ? g_sec2[(g_pg_id - 1u) & 3u][i] : g_sec[(g_pg_id - 1u) & 3u][i]) : _lv[i]);   /* SEC-695 / SHAPE-696 */
	} else if (g_pg_open && g_pg_id == 3u) {
		/* LAYOUT-562: PAGE 3 VIEW -- phaser / sweep / tremolo / [empty]. */
		const uint32_t _lv[4] = { (g_lfo_div[0] >= 3u) ? 0u : (uint32_t)g_phs_amt, (g_lfo_div[1] >= 3u) ? 0u : (uint32_t)g_swp_amt, (g_lfo_div[2] >= 3u) ? 0u : (uint32_t)g_trm_amt, (uint32_t)g_rv_mix };   /* A6: depth; OFF dark; fader 4 = reverb (REVERB-676) */
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, (g_sec_led == (uint8_t)(i + 1)) ? (uint32_t)(g_sec_led2 ? g_sec2[(g_pg_id - 1u) & 3u][i] : g_sec[(g_pg_id - 1u) & 3u][i]) : _lv[i]);   /* SEC-695 / SHAPE-696 */
	} else if (g_pg_open && g_pg_id == 4u) {
		/* TAPE-569: PAGE 4 VIEW -- drive / tone / hiss / wobble. */
		/* A6: depth; tone (A1) is bipolar with a 120..136 deadband; the hiss is 655's */
		const uint32_t _lv[4] = { g_tp_drive, bipolar_depth(g_tp_tone), bipolar_depth(g_tp_hiss), g_tp_wob };   /* HISS2-701 */
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, (g_sec_led == (uint8_t)(i + 1)) ? (uint32_t)(g_sec_led2 ? g_sec2[(g_pg_id - 1u) & 3u][i] : g_sec[(g_pg_id - 1u) & 3u][i]) : _lv[i]);   /* SEC-695 / SHAPE-696 */
	} else if (g_pg_open && g_pg_id == 5u) {
		/* EQ-691: PAGE 5 VIEW -- four bands, LED = |gain| (bipolar, A1's deadband). */
		const uint32_t _lv[4] = { bipolar_depth(g_eq_g[0]), bipolar_depth(g_eq_g[1]), bipolar_depth(g_eq_g[2]), bipolar_depth(g_eq_g[3]) };
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, (g_sec_led == (uint8_t)(i + 1)) ? (uint32_t)(g_sec_led2 ? g_sec2[(g_pg_id - 1u) & 3u][i] : g_sec[(g_pg_id - 1u) & 3u][i]) : _lv[i]);   /* SEC-695 / SHAPE-696 */
	} else if (g_pg_open && g_pg_id == 6u) {
		/* PLACE-715: PAGE 6 VIEW -- LED = distance from centre (bipolar, the hiss / EQ deadband). */
		for (int i = 0; i < NUM_TRACK_LEDS; i++) page_led_depth(i, bipolar_depth(place_get(i)));
	} else if (g_pg_open && g_pg_id == 7u) {
		/* PAGE7V-718: PAGE 7 VIEW -- time, not fader depth.
		 *  hold TN      : that lane = |nudge|, the others dark;
		 *  preset take  : a countdown -- lit = units (bars / base loops) to go, <= 4,
		 *                 the last one blinking on the beat;
		 *  otherwise    : the 1-2-3-4 chase off the EFFECTIVE anchor (fader 1 moves
		 *                 it), nudged lanes glow dim between beats; no grid = dark. */
		const int _rt7 = g_rec_track;
		const uint32_t _ta7 = g_take_auto_at;
		const uint8_t _pn7 = (g_slot < NUM_SLOTS) ? g_take_preset[g_slot] : 0u;
		int _gb7 = -1, _ob7 = 0;
		if (g_grid_active && g_grid_beat_frames) {
			const uint64_t _ph7 = g_sample_clock - g_grid_anchor_e;
			_gb7 = (int)((_ph7 / g_grid_beat_frames) & 3u);
			_ob7 = ((uint32_t)(_ph7 % g_grid_beat_frames) < g_grid_beat_frames / 8u);
		}
		if (g_sec_led) {
			for (int i = 0; i < NUM_TRACK_LEDS; i++)
				page_led_depth(i, (g_sec_led == (uint8_t)(i + 1) && g_slot < NUM_SLOTS)
				                  ? bipolar_depth(g_trk_nudge[g_slot][i] ? g_trk_nudge[g_slot][i] : 128u) : 0u);
		} else if (_rt7 >= 0 && _rt7 < NTRK && trk[_rt7].state == TS_REC && _ta7 && _pn7) {
			const uint32_t _rc7 = trk[_rt7].rec_count;
			const uint32_t _unit = _ta7 / _pn7;
			uint32_t _left = (_rc7 < _ta7 && _unit) ? ((_ta7 - _rc7) + _unit - 1u) / _unit : 0u;
			if (_left > 4u) _left = 4u;
			const uint32_t _bl = (k_uptime_get_32() >> 7) & 1u;   /* the last unit blinks */
			for (int i = 0; i < NUM_TRACK_LEDS; i++)
				page_led_depth(i, ((uint32_t)i < _left) ? ((_left == 1u && !_bl) ? 0u : 255u) : 0u);
		} else {
			for (int i = 0; i < NUM_TRACK_LEDS; i++) {
				const uint8_t _nq = (g_slot < NUM_SLOTS) ? g_trk_nudge[g_slot][i] : 0u;
				const uint32_t _dim = (_nq && _nq != 128u) ? 40u : 0u;
				page_led_depth(i, (i == _gb7 && _ob7) ? 255u : _dim);
			}
		}
	} else if (g_pg_open && g_pg_id == 8u) {
		/* PG-533/LS-534: the MODE PAGE view -- each track LED shows
		 * its NEXT-record mode live: BLINK = stereo, SOLID = mono
		 * (marc's call: mono is the special state, it gets the
		 * steady light). */
		uint32_t _ph = (k_uptime_get_32() >> 7) & 1u;
		for (int i = 0; i < NUM_TRACK_LEDS; i++) {
			uint8_t _on = trk[i].p16m_next ? 1u : (uint8_t)_ph;
			if (_on) track_led_on(i); else track_led_off(i);
		}
	} else if (g_snap_sweep) {
		/* M23-r5 THE HOOK. A one-shot nudge deserves a one-shot picture:
		 * the row sweeps BACK AND FORTH — hunting, not yet sure — and
		 * then catches the beat and rides it BACKWARD, hand-in-hand,
		 * before letting go. It reads as "found it, locked, done", and
		 * because nothing persists afterwards there is no mode to
		 * explain. Sits above standby and the metronome: those own the
		 * whole row unconditionally and would overwrite it. */
		/* It sweeps, then it HUNTS FOR THE BEAT AND STOPS ON IT. The
		 * row bounces 1-2-3-4-3-2-1-2... off a single index, two
		 * frames per LED so the turnaround is not a stall (r8 split
		 * it in two and the shared end LED held double). From the
		 * second bounce on, every frame asks whether the walking LED
		 * is the one the grid is on RIGHT NOW, and the first time it
		 * is, the sweep ends there. led_service falls straight
		 * through to the metronome, already lit on that same LED, so
		 * there is no jump: the hunt catches the beat and the beat
		 * carries on. The walker steps ~9x faster than the beat, so a
		 * catch inside one bounce is certain; 48 is only a floor. */
		uint32_t sstep = (uint32_t)(48u - g_snap_sweep) / 2u;
		uint32_t sp_   = sstep % 6u;
		uint32_t lit   = (sp_ <= 3u) ? sp_ : (6u - sp_);
		int sgb = -1;
		if (g_grid_active && g_grid_beat_frames)
			sgb = (int)(((g_sample_clock - g_grid_anchor_e) /
				g_grid_beat_frames) & 3u);
		for (int i = 0; i < NUM_TRACK_LEDS; i++)
			((uint32_t)i == lit) ? track_led_on(i) : track_led_off(i);
		if (sstep >= 6u && sgb >= 0 && (uint32_t)sgb == lit) g_snap_sweep = 0;
		else g_snap_sweep--;
	} else if (g_led_shrug) {
		/* M25-r12 THE SHRUG, HOISTED. It used to be handled INSIDE the
		 * per-track LED loop, which only runs once something is
		 * recorded or audio has streamed. But the snap declines while
		 * you are still SETTING UP a grid — nothing recorded, no input
		 * — so the standby chase owned the row, the shrug was never
		 * drawn, and it never decremented either. It sat at 20 until
		 * the first track was armed and then drained all at once at
		 * the ~8 ms released cadence: a 15 Hz flicker arriving long
		 * after the gesture that caused it. This is EXACTLY the bug
		 * the snap sweep had in M23, hoisted for exactly the same
		 * reason: anything that answers a GESTURE has to outrank the
		 * ambient displays, because the gesture can happen while they
		 * own the row. (The copy still inside the per-track loop is
		 * now unreachable and harmless; the bounce's shrug comes
		 * through here too and gets the same guarantee.) */
		uint32_t son = (g_led_shrug >> 2) & 1u;
		for (int i = 0; i < NUM_TRACK_LEDS; i++)
			son ? track_led_on(i) : track_led_off(i);
		g_led_shrug--;
	} else {
		/* LEDIDLE-685: the standby chase is gone -- an empty, ungridded song shows a DARK
		 * track row (the status row carries the song); a gridded one shows its metronome. */
		int on_beat = (g_beat_phase < (BEAT_SAMPLES_L / 8u));
		int gbeat = -1;            /* tapped grid: beat 0..3 within the bar */
		if (g_grid_active && g_grid_beat_frames) {
			uint64_t ph = g_sample_clock - g_grid_anchor_e;
			uint32_t bf = g_grid_beat_frames;
			gbeat  = (int)((ph / bf) & 3u);
			on_beat = ((uint32_t)(ph % bf) < bf / 8u);  /* grid outranks
			                                             * the take beat */
		}
		int loaded = 0;
		for (int i = 0; i < NUM_TRACK_LEDS; i++)
			if (trk[i].state != TS_EMPTY) loaded = 1;
		if (gbeat >= 0 && !loaded) {
			/* gridded song, nothing recorded yet: 1-2-3-4 metronome
			 * chase (downbeat = LED 1) — the tapped grid made visible.
 */
			for (int i = 0; i < NUM_TRACK_LEDS; i++)
				((i == gbeat) && on_beat) ? track_led_on(i)
				                          : track_led_off(i);
		} else for (int i = 0; i < NUM_TRACK_LEDS; i++) {
			uint8_t st = trk[i].state;
			if (g_led_shrug) {   /* M19b: the "no" — all four double-blink */
				(((g_led_shrug >> 2) & 1u) ? track_led_on(i)
				                           : track_led_off(i));
				if (i == NUM_TRACK_LEDS - 1) g_led_shrug--;
				continue;
			}
			if (st == TS_REC && trk[i].rec_target && !trk[i].rec_silence &&
			    g_grid_active && g_grid_beat_frames) {
				/* grid run-on ("finishing the beat"): double-blink so
				 * continued recording reads deliberate, not stuck */
				uint64_t ph3 = g_sample_clock - g_grid_anchor_e;
				uint32_t hb2 = g_grid_beat_frames / 2u;
				((hb2 && (uint32_t)(ph3 % hb2) < hb2 / 4u)
					? track_led_on(i) : track_led_off(i));
			}
			else if (st == TS_REC || st == TS_DONE) track_led_on(i);
			else if (st == TS_ARMED) {
				int ab = on_beat;
				if (g_grid_punch_at && g_grid_active && g_grid_beat_frames) {
					/* waiting for the punch-in: blink at HALF-beat
					 * rate — clearly alive, clearly on purpose */
					uint64_t ph2 = g_sample_clock - g_grid_anchor_e;
					uint32_t hb = g_grid_beat_frames / 2u;
					if (hb) ab = ((uint32_t)(ph2 % hb) < hb / 4u);
				}
				(ab ? track_led_on(i) : track_led_off(i));
			}
			else if ((st == TS_PLAY || head_active(i)) && !trk[i].muted && !g_playing)
				track_led_on(i);   /* stopped: content reads solid, not
				                    * frozen-dark like an empty track */
			else if ((st == TS_PLAY || head_active(i)) && !trk[i].muted) {
				/* M12 (community ask): PER-TRACK WRAP PULSES when there
				 * is no grid. All four playing lights used to pulse in
				 * unison off one beat clock — four LEDs, one bit. Now
				 * each light pulses as ITS OWN loop wraps (chop-aware:
				 * the audible cycle is len/div in both modes), so
				 * different-length loops literally paint their
				 * polyrhythm on the panel. Gridded songs keep the
				 * shared grid pulse — there the point IS the one clock.
				 * Long loops get a capped ~2-beat flash at each wrap
				 * instead of a 1/8-duty minute-long glow. */
				int tp = on_beat;
				if (!g_grid_active || heads_engaged()) {
					/* M13: heads pulse against the SOURCE loop, each
					 * offset a quarter — the four lights chase in
					 * canon, matching what you hear. Heads ENGAGED
					 * overrides the gridded shared pulse too: in
					 * heads mode the canon is the clock. */
					struct looptrk *hs2 = head_active(i) ? &trk[g_head_src]
					                                     : &trk[i];
					uint32_t gb2 = hs2->len_blocks ? hs2->len_blocks
						     : (g_loop_blocks ? g_loop_blocks : 1u);
					uint32_t wper2, win2, wb2, cyc2;   /* CHOPCORE-805: the light marks the AUDIBLE wrap -- the real tile, */
					chop_tile(gb2, TSPB(hs2), &wper2, &win2, &wb2, &cyc2);   /* fixed mode included, which the old ad-hoc cyc ignored */
					uint32_t ho2 = heads_engaged()
					             ? (((uint32_t)g_head_pos[i] * cyc2) >> 8) : 0u;
					uint32_t pwb2 = (uint32_t)(g_consume_pos / TSPB(hs2));   /* CHOPCORE-805: the track's own blocks, not a fixed 280 */
					uint32_t c2 = (chop_phase(head_active(i) ? (int)g_head_src : i, hs2->start_blk,
								  pwb2, gb2, cyc2, TSPB(hs2)) + ho2) % cyc2;   /* NUDGE-717 / CHOPANCHOR-805 */
					{
						int rv2 = g_win_rev ? 1 : 0;
						if (g_head_rev[i]) rv2 ^= 1;   /* REV2-641 */
						if (rv2)
							c2 = (cyc2 - 1u) - c2;   /* chase walks back */
					}
					uint32_t onw = cyc2 / 8u;
					if (onw < 1u) onw = 1u;
					if (onw > 280u) onw = 280u;   /* ~2 beats */
					tp = (c2 < onw);
				}
				/* M19b-r2: a HOLLOW head (nothing underneath) chases
				 * at GHOST intensity — faint = printable, so you can
				 * see the bounce targets mid-performance without
				 * remembering the song from before entry. Consistent
				 * with dark = empty outside heads mode. */
				if (head_active(i) && trk[i].state == TS_EMPTY &&
				    !(g_slot < NUM_SLOTS &&
				      g_meta.slot[g_slot].present[i]))
					(tp ? track_led_ghost(i) : track_led_off(i));
				else
					(tp ? track_led_on(i) : track_led_off(i));
			}
			else if ((st == TS_PLAY || head_active(i)) && trk[i].muted)
				track_led_ghost(i);
			else                                    track_led_off(i);
		}
	}
}

/* FN+PLAY mode toggle (v1.2.2: fires on PLAY RELEASE, 0.7-5 s of hold —
 * holding through 5 s becomes the brightness toggle instead). M7c two-layer
 * semantics + the LED confirm, verbatim from the old in-hold body. */
static void feed_wdt(void);
/* REV2-641 (W308): flip track ti's playback direction, in any mode -- the
 * heads double-tap's mechanics minus the mute toggle. The block path reads
 * hrev = g_head_rev[i] ^ g_win_rev; the 3-block dip masks the splice and the
 * ring is re-anchored to the playhead so the streamer refills it in the new
 * direction on its next pass (the audio thread sees a short starve behind
 * the dip, exactly as the heads scrub does). An empty track has no
 * direction; a track that is taking keeps the flag for the playback that
 * follows (the recorder writes forward by construction, row 59). */
static void __attribute__((noinline)) rev_toggle(int ti)
{
	pop_stamp(2u);   /* POPLOG-730 */
	if (ti < 0 || ti >= NTRK || (trk[ti].state == TS_EMPTY && !head_active(ti))) return;   /* ISO2-643: a head counts */
	g_head_rev[ti] = (uint8_t)!g_head_rev[ti];
	if (trk[ti].state == TS_PLAY || head_active(ti)) {
		g_head_blip[ti] = 3;
		trk[ti].p_w = (g_consume_pos / TSPB_SRC(ti)) * TSPB_SRC(ti);   /* HG-646 */
	}
}

/* ISO2-643 (§1.3): momentary ISOLATE. Only track ti audible while the chord is
 * held; the four mute flags are snapshotted at engage and restored at release.
 * Session state: the persisted mute bits (g_meta.song_mode) are never touched,
 * so a song comes back exactly as it was saved. Only tracks the mixer would
 * serve are muted (state == TS_PLAY || head_active -- its own predicate); the
 * snapshot covers all four so one that starts playing during the hold is
 * restored to what it was. */
static uint8_t g_iso_on, g_iso_mask;
static void __attribute__((noinline)) iso_engage(int ti)
{
	pop_stamp(3u);   /* POPLOG-730 */
	if (g_iso_on || ti < 0 || ti >= NTRK || (trk[ti].state == TS_EMPTY && !head_active(ti))) return;
	g_iso_mask = 0;
	for (int k = 0; k < NTRK; k++) {
		if (trk[k].muted) g_iso_mask |= (uint8_t)(1u << k);
		if (k == ti) trk[k].muted = 0;
		else if (trk[k].state == TS_PLAY || head_active(k)) trk[k].muted = 1;
	}
	g_iso_on = 1;
}
static void __attribute__((noinline)) iso_release(void)
{
	pop_stamp(4u);   /* POPLOG-730 */
	if (!g_iso_on) return;
	for (int k = 0; k < NTRK; k++) trk[k].muted = (uint8_t)((g_iso_mask >> k) & 1u);
	g_iso_on = 0;
}

/* STACKT-716: the preset take length in RECORDED samples for the take about to start, 0 = none.
 * Variable mode counts BARS of the grid (no grid: nothing to count); fixed mode counts BASE LOOPS
 * (the first take has none). Recording follows the tape, as the punch derives it (rs = nf * speed). */
static uint32_t take_preset_samps(void)
{
	if (g_slot >= NUM_SLOTS) return 0u;
	const uint32_t n = g_take_preset[g_slot];
	if (!n) return 0u;
	uint64_t t;
	if (g_fixed_len) {
		if (!g_loop_len) return 0u;
		t = (uint64_t)g_loop_len * n;
	} else {
		if (!g_grid_active || !g_grid_beat_frames) return 0u;
		const uint64_t rs = ((uint64_t)g_grid_beat_frames * g_cur_speed_q16) >> 16;
		if (!rs) return 0u;
		t = rs * 4u * n;
	}
	return (t == 0u || t >= (uint64_t)MAX_LOOP_SAMPLES) ? 0u : (uint32_t)t;
}
/* STACKT-716: the state part of the FN+PLAY toggle, without its LED show (page 7's T1 tap). */
static void mode_toggle_core(void)
{
	g_fixed_len ^= 1u;
	{
		int has = 0;
		for (int k = 0; k < NTRK; k++)
			if (trk[k].state != TS_EMPTY ||
			    (g_slot < NUM_SLOTS &&
			     g_meta.slot[g_slot].present[k]))
				has = 1;
		if (has && g_slot < NUM_SLOTS) {
			g_meta.song_mode[g_slot] = (uint8_t)
				((g_meta.song_mode[g_slot] & 0xF0u) |
				 (g_fixed_len ? 2u : 1u));
		} else {
			g_mode_pref = g_fixed_len;
			g_meta.fixed_len = g_fixed_len;
		}
	}
	g_meta_save_req = 1;
}
static void fnp_mode_toggle(void)
{
	mode_toggle_core();
	all_off(); track_all_off();
	if (g_fixed_len) {
		for (int r = 0; r < 2; r++) {
			for (int i = 0; i < NUM_LEDS; i++) led_on(i);
			feed_wdt(); k_msleep(150);
			for (int i = 0; i < NUM_LEDS; i++) led_off(i);
			feed_wdt(); k_msleep(120);
		}
	} else {
		for (int i = 0; i < NUM_LEDS; i++) {
			led_on(i); feed_wdt(); k_msleep(110); led_off(i);
		}
		for (int i = NUM_LEDS - 2; i >= 0; i--) {
			led_on(i); feed_wdt(); k_msleep(90); led_off(i);
		}
	}
	all_off();
}

/* ---------- watchdog ---------- */
static void feed_wdt(void)
{
	for (int ch = 0; ch < 8; ch++)
		NRF_WDT->RR[ch] = WDT_RR_RR_Reload;
}

/* ---------- power button ---------- */
static bool pwr_pressed(void)
{
	return (PWR_PORT->IN & (1u << PWR_PIN)) == 0u;   /* low = pressed */
}

static void pwr_btn_cfg_input(void)
{
	PWR_PORT->PIN_CNF[PWR_PIN] =
		(GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)  |
		(GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
}

/* arm the button to wake the chip out of SYSTEM_OFF (sense the low level) */
static void pwr_btn_arm_wake(void)
{
	PWR_PORT->PIN_CNF[PWR_PIN] =
		(GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)  |
		(GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)|
		(GPIO_PIN_CNF_SENSE_Low     << GPIO_PIN_CNF_SENSE_Pos);
}

/* ========================================================================
 *  POWER / PERSISTENCE  —  battery charger control, the graceful
 *  stop_and_flush() (finalize any take, then flush the card's volatile write
 *  cache so loops + the slot index survive a power cut), power_off() ->
 *  SYSTEM_OFF (clean return to the bootloader; there is no reset pin),
 *  enter_dfu() (a track combo forces the bootloader for reflashing), and
 *  song-slot switching.
 * ======================================================================== */
/* ---------- battery charger ---------- */
/* Explicitly enable charging by driving the BQ24232 /CE pin low, and set the
 * two status pins as inputs with pull-ups (they are open-drain on the charger). */
static void charger_init(void)
{
	BQ_PORT->PIN_CNF[BQ_NCHG_PIN] =
		(GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)  |
		(GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
	BQ_PORT->PIN_CNF[BQ_NPGOOD_PIN] =
		(GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos)  |
		(GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);

	BQ_PORT->OUTCLR = (1u << BQ_NCE_PIN);          /* drive low first  */
	BQ_PORT->PIN_CNF[BQ_NCE_PIN] =
		(GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)  |
		(GPIO_PIN_CNF_DRIVE_S0S1    << GPIO_PIN_CNF_DRIVE_Pos)|
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
	BQ_PORT->OUTCLR = (1u << BQ_NCE_PIN);          /* /CE low = charge enabled */
}

/* BQ24232 status (per the SP-1-dev wiki): open-drain, LOW = active */
static bool usb_present(void)
{
	return (BQ_PORT->IN & (1u << BQ_NPGOOD_PIN)) == 0u;   /* low = USB power good */
}
static bool charging(void)
{
	return (BQ_PORT->IN & (1u << BQ_NCHG_PIN)) == 0u;     /* low = charging */
}

/* ---------- graceful stop before power-off / DFU ----------
 * If a take is mid-record, end it and give the streamer a bounded window to
 * flush the rec ring and persist the song metadata, so powering off or dropping
 * into the bootloader can't lose the loop or its saved BPM/length. WDT-fed. */
static void stop_and_flush(void)
{
	g_stop_req = 1;                       /* finalize any in-progress take */
	/* M39: stamp the live tape speed into the song index before the final
	 * flush. The only other copy happens on song SWITCH, so a tempo set
	 * and powered straight off reverted to the last saved value — while
	 * switching songs first made it stick (confirmed on hardware, both
	 * ways). Every save trigger (take end, delete, brightness) wrote a
	 * stale slot speed. The busy wait below already blocks until
	 * g_meta_save_req is serviced, so this is one index write at
	 * power-off and nothing anywhere else. */
	if (g_meta_loaded && g_slot < NUM_SLOTS &&
	    g_meta.slot[g_slot].speed_q16 != g_play_speed_q16) {
		g_meta.slot[g_slot].speed_q16 = g_play_speed_q16;
		g_meta_save_req = 1;
	}
	for (int i = 0; i < 300; i++) {      /* bounded ~3 s (WDT is 4 s, fed each pass) */
		feed_wdt();
		int busy = (g_rec_track >= 0) || g_meta_save_req;
		for (int t = 0; t < NTRK; t++)
			if (trk[t].state == TS_REC || trk[t].state == TS_DONE) busy = 1;
		if (!busy) break;
		k_msleep(10);
	}
	/* Now flush the card's volatile write cache so the just-finished take + the
	 * slot index are durable across the power cut. The recording is finalized and
	 * we're shutting down, so the bus-blocking flush has nothing live to starve.
	 * The streamer (only eMMC user) does it; we wait, feeding the WDT. */
	if (g_cache_on) {
		g_cache_flush_req = 1;
		for (int i = 0; i < 1000 && g_cache_flush_req; i++) {  /* bounded ~10 s (flush itself is allowed 8 s) */
			feed_wdt();
			k_msleep(10);
		}
	}
}

/* ---------- power off ---------- */
static void power_off(void)
{
	/* PAD-594 (W287): THE MIXER'S 32-BYTE LINE, WITHOUT aligned().
	 * Zephyr links with --sort-section=alignment, so aligned(32) on the
	 * mixer does not pad it in place -- it PULLS it to the front of flash
	 * next to the other pinned function (593: mixer landed +544 B after
	 * the unpack, the 574 shape). Within the normal 4-byte class the
	 * order is GCC's emission order, and power_off is emitted just before
	 * looper_audio_block. So: the build script measures the mixer's
	 * address and sets this nop count so it lands on mod32 == 0. The nops
	 * execute once, at power-off. 592 needs 0 of them. */
	/* PADHOST-715: the nop pad moved to tempo_refine (this build's between-function) */
	g_off_fade = 1;                      /* M10: fade the outputs (~85 ms) so the
	                                      * codecs power down on silence — the
	                                      * fade completes during the flush and
	                                      * LED sweep below */
	stop_and_flush();                    /* never lose an in-progress recording */

	/* shutdown sweep across BOTH rows, then force EVERY LED dark before
	 * SYSTEM_OFF latches the GPIO levels. Clearing BOTH rows is the fix for the
	 * track/fader lights staying lit after power-off (the old code cleared only
	 * the status row, so the track row froze on into sleep). */
	for (int i = NUM_LEDS - 1; i >= 0; i--) {
		led_off(i); track_led_off(i);
		feed_wdt();
		k_msleep(80);
	}
	shutdown_leds();

	/* Wait for the finger to come off the button first, otherwise the
	 * level-sense we are about to arm would instantly wake us again. */
	while (pwr_pressed()) {
		feed_wdt();
		k_msleep(20);
	}
	k_msleep(60);             /* debounce the release */

	shutdown_leds();          /* re-assert dark immediately before sleep */

	/* POWER DOWN THE EXTERNAL CHIPS. SYSTEM_OFF only stops the nRF — the
	 * speaker amp, the headphone codec and the eMMC I/O rail are separate
	 * chips, and the retained GPIO levels would otherwise keep them powered
	 * for days: the "battery drains overnight" reports. A powered amp whose
	 * clock has been removed can also murmur on its own — the "sound after
	 * shutdown" reports. Order: amp first, then codec, then the flash rail
	 * (its cache was flushed in stop_and_flush above). */
	/* M10: the mix has been silent for a while (fade above); now put both
	 * output stages in their own MUTE — registers our init already uses —
	 * so the drivers discharge quietly instead of stepping to ground
	 * (the power-off pop, user report). */
	(void)cs42_wr16(0x2001, 0x0D);   /* CS42L42 HP Control: mute all */
	tas_page(0x01);
	(void)tas_wr(0x30, 0x00);        /* TAS2505 Class-D driver: mute (P1/R48) */
	feed_wdt();
	k_msleep(30);
	tas_page(0x00);
	(void)tas_wr(0x01, 0x01);        /* TAS2505 software reset: every block
	                                  * back to its powered-down default */
	gpio_drive_low(CS42_RST_PORT, CS42_RST_PIN);   /* CS42L42 held in reset */
	emmc_power_down();               /* bus pins released, VCCQ rail off */

	gpio_drive_low(OSC_EN_PORT, OSC_EN_PIN);   /* osc off: it would otherwise
	                              keep drawing battery through SYSTEM_OFF */
	pwr_btn_arm_wake();
	feed_wdt();
	if (usb_present()) {
		/* v1.2.4: powering OFF while PLUGGED lands in the charge-standby
		 * gauge, exactly like plugging in an off device. SYSTEM_OFF with
		 * VBUS already high has no wake edge — the device just went dark
		 * until a replug (user request). A clean soft reset boots into
		 * standby instead: RESETREAS is cleared every boot, so only SREQ
		 * is set and the standby gate (!(OFF|DOG)) admits it; the external
		 * chips we just powered down stay down through standby, same as a
		 * cold plug-in. Unplugging from that standby SYSTEM_OFFs cleanly. */
		NVIC_SystemReset();
	}
	NRF_POWER->RESETREAS = 0xFFFFFFFFu;   /* best practice before SYSTEM_OFF */
	__DSB();
	NRF_POWER->SYSTEMOFF = 1u;
	__DSB();
	for (;;) { /* CPU is now off; wakes via the bootloader on button press */ }
}

/* FAILSAFE recovery: reset into the bootloader so the device can ALWAYS be
 * reflashed. Triggered by holding Track1+Track4 together (the same combo the
 * bootloader scans for at boot). We flush any recording first, then show a clean
 * cue (status row dark, all 4 track LEDs lit = "loading firmware"), write the
 * UF2 magic (harmless if the bootloader ignores it) and reset; the user keeps
 * holding 1+4 through the reset and the bootloader's own button scan enters DFU. */
static void enter_dfu(void)
{
	stop_and_flush();
	all_off();                                                 /* status row dark */
	for (int i = 0; i < NUM_TRACK_LEDS; i++) track_led_on(i);  /* 4 track LEDs = DFU */
	NRF_POWER->GPREGRET = 0x57u;
	__DSB();
	NVIC_SystemReset();
	for (;;) { }
}

/* STACKA-664 A4: FN + track TAP with a page open = tap THAT lane's rate. Lanes:
 * page 1 T4 gate (0) / page 2 T4 echo (1) / page 3 T1 T2 T3 phaser sweep tremolo
 * (2 3 4) / page 1 T2 chorus (5). The song tap-tempo's own rules: 200..1500 ms between taps or the run
 * restarts, two taps give a period, more average it. The period is stored in
 * engine frames (48 kHz); the first tap of a run is the gate's downbeat. */
static void lane_tap(uint32_t page, int btn, int64_t t_ms, uint64_t t_s)
{
	static int64_t  lt_last = 0, lt_first = 0;
	static uint64_t lt_first_s = 0;
	static int      lt_lane = -1, lt_n = 0;
	int lane = -1;
	if      (page == 1u && btn == 3) lane = 0;
	else if (page == 1u && btn == 1) lane = 5;   /* 660: chorus */
	else if (page == 2u && btn == 3) lane = 1;
	else if (page == 3u && btn >= 0 && btn <= 2) lane = 2 + btn;
	else if (page == 4u && btn == 3) lane = 6;   /* WOBTAP-675: the wobble's wow */
	if (lane < 0) return;
	if (lane != lt_lane || lt_n == 0 || t_ms - lt_last > 1500 || t_ms - lt_last < 200) {
		lt_lane = lane; lt_n = 0; lt_first = t_ms; lt_first_s = t_s;
	}
	lt_last = t_ms; lt_n++;
	if (lt_n >= 2) {
		int64_t per_ms = (lt_last - lt_first) / (lt_n - 1);
		if (per_ms < 100) per_ms = 100;
		if (per_ms > 4000) per_ms = 4000;
		g_lane_per[lane] = (uint32_t)(per_ms * 48);
		if (lane == 0) g_lane_anc = lt_first_s;
	}
}

/* STACKA-664 (marc 09-05 late): with a page open, FN + PLAY + track = that lane's tapped
 * rate back to the grid -- the modifier layer's shape (FN + PLAY + VOL- is the chop
 * reset). The same lane map as lane_tap. */
static void lane_reset(uint32_t page, int btn)
{
	int lane = -1;
	if      (page == 1u && btn == 3) lane = 0;
	else if (page == 1u && btn == 1) lane = 5;
	else if (page == 2u && btn == 3) lane = 1;
	else if (page == 3u && btn >= 0 && btn <= 2) lane = 2 + btn;
	else if (page == 4u && btn == 3) lane = 6;   /* WOBTAP-675 */
	if (lane >= 0) g_lane_per[lane] = 0u;
}

/* Jump to song slot ns (M4b: FUNCTION+Track bank jump, and the tap-advance).
 * Saves the current song's BPM, loads the target's, signals the audio thread
 * to reload that slot's tracks. Refuses while a take is armed/recording/
 * flushing — the reload would trample the take and strand unflushed audio. */
/* GRIDCORE-733: a song's grid at load. With a loop and a beat count the tape is
 * the clock (the service derives everything); a loop without a count on an old
 * card gets its count from the saved tempo ONCE (nearest whole number of beats --
 * robust to the wall/speed mismatch that broke 09-10) and is rewritten; an empty
 * tapped song keeps the clock grid, its phase provisional until a tap. */
static void __attribute__((noinline)) grid_load_song(uint32_t ns)
{
	if (ns >= NUM_SLOTS) { g_grid_active = 0; return; }
	const uint32_t L = g_meta.slot[ns].loop_len;
	if (L && !g_grid_n[ns] && g_grid_bpm_q8[ns]) {
		const uint32_t nf = (uint32_t)((48000ULL * 60u * 256u) / g_grid_bpm_q8[ns]);
		const uint32_t sp = g_meta.slot[ns].speed_q16 ? g_meta.slot[ns].speed_q16 : 65536u;
		const uint64_t bs = ((uint64_t)nf * sp) >> 16;   /* the saved beat in samples IF the saved speed was the one */
		uint32_t nb = bs ? (uint32_t)(((uint64_t)L + bs / 2u) / bs) : 0u;
		if (nb < 1u) nb = 1u;
		if (nb > 255u) nb = 255u;
		g_grid_n[ns] = (uint8_t)nb; g_grid_o[ns] = 0u;
		g_grid_bpm_q8[ns] = (uint16_t)((48000ULL * 60u * 256u * nb) / L);
		g_grid_save_req = 1;
	}
	if (L && g_grid_n[ns]) {
		g_grid_active = 1;   /* the service publishes bf / anchor_e from the tape next block */
		g_grid_beat_frames = (uint32_t)(((uint64_t)L * 65536u) / ((uint64_t)g_grid_n[ns] * (g_play_speed_q16 ? g_play_speed_q16 : 65536u)));
		if (!g_grid_beat_frames) g_grid_beat_frames = 1u;
		g_grid_anchor = g_sample_clock; g_grid_anchor_e = g_grid_anchor;
		g_grid_next_tick = g_sample_clock;
	} else if (g_grid_bpm_q8[ns]) {
		g_grid_beat_frames = (uint32_t)((48000ULL * 60u * 256u) / g_grid_bpm_q8[ns]);
		g_grid_anchor = g_sample_clock; g_grid_anchor_e = grid_anchor_eff();   /* STACKT-716 */
		g_grid_next_tick = g_sample_clock;
		g_grid_active = 1;
	} else {
		g_grid_active = 0;
	}
}
static void jump_to_slot(uint32_t ns)
{
	if (!g_meta_loaded || g_slot_switch_req) return;    /* ignore until the last switch lands */
	if (g_rec_track >= 0) return;
	for (int i = 0; i < NTRK; i++) {
		uint8_t st = trk[i].state;
		if (st == TS_ARMED || st == TS_REC || st == TS_DONE) return;
	}
	if (ns >= NUM_SLOTS) return;
	if (g_slot >= NUM_SLOTS) g_slot = 0;
	if (ns == g_slot) return;
	g_meta.slot[g_slot].speed_q16 = g_play_speed_q16;   /* remember where you left it */
	g_meta.cur_slot = ns;
	g_slot = ns;
	g_play_speed_q16 = g_meta.slot[ns].speed_q16;        /* resume the new song's BPM */
	g_play_bpm = (int)(((uint64_t)g_play_speed_q16 * LOOP_BPM_BASE + 32768u) / 65536u);
	if (g_play_bpm < BPM_MIN) g_play_bpm = BPM_MIN;
	if (g_play_bpm > BPM_MAX) g_play_bpm = BPM_MAX;
	{	/* M7: restore the target song's persisted chop + effective mode */
		uint32_t cd, co;
		chop_meta_decode(g_meta.chop[ns], &cd, &co);   /* CHOPCAP-690 */
		g_chop_div = cd; g_chop_off = co;
		g_fixed_len = (g_meta.song_mode[ns] & 0x0Fu)
			    ? ((g_meta.song_mode[ns] & 0x0Fu) == 2u ? 1u : 0u) : g_mode_pref;
		grid_load_song(ns);   /* GRIDCORE-733 */
	}
	g_grid_fresh = 0;  /* M20 F1: a persisted grid's phase is provisional */
	g_grid_base_beats = 0; g_grid_base_blocks = 0;   /* M20 F7 */
	g_gridrec_beat_samps = 0;   /* LOCKLOAD-725: the stored beat belongs to the song that punched it */
	g_win_free = 0;     /* M16: the free window is session performance state */
	g_br_shift = 0;     /* LOOPHOLD-804: so is the tape the repeat held -- per song, session only */
	g_win_rev = 0;
	for (int _r = 0; _r < NTRK; _r++) g_head_rev[_r] = 0;   /* REV2-641: so are the directions */
	g_heads_mode = 0;   /* M13: heads are per-song doctrine like speed/mutes/
	                     * chop — a new song always opens playing normally;
	                     * triple-tap re-enters (session-only, never stored) */
	g_slot_switch_req = 1;
	g_meta_save_req = 1;
}

/* Advance to the next song slot (FUNCTION tap). */
static void next_slot(void)
{
	if (g_slot >= NUM_SLOTS) g_slot = 0;
	jump_to_slot((g_slot + 1u) % NUM_SLOTS);
}

/* WDT PRE-WARNING (nRF52: fires ~61 us before the reset): the reported crash
 * was rr=2 = a WATCHDOG reset — something kept main (the feeder) off the CPU
 * for 4 s. Stamp WHO was running into the fault breadcrumb: 'A'udio,
 * 'S'treamer, 'M'IDI, 'm'ain (stuck in its own loop), 'I'dle (CPU idle =>
 * main is BLOCKED on something, not starved) — printed next boot as
 * flt=d09000XX@tcb. */
extern struct k_thread z_main_thread;
extern struct k_thread z_idle_threads[];
static void wdt_prewarn(const struct device *dev, int ch)
{
	ARG_UNUSED(dev); ARG_UNUSED(ch);
	k_tid_t t = k_current_get();
	uint32_t who = '?';
	if      (t == &audio_tcb)        who = 'A';
	else if (t == &streamer_tcb)     who = 'S';
	else if (t == &midi_tcb)         who = 'M';
	else if (t == &z_main_thread)    who = 'm';
	else if (t == &z_idle_threads[0]) who = 'I';
	g_fault_reason = 0xD0900000u | who;
	g_fault_pc = (uint32_t)t;
	g_fault_key = 0xFA17FA17u;
	/* RAM breadcrumbs did NOT survive a real WDT reset (the bootloader runs
	 * first and scrubs that RAM) — GPREGRET2 is a RETAINED register that
	 * survives every soft/WDT reset and the bootloader leaves it alone. */
	NRF_POWER->GPREGRET2 = (uint8_t)who;
}

int main(void)
{
	/* Why did the last boot end? (bit0 pin reset, bit1 watchdog, bit2 soft
	 * reset, bit3 CPU lockup — see nRF52840 POWER.RESETREAS.) */
	g_resetreas = NRF_POWER->RESETREAS;
	NRF_POWER->RESETREAS = 0xFFFFFFFFu;
	if (g_fault_key == 0xFA17FA17u) {
		g_last_fault_reason = g_fault_reason;   /* previous boot CRASHED */
		g_last_fault_pc = g_fault_pc;
		g_fault_key = 0u;
	} else if (NRF_POWER->GPREGRET2 != 0u) {
		/* RAM breadcrumb lost (bootloader scrub) but the retained register
		 * survived: recover the watchdog culprit letter from it. */
		g_last_fault_reason = 0xD0900000u | NRF_POWER->GPREGRET2;
		g_last_fault_pc = 0u;
	}
	NRF_POWER->GPREGRET2 = 0u;
	/* DWT cycle counter: feeds the audio-block exec-time watermark (aus=). */
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	/* Main runs at PREEMPT(1): BELOW the audio engine (0), ABOVE the streamer
	 * (5) and MIDI (6). History: main once defaulted to 0 and its blocking
	 * ladder-ADC reads preempting the streamer caused rec overflows, so a
	 * rescue round demoted it to (8) — but that turned "streamer busy" into
	 * "lights, buttons and the WATCHDOG FEED all crawl", and a 4 s busy
	 * stretch (easy with 4 independent tracks + record at 48 kHz) became a
	 * watchdog reset: the field-reported freeze/crash (rr=2, lights slow).
	 * Preempting the streamer is harmless NOW: the rings ride 341 ms and
	 * every bus wait is fail-safe/time-bounded — a few ms of ladder reads or
	 * CDC prints cannot overflow anything. Responsiveness is structural. */
	/* W3-r5: PREEMPT(2), not (1) -- the streamer's pressure sprint
	 * boosts to PREEMPT(1) and Zephyr does not preempt on ties, so
	 * at (1) main kept every cycle it held (census §48). At (2) the
	 * sprint takes the CPU during rec pressure; the 150/15 duty
	 * still hands main the level back every 165 ms (WDT, buttons). */
	k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(2));

	const struct device *wdt = DEVICE_DT_GET(WDT_NODE);

	/* Wake cause: captured ONCE at main() entry into g_resetreas (the register
	 * is write-1-to-clear and is already cleared there — a second read here
	 * returned 0 and broke this gate, parking watchdog recoveries in standby
	 * and SYSTEM_OFF-wiping the crash breadcrumb on battery). OFF = woken from
	 * SYSTEM_OFF by the power button; DOG = watchdog recovery (resume fast). */
	uint32_t wake_reas = g_resetreas;

	pwr_btn_cfg_input();
	charger_init();                 /* make sure the battery actually charges */
	for (int i = 0; i < NUM_LEDS; i++)
		led_cfg_output(&leds[i]);
	for (int i = 0; i < NUM_TRACK_LEDS; i++)
		led_cfg_output(&track_leds[i]);
	all_off();
	track_all_off();
	led_pwm_init();   /* ALWAYS-DIM: start the LED soft-PWM now, before the
	                   * charge-standby loop, so the battery gauge is dim too
	                   * (TechnicsOP's build started it later in boot). */

	if (device_is_ready(wdt)) {
		wdt_install_timeout(wdt, &(struct wdt_timeout_cfg){
			.window.max = 4000, .callback = wdt_prewarn,
		});
		wdt_setup(wdt, 0);
	}
	feed_wdt();

	/* EARLY controls_init: the battery gauge in charge-standby below needs the
	 * ladder rail + ADC channels, which used to come up only after standby.
	 * Idempotent (pure register config); the original call later is unchanged. */
	controls_init();

	/* EARLY streamer start (v1.2.3): the saved brightness lives in the song
	 * index, and only the streamer reads the eMMC — but it used to be created
	 * AFTER standby, so the charging gauge could never see the setting and
	 * always showed the dim default (user report). Started here it inits the
	 * eMMC, loads the index (g_meta_loaded -> the standby loop applies
	 * led_full), then idles; the audio_init call is guarded against a double
	 * create, and its transfer polling waits for USB (g_usb_up). */
	streamer_start();

	/* ---- CHARGE-STANDBY: the device no longer springs to life on its own ----
	 * Plugging USB in (or finishing a flash, or inserting a battery) lands here:
	 * silent, looper untouched, LED 1 blinking while charging / solid when full.
	 * HOLD the power button ~1.5 s (stock-length) to actually switch ON.
	 * M18: a button wake from SYSTEM_OFF used to SKIP this gate entirely
	 * ("the user waking the device is already holding the button") — which
	 * quietly made pocket presses a one-click power-on: breakbeats in your
	 * pants and a drained battery (luuuciano's report). EVERY power-on now
	 * walks through the same hold; releasing early on battery drops
	 * straight back to SYSTEM_OFF (an accidental blip costs milliseconds).
	 * On battery with no button held there is nothing to do -> clean
	 * SYSTEM_OFF (button wakes). Only watchdog recovery and a preserved
	 * fault breadcrumb still skip the gate: the user was mid-session, and
	 * the battery standby path would SYSTEM_OFF away the forensics. */
	if (!(wake_reas & POWER_RESETREAS_DOG_Msk) &&
	    g_last_fault_reason == 0xFFFFFFFFu) {
		/* (a valid fault breadcrumb also skips standby: the user was
		 * mid-session, and the battery standby path would SYSTEM_OFF and
		 * wipe the very forensics we just preserved) */
		int64_t hold_t = -1;
		uint32_t blink = 0;
		while (1) {
			feed_wdt();
			/* v1.2.3-r7: apply the saved brightness at the TOP of the
			 * standby loop so EVERY branch honors it — the r5 apply sat
			 * inside the gauge branch only, so the hold-to-turn-on
			 * feedback and the turn-on transition stayed dim (user
			 * report). The early streamer (r6) has the index loaded
			 * well inside the 600 ms hold. */
			if (g_meta_loaded)
				{ g_led_dim = (g_meta.led_full & 1u) ? 0u : 1u; led_hw_refresh(); }
			if (pwr_pressed()) {
				int64_t hnow = k_uptime_get();
				if (hold_t < 0) hold_t = hnow;
				else if (hnow - hold_t >= 1500)
					break;                    /* -> full power-on */
				/* M18-r2: the power-off countdown, mirrored — the
				 * side row FILLS across the 1.5 s hold so the gesture
				 * teaches its own length. A pocket blip still shows
				 * just one dim LED for an instant, nothing more. */
				{
					int lit = (int)(((hnow - hold_t) * NUM_LEDS) / 1500) + 1;
					if (lit > NUM_LEDS) lit = NUM_LEDS;
					all_off();
					for (int li = 0; li < lit; li++) led_on(li);
				}
			} else {
				hold_t = -1;
				if (!usb_present())
					power_off();              /* battery, idle -> off */
				/* BATTERY GAUGE (plan §3.5): 1-4 LEDs = approximate
				 * charge level. LEDs below the level are solid; the top
				 * one blinks while charging and goes solid when the
				 * charger reports done (all four solid = full).
				 * Thresholds are RAW 12-bit readings of the AIN4
				 * battery divider (gain 1/6, 0.6 V internal ref) —
				 * PLACEHOLDERS until calibrated: note the diag line's
				 * batt= value when full and when nearly empty, then
				 * space these three between those readings. If the ADC
				 * read fails (<0), lvl stays 1 and this degrades to the
				 * original single-LED blink/solid display. */
				/* Interim calibration 2026-07-20: full anchor MEASURED
				 * at raw ~2380 (resting, plugged-not-charging = ~4.21 V);
				 * empty end is a ~3.35 V physics estimate pending a real
				 * low reading. Spread at 25/50/75% of that range. Refine
				 * batt_thr once a near-empty batt= value is logged. */
				/* v1.2.3: standby (charging) runs BEFORE the boot
				 * block that applies the saved brightness, so the
				 * gauge always showed the dim default even in full
				 * mode (user report). Apply it here as soon as the
				 * streamer has the index; idempotent per pass. */
				if (g_meta_loaded)
					{ g_led_dim = (g_meta.led_full & 1u) ? 0u : 1u; led_hw_refresh(); }
				static const int batt_thr[3] = { 2020, 2140, 2260 };
				static int bavg = -1;   /* smoothed reading (EMA over ~10 passes) */
				static int blvl = 0;    /* sticky displayed level (hysteresis) */
				int braw = ladder_read(&adc_ladder[LAD_BATT]);
				if (braw >= 0)
					bavg = (bavg < 0) ? braw
					     : bavg + (braw - bavg) / 8;
				if (bavg >= 0) {
					/* v1.2.1 gauge fix (user report: LED 2 flickered
					 * while charging near-empty): a SINGLE raw sample
					 * per pass with no hysteresis let ADC noise +
					 * charger ripple flip the level ~25x/s at a
					 * threshold — the boundary LED strobed between
					 * off and blinking. Smooth first, then only move
					 * the level once the average clears a threshold
					 * by ±18 counts (a step is ~120 counts wide). */
					int nl = 1;
					for (int k = 0; k < 3; k++)
						if (bavg > batt_thr[k]) nl = k + 2;
					if (blvl == 0)      blvl = nl;   /* first read seeds */
					else if (nl > blvl && bavg > batt_thr[blvl - 1] + 18)
						blvl = nl;
					else if (nl < blvl && bavg < batt_thr[blvl - 2] - 18)
						blvl = nl;
				}
				int lvl = blvl ? blvl : 1;
				int bl = ((++blink / 12u) & 1u) == 0u;
				for (int i = 0; i < NUM_LEDS; i++) {
					int on;
					if (i < lvl - 1)       on = 1;
					else if (i == lvl - 1) on = charging() ? bl : 1;
					else                   on = 0;
					on ? led_on(i) : led_off(i);
				}
			}
			k_msleep(40);
		}
		all_off();
		/* wait for release so the hold doesn't bleed into the FUNCTION logic */
		while (pwr_pressed()) { feed_wdt(); k_msleep(20); }
	}

	controls_init();                /* power the button ladders + ADC + serial */
	codec_init();                   /* release codec resets + scan the I2C bus */
	audio_init();                   /* osc on, TAS2505 configured, I2S running  */
	hp_init();                      /* headphone codec on (always-on, TimK's driver) */
	usb_audio_start();              /* device_next: UAC2 audio-in + CDC console  */
	g_usb_up = 1;                   /* streamer may poll the transfer page now */
	feed_wdt();

	/* HEADPHONE AUTO-MUTE boot state: start muted if headphones are already in. */
#if HP_TIM_TEST
	if (g_hp_on == 1) {
		int votes = 0, reads = 0;
		for (int i = 0; i < 5; i++) {
			int c = hp_detect_connected();
			if (c >= 0) { reads++; votes += c; }
			k_msleep(8);
		}
		g_hp_in = (reads > 0 && votes * 2 > reads) ? 1 : 0;
		tas_set_speaker(!g_hp_in);
	}
#endif

	/* v1.2.3-r8: apply the saved brightness BEFORE the power-ON sweep.
	 * Button wakes skip standby entirely, so none of the standby-loop
	 * applies run on the battery power-on path — the sweep rendered three
	 * lines before the meta-wait and always used the dim default (user
	 * report, third location of the same boot-ordering gap). The early
	 * streamer has the index long before this point; the bounded wait is
	 * effectively zero. */
	for (int bw = 0; bw < 100 && !g_meta_loaded; bw++) { feed_wdt(); k_msleep(5); }
	if (g_meta_loaded)
		{ g_led_dim = (g_meta.led_full & 1u) ? 0u : 1u; led_hw_refresh(); }

	/* ---- power-ON indication: sweep the LEDs on, then clear ---- */
	for (int i = 0; i < NUM_LEDS; i++) {
		led_on(i);
		feed_wdt();
		k_msleep(90);
	}
	k_msleep(160);
	all_off();

	/* wait for the streamer to load the song metadata (block 0), then select the
	 * last-used song and its saved BPM and load its tracks. */
	for (int i = 0; i < 200 && !g_meta_loaded; i++) { feed_wdt(); k_msleep(5); }
	if (g_meta_loaded) {
		g_slot = (g_meta.cur_slot < NUM_SLOTS) ? g_meta.cur_slot : 0;   /* defensive clamp */
		g_play_speed_q16 = g_meta.slot[g_slot].speed_q16;
		g_play_bpm = (int)(((uint64_t)g_play_speed_q16 * LOOP_BPM_BASE + 32768u) / 65536u);
		if (g_play_bpm < BPM_MIN) g_play_bpm = BPM_MIN;
		if (g_play_bpm > BPM_MAX) g_play_bpm = BPM_MAX;
		{ g_led_dim = (g_meta.led_full & 1u) ? 0u : 1u; led_hw_refresh(); }   /* restore brightness mode */
		g_instant_rec = (uint8_t)(((g_meta.led_full >> 1) & 1u) ? 0u : 1u);   /* M41-r5: bit 1 SET = classic */
		{	/* M7: current song's persisted chop + effective mode */
			uint32_t cd, co;
			chop_meta_decode(g_meta.chop[g_slot], &cd, &co);   /* CHOPCAP-690 */
			g_chop_div = cd; g_chop_off = co;
			g_fixed_len = (g_meta.song_mode[g_slot] & 0x0Fu)
				    ? ((g_meta.song_mode[g_slot] & 0x0Fu) == 2u ? 1u : 0u)
				    : g_mode_pref;
			grid_load_song(g_slot);   /* GRIDCORE-733: boot */
		}
		g_slot_switch_req = 1;
	}

	int64_t press_start = -1;
	int64_t tap_first = 0, tap_last = 0;  /* M8a FN-tap tempo run */
	int      tap_n = 0;
	uint64_t tap_first_s = 0;             /* sample-clock at first tap */
	uint64_t tap_last_s  = 0;             /* sample-clock at the latest tap */
	uint64_t press_start_s = 0;           /* M20 F9: sample-clock at the FN PRESS edge */
	int64_t  any_tap_t = 0;               /* M23-r6: every FN tap, run or not */
	uint8_t  fast_pair = 0;               /* M23-r6: last two taps < 280 ms apart */
	int      fnp_low = 0;                 /* PLAY-release debounce (passes) */
	int64_t combo_start = -1;   /* FUNCTION+PLAY: when the combo was first seen */
	uint8_t combo_fired = 0;    /* mode already toggled this combo press */
	uint8_t combo_seen  = 0;    /* PLAY was seen at all during this FUNCTION press */
	uint8_t suppress_play = 0;  /* swallow a trailing PLAY held past combo exit */
	enum trk_btn bj_cand = TRK_NONE; /* FUNCTION+Track bank jump: sticky candidate band */
	int bj_cnt = 0;                  /*   consecutive passes the candidate has held     */
	int bj_fired = -1;               /*   band already jumped during this FUNCTION press */
	int64_t pg_t0 = 0;               /* PG-533: when the pending FN+track press began */
	uint64_t pg_t0_s = 0;   /* STACKA-664 A4 */
	int pg_pend = -1;                /* PG-533: pressed track -- jump-on-release or page-on-dwell */
	int64_t fnp_edge = -1;           /* FUNCTION+PLAY dim toggle: last PLAY press edge */
	int fnp_chain = 0;               /* M13: consecutive PLAY taps (2 = 1.0x snap, 3 = heads) */
	int fnp_pend_snap = 0;           /* M15-r3: snap DEFERRED past the triple window */
	int fnp_presses = 0;             /* M15-r4: PLAY press edges this FUNCTION hold */
	int fnr_cand = -1, fnr_cnt = 0;  /* REV2-641: FN+PLAY+track two-pass confirm */
	uint8_t fnr_held = 0;            /* the chord confirmed; its release sweep is owned until idle */
	int     fnr_trk = -1, fnr_off = 0;   /* ISO2-643: the chord's track; off-band passes seen */
	uint8_t fnr_mode = 0;            /* ISO2-643: 1 = ISOLATE (PLAY landed first), 2 = REVERSE (the track did) */
	uint8_t fnr_pre = 0;             /* ISO2-643: the band the ladder came from: 1 idle, 2 PLAY, 3 one track, 4 other */
	enum vol_btn cp_cand = VOL_NONE; /* FUNCTION+rocker/Vol chop: sticky candidate */
	int cp_cnt = 0;                  /*   consecutive passes it has held */
	enum vol_btn fv_cand = VOL_NONE; /* MOD-654: FN+PLAY + Vol/rocker sticky candidate */
	int fv_cnt = 0;                  /*   consecutive passes it has held (2 = commit, once) */
	int fv_oct_from = 0;             /*   MOD-654: the BPM an octave DOWN left (0 = none) */
	int cp_rep_at = 0;               /*   M10 glide: cp_cnt of the next auto-repeat */
	int cp_rep_iv = 0;               /*   M10 glide: repeat interval, in ~25 ms passes */
	uint8_t ctl_flush = 0;      /* looper decode state went stale (FUNCTION page / USB transfer owned the loop) */
	int64_t last_diag = 0;      /* throttle the control read-out */

	while (1) {
		feed_wdt();

		/* USB block-transfer in progress: audio is paused and the streamer is
		 * servicing reads/writes. Ignore the controls and show a "busy" pattern
		 * (all four track LEDs blinking together) so the device clearly reads as
		 * mid-transfer rather than frozen. */
		if (g_xfer_mode) {
			static uint32_t xb;
			int on = ((xb++ / 8u) & 1u);
			for (int i = 0; i < NUM_TRACK_LEDS; i++)
				on ? track_led_on(i) : track_led_off(i);
			ctl_flush = 1;
			k_msleep(20);
			continue;
		}

		ladder_scan();   /* ADCSCAN-710: every ladder, once, for this pass */

		/* Print one status line ~twice a second (the 500 ms gate below) for
		 * monitoring. Only prints when a serial monitor is attached (DTR). */
		int64_t now = k_uptime_get();
		if (now - last_diag >= 500) {
			last_diag = now;
			controls_diag();
			feed_wdt();      /* the diag print path can be slow; never starve the WDT */
		}

		/* USB FEEDBACK-FORMAT AUTO-NEGOTIATION. Windows and Apple disagree
		 * about the Full-Speed feedback value format (4-byte Q16.16 vs the
		 * spec's 3-byte Q10.14) and each kills or cripples the stream when
		 * fed the other's. The wrong choice always shows up the same way:
		 * the host holds the stream OPEN but delivers (almost) nothing, so
		 * the mixer stitches silence (g_zero_pad counts it). If more than
		 * half of each 100 ms window is stitched silence for ~400 ms
		 * straight, flip the format and let the host try again — the flip
		 * repeats until data flows, so the device converges on whatever
		 * the connected host actually parses, on every OS. A closed
		 * stream never pads, so this can't fire from mere silence. */
		{
			static int64_t fb_probe_t;
			static uint32_t fb_zp_last;
			static int fb_starve_streak;
			if (fb_probe_t == 0) {
				fb_probe_t = now;
				fb_zp_last = g_zero_pad;
			} else if (now - fb_probe_t >= 100) {
				fb_probe_t = now;
				uint32_t zpn = g_zero_pad;
				uint32_t d = zpn - fb_zp_last;
				fb_zp_last = zpn;
				if (d >= (LOOP_RATE / 20u)) {   /* >50% of the window */
					if (++fb_starve_streak >= 4) {
						uac2_fs_fb_windows_fmt =
							!uac2_fs_fb_windows_fmt;
						fb_starve_streak = 0;
					}
				} else {
					fb_starve_streak = 0;
				}
			}
		}

		/* (track LEDs are driven by the looper beat clock below) */

		/* FUNCTION button: a SHORT tap changes song; a long HOLD powers off
		 * (the same button does both, like the original device). */
		if (pwr_pressed()) {
			ctl_flush = 1;
			if (press_start < 0) {
				press_start = k_uptime_get();
				g_fn_held = 1;   /* LED-549 */
				/* M20 F9: stamp the PRESS, not the release. The tap
				 * cannot be CLASSIFIED until the release (a long hold
				 * means something else), but the moment it names is
				 * the press — so remember the clock here and judge
				 * later. FUNCTION is a plain GPIO with no debounce,
				 * so this read trails the finger by at most one 8 ms
				 * control pass. */
				press_start_s = g_sample_clock;
				/* M23-r6b: measure the gap from the last completed
				 * tap to THIS PRESS. The clear gesture is tap then
				 * press-and-HOLD, so the second press never becomes
				 * a tap — detecting at tap-release (r6) could not
				 * fire for the very gesture it was written for. */
				/* 280 ms is NOT an accident guard — it is the
				 * discriminator, and it pairs with the tap-run
				 * rule below that resets tap_n whenever two taps
				 * land under 200 ms apart. Both encode the same
				 * idea: this pair is FASTER THAN ANY TEMPO, so it
				 * cannot be a tap run. M25 tried widening it to
				 * 800 ms for #33 and that was wrong — 800 ms is
				 * 75 BPM, squarely musical. Worse, a tap run that
				 * FAILS to register (taps <200 ms, >1500 ms, or
				 * >20% irregular) leaves tap_n at 1, so the very
				 * next hold takes the clear branch: a fumbled snap
				 * would erase the grid. #33's discoverability is a
				 * DOCS problem, not a timing one. */
				fast_pair = (any_tap_t &&
					     (press_start - any_tap_t) < 280) ? 1u : 0u;
			}

			/* MODE TOGGLE — FUNCTION + PLAY held together ~0.7 s flips the
			 * fixed/variable loop-length mode. The normal ladder decode below
			 * is skipped while FUNCTION is held, so read PLAY here. PLAY is at
			 * the TOP of the AIN0 ladder (~1823); require >1600 so a Track-4
			 * (~1220) or the 1+4 bootloader combo (~1325) can never be mistaken
			 * for it. FUNCTION is a separate GPIO, so holding it does not shift
			 * the ladder voltage. While the combo is engaged the power-off
			 * countdown/shutdown is suppressed (this gesture must never risk a
			 * power-off), and the FUNCTION-release song-change is suppressed. */
			/* M15-r3: deferred 1.0x snap fires once the triple window is
			 * over (no 3rd tap arrived). Runs every FN-held pass. */
			if (fnp_pend_snap && fnp_edge >= 0 &&
			    k_uptime_get() - fnp_edge > 600) {
				fnp_pend_snap = 0;
				g_play_speed_q16 = 65536u;
				g_play_bpm = 80;
			}
			int fraw = ladder_read(&adc_ladder[LAD_TRACKS]);
			/* ISO2-643: remember which band the ladder came FROM, so a PLAY+TN
			 * chord knows whether PLAY or the track landed first (the split).
			 * Single-track bands = the bank-jump decoder's (M31-r2). */
			if (fraw < 110)                          fnr_pre = 1;
			else if (fraw >= 1840 && fraw < 2267)   { /* the chord itself: keep */ }
			else if (fraw > 1773)                    fnr_pre = 2;   /* bare PLAY, or PLAY + several tracks */
			else if ((fraw >= 110  && fraw <  308) || (fraw >= 308  && fraw <  488) ||
			         (fraw >= 650  && fraw <  795) || (fraw >= 1099 && fraw < 1256)) fnr_pre = 3;
			else                                     fnr_pre = 4;
			/* M31: 1600 -> 1773. M27 made multi-track combos reachable codes on
			 * this ladder (2+3+4 = 1683, ALL4 = 1743), and anything over the old
			 * 1600 counted as a PLAY press under FN - three phantom taps inside
			 * 600 ms toggled heads mode. 1773 sits between ALL4 and PLAY (1798+)
			 * with 25+ counts of margin against +/-9 measured noise. */
			if (fraw > 1773) {
				fnp_low = 0;
				combo_seen = 1;
				if (combo_start < 0) {           /* fresh PLAY press edge */
					int64_t fnp_now = k_uptime_get();
					if (fraw >= 1840) {
						/* ISO2-643: PLAY landed on a held track (or with it): a
						 * chord, never a PLAY tap -- no snap chain, no heads
						 * triple, no mode toggle on release. */
						combo_start = fnp_now;
						combo_fired = 1;
					} else if (g_br_on) {
						/* BRFN-765: FN pressed with a repeat live under PLAY = the FN layer of the
						 * repeat -- a chord: no snap chain, no heads triple, no mode toggle. */
						combo_start = fnp_now;
						combo_fired = 1;
					} else {
					fnp_presses++;
					/* FUNCTION + PLAY DOUBLE-TAP = snap to 1.0x. A
					 * second PLAY press edge fires it and blocks the
					 * hold tiers for this press so one gesture can't
					 * do two things. Window 450 -> 600 ms (M15-r4):
					 * an unhurried triple's 3rd tap kept missing it,
					 * and a lapsed chain turned the last tap into a
					 * phantom mode-toggle chord. */
					if (fnp_edge >= 0 && fnp_now - fnp_edge <= 600)
						fnp_chain++;
					else
						fnp_chain = 1;
					if (fnp_chain == 2 && !combo_fired) {
						/* M8c SNAP TO 1.0x — but DEFERRED (M15-r3)
						 * until the 450 ms triple window closes: the
						 * snap used to fire on this edge, so reaching
						 * for a triple's 3rd tap yanked beatmatched
						 * speed (marc's report). A double alone still
						 * snaps, just ~0.4 s later — "come home" is
						 * not rhythm-critical; a completed triple now
						 * never touches the tape at all. */
						fnp_pend_snap = 1;
						combo_fired = 1;
					} else if (fnp_chain == 3) {
						/* M13: TRIPLE-tap = HEADS MODE toggle. The
						 * 3rd tap CANCELS the pending snap — heads
						 * enter at the speed you were playing at.
						 * ON requires content playing on track 1.
						 * Rings 2-4 are re-anchored so the heads (or
						 * the old loops) fade in cleanly via the
						 * starve machinery + a declick dip. */
						fnp_pend_snap = 0;
						if (g_heads_mode) {
							g_heads_mode = 0;
							for (int hm = 0; hm < NTRK; hm++)
								trk[hm].muted = (uint8_t)
								    ((g_head_mute_save >> hm) & 1u);
						} else {
							/* M19a-r2: source = the lowest playing
							 * UNMUTED track — a muted track still
							 * reads state TS_PLAY (mute is a flag,
							 * the ghost-glow design), and marc's
							 * live report was heads engaging on a
							 * muted track 1 over the audible track
							 * 2. All-muted songs fall back to any
							 * playing track. */
							int hs = -1;
							for (int hp = 0; hp < NTRK; hp++)
								if (trk[hp].state == TS_PLAY &&
								    !trk[hp].muted) {
									hs = hp; break;
								}
							if (hs < 0)
								for (int hp = 0; hp < NTRK; hp++)
									if (trk[hp].state == TS_PLAY) {
										hs = hp; break;
									}
							if (hs >= 0) {
								g_head_src = (uint8_t)hs;
								/* M19a-r4: snapshot the song's
								 * mutes and start every head
								 * AUDIBLE — heads mutes are
								 * performance state, restored
								 * on exit, never persisted
								 * (marc: a muted track must not
								 * ghost into the canon, and a
								 * silenced head must not come
								 * back as a muted track). */
								g_head_mute_save = 0;
								for (int hm = 0; hm < NTRK; hm++) {
									if (trk[hm].muted)
										g_head_mute_save |=
										    (uint8_t)(1u << hm);
									trk[hm].muted = 0;
								}
								g_heads_mode = 1;
							}
						}
						for (int hk = 0; hk < NTRK; hk++) {
							if (hk == (int)g_head_src) continue;
							trk[hk].p_w = (uint32_t)(g_consume_pos /
								TSPB_SRC(hk)) * TSPB_SRC(hk);   /* HG-646 */
						}
						for (int hk = 0; hk < NTRK; hk++)
							g_head_pos[hk] = (uint8_t)(hk * 64);
						/* REV2-641: directions are per-track state and survive
						 * heads entry -- "reverse kept" (W308) */
						g_dip_req = 1;
						combo_fired = 1;
					}
					fnp_edge = fnp_now;
					combo_start = fnp_now;
					}   /* ISO2-643: a bare PLAY press */
				}
				/* ISO2-643 (§1.3, W310): FN + PLAY + track -- ONE chord, TWO gestures,
				 * split by which finger landed second (no dwell, no blip):
				 *   PLAY first, then a track = momentary ISOLATE: on at the press,
				 *     off at release, hold as long as you like; another track
				 *     under the same hold moves the solo.
				 *   a track first, then PLAY = REVERSE that track, at the PLAY
				 *     press; tap PLAY again under the same hold = flip back.
				 * The bands are the bounce chord's, measured on 508 (PLAY 1807 |
				 * +T1 1861 | +T2 1910 | +T3 2004 | +T4 2159); FUNCTION is a
				 * separate GPIO. Two passes confirm. The FN+PLAY press is spent
				 * (no mode toggle on release, no brightness at 5 s, no deferred
				 * snap); the track's bank-jump / page candidates are spent (no
				 * song switch, no page); the release sweep is owned until the
				 * ladder is idle. PLAY + two or more tracks is not a gesture
				 * (W115): it reads above the T4 band and ends the chord. */
				{
					int rch = -1;
					if      (fraw >= 1840 && fraw < 1886) rch = 0;   /* PLAY+T1 ~1861 */
					else if (fraw >= 1886 && fraw < 1957) rch = 1;   /* PLAY+T2 ~1910 */
					else if (fraw >= 1957 && fraw < 2081) rch = 2;   /* PLAY+T3 ~2004 */
					else if (fraw >= 2081 && fraw < 2267) rch = 3;   /* PLAY+T4 ~2159 */
					if (rch >= 0 && !(g_bnc_on && rch == g_rec_track)) {   /* SPEEDBAKE2-769: PLAY on the held print target is the octave's modifier, not isolate / reverse */
						fnr_off = 0;
						if (rch == fnr_cand) { if (fnr_cnt < 3) fnr_cnt++; }
						else { fnr_cand = rch; fnr_cnt = 1; }
						if (fnr_cnt == 2) {
							fnr_trk = rch;
							fnr_held = 1; combo_fired = 1; fnp_pend_snap = 0;
							pg_pend = -1; bj_cand = TRK_NONE; bj_cnt = 0;   /* no song switch, no page */
							if (g_pg_open)    { fnr_mode = 3; lane_reset((uint32_t)g_pg_id, rch); }   /* STACKA-664: the lane's tempo back to the grid; nothing on release */
							else if (fnr_pre == 3) { fnr_mode = 2; rev_toggle(rch); }   /* the track landed first */
							else              { fnr_mode = 1; iso_engage(rch); }  /* PLAY did (or both at once) */
						}
					} else if (fnr_cnt >= 2 && ++fnr_off < 2) {
						/* one off-band pass is noise; the end needs two */
					} else {
						if (fnr_cnt >= 2 && fnr_mode == 1) iso_release();   /* the track lifted under a held PLAY */
						fnr_cand = -1; fnr_cnt = 0; fnr_off = 0; fnr_mode = 0;
					}
				}
				/* MOD-654 (§1.3): FN + PLAY held + the Vol/rocker ladder -- the
				 * second modifier's own surface (this branch owns the pass, so
				 * the chop block below never sees these presses):
				 *   VOL-  = CHOP RESET    (whole loop, chop gone, direction kept)
				 *   VOL+  = window HOME   (offset 0, chop size kept)
				 *   FWD   = OCTAVE UP     (BPM x2, only if it fits BPM_MIN..BPM_MAX)
				 *   RWD   = OCTAVE DOWN   (BPM /2, only if it fits; remembers
				 *           the BPM it left so the next UP restores it exactly)
				 * Two passes confirm, one action per press edge. The octave
				 * glides through the engine's own 2 %/block speed ramp -- the
				 * same slew as play/pause and the semitone double-click -- and
				 * is locked while a take is in flight, like the rocker. Every
				 * commit spends the PLAY press (no snap, no mode toggle, no
				 * brightness). */
				{
					enum vol_btn fvb = decode_vol(ladder_read(&adc_ladder[LAD_VOL]));
					if (fvb == VOL_NONE) { fv_cand = VOL_NONE; fv_cnt = 0; }
					else if (fvb == fv_cand) { if (fv_cnt < 1000) fv_cnt++; }
					else { fv_cand = fvb; fv_cnt = 1; }
					if (fv_cnt == 2) {   /* the committed press edge */
						combo_fired = 1; fnp_pend_snap = 0; combo_seen = 1;
						if (g_br_on && (fvb == VOL_UP || fvb == VOL_DOWN)) {   /* BRFN-765: the repeat's FN layer -- VOL-/+ = the window one earlier / later; BROCT-771: the rocker falls through to the OCTAVE (bare PLAY + rocker is shorter / longer) */
							if (g_br_len8) br_setwin((fvb == VOL_UP) ? (g_br_s8 + g_br_len8) : (g_br_s8 + 256u - g_br_len8), g_br_len8);   /* BRCHOP-800: Q8 of the audible cycle */
							g_chop_req = 1; g_dip_req = 1;
						} else if (fvb == VOL_UP || fvb == VOL_DOWN) {
							uint32_t d = (fvb == VOL_DOWN) ? 1u : g_chop_div;   /* VOL-: reset, whole loop; VOL+: home, size kept */
							g_win_free = 0;
							/* WINRESET-815 (marc): VOL- is the RESET, so it returns the window to the whole loop
							 * PLAYING FORWARD -- it clears everything the FN faders set, direction included.
							 * This reverses M23-r11 (nervouskidz), which kept the direction on the reasoning that
							 * crossing the faders is a physical statement the buttons may not overrule. The reasoning
							 * is right; the premise was never implemented. g_win_rev is written ONLY when a fader
							 * moves under FN, and that same write also sets g_win_free = 1 -- so after a reset the
							 * direction was stranded, with the only control that could clear it handing back a window
							 * you then had to reset again. The crossing still speaks: it re-asserts the moment a
							 * crossed fader is actually MOVED under FN, which is the statement being made rather than
							 * one made minutes ago. PER-TRACK reverse (g_head_rev[], REV2-641) is NOT touched -- it is
							 * a different gesture with its own control (marc: "dont touch the per-track reverse").
							 * VOL+ is "home, size kept" and is not a reset, so it leaves the direction alone. */
							if (fvb == VOL_DOWN) g_win_rev = 0;
							g_chop_off = 0u;
							g_chop_div = d;
							if (g_slot < NUM_SLOTS) {
								chop_meta_encode(g_meta.chop[g_slot], d, 0u);   /* CHOPCAP-690 */
								g_meta_save_req = 1;
							}
							g_chop_req = 1;
							g_dip_req = 1;
						} else if ((g_rec_track < 0 || g_bnc_on) && (fvb == VOL_TEMPO_UP || fvb == VOL_TEMPO_DOWN)) {   /* SPEEDBAKE2-769: the octave is free during a bounce */   /* the octave (the ROCKER only -- MUTEFIX-739: VOL_BOTH fell in here as an octave down); tempo locked mid-take */
							int b = g_play_bpm, nb = b;
							if (fvb == VOL_TEMPO_UP) {
								nb = (fv_oct_from && b == fv_oct_from / 2) ? fv_oct_from : b * 2;
								fv_oct_from = 0;
							} else {
								nb = b / 2;
							}
							if (nb >= BPM_MIN && nb <= BPM_MAX && nb != b) {
								if (fvb == VOL_TEMPO_DOWN) fv_oct_from = b;
								g_play_bpm = nb;
								g_play_speed_q16 = (uint32_t)nb * 65536u / LOOP_BPM_BASE;
							}
						}
					}
				}
				if (!combo_fired &&
				    k_uptime_get() - combo_start >= 5000) {
					/* v1.2.2: HOLD THROUGH 5 s = BRIGHTNESS toggle,
					 * firing WITHOUT release — the light change is
					 * the confirm and the press is spent. The mode
					 * toggle moved to PLAY-RELEASE (0.7-5 s), so a
					 * hold that reaches 5 s never flips the mode. */
					g_led_dim ^= 1u;
					g_meta.led_full = (g_meta.led_full & ~1u) | (g_led_dim ? 0u : 1u);   /* keep bit 1 (M41-r5) */
					g_meta_save_req = 1;
					led_hw_refresh();   /* LEDS-725: the flip IS the confirm -- push the new duties now, not on the release */
					combo_fired = 1;
				}
				if (g_br_on && g_loop_len) {   /* BRFN2-766: FN + faders 1-3 on the repeat's window (the chord branch owns the pass, so the free-window code below never runs) */
					static int64_t bf_press = -1;
					static int16_t bf_snap[4], bf_s, bf_e, bf_z = 255; static uint8_t bf_eng[4];   /* 18 B: the floor is 3,500.
					 * BRFADER4-814: four faders, not three -- bf_z is fader 4's SIZE trim, the wf_* handler's law verbatim. */
					static int16_t bf_as = -1, bf_ae = -1;   /* COARSEWIN-815: the wf_* handler's retention, same law, same reason */
					if (bf_press != combo_start) {
						bf_press = combo_start;
						for (int k = 0; k < 4; k++) { bf_snap[k] = -1; bf_eng[k] = 0; }
						bf_z = 255;   /* BRFADER4-814: a hold starts untrimmed, exactly as the wf_* handler does */
						{   /* COARSEWIN-815: keep the COARSE parent if the repeat's window is still exactly what we applied */
							int _bs = (int)g_br_s8, _be = _bs + (int)g_br_len8;
							if (_be > 255) _be = 255;
							if (!(_bs == (int)bf_as && _be == (int)bf_ae)) { bf_s = (int16_t)_bs; bf_e = (int16_t)_be; }
						}
					}
					int bf_pend = 0;
					for (int k = 0; k < 4; k++) {   /* BRFADER4-814: fader 4 joins the chord gesture */
						int fv = ladder_read(&adc_ladder[LAD_FADER0 + k]);
						if (fv < 0) continue;
						int q = (int)((uint32_t)fv * 256u / 3700u); if (q > 255) q = 255;
						if (bf_snap[k] < 0) { bf_snap[k] = (int16_t)q; continue; }
						if (!bf_eng[k]) {
							int d = q - bf_snap[k]; if (d < 0) d = -d;
							if (d < 7) continue;                      /* the M31 intent gate */
							bf_eng[k] = 1; combo_seen = 1;
							g_fh_latch[k] = 1; g_fh_lastq[k] = -1;    /* the volume pickup law: no jump on the release */
						}
						if (k == 0)      { if (q != bf_s) { bf_s = (int16_t)q; bf_pend = 1; } }
						else if (k == 1) { if (q != bf_e) { bf_e = (int16_t)q; bf_pend = 1; } }
						else if (k == 2) {   /* POSITION: the window slides, width and order preserved */
							int w = (bf_s <= bf_e) ? bf_e - bf_s : bf_s - bf_e;
							int base = (q * (255 - w)) >> 8;
							bf_s = (int16_t)base; bf_e = (int16_t)(base + w); bf_pend = 1;
						}
						else { if (q != bf_z) { bf_z = (int16_t)q; bf_pend = 1; } }   /* BRFADER4-814: SIZE */
					}
					if (bf_pend) {
						int ws = bf_s, we = bf_e;
						if (ws > we) { int t2 = ws; ws = we; we = t2; }
						if (bf_z < 255) {   /* BRFADER4-814: tighten about the CENTRE -- the wf_* handler's zoom, verbatim */
							int _c2 = ws + we, _w = we - ws;
							int _wz = (_w * bf_z) >> 8;
							if (_wz < 2) _wz = 2;
							ws = (_c2 - _wz) >> 1;
							if (ws < 0) ws = 0;
							we = ws + _wz;
							if (we > 255) { we = 255; ws = we - _wz; if (ws < 0) ws = 0; }
						}
						if (we - ws < 2) { we = ws + 2; if (we > 255) { we = 255; ws = 253; } }
						bf_as = (int16_t)ws; bf_ae = (int16_t)we;   /* COARSEWIN-815 */
						br_setwin((uint32_t)ws, (uint32_t)(we - ws));   /* BRFIX-806: Q8 of the audible cycle -- 800 converted the other three sites and missed this copy, so FN + faders 1/2 saturated to the whole cycle */
					}
				}
				k_msleep(25);
				continue;                /* combo owns the button */
			}
			if (fnr_held) {
				/* REV2-641 / ISO2-643: PLAY lifted with the track still down. The
				 * track sits in its bare band, which the BANK JUMP below would
				 * commit 75 ms later -- nothing in this sweep is a gesture: own
				 * the button until the ladder is idle (two passes, so a stray
				 * dip mid-hold cannot mint a phantom PLAY edge). An isolate ends
				 * here; a reverse chord just re-arms, so PLAY tapped again under
				 * the held track flips it back (the combo branch above sees the
				 * re-press; fnr_pre still says 'track'). */
				if (fnr_cnt >= 2) {
					if (++fnr_off < 2) { k_msleep(25); continue; }   /* one pass below the band is noise */
					if (fnr_mode == 1) iso_release();
					fnr_cand = -1; fnr_cnt = 0; fnr_off = 0; fnr_mode = 0;
				}
				if (!(fraw >= 0 && fraw < 110)) { fnp_low = 0; k_msleep(25); continue; }
				if (++fnp_low < 2) { k_msleep(25); continue; }   /* two idle passes, as the tap release */
				fnr_held = 0; fnp_low = 0; combo_start = -1;
				k_msleep(25);
				continue;
			}
			if (combo_start >= 0) {
				/* v1.2.2-r4: DEBOUNCED release — the shared ladder can
				 * dip below the PLAY band for a stray pass mid-hold,
				 * which used to reset the 5 s clock (user: "brightness
				 * takes ~7 s"). Only 3 consecutive low passes count as
				 * a real release — for HOLDS. M15-r4: a short press
				 * (<300 ms) is a TAP, and its release commits after 2
				 * passes: fast triples' gaps were shorter than the
				 * 75 ms debounce, so taps merged, the chain counted 2,
				 * and the snap fired instead of heads (live report).
				 * Two passes still filters the single-sample dip the
				 * debounce was built for. */
				int fnp_need =
					(k_uptime_get() - combo_start < 300) ? 2 : 3;
				if (++fnp_low < fnp_need) { k_msleep(25); continue; }
				if (!combo_fired && fnp_presses < 2) {
					/* (2+ presses this hold = a tap chain, never a
					 * mode chord — the toggle is a single-press
					 * gesture, M15-r4) */
					int64_t fnp_held = k_uptime_get() - combo_start;
					if (fnp_held >= 350 && fnp_held < 5000)
						fnp_mode_toggle();  /* mode fires on RELEASE */
				}
			}
			fnp_low = 0;
			combo_start = -1;            /* PLAY not held */

			/* BANK JUMP — FUNCTION + Track N -> first song of bank N (M4b).
			 * POWER-OFF SAFETY (the whole point): committing a track band
			 * during a FUNCTION hold sets combo_seen — the same flag the
			 * FUNCTION+PLAY combo uses — which suppresses the power-off
			 * countdown, the shutdown itself, and the release song-advance
			 * for the remainder of this press. Turning the device off now
			 * requires a CLEAN FUNCTION-only hold, exactly as before.
			 * Sticky commit: the same band must be seen on 3 consecutive
			 * passes (~75 ms) — a finger transiting the ladder can't fire.
			 * Keeping FUNCTION held and pressing another track jumps again
			 * (bank surfing). While recording, jump_to_slot() refuses, as
			 * the tap-advance always has. Note: physically pressing T1+T4
			 * with FUNCTION held reads as the Track-4 band -> bank 4; the
			 * DFU combo remains a no-FUNCTION gesture. */
			{
				/* M31-r2: only the four MEASURED single-track bands may bank-jump.
				 * The old filter (110..1500 -> decode_tracks) let combo codes through
				 * as WRONG single tracks (1+2=572 read as track 3, 2+3=989 as track 4,
				 * 1+4=1303 as track 4...), so pressing several tracks with FN held
				 * fired phantom jump_to_slot() calls - songs switched by themselves
				 * and could land on an empty slot. Combos under FN now do NOTHING.
				 * Bands from SP1-BUTTON-LADDER-MAP.md, gaps between bands excluded. */
				/* ===== FXRST2-564: FN + T1 + T4 = RESET EVERY FX =====
				 * This lives HERE, inside the FN-held branch, because the branch
				 * ends in an unconditional `continue` -- nothing downstream of it
				 * runs while FN is down. 563 put the same handler after that
				 * continue and it could never execute.
				 * 1+4 reads ~1303, outside all four single-track bands below, so
				 * it is invisible to the bank-jump and page decoders (M31-r2 left
				 * combos under FN doing nothing). Two consecutive reads (~50 ms)
				 * is the same debounce depth the combo path uses; firing at
				 * exactly 2 makes it a CLICK, once per press, never a dwell. */
				static int fxr_cnt;
				if (fraw >= 1256 && fraw < 1347) {
					if (++fxr_cnt == 2) {
						if (g_pg_open && g_pg_id == 6u) {   /* PLACE-715: on page 6 the chord centres the four places */
							for (int _k = 0; _k < NTRK; _k++) { place_set(_k, 128u); g_fx_pick[_k] = 1; g_fx_lastq[_k] = -1; }
						} else if (g_pg_open && g_pg_id == 7u) {   /* PAGE7V-718: on page 7 the chord resets the page */
							if (g_slot < NUM_SLOTS) {
								g_grid_off_q8[g_slot] = 0; g_take_preset[g_slot] = 0u;
								g_grid_dirty_ms = k_uptime_get_32() | 1u;
							}
							for (int _k = 0; _k < NTRK; _k++) { nudge_set(_k, 128u); g_fx_pick[_k] = 1; g_fx_lastq[_k] = -1; }
						} else
							fx_reset_all();
						g_fxrst_lock = 1;
					}
					if (fxr_cnt > 1000) fxr_cnt = 1000;   /* no wrap on a long hold */
				} else if (fraw < 110) {
					fxr_cnt = 0;                          /* re-arm on true idle */
				}
				enum trk_btn tb = TRK_NONE;
				if      (fraw >= 110  && fraw <  308) tb = TRK_1;   /* ~213  */
				else if (fraw >= 308  && fraw <  488) tb = TRK_2;   /* ~404  */
				else if (fraw >= 650  && fraw <  795) tb = TRK_3;   /* ~728  */
				else if (fraw >= 1099 && fraw < 1256) tb = TRK_4;   /* ~1209 */
				if (tb >= TRK_1 && tb <= TRK_4 && !(g_bnc_on && (int)tb - (int)TRK_1 == g_rec_track)) {   /* SPEEDBAKE2-769: the held print target is no bank-jump / page candidate */
					if (tb == bj_cand) bj_cnt++;
					else { bj_cand = tb; bj_cnt = 1; }
					if (bj_cnt == 3) {   /* exact edge: once per press */
						combo_seen = 1;      /* never a power-off now */
						/* PF-545: page toggles moved to the BASE layer
						 * (sticky pages, W156). With FN held a track
						 * press is ALWAYS the jump-on-release / page-
						 * dwell candidate, page open or not. */
						pg_t0 = k_uptime_get();
						pg_t0_s = g_sample_clock;   /* A4: the tap's downbeat in the sample clock */
						pg_pend = (int)tb;
					}
					if (pg_pend >= (int)TRK_1 && pg_pend <= (int)TRK_4 &&
					    k_uptime_get() - pg_t0 >= 400) {
						{	/* FXP-547: same-TN dwell toggles ITS page;
							 * a different TN SWITCHES pages directly. */
							/* PGOPEN-561: TRK_1 is 0, so +1 maps the four
							 * bookmark buttons onto pages 1-4 directly. The
							 * old ternary could only ever emit 2 or 4 -- a
							 * two-page constant that outlived two pages. */
							uint8_t _id = (uint8_t)(pg_pend - (int)TRK_1 + 1);
							if (g_pg_open && g_pg_id == _id) {
								g_pg_open = 0;
								g_pg_sweep = 0;    /* r11: cancel an entry mid-flight */
								g_pg_exit  = 24;   /* r11: hold, then walk back out */
								
								/* PF-549 r15 (marc): HAND THE FADERS BACK WITH PICKUP.
								 * Closing a page dropped the faders straight onto the track
								 * volumes with no catch, so the very next poll wrote the
								 * PHYSICAL position into trk[].vol_q8 -- open the page, run
								 * the FX fader to the top, close, and a track that had been
								 * all the way DOWN came back all the way UP. The page takes
								 * the faders with pickup and has to return them the same
								 * way. This is the SAME latch heads mode uses on ITS exit;
								 * the volume simply stays put until a fader crosses it. */
								for (int _f = 0; _f < 4; _f++) {
									g_fh_latch[_f] = 1;
									g_fh_lastq[_f] = -1;
								}
							} else {
								g_pg_open = 1; g_pg_id = _id;
								g_pg_swmode = 0; g_pg_sweep = 24;   /* LED-549 r8: walk, then land */
								g_pg_exit  = 0;    /* r11: opening cancels an exit */
								g_pg_cnt   = 0;    /* r11: count from the top */
								for (int _f = 0; _f < 4; _f++) {
									g_fx_pick[_f] = 1;   /* pickup law */
									g_fx_lastq[_f] = -1;
								}
							}
						}   /* PF-545 r4: the same
						          * FN+hold-TN dwell TOGGLES its page (W156).
						          * PGOPEN-561: ALL FOUR now open their own
						          * page on the dwell. A QUICK tap is still the
						          * bank jump -- the dwell cancels it by
						          * clearing pg_pend, as T2/T4 already did. */
						pg_pend = -1;
					}
					led_service();           /* live song display mid-hold */
					k_msleep(25);
					continue;                /* track held: combo owns the button */
				}
				if (pg_pend >= 0) {
					if (g_pg_open) {
						/* STACKA-664 A4 (D4): inside a page the track buttons serve the
						 * page -- FN + track TAP taps that lane's rate. No song switch
						 * inside a page (marc 09-05). The tap's time is its PRESS. */
						lane_tap((uint32_t)g_pg_id, pg_pend - (int)TRK_1, pg_t0, pg_t0_s);
					} else {
					/* PG-533: the press ended before the dwell -- fire the
					 * M8a bank jump ON RELEASE, semantics unchanged. */
					uint32_t bank = (uint32_t)pg_pend * 4u;
					if (g_slot / 4u == (uint32_t)pg_pend)
						jump_to_slot(bank + ((g_slot % 4u) + 1u) % 4u);
					else
						jump_to_slot(bank);
					}
					bj_fired = pg_pend;
					pg_pend = -1;
				}
				bj_cand = TRK_NONE; bj_cnt = 0;
			}

			/* LOOP CHOP (scheme A', collision-audited): while FUNCTION is
			 * held the Vol/rocker ladder — which stock never reads during
			 * FUNCTION holds — becomes the chop surface:
			 *   FWD  = window /2 (shorter)   RWD  = window x2 (longer)
			 *   Vol+ = shift window right    Vol- = shift window left
			 *   (MOD-654, row 77: the rocker double-click RESET is gone --
			 *    every click steps; reset/home live on FN + PLAY + VOL)
			 * Sticky 3-pass commit (transit-proof); every commit sets
			 * combo_seen so the press can never become a power-off; bare
			 * rocker/Vol behavior outside FUNCTION holds is untouched. */
			{
				enum vol_btn vb = decode_vol(ladder_read(&adc_ladder[LAD_VOL]));
				if (vb != VOL_NONE) {
					if (vb == cp_cand) { if (cp_cnt < 1000) cp_cnt++; }
					else { cp_cand = vb; cp_cnt = 1; }
					if (vb == VOL_BOTH) {
						/* MUTEANY-738: FN + both VOL is swallowed (the mute moved to
						 * PLAY + both VOL); combo_seen so the press is not a power-off. */
						if (cp_cnt == 3) combo_seen = 1;
					} else if (g_pg_open && (vb == VOL_UP || vb == VOL_DOWN)) {
						if (cp_cnt == 3) {   /* PF-549 r12: WALK THE PAGE LIST */
							uint32_t _n = (uint32_t)g_pg_id;
							if (_n < 1u || _n > PG_LAST) _n = 1u;
							_n = (vb == VOL_UP) ? ((_n % PG_LAST) + 1u)
							                   : (((_n + PG_LAST - 2u) % PG_LAST) + 1u);
							g_pg_id    = (uint8_t)_n;
							{	/* LEDS-725: the COUNT sweep (1-2-3-4, gap, 1..n-4; or 1..n) */
								uint32_t _r1 = (_n > 4u) ? 4u : _n, _r2 = (_n > 4u) ? (_n - 4u) : 0u;
								g_pg_swmode = 1;
								g_pg_sweep  = (uint8_t)(2u * _r1 + (_r2 ? 2u + 2u * _r2 : 0u));
							}
							g_pg_exit  = 0;
							g_pg_cnt   = 0;   /* the count restarts on the new number */
							combo_seen = 1;   /* a flip is a COMBO: the FN release
							                   * must not also close the page */
							cp_rep_at  = 0;   /* and it does not hold-to-glide */
							for (int _f = 0; _f < 4; _f++) {
								g_fx_pick[_f]  = 1;   /* pickup law, as on open */
								g_fx_lastq[_f] = -1;
							}
						}
					} else if (cp_cnt == 3) {   /* committed press edge */
						combo_seen = 1;
						/* M23-r11 (nervouskidz): the buttons reclaim
						 * the window SHAPE from the faders — they do
						 * NOT get to overrule which DIRECTION the
						 * faders are asking for. Crossing the pair is
						 * a physical statement that stays true while
						 * the faders stay crossed, so a rocker reset
						 * must not silently un-reverse playback while
						 * the hardware still says reversed. Clearing
						 * g_win_free alone used to do exactly that,
						 * because every consumer read the direction as
						 * (g_win_free && g_win_rev) — see below, they
						 * now read g_win_rev on its own. */
						/* CHOPNEST-813: the rocker NO LONGER discards the region -- it subdivides it.
						 * FN + PLAY + VOL- (the chop reset) and a track delete still clear it. */
						uint32_t d = g_chop_div, o = g_chop_off;
						if (vb == VOL_TEMPO_UP || vb == VOL_TEMPO_DOWN) {
							if (vb == VOL_TEMPO_UP) {
								if (d < chop_div_cap()) { d <<= 1; o <<= 1; }   /* CHOPCAP-690 */
							} else {
								if (d > 1u) { d >>= 1; o >>= 1; }
							}
						} else if (vb == VOL_UP) {
							o = (o + 1u) % d;
						} else {                  /* VOL_DOWN */
							o = (o + d - 1u) % d;
						}
						g_chop_off = (d > 1u) ? (o % d) : 0u;
						g_chop_div = d;
						if (g_slot < NUM_SLOTS) { /* M7a: persist per song */
							chop_meta_encode(g_meta.chop[g_slot], d, g_chop_off);   /* CHOPCAP-690 */
							g_meta_save_req = 1;
						}
						g_chop_req = 1;           /* engine: snap to it */
						g_dip_req = 1;            /* M10: declick the jump */
						cp_rep_at = cp_cnt + 18;  /* M10: first repeat ~450 ms in */
						cp_rep_iv = 10;           /*      then ~250 ms, accelerating */
					} else if (cp_cnt > 3 && cp_rep_at && cp_cnt >= cp_rep_at &&
						   (vb == VOL_UP || vb == VOL_DOWN) &&
						   g_chop_div > 1u) {
						/* M10 HOLD-TO-GLIDE: keep shifting while the chord
						 * is held — declicked whole-window steps at an
						 * accelerating rate read as a tape scrub across
						 * the loop. Only the SHIFT buttons repeat: a
						 * repeating halve/double would sweep the whole
						 * div range in a blink. Double-click detection
						 * keys on press EDGES, so repeats can't fake it. */
						uint32_t d2 = g_chop_div;
						uint32_t o2 = g_chop_off;
						o2 = (vb == VOL_UP) ? (o2 + 1u) % d2
						                    : (o2 + d2 - 1u) % d2;
						g_chop_off = o2;
						if (g_slot < NUM_SLOTS) {
							chop_meta_encode(g_meta.chop[g_slot], d2, o2);   /* CHOPCAP-690 */
							g_meta_save_req = 1;  /* writer coalesces */
						}
						g_chop_defer = 1;   /* M24: glide is continuous */
						g_defer_t = k_uptime_get();   /* r3 */
						cp_rep_at = cp_cnt + cp_rep_iv;
						if (cp_rep_iv > 5) cp_rep_iv--;   /* floor ~125 ms */
					} else if (cp_cnt > 3 && cp_rep_at && cp_cnt >= cp_rep_at &&
						   (vb == VOL_TEMPO_UP || vb == VOL_TEMPO_DOWN)) {
						/* M15 LENGTH GLIDE: holding FN+FWD/RWD now
						 * repeats the halve/double too — a STEADY
						 * ~375 ms cadence, not the accelerating shift
						 * glide: only 7 sizes exist, so bottom-to-top
						 * takes ~2.3 s under full control (the M10
						 * blink-sweep objection was about speed, and
						 * this is the slow version marc asked for).
						 * At either end the hold idles harmlessly.
						 * Double-click reset still keys on press
						 * EDGES, so repeats can never fake it. */
						uint32_t d2 = g_chop_div, o2 = g_chop_off;
						if (vb == VOL_TEMPO_UP) {
							if (d2 < chop_div_cap()) { d2 <<= 1; o2 <<= 1; }   /* CHOPCAP-690 */
						} else {
							if (d2 > 1u)  { d2 >>= 1; o2 >>= 1; }
						}
						if (d2 != g_chop_div) {
							g_chop_off = (d2 > 1u) ? (o2 % d2) : 0u;
							g_chop_div = d2;
							if (g_slot < NUM_SLOTS) {
								chop_meta_encode(g_meta.chop[g_slot], d2, g_chop_off);   /* CHOPCAP-690 */
								g_meta_save_req = 1;
							}
							g_chop_defer = 1;   /* M24 */
							g_defer_t = k_uptime_get();   /* r3 */
						}
						cp_rep_at = cp_cnt + 15;   /* steady ~375 ms */
					}
					led_service();
					k_msleep(25);
					continue;                 /* chord owns the button */
				}
				cp_cand = VOL_NONE; cp_cnt = 0;
			}
			/* M14 HEADS v2: while FUNCTION is held with heads engaged,
			 * the faders are HEAD POSITIONS — absolute (grab = the head
			 * jumps to the fader; it's a scrub, jumping is the point),
			 * gated only by intent (move >=3 counts from the FN-down
			 * snapshot, the bank-jump brush guard). Each apply is
			 * rate-limited, re-anchors ONLY that head's ring, and asks
			 * for that track's blip. Engaging spends the press (M11a
			 * lesson: a scrubbing hold can never power off) and arms
			 * the volume re-cross latch for FUNCTION release. */
			if (heads_engaged()) {
				static int64_t hf_press = -1;
				static uint8_t hf_eng[NTRK];
				static int hf_snap[NTRK];
				static int64_t hf_at[NTRK];
				if (hf_press != press_start) {
					hf_press = press_start;
					for (int hf = 0; hf < NTRK; hf++) {
						hf_eng[hf] = 0; hf_snap[hf] = -1; hf_at[hf] = 0;
					}
				}
				int64_t hnow = k_uptime_get();
				for (int hf = 0; hf < NTRK; hf++) {
					int fv = ladder_read(&adc_ladder[LAD_FADER0 + hf]);
					if (fv < 0) continue;
					int q = (int)((uint32_t)fv * 256u / 3700u);
					if (q > 255) q = 255;
					if (hf_snap[hf] < 0) { hf_snap[hf] = q; continue; }
					if (!hf_eng[hf]) {
						int d = q - hf_snap[hf];
						if (d < 0) d = -d;
						/* M31: intent gate 3 -> 7 (~2.7% of travel). 3 was under the
						 * measured drift of a loose fader and under hand-wobble while
						 * holding FN for something else; one accidental crossing keeps
						 * the fader engaged for the WHOLE hold, which was the phantom
						 * head-scrub during unrelated FN gestures. */
						if (d < 7) continue;      /* intent gate */
						hf_eng[hf] = 1;
						combo_seen = 1;           /* press is spent */
						g_fh_latch[hf] = 1;
						g_fh_lastq[hf] = -1;
					}
					int dd = q - (int)g_head_pos[hf];
					if (dd < 0) dd = -dd;
					if (dd < 2) continue;             /* ADC deadband */
					if (hnow - hf_at[hf] < 45) continue;  /* rate limit */
					hf_at[hf] = hnow;
					g_head_blip[hf] = 3;
					g_head_pos[hf] = (uint8_t)q;
					trk[hf].p_w = (g_consume_pos / TSPB_SRC(hf)) * TSPB_SRC(hf);   /* HG-646 */
				}
			} else {
				/* M16 WINDOW FADERS: FUNCTION held, heads NOT engaged —
				 * faders 1-3 shape the free window (see the globals
				 * comment). Same rules as the heads scrub: absolute
				 * jump-on-grab, >=3-count intent gate, engagement spends
				 * the press and arms the volume re-cross latch. Applies
				 * are rate-limited and ride g_chop_req + the declick dip
				 * (every apply is a chop edit). Fader 4 is untouched. */
				static int64_t wf_press = -1;
				static uint8_t wf_eng[4];
				static int wf_snap[4];
				static int wf_s = 0, wf_e = 255;  /* RAW ends; s>e = reversed */
				static int wf_q3 = -1;
				static int wf_pend;
				static int64_t wf_at;
				static int wf_z = 255;   /* FADER4ZOOM-812: FN + fader 4, Q8. 255 = the coarse window as set, 0 = tightest */
				/* COARSEWIN-815: what this handler last APPLIED, as the seed below will read it back.
				 * If the audible window still matches, nothing outside has touched it and the COARSE
				 * pair in wf_s/wf_e is still the true parent -- so fader 4 zooms from the same window
				 * it zoomed from last time, instead of from its own output. wf_ab: 0 = the free window,
				 * 1 = the repeat's window, -1 = nothing applied yet (never matches). */
				static int16_t wf_as = -1, wf_ae = -1; static int8_t wf_ab = -1;
				if (wf_press != press_start) {
					wf_press = press_start;
					for (int wf = 0; wf < 4; wf++) {
						wf_eng[wf] = 0; wf_snap[wf] = -1;
					}
					{   /* FNSEED-812: the four FN faders edit ONE window, and it starts from WHAT IS AUDIBLE --
					     * not from the whole loop. Without this, touching any of them while a chop is live threw
					     * the window out to the take's full length before the fader did anything. */
						const uint32_t _cd = g_chop_div ? g_chop_div : 1u;
						int _s, _e, _bi;
						if (g_br_on && g_br_len8) {            /* the repeat's window, Q8 of the audible cycle */
							_bi = 1; _s = (int)g_br_s8; _e = _s + (int)g_br_len8 - 1;
						} else if (g_win_free) {               /* the free window already in force */
							_bi = 0; _s = (int)g_win_s8; _e = (int)g_win_e8;
						} else {                               /* the stepped chop, as Q8 of the loop */
							_bi = 2;
							_s = (int)((g_chop_off * 256u) / _cd);
							_e = _s + (int)(256u / _cd) - 1;
						}
						/* COARSEWIN-815: compare BEFORE the clamps -- this is the raw pair the apply recorded. */
						int _mine = (_bi == (int)wf_ab && _s == (int)wf_as && _e == (int)wf_ae);
						if (_s < 0) _s = 0; if (_s > 253) _s = 253;
						if (_e > 255) _e = 255;
						if (_e < _s + 2) _e = _s + 2;
						if (!_mine) { wf_s = _s; wf_e = _e; }   /* somebody else set it: adopt it as the new parent */
						wf_q3 = -1; wf_z = 255;                 /* and a hold always begins at 'no zoom' */
					}
				}
				int64_t wnow = k_uptime_get();
				for (int wf = 0; wf < 4; wf++) {
					int fv = ladder_read(&adc_ladder[LAD_FADER0 + wf]);
					if (fv < 0) continue;
					int q = (int)((uint32_t)fv * 256u / 3700u);
					if (q > 255) q = 255;
					if (wf_snap[wf] < 0) { wf_snap[wf] = q; continue; }
					if (!wf_eng[wf]) {
						int d = q - wf_snap[wf];
						if (d < 0) d = -d;
						/* M31: intent gate 3 -> 7, same reasoning as the heads loop.
						 * This is ALSO the row-49 fix: fader 4 here is the DJ filter,
						 * and its physical drift crossing the old 3-count gate while
						 * FN was held for chopping is what stripped the low end from
						 * all four loops (luuuciano's video, marc's random high-pass). */
						if (d < 7) continue;      /* intent gate */
						wf_eng[wf] = 1;
						combo_seen = 1;           /* press is spent */
						g_fh_latch[wf] = 1;
						g_fh_lastq[wf] = -1;
					}
					if (wf == 3) {
						/* FADER4ZOOM-812 (row 144): fader 4 = the window ZOOM -- it
						 * tightens the window faders 1/2/3 set, about its centre.
						 * It no longer drives the DJ filter: that was a SECOND handle
						 * on g_flt_pos (page 1 fader 1 writes the same state and keeps
						 * it), and its drift under a chop-hold is what once stripped
						 * the low end from all four loops. */
						int d = (wf_z < 0) ? 99 : q - wf_z;
						if (d < 0) d = -d;
						if (d >= 2) { wf_z = q; wf_pend = 1; }
					} else if (wf == 0) {
						int d = q - wf_s; if (d < 0) d = -d;
						if (d >= 2) { wf_s = q; wf_pend = 1; }
					} else if (wf == 1) {
						int d = q - wf_e; if (d < 0) d = -d;
						if (d >= 2) { wf_e = q; wf_pend = 1; }
					} else {
						int d = (wf_q3 < 0) ? 99 : q - wf_q3;
						if (d < 0) d = -d;
						if (d >= 2) {
							wf_q3 = q;
							/* SHIFT: absolute position, width AND
							 * order (= direction) preserved */
							int w = (wf_s <= wf_e) ? wf_e - wf_s
							                       : wf_s - wf_e;
							int base = (q * (255 - w)) >> 8;
							if (wf_s <= wf_e) { wf_s = base; wf_e = base + w; }
							else              { wf_e = base; wf_s = base + w; }
							wf_pend = 1;
						}
					}
				}
				if (wf_pend && wnow - wf_at >= 60) {
					wf_at = wnow; wf_pend = 0;
					int ws = wf_s, we = wf_e, rv = 0;
					if (ws > we) { int t2 = ws; ws = we; we = t2; rv = 1; }
					if (wf_z < 255) {   /* FADER4ZOOM-812: tighten about the CENTRE, coarse window preserved */
						int _c2 = ws + we, _w = we - ws;
						int _wz = (_w * wf_z) >> 8;
						if (_wz < 2) _wz = 2;
						ws = (_c2 - _wz) >> 1;
						if (ws < 0) ws = 0;
						we = ws + _wz;
						if (we > 255) { we = 255; ws = we - _wz; if (ws < 0) ws = 0; }
					}
					if (we - ws < 2) {          /* floor ~1/128 sliver */
						we = ws + 2;
						if (we > 255) { we = 255; ws = 253; }
					}
					if (g_br_on) {   /* BRFN-765: faders 1/2 = the repeat window's start / end, fader 3 = its shift; not the chop's free window */
						br_setwin((uint32_t)ws, (uint32_t)(we - ws));   /* BRCHOP-800: Q8 of the audible cycle */
						(void)rv;
						wf_as = (int16_t)ws; wf_ae = (int16_t)(we - 1); wf_ab = 1;   /* COARSEWIN-815: br_setwin stores a LENGTH, and the seed reads it back as s + len - 1 */
					} else {
					g_win_s8 = (uint8_t)ws;
					g_win_e8 = (uint8_t)we;
					g_win_rev = (uint8_t)rv;
					g_win_free = 1;
					wf_as = (int16_t)ws; wf_ae = (int16_t)we; wf_ab = 0;   /* COARSEWIN-815: the free window stores the END, read back as-is */
					/* M24 (geraasmasjien + luuuciano): this used to
					 * snap the rings and dip the master EVERY 60 ms
					 * for the whole sweep. The dip slams gain to 0
					 * and recovers over ~28 ms, so at a 60 ms cadence
					 * it is a ~16 Hz tremolo — and the snap threw away
					 * the read-ahead on top, which is the silence they
					 * described. Neither is needed while moving: the
					 * streamer re-reads g_win_* on every fill round,
					 * so the ring converges on the new window all by
					 * itself. Defer, and settle up on release. */
					g_chop_defer = 1;
					g_defer_t = k_uptime_get();   /* r3 */
					}   /* BRFN-765 */
				}
			}
			if (combo_seen) {
				/* The combo has been engaged this FUNCTION press: once PLAY
				 * is lifted, do NOTHING further for the rest of the hold —
				 * no power-off countdown, no shutdown (press_start still
				 * dates from the original FUNCTION-down, so the 2.5 s
				 * power-off would otherwise fire). The FUNCTION press is
				 * spent; it ends cleanly on release below. */
				led_service();
				k_msleep(25);
				continue;
			}

			int64_t held = k_uptime_get() - press_start;

			/* M8a: a HOLD right after a tap run = CLEAR this song's grid.
			 * The run also spends the press — never a power-off. */
			if (tap_n > 0 && k_uptime_get() - tap_last < 3000) {
				/* M20 F6: window widened 1500 -> 3000 ms — the
				 * any-time grid clear (tap FN once, then hold ~1 s)
				 * was real but nearly impossible to hit
				 * (geraasmasjien's "can't get back to free mode") */
				if (held >= 1000 && !combo_seen && fast_pair) {
					/* DOUBLE-TAP then hold = clear the grid.
					 * fast_pair alone decides this: a press
					 * landing within 280 ms of a tap is faster
					 * than any tempo, so it IS a double-click.
					 * M25-r3: the old extra tap_n < 4 test broke
					 * the gesture. The first tap of the double
					 * click is indistinguishable from another
					 * TEMPO tap, so after a 4-tap run it pushed
					 * tap_n to 5 and the hold fell through to the
					 * snap branch — marc's "sometimes it rounds
					 * instead of deleting". It only misfired when
					 * the delete-tap happened to land near the
					 * beat being tapped, which is why it was
					 * intermittent. Snap still needs tap_n >= 4
					 * AND a gap wider than 280 ms, which is what
					 * tapping a tempo and then holding gives you
					 * at any sane BPM (469 ms at 128). */
					g_grid_bpm_q8[g_slot] = 0;
					g_grid_n[g_slot] = 0u; g_grid_o[g_slot] = 0u;   /* GRIDCORE-733: the tape grid goes with it */
					g_grid_off_q8[g_slot] = 0;   /* NUDGE-717: the offset goes with the grid (the tail rides the same save) */
					g_gridrec_beat_samps = 0;    /* LOCKLOAD-725: no grid, no stored beat */
					g_grid_active = 0;
					g_grid_fresh = 0;
					g_grid_save_req = 1;
					tap_n = 0;
					fast_pair = 0;
					any_tap_t = 0;
					combo_seen = 1;      /* spend the press */
				} else if (held >= 1000 && !combo_seen && tap_n >= 4) {
					/* M23: a hold after a COMMITTED run (4+ taps)
					 * toggles integer-BPM snap. The press that
					 * lands here would otherwise do nothing, and
					 * a held press never registers as a tap (the
					 * tap branch needs a release under 600 ms),
					 * so tap_n is still the run's count. 1-3 taps
					 * then hold stays the grid clear, exactly as
					 * documented. */
					/* M23-r5: a ONE-SHOT nudge, not a mode. The
					 * grid you just tapped is pulled onto the
					 * nearest whole BPM, once, right now — and
					 * then everything behaves exactly as normal.
					 * Nothing to remember, nothing to turn off,
					 * no state to read off the panel afterwards. */
					if (g_grid_active && g_grid_beat_frames && g_loop_len && g_slot < NUM_SLOTS && g_grid_n[g_slot]) {
						/* GRIDCORE-733: a loaded song snaps by its SPEED -- the loop is
						 * untouched (n beats in L samples is the tempo), so the snap is
						 * exact and reversible. bf = the wall beat now; nb = the wall
						 * beat of the nearest whole BPM; speed *= bf / nb. */
						uint32_t nb = bpm_snap(g_grid_beat_frames);
						g_snap_took = (nb && nb != g_grid_beat_frames);
						if (g_snap_took) {
							uint64_t sp = ((uint64_t)g_play_speed_q16 * g_grid_beat_frames + nb / 2u) / nb;
							if (sp < 16384u) sp = 16384u;
							else if (sp > 98304u) sp = 98304u;
							g_play_speed_q16 = (uint32_t)sp;
							g_play_bpm = (int)((sp * 80u + 32768u) >> 16);
							if (g_play_bpm < BPM_MIN) g_play_bpm = BPM_MIN;
							if (g_play_bpm > BPM_MAX) g_play_bpm = BPM_MAX;
							g_det_bpm = (int)(((uint64_t)LOOP_RATE * 60u + nb / 2u) / nb);
						}
					} else if (g_grid_active && g_grid_beat_frames) {
						uint32_t nb = bpm_snap(g_grid_beat_frames);
						g_snap_took = (nb && nb != g_grid_beat_frames);
						if (g_snap_took) {
							beat_set(nb);   /* r7: already grid-domain */
							g_det_bpm = (int)(((uint64_t)LOOP_RATE *
								60u + nb / 2u) / nb);
							for (int k3 = 0; k3 < NTRK; k3++) {
								uint32_t Lk = trk[k3].len_samps;
								if (!Lk || !g_gridrec_beat_samps)
									continue;
								uint32_t Nk = (Lk +
									g_gridrec_beat_samps / 2u) /
									g_gridrec_beat_samps;
								if (!Nk) continue;
								uint32_t Ln = Nk * nb;
								/* M25-r8: same truncating clamp as the
								 * convergence path — see there. */
								uint32_t dL3 = (Ln > Lk) ? (Ln - Lk)
										         : (Lk - Ln);
								if (Ln && (uint64_t)dL3 * 16u <=
								          (uint64_t)Lk)
									trk[k3].len_samps = Ln;
								if (trk[k3].state == TS_PLAY)
									trk[k3].p_w =
										(g_consume_pos / TSPBI(k3))
										* TSPBI(k3);
							}
							/* r7: beat_set did the rec beat too */
							g_dip_req = 1;   /* declick, as ever */
						}
					}
					if (g_snap_took) {
						g_snap_sweep = 48;   /* budget; the catch ends it */
					} else {
						/* M25-r10: declined — the tapped tempo is not
						 * near a whole number (or is out of range). Say
						 * so. Reuses the bounce's shrug: all four
						 * double-blink. Silence would be worse than
						 * either outcome, because the gesture and the
						 * grid-clear share a shape and you would not
						 * know which one you had just missed. */
						g_led_shrug = 20;
					}
					tap_n = 0;
					combo_seen = 1;      /* spend the press */
				}
				led_service();
				k_msleep(25);
				continue;
			}

			/* M28 (luuuciano): HOLD FN both rounds a tapped BPM and powers the
			 * device off, and people were switching it off mid-performance
			 * reaching for the musical gesture. Only honour the power-off hold
			 * when the tape is STOPPED — his own suggestion, and it also frees
			 * a plain HOLD FN while playing for future use. To power off, stop
			 * the tape first. */
			/* M31-r2: also allow power-off when NO LOOP EXISTS. A song switch
			 * onto an empty slot leaves g_playing latched with nothing loaded,
			 * and the M28 gate then refused power-off on a silent device. */
			if (held >= HOLD_MS_TO_OFF && (!g_playing || !g_loop_active))
				power_off();             /* never returns */

			/* show the power-off countdown only once it's clearly a hold, so a
			 * quick tap (song change) doesn't flash it. Clear BOTH rows so the
			 * countdown fills cleanly against a dark track row. */
			if (held > 400 && !g_snap_sweep && (!g_playing || !g_loop_active)) {
				/* M23: a pending snap sweep owns the display. The
				 * countdown clears BOTH rows every 25 ms and skips
				 * led_service, so without this the confirmation was
				 * invisible for as long as the finger stayed down —
				 * which is the whole duration of the gesture. */
				int lit = (int)((held * NUM_LEDS) / HOLD_MS_TO_OFF) + 1;
				if (lit > NUM_LEDS) lit = NUM_LEDS;
				all_off();
				track_all_off();
				for (int i = 0; i < lit; i++) led_on(i);
			} else if (held > 400 && g_snap_sweep) {
				all_off();          /* side row dark; sweep is the message */
				led_service();
			} else {
				/* LED-549 r16: a PLAIN FUNCTION hold. Keep servicing the
				 * LEDs -- this is the branch the song-indicator escape
				 * hatch lives on, and without it the row just freezes. */
				led_service();
			}
			k_msleep(25);
			continue;
		}

		if (press_start >= 0) {                  /* just released */
			/* v1.2.2-r4: releasing FUNCTION first (or both together —
			 * the natural way to end the chord) must ALSO fire the
			 * release-toggle; before, only a PLAY-first release did,
			 * so the gesture silently aborted most of the time (user:
			 * "mode takes ~4 s" = retries until a lucky stagger). */
			if (combo_start >= 0 && !combo_fired && fnp_presses < 2) {
				int64_t fnp_held2 = k_uptime_get() - combo_start;
				if (fnp_held2 >= 350 && fnp_held2 < 5000)
					fnp_mode_toggle();
			}
			if (!combo_seen &&
			    (k_uptime_get() - press_start) < 600) {
				/* M8a: FN-tap = TAP TEMPO (navigation moved into the
				 * FN hold). 1-3 taps: nothing. 4+ taps in steady
				 * rhythm: commit the grid — tempo from mean spacing,
				 * downbeat = the first tap. Every further tap refines. */
				/* M20 F9: the tap happened when the button went
				 * DOWN. Timing it at the release planted the whole
				 * grid late by however long the finger stayed on the
				 * button — tens of ms, different every tap — and F8
				 * could never see it, because refinement corrects
				 * SPACING and leaves PHASE alone. */
				int64_t tnow = press_start;
				uint64_t snow = press_start_s;
				/* M23-r6: a DOUBLE-TAP is faster than any tempo.
				 * The grid runs 50-200 BPM, i.e. 300-1200 ms
				 * between taps, so anything under 280 ms cannot
				 * be someone tapping time — it can only be a
				 * deliberate double. That is now what separates
				 * "clear the grid" from "round the BPM", instead
				 * of the tap COUNT, which the two gestures kept
				 * confusing each other over. */
				any_tap_t = tnow;
				if (tap_n > 0 && (tnow - tap_last > 1500 ||
				                  tnow - tap_last < 200)) tap_n = 0;
				if (tap_n > 1) {
					int64_t mean = (tap_last - tap_first) / (tap_n - 1);
					int64_t dvi = (tnow - tap_last) - mean;
					if (dvi < 0) dvi = -dvi;
					if (dvi * 5 > mean) tap_n = 0;  /* >20% off: new run */
				}
				if (tap_n == 0) { tap_first = tnow; tap_first_s = snow; }
				tap_last = tnow; tap_last_s = snow; tap_n++;
				if (tap_n >= 4) {
					/* M20 F9: the grid spacing comes from the SAMPLE
					 * clock — the same clock the audio is written
					 * with — instead of the millisecond uptime it used
					 * to be rounded through. One division, no trip
					 * through BPM and back, and 48000 ticks per second
					 * of resolution instead of 1000. */
					uint32_t nf = (uint32_t)((tap_last_s - tap_first_s) /
								 (uint64_t)(tap_n - 1));
					if (nf >= (48000u * 60u) / 200u &&
					    nf <= (48000u * 60u) / 50u) {   /* 50..200 BPM */
						uint32_t bpmq8 = (uint32_t)
							((48000ULL * 60u * 256u) / nf);
						/* M8c BEATMATCH: if this song already has
						 * loops, the tap run means "match THIS" —
						 * capture their native tempo first. */
						/* TAPFIX-663: the loops' native tempo AT 1x. The grid follower
						 * keeps ref_nf * ref_spd = the loops' beat in TAPE samples
						 * (W301), so that product is the reference whatever the tape
						 * is doing now; a saved wall tempo is normalised by the speed
						 * it plays at; an ungridded song's beat is tape-domain already.
						 * (The old read took the WALL tempo of the last setting -- the
						 * previous tap's -- so tap 5 retuned to ~1x and undid tap 4.) */
						uint32_t native_q8 = 0;
						if (g_loop_len > 0u) {
							if (g_grid_n[g_slot]) {   /* GRIDCORE-733: n beats in L samples, at 1x */
								native_q8 = (uint32_t)((48000ULL * 60u * 256u * g_grid_n[g_slot]) / g_loop_len);
							} else if (g_grid_bpm_q8[g_slot] && g_play_speed_q16) {
								native_q8 = (uint32_t)(((uint64_t)g_grid_bpm_q8[g_slot] << 16) / g_play_speed_q16);
							} else if (g_beat_samples) {
								native_q8 = (uint32_t)
									((48000ULL * 60u * 256u) /
									 g_beat_samples);
							}
						}
						g_grid_bpm_q8[g_slot] = (uint16_t)bpmq8;
						/* GRIDLOCK-720: g_grid_beat_frames is published LAST (below), after the
						 * retune and after the follower's reference pair -- the follower adopts
						 * (nf, g_play_speed_q16) the moment nf changes, and a stale speed there
						 * was a grid off by the retune ratio (row 127). */
						g_grid_fresh = 1;   /* M20 F1: taps = truth */
						g_grid_anchor = tap_first_s;
						g_grid_next_tick = g_sample_clock;
						g_grid_active = 1;
						g_grid_save_req = 1;
						if (native_q8) {
							/* retune the tape so the loops play at
							 * the tapped tempo (vinyl rules: pitch
							 * moves too), clamped to the physical
							 * 0.5-1.5x range, and restart the loops
							 * on the tapped downbeat at the next
							 * bar line — tempo AND phase matched. */
							uint64_t sp = ((uint64_t)bpmq8 << 16) /
								      native_q8;
							if (sp < 16384u) sp = 16384u;          /* RANGE-655: 0.25x */
							else if (sp > 98304u) sp = 98304u;     /* CAP-665: 1.5x */
							/* TAPFIX-663 (marc): the loops are NOT restarted by a tap run any
							 * more -- they keep playing and the tape glides to the tapped tempo
							 * (M8c's bar-line restart on the tapped '1' read as the loop
							 * 'repeating from the beginning before it finishes'). The grid's
							 * downbeat is still the first tap; the loops keep their own phase. */
							g_play_speed_q16 = (uint32_t)sp;
							g_play_bpm = (int)((sp * 80u + 32768u) >> 16);
							if (g_play_bpm < BPM_MIN) g_play_bpm = BPM_MIN;
							if (g_play_bpm > BPM_MAX) g_play_bpm = BPM_MAX;
						}
						if (g_loop_len > 0u) {
							/* GRIDCORE-733: a loaded song. The loop keeps n (the tape was
							 * retuned to the taps above); an ungridded loop gets its count
							 * from the taps; the "1" moves to the first tap -- the service
							 * converts that wall frame to a loop sample with a coherent (W, P).
							 * bpm_q8 = the loop's 1x tempo (speed-invariant). */
							if (!g_grid_n[g_slot]) {
								uint64_t bs = ((uint64_t)nf * g_play_speed_q16) >> 16;
								uint32_t nb = bs ? (uint32_t)(((uint64_t)g_loop_len + bs / 2u) / bs) : 0u;
								if (nb < 1u) nb = 1u;
								if (nb > 255u) nb = 255u;
								g_grid_n[g_slot] = (uint8_t)nb;
							}
							g_grid_bpm_q8[g_slot] = (uint16_t)((48000ULL * 60u * 256u * g_grid_n[g_slot]) / g_loop_len);
							g_grid_o_req_w = tap_first_s; g_grid_o_req = 1u;
						} else {
							g_grid_beat_frames = nf;   /* F9: exact (the clock grid of an empty song) */
							g_grid_anchor_e = grid_anchor_eff();   /* STACKT-716 */
						}
					}
				}
			}
			all_off();
			/* If the combo was ended by lifting FUNCTION FIRST while PLAY is
			 * still down, swallow that trailing PLAY until it is released, so
			 * it can't leak into the normal decode as a restart / play-stop. */
			if (combo_seen &&
			    ladder_read(&adc_ladder[LAD_TRACKS]) >= 110) suppress_play = 1;
			/* PF-549 r17: a HOLD previews the songs (see the FN-held arm of
			 * the LED router) and leaves the page alone; only a TAP closes.
			 * Same 600 ms window FN already uses for tap-tempo. */
			if (g_pg_open && !combo_seen &&
			    (k_uptime_get() - press_start) < 600) {
				g_pg_sweep = 0;    /* LED-549 r11: cancel an entry mid-flight */
				g_pg_exit  = 24;   /* LED-549 r11: hold, then walk back out */
				
				/* PF-549 r15 (marc): HAND THE FADERS BACK WITH PICKUP.
				 * Closing a page dropped the faders straight onto the track
				 * volumes with no catch, so the very next poll wrote the
				 * PHYSICAL position into trk[].vol_q8 -- open the page, run
				 * the FX fader to the top, close, and a track that had been
				 * all the way DOWN came back all the way UP. The page takes
				 * the faders with pickup and has to return them the same
				 * way. This is the SAME latch heads mode uses on ITS exit;
				 * the volume simply stays put until a fader crosses it. */
				for (int _f = 0; _f < 4; _f++) {
					g_fh_latch[_f] = 1;
					g_fh_lastq[_f] = -1;
				}
				g_pg_open = 0;   /* PF-545 r3: a BARE FN tap closes the
				                  * page (W156). EDGE-ONLY -- inside the
				                  * release block; r2 ran every pass and
				                  * closed one pass after combo_seen
				                  * cleared. Any combo (jump, chop, the
				                  * opening dwell) leaves it OPEN. */
			}
		}
		g_fn_held = 0;   /* LED-549 */
		press_start = -1;
		combo_start = -1;
		combo_fired = 0;
		combo_seen  = 0;
		if (g_chop_defer) {
			/* M24: the gesture is over — pay once. The rings have been
			 * tracking the window all along, so this is a latency snap
			 * rather than a correction; the single dip covers whatever
			 * splice the last edit left in flight. Discrete presses do
			 * NOT come through here — they still take effect instantly,
			 * because chop is a rhythmic gesture and immediacy is the
			 * whole point of it. */
			g_chop_defer = 0;
			if (k_uptime_get() - g_defer_t < 150) {
				/* released mid-motion: the last edit's splice may still be
				 * in flight — settle as before (snap + one covering dip). */
				g_chop_req = 1;
				g_dip_req = 1;
			}
			/* r3: released after settling — the rings converged on the final
			 * window rounds ago (see the M24 comment above); snapping and
			 * dipping here was the audible FN-release silence. Skip both. */
		}
		if (fnp_pend_snap) {   /* M15-r3: released mid-window — still a double */
			fnp_pend_snap = 0;
			g_play_speed_q16 = 65536u;
			g_play_bpm = 80;
		}
		bj_cand = TRK_NONE; bj_cnt = 0; bj_fired = -1; fnp_edge = -1; fnp_chain = 0;
		if (fnr_mode == 1) iso_release();   /* ISO2-643: FN lifted first */
		fnr_cand = -1; fnr_cnt = 0; fnr_held = 0; fnr_off = 0; fnr_mode = 0; fnr_pre = 0;   /* REV2-641 / ISO2-643 */
		pg_pend = -1;   /* PF-545: the momentary close is GONE (sticky) */
		fnp_presses = 0;
		cp_cand = VOL_NONE; cp_cnt = 0;
		fv_cand = VOL_NONE; fv_cnt = 0;   /* MOD-654 */

		/* ---- looper controls + LEDs ---- */
		{
			/* FAILSAFE: Track1+Track4 combo (AIN0 ~1325, between T4 1220 and PLAY
			 * 1823) held ~1.2 s -> reset into the bootloader for reflashing. Checked
			 * BEFORE the normal decode so the combo isn't mistaken for a Track-4 press. */
			int trk_raw = ladder_read(&adc_ladder[LAD_TRACKS]);
			const uint8_t _volp = (ladder_read(&adc_ladder[LAD_VOL]) >= 200) ? 1u : 0u;   /* BRSAG2-778: a VOL-ladder button is down (the rail sags, W341) */
			{	/* MUTEFIX4-743 (W341): with BOTH VOL buttons down the shared rail sags and PLAY
				 * (~1807) reads inside the ALL4 chord band (1713-1773); the chord's release then
				 * muted every track. While the VOL ladder reads the pair, anything from the 3+4
				 * band up to PLAY's floor IS PLAY (a real track chord under both VOL is no gesture). */
				if (_volp) {   /* BRSAG2-778: one read per pass */   /* BEATREP-749: ANY VOL-ladder press (was the pair only, >= 1910): the rocker rides the same rail */
					if (trk_raw >= 1509) g_mt_tr = (uint16_t)trk_raw;
					if (trk_raw >= 1509 && trk_raw < 1840) trk_raw = 1823;
				}
			}
			static int64_t combo14_t = -1;     /* when the 1+4 band was first seen */
			enum trk_btn raw;
			/* This DFU check runs BEFORE ctl_flush is consumed below, so clear
			 * the stale 1+4 timestamp here: after a FUNCTION+PLAY mode toggle
			 * (which freezes this block for the whole combo) a PLAY release
			 * sweeping through the 1280-1390 band must not find a >1.2 s-old
			 * combo14_t and reboot to the bootloader mid-performance. */
			static uint8_t combo_held;         /* M27: tracks seen during this gesture */
			static int combo_cand, combo_cnt;  /* M27-r3: two-pass confirm */
			/* BNC-597: PLAY + ONE TRACK = BOUNCE INTO THAT TRACK. */
			static int     bch_cand = -1, bch_cnt;   /* two-pass confirm, as the combos */
			static uint8_t bch_held;                 /* fired: swallow the release sweep */
			static int     bch_fire = -1;            /* this pass: the chord landed on TN */
			static uint8_t ep_play_spent;            /* PLAY was a modifier this episode */
			if (ctl_flush) { combo14_t = -1; combo_held = 0; combo_cand = 0; combo_cnt = 0;
			                 bch_cand = -1; bch_cnt = 0; bch_held = 0; bch_fire = -1; ep_play_spent = 0; }
			/* ===== M27-r3 COMBO DECODE =====================================
			 * Any set of track buttons pressed together makes its OWN code on this
			 * ladder. All sixteen states measured on hardware, all separable
			 * (SP1-BUTTON-LADDER-MAP.md):
			 *
			 *   idle    2 | T1   213 | T2    404 | 1+2   572 | T3    728
			 *   1+3   862 | 2+3  989 | 1+2+3 1100 | T4   1209 | 1+4  1303
			 *   2+4  1391 | 1+2+4 1470 | 3+4  1548 | 1+3+4 1618
			 *   2+3+4 1683 | ALL4 1743 | PLAY 1804
			 *
			 * Tightest gap 60 counts against +/-9 of noise. Bands are midpoints;
			 * anything unlisted falls through to decode_tracks(), which still owns
			 * idle / T1 / T2 / T3 / T4 / PLAY.
			 *
			 * THE HARD PART IS NOT THE BANDS, IT IS THE EDGES. Fingers neither
			 * land nor lift together, so pressing 2+3+4 walks
			 *   idle -> T2 -> 2+3 -> 2+3+4 -> 2+3 -> T2 -> idle
			 * Hence three rules, each of which fixed a real hardware symptom:
			 *  1. ACCUMULATE the mask (|=). r2 assigned it, so the 2+3 on the way
			 *     OUT overwrote 2+3+4 and track 4 was never toggled — 'muting 3
			 *     leaves one behind'.
			 *  2. Toggle only at TRUE IDLE, not merely when no combo is present:
			 *     the release sweep sits in single-button bands for several passes.
			 *  3. Require a combo to hold for TWO passes before accepting it. A
			 *     fast single press can transit a combo band for one sample on the
			 *     way up, which would otherwise mute tracks nobody pressed.
			 *
			 * Bit order: 1<<0 = track 1 ... 1<<3 = track 4. */
			int combo_now = 0;
			if      (trk_raw >=  488 && trk_raw <  650) combo_now = 0x3; /* 1+2   ~572  */
			else if (trk_raw >=  795 && trk_raw <  925) combo_now = 0x5; /* 1+3   ~862  */
			else if (trk_raw >=  925 && trk_raw < 1044) combo_now = 0x6; /* 2+3   ~989  */
			else if (trk_raw >= 1044 && trk_raw < 1154) combo_now = 0x7; /* 1+2+3 ~1100 */
			else if (trk_raw >= 1256 && trk_raw < 1347) combo_now = 0x9; /* 1+4   ~1303 */
			else if (trk_raw >= 1347 && trk_raw < 1430) combo_now = 0xA; /* 2+4   ~1391 */
			else if (trk_raw >= 1430 && trk_raw < 1509) combo_now = 0xB; /* 1+2+4 ~1470 */
			else if (trk_raw >= 1509 && trk_raw < 1583) combo_now = 0xC; /* 3+4   ~1548 */
			else if (trk_raw >= 1583 && trk_raw < 1650) combo_now = 0xD; /* 1+3+4 ~1618 */
			else if (trk_raw >= 1650 && trk_raw < 1713) combo_now = 0xE; /* 2+3+4 ~1683 */
			else if (trk_raw >= 1713 && trk_raw < 1773) combo_now = 0xF; /* ALL4  ~1743 */
			/* BNC-597: PLAY + TN, measured on 508 under load (spread <= 10):
			 *   PLAY 1807 | +T1 1861 | +T2 1910 | +T3 2004 | +T4 2159 | +ALL4 2376
			 * Bands at the midpoints, the T1 floor raised to 1840 (33 above PLAY's
			 * ceiling). PLAY + two or more tracks is NOT a gesture (W115): every
			 * reading above the T4 band is swallowed. A chord must hold for two
			 * passes and fires ONCE; everything until the ladder is idle again is
			 * its release sweep -- which passes through PLAY (1807) and, if PLAY
			 * lifts first, through the bare track band, where a fresh press would
			 * otherwise read as the STOP tap 48 ms later. */
			{	/* TAPECOPY-684: the tap/hold verdict on the bounce chord. Armed below when the
				 * chord fires; TN lifted (the ladder drops out of the chord bands) before
				 * BK_DECIDE_MS = a tap = TAPE COPY; still down at BK_DECIDE_MS = SAMPLER. */
				static int64_t bk_dec_t;
				if (g_bnc_arm_t != 0) { bk_dec_t = g_bnc_arm_t; g_bnc_arm_t = 0; g_bk_hold_trk = -1; }
				if (bk_dec_t != 0) {
					int64_t _tn = k_uptime_get();
					if (trk_raw < 1840) { g_bk_mode = 1u; g_bk_spd = 0u; bk_dec_t = 0; }
					else if (_tn - bk_dec_t >= BK_DECIDE_MS) { g_bk_mode = 2u; g_bk_hold_trk = g_bk_trk_ctl; bk_dec_t = 0; }   /* HOLDSTOP-686: a held bounce */
				}
			}
			int bchord = -1;
			if      (trk_raw >= 1840 && trk_raw < 1886) bchord = 0;   /* PLAY+T1 ~1861 */
			else if (trk_raw >= 1886 && trk_raw < 1957) bchord = 1;   /* PLAY+T2 ~1910 */
			else if (trk_raw >= 1957 && trk_raw < 2081) bchord = 2;   /* PLAY+T3 ~2004 */
			else if (trk_raw >= 2081 && trk_raw < 2267) bchord = 3;   /* PLAY+T4 ~2159 */
			if (trk_raw >= 1840) {
				raw = TRK_NONE;          /* never PLAY, never a track */
				combo14_t = -1;
				if (bchord >= 0 && (!suppress_play || g_br_on)) {   /* REV2-641: FN lifted first out of FN+PLAY+TN is not a bounce -- BRBNC3-767: unless a repeat is live (its FN layer parks that chord machine) */
					if (bchord == bch_cand) { if (bch_cnt < 3) bch_cnt++; }
					else { bch_cand = bchord; bch_cnt = 1; }
					/* BNC2-600: fires once per PRESS (the count passes 2 exactly
					 * once; a lift resets it below), not once per hold of PLAY. */
					if (bch_cnt == 2) { bch_held = 1; bch_fire = bchord; }
				} else if (!bch_held) {   /* BRSTOP-775: a chord still held (FN lifted over it) keeps its count -- it must not re-fire */
					bch_cand = -1; bch_cnt = 0;
				}
			} else if (bch_held) {
				/* the chord's release sweep: nothing in it is a gesture. The
				 * track has lifted (PLAY may still be down): re-arm the chord so
				 * the next PLAY+TN press fires again (BNC2-600). */
				raw = TRK_NONE;
				combo14_t = -1;
				combo_cand = 0; combo_cnt = 0;
				if (!(trk_raw >= 1509 && _volp)) { bch_cand = -1; bch_cnt = 0; }   /* BRSAG2-778: a VOL press sags the chord into the PLAY region -- not a release, the count stays (no re-fire on the rocker's lift) */
				if (trk_raw >= 0 && trk_raw < 110) bch_held = 0;
			} else if (combo_now) {
				raw = TRK_NONE;          /* a combo must never reach the single decode */
				if (combo_now == combo_cand) { if (combo_cnt < 3) combo_cnt++; }
				else { combo_cand = combo_now; combo_cnt = 1; }
				if (combo_cnt >= 2) combo_held |= (uint8_t)combo_now;   /* rule 1 + 3 */
				if (g_fxrst_lock) {
					/* FXRST2-564: an FX reset just fired under FN. If FN is
					 * lifted before the tracks -- which is the natural way to
					 * let go -- the chord arrives here as a bare 1+4 and would
					 * mute tracks 1 and 4, or (held long enough) reach the
					 * bootloader. Swallow it until the ladder is truly idle.
					 * ⚠ THIS is the real hazard. 563 guarded an imaginary one
					 * in code that never executes. */
					combo14_t  = -1;
					combo_held = 0;
					raw = TRK_NONE;
				} else if (combo_now == 0x9) {  /* ONLY exactly 1+4 arms the bootloader */
					/* time-based (not a +8/iter counter) so the diag-print path
					 * can't skew the threshold.
					 * ⚠ UNGUARDED BY DESIGN. There is no reset pin; this is the
					 * only way back. FN cannot reach here anyway (the FN branch
					 * continues out ~230 lines above), so a modifier test would
					 * be theatre. */
					if (combo14_t < 0) combo14_t = k_uptime_get();
					else if (k_uptime_get() - combo14_t >= DFU_HOLD_MS) enter_dfu();
				} else {
					combo14_t = -1;
				}
			} else if (combo_held) {
				/* Mid-release: still sweeping down through single-button bands.
				 * Swallow everything and wait for TRUE idle (rule 2). */
				raw = TRK_NONE;
				combo14_t = -1;
				combo_cand = 0; combo_cnt = 0;
				if (trk_raw >= 0 && trk_raw < 110) {
					for (int _p = 0; _p < NTRK; _p++) {
						if (!(combo_held & (1u << _p))) continue;
						if (trk[_p].state == TS_EMPTY) continue;  /* nothing to mute */
						trk[_p].muted ^= 1u;
						if (g_slot < NUM_SLOTS) {
							if (trk[_p].muted)
								g_meta.song_mode[g_slot] |= (uint8_t)(0x10u << _p);
							else
								g_meta.song_mode[g_slot] &= (uint8_t)~(uint8_t)(0x10u << _p);
						}
					}
					if (g_slot < NUM_SLOTS) g_meta_save_req = 1;  /* mutes persist */
					g_mt_cmb++;   /* MUTEFIX4-743 */
					combo_held = 0;
					suppress_play = 1;
				}
			} else {
				combo14_t = -1;
				g_fxrst_lock = 0;   /* FXRST2-564: ladder idle, lock released */
				combo_cand = 0; combo_cnt = 0;
				raw = decode_tracks(trk_raw);
			}
			/* trailing-PLAY guard (see the FUNCTION+PLAY combo exit): ignore
			 * the ladder until the RAW reading goes fully idle once, so a PLAY
			 * still held after the mode toggle — and its whole release sweep
			 * down through the track bands — never reaches the decode. Idle
			 * means the reading itself: 1280-1390 decodes as NONE but is NOT
			 * idle, and clearing there would expose the rest of the sweep. */
			if (suppress_play) {
				if (trk_raw >= 0 && trk_raw < 110) suppress_play = 0;
				else raw = TRK_NONE;
			}

			/* STICKY DEBOUNCE -> `committed` (the stable, settled button). Recording
			 * stops on RELEASE, so a single noisy ADC sample (audio/USB activity
			 * couples into the button ladder while a loop streams) must NOT look like
			 * a release: the committed button only changes after a DIFFERENT value is
			 * seen on 3 consecutive reads (~24 ms); a lone glitch back to the held
			 * value resets the counter, so a steady hold can never false-trigger. */
			static enum trk_btn committed = TRK_NONE, cand = TRK_NONE;
			static int cand_cnt;
			static int64_t cand_t0;              /* M96: first sighting of `cand` */
			static int64_t press_t[NTRK];        /* when committed first named this track */
			static int64_t tap_deadline[NTRK];   /* >0: a single tap awaiting a possible 2nd */
			static uint8_t armed_press[NTRK];    /* this press already armed a take */
			/* M44-r2 instant-arm bookkeeping: when the current arm fired,
			 * whether it was an instant EMPTY arm (migration only ever
			 * cancels those), and whether a press edge arrived from a
			 * HIGHER band (= that button's release down-sweep). */
			static int64_t arm_t;
			static uint8_t arm_was_instant;
			static uint8_t press_from_above[NTRK];
			static int stop_tap_trk = -1;        /* R1: stop already fired at press;
			                                      * swallow that press's release */
			static int64_t ep_time[TRK_PLAY + 1];/* committed ms per button, this episode */
			static int64_t ep_since;             /* when `committed` last changed */
			static uint8_t ep_open;              /* a press episode is in progress */
			static uint8_t ep_play_held;         /* this episode's PLAY press became a hold */
			static int64_t play_t = -1;          /* when PLAY was committed (hold timing) */
			static int     play_held;            /* this PLAY press already fired the restart */
			/* FUNCTION (or a USB transfer) owned the loop since the last pass
			 * here, so every static above is stale: a PLAY committed just
			 * before the combo froze this block would otherwise look like a
			 * long hold (phantom restart) and its open episode would fire a
			 * phantom play/stop on release. Reset everything and swallow the
			 * ladder until it reads idle. */
			if (ctl_flush) {
				ctl_flush = 0;
				committed = TRK_NONE; cand = TRK_NONE; cand_cnt = 0; cand_t0 = 0;
				for (int k = TRK_1; k <= TRK_PLAY; k++) ep_time[k] = 0;
				ep_open = 0; ep_play_held = 0; ep_play_spent = 0;
				play_t = -1; play_held = 0;
				for (int k = 0; k < NTRK; k++) { tap_deadline[k] = 0; armed_press[k] = 0; }
				stop_tap_trk = -1;
				if (!(trk_raw >= 0 && trk_raw < 110)) suppress_play = 1;
				raw = TRK_NONE;
			}
			enum trk_btn before = committed;
			/* M96: TIME-BASED commit. Was `++cand_cnt >= 3`, i.e. three
			 * control passes -- which reads as 24 ms only while the pass
			 * runs every 8 ms. k_msleep(8) is a FLOOR; under corner load
			 * the pass stretches and the gesture stretched with it.
			 * Healthy behaviour is IDENTICAL: at an 8 ms pass the 2nd
			 * confirming read is at 16 ms (< 24), so commit still lands
			 * on the 3rd at ~24 ms. Glitch rejection unchanged -- a lone
			 * bad read still falls to the else and restarts the window. */
			if (raw == committed) {
				cand_cnt = 0;
			} else if (raw == cand) {
				int64_t _el96 = k_uptime_get() - cand_t0;
				if (++cand_cnt >= 2 && _el96 >= BTN_DEBOUNCE_MS) {
					committed = raw; cand_cnt = 0;
					g_stop_lat_ms = (uint32_t)_el96;
					if ((uint32_t)_el96 > g_stop_lat_max)
						g_stop_lat_max = (uint32_t)_el96;
				}
			} else {
				cand = raw; cand_cnt = 1; cand_t0 = k_uptime_get();
			}

			/* TRACK buttons:
			 *   HOLD (button physically down >= HOLD_RECORD_MS) -> RECORD (auto-start
			 *      then captures from the first sound). A quick tap never lasts this long.
			 *   TAP (released before that) -> MUTE / unmute.
			 *   DOUBLE-TAP (a 2nd tap within DTAP_GAP_MS of the 1st tap's release) -> DELETE.
			 * Tap-vs-hold is decided by the PHYSICAL down-time and double-tap by the
			 * rhythm of two quick taps, so taps/double-taps stay reliable regardless of
			 * how fast recording arms (a quick ~HOLD_RECORD_MS hold instead of 300ms).
			 *
			 * PRESS EPISODE tracker: one episode = the ladder leaving idle
			 * until it settles back at idle. A finger pressing or releasing a
			 * HIGHER ladder button sweeps the voltage THROUGH the lower
			 * buttons' bands, and the debounce can commit one of them for a
			 * beat (~24-32 ms) on the way — the old code treated every
			 * committed change as a real release edge and fired PHANTOM taps
			 * ("recording track 4 muted track 1"). Now committed-time is
			 * accumulated per button and the release action fires ONCE, at
			 * episode end, for the DOMINANT (longest-committed) button.
			 * Three rules keep the phantom window closed:
			 *   - a press edge wipes the accumulated time of every band BELOW
			 *     it (provably the up-sweep in transit, not a press);
			 *   - the episode only ends when the RAW reading is idle, so a
			 *     slow release dwelling in the 1280-1390 no-man's band (which
			 *     decodes as NONE) can't split one gesture into two;
			 *   - a dominant under 40 ms fires nothing (a real tap commits
			 *     ~40 ms+, a transit blip caps at ~32 ms per traversal).
			 * Hold actions (arm, restart) are duration-based and transit-immune. */
			if (committed != before) {
				int64_t tnow = k_uptime_get();
				if (before != TRK_NONE)
					ep_time[(int)before] += tnow - ep_since;
				ep_since = tnow;
				if (committed != TRK_NONE) {
					ep_open = 1;
					if (before == TRK_NONE) ep_play_spent = (uint8_t)(g_vol_pair || g_br_on);   /* BNC-597: a fresh episode -- MUTEFIX3-741 / BRSPENT-753: unless the VOL pair is still held or a repeat is live (a rail flicker must not un-spend PLAY) */
					for (int k = TRK_1; k < (int)committed; k++)
						ep_time[k] = 0;  /* below = up-sweep transit */
					/* M44-r2 ARM MIGRATION: with instant empty arms, an
					 * up-sweep transit can arm a LOWER empty track for
					 * the ~24-32 ms the finger needs to land on the
					 * button it actually wants. The theft signature is
					 * this exact transition: a DIFFERENT button commits
					 * while a young (<=40 ms), still-silent instant arm
					 * waits. Cancel it — stop-on-ARMED is already the
					 * cancel and nothing was recorded; the real button
					 * then arms on its own press within a pass. Also
					 * catches PLAY presses sweeping the track bands. A
					 * fast roll between two empty tracks resolves to
					 * the LAST one — the finger's final word. */
					if (g_rec_track >= 0 &&
					    (int)committed != g_rec_track &&
					    arm_was_instant &&
					    trk[g_rec_track].state == TS_ARMED &&
					    tnow - arm_t <= 40) {
						g_stop_req = 1;
						armed_press[g_rec_track] = 0;
						arm_was_instant = 0;
					}
				}
				if (committed >= TRK_1 && committed <= TRK_4) { /* PRESS edge */
					int ti = (int)committed;
					press_t[ti] = tnow;
					armed_press[ti] = 0;
					/* M44-r2: from a HIGHER band = that button's release
					 * down-sweep; it must not instant-arm (the 48 ms
					 * floor below outlasts any transit). */
					press_from_above[ti] =
						(before > committed && before <= TRK_PLAY) ? 1u : 0u;
				}
			}
			/* BNC-597 THE BOUNCE, on the chord's second pass. Spend PLAY (its
			 * release must neither toggle the transport nor restart it); the
			 * track's release is swallowed with the sweep. A young instant arm the
			 * chord's own finger caused on the way in is cancelled losslessly --
			 * the engine processes the cancel before the arm in the same block.
			 * Refusals shrug: not playing, nothing else playing, a take busy. */
			/* HOLDSTOP-686: a HELD bounce stops when TN comes up. 'Up' = the ladder is neither
			 * in the chord bands nor on TN's own band (PLAY lifted first leaves TN alone on the
			 * ladder -- still a hold). The stop is the ordinary stop request. */
			if (g_bk_hold_trk >= 0 && trk_raw < 1840 && (int)decode_tracks(trk_raw) != (int)g_bk_hold_trk
			    && !(trk_raw >= 1509 && _volp)) {   /* HOLDSAG-776: a VOL press sags the chord to bare PLAY (W341) -- not a lift */
				int _ht = (int)g_bk_hold_trk; g_bk_hold_trk = -1;
				if (g_rec_track == _ht && (trk[_ht].state == TS_ARMED || trk[_ht].state == TS_REC)) {
					g_bk_stop_pos = g_consume_pos;   /* SMPSTART-687: the print's beginning is HERE */
					g_stop_req = 1; tap_deadline[_ht] = 0; stop_tap_trk = _ht;
				}
			}
			if (bch_fire >= 0) {
				int ti = bch_fire; bch_fire = -1;
				int64_t tnow = k_uptime_get();
				ep_play_spent = 1;
				if (g_rec_track == ti &&
				    (trk[ti].state == TS_ARMED || trk[ti].state == TS_REC)) {
					/* BNC2-599: PLAY + the track that is taking = STOP it,
					 * exactly what the bare tap does (marc). */
					g_stop_req = 1;
					tap_deadline[ti] = 0;
				} else {
				int freed = -1;
				if (g_rec_track >= 0 && trk[g_rec_track].state == TS_ARMED &&
				    arm_was_instant && tnow - arm_t <= 200) {
					g_stop_req = 1;
					armed_press[g_rec_track] = 0;
					arm_was_instant = 0;
					freed = g_rec_track;
				}
				int busy = 0, src = 0;
				for (int k = 0; k < NTRK; k++) {
					uint8_t st = trk[k].state;
					if (st == TS_REC || st == TS_DONE) busy = 1;
					if (st == TS_ARMED && !(freed >= 0 && k == freed)) busy = 1;
					if (k != ti && st == TS_PLAY) src = 1;
				}
				if (g_playing && g_loop_active && g_loop_len != 0u &&
				    (g_rec_track < 0 || freed >= 0) && !busy && src) {
					/* NOT armed_press[ti]: the episode-end sweep guard cancels any
					 * armed track that is not the episode's dominant button, and
					 * PLAY is. The track needs no spending -- the whole release
					 * sweep is swallowed above, so it can never commit. */
					tap_deadline[ti] = 0;
					press_from_above[ti] = 0;
					g_bnc_arm = (int8_t)ti;
					g_bk_mode = 0u; g_bnc_arm_t = tnow; g_bk_trk_ctl = (int8_t)ti;   /* TAPECOPY-684 / HOLDSTOP-686 */
					g_take_auto_at = 0u;          /* STACKT-716: a bounce never takes the preset */
					__DSB();                      /* the flag lands before the request */
					g_arm_req[ti] = 1;
				} else {
					g_led_shrug = 20;             /* the four-LED 'no' */
				}
				}
			}
			if (ep_open && committed == TRK_NONE &&
			    trk_raw >= 0 && trk_raw < 110) {
				/* EPISODE END (ladder settled at idle): attribute the
				 * release to the button that was committed the longest. */
				ep_open = 0;
				int64_t tnow = k_uptime_get();
				int b = -1; int64_t bt = 0;
				for (int k = TRK_1; k <= TRK_PLAY; k++) {
					if (ep_time[k] > bt) { bt = ep_time[k]; b = k; }
				}
				/* Order matters: first decide the episode is REAL (its
				 * dominant out-lasts any possible transit blip), THEN decide
				 * which button owns it. */
				if (bt < 40) {
					b = -1;          /* pure transit blip: fire nothing */
				} else {
					/* ROLL-OFF ATTRIBUTION: a release sweep only ever dwells
					 * on bands BELOW the button that was really pressed (the
					 * ladder cannot overshoot above it, and up-sweep transit
					 * is wiped at the press edge). So when a lower band
					 * out-dwelt the HIGHEST committed button, prefer the
					 * highest — provided it was committed >=24 ms (a real
					 * contact, longer than debounce noise) and at least half
					 * the dominant's time. This keeps a quick stop-tap on the
					 * recording track from becoming a phantom mute with the
					 * take left running, and equally protects the taps right
					 * AFTER a take finalizes and lazy PLAY releases — the old
					 * rule only guarded the recording track, so the "did it
					 * stop?" and delete taps had no protection at all. */
					int H = -1;
					for (int k = TRK_PLAY; k >= TRK_1; k--)
						if (ep_time[k] >= 24) { H = k; break; }
					if (H > b && ep_time[H] * 2 >= bt) {
						b = H; bt = ep_time[H];
					}
				}
				for (int k = TRK_1; k <= TRK_PLAY; k++) ep_time[k] = 0;
				/* PHANTOM-ARM SWEEP GUARD: the empty-track 40 ms instant
				 * arm can be tripped by a slow roll toward a HIGHER button
				 * dwelling on an empty track in transit. The episode's
				 * dominant button tells the truth at release: any track
				 * that armed during this episode but is NOT the dominant
				 * was a transit artifact — cancel it (an ARMED take
				 * cancels losslessly; one that already caught sound
				 * finalizes tiny and double-tap deletes). */
				for (int x = 0; x < NTRK; x++) {
					if (!armed_press[x] || x == b) continue;
					armed_press[x] = 0;
					if (g_rec_track == x) {
						g_stop_req = 1;
						tap_deadline[x] = 0;
					}
				}
				if (b >= TRK_1 && b <= TRK_4) {
					int ti = b;
					if (g_pg_open && g_pg_id == 2u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* FX2-558: a track tap on page 5 resets that effect
						 * to neutral -- the same blind kill switch page 2 has. */
						if (ti == 0) g_bcr_amt = 0u;
						if (ti == 1) g_rng_amt = 0u;
						if (ti == 2) g_awh_amt = 0u;
						/* ECHO-572: T4 cycles the delay division, exactly as the
						 * trance gate's T4 cycles its pattern. One page, one rule. */
						if (ti == 3) { g_ec_div = (uint8_t)((g_ec_div + 1u) % 4u); g_lane_per[1] = 0u; led_flash_lane(ti, (g_ec_div < 3u) ? (uint32_t)g_ec_div + 1u : 0u); }   /* A4 (660): 1/16 -> dotted -> 1/8 -> OFF; 661/664: count the division, not OFF */
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 1u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* FXP-547: on the FX PAGE a track tap RESETS that
						 * effect to neutral -- a kill switch you can hit
						 * blind mid-performance. Only the filter exists
						 * this rung; the others land with their kernels. */
						if (ti == 0) g_flt_pos = 128u;   /* bypass -- the fast kill (marc 09-05) */
						if (ti == 1) g_chr_mix = 0u;     /* FX2-550: dry   */
						if (ti == 2) { g_dst_typ = (uint8_t)((g_dst_typ + 1u) % 4u); led_flash_lane(ti, (g_dst_typ < 3u) ? (uint32_t)g_dst_typ + 1u : 0u); }   /* A5 (660): soft -> hard -> fold -> OFF; 661/664: count the type, not OFF */
						if (ti == 3) {                   /* TG-551: next pattern */
							g_gat_pat = (uint8_t)((g_gat_pat + 1u) % 3u);
							led_flash_lane(ti, (g_gat_pat < 2u) ? (uint32_t)g_gat_pat + 1u : 0u);   /* 661/664: count the pattern; OFF just goes dark */
							g_gat_g = 4096;   /* Q12 unity. The old 256 here was a
							                   * Q8 constant in a Q12 gain and cut
							                   * the output by 24 dB on every tap. */
						}
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 3u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* A5: page 3 tap = the lane's DIVISION (beat -> half -> quarter) and
						 * it clears a tapped rate; the fader to the bottom is the kill. */
						if (ti < 3) { g_lfo_div[ti] = (uint8_t)((g_lfo_div[ti] + 1u) % 4u); g_lane_per[2 + ti] = 0u; led_flash_lane(ti, (g_lfo_div[ti] < 3u) ? (uint32_t)g_lfo_div[ti] + 1u : 0u); }   /* 660: beat -> half -> quarter -> OFF; 661/664: count the division, not OFF */
						if (ti == 3) g_rv_mix = 0u;   /* REVERB-676: the kill */
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 4u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* TAPE-569: the kill switch upstream's users did not have.
						 * Their open 'can't disable drive on certain loops' report
						 * is the PICKUP LAW: a fader must cross the stored value
						 * before it takes over, and nothing shows where that value
						 * sits. A tap that zeroes it outright is the answer. */
						if (ti == 0) g_tp_drive = 0u;
						if (ti == 1) g_tp_tone  = 128u;
						if (ti == 2) g_tp_hiss  = 128u;   /* HISS2-701: centre = off */
						if (ti == 3) { g_tp_wob = 0u; g_lane_per[6] = 0u; }   /* WOBTAP-675: the kill also frees the wow (page 3's rule) */
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 5u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* EQ-691: a tap on page 5 = that band FLAT (the kill), the
						 * pickup re-armed like every other page. */
						g_eq_g[ti] = 128u; g_eq_live = 1u;
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 6u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* PLACE-715: a tap on page 6 = CENTRE (the kill), the
						 * pickup re-armed like every other page. */
						place_set(ti, 128u);
						g_fx_pick[ti] = 1; g_fx_lastq[ti] = -1;
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 7u &&
					    g_rec_track < 0 && !armed_press[ti]) {
						/* PAGE7V-718: every tap is a RESET (the other pages' rule; the
						 * mode tap is gone -- FN+PLAY is the one gesture). A nudged
						 * track's tap centres it; a centred T1 zeroes the offset, a
						 * centred T2 switches the preset off; T3/T4 centred = nothing. */
						if (g_slot < NUM_SLOTS) {
							const uint8_t _nq = g_trk_nudge[g_slot][ti];
							if (_nq && _nq != 128u) {
								nudge_set(ti, 128u);
							} else if (ti == 0 && g_grid_off_q8[g_slot]) {
								g_grid_off_q8[g_slot] = 0; g_grid_dirty_ms = k_uptime_get_32() | 1u;
								g_fx_pick[0] = 1; g_fx_lastq[0] = -1;
							} else if (ti == 1 && g_take_preset[g_slot]) {
								g_take_preset[g_slot] = 0u; g_grid_dirty_ms = k_uptime_get_32() | 1u;
								g_fx_pick[1] = 1; g_fx_lastq[1] = -1;
							}
						}
						tap_deadline[ti] = 0;
					} else if (g_pg_open && g_pg_id == 8u && g_rec_track < 0 &&
					    !armed_press[ti]) {
						/* PF-545: the OPEN PAGE owns track taps (sticky
						 * pages, W156). MODE page: flip the NEXT-record
						 * mode, stamp + persist (the FX2/E3 discipline).
						 * No mute, no delete window; record stays one
						 * gesture away (a HOLD arms straight through). */
						trk[ti].p16m_next = !trk[ti].p16m_next;
						if (g_slot < NUM_SLOTS)
							g_x3.t[g_slot][ti].rsv = (uint8_t)(0x80u |
							    (trk[ti].p16m_next & 1u));
						g_meta_save_req = 1;
						tap_deadline[ti] = 0;
					} else if (ti == stop_tap_trk) {
						stop_tap_trk = -1;   /* R1: stop fired at press;
						                      * this release is spent */
					} else if (armed_press[ti]) {
						armed_press[ti] = 0;
						/* LATCHED RECORDING: releasing the arming hold
						 * does NOT stop the take — it records hands-free
						 * until the same track is tapped again or the
						 * region fills. (See the HOLD-ARM comment for why
						 * the momentary variant was rolled back.) */
					} else if ((g_rec_track == ti &&
						    (trk[ti].state == TS_ARMED ||
						     trk[ti].state == TS_REC)) ||
						   trk[ti].state == TS_DONE) {
						/* tap on the recording track = STOP
						 * (on ARMED-but-silent = cancel; on a
						 * just-auto-finalized TS_DONE take the
						 * tap is swallowed — never a mute or
						 * delete window on a fresh take). */
						g_stop_req = 1;
						tap_deadline[ti] = 0;
					} else if (tap_deadline[ti] > 0 && tnow <= tap_deadline[ti]) {
						/* (heads mode: taps only mute — never
						 * delete or arm; the mode is playback.
						 * REV2-641: the heads double-tap REVERSE is
						 * gone -- reverse is FN+PLAY+track in any
						 * mode; a 2nd tap here is another mute) */
						/* PG-533: the base-layer toggle is REMOVED -- marc's
						 * rule: no gesture passes THROUGH mute. The toggle
						 * lives on the FN+hold-T4 MODE PAGE. A quick 2nd
						 * tap is just another mute; the DELETE dwell (kept,
						 * marc-approved) still owns the held 2nd tap. */
						tap_deadline[ti] = tnow + DTAP_GAP_MS;   /* DELFIX-762: every tap re-opens the window (was 0: a tap inside a window closed it, so tap-tap-HOLD failed) */
						trk[ti].muted = !trk[ti].muted;
					} else {
						/* tap -> mute, INSTANT on gridded and
						 * ungridded songs alike (v2.0.0: the M8c
						 * bar-wait was removed after live testing —
						 * see the bar-service note). */
						trk[ti].muted = !trk[ti].muted;
						if (!g_heads_mode && g_slot < NUM_SLOTS) {
							/* M7-r4: remember per song — but NOT
							 * in heads mode (M19a-r4): head mutes
							 * are session performance state */
							uint8_t mb = (uint8_t)(0x10u << ti);
							if (trk[ti].muted) g_meta.song_mode[g_slot] |= mb;
							else               g_meta.song_mode[g_slot] &= (uint8_t)~mb;
							g_meta_save_req = 1;
						}
						tap_deadline[ti] = tnow + DTAP_GAP_MS;
					}
				} else if (b == TRK_PLAY) {
					/* PLAY tap -> toggle play/stop. ep_play_held was set
					 * the instant the hold-restart fired (a hold is not a
					 * tap). Ignored while a take is in progress: stopping
					 * would freeze the recording mid-take. */
					g_mt_rel++; g_mt_last = (uint8_t)((ep_play_spent ? 1u : 0u) | (g_vol_pair ? 2u : 0u) | (ep_play_held ? 4u : 0u));   /* MTDIAG-742 */
					if (ep_play_spent || g_vol_pair || g_br_on) {   /* MUTEFIX3-741 / BRSPENT-753: a PLAY released with the pair held or a repeat live is the chord's release, never a tap and never the restart */
						g_mt_sp++;   /* BNC-597: PLAY was the bounce modifier -- spent */
					} else if (ep_play_held) {
						/* BNC-570 B1a: the hold DISPATCHES here now. */
						g_restart_req = 1; g_mt_rst++;
					} else if (g_rec_track < 0) {
						g_playing = !g_playing; g_mt_tap++;
						if (g_playing) g_midi_start_pending = 1;
						else           g_midi_stop_pending  = 1;
					}
				}
				ep_play_held = 0; ep_play_spent = 0;
			}
			/* R1 STOP-ON-PRESS (perfect-loop): on the RECORDING track a
			 * press can only mean STOP — no mute/delete/arm ambiguity —
			 * so fire it once the commit has SUSTAINED ~48 ms (transit
			 * grazes commit for at most ~32 ms per the episode notes)
			 * instead of waiting for the release: the tap's physical
			 * duration (50-150 ms, different every time) no longer
			 * stretches the loop. CRITICAL: armed_press excludes the
			 * press that ARMED this take — releasing the arming hold
			 * stays latched (it must never read as a stop; without this
			 * the arm cancelled itself ~50 ms after arming). The
			 * episode-end handler above swallows this press's release;
			 * R2 backdates the remaining constant. */
			if (committed >= TRK_1 && committed <= TRK_4) {
				int ti = (int)committed;
				if (ti != stop_tap_trk && !armed_press[ti] &&
				    ((g_rec_track == ti &&
				      (trk[ti].state == TS_ARMED ||
				       trk[ti].state == TS_REC)) ||
				     trk[ti].state == TS_DONE) &&
				    k_uptime_get() - press_t[ti] >= 48) {
					g_stop_req = 1;
					tap_deadline[ti] = 0;
					stop_tap_trk = ti;
				}
			}
			/* HOLD-ARM, always LATCHED on release. EMPTY tracks arm after
			 * just 100 ms: a tap has no meaning there (nothing to mute or
			 * delete), so there is nothing to disambiguate — and 100 ms is
			 * above the realistic transit-graze range (blips commit
			 * 24-32 ms; only a deliberately lazy roll dwells ~100 ms+, and
			 * the episode-end sweep guard cancels those losslessly).
			 * Unlike the rolled-back 40 ms instant-arm there is NO
			 * provisional RAM-only phase here — flushing starts
			 * immediately, so the write pattern is identical to the
			 * release (the provisional's clumped catch-up burst was what
			 * starved playback at high tape speed). Content tracks keep
			 * the full HOLD_RECORD_MS so tap-mute stays instant. The
			 * hold-duration MOMENTARY variant stays rolled back: with a
			 * slow arm its latch window collapsed to a sliver and broke
			 * hands-free recording. */
			if (committed >= TRK_1 && committed <= TRK_4) {
				int ti = (int)committed;
				int empt = (trk[ti].state == TS_EMPTY &&
					    !(g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[ti]));
				if (g_heads_mode && !armed_press[ti] &&
				    ti != stop_tap_trk && heads_engaged() &&
				    trk[ti].state == TS_PLAY && ti != (int)g_head_src &&
				    k_uptime_get() - press_t[ti] >= 400) {
					/* M19a: in heads mode, HOLD a LOADED track =
					 * make IT the tape ("hold the loop you want
					 * to head"). All other rings re-anchor behind
					 * per-track blips; positions and directions
					 * are KEPT — they are the performance. The
					 * press is spent via armed_press (its release
					 * is swallowed by the latched-arm branch), so
					 * it can never read as a mute. Empty-track
					 * holds are reserved for the M19b bounce. */
					g_head_src = (uint8_t)ti;
					for (int hk = 0; hk < NTRK; hk++) {
						if (hk == ti) continue;
						g_head_blip[hk] = 3;
						trk[hk].p_w = (g_consume_pos /
							TSPB_SRC(hk)) * TSPB_SRC(hk);   /* HG-646 */
					}
					armed_press[ti] = 1;   /* spend the press */
					tap_deadline[ti] = 0;
				}
				/* GS-531 (map v2 row 99): DOUBLE-TAP-AND-HOLD = DELETE. The press
			 * began inside the window (stable test: the deadline is only
			 * consumed by the quick-toggle or by this delete, so it cannot
			 * lapse mid-hold) and has dwelt DTAP_DEL_HOLD_MS. */
			if (!armed_press[ti] && ti != stop_tap_trk && !g_heads_mode &&
			    tap_deadline[ti] > 0 && press_t[ti] <= tap_deadline[ti] &&
			    k_uptime_get() - press_t[ti] >= DTAP_DEL_HOLD_MS) {
				tap_deadline[ti] = 0;
				g_del_req[ti] = 1;
				g_head_rev[ti] = 0;   /* REV2-641: an empty track has no direction */
				trk[ti].muted = 0;
				armed_press[ti] = 1;   /* spend the press: its release
				                        * must not read as a mute */
			}
			if (!armed_press[ti] && ti != stop_tap_trk &&
				    g_rec_track < 0 && !g_heads_mode && !g_pg_open &&
				    !(tap_deadline[ti] > 0 &&
				      press_t[ti] <= tap_deadline[ti]) &&   /* GS-531: inside the
				      * double-tap window a hold means DELETE (below), never ARM */
				    trk[ti].state != TS_DONE &&
				    k_uptime_get() - press_t[ti] >=
				        (empt ? (((g_grid_active && g_grid_fresh) ||
				                  !press_from_above[ti])
				                 ? 0 : EMPTY_ARM_MS)   /* M44-r2: instant
				                  * from idle/lower; 48 ms only for the
				                  * release down-sweep case */
				              : HOLD_RECORD_MS)) {
					/* A-r2: on a FRESH-TAPPED grid an empty track
					 * arms at the press COMMIT (~25-30 ms) — the
					 * 100 ms filter made pressing ON the beat the
					 * worst possible phase (the line passed during
					 * the filter and the punch waited a whole
					 * beat; marc's "small delay"). Press ~40 ms
					 * before the line and the punch catches it
					 * sample-exact; a committed graze merely arms
					 * a visible fast-blink, cancellable with a
					 * tap. */
					/* v2.0.0: the gridded re-record hold is HOLD_RECORD_MS
					 * again. M8b-r5 trimmed it to 120 ms ("the punch waits
					 * for the bar anyway") but real taps measure 50-150 ms
					 * (the R1 notes), so the top of the tap band was ARMING
					 * RE-RECORDS on gridded songs — eating the 2nd tap of
					 * double-tap delete and zeroing its window (user found
					 * it as "delete works worse on gridded songs"). The
					 * 60 ms the trim saved was invisible anyway: an overdub
					 * punch waits for the bar regardless. Empty tracks keep
					 * the 100 ms instant arm (a tap means nothing there). */
					/* g_rec_track < 0: one take at a time — while a latched
					 * take runs, holding ANY track does nothing (no phantom
					 * arm, no forced g_playing). state != TS_DONE: a hold on
					 * a just-auto-finalized take (user trying to stop it)
					 * must not silently arm a latched re-record that would
					 * overwrite the take it is still flushing.
					 * ti != stop_tap_trk (M8b-r4): the press that STOPPED a
					 * take is SPENT — R1 stops fire at press-down, so the
					 * finger is still on the button while the take flushes;
					 * once it lands back in TS_PLAY the TS_DONE guard no
					 * longer covers it, and a deliberate 200-400 ms stop
					 * press re-armed a re-record that overwrote the loop
					 * just made (user report; the grid punch then started
					 * it recording all by itself). Arming requires a FRESH
					 * press — the latch clears at episode end. */
					armed_press[ti] = 1;
					arm_t = k_uptime_get();          /* M44-r2 */
					arm_was_instant = empt ? 1u : 0u;
					tap_deadline[ti] = 0;            /* a hold cancels a pending single-tap */
					{	/* A-r2: remember when the FINGER landed,
						 * in engine samples (approximate the
						 * elapsed ms back from now). M44: plus the
						 * constant pipeline latency press_t itself
						 * cannot see (debounce + pass), so the
						 * stamp lands on the touchdown. */
						int64_t _ago = k_uptime_get() - press_t[ti]
						             + PRESS_COMP_MS;
						if (_ago < 0) _ago = 0;
						uint64_t _sc = g_sample_clock;
						uint64_t _back = (uint64_t)_ago * 48u;
						g_arm_press_sclk =
							(_sc > _back) ? (_sc - _back) : _sc;
					}
					g_take_auto_at = take_preset_samps();   /* STACKT-716: the preset for THIS take (0 = none) */
					g_arm_req[ti] = 1;
					g_playing = 1;                   /* recording implies play */
				}
			}
			g_dbg_btn = (int)committed;                      /* diag: settled button */

			/* PLAY/STOP button: a short TAP toggles play/stop in place (tape ramp);
			 * a HOLD (>=400 ms) jumps to the START of the song and plays — a reliable
			 * "play the whole thing from the top" that never depends on current state. */
			if (committed == TRK_PLAY) {
				if (play_t < 0) { play_t = k_uptime_get(); play_held = 0; }
				else if (!play_held && (k_uptime_get() - play_t) >= 400) {
					/* BNC-570 B1a: the 400 ms edge is now a pure MARK. The
					 * restart itself moved to the RELEASE (see the TRK_PLAY
					 * release branch), so a PLAY hold is a modifier you can
					 * enter and back out of instead of an action that has
					 * already happened by the time you change your mind. */
					play_held = 1;
					/* mark the episode a hold NOW — a clean PLAY->idle
					 * release dispatches the episode end before this block
					 * runs again; marking it at release was too late (the
					 * "tap" toggle fired right after the restart and the
					 * stale flag then swallowed the next genuine tap). */
					ep_play_held = 1;
				}
			} else {
				play_t = -1;
			}

#if HP_TIM_TEST
			/* HEADPHONE AUTO-MUTE: poll the codec jack-detect ~5x/s and mute the
			 * speaker while headphones are in. Debounced (3 consecutive equal
			 * reads) so a single noisy read can't flip it; failed reads hold. */
			if (g_hp_on == 1) {
				static int hp_poll, hp_cand = -1, hp_cnt;
				if (++hp_poll >= 5) {            /* ~40 ms */
					hp_poll = 0;
					int c = hp_detect_connected();
					if (c >= 0) {
						if (c == hp_cand) {
							if (++hp_cnt >= 3 && c != g_hp_in) {
								g_hp_in = c;
								tas_set_speaker(!c);
							}
						} else { hp_cand = c; hp_cnt = 1; }
					}
				}
			}
#endif

			/* faders -> per-track volume (Q8); ~0..3700 maps to 0..256 (unity).
			 * ROUND-ROBIN one fader per pass (each still updates every ~32 ms —
			 * imperceptible for a volume slider) to keep the main loop's blocking
			 * ADC time low; see the ladder_read comment for why that matters. */
			{
				static int fi;
				int fv = ladder_read(&adc_ladder[LAD_FADER0 + fi]);
				if (fv >= 0) {        /* ADC error -> hold the last volume */
					uint32_t q = (uint32_t)fv * 256u / 3700u;
					if (q > 256u) q = 256u;
					/* M14: a fader that was scrubbing a head keeps its
					 * OLD volume until it rejoins it (±6%) or crosses
					 * it — no volume jump on FUNCTION release. */
					if (g_fh_latch[fi]) {
						int d = (int)q - (int)trk[fi].vol_q8;
						int p = g_fh_lastq[fi];
						g_fh_lastq[fi] = (int)q;
						if ((d >= -15 && d <= 15) ||
						    (p >= 0 &&
						     ((p - (int)trk[fi].vol_q8 > 0) != (d > 0))))
							g_fh_latch[fi] = 0;
					}
					/* SEC-695: THE SECONDARY LAYER. On a page, the fader whose TRACK
					 * BUTTON is held moves that lane's second parameter, RELATIVE to
					 * the fader's position at the hold's start; the first move
					 * (>= 3 counts) spends the press so the release is not the
					 * kill; on release the primary's pickup is re-armed (W155). */
					static int     sec_q0[4], sec_base[4];
					static uint8_t sec_on[4], sec_moved[4];
					int _sec = 0;
					const int _held = (committed >= TRK_1 && committed <= TRK_4) ? (int)committed : -1;
					const int _slot = (_held < 0) ? 0 : (fi == _held) ? 1 : (fi == ((_held + 1) & 3)) ? 2 : 0;   /* 1 = own fader, 2 = the fader to the right (SHAPE-696) */
					if (g_pg_open && ((g_pg_id >= 1u && g_pg_id <= 4u && _slot) ||
					    (g_pg_id == 7u && _slot == 1 && g_slot < NUM_SLOTS))) {   /* NUDGE-717: page 7, own fader = the nudge */
						volatile uint8_t *_tab = (g_pg_id == 7u) ? &g_trk_nudge[g_slot][_held]
						                       : (_slot == 1) ? &g_sec[g_pg_id - 1u][_held] : &g_sec2[g_pg_id - 1u][_held];
						if (g_pg_id == 7u && *_tab == 0u) *_tab = 128u;   /* unset reads as centre */
						if (!sec_on[fi]) { sec_on[fi] = 1; sec_q0[fi] = (int)q; sec_moved[fi] = 0; sec_base[fi] = (int)*_tab; }
						int _d = (int)q - sec_q0[fi];
						if (!sec_moved[fi] && (_d >= 3 || _d <= -3)) {
							sec_moved[fi] = 1;
							armed_press[_held] = 1;   /* spend the press: its release is not the kill */
							tap_deadline[_held] = 0;
							g_sec_led2 = (uint8_t)(_slot == 2);   /* the LED follows the fader that moved last */
						}
						if (sec_moved[fi]) {
							int _v = sec_base[fi] + _d;
							if (_v < 0) _v = 0; else if (_v > 255) _v = 255;
							if (g_pg_id == 7u) {
								/* NUDGE-717: 1..255 (0 = unset), dead band = centre; a taking
								 * track is refused; on a change the M14 dip re-anchors the ring. */
								if (_v < 1) _v = 1;
								if (_v >= 120 && _v <= 136) _v = 128;
								const uint8_t _st = trk[_held].state;
								if (_st != TS_ARMED && _st != TS_REC && _st != TS_DONE && *_tab != (uint8_t)_v) {
									*_tab = (uint8_t)_v;
									g_grid_dirty_ms = k_uptime_get_32() | 1u;
									if (_st == TS_PLAY || head_active(_held)) {
										g_head_blip[_held] = 3;
										trk[_held].p_w = (g_consume_pos / TSPB_SRC(_held)) * TSPB_SRC(_held);
									}
								}
							} else
								*_tab = (uint8_t)_v;
						}
						g_sec_led = (uint8_t)(_held + 1);
						_sec = 1;
					} else if (sec_on[fi]) {   /* the hold ended, or the page went */
						sec_on[fi] = 0;
						if (sec_moved[fi]) { sec_moved[fi] = 0; g_fx_pick[fi] = 1; g_fx_lastq[fi] = -1; }
						if (!sec_on[0] && !sec_on[1] && !sec_on[2] && !sec_on[3]) { g_sec_led = 0; g_sec_led2 = 0; }
					}
					if (_sec) {
						/* the secondary owns this fader this pass */
					} else if (g_pg_open && g_pg_id == 2u) {
						/* FX2-558: page 5 owns the faders, same pickup law. */
						uint8_t _pv = (fi == 0) ? g_bcr_amt
						            : (fi == 1) ? g_rng_amt
						            : (fi == 2) ? g_awh_amt : g_ec_mix;
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;
						}
						if (!g_fx_pick[fi]) {
							if      (fi == 0) g_bcr_amt = (uint8_t)_q8;
							else if (fi == 1) g_rng_amt = (uint8_t)_q8;
							else if (fi == 2) g_awh_amt = (uint8_t)_q8;
							else              g_ec_mix = (uint8_t)_q8;   /* ECHO-572 */
							if (fi == 3 && _q8 > 0 && g_ec_div >= 3u) { g_ec_div = 0u; led_flash_lane(fi, 1u); }   /* OFFWAKE-697: the fader wakes an OFF lane */
						}
					} else if (g_pg_open && g_pg_id == 1u) {
						/* FXP-547: THE FX PAGE OWNS THE FADERS while it is
						 * open (track volumes are untouched and resume
						 * exactly where they were). PICKUP (framework law,
						 * W155): a fader does nothing until it CROSSES the
						 * parameter's stored value, so opening a page can
						 * never jump a parameter to the fader's position. */
						uint8_t _pv = (fi == 0) ? g_flt_pos
						            : (fi == 1) ? g_chr_mix
						            : (fi == 2) ? g_dst_amt
						            : g_gat_amt;   /* FX2-550: all four live */
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;   /* crossed: live now */
						}
						if (!g_fx_pick[fi] && fi == 1)
							g_chr_mix = (uint8_t)_q8;   /* FX2-550:
							 * fader 2 = chorus depth; bottom = dry */
						if (!g_fx_pick[fi] && fi == 3) {
							g_gat_amt = (uint8_t)_q8;   /* FX2-550:
							 * fader 4 = gate threshold; bottom = open */
							if (_q8 > 0 && g_gat_pat >= 2u) { g_gat_pat = 0u; g_gat_g = 4096; led_flash_lane(fi, 1u); }   /* OFFWAKE-697 */
						}
						if (!g_fx_pick[fi] && fi == 2) {
							g_dst_amt = (uint8_t)_q8;   /* DST-548:
							 * fader 3 = drive; bottom = clean */
							if (_q8 > 0 && g_dst_typ >= 3u) { g_dst_typ = 0u; led_flash_lane(fi, 1u); }   /* OFFWAKE-697 */
						}
						if (!g_fx_pick[fi] && fi == 0)
							g_flt_pos = (uint8_t)_q8;   /* ONE filter,
							 * two handles: FN+fader-4 and this page fader
							 * drive the SAME state (the map's decision) */
					} else if (g_pg_open && g_pg_id == 7u) {
						/* STACKT-716: page 7 owns the faders -- F1 = the downbeat
						 * offset (bipolar, +-1/2 beat), F2 = the preset take length
						 * (off / 1 / 2 / 4 / 8), F3/F4 nothing; the pickup law (W155). */
						if (fi < 2 && g_slot < NUM_SLOTS) {
							const uint8_t _cur = g_take_preset[g_slot];
							uint8_t _pv = (fi == 0) ? (uint8_t)((int)g_grid_off_q8[g_slot] + 128)
							            : (_cur == 8u) ? 230u : (_cur == 4u) ? 178u : (_cur == 2u) ? 127u : (_cur == 1u) ? 76u : 0u;
							int _q8 = (int)((q > 255u) ? 255u : q);
							if (g_fx_pick[fi]) {
								int _d = _q8 - (int)_pv;
								int _p = g_fx_lastq[fi];
								g_fx_lastq[fi] = _q8;
								if ((_d >= -6 && _d <= 6) ||
								    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
									g_fx_pick[fi] = 0;
							}
							if (!g_fx_pick[fi]) {
								if (fi == 0) {
									int _o = (_q8 >= 120 && _q8 <= 136) ? 0 : (_q8 - 128);
									if (_o != (int)g_grid_off_q8[g_slot]) { g_grid_off_q8[g_slot] = (int8_t)_o; g_grid_dirty_ms = k_uptime_get_32() | 1u; }
								} else {
									uint8_t _n = (_q8 < 51) ? 0u : (_q8 < 102) ? 1u : (_q8 < 153) ? 2u : (_q8 < 204) ? 4u : 8u;
									if (_n != _cur) {
										g_take_preset[g_slot] = _n; g_grid_dirty_ms = k_uptime_get_32() | 1u;
										led_flash_lane(1, (_n == 8u) ? 4u : (_n == 4u) ? 3u : (_n == 2u) ? 2u : (_n == 1u) ? 1u : 0u);   /* PAGE7V-718 */
									}
								}
							}
						}
					} else if (g_pg_open && g_pg_id == 6u) {
						/* PLACE-715: page 6 owns the faders -- fader N = track N's
						 * place, the same pickup law (W155). Was: fell through to
						 * the track volume (the pages 6-7 plumbing bug). */
						uint8_t _pv = place_get(fi);
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;
						}
						if (!g_fx_pick[fi]) place_set(fi, (uint8_t)(_q8 ? _q8 : 1));
					} else if (g_pg_open && g_pg_id == 5u) {
						/* EQ-691: page 5 owns the faders, the same pickup law (W155). */
						uint8_t _pv = g_eq_g[fi];
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;
						}
						if (!g_fx_pick[fi]) { g_eq_g[fi] = (uint8_t)_q8; g_eq_live = 1u; }
					} else if (g_pg_open && g_pg_id == 4u) {
						uint8_t _pv = (fi == 0) ? g_tp_drive
						            : (fi == 1) ? g_tp_tone
						            : (fi == 2) ? g_tp_hiss : g_tp_wob;
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;
						}
						if (!g_fx_pick[fi]) {
							if      (fi == 0) g_tp_drive = (uint8_t)_q8;
							else if (fi == 1) g_tp_tone  = (uint8_t)_q8;
							else if (fi == 2) g_tp_hiss  = (uint8_t)_q8;
							else              g_tp_wob   = (uint8_t)_q8;
						}
					} else if (g_pg_open && g_pg_id == 3u) {
						/* PG8-560: page 3 owns the faders, same pickup law
						 * (W155) -- a fader does nothing until it CROSSES the
						 * stored value, so opening a page can never jump a
						 * parameter to wherever the fader happens to sit. */
						uint8_t _pv = (fi == 0) ? g_phs_amt
						            : (fi == 1) ? g_swp_amt
						            : (fi == 2) ? g_trm_amt : g_rv_mix;   /* REVERB-676 */
						int _q8 = (int)((q > 255u) ? 255u : q);
						if (g_fx_pick[fi]) {
							int _d = _q8 - (int)_pv;
							int _p = g_fx_lastq[fi];
							g_fx_lastq[fi] = _q8;
							if ((_d >= -6 && _d <= 6) ||
							    (_p >= 0 && ((_p - (int)_pv > 0) != (_d > 0))))
								g_fx_pick[fi] = 0;
						}
						if (!g_fx_pick[fi]) {
							if      (fi == 0) g_phs_amt = (uint8_t)_q8;
							else if (fi == 1) g_swp_amt = (uint8_t)_q8;
							else if (fi == 2) g_trm_amt = (uint8_t)_q8;
							else              g_rv_mix  = (uint8_t)_q8;   /* REVERB-676: page 3 fader 4 */
							if (fi < 3 && _q8 > 0 && g_lfo_div[fi] >= 3u) { g_lfo_div[fi] = 0u; led_flash_lane(fi, 1u); }   /* OFFWAKE-697 */
						}
					} else if (!g_fh_latch[fi])
						trk[fi].vol_q8 = (uint16_t)q;
				}
				fi = (fi + 1) & 3;
			}
			if (g_place_dirty_ms && (k_uptime_get_32() - g_place_dirty_ms) > 1500u) {
				g_place_dirty_ms = 0u; g_meta_save_req = 1;   /* PLACE-715: persist 1.5 s after the last move */
			}
			if (g_grid_dirty_ms && (k_uptime_get_32() - g_grid_dirty_ms) > 1500u) {
				g_grid_dirty_ms = 0u; g_grid_save_req = 1;    /* STACKT-716: block 2's tail, the same way */
			}

			/* VOL ladder (master vol buttons + FWD/RWD varispeed rocker), DEBOUNCED
			 * the same sticky way as the tracks — it sits on the same noisy rail and
			 * single raw reads were causing spurious volume/tempo jumps. */
			static enum vol_btn vcommit = VOL_NONE, vcand = VOL_NONE;
			static int vcnt;
			enum vol_btn vraw = decode_vol(ladder_read(&adc_ladder[LAD_VOL]));
			enum vol_btn vbefore = vcommit;
			if (vraw == vcommit)       { vcnt = 0; }
			else if (vraw == vcand)    { if (++vcnt >= 3) { vcommit = vraw; vcnt = 0; } }
			else                       { vcand = vraw; vcnt = 1; }

			/* master volume: one perceptual (~3 dB) step per fresh press, along
			 * g_vol_table[] — gradual from full (256) down to fully muted (0).
			 * Hold to repeat for a quick sweep. */
			/* INFX-672: PLAY held + VOL-/VOL+/both, anywhere = route the WHOLE
			 * chain (INPUT / TRACKS / BOTH, all pages at once). One action per
			 * press edge; the PLAY press is spent (no toggle, no restart); the
			 * master volume is suppressed while PLAY is down. */
			int _rt_hold = 0;
			static int64_t _rt_last_t;   /* INFX-672: the previous VOL press under PLAY (double-click = BOTH) */
			static enum vol_btn _rt_pend = VOL_NONE;   /* MUTEANY-738: a single VOL press waiting out the grace window */
			static int64_t _rt_pend_t;
			static uint8_t _rt_swallow;               /* MUTEANY-738: after the pair, singles are the release -- ignored until VOL_NONE */
			static int64_t _br_rep_t;                 /* BRHOLD-756: the next auto-step of a held rocker (0 = none) */
			if (committed == TRK_PLAY || (bch_held && trk_raw >= 1509) || (suppress_play && g_br_on)) {   /* SPEEDBAKE2-769: a held bounce with PLAY lifted is not the chord -- the rocker is the tempo */   /* anywhere -- the routing is GLOBAL; BRREC-762 / BRREC2-763 / BRFN2-766: PLAY still down after a bounce chord (the swallowed sweep) or after FN lifted (suppress_play) is PLAY held -- the repeat's rocker stays alive */
				_rt_hold = 1;
				int64_t _tn = k_uptime_get();
				if (vcommit == VOL_NONE) _rt_swallow = 0;
				if (vcommit == VOL_BOTH) {
					/* MUTEANY-738: PLAY + both VOL = the monitor mute, anywhere. The pending
					 * single (the first thumb) is forgotten; the singles on the way out are
					 * swallowed. One toggle per pair (the sticky commit edges once). */
					if (!_rt_swallow) {   /* MUTEFIX2-740: by STATE, not by edge -- the pair may have landed before PLAY */
						g_mon_mute = (uint8_t)!g_mon_mute; g_mt_tog++;   /* MTDIAG-742 */
						g_mt_flash = g_mon_mute ? 1u : 2u;
						g_mt_tick  = 0u;
						ep_play_spent = 1;
					}
					_rt_pend = VOL_NONE; _rt_swallow = 1; _rt_last_t = 0;
				} else if (!_rt_swallow && vcommit != vbefore && (vcommit == VOL_DOWN || vcommit == VOL_UP)) {
					_rt_pend = vcommit; _rt_pend_t = _tn;   /* MUTEANY-738: wait for the other thumb */
					ep_play_spent = 1;
				} else if (!_rt_swallow && vcommit != vbefore && (vcommit == VOL_TEMPO_UP || vcommit == VOL_TEMPO_DOWN)) {
					br_click(vcommit == VOL_TEMPO_UP);   /* BRDIR-757: UP = shorter, DOWN = longer, as FN + rocker */   /* BEATREP-749: PLAY + rocker = the beat repeat (engage / resize) */
					ep_play_spent = 1;
					_br_rep_t = _tn + 450;   /* BRHOLD-756: hold the rocker = repeat the step at FN + rocker's cadence */
				} else if (g_br_on && _br_rep_t && vcommit == vbefore && (vcommit == VOL_TEMPO_UP || vcommit == VOL_TEMPO_DOWN) && _tn >= _br_rep_t) {
					br_click(vcommit == VOL_TEMPO_UP);   /* BRDIR-757: UP = shorter, DOWN = longer, as FN + rocker */   /* BRHOLD-756: the held rocker keeps halving / doubling, ~375 ms a step */
					_br_rep_t = _tn + 375;
				}
				if (vcommit != VOL_TEMPO_UP && vcommit != VOL_TEMPO_DOWN) _br_rep_t = 0;   /* BRHOLD-756: the rocker lifted */
			}
			if (_rt_pend != VOL_NONE && (committed != TRK_PLAY || k_uptime_get() - _rt_pend_t >= 100)) {
				/* the window closed without the pair (or PLAY lifted): the route fires as before */
				if (g_br_on) {   /* BRVOL-770: a repeat is live under PLAY -- VOL-/+ = the window one earlier / later (BRFN-765's FN layer, no FN) */
					if (g_br_len8) br_setwin((_rt_pend == VOL_UP) ? (g_br_s8 + g_br_len8) : (g_br_s8 + 256u - g_br_len8), g_br_len8);   /* BRCHOP-800: Q8 of the audible cycle */
					g_chop_req = 1; g_dip_req = 1;
					_rt_last_t = 0;
				} else {
				uint8_t nr = (_rt_last_t != 0 && _rt_pend_t - _rt_last_t <= 350) ? RT_BOTH
				           : (_rt_pend == VOL_DOWN) ? RT_IN : RT_TRK;
				_rt_last_t = (nr == RT_BOTH) ? 0 : _rt_pend_t;   /* a double-click closes the pair */
				for (int _p = 1; _p <= 4; _p++) g_pg_route[_p] = nr;   /* all four pages */
				g_rt_flash = (nr == RT_IN) ? 1u : (nr == RT_TRK) ? 2u : 3u;
				g_rt_tick  = 0u;
				}
				_rt_pend = VOL_NONE;
			}
			if (committed != TRK_PLAY) _rt_last_t = 0;   /* PLAY lifted: the next press starts fresh (MUTEFIX2-740: the swallow clears on VOL_NONE only) */
			if (g_br_on && ((committed != TRK_PLAY && ladder_read(&adc_ladder[LAD_TRACKS]) < 110) || !g_playing || g_slot_switch_req)) br_release(g_fn_held && !combo_fired);   /* LOOPHOLD-804: an UNSPENT FN held at the lift = roll; bare = the tape was held */   /* BEATREP-749 / BRBNC-758 / BRBNC2-761: the ladder IDLE (PLAY lifted, no chord in flight) or the song stopped / switched = release; the bounce chord (PLAY + track reads as one value) and a recording do NOT end it */
			if (vcommit == VOL_NONE) _rt_swallow = 0;
			g_vol_pair = _rt_swallow;   /* MUTEFIX3-741 */
			{
				static int64_t vrep_t = -1, vrep_last;
				int vdir = (_rt_hold || _rt_swallow) ? 0 : (vcommit == VOL_UP) ? 1 : (vcommit == VOL_DOWN) ? -1 : 0;   /* MUTEFIX2-740: a pair's release is not a volume press */
				int vstep = 0;
				if (vdir != 0) {
					int64_t tnow = k_uptime_get();
					if (vcommit != vbefore) { vstep = 1; vrep_t = tnow; vrep_last = tnow; }
					else if (tnow - vrep_t >= 500 && tnow - vrep_last >= 110) {
						vstep = 1; vrep_last = tnow;
					}
				} else { vrep_t = -1; }
				if (vstep) {
					g_vol_idx += vdir;
					if (g_vol_idx < 0) g_vol_idx = 0;
					if (g_vol_idx > VOL_STEPS) g_vol_idx = VOL_STEPS;
					g_master_vol_q8 = g_vol_table[g_vol_idx];
				}
			}
			/* FWD/RWD rocker -> tempo, 1 BPM PER CLICK for fine control (the old
			 * version ramped ~37 BPM/s — way too coarse). Holding repeats slowly
			 * (~12 BPM/s) after 600 ms so big jumps don't need 40 clicks. Speed is
			 * derived exactly from the integer BPM, so 80 = exactly 1.0x.
			 * DOUBLE-CLICK (a 2nd click within 350 ms, same direction) = jump a
			 * SEMITONE: snap to the next 2^(k/12) grid point (see k_semi_q16),
			 * computed from the speed BEFORE the first click so the +/-1 BPM
			 * that click already applied is absorbed, not compounded. Further
			 * quick clicks chain more semitones. Single click and hold are
			 * exactly as before. */
			{
				static int64_t tempo_t = -1, tempo_last;
				static int64_t dclick_t;        /* last fresh click (0 = none) */
				static int     dclick_dir;      /* its direction */
				static uint32_t dclick_base;    /* the speed BEFORE that click */
				/* tempo LOCKED while a take is in flight: a mid-take speed
				 * glide records the warp into the loop (tape-bend artifact) */
				int dir = ((g_rec_track >= 0 && !g_bnc_on) || _rt_hold) ? 0 :   /* BEATREP-749: the rocker under PLAY is the repeat, not the tempo; SPEEDBAKE-768: free during a bounce */
					  (vcommit == VOL_TEMPO_UP) ? 1 :
					  (vcommit == VOL_TEMPO_DOWN) ? -1 : 0;
				int step = 0;
				if (dir != 0) {
					int64_t tnow = k_uptime_get();
					if (vcommit != vbefore) {            /* fresh click */
						if (dclick_t != 0 && dir == dclick_dir &&
						    tnow - dclick_t <= 350) {
							/* DOUBLE-CLICK -> next semitone */
							uint32_t ns = semitone_next(dclick_base, dir);
							int b = (int)(((uint64_t)ns * LOOP_BPM_BASE
								       + 32768u) / 65536u);
							if (b < BPM_MIN) {
								b = BPM_MIN;
								ns = (uint32_t)b * 65536u / LOOP_BPM_BASE;
							} else if (b > BPM_MAX) {
								b = BPM_MAX;
								ns = (uint32_t)b * 65536u / LOOP_BPM_BASE;
							}
							g_play_bpm = b;
							g_play_speed_q16 = ns;
							dclick_base = ns;   /* chain steps the grid */
							dclick_t = tnow;
							tempo_t = -1;       /* a double never hold-repeats */
						} else {
							dclick_base = g_play_speed_q16;
							dclick_dir  = dir;
							dclick_t    = tnow;
							step = 1; tempo_t = tnow; tempo_last = tnow;
						}
					} else if (tempo_t >= 0 && tnow - tempo_t >= 600 &&
						   tnow - tempo_last >= 80) {  /* slow hold-repeat */
						step = 1; tempo_last = tnow;
						dclick_t = 0;   /* a hold is not a click */
					}
				} else {
					tempo_t = -1;
				}
				if (step) {
					int b = g_play_bpm + dir;
					if (b < BPM_MIN) b = BPM_MIN;
					if (b > BPM_MAX) b = BPM_MAX;
					g_play_bpm = b;
					g_play_speed_q16 = (uint32_t)b * 65536u / LOOP_BPM_BASE;
				}
			}

			led_service();         /* one owner: song row + track row + standby */
			feed_wdt();
			k_msleep(8);
		}
	}

	return 0;
}
