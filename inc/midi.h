/*
 * midi.h - MIDI I/O mode for SWN
 *
 * Adds bidirectional MIDI support using UART5 (RX: PD2, TX: PC12).
 * Features: note-to-pitch with pitch bend, per-voice CC control,
 * 14-bit high-res sphere navigation, velocity-to-VCA, parameter TX echo,
 * MIDI clock sync.
 *
 * Two voice modes:
 *   MIDI_VOICEMODE_OMNI:    Round-robin allocation, all channels accepted
 *   MIDI_VOICEMODE_CHANNEL: Channel-per-voice (ch1→A, ch2→B, ... ch6→F)
 */

#pragma once

#include <stdint.h>

// MIDI Status bytes
#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_CC             0xB0
#define MIDI_PROG_CHANGE    0xC0
#define MIDI_PITCH_BEND     0xE0
#define MIDI_CLOCK          0xF8
#define MIDI_START          0xFA
#define MIDI_CONTINUE       0xFB
#define MIDI_STOP           0xFC

// Voice modes
#define MIDI_VOICEMODE_OMNI     0   // round-robin, omni channel
#define MIDI_VOICEMODE_CHANNEL  1   // ch 1-6 → voice A-F

// --- CC assignments (7-bit, coarse) ---
// Standard CCs:
#define MIDI_CC_BROWSE          1   // Mod wheel → sphere browse
#define MIDI_CC_LATITUDE        2   // → latitude (sphere Z)
#define MIDI_CC_LONGITUDE       3   // → longitude (sphere X)
#define MIDI_CC_DEPTH           4   // → depth (sphere Y)
#define MIDI_CC_WTSPREAD        5   // → wavetable spread
#define MIDI_CC_TRANSPOSE       6   // → transpose offset
#define MIDI_CC_CHORD           7   // Volume → chord selection
#define MIDI_CC_LFO_SPEED       14  // → LFO speed
#define MIDI_CC_LFO_SHAPE       15  // → LFO shape
#define MIDI_CC_DISPERSION      17  // → dispersion amount
#define MIDI_CC_DISPPAT         18  // → dispersion pattern

// --- CC assignments (14-bit, MSB + LSB) ---
// MSB = CC number, LSB = CC number + 32
// These provide 16384 steps for smooth sphere morphing
#define MIDI_CC_LATITUDE_14     2   // MSB; LSB = 34
#define MIDI_CC_LONGITUDE_14    3   // MSB; LSB = 35
#define MIDI_CC_DEPTH_14        4   // MSB; LSB = 36
#define MIDI_CC_14BIT_LSB_OFFSET 32 // LSB CC = MSB CC + 32

// Pitch bend range in semitones (each direction)
#define MIDI_PITCHBEND_RANGE    2

#define MIDI_CLOCKS_PER_BEAT    24
#define MIDI_TX_RATE_MS         20  // ~50Hz parameter echo rate

#define MIDI_MAX_VOICES         6

typedef struct {
	uint8_t  enabled;
	uint8_t  voice_mode;            // MIDI_VOICEMODE_OMNI or MIDI_VOICEMODE_CHANNEL

	// Per-voice state
	uint8_t  last_note[MIDI_MAX_VOICES];
	uint8_t  last_velocity[MIDI_MAX_VOICES];
	uint8_t  voice_active[MIDI_MAX_VOICES];
	int16_t  pitch_bend[MIDI_MAX_VOICES];   // -8192..+8191 (14-bit centered)
	uint8_t  next_voice;                     // round-robin index (omni mode)

	// Per-voice sphere navigation (channel mode only)
	float    voice_nav[3][MIDI_MAX_VOICES];  // lat/lon/depth per voice

	// 14-bit CC accumulators (MSB stored, waiting for LSB)
	uint8_t  cc_msb[MIDI_MAX_VOICES][128];

	// RX parser state
	uint8_t  rx_status;
	uint8_t  rx_data[2];
	uint8_t  rx_count;
	uint8_t  rx_expected;

	// Clock sync
	uint32_t clock_count;
	uint32_t clock_last_tick;
	float    clock_bpm;
	uint8_t  clock_running;

	// TX delta tracking
	uint8_t  tx_cc_last[6][128];     // per-channel last sent CC
	uint32_t tx_last_time;
} MidiState;

void    midi_init(void);
void    midi_enable(void);
void    midi_disable(void);
uint8_t midi_is_enabled(void);
void    midi_set_voice_mode(uint8_t mode);
uint8_t midi_get_voice_mode(void);

void    midi_process_byte(uint8_t byte);
void    midi_tx_param_state(void);

// Called from update_pitch(): returns voct multiplier, incorporates pitch bend
float   midi_get_voct_override(uint8_t chan, uint8_t *active);

// Called from read_level_and_pan(): returns velocity-scaled level (0-4095)
float   midi_get_velocity_level(uint8_t chan, uint8_t *active);

// Called from calc_wt_pos(): returns per-voice nav offset for a dimension
float   midi_get_voice_nav(uint8_t chan, uint8_t dim);

// Clock
uint8_t midi_clock_is_active(void);
float   midi_get_clock_bpm(void);
