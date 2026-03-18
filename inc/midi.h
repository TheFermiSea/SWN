/*
 * midi.h - MIDI I/O mode for SWN
 *
 * Adds bidirectional MIDI support using UART5 (RX: PD2, TX: PC12).
 * Features: note-to-pitch, CC-to-parameter, parameter TX echo, clock sync.
 */

#pragma once

#include <stdint.h>

// MIDI Status bytes (channel-independent)
#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_CC             0xB0
#define MIDI_PROG_CHANGE    0xC0
#define MIDI_PITCH_BEND     0xE0
#define MIDI_CLOCK          0xF8
#define MIDI_START          0xFA
#define MIDI_CONTINUE       0xFB
#define MIDI_STOP           0xFC

// CC numbers mapped to SWN parameters
#define MIDI_CC_BROWSE      1
#define MIDI_CC_LATITUDE    2
#define MIDI_CC_LONGITUDE   3
#define MIDI_CC_DEPTH       4
#define MIDI_CC_WTSPREAD    5
#define MIDI_CC_TRANSPOSE   6
#define MIDI_CC_CHORD       7
#define MIDI_CC_LFO_SPEED   14
#define MIDI_CC_LFO_SHAPE   15
#define MIDI_CC_LFO_DEPTH   16

#define MIDI_CLOCKS_PER_BEAT 24
#define MIDI_TX_RATE_MS      20   // ~50Hz parameter echo rate

// Note allocation: incoming notes assigned round-robin to 6 channels
#define MIDI_MAX_VOICES      6

typedef struct {
	uint8_t  enabled;
	uint8_t  midi_channel;        // 0 = omni (listen on all), 1-16 = specific
	uint8_t  last_note[MIDI_MAX_VOICES];
	uint8_t  last_velocity[MIDI_MAX_VOICES];
	uint8_t  voice_active[MIDI_MAX_VOICES];
	uint8_t  next_voice;          // round-robin index

	// RX parser state
	uint8_t  rx_status;           // running status byte
	uint8_t  rx_data[2];
	uint8_t  rx_count;
	uint8_t  rx_expected;

	// Clock sync
	uint32_t clock_count;
	uint32_t clock_last_tick;     // HAL_GetTick() at last clock
	float    clock_bpm;
	uint8_t  clock_running;

	// TX delta tracking (last sent CC values)
	uint8_t  tx_cc_last[128];
	uint32_t tx_last_time;
} MidiState;

void    midi_init(void);
void    midi_enable(void);
void    midi_disable(void);
uint8_t midi_is_enabled(void);

void    midi_process_byte(uint8_t byte);
void    midi_tx_param_state(void);

// Called from update_pitch() when MIDI mode is active
float   midi_get_voct_override(uint8_t chan, uint8_t *active);

// Called to check if MIDI clock should override LFO rate
uint8_t midi_clock_is_active(void);
float   midi_get_clock_bpm(void);
