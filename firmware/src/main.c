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
 *    * Storage uses the nRF's SPIM3 SPI engine at 16 MHz with hardware CRC
 *      checking + retry, so the flash bus is fast and self-correcting.
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
#include <soc.h>
#include <math.h>
#include <string.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include "sp1_emmc.h"

/* FAILSAFE: turn ANY unrecoverable fault (bad pointer, stack overflow, kernel
 * panic, failed assert) into a clean reboot instead of a dead hang, so the
 * device can never get stuck in a bricked-looking state. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(esf);
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

static void track_led_on(int i);
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
static int ladder_read(const struct adc_dt_spec *spec)
{
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

	for (int i = 0; i < NUM_LADDERS; i++) {
		if (device_is_ready(adc_ladder[i].dev))
			adc_channel_setup_dt(&adc_ladder[i]);
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

K_MEM_SLAB_DEFINE(tx_slab, BLK_BYTES, 10, 4);   /* deep queue so the DMA never starves */
static const struct device *const i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));

static int  audio_cfg_rc = 1;        /* i2s_configure() result, for serial diag */
static bool tas_cfg_ok;              /* did the TAS2505 register writes all ACK?  */
static volatile int  audio_blocks;   /* count of I2S blocks actually written      */
static volatile int  audio_write_rc = 99;
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
	 * deviation: mixer volume -19 dB instead of his example's full scale. */
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
	(void)tpw(0x2301, 0x13);   /* Mixer A vol (-19 dB; Tim's ex. = 0x00)*/
	(void)tpw(0x2303, 0x13);   /* Mixer B vol                           */
	(void)tpw(0x1101, 0x96);   /* power up the codec                    */
	k_msleep(10);              /* HP amp operational after 10 ms        */
	(void)tpw(0x1121, 0x41);   /* Headset switch control                */
	(void)tpw(0x1B74, 0x03);   /* Miscellaneous detect control          */
	(void)tpw(0x1129, 0x01);   /* Headset clamp disable                 */
	(void)tpw(0x2001, 0x0D);   /* HP Control: mute all                  */
	(void)tpw(0x1F06, 0x84);   /* DAC Control 2                         */
	(void)tpw(0x2301, 0x13);   /* Mixer A vol again                     */
	(void)tpw(0x2303, 0x13);   /* Mixer B vol again                     */
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
static K_THREAD_STACK_DEFINE(audio_stack, 2048);
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
#define USB_RING_FRAMES   4096u              /* ~85 ms at 48 kHz */
/* Target ring fill (frames, ~21 ms). Used both as the prebuffer target before
 * the consumer starts draining a freshly-enabled stream, and as the feedback
 * regulator's setpoint, so the hand-off from prebuffering to draining is smooth. */
#define FB_SETPOINT       1024
RING_BUF_DECLARE(usb_audio_ring, USB_RING_FRAMES * USB_FRAME_BYTES);

static volatile bool g_usb_streaming;        /* host has enabled the UAC2 terminal */

/* Diagnostics streamed over the CDC console (controls_diag): if the ring keeps
 * underrunning (drain faster than host delivers) or overflowing (host faster),
 * the rate-matching is off and audio will glitch. If both stay ~0 but it still
 * sounds wrong, the problem is NOT the buffer (look at level/codec instead). */
static volatile uint32_t g_ring_underruns;
static volatile uint32_t g_ring_overflows;

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
#define DECIM            1u
#define LOOP_RATE        (I2S_TRUE_HZ / DECIM)             /* 48000 Hz mono — FULL RATE, no decimation */
#define SAMP_PER_BLK     (EMMC_BLOCK_SIZE / 2u)            /* 256 int16 / block */
#define LOOP_BPM_BASE    80u                               /* BPM label for 1.0x varispeed */
/* FULL-RATE LOOPS: the SPIM2 hardware eMMC path measures 1333 blk/s sustained
 * REWRITE (2026-06-12 capture) — 48 kHz mono needs 187.5 blk/s write + 750
 * blk/s read (4 tracks): ~14% / ~60% of capacity. DECIM=1 also means the
 * decimator/interpolator is bit-transparent — loops record and play exactly
 * what the engine hears. Mono remains the only compromise. */
#define BEAT_SAMPLES_I2S 35840u                            /* I2S frames / beat (140 blocks ÷256) */
#define BEAT_SAMPLES_L   (BEAT_SAMPLES_I2S / DECIM)        /* 35840 = 140 blocks (÷256) */
#define BAR_SAMPLES      (BEAT_SAMPLES_L * 4u)             /* 4 beats — for display / phrasing */
#define MAX_BEATS        800u                              /* longest loop ~9.9 min @ 80.87 BPM */
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
#define TRACK_BLOCKS     ((((MAX_LOOP_BLOCKS + 8u) + 15u) / 16u) * 16u)
#define RING_SAMPLES     16384u                            /* 341 ms read-ahead @48k: rides out the card's ~150 ms random stalls (measured min=194 of 8192 before doubling) */
#define RING_MASK        (RING_SAMPLES - 1u)

/* ---- SONG SLOTS + eMMC layout ----------------------------------------------
 * The looper owns the whole eMMC starting at block 0: block 0 holds the slot
 * metadata (this OVERWRITES the original TE "ALBUM_PR" index, deleting the songs
 * and reclaiming the space — they couldn't be played anyway), tracks follow.
 * NUM_SLOTS independent songs, each with its own saved BPM + 4 tracks. There are
 * exactly 4 songs so each maps to one of the 4 status LEDs (song N -> LED N). */
#define NUM_SLOTS        4u
#define META_BLOCK       0u
#define SLOT0_BLOCK      16u    /* 8KB-aligned (block 0 = meta, 1-15 spare) */
/* FIXED storage signature: reflashing KEEPS the saved songs (the earlier
 * wipe-on-reflash build stamp is gone — user prefers persistence; double-tap a
 * track to delete it instead). Storage only re-formats if this constant or the
 * layout ever changes. */
#define META_MAGIC       0x53503933u                       /* 'SP93' — 48 kHz full-rate layout, persistent */
static inline uint32_t trk_blk(uint32_t slot, uint32_t t)
{
	return SLOT0_BLOCK + (slot * NTRK + t) * TRACK_BLOCKS;
}
/* loop_len = this song's loop length in loop-samples (a whole number of bars,
 * 0 = empty/no loop yet). Saved so a song resumes at its own length + tempo. */
struct slot_state { uint32_t speed_q16; uint32_t loop_len; uint8_t present[NTRK]; };
struct meta_blk {
	uint32_t magic;
	uint32_t cur_slot;
	struct slot_state slot[NUM_SLOTS];
};
static struct meta_blk   g_meta;
static volatile uint32_t g_slot;
static volatile int      g_slot_switch_req;   /* main -> audio: reload tracks for the new slot */
static volatile int      g_meta_save_req;     /* -> streamer: persist g_meta to eMMC */
static volatile int      g_meta_loaded;       /* streamer -> main: g_meta read at boot */

enum trk_state { TS_EMPTY, TS_ARMED, TS_REC, TS_DONE, TS_PLAY };

struct looptrk {
	volatile uint8_t  state;
	volatile uint16_t vol_q8;            /* fader volume, 256 = unity */
	int16_t  pring[RING_SAMPLES];        /* play ring:  streamer writes, audio reads */
	volatile uint32_t p_w;               /*   streamer fill frontier (loop samples) */
	volatile uint32_t r_w;               /*   rec ring: audio produce (into g_rring) */
	volatile uint32_t r_r;               /*   rec ring: streamer consume */
	volatile uint32_t rec_count;         /* samples recorded so far (audio) */
	volatile uint32_t rec_target;        /* stop after this many samples (0 = open, first loop) */
	volatile uint8_t  rec_silence;       /* live phrase ended; pad silence to rec_target */
	volatile uint8_t  muted;             /* tap-to-mute: track silenced but kept */
	volatile uint8_t  starved;           /* ring underran; silent until half-refilled */
	uint32_t flush_blk;                  /* streamer: next loop block to write */
	uint32_t flush_mod;                  /* wrap the flush at this many blocks (overdub = loop len) */
	/* AUTO-START-ON-SOUND: a take ARMS on the button hold and the recorder only
	 * begins capturing at the first input past SOUND_THRESHOLD (or SOUND_WAIT_TICKS
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
#define RRING_SAMPLES    16384u
#define RRING_MASK       (RRING_SAMPLES - 1u)
static int16_t g_rring[RRING_SAMPLES];
static volatile uint32_t g_rec_overruns;         /* diag: rec ring overflow events */

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
static volatile uint32_t g_beat_phase;            /* phase within a beat (loop samples), for LEDs */
static volatile int      g_emmc_ready;
static volatile int      g_dbg_beat;              /* current beat number (diag) */
static volatile int      g_dbg_btn = -1;          /* committed track button (diag) */
static uint64_t          g_sample_clock;          /* free-running I2S frames (idle metronome) */
static int32_t           g_dec_acc;                /* live accumulator for record decimation */
static uint32_t          g_frames_since;           /* I2S frames since the last loop-sample tick */
static uint32_t          g_pphase;                 /* Q16 playback phase */
static volatile uint32_t g_play_speed_q16 = 65536; /* tape speed when playing (Q16, 65536=1.0x); rocker sets */
/* Tempo as an INTEGER BPM (rocker steps it 1 BPM per click for fine control).
 * Speed is derived exactly: speed = bpm * 65536 / LOOP_BPM_BASE, so 80 BPM is
 * exactly 1.0x — no detent/snap logic needed. Range 40..120 = 0.5x..1.5x. */
#define BPM_MIN 40
#define BPM_MAX 120
static volatile int g_play_bpm = 80;
/* auto-start thresholds (loop-sample domain @ LOOP_RATE) */
#define SOUND_THRESHOLD  1000              /* int16 level (~ -30 dBFS) */
#define SOUND_WAIT_TICKS (LOOP_RATE * 4u)  /* ~4 s fallback */

/* BEAT GRID for the LED pulse + MIDI clock — defaults to the nominal beat, but
 * the first-track TEMPO ESTIMATOR replaces it with the detected beat period so
 * the lights/clock track the music. It does NOT change playback speed/pitch
 * (the rocker still does tape varispeed); it's the metronome grid only. */
static volatile uint32_t g_beat_samples = BEAT_SAMPLES_L;
static volatile int      g_det_bpm;       /* diag: last detected BPM (0 = none) */

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
		if (g_tempo.last_onset && g_tempo.n < TEMPO_MAX_ONSETS) {
			uint32_t d = pos - g_tempo.last_onset;
			if (d > LOOP_RATE / 8u) g_tempo.ioi[g_tempo.n++] = d;
		}
		g_tempo.last_onset = pos;
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
	g_det_bpm = (int)(((uint64_t)LOOP_RATE * 60u + beat / 2u) / beat);
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
static inline int16_t soft_limit(int32_t x)
{
	const int32_t TH = 26000;        /* ~0.8 FS linear region */
	const int32_t HEAD = 32767 - TH; /* room above the knee   */
	int32_t s = (x < 0) ? -1 : 1;
	int32_t a = x * s;               /* |x| */
	if (a > TH) {
		int32_t over = a - TH;       /* compress: y = TH + HEAD*over/(over+HEAD) */
		a = TH + (int32_t)(((int64_t)HEAD * over) / (over + HEAD));
	}
	return (int16_t)(s * a);         /* a <= 32767 by construction */
}
static void looper_audio_block(int16_t *s)
{
	static int16_t tmp[BLK_FRAMES * 2];
	/* PREBUFFER: do not start draining a freshly-(re)enabled stream until the
	 * ring holds FB_SETPOINT frames — the feedback regulator over-delivers to
	 * fill it in ~20 ms. Without this gate the consumer races the empty ring and
	 * the first moments of every host play start dribble out as choppy fragments
	 * (this gate existed in the old direct path but was lost in the looper). */
	static bool primed;
	if (!g_usb_streaming)
		primed = false;
	else if (!primed &&
		 ring_buf_size_get(&usb_audio_ring) >= FB_SETPOINT * USB_FRAME_BYTES)
		primed = true;
	uint32_t bytes = primed ?
		ring_buf_get(&usb_audio_ring, (uint8_t *)tmp, sizeof(tmp)) : 0;
	uint32_t got = bytes / USB_FRAME_BYTES;
	if (primed && got < BLK_FRAMES) g_ring_underruns++;

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
		trk[i].state = TS_EMPTY;
		trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].rec_count = 0; trk[i].muted = 0;
		if (g_slot < NUM_SLOTS) g_meta.slot[g_slot].present[i] = 0;
		int any = 0;
		for (int k = 0; k < NTRK; k++)
			if (trk[k].state != TS_EMPTY ||
			    (g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[k]))
				any = 1;
		if (!any) {
			g_loop_len = 0; g_loop_blocks = 0; g_loop_active = 0;
			if (g_slot < NUM_SLOTS) g_meta.slot[g_slot].loop_len = 0;
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
				/* released before any sound -> cancel. If it was the first take of
				 * an empty song, stop the transport too. */
				trk[i].state = (g_slot < NUM_SLOTS && g_meta.slot[g_slot].present[i])
					       ? TS_PLAY : TS_EMPTY;
				g_rec_track = -1;
				if (g_loop_len == 0u) {
					int anyp = 0;
					for (int k = 0; k < NTRK; k++) if (trk[k].state == TS_PLAY) anyp = 1;
					if (!anyp) g_loop_active = 0;
				}
			} else if (trk[i].state == TS_REC) {
				if (g_loop_len == 0u) {
					/* first take: the loop is EXACTLY what was held — no tempo
					 * quantization (works for podcasts/speech, never "jumpy").
					 * Rounded only to the 256-sample storage block (~±19 ms,
					 * inaudible) so the eMMC streaming stays block-aligned. */
					uint32_t blocks = (trk[i].rec_count + SAMP_PER_BLK / 2u)
							  / SAMP_PER_BLK;
					if (blocks < 1u) blocks = 1u;
					else if (blocks > MAX_LOOP_BLOCKS) blocks = MAX_LOOP_BLOCKS;
					g_loop_len = blocks * SAMP_PER_BLK;
					g_loop_blocks = blocks;
					trk[i].rec_target = g_loop_len;
					tempo_finish();         /* set the detected beat grid + BPM */
					if (g_slot < NUM_SLOTS) {
						g_meta.slot[g_slot].loop_len = g_loop_len;
						g_meta_save_req = 1;
					}
				}
				/* end the live phrase; the per-tick recorder pads silence (if any)
				 * up to rec_target, then -> TS_DONE. */
				trk[i].rec_silence = 1;
			}
		}
	}

	/* PRESS -> start recording that track (if nothing else is recording) */
	for (int i = 0; i < NTRK; i++) {
		if (!g_arm_req[i]) continue;
		g_arm_req[i] = 0;
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
		if (!others) { g_loop_len = 0; g_loop_blocks = 0; g_loop_active = 0; }

		if (g_loop_len == 0u) {
			/* FIRST take: start the transport NOW so the recorder can watch the
			 * input, but DON'T capture yet — recording begins at the first sound
			 * (auto-start), at which point the playhead is reset so that sound is
			 * loop position 0. Snap the tape speed so no spin-up ramp is baked in. */
			g_cur_speed_q16 = g_play_speed_q16;
			g_loop_active = 1; g_consume_pos = 0;
			g_pphase = 0; g_frames_since = 0; g_dec_acc = 0;
		}
		/* ARM (first take AND overdub): wait for the first sound, then the tick
		 * handler begins the capture so the loop starts exactly on the audio. */
		trk[i].r_w = 0; trk[i].r_r = 0; trk[i].flush_blk = 0;
		trk[i].flush_mod = MAX_LOOP_BLOCKS;
		trk[i].rec_count = 0; trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].muted = 0;
		trk[i].wait_peak = 0; trk[i].wait_ticks = 0;
		trk[i].state = TS_ARMED;
		g_rec_track = i;
	}

	/* HOLD PLAY -> jump to the start of the song and play. Rewind the shared
	 * playhead to 0 and reset every track's read frontier so the streamer refills
	 * each loop from its first block; they all restart together, in sync. Ignored
	 * while recording (so a take isn't disrupted). */
	if (g_restart_req) {
		g_restart_req = 0;
		if (g_rec_track < 0 && g_loop_active) {
			g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0;
			for (int i = 0; i < NTRK; i++) trk[i].p_w = 0;
			g_playing = 1;
			g_midi_start_pending = 1;
		}
	}

	/* SONG SWITCH: reload the tracks for the newly-selected slot. Tracks that the
	 * slot already has recorded -> PLAY (streamer refills from that slot's eMMC
	 * region from block 0); empty tracks -> ready to record. Restart the loop. */
	if (g_slot_switch_req) {
		g_slot_switch_req = 0;
		g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0;
		g_rec_track = -1;
		/* this song's remembered loop length (0 = empty, ready for a fresh take) */
		g_loop_len    = (g_slot < NUM_SLOTS) ? g_meta.slot[g_slot].loop_len : 0;
		g_loop_blocks = g_loop_len / SAMP_PER_BLK;
		int any = 0;
		for (int i = 0; i < NTRK; i++) {
			uint8_t pres = (g_slot < NUM_SLOTS) ? g_meta.slot[g_slot].present[i] : 0;
			trk[i].state = pres ? TS_PLAY : TS_EMPTY;
			trk[i].p_w = 0;
			trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].rec_count = 0; trk[i].muted = 0;
			if (pres) any = 1;
		}
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
	uint32_t step = g_cur_speed_q16 / DECIM;                              /* Q16 per I2S frame */

	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		int32_t live = (f < got)
			? (((int32_t)tmp[2 * f] + (int32_t)tmp[2 * f + 1]) >> 1) : 0;

		/* (the first take is started immediately by the press handler above, and
		 * overdubs are started on the next beat by the wrap logic below) */

		/* mix the 4 tracks (fractional-phase interpolated) over the live monitor */
		int32_t mix = live;
		uint32_t cpos = g_consume_pos;
		uint32_t frac = g_pphase & 0xFFFFu;
		for (int i = 0; i < NTRK; i++) {
			if (trk[i].state != TS_PLAY || trk[i].muted) continue;
			/* underrun gate WITH HYSTERESIS: gating per-sample at the empty
			 * boundary chatters audibly (stuttery "jumpy" flicker). Once a
			 * ring runs dry the track stays cleanly silent until the streamer
			 * has refilled half the ring. */
			int32_t avail = (int32_t)(trk[i].p_w - cpos);
			if (trk[i].starved) {
				if (avail >= (int32_t)(RING_SAMPLES / 2u))
					trk[i].starved = 0;
				else
					continue;
			} else if (avail < 2) {
				trk[i].starved = 1;
				continue;
			}
			int16_t a = trk[i].pring[cpos & RING_MASK];
			int16_t bb = trk[i].pring[(cpos + 1) & RING_MASK];
			int16_t sv = (int16_t)((int32_t)a + (((int32_t)(bb - a) * (int32_t)frac) >> 16));
			mix += ((int32_t)sv * trk[i].vol_q8) >> 8;
		}
		int16_t out = soft_limit((mix * g_master_vol_q8) >> 8);
		s[2 * f] = out; s[2 * f + 1] = out;

		/* advance the playback phase; each integer step is one loop-sample tick */
		g_dec_acc += live; g_frames_since++;
		if (g_loop_active) {
			g_pphase += step;
			while (g_pphase >= 65536u) {
				g_pphase -= 65536u;
				int16_t lsamp = (int16_t)(g_dec_acc /
					(int32_t)(g_frames_since ? g_frames_since : 1u));
				g_dec_acc = 0; g_frames_since = 0;

				int rt_i = g_rec_track;
				if (rt_i >= 0 && trk[rt_i].state == TS_ARMED) {
					/* AUTO-START: hold armed until the input first crosses the
					 * threshold (or a timeout), then begin recording. */
					struct looptrk *rt = &trk[rt_i];
					int32_t aa = lsamp < 0 ? -lsamp : lsamp;
					if (aa > rt->wait_peak) rt->wait_peak = aa;
					if (rt->wait_peak >= SOUND_THRESHOLD ||
					    ++rt->wait_ticks >= SOUND_WAIT_TICKS) {
						if (g_loop_len == 0u) {
							/* first take: this sound is loop position 0 */
							g_consume_pos = 0; g_midi_start_pending = 1;
							tempo_reset();
							rt->flush_blk = 0; rt->flush_mod = MAX_LOOP_BLOCKS;
							rt->rec_target = 0;
						} else {
							/* overdub: anchor at the current transport block and
							 * record one base loop, wrapping (in sync). */
							uint32_t sb = (g_consume_pos / SAMP_PER_BLK) % g_loop_blocks;
							rt->flush_blk = sb;
							rt->flush_mod = g_loop_blocks;
							rt->rec_target = g_loop_len;
						}
						rt->r_w = 0; rt->r_r = 0; rt->rec_count = 0; rt->rec_silence = 0;
						rt->state = TS_REC;
					}
				}
				if (g_rec_track >= 0 && trk[g_rec_track].state == TS_REC) {
					struct looptrk *rt = &trk[g_rec_track];
					if ((rt->r_w - rt->r_r) >= RRING_SAMPLES)
						g_rec_overruns++;   /* take corrupting: flush too slow */
					g_rring[rt->r_w & RRING_MASK] = rt->rec_silence ? 0 : lsamp;
					rt->r_w++;
					rt->rec_count++;
					if (g_tempo.active) tempo_feed(lsamp, rt->rec_count);
					if (rt->rec_target == 0u) {
						/* open first take: force-stop at the maximum length */
						if (rt->rec_count >= MAX_LOOP_SAMPLES) {
							g_loop_len = MAX_LOOP_SAMPLES;
							g_loop_blocks = g_loop_len / SAMP_PER_BLK;
							rt->rec_target = g_loop_len;
							tempo_finish();
							if (g_slot < NUM_SLOTS) {
								g_meta.slot[g_slot].loop_len = g_loop_len;
								g_meta_save_req = 1;
							}
							rt->state = TS_DONE; g_rec_track = -1;
						}
					} else if (rt->rec_count >= rt->rec_target) {
						rt->state = TS_DONE; g_rec_track = -1;
					}
				}

				g_consume_pos++;
				{ uint32_t md = g_beat_samples / 24u; if (md && (g_consume_pos % md) == 0u) g_midi_clk_produced++; }
				if (g_loop_len > 0u) {
					uint32_t lp = g_consume_pos % g_loop_len;
					/* (overdubs now start on first sound, handled above —
					 * no beat-boundary arm here.) */
					uint32_t bs = g_beat_samples ? g_beat_samples : BEAT_SAMPLES_L;
					g_beat_phase = lp % bs;
					g_dbg_beat = (int)(lp / bs);
				} else {
					/* first take in progress: count up, no wrap until length is set */
					uint32_t bs = g_beat_samples ? g_beat_samples : BEAT_SAMPLES_L;
					g_beat_phase = g_consume_pos % bs;
					g_dbg_beat = (int)(g_consume_pos / bs);
				}
			}
		}
	}
	g_sample_clock += BLK_FRAMES;
	if (!g_loop_active)
		g_beat_phase = (uint32_t)((g_sample_clock % BEAT_SAMPLES_I2S) / DECIM);
}

/* ---- background eMMC streamer (the ONLY eMMC user) -------------------------
 * Preemptible priority BELOW the cooperative audio thread, so the audio thread
 * can always preempt the bit-bang busy-waits and keep the I2S DMA fed. Per
 * PLAY track: read-ahead into the play ring. Per REC/DONE track: flush the rec
 * ring to the card; on DONE, finish the tail then switch the track to PLAY. */
static K_THREAD_STACK_DEFINE(streamer_stack, 2048);
static struct k_thread streamer_tcb;

static void streamer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t blk[EMMC_BLOCK_SIZE];
	/* Flush the rec ring in MULTI-BLOCK (CMD25) bursts: the card pipelines the
	 * programming across the burst instead of fully programming each block (~30 ms
	 * single-block), so the sustained write keeps up with live recording. */
#define FLUSH_BATCH 32u   /* 16KB bursts = 2 whole 8KB pages per CMD25 */
	static uint8_t batchbuf[FLUSH_BATCH * EMMC_BLOCK_SIZE];

	(void)emmc_init();
	/* AFTER init: emmc_init() resets the clock to the slow safe value — the
	 * old code zeroed it BEFORE init, so every bit-bang phase (start-bit
	 * hunts, CRC tokens, busy polls) has been running ~4x slower than
	 * intended this whole time. Zero it here so it actually sticks. */
	g_emmc_clk_half_us = 0u;
	g_emmc_ready = emmc_is_ready() ? 1 : 0;

	/* Load the slot metadata (block 0). If absent/invalid, format fresh — this
	 * overwrites the old TE album index, deleting the original songs + reclaiming
	 * the space (they couldn't be played on this hardware anyway). */
	memset(&g_meta, 0, sizeof(g_meta));
	g_meta.magic = META_MAGIC;
	for (uint32_t s = 0; s < NUM_SLOTS; s++) g_meta.slot[s].speed_q16 = 65536u;
	if (g_emmc_ready && emmc_read_blocks(META_BLOCK, blk, 1)) {
		struct meta_blk *m = (struct meta_blk *)blk;
		if (m->magic == META_MAGIC && m->cur_slot < NUM_SLOTS) {
			memcpy(&g_meta, m, sizeof(g_meta));     /* resume saved songs */
		} else {
			memset(blk, 0, EMMC_BLOCK_SIZE);
			memcpy(blk, &g_meta, sizeof(g_meta));
			(void)emmc_write_blocks(META_BLOCK, blk, 1);   /* delete old album */
		}
	}
	g_slot = g_meta.cur_slot;
	g_meta_loaded = 1;

	while (1) {
		bool work = false;
		uint32_t cpos = g_consume_pos;
		uint32_t slot = g_slot;

		if (g_meta_save_req) {                       /* persist songs + BPMs */
			g_meta_save_req = 0;
			if (g_emmc_ready) {
				memset(blk, 0, EMMC_BLOCK_SIZE);
				memcpy(blk, &g_meta, sizeof(g_meta));
				(void)emmc_write_blocks(META_BLOCK, blk, 1);
				work = true;
			}
		}

		/* PASS 1 — WRITES FIRST. Flushing the rec ring always outranks play
		 * read-ahead: a rec-ring overflow corrupts the take permanently, while a
		 * play-ring underrun is only a brief, recoverable dropout. */
		for (int i = 0; i < NTRK; i++) {
			struct looptrk *t = &trk[i];
			uint8_t st = t->state;

			if (st == TS_REC || st == TS_DONE) {
				while ((t->r_w - t->r_r) >= SAMP_PER_BLK) {
					uint32_t fm = t->flush_mod ? t->flush_mod : MAX_LOOP_BLOCKS;
					/* batch as many contiguous blocks as are ready, up to the
					 * buffer size and the loop-wrap boundary, into one CMD25 write */
					uint32_t navail = (t->r_w - t->r_r) / SAMP_PER_BLK;
					uint32_t n = navail < FLUSH_BATCH ? navail : FLUSH_BATCH;
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
					 * the final tail after the take ends. */
					uint32_t mis = blkno % 16u;
					if (mis) {
						uint32_t to_page = 16u - mis;
						if (n > to_page) n = to_page;
					} else if (n >= 16u) {
						n &= ~15u;        /* whole pages only */
					} else if (t->state == TS_REC && n < to_wrap) {
						break;            /* let a full page accumulate */
					}
					int16_t *b16 = (int16_t *)batchbuf;
					for (uint32_t s = 0; s < n * SAMP_PER_BLK; s++)
						b16[s] = g_rring[(t->r_r + s) & RRING_MASK];
					static uint8_t wfails;
					if (!emmc_write_blocks(blkno, batchbuf, n)) {
						/* card rejected a block (bus CRC): data is
						 * still in the ring — retry next pass. After
						 * 8 consecutive failures, advance anyway (one
						 * stored glitch beats a frozen take). */
						if (++wfails < 8) {
							work = true;
							break;
						}
					}
					wfails = 0;
					t->r_r += n * SAMP_PER_BLK;
					t->flush_blk += n;
					work = true;
				}
				/* Promotion re-reads the LIVE state (not the pass-start snapshot)
				 * so an engine transition during the flush can't be overwritten.
				 * Order matters: request the meta save BEFORE publishing TS_PLAY,
				 * or stop_and_flush() (power-off/DFU) can observe "idle" between
				 * the two stores and sleep with the new recording unsaved. */
				if (t->state == TS_DONE && (t->r_w - t->r_r) < SAMP_PER_BLK) {
					/* Start playback BLOCK-ALIGNED at the live playhead. p_w must be a
					 * multiple of SAMP_PER_BLK or the streamer writes each eMMC block at
					 * a misaligned ring offset and the track plays ~16 ms out of sync. */
					if (slot < NUM_SLOTS) g_meta.slot[slot].present[i] = 1;
					g_meta_save_req = 1;             /* persist the new recording */
					t->p_w = g_consume_pos & ~(SAMP_PER_BLK - 1u);
					t->state = TS_PLAY;
					work = true;
				}
			}
		}

		/* PASS 2 — play read-ahead, only after all pending writes are flushed.
		 * Skip refills entirely while a big rec backlog exists so the recorder
		 * always wins the bus (the play rings hold ~1.2 s and can coast). */
		/* rec_backlog gate: skip playback refills ONLY when a rec ring is near
		 * overflow (7/8). On the slow bit-bang bus this fired at half-full and
		 * starved every backing track during overdubs (the playhead raced
		 * seconds past the fill frontier — measured min ~ -100k). The SPIM bus
		 * sustains ~1900 blk/s; recording (187) + 3-track playback (562) is a
		 * third of that, so playback no longer needs to yield except in a true
		 * write-stall emergency. */
		int rec_backlog = 0;
		for (int i = 0; i < NTRK; i++)
			if ((trk[i].r_w - trk[i].r_r) >= (RRING_SAMPLES - RRING_SAMPLES / 8u))
				rec_backlog = 1;

		/* SERVICE BY ROUND: ONE chunk per needy track per round, rotating the
		 * start order — the track nearest its cliff is served within one
		 * chunk-time (~10 ms) instead of waiting for siblings' full refills.
		 * Chunks are 16 blocks (4096 samples) per CMD18: one command's
		 * overhead per 85 ms of audio. */
		static int rr_start;
		rr_start = (rr_start + 1) & 3;
		bool more = !rec_backlog;
		while (more && g_slot == slot) {
			more = false;
			/* RE-READ the playhead every round: PASS 1's flushes (and a long
			 * round of reads) let g_consume_pos advance well past the snapshot
			 * taken at the top of the streamer loop — using the stale value
			 * made PASS 2 under-fill the playing tracks during overdubs, the
			 * residual negative-margin dips at take boundaries. */
			cpos = g_consume_pos;
			for (int k = 0; k < NTRK; k++) {
				int i = (rr_start + k) & 3;
				struct looptrk *t = &trk[i];
				if (t->state != TS_PLAY) continue;
				if ((int32_t)(t->p_w - cpos) >
				    (int32_t)(RING_SAMPLES - 16u * SAMP_PER_BLK))
					continue;          /* this ring is fed */
				uint32_t gb = g_loop_blocks ? g_loop_blocks : 1u;
				/* Snapshot the frontier: the (higher-priority) audio thread
				 * can reset p_w mid-eMMC-read on a song switch / restart.
				 * Fill from the snapshot, COMMIT only if unchanged. */
				uint32_t pw = t->p_w;
				uint32_t loop_blk = (pw / SAMP_PER_BLK) % gb;
				uint32_t n = 16u;
				if (loop_blk + n > gb) n = gb - loop_blk;  /* stop at the wrap */
				uint32_t blkno = trk_blk(slot, (uint32_t)i) + loop_blk;
				if (!emmc_read_blocks(blkno, batchbuf, n)) {
					work = true;       /* read failed (CRC): retry next round */
					continue;
				}
				if (t->p_w != pw) { work = true; continue; } /* reset raced us */
				int16_t *b16 = (int16_t *)batchbuf;
				for (uint32_t m = 0; m < n * SAMP_PER_BLK; m++)
					t->pring[(pw + m) & RING_MASK] = b16[m];
				t->p_w = pw + n * SAMP_PER_BLK;
				work = true;
				more = true;           /* keep rounding until all rings fed */
			}
		}
		if (!work) k_msleep(2);
	}
}

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

static K_THREAD_STACK_DEFINE(midi_stack, 768);
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

static void midi_send(uint8_t b)
{
	unsigned int key = irq_lock();           /* keep the 10 bits evenly spaced */
	midi_line(0); k_busy_wait(MIDI_BIT_US);  /* start bit (space) */
	for (int i = 0; i < 8; i++) {            /* 8 data bits, LSB first */
		midi_line((b >> i) & 1);
		k_busy_wait(MIDI_BIT_US);
	}
	midi_line(1); k_busy_wait(MIDI_BIT_US);  /* stop bit (mark) */
	irq_unlock(key);
}

static void midi_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t consumed = 0, po_clk = 0;
	int64_t  po_off = -1;
	while (1) {
		if (g_midi_start_pending) { g_midi_start_pending = 0; midi_send(0xFA); po_clk = 0; }
		if (g_midi_stop_pending)  { g_midi_stop_pending  = 0; midi_send(0xFC); }
		if (consumed != g_midi_clk_produced) {
			consumed++;
			midi_send(0xF8);                       /* MIDI clock, 24 PPQN */
			if (++po_clk >= PO_DIV) {              /* PO/Volca sync pulse */
				po_clk = 0;
				NRF_P0->OUTSET = (1u << POSYNC_PIN) | (1u << POSYNC_PIN_B);
				po_off = k_uptime_get() + PO_PULSE_MS;
			}
		} else {
			k_msleep(1);
		}
		if (po_off >= 0 && k_uptime_get() >= po_off) {
			NRF_P0->OUTCLR = (1u << POSYNC_PIN) | (1u << POSYNC_PIN_B);
			po_off = -1;
		}
	}
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

		/* Looper engine: drains the live USB input (prebuffer-gated inside;
		 * silence if the host isn't streaming) and mixes the 4 tracks on top. */
		looper_audio_block(blk);

		int wrc = i2s_write(i2s_dev, blk, BLK_BYTES);
		audio_write_rc = wrc;
		if (wrc != 0) {
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
		audio_blocks++;
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
			K_PRIO_COOP(7), 0, K_NO_WAIT);   /* above main: USB prints can't starve the feed */

	/* eMMC streamer: preemptible + below the audio thread so audio always wins. */
	k_thread_create(&streamer_tcb, streamer_stack, K_THREAD_STACK_SIZEOF(streamer_stack),
			streamer_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	/* MIDI clock + PO sync out over the SYNC jack (bit-banged GPIO, TimK pins).
	 * Low priority; sends bytes when the engine flags them. */
	midi_pins_init();
	k_thread_create(&midi_tcb, midi_stack, K_THREAD_STACK_SIZEOF(midi_stack),
			midi_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(6), 0, K_NO_WAIT);

	/* Wait until the audio thread has the I2S stream running (BCLK live), then
	 * configure the codec here on the main thread. The audio thread keeps the
	 * DMA fed throughout, so its config sleeps never starve the I2S. */
	for (int i = 0; i < 100 && !audio_started; i++)
		k_msleep(2);
	tas2505_configure();
}

/* ---- UAC2 explicit feedback: software regulator (v1) ------------------------
 * The host needs to know how fast the SP-1 actually consumes samples. The
 * The SP-1 I2S bus runs at exactly 48000 Hz (codec-mastered), so we report
 * nominal rate would make the host over-deliver and overflow the ring. Nordic
 * only ships a hardware feedback measurement for the nRF5340 (it needs an I2S
 * FRAMESTART event the nRF52840 lacks), so we regulate in software, reporting a
 * USB Q10.14 "samples per SOF" value (1.0 sample = 1<<14 in the low 24 bits).
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
 * over-speed packets. A handful of buffers covers SOF/scheduling overlap. */
#define UAC2_IN_TERMINAL_ID  UAC2_ENTITY_ID(DT_NODELABEL(in_terminal))
#define UAC2_MAX_PKT         ((48 + 1) * USB_FRAME_BYTES)
K_MEM_SLAB_DEFINE_STATIC(uac2_rx_slab, ROUND_UP(UAC2_MAX_PKT, UDC_BUF_GRANULARITY),
			 6, UDC_BUF_ALIGN);

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
		if (k_mem_slab_alloc(&uac2_rx_slab, &buf, K_NO_WAIT) != 0) {
			buf = NULL;            /* back-pressure: stack will retry */
		}
	}

	return buf;
}

static void uac2_data_recv_cb(const struct device *dev, uint8_t terminal,
			      void *buf, uint16_t size, void *user_data)
{
	ARG_UNUSED(dev); ARG_UNUSED(terminal); ARG_UNUSED(user_data);

	if (g_usb_streaming && size) {
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
	if (g_fb_running) {
		feedback_update();
	}
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

	if (usbd_enable(usbd) != 0) {
		printk("usbd enable failed\n");
	}
}

/* Stream the raw ladder codes, but ONLY when a host has opened the port
 * (DTR asserted). That keeps us from ever stalling the watchdog loop when
 * nothing is listening. Throttled by the caller. */
static void controls_diag(void)
{
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
	printk("LOOPER %dHz song=%d %s hp=%d hpin=%d usb=%d chg=%d batt=%d bpm=%d detbpm=%d vol=%d "
	       "trk[%s %s %s %s] rec=%d ovr=%u rerr=%u werr=%u\n",
	       (int)LOOP_RATE, (int)g_slot, g_playing ? "PLAY" : "STOP", g_hp_on, g_hp_in,
	       usb_present() ? 1 : 0, charging() ? 1 : 0, batt,
	       g_play_bpm, g_det_bpm, g_master_vol_q8,
	       tsn[trk[0].state % 5], tsn[trk[1].state % 5],
	       tsn[trk[2].state % 5], tsn[trk[3].state % 5],
	       g_rec_track, (unsigned)g_rec_overruns,
	       (unsigned)emmc_crc_rd_errs, (unsigned)emmc_crc_wr_errs);
}

/* ---- decode the ladders into named buttons (verified thresholds) ---- */
enum trk_btn { TRK_NONE = -1, TRK_1, TRK_2, TRK_3, TRK_4, TRK_PLAY };
enum vol_btn { VOL_NONE = -1, VOL_TEMPO_DOWN, VOL_DOWN, VOL_TEMPO_UP, VOL_UP };

static enum trk_btn decode_tracks(int v)
{
	if (v <  110) return TRK_NONE;
	if (v <  300) return TRK_1;     /* ~213  */
	if (v <  560) return TRK_2;     /* ~403  */
	if (v <  950) return TRK_3;     /* ~733  */
	if (v < 1500) return TRK_4;     /* ~1220 */
	return TRK_PLAY;                /* ~1823 */
}

static enum vol_btn decode_vol(int v)
{
	if (v <  200) return VOL_NONE;
	if (v <  560) return VOL_TEMPO_DOWN; /* ~404  */
	if (v <  950) return VOL_DOWN;       /* ~729  */
	if (v < 1500) return VOL_TEMPO_UP;   /* ~1220 */
	return VOL_UP;                       /* ~1820 */
}

/* ---------- LED helpers ---------- */
static void led_cfg_output(const struct led *l)
{
	l->port->PIN_CNF[l->pin] =
		(GPIO_PIN_CNF_DIR_Output    << GPIO_PIN_CNF_DIR_Pos)   |
		(GPIO_PIN_CNF_DRIVE_S0S1    << GPIO_PIN_CNF_DRIVE_Pos) |
		(GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
}
static void led_on(int i)  { leds[i].port->OUTSET = (1u << leds[i].pin); }
static void led_off(int i) { leds[i].port->OUTCLR = (1u << leds[i].pin); }
static void all_off(void)  { for (int i = 0; i < NUM_LEDS; i++) led_off(i); }
/* Show which song is active on the 4 status LEDs (song 0 -> LED 0 ... song 3 ->
 * LED 3) — the same LEDs that do the power on/off sweep. */
static void show_song_leds(void)
{
	for (int i = 0; i < NUM_LEDS; i++)
		((uint32_t)i == g_slot) ? led_on(i) : led_off(i);
}

static void track_led_on(int i)  { track_leds[i].port->OUTSET = (1u << track_leds[i].pin); }
static void track_led_off(int i) { track_leds[i].port->OUTCLR = (1u << track_leds[i].pin); }
static void track_all_off(void)  { for (int i = 0; i < NUM_TRACK_LEDS; i++) track_led_off(i); }

/* Clear BOTH LED rows. Used on power-off so nothing is left lit when SYSTEM_OFF
 * freezes the GPIO levels (the old power_off cleared only the status row, which
 * is exactly why the track/fader lights stayed on after powering down). */
static void shutdown_leds(void) { all_off(); track_all_off(); }

/* The single owner of the LEDs in normal running. Status row = song indicator.
 * Track row = per-track looper state (rec solid / armed blink / playing pulse),
 * OR — when no host audio is streaming AND nothing is recorded — a calm "standby"
 * chase so the device clearly reads as on-and-waiting instead of four dead LEDs.
 * As soon as a host streams audio or a loop exists, it falls through to state. */
static void led_service(void)
{
	/* The standby chase means "never used yet": it shows until the FIRST time a
	 * host streams audio (or anything is recorded) and then never returns. A
	 * live host-presence gate flickered the chase mid-session whenever the
	 * player closed the stream between songs / on pause. */
	static int ever_streamed;
	if (g_usb_streaming) ever_streamed = 1;

	show_song_leds();                              /* status row = current song */

	int active = g_loop_active;
	for (int i = 0; i < NTRK; i++)
		if (trk[i].state != TS_EMPTY) active = 1;

	if (!ever_streamed && !active) {
		/* STANDBY: no audio in + nothing recorded -> gentle chase = "waiting" */
		static uint32_t ch;
		uint32_t pos = (ch++ / 40u) % NUM_TRACK_LEDS;   /* advance ~every 320 ms */
		for (int i = 0; i < NUM_TRACK_LEDS; i++)
			((uint32_t)i == pos) ? track_led_on(i) : track_led_off(i);
	} else {
		int on_beat = (g_beat_phase < (BEAT_SAMPLES_L / 8u));
		for (int i = 0; i < NUM_TRACK_LEDS; i++) {
			uint8_t st = trk[i].state;
			if (st == TS_REC || st == TS_DONE)      track_led_on(i);
			else if (st == TS_ARMED)                (on_beat ? track_led_on(i) : track_led_off(i));
			else if (st == TS_PLAY && on_beat && !trk[i].muted) track_led_on(i);
			else                                    track_led_off(i);
		}
	}
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
	for (int i = 0; i < 300; i++) {      /* bounded ~3 s (WDT is 4 s, fed each pass) */
		feed_wdt();
		int busy = (g_rec_track >= 0) || g_meta_save_req;
		for (int t = 0; t < NTRK; t++)
			if (trk[t].state == TS_REC || trk[t].state == TS_DONE) busy = 1;
		if (!busy) break;
		k_msleep(10);
	}
}

/* ---------- power off ---------- */
static void power_off(void)
{
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
	gpio_drive_low(OSC_EN_PORT, OSC_EN_PIN);   /* osc off: it would otherwise
	                              keep drawing battery through SYSTEM_OFF */
	pwr_btn_arm_wake();
	feed_wdt();
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

/* Advance to the next song slot: save the current song's BPM, load the next
 * one's BPM, and signal the audio thread to reload that slot's tracks. */
static void next_slot(void)
{
	if (!g_meta_loaded || g_slot_switch_req) return;    /* ignore until the last switch lands */
	/* never switch songs while a take is armed/recording/flushing — the slot
	 * reload would trample the take's state and strand its unflushed audio */
	if (g_rec_track >= 0) return;
	for (int i = 0; i < NTRK; i++) {
		uint8_t st = trk[i].state;
		if (st == TS_ARMED || st == TS_REC || st == TS_DONE) return;
	}
	if (g_slot >= NUM_SLOTS) g_slot = 0;
	g_meta.slot[g_slot].speed_q16 = g_play_speed_q16;   /* remember where you left it */
	uint32_t ns = (g_slot + 1u) % NUM_SLOTS;
	g_meta.cur_slot = ns;
	g_slot = ns;
	g_play_speed_q16 = g_meta.slot[ns].speed_q16;        /* resume the new song's BPM */
	g_play_bpm = (int)(((uint64_t)g_play_speed_q16 * LOOP_BPM_BASE + 32768u) / 65536u);
	if (g_play_bpm < BPM_MIN) g_play_bpm = BPM_MIN;
	if (g_play_bpm > BPM_MAX) g_play_bpm = BPM_MAX;
	g_slot_switch_req = 1;
	g_meta_save_req = 1;
}

int main(void)
{
	const struct device *wdt = DEVICE_DT_GET(WDT_NODE);

	/* Read the wake cause BEFORE clearing (write-1-to-clear). OFF = woken from
	 * SYSTEM_OFF by a GPIO SENSE (the power button); DOG = watchdog recovery
	 * (resume fast — the user was mid-session); anything else (VBUS plug-in,
	 * soft reset after a flash, cold power-on) parks in charge-standby below. */
	uint32_t wake_reas = NRF_POWER->RESETREAS;
	NRF_POWER->RESETREAS = 0xFFFFFFFFu;   /* clear on boot */

	pwr_btn_cfg_input();
	charger_init();                 /* make sure the battery actually charges */
	for (int i = 0; i < NUM_LEDS; i++)
		led_cfg_output(&leds[i]);
	for (int i = 0; i < NUM_TRACK_LEDS; i++)
		led_cfg_output(&track_leds[i]);
	all_off();
	track_all_off();

	if (device_is_ready(wdt)) {
		wdt_install_timeout(wdt, &(struct wdt_timeout_cfg){
			.window.max = 4000, .callback = NULL,
		});
		wdt_setup(wdt, 0);
	}
	feed_wdt();

	/* ---- CHARGE-STANDBY: the device no longer springs to life on its own ----
	 * Plugging USB in (or finishing a flash, or inserting a battery) lands here:
	 * silent, looper untouched, LED 1 blinking while charging / solid when full.
	 * HOLD the power button ~0.6 s to actually switch ON. On battery with no
	 * button held there is nothing to do -> clean SYSTEM_OFF (button wakes).
	 * A power-button wake or watchdog recovery skips straight to full boot —
	 * and even if the bootloader scrubs RESETREAS, the user waking the device
	 * is already holding the button, so the hold path turns it on anyway. */
	if (!(wake_reas & (POWER_RESETREAS_OFF_Msk | POWER_RESETREAS_DOG_Msk))) {
		int64_t hold_t = -1;
		uint32_t blink = 0;
		while (1) {
			feed_wdt();
			if (pwr_pressed()) {
				if (hold_t < 0) hold_t = k_uptime_get();
				else if (k_uptime_get() - hold_t >= 600)
					break;                    /* -> full power-on */
				led_on(0);                        /* press feedback */
			} else {
				hold_t = -1;
				if (!usb_present())
					power_off();              /* battery, idle -> off */
				/* charge indication: blink = charging, solid = full */
				if (charging()) ((++blink / 12u) & 1u) ? led_on(0) : led_off(0);
				else            led_on(0);
				for (int i = 1; i < NUM_LEDS; i++) led_off(i);
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
		g_slot_switch_req = 1;
	}

	int64_t press_start = -1;
	int64_t last_diag = 0;      /* throttle the control read-out */

	while (1) {
		feed_wdt();

		/* Milestone 1: report the raw ladder codes ~6x/sec so we can map
		 * each button. Only prints when a serial monitor is attached. */
		int64_t now = k_uptime_get();
		if (now - last_diag >= 500) {
			last_diag = now;
			controls_diag();
			feed_wdt();      /* the diag print path can be slow; never starve the WDT */
		}

		/* (track LEDs are driven by the looper beat clock below) */

		/* FUNCTION button: a SHORT tap changes song; a long HOLD powers off
		 * (the same button does both, like the original device). */
		if (pwr_pressed()) {
			if (press_start < 0)
				press_start = k_uptime_get();

			int64_t held = k_uptime_get() - press_start;

			if (held >= HOLD_MS_TO_OFF)
				power_off();             /* never returns */

			/* show the power-off countdown only once it's clearly a hold, so a
			 * quick tap (song change) doesn't flash it. Clear BOTH rows so the
			 * countdown fills cleanly against a dark track row. */
			if (held > 400) {
				int lit = (int)((held * NUM_LEDS) / HOLD_MS_TO_OFF) + 1;
				if (lit > NUM_LEDS) lit = NUM_LEDS;
				all_off();
				track_all_off();
				for (int i = 0; i < lit; i++) led_on(i);
			}
			k_msleep(25);
			continue;
		}

		if (press_start >= 0) {                  /* just released */
			if ((k_uptime_get() - press_start) < 600) next_slot();   /* short tap -> next song */
			all_off();
		}
		press_start = -1;

		/* ---- looper controls + LEDs ---- */
		{
			/* FAILSAFE: Track1+Track4 combo (AIN0 ~1325, between T4 1220 and PLAY
			 * 1823) held ~1.2 s -> reset into the bootloader for reflashing. Checked
			 * BEFORE the normal decode so the combo isn't mistaken for a Track-4 press. */
			int trk_raw = ladder_read(&adc_ladder[LAD_TRACKS]);
			static int64_t combo14_t = -1;     /* when the 1+4 band was first seen */
			enum trk_btn raw;
			if (trk_raw >= 1280 && trk_raw <= 1390) {
				/* time-based (not a +8/iter counter) so the diag-print path can't
				 * skew the 1.2 s threshold; the oversampled read + the band needing
				 * to hold for a full 1.2 s makes an accidental Track-4 drift safe. */
				if (combo14_t < 0) combo14_t = k_uptime_get();
				else if (k_uptime_get() - combo14_t >= 1200) enter_dfu();
				raw = TRK_NONE;
			} else {
				combo14_t = -1;
				raw = decode_tracks(trk_raw);
			}

			/* STICKY DEBOUNCE -> `committed` (the stable, settled button). Recording
			 * stops on RELEASE, so a single noisy ADC sample (audio/USB activity
			 * couples into the button ladder while a loop streams) must NOT look like
			 * a release: the committed button only changes after a DIFFERENT value is
			 * seen on 3 consecutive reads (~24 ms); a lone glitch back to the held
			 * value resets the counter, so a steady hold can never false-trigger. */
			static enum trk_btn committed = TRK_NONE, cand = TRK_NONE;
			static int cand_cnt;
			enum trk_btn before = committed;
			if (raw == committed) {
				cand_cnt = 0;
			} else if (raw == cand) {
				if (++cand_cnt >= 3) { committed = raw; cand_cnt = 0; }
			} else {
				cand = raw; cand_cnt = 1;
			}

			/* TRACK buttons, three gestures:
			 *   TAP  (<300 ms)            -> MUTE/unmute the track (audio off,
			 *                                content kept — tap again to bring
			 *                                it back)
			 *   HOLD (>=300 ms)           -> RECORD while held (take starts at
			 *                                the 300 ms mark, stops on release)
			 *   DOUBLE-TAP (2nd press
			 *      <400 ms after a tap)   -> DELETE the track
			 * Recording no longer starts on the press edge — a press only arms
			 * a pending hold, so taps can't leave record stubs on a track. */
			static int64_t tap_press_t[NTRK], tap_release_t[NTRK];
			static int hold_pend = -1;       /* track whose press awaits 300 ms */
			if (committed != before) {
				int64_t tnow = k_uptime_get();
				if (before >= TRK_1 && before <= TRK_4) {
					int ti = (int)before;
					g_stop_req = 1;          /* released -> stop a running take */
					tap_release_t[ti] = tnow;
					if (hold_pend == ti) {   /* released before 300 ms = TAP */
						hold_pend = -1;
						trk[ti].muted = !trk[ti].muted;
					}
				}
				if (committed >= TRK_1 && committed <= TRK_4) {
					int ti = (int)committed;
					int64_t dur = tap_release_t[ti] - tap_press_t[ti];
					int was_tap = (dur > 0 && dur < 300);
					if (was_tap && (tnow - tap_release_t[ti]) < 400) {
						g_del_req[ti] = 1;               /* double-tap -> delete */
						trk[ti].muted = 0;               /* undo the tap's mute */
					} else {
						hold_pend = ti;                  /* wait out the tap window */
					}
					tap_press_t[ti] = tnow;
				}
			}
			/* press has outlasted the tap window -> it's a HOLD: start recording */
			if (hold_pend >= 0) {
				if (committed != (enum trk_btn)hold_pend) {
					hold_pend = -1;          /* lost the button (noise/slip) */
				} else if (k_uptime_get() - tap_press_t[hold_pend] >= 300) {
					g_arm_req[hold_pend] = 1;
					g_playing = 1;           /* recording implies play */
					hold_pend = -1;
				}
			}
			g_dbg_btn = (int)committed;                      /* diag: settled button */

			/* PLAY/STOP button: a short TAP toggles play/stop in place (tape ramp);
			 * a HOLD (>=400 ms) jumps to the START of the song and plays — a reliable
			 * "play the whole thing from the top" that never depends on current state. */
			static int64_t play_t = -1; static int play_held;
			if (committed == TRK_PLAY) {
				if (play_t < 0) { play_t = k_uptime_get(); play_held = 0; }
				else if (!play_held && (k_uptime_get() - play_t) >= 400) {
					g_restart_req = 1; play_held = 1;        /* hold -> play from start */
				}
			} else {
				/* short tap -> toggle play/stop. Ignored while a take is in
				 * progress: stopping would ramp the tape to 0 and freeze the
				 * recording mid-take (it could never reach its end). */
				if (play_t >= 0 && !play_held && g_rec_track < 0) {
					g_playing = !g_playing;
					if (g_playing) g_midi_start_pending = 1;
					else           g_midi_stop_pending  = 1;
				}
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
					trk[fi].vol_q8 = (uint16_t)(q > 256u ? 256u : q);
				}
				fi = (fi + 1) & 3;
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
			{
				static int64_t vrep_t = -1, vrep_last;
				int vdir = (vcommit == VOL_UP) ? 1 : (vcommit == VOL_DOWN) ? -1 : 0;
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
			 * derived exactly from the integer BPM, so 80 = exactly 1.0x. */
			{
				static int64_t tempo_t = -1, tempo_last;
				/* tempo LOCKED while a take is in flight: a mid-take speed
				 * glide records the warp into the loop (tape-bend artifact) */
				int dir = (g_rec_track >= 0) ? 0 :
					  (vcommit == VOL_TEMPO_UP) ? 1 :
					  (vcommit == VOL_TEMPO_DOWN) ? -1 : 0;
				int step = 0;
				if (dir != 0) {
					int64_t tnow = k_uptime_get();
					if (vcommit != vbefore) {            /* fresh click */
						step = 1; tempo_t = tnow; tempo_last = tnow;
					} else if (tempo_t >= 0 && tnow - tempo_t >= 600 &&
						   tnow - tempo_last >= 80) {  /* slow hold-repeat */
						step = 1; tempo_last = tnow;
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
