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

K_MEM_SLAB_DEFINE(tx_slab, BLK_BYTES, 10, 4);   /* 10 blks ~106ms DMA cushion — the PROVEN WORKING.bin
                                                 * value. (A codec-era trim to 4 was never validated on
                                                 * hardware and rode along in every failed build.) */
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
static K_THREAD_STACK_DEFINE(audio_stack, 3072);  /* +1K margin over the historical 2048: the
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
#define USB_RING_FRAMES   4096u              /* ~85 ms — the PROVEN WORKING.bin value. This ring buffers
                                              * the host's UAC2 stream, i.e. the RECORD SOURCE: the
                                              * codec-era trim to 2048 was never validated and rode
                                              * along in every failed build. */
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
static volatile uint32_t g_usb_pkts;               /* diag: ISO packets received (~1000/s streaming) */
static volatile uint32_t g_usb_frames;             /* diag: audio frames received (~48000/s streaming) */
static volatile uint32_t g_sof_cnt;                /* diag: SOFs seen by the feedback regulator (1000/s) */
static volatile uint32_t g_zero_pad;               /* diag: silence frames padded into short blocks */
static volatile uint32_t g_rx_nobuf;               /* diag: ISO packets DROPPED — rx pool empty (the
                                                    * exact mechanism: ISO never retries a NAKed buffer) */
static volatile uint32_t g_rx_slab_min = 0xFFFF;   /* diag: window MIN free rx buffers */
static volatile int32_t  g_usb_lowat = 0x7FFFFFFF; /* diag: window MIN usb-in ring fill, frames */
static volatile uint32_t g_usb_hiwat;              /* diag: window MAX usb-in ring fill, frames */

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
#ifndef SP1_CODEC
#define SP1_CODEC        SP1_CODEC_PCM    /* FULL 16-BIT PCM — the proven WORKING.bin
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
#else
#error "SP1_CODEC must be 0 (PCM), 1 (ULAW) or 2 (ADPCM)"
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
/* round the per-track region UP to a 4096-block (2MB) multiple so every track
 * region stays 2MB-aligned. (The original reason was a pre-erase pass that has
 * since been removed; the alignment is harmless and is kept so the on-card
 * layout / META_MAGIC do not change.) */
#define TRACK_BLOCKS     ((((MAX_LOOP_BLOCKS + 8u) + 4095u) / 4096u) * 4096u)
#define RING_SAMPLES     16384u                            /* ~341 ms read-ahead @48k (reverted 8192->16384 to give the compressed codecs comfortable play-ring margin) */
#define RING_MASK        (RING_SAMPLES - 1u)
/* Play-ring critical margin for scheduling decisions: 128 ms expressed in
 * samples at the loop rate — EXPLICIT and codec-independent (the old
 * 24u*SAMP_PER_BLK silently varied 2.5x across codec block sizes). */
#define PLAY_CRIT_SAMPLES (128u * (LOOP_RATE / 1000u))

/* ---- SONG SLOTS + eMMC layout ----------------------------------------------
 * The looper owns the whole eMMC starting at block 0: block 0 holds the slot
 * metadata (this OVERWRITES the original TE "ALBUM_PR" index, deleting the songs
 * and reclaiming the space — they couldn't be played anyway), tracks follow.
 * NUM_SLOTS independent songs, each with its own saved BPM + 4 tracks. There are
 * 8 songs shown on the 4 status LEDs in two banks: LED = song % 4; songs 1-4
 * display solid (identical to the original), songs 5-8 blink ~2 Hz. */
#define NUM_SLOTS        8u
#define META_BLOCK       0u
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
#define META_MAGIC       0x53453841u                       /* 'SE8A' — 48 kHz, PCM 16-bit, 8-song index */
#define META_MAGIC_V4    0x53453441u                       /* 'SE4A' — the pre-fork 4-song index (migrated) */
#  elif SP1_CODEC == SP1_CODEC_ULAW
#define META_MAGIC       0x53455534u                       /* 'SEU4' — 48 kHz, u-law 8-bit */
#  else
#define META_MAGIC       0x53454134u                       /* 'SEA4' — 48 kHz, IMA-ADPCM 4-bit */
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
};
/* The index must fit its single eMMC block. 8 songs = 492 of 512 bytes — the
 * exact maximum. 9+ songs needs a multi-block index format; this guard turns
 * that mistake into a compile error instead of storage corruption. */
BUILD_ASSERT(sizeof(struct meta_blk) <= EMMC_BLOCK_SIZE,
	     "meta_blk outgrew eMMC block 0 — needs a multi-block index format");
static struct meta_blk   g_meta;
static volatile uint32_t g_slot;
static volatile int      g_slot_switch_req;   /* main -> audio: reload tracks for the new slot */
static volatile int      g_meta_save_req;     /* -> streamer: persist g_meta to eMMC */
/* PASS 2 forensics (printed + zeroed each diag window): blocks delivered per
 * track, dead-history snaps per track, and round aborts (rec yield / read fail). */
static volatile uint32_t g_p2blk[4];
static volatile uint32_t g_p2snap[4];
static volatile uint32_t g_p2yield, g_p2rfail;
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
	uint16_t          fade;              /* starve-recovery fade-in position (256 = full; mixer-only) */
	uint16_t          vol_now;           /* gain actually applied last block (mixer-only; ramps toward fader/mute target) */
	uint8_t           rec_fade;          /* stop-pad fade-down remaining, of 128 (recorder-only) */
	uint8_t           rec_fstep;         /* fade decrement per sample (fits the fade inside the pad) */
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
#define RRING_SAMPLES    16384u   /* ~341 ms record backlog (reverted 32768->16384): the compressed codecs cut flush traffic, so the doubled rec ring is no longer needed; this reclaims RAM for the play-ring revert */
#define RRING_MASK       (RRING_SAMPLES - 1u)
static int16_t g_rring[RRING_SAMPLES];
static volatile uint32_t g_rec_overruns;         /* diag: rec ring overflow events */
static volatile uint32_t g_starve_cnt[NTRK];     /* diag: per-track play-ring underrun episodes */
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
static volatile uint32_t g_beat_phase;            /* phase within a beat (loop samples), for LEDs */
static volatile int      g_emmc_ready;
static volatile int      g_dbg_beat;              /* current beat number (diag) */
static volatile int      g_dbg_btn = -1;          /* committed track button (diag) */
static uint64_t          g_sample_clock;          /* free-running I2S frames (idle metronome) */
static int64_t           g_dec_acc;                /* live accumulator for record decimation (int64: cannot overflow when the transport is stopped / step rounds to 0) */
static uint32_t          g_frames_since;           /* I2S frames since the last loop-sample tick */
static uint32_t          g_pphase;                 /* Q16 playback phase */
static volatile uint32_t g_play_speed_q16 = 65536; /* tape speed when playing (Q16, 65536=1.0x); rocker sets */
static volatile uint8_t  g_fixed_len;              /* 0 = independent/variable loop lengths (default);
                                                    * 1 = fixed: overdubs snap to a whole multiple of
                                                    * track 1 so all tracks stay in sync. Toggled live by
                                                    * the FUNCTION+PLAY gesture; see the control loop. */
/* Tempo as an INTEGER BPM (rocker steps it 1 BPM per click for fine control).
 * Speed is derived exactly: speed = bpm * 65536 / LOOP_BPM_BASE, so 80 BPM is
 * exactly 1.0x — no detent/snap logic needed. Range 40..120 = 0.5x..1.5x. */
#define BPM_MIN 40
#define BPM_MAX 120
static volatile int g_play_bpm = 80;
/* auto-start thresholds (loop-sample domain @ LOOP_RATE) */
#define SOUND_THRESHOLD  1000              /* int16 level (~ -30 dBFS) */
#define SOUND_WAIT_TICKS (LOOP_RATE * 4u)  /* ~4 s fallback */
/* track-button gesture timing */
#define HOLD_RECORD_MS   180   /* physical button-down this long (ms) => RECORD; shorter => TAP */
#define DTAP_GAP_MS      420   /* 2nd tap within this of the 1st tap's release => DOUBLE-TAP delete */

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
	g_midi_div = (beat + 12u) / 24u;          /* precompute once: no per-sample divide */
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
/* mix-only -O2: the audio hot path. Safe here (unlike global -O2): the two signed-
 * overflow UB sites are fixed with int64 casts, -fno-strict-aliasing is global, and
 * this function contains NO flash-write code -- same per-function -O2 already proven
 * werr-safe on the eMMC read path. Speeds the per-frame interp/volume/limit work. */
__attribute__((optimize("O2")))
static void looper_audio_block(int16_t *s)
{
	static int16_t tmp[BLK_FRAMES * 2];
	if (g_xfer_mode) { memset(s, 0, BLK_BYTES); return; }   /* USB transfer: silence out */
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
	}
	uint32_t bytes = primed ?
		ring_buf_get(&usb_audio_ring, (uint8_t *)tmp, sizeof(tmp)) : 0;
	k_sched_unlock();
	uint32_t got = bytes / USB_FRAME_BYTES;
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
		trk[i].state = TS_EMPTY;
		trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].rec_count = 0; trk[i].muted = 0;
		trk[i].len_blocks = 0; trk[i].start_blk = 0; trk[i].content_blocks = 0;  /* drop all its segments */
		if (g_slot < NUM_SLOTS) {
			g_meta.slot[g_slot].present[i] = 0;
			g_meta.slot[g_slot].trk_len[i] = 0;
			g_meta.slot[g_slot].trk_start[i] = 0;
			g_meta.trk_content[g_slot][i] = 0;   /* keep the on-flash block self-consistent */
		}
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
				g_rec_track = -1;
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
						g_loop_blocks = g_loop_len / SAMP_PER_BLK;
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
				uint32_t content = (trk[i].rec_count + SAMP_PER_BLK - 1u)
						   / SAMP_PER_BLK;
				if (content < 1u) content = 1u;
				else if (content > MAX_LOOP_BLOCKS) content = MAX_LOOP_BLOCKS;
				if (g_loop_len == 0u) {
					g_loop_len = content * SAMP_PER_BLK;
					g_loop_blocks = content;
					tempo_finish();         /* set the detected beat grid + BPM */
					if (g_slot < NUM_SLOTS) {
						g_meta.slot[g_slot].loop_len = g_loop_len;
						g_meta_save_req = 1;
					}
				}
				uint32_t len = content;
				uint32_t tgt = content * SAMP_PER_BLK;   /* default: stop now */
				uint8_t  sil = 1;                        /* pad the final sub-block */
				if (trk[i].rec_target && !trk[i].rec_silence) {
					/* SECOND tap while a fixed-mode take is running on to
					 * the bar line (below): stop IMMEDIATELY — the loop
					 * keeps the already-snapped bar length; the unfilled
					 * remainder plays as silence (the old behavior, as an
					 * escape hatch). */
					len = trk[i].len_blocks;
				} else if (g_fixed_len && g_loop_blocks) {
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
					uint32_t mult = (content + g_loop_blocks / 2u) / g_loop_blocks;
					if (mult < 1u) mult = 1u;
					uint32_t nlen = mult * g_loop_blocks;
					if (nlen <= MAX_LOOP_BLOCKS) {
						len = nlen;
						if (nlen * SAMP_PER_BLK > trk[i].rec_count) {
							/* EARLY: run on to the bar, capturing live */
							content = nlen;
							tgt = nlen * SAMP_PER_BLK;
							sil = 0;
						}
					}
					/* nlen over the region: len stays = content, stop now */
				}
				trk[i].content_blocks = content;     /* audio ends here */
				trk[i].len_blocks     = len;         /* loop length */
				trk[i].rec_target     = tgt;
				/* end the live phrase. When padding (immediate stops), the
				 * pad used to be hard zeros — a click baked into the seam;
				 * fade the first 128 pad samples (~2.7 ms) down instead. */
				trk[i].rec_silence = sil;
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
			g_cur_speed_q16 = g_play_speed_q16;   /* snap to the set tape speed */
			g_loop_active = 1; g_consume_pos = 0;
			g_pphase = 0; g_frames_since = 0; g_dec_acc = 0;
		}
		/* ARM (first take AND overdub): wait for the first sound, then the tick
		 * handler begins the capture so the loop starts exactly on the audio. */
		trk[i].r_w = 0; trk[i].r_r = 0; trk[i].flush_blk = 0;
		trk[i].flush_mod = MAX_LOOP_BLOCKS;
		trk[i].rec_count = 0; trk[i].rec_silence = 0; trk[i].rec_target = 0; trk[i].muted = 0;
		trk[i].wait_peak = 0; trk[i].wait_ticks = 0;
		/* NOTE: len_blocks/start_blk are NOT reset here -- they are set when the
		 * first sound lands (TS_REC). Leaving them intact means a re-record that
		 * is cancelled (released before any sound) returns the track to PLAY with
		 * its ORIGINAL length, not a clobbered one. */
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
			g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0; g_midi_cnt = 0;
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
		g_consume_pos = 0; g_pphase = 0; g_frames_since = 0; g_dec_acc = 0; g_midi_cnt = 0;
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
			/* SEGMENT: restore this track's own length + phase anchor (older saves
			 * with trk_len==0 fall back to the base length = one segment). */
			if (pres && g_slot < NUM_SLOTS) {
				uint32_t L = g_meta.slot[g_slot].trk_len[i];
				trk[i].len_blocks = L ? L : g_loop_blocks;
				trk[i].start_blk  = g_meta.slot[g_slot].trk_start[i];
				trk[i].content_blocks = g_meta.trk_content[g_slot][i]; /* 0 = whole track */
			} else {
				trk[i].len_blocks = 0; trk[i].start_blk = 0;
				trk[i].content_blocks = 0;
			}
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
	for (int i = 0; i < NTRK; i++) vol_s[i] = trk[i].vol_q8;

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
	static int32_t  mix32[BLK_FRAMES];
	for (uint32_t f = 0; f < BLK_FRAMES; f++) {
		int32_t live = (f < got)
			? (((int32_t)tmp[2 * f] + (int32_t)tmp[2 * f + 1]) >> 1) : 0;

		/* (the first take is started immediately by the press handler above, and
		 * overdubs are started on the next beat by the wrap logic below) */
		mix32[f] = live;                        /* live monitor under the mix */
		posb[f]  = g_consume_pos;
		fracb[f] = (uint16_t)(g_pphase & 0xFFFFu);

		/* advance the playback phase; each integer step is one loop-sample tick */
		g_dec_acc += live; g_frames_since++;
		if (g_loop_active) {
			g_pphase += step;
			while (g_pphase >= 65536u) {
				g_pphase -= 65536u;
				/* Decimate the live input to the current tape rate.
				 * >1x (frames_since==0, a 2nd+ emit in one input frame):
				 * HOLD the current sample instead of emitting a zero — the
				 * old zero-stuffing was the metallic aliasing/bitcrush. 1x:
				 * the one accumulated sample. <1x: average the frames. */
				int16_t lsamp;
				if (g_frames_since == 0u)      lsamp = (int16_t)live;
				else if (g_frames_since == 1u) lsamp = (int16_t)g_dec_acc;
				else if (g_frames_since < 65536u)
					lsamp = (int16_t)((int32_t)g_dec_acc /
							  (int32_t)g_frames_since); /* hw SDIV, bit-identical */
				else lsamp = (int16_t)(g_dec_acc / (int64_t)g_frames_since);
				g_dec_acc = 0; g_frames_since = 0;

				int rt_i = g_rec_track;
				if (rt_i >= 0 && trk[rt_i].state == TS_ARMED) {
					/* AUTO-START: hold armed until the input first crosses
					 * the threshold. NO TIMEOUT any more: the old ~4 s
					 * fallback started recording SILENCE on its own
					 * (community: "once armed it should only rely on sound
					 * input... after 8 tics it starts on its own"). An
					 * armed track now waits indefinitely — tap it to
					 * cancel; the blinking LED shows it is armed. */
					struct looptrk *rt = &trk[rt_i];
					int32_t aa = lsamp < 0 ? -lsamp : lsamp;
					/* trigger directly on the first sample past threshold (no
					 * running-peak tracking needed) -- one less op per armed sample */
					if (aa >= SOUND_THRESHOLD) {
						if (g_loop_len == 0u) {
							/* first take: this sound is loop position 0 */
							g_consume_pos = 0; g_midi_start_pending = 1; g_midi_cnt = 0;
							tempo_reset();
							rt->flush_blk = 0; rt->flush_mod = MAX_LOOP_BLOCKS;
							rt->rec_target = 0;
							rt->start_blk = 0;        /* the base take anchors the grid at 0 */
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
							rt->start_blk = g_consume_pos / SAMP_PER_BLK;
						}
						rt->r_w = 0; rt->r_r = 0; rt->rec_count = 0; rt->rec_silence = 0;
						rt->state = TS_REC;
					}
				}
				if (g_rec_track >= 0 && trk[g_rec_track].state == TS_REC) {
					struct looptrk *rt = &trk[g_rec_track];
					if ((rt->r_w - rt->r_r) >= RRING_SAMPLES)
						g_rec_overruns++;   /* take corrupting: flush too slow */
					int16_t wsamp = lsamp;
					if (rt->rec_silence) {
						uint8_t fg = rt->rec_fade;
						if (fg) {
							wsamp = (int16_t)(((int32_t)lsamp * fg) >> 7);
							uint8_t st = rt->rec_fstep ? rt->rec_fstep : 1u;
							rt->rec_fade = (fg > st) ? (uint8_t)(fg - st) : 0u;
						} else {
							wsamp = 0;
						}
					} else if (rt->rec_target) {
						/* fixed-mode run-to-the-bar: fade the final ~2.7 ms
						 * into the bar line so the loop seam can't click */
						uint32_t rem = rt->rec_target - rt->rec_count;
						if (rem <= 128u)
							wsamp = (int16_t)(((int32_t)lsamp *
									   (int32_t)rem) >> 7);
					}
					g_rring[rt->r_w & RRING_MASK] = wsamp;
					rt->r_w++;
					rt->rec_count++;
					if (g_tempo.active) tempo_feed(lsamp, rt->rec_count);
					if (rt->rec_target == 0u) {
						/* OPEN take (first take AND independent overdubs):
						 * force-stop at the maximum length. Only a FIRST
						 * take defines the song grid/BPM. */
						if (rt->rec_count >= MAX_LOOP_SAMPLES) {
							if (g_loop_len == 0u) {
								g_loop_len = MAX_LOOP_SAMPLES;
								g_loop_blocks = g_loop_len / SAMP_PER_BLK;
								tempo_finish();
								if (g_slot < NUM_SLOTS) {
									g_meta.slot[g_slot].loop_len = g_loop_len;
									g_meta_save_req = 1;
								}
							}
							rt->rec_target = MAX_LOOP_SAMPLES;
							rt->len_blocks = MAX_LOOP_BLOCKS;
							rt->content_blocks = MAX_LOOP_BLOCKS;  /* all content */
							rt->state = TS_DONE; g_rec_track = -1;
						}
					} else if (rt->rec_count >= rt->rec_target) {
						/* Take FINALIZE: rec_target is set by the stop tap
						 * (free-length, block-rounded) — the recorder pads
						 * the sub-block remainder with silence and lands
						 * here. The take now loops at its own length. */
						rt->state = TS_DONE; g_rec_track = -1;
					}
				}

				g_consume_pos++;
				/* MIDI 24-PPQN clock: a cheap per-sample COUNTER (the divisor
				 * g_midi_div is precomputed when the tempo is detected) -- no
				 * runtime divide here. The beat-phase display moved to once-per-
				 * block (after this loop); it only drives the LED + diag. */
				if (g_midi_div && ++g_midi_cnt >= g_midi_div) {
					g_midi_cnt = 0; g_midi_clk_produced++;
				}
			}
		}
	}

	/* ==== PASS B: accumulate each playing track over the whole block ==== */
	for (int i = 0; i < NTRK; i++) {
		if (trk[i].state != TS_PLAY) continue;
		/* GAIN SMOOTHING + CLICKLESS MUTE: the fader value used to be
		 * applied as a hard step once per 5 ms block (and mute as an
		 * instant gate) — fast fader rides audibly zipper-clicked and
		 * every mute/unmute popped (community: "fast up-and-down fader
		 * movement sounds a little clicky"). The applied gain now ramps
		 * linearly across the block toward the target (mute = target 0),
		 * spreading any change over 256 samples; a muted track is skipped
		 * entirely once its ramp settles at zero. */
		const int32_t vtar = trk[i].muted ? 0 : (int32_t)vol_s[i];
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
		if (vd == 1 || vd == -1) vd = 0;
		if (vtar == 0 && vd == 0 && vprev == 0) continue;  /* silent and settled */
		trk[i].vol_now = (uint16_t)vtar;
		const int16_t *const pr = trk[i].pring;
		const int32_t vol = vtar;
		/* STOPPED fast path: the transport is frozen (no phase steps this
		 * block), so this track contributes ONE constant sample — compute
		 * it once instead of 256 times. Falls back to the exact per-frame
		 * loop whenever a starve or fade boundary is in flight so those
		 * transitions keep their per-frame behavior. */
		if (step == 0u && !trk[i].starved && trk[i].fade >= 256u && vd == 0) {
			int32_t avail = (int32_t)(trk[i].p_w - posb[0]);
			if (avail < 2) {
				trk[i].starved = 1; g_starve_cnt[i]++;
				continue;
			}
			int16_t a = pr[posb[0] & RING_MASK];
			int16_t sv;
			if (fracb[0] == 0u) {
				sv = a;
			} else {
				int16_t bb = pr[(posb[0] + 1) & RING_MASK];
				sv = (int16_t)((int32_t)a +
					(int32_t)(((int64_t)(bb - a) * (int32_t)fracb[0]) >> 16));
			}
			if (avail < 256) sv = (int16_t)(((int32_t)sv * avail) >> 8);
			int32_t add = ((int32_t)sv * vol) >> 8;
			for (uint32_t f = 0; f < BLK_FRAMES; f++) mix32[f] += add;
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
			for (uint32_t f = 0; f < BLK_FRAMES; f++) {
				uint32_t cpos = posb[f];
				uint32_t frac = fracb[f];
				int16_t a = pr[cpos & RING_MASK];
				int16_t sv;
				if (frac == 0u) {
					sv = a;
				} else {
					int16_t bb = pr[(cpos + 1) & RING_MASK];
					sv = (int16_t)((int32_t)a +
						(int32_t)(((int64_t)(bb - a) * (int32_t)frac) >> 16));
				}
				mix32[f] += ((int32_t)sv * vol) >> 8;
			}
			continue;
		}
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			uint32_t cpos = posb[f];
			/* underrun gate WITH HYSTERESIS (semantics unchanged): once a
			 * ring runs dry the track stays silent until half-refilled
			 * (recovering earlier re-dips and chatters — hardware-tested). */
			int32_t avail = (int32_t)(trk[i].p_w - cpos);
			if (trk[i].starved) {
				if (avail >= (int32_t)(RING_SAMPLES / 2u)) {
					trk[i].starved = 0;
					trk[i].fade = 0;   /* ramp back in (~5 ms), no click */
				} else {
					continue;
				}
			} else if (avail < 2) {
				trk[i].starved = 1; g_starve_cnt[i]++;
				continue;
			}
			uint32_t frac = fracb[f];
			int16_t a = pr[cpos & RING_MASK];
			int16_t sv;
			if (frac == 0u) {
				sv = a;                /* unity speed: no interpolation */
			} else {
				int16_t bb = pr[(cpos + 1) & RING_MASK];
				/* int64 product: (bb-a)*frac can exceed INT32_MAX = signed-
				 * overflow UB; the cast keeps it defined (SMULL on M4). */
				sv = (int16_t)((int32_t)a +
					(int32_t)(((int64_t)(bb - a) * (int32_t)frac) >> 16));
			}
			/* BOUNDARY FADE (unchanged): fade out over the last ~5 ms as
			 * the ring drains, fade in after recovery — dropouts duck
			 * instead of clicking. */
			{
				int32_t g = 256;
				if (avail < 256) g = avail;
				if (trk[i].fade < 256u) {
					if ((int32_t)trk[i].fade < g) g = (int32_t)trk[i].fade;
					trk[i].fade++;
				}
				if (g < 256) sv = (int16_t)(((int32_t)sv * g) >> 8);
			}
			int32_t vf = vd ? (vprev + ((vd * (int32_t)(f + 1)) >> 8)) : vol;
			mix32[f] += ((int32_t)sv * vf) >> 8;
		}
	}

	/* ==== PASS C: master volume + soft limiter -> stereo out ==== */
	{
		/* the VOL buttons step ~3 dB at a time — ramp each step across the
		 * block instead of applying it as a hard gain jump (a click). */
		const int32_t mv = (int32_t)g_master_vol_q8;
		static int32_t mv_prev;
		const int32_t md = mv - mv_prev;
		const int32_t m0 = mv_prev;
		mv_prev = mv;
		for (uint32_t f = 0; f < BLK_FRAMES; f++) {
			int32_t m = md ? (m0 + ((md * (int32_t)(f + 1)) >> 8)) : mv;
			int16_t out = soft_limit((mix32[f] * m) >> 8);
			s[2 * f] = out; s[2 * f + 1] = out;
		}
	}
	g_sample_clock += BLK_FRAMES;
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
		if (sj == TS_PLAY &&
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
static K_THREAD_STACK_DEFINE(streamer_stack, 4096);  /* 4096: the eMMC driver is -O2 here, so its read/send_command/crc chain inlines into a deeper frame on this thread */
static struct k_thread streamer_tcb;

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
static void xfer_commit(void)
{
	static uint8_t mblk[EMMC_BLOCK_SIZE];
	if (g_emmc_ready && emmc_read_blocks(META_BLOCK, mblk, 1)) {
		struct meta_blk *m = (struct meta_blk *)mblk;
		if (m->magic == META_MAGIC && m->cur_slot < NUM_SLOTS) {
			uint32_t keep[NUM_SLOTS][NTRK];
			memcpy(keep, g_meta.trk_content, sizeof(keep));
			memcpy(&g_meta, m, sizeof(g_meta));
			g_slot = g_meta.cur_slot;
			/* the host only manages the legacy fields (see g_xfer_dirty):
			 * restore the mode setting and every untouched track's content
			 * length, then write the repaired index back (skipped when the
			 * host's copy already matches, e.g. a read-only session). */
			g_meta.fixed_len = g_fixed_len;
			for (int s = 0; s < NUM_SLOTS; s++)
				for (int t = 0; t < NTRK; t++)
					if (!g_xfer_dirty[s][t])
						g_meta.trk_content[s][t] = keep[s][t];
			if (memcmp(mblk, &g_meta, sizeof(g_meta)) != 0) {
				memset(mblk, 0, EMMC_BLOCK_SIZE);
				memcpy(mblk, &g_meta, sizeof(g_meta));
				(void)emmc_write_blocks(META_BLOCK, mblk, 1);
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
/* ---- PCM: memcpy-equivalent (handles the single ring wrap) ---------------- */
static void codec_pack(const int16_t *ring, uint32_t ring_mask, uint32_t start,
                       uint8_t *flash, uint32_t nblk)
{
	int16_t *out = (int16_t *)flash;
	uint32_t ntot = nblk * SAMP_PER_BLK;
	uint32_t ring_samps = ring_mask + 1u;
	uint32_t run1 = ring_samps - start;
	if (run1 > ntot) run1 = ntot;
	memcpy(out, &ring[start], run1 * 2u);
	if (ntot > run1)
		memcpy(out + run1, &ring[0], (ntot - run1) * 2u);
}
static void codec_unpack(int16_t *ring, uint32_t ring_mask, uint32_t start,
                         const uint8_t *flash, uint32_t nblk)
{
	const int16_t *in = (const int16_t *)flash;
	uint32_t ntot = nblk * SAMP_PER_BLK;
	uint32_t ring_samps = ring_mask + 1u;
	uint32_t run1 = ring_samps - start;
	if (run1 > ntot) run1 = ntot;
	memcpy(&ring[start], in, run1 * 2u);
	if (ntot > run1)
		memcpy(&ring[0], in + run1, (ntot - run1) * 2u);
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

static void streamer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	static uint8_t blk[EMMC_BLOCK_SIZE];
	/* Flush the rec ring in MULTI-BLOCK (CMD25) bursts: the card pipelines the
	 * programming across the burst instead of fully programming each block (~30 ms
	 * single-block), so the sustained write keeps up with live recording. */
#define FLUSH_BATCH 32u   /* 16KB bursts = 2 whole 8KB pages per CMD25 (reverted from 16: the interleave+16 experiment caused catastrophic rec-ring overflow + flash write errors) */
	static uint8_t batchbuf[FLUSH_BATCH * EMMC_BLOCK_SIZE];

	(void)emmc_init();
	/* AFTER init: emmc_init() resets the clock to the slow safe value — the
	 * old code zeroed it BEFORE init, so every bit-bang phase (start-bit
	 * hunts, CRC tokens, busy polls) has been running ~4x slower than
	 * intended this whole time. Zero it here so it actually sticks. */
	g_emmc_clk_half_us = 0u;
	g_emmc_ready = emmc_is_ready() ? 1 : 0;

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
	if (g_emmc_ready && emmc_read_blocks(META_BLOCK, blk, 1)) {
		struct meta_blk *m = (struct meta_blk *)blk;
		if (m->magic == META_MAGIC && m->cur_slot < NUM_SLOTS) {
			memcpy(&g_meta, m, sizeof(g_meta));     /* resume saved songs */
		} else if (m->magic == META_MAGIC_V4 && m->cur_slot < 4u) {
			/* MIGRATION from the pre-fork 4-song index: the track
			 * REGIONS are unchanged (region address depends only on
			 * slot*NTRK+track), so the four old songs are lifted into
			 * slots 0-3 of the 8-song index and keep playing; slots
			 * 4-7 start empty. Field offsets moved because slot[]
			 * grew, so copy from the old on-disk layout explicitly:
			 * magic@0 cur@4 slot[4]@8 (44 B each: speed@+0 loop@+4
			 * present[4]@+8 trk_len[4]@+12 trk_start[4]@+28)
			 * fixed_len@184 trk_content[4][4]@188. One-time; the
			 * index is immediately re-persisted with the new magic. */
			const uint8_t *ob = blk;
			g_meta.cur_slot = m->cur_slot;
			for (uint32_t s = 0; s < 4u; s++) {
				const uint8_t *o = ob + 8u + s * 44u;
				memcpy(&g_meta.slot[s].speed_q16, o + 0u,  4u);
				memcpy(&g_meta.slot[s].loop_len,  o + 4u,  4u);
				memcpy(g_meta.slot[s].present,    o + 8u,  4u);
				memcpy(g_meta.slot[s].trk_len,    o + 12u, 16u);
				memcpy(g_meta.slot[s].trk_start,  o + 28u, 16u);
			}
			memcpy(&g_meta.fixed_len, ob + 184u, 4u);
			for (uint32_t s = 0; s < 4u; s++)
				memcpy(g_meta.trk_content[s], ob + 188u + s * 16u, 16u);
			memset(blk, 0, EMMC_BLOCK_SIZE);
			memcpy(blk, &g_meta, sizeof(g_meta));
			(void)emmc_write_blocks(META_BLOCK, blk, 1);   /* persist as 'SE8A' */
		} else {
			memset(blk, 0, EMMC_BLOCK_SIZE);
			memcpy(blk, &g_meta, sizeof(g_meta));
			(void)emmc_write_blocks(META_BLOCK, blk, 1);   /* delete old album */
		}
	}
	g_slot = g_meta.cur_slot;
	g_fixed_len = g_meta.fixed_len ? 1u : 0u;   /* restore the saved length mode */
	g_meta_loaded = 1;

	while (1) {
#if SP1_XFER_ENABLE
		/* Website loop transfer: scan for the connect-magic / run one command
		 * per pass. While a transfer is active the transport is paused and
		 * the streamer serves ONLY the transfer (audio is silent anyway). */
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
					} else if (t->state == TS_REC && n < to_wrap) {
						break;            /* let a full page accumulate */
					}
					/* ENCODE: rec ring (int16, wraps at RRING_MASK, r_r is
					 * block-aligned) -> packed flash bytes for n blocks. PCM is
					 * memcpy-equivalent; ULAW/ADPCM compress 2x/4x so this CMD25
					 * moves half/quarter the bytes the card must program. */
					codec_pack(g_rring, RRING_MASK, t->r_r & RRING_MASK,
					           batchbuf, n);
					static uint32_t wfail_start;   /* 0 = no failure streak */
					static uint32_t wfail_key;     /* streak identity (track|flush pos) */
					static uint8_t  wfail_ready1;  /* card seen READY once this streak */
					uint32_t _wkey = ((uint32_t)i << 28) ^ t->flush_blk;
					if (!emmc_write_blocks(blkno, batchbuf, n)) {
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
							k_msleep(2);
							work = true;
							break;
						}
						g_stored_glitch_cnt++;  /* audible as a REPEATING artifact */
					}
					wfail_start = 0;
					wfail_ready1 = 0;
					t->r_r += n * SAMP_PER_BLK;
					t->flush_blk += n;
					work = true;
					/* POST-STALL DRAIN ORDER: a big rec backlog must not
					 * starve the playing rings at their emptiest moment.
					 * After each burst, if any playing ring is inside its
					 * critical margin and the rec ring is NOT at the 7/8
					 * overflow emergency, break to PASS 2 to feed the
					 * emptiest ring one chunk, then resume flushing here.
					 * Burst-granular alternation only (one fully-terminated
					 * CMD25, then reads) — NOT the sub-page interleave that
					 * broke writes in an earlier experiment. */
					if ((t->r_w - t->r_r) <
					    (RRING_SAMPLES - RRING_SAMPLES / 8u)) {
						bool _pcrit = false;
						for (int j = 0; j < NTRK; j++)
							if (trk[j].state == TS_PLAY &&
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
				if (t->state == TS_DONE && (t->r_w - t->r_r) < SAMP_PER_BLK) {
					/* Start playback BLOCK-ALIGNED at the live playhead. p_w must be a
					 * multiple of SAMP_PER_BLK or the streamer writes each eMMC block at
					 * a misaligned ring offset and the track plays ~16 ms out of sync. */
					if (slot < NUM_SLOTS) {
						g_meta.slot[slot].present[i]   = 1;
						g_meta.slot[slot].trk_len[i]   = t->len_blocks;  /* SEGMENT: per-track length */
						g_meta.slot[slot].trk_start[i] = t->start_blk;   /* + phase anchor */
						g_meta.trk_content[slot][i]    = t->content_blocks; /* silence-pad boundary */
					}
					g_meta_save_req = 1;             /* persist the new recording */
#if SP1_CODEC == SP1_CODEC_PCM
					/* TRUNCATED-STOP SEAM (fixed mode, stopped late): the
					 * played region now ends mid-audio at len_blocks — fade
					 * its last ~2.7 ms down on flash so the loop seam
					 * doesn't click. The overhang past len is never read. */
					if (t->content_blocks > t->len_blocks && t->len_blocks) {
						uint32_t _bl = trk_blk(slot, (uint32_t)i) +
							       t->len_blocks - 1u;
						if (emmc_read_blocks(_bl, batchbuf, 1)) {
							int16_t *_sm = (int16_t *)batchbuf;
							for (int _k = 0; _k < 128; _k++) {
								int _ix = SAMP_PER_BLK - 128 + _k;
								_sm[_ix] = (int16_t)(((int32_t)_sm[_ix] *
										      (127 - _k)) >> 7);
							}
							if (!emmc_write_blocks(_bl, batchbuf, 1))
								(void)emmc_write_blocks(_bl, batchbuf, 1);
						}
					}
					/* LOOP-SEAM DECLICK (write side): ramp the take's first
					 * ~1.3 ms in, ONCE, on flash. Every lap of the loop plays
					 * last-sample -> first-sample; with a hard start that seam
					 * clicks ("loop in/out transient" in community feedback).
					 * The stop side is faded live by the recorder (rec_fade),
					 * so with both ends tapered the seam is silent-to-silent.
					 * 64 samples barely soften a real attack transient. PCM
					 * only: in-place sample math on packed flash bytes. */
					{
						uint32_t _b0 = trk_blk(slot, (uint32_t)i);
						if (emmc_read_blocks(_b0, batchbuf, 1)) {
							int16_t *_sm = (int16_t *)batchbuf;
							for (int _k = 0; _k < 64; _k++)
								_sm[_k] = (int16_t)(((int32_t)_sm[_k] * _k) >> 6);
							if (!emmc_write_blocks(_b0, batchbuf, 1))
								(void)emmc_write_blocks(_b0, batchbuf, 1);
						}
					}
#endif
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
					uint32_t _pw_snap = t->p_w;   /* detect restart/reset mid-prime */
					uint32_t _pw   = (g_consume_pos / SAMP_PER_BLK) * SAMP_PER_BLK;
					uint32_t _gb   = t->len_blocks ? t->len_blocks
					               : (g_loop_blocks ? g_loop_blocks : 1u);
					uint32_t _want = (RING_SAMPLES / 2u) + 16u * SAMP_PER_BLK;
					if (_want > RING_SAMPLES) _want = RING_SAMPLES - SAMP_PER_BLK;
					for (uint32_t _got = 0; _got < _want; ) {
						uint32_t _pwb = _pw / SAMP_PER_BLK;
						uint32_t _lb  = ((_pwb % _gb) + _gb - (t->start_blk % _gb)) % _gb;
						uint32_t _n   = 32u;
						if (_n > (RING_SAMPLES / SAMP_PER_BLK) - 1u) _n = (RING_SAMPLES / SAMP_PER_BLK) - 1u;
						if (_lb + _n > _gb) _n = _gb - _lb;
						/* SILENCE PAD (see PASS 2): [content, _gb) is synthesised
						 * zeros, never read from flash. */
						uint32_t _content = t->content_blocks ? t->content_blocks : _gb;
						bool _psil = (_lb >= _content);
						if (!_psil && _lb + _n > _content) _n = _content - _lb;
						if (_psil) {
							memset(batchbuf, 0, (size_t)_n * EMMC_BLOCK_SIZE);
						} else if (!emmc_read_blocks(trk_blk(slot, (uint32_t)i) + _lb, batchbuf, _n)) {
							break;
						}
						/* DECODE the prime burst (_n blocks) into the play ring. */
						uint32_t _ntot = _n * SAMP_PER_BLK;
						codec_unpack(t->pring, RING_MASK, _pw & RING_MASK,
						             batchbuf, _n);
						_pw  += _ntot;
						_got += _ntot;
					}
					if (t->p_w == _pw_snap)
						t->p_w = _pw;   /* publish: ring now has ~170 ms cushion */
					/* else a restart/song-switch reset p_w mid-prime: keep the
					 * reset value (int16 ring zeros = silence); PASS 2 refills
					 * from the new playhead. */
					t->state = TS_PLAY;    /* publish AFTER priming -> no entry starve */
					work = true;
				}
			}
		}

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
			static uint32_t rr;
			bool more = true;
			while (more && g_slot == slot) {
				more = false;
			rr = (rr + 1u) & 3u;
			cpos = g_consume_pos;    /* fresh playhead for this round */
			for (int k = 0; k < NTRK; k++) {
				int i = (int)((rr + (uint32_t)k) & 3u);
				if (g_slot != slot) break;
				struct looptrk *t = &trk[i];
				if (t->state != TS_PLAY) continue;
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
				if (avail < -(int32_t)SAMP_PER_BLK ||
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
					if (a2 < -(int32_t)SAMP_PER_BLK ||
					    a2 > (int32_t)RING_SAMPLES) {
						uint32_t anchor = (cnow / SAMP_PER_BLK) * SAMP_PER_BLK;
						t->p_w = anchor;   /* audio thread sees starved either way */
						a2 = (int32_t)(anchor - cnow);
						g_p2snap[i]++;
					}
					avail = a2;
				}
				if (avail > (int32_t)(RING_SAMPLES - 8u * SAMP_PER_BLK))
					continue;          /* ring PINNED ~full (<=8 blocks of headroom):
					                    * the cushion is real at stall onset instead of
					                    * sawtoothing between half and full. 8 blocks
					                    * (not 4) so steady-state top-ups are >=7-block
					                    * bursts, not 3-block CMD18 spam. */
				/* SEGMENT: this track loops at ITS OWN length (a whole multiple
				 * of the base), not the shared g_loop_blocks. */
				uint32_t gb = t->len_blocks ? t->len_blocks
					    : (g_loop_blocks ? g_loop_blocks : 1u);
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
					/* phase-anchored loop position: (pw_block - start_blk)
					 * mod gb, safe when pw_block < start_blk (restart). */
					uint32_t pwb = pw / SAMP_PER_BLK;
					uint32_t loop_blk = ((pwb % gb) + gb - (t->start_blk % gb)) % gb;
					uint32_t n = budget;
					if (n > (RING_SAMPLES / SAMP_PER_BLK) - 1u) n = (RING_SAMPLES / SAMP_PER_BLK) - 1u;
					/* VARIABLE TOP-UP: fill to ~full (keep a 1-block
					 * producer/consumer gap) so rings park at ~100%. */
					{
						int32_t av = (int32_t)(pw - cpos);
						int32_t _room = (int32_t)(RING_SAMPLES - SAMP_PER_BLK) - av;
						uint32_t _rb = _room > 0 ? (uint32_t)_room / SAMP_PER_BLK : 0u;
						if (n > _rb) n = _rb;
					}
					if (!n) break;
					if (loop_blk + n > gb) n = gb - loop_blk;  /* stop at the wrap */
					/* SILENCE PAD: the loop length can exceed the recorded
					 * content (fixed mode). [content, gb) was never written
					 * to flash — read it as synthesised zeros instead of
					 * stale flash data. NOTE: memset(0) is true silence ONLY
					 * for PCM. A compressed codec (u-law/ADPCM) would need
					 * its own encoded-silence bytes here, not zeros (u-law
					 * 0x00 decodes to a loud tone). */
					uint32_t content = t->content_blocks ? t->content_blocks : gb;
					bool _sil = (loop_blk >= content);
					if (!_sil && loop_blk + n > content) n = content - loop_blk;
					uint32_t blkno = trk_blk(slot, (uint32_t)i) + loop_blk;
					bool _rok;
					if (_sil) { memset(batchbuf, 0, (size_t)n * EMMC_BLOCK_SIZE); _rok = true; }
					else      { _rok = emmc_read_blocks(blkno, batchbuf, n); }
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
							    (RRING_SAMPLES - RRING_SAMPLES / 4u))
								_rec_press = true;
						}
						if (_rec_press) round_abort = true;
						break;
					}
					if (t->p_w != pw) { work = true; break; } /* reset raced us */
					/* DECODE: packed flash bytes (n blocks just read) -> play
					 * ring (int16, wraps at RING_MASK, pw is block-aligned).
					 * PCM is memcpy-equivalent. */
					codec_unpack(t->pring, RING_MASK, pw & RING_MASK,
					             batchbuf, n);
					t->p_w = pw + n * SAMP_PER_BLK;
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
					    16u * SAMP_PER_BLK) page_ready = true;
				}
				if (page_ready) {
					g_p2yield++;
					more = false;
					break;
				}
			}
			}
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
			static int64_t flush_last;
			int64_t nowms = k_uptime_get();
			if (quiet && nowms - flush_last >= 50) {
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
		 * silence if the host isn't streaming) and mixes the 4 tracks on top.
		 * DWT-timed: worst-case exec must stay far below the 5.33 ms block
		 * budget — aus= in the diag definitively exonerates (or convicts)
		 * the CPU path for the crackle. */
		uint32_t _c0 = DWT->CYCCNT;
		looper_audio_block(blk);
		uint32_t _cus = (DWT->CYCCNT - _c0) / 64u;   /* 64 MHz -> us */
		if (_cus > g_audio_us_max) g_audio_us_max = _cus;

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

	/* eMMC streamer: preemptible + below the audio thread so audio always wins. */
	k_thread_create(&streamer_tcb, streamer_stack, K_THREAD_STACK_SIZEOF(streamer_stack),
			streamer_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

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
			 32, UDC_BUF_ALIGN);

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
 * k = -12..+12 (0.5x..2.0x; the BPM clamp bounds the usable range). A
 * double-click jumps the speed to the next exact equal-tempered semitone
 * relative to 1.0x (= 80 BPM) — one musical pitch step instead of forty
 * 1-BPM clicks — and a detuned speed SNAPS ONTO the grid rather than
 * drifting off it. Integer-only; the exact Q16 speed is what the song
 * saves, so semitone speeds survive power-off bit-exact. */
static const uint32_t k_semi_q16[25] = {
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
		for (int k = 0; k < 25; k++)
			if (k_semi_q16[k] > sp + sp / 250u)
				return k_semi_q16[k];
		return k_semi_q16[24];
	}
	for (int k = 24; k >= 0; k--)
		if (k_semi_q16[k] < sp - sp / 250u)
			return k_semi_q16[k];
	return k_semi_q16[0];
}

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
	uint32_t cpos = g_consume_pos;
	int mg[NTRK];
	for (int _i = 0; _i < NTRK; _i++)
		mg[_i] = (int)((int32_t)(trk[_i].p_w - cpos) / (int)(LOOP_RATE / 1000u));
	printk("LOOPER %dHz song=%d %s hp=%d hpin=%d usb=%d chg=%d batt=%d bpm=%d detbpm=%d vol=%d "
	       "trk[%s %s %s %s] rec=%d mut=%u%u%u%u ovr=%u rerr=%u werr=%u marg=[%d %d %d %d]ms stv=[%u %u %u %u] len=[%u %u %u %u] st=[%u %u %u %u] spim=%d cache=%d ckb=%u wbi=%u\n",
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
	       emmc_spim_active() ? 1 : 0, g_cache_on ? 1 : 0, (unsigned)g_cache_kb, (unsigned)emmc_dbg_wr_busy_max);
	{
		/* CPU= per-thread share of the last window, in percent: audio,
		 * streamer, midi, main, everything-else(usb/idle/isr). Answers
		 * WHERE the cycles actually go when refill can't build surplus. */
		static uint64_t l_aud, l_str, l_mid, l_mai, l_all;
		k_thread_runtime_stats_t rs;
		uint64_t aud = 0, str = 0, mid = 0, mai = 0, all = 0;
		if (!k_thread_runtime_stats_get(&audio_tcb, &rs))    aud = rs.execution_cycles;
		if (!k_thread_runtime_stats_get(&streamer_tcb, &rs)) str = rs.execution_cycles;
		if (!k_thread_runtime_stats_get(&midi_tcb, &rs))     mid = rs.execution_cycles;
		if (!k_thread_runtime_stats_get(k_current_get(), &rs)) mai = rs.execution_cycles;
		if (!k_thread_runtime_stats_all_get(&rs))            all = rs.execution_cycles;
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
		printk("EMMC48 wus=%u/%u rus=%u sus=%u bto=%u low=%dms hiw=%ums gl=%u iwf=%u aus=%u rr=%x flt=%x@%x hi=%u,%u uu=%u uo=%u up=%u ufl=%d,%u fb=%d ec=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
		       (unsigned)emmc_dbg_wr_busy_us_max, (unsigned)emmc_dbg_wr_busy_us_peak,
		       (unsigned)emmc_dbg_rd_wait_us_max, (unsigned)emmc_dbg_switch_busy_us_max,
		       (unsigned)emmc_dbg_busy_timeouts, _lw,
		       (unsigned)(g_rec_hiwat / (LOOP_RATE / 1000u)),
		       (unsigned)g_stored_glitch_cnt, (unsigned)g_i2s_wfail_cnt,
		       (unsigned)g_audio_us_max,
		       (unsigned)g_resetreas,
		       (unsigned)g_last_fault_reason, (unsigned)g_last_fault_pc,
		       (unsigned)g_hpi_on, (unsigned)emmc_dbg_hpi_fires,
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
/* Status row = song indicator, 8 songs on 4 LEDs in 2 banks: LED index =
 * song % 4; bank 0 (songs 1-4) shows SOLID — pixel-identical to the original
 * 4-song firmware — and bank 1 (songs 5-8) BLINKS ~2 Hz (250 ms on/off).
 * Pure function of (g_slot, uptime): no state, no blocking, evaluated every
 * ~8 ms control-loop pass. (Same LEDs the power on/off sweep uses.) */
static void show_song_leds(void)
{
	uint32_t slot = g_slot;                 /* volatile: read once */
	uint32_t pos  = slot & 3u;              /* slot % 4 */
	int on = (slot < 4u) || (((k_uptime_get_32() / 250u) & 1u) == 0u);
	for (int i = 0; i < NUM_LEDS; i++)
		((uint32_t)i == pos && on) ? led_on(i) : led_off(i);
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
	tas_page(0x00);
	(void)tas_wr(0x01, 0x01);        /* TAS2505 software reset: every block
	                                  * back to its powered-down default */
	gpio_drive_low(CS42_RST_PORT, CS42_RST_PIN);   /* CS42L42 held in reset */
	emmc_power_down();               /* bus pins released, VCCQ rail off */

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
	k_thread_priority_set(k_current_get(), K_PRIO_PREEMPT(1));

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

	if (device_is_ready(wdt)) {
		wdt_install_timeout(wdt, &(struct wdt_timeout_cfg){
			.window.max = 4000, .callback = wdt_prewarn,
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
	if (!(wake_reas & (POWER_RESETREAS_OFF_Msk | POWER_RESETREAS_DOG_Msk)) &&
	    g_last_fault_reason == 0xFFFFFFFFu) {
		/* (a valid fault breadcrumb also skips standby: the user was
		 * mid-session, and the battery standby path would SYSTEM_OFF and
		 * wipe the very forensics we just preserved) */
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
	int64_t combo_start = -1;   /* FUNCTION+PLAY: when the combo was first seen */
	uint8_t combo_fired = 0;    /* mode already toggled this combo press */
	uint8_t combo_seen  = 0;    /* PLAY was seen at all during this FUNCTION press */
	uint8_t suppress_play = 0;  /* swallow a trailing PLAY held past combo exit */
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
			if (press_start < 0)
				press_start = k_uptime_get();

			/* MODE TOGGLE — FUNCTION + PLAY held together ~0.7 s flips the
			 * fixed/variable loop-length mode. The normal ladder decode below
			 * is skipped while FUNCTION is held, so read PLAY here. PLAY is at
			 * the TOP of the AIN0 ladder (~1823); require >1600 so a Track-4
			 * (~1220) or the 1+4 bootloader combo (~1325) can never be mistaken
			 * for it. FUNCTION is a separate GPIO, so holding it does not shift
			 * the ladder voltage. While the combo is engaged the power-off
			 * countdown/shutdown is suppressed (this gesture must never risk a
			 * power-off), and the FUNCTION-release song-change is suppressed. */
			if (ladder_read(&adc_ladder[LAD_TRACKS]) > 1600) {
				combo_seen = 1;
				if (combo_start < 0) combo_start = k_uptime_get();
				if (!combo_fired &&
				    k_uptime_get() - combo_start >= 700) {
					g_fixed_len ^= 1u;
					combo_fired = 1;
					g_meta.fixed_len = g_fixed_len;  /* persist across power-off */
					g_meta_save_req = 1;
					/* LED feedback: FIXED = all four blink together twice
					 * ("locked"); VARIABLE = a 1->4->1 sweep ("independent"). */
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
				k_msleep(25);
				continue;                /* combo owns the button */
			}
			combo_start = -1;            /* PLAY not held */
			if (combo_seen) {
				/* The combo has been engaged this FUNCTION press: once PLAY
				 * is lifted, do NOTHING further for the rest of the hold —
				 * no power-off countdown, no shutdown (press_start still
				 * dates from the original FUNCTION-down, so the 2.5 s
				 * power-off would otherwise fire). The FUNCTION press is
				 * spent; it ends cleanly on release below. */
				k_msleep(25);
				continue;
			}

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
			if (!combo_seen &&
			    (k_uptime_get() - press_start) < 600) next_slot();   /* short tap -> next song */
			all_off();
			/* If the combo was ended by lifting FUNCTION FIRST while PLAY is
			 * still down, swallow that trailing PLAY until it is released, so
			 * it can't leak into the normal decode as a restart / play-stop. */
			if (combo_seen &&
			    ladder_read(&adc_ladder[LAD_TRACKS]) > 1600) suppress_play = 1;
		}
		press_start = -1;
		combo_start = -1;
		combo_fired = 0;
		combo_seen  = 0;

		/* ---- looper controls + LEDs ---- */
		{
			/* FAILSAFE: Track1+Track4 combo (AIN0 ~1325, between T4 1220 and PLAY
			 * 1823) held ~1.2 s -> reset into the bootloader for reflashing. Checked
			 * BEFORE the normal decode so the combo isn't mistaken for a Track-4 press. */
			int trk_raw = ladder_read(&adc_ladder[LAD_TRACKS]);
			static int64_t combo14_t = -1;     /* when the 1+4 band was first seen */
			enum trk_btn raw;
			/* This DFU check runs BEFORE ctl_flush is consumed below, so clear
			 * the stale 1+4 timestamp here: after a FUNCTION+PLAY mode toggle
			 * (which freezes this block for the whole combo) a PLAY release
			 * sweeping through the 1280-1390 band must not find a >1.2 s-old
			 * combo14_t and reboot to the bootloader mid-performance. */
			if (ctl_flush) combo14_t = -1;
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
			static int64_t press_t[NTRK];        /* when committed first named this track */
			static int64_t tap_deadline[NTRK];   /* >0: a single tap awaiting a possible 2nd */
			static uint8_t armed_press[NTRK];    /* this press already armed a take */
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
				committed = TRK_NONE; cand = TRK_NONE; cand_cnt = 0;
				for (int k = TRK_1; k <= TRK_PLAY; k++) ep_time[k] = 0;
				ep_open = 0; ep_play_held = 0;
				play_t = -1; play_held = 0;
				for (int k = 0; k < NTRK; k++) { tap_deadline[k] = 0; armed_press[k] = 0; }
				if (!(trk_raw >= 0 && trk_raw < 110)) suppress_play = 1;
				raw = TRK_NONE;
			}
			enum trk_btn before = committed;
			if (raw == committed) {
				cand_cnt = 0;
			} else if (raw == cand) {
				if (++cand_cnt >= 3) { committed = raw; cand_cnt = 0; }
			} else {
				cand = raw; cand_cnt = 1;
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
					for (int k = TRK_1; k < (int)committed; k++)
						ep_time[k] = 0;  /* below = up-sweep transit */
				}
				if (committed >= TRK_1 && committed <= TRK_4) { /* PRESS edge */
					int ti = (int)committed;
					press_t[ti] = tnow;
					armed_press[ti] = 0;
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
					if (armed_press[ti]) {
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
						tap_deadline[ti] = 0;   /* 2nd tap -> DELETE */
						g_del_req[ti] = 1;
						trk[ti].muted = 0;
					} else {
						trk[ti].muted = !trk[ti].muted;  /* tap -> mute */
						tap_deadline[ti] = tnow + DTAP_GAP_MS;
					}
				} else if (b == TRK_PLAY) {
					/* PLAY tap -> toggle play/stop. ep_play_held was set
					 * the instant the hold-restart fired (a hold is not a
					 * tap). Ignored while a take is in progress: stopping
					 * would freeze the recording mid-take. */
					if (!ep_play_held && g_rec_track < 0) {
						g_playing = !g_playing;
						if (g_playing) g_midi_start_pending = 1;
						else           g_midi_stop_pending  = 1;
					}
				}
				ep_play_held = 0;
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
				if (!armed_press[ti] && g_rec_track < 0 &&
				    trk[ti].state != TS_DONE &&
				    k_uptime_get() - press_t[ti] >= (empt ? 100 : HOLD_RECORD_MS)) {
					/* g_rec_track < 0: one take at a time — while a latched
					 * take runs, holding ANY track does nothing (no phantom
					 * arm, no forced g_playing). state != TS_DONE: a hold on
					 * a just-auto-finalized take (user trying to stop it)
					 * must not silently arm a latched re-record that would
					 * overwrite the take it is still flushing. */
					armed_press[ti] = 1;
					tap_deadline[ti] = 0;            /* a hold cancels a pending single-tap */
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
					g_restart_req = 1; play_held = 1;        /* hold -> play from start */
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
				int dir = (g_rec_track >= 0) ? 0 :
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
