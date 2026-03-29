/*
 * midi.c - MIDI I/O engine for SWN
 *
 * Bidirectional MIDI with two voice modes:
 *   OMNI:    Round-robin note allocation, shared CC control
 *   CHANNEL: Ch 1-6 → voice A-F, per-voice CC + pitch bend
 *
 * Features:
 *   - Note on/off with velocity → VCA level
 *   - 14-bit pitch bend per voice (±2 semitones, 0.024 cent resolution)
 *   - 7-bit and 14-bit CC mapping to sphere navigation + parameters
 *   - Per-voice wavetable position in channel mode
 *   - MIDI clock → LFO BPM sync
 *   - TX echo of parameter state at ~50Hz with delta tracking
 */

#include "midi.h"
#include "drivers/uart_driver.h"
#include "globals.h"
#include "params_update.h"
#include <stm32f7xx.h>
#include <math.h>

extern o_params params;
extern o_calc_params calc_params;
extern UART_HandleTypeDef *midiUART;

static MidiState midi;
static uint8_t tx_buf[3];

// --- Pitch conversion ---
// freq = F_BASE_FREQ * transposition * voct * 2^oct
// With oct=INIT_OCT(3): freq = 16.35 * voct * 8 = 130.8 * voct
// For MIDI note N: freq = 440 * 2^((N-69)/12)
// So voct = 2^((N - 36) / 12.0)
// With pitch bend (±MIDI_PITCHBEND_RANGE semitones):
//   voct = 2^((N - 36 + bend_semitones) / 12.0)

static float midi_note_bend_to_voct(uint8_t note, int16_t bend)
{
	float bend_semitones = ((float)bend / 8192.0f) * (float)MIDI_PITCHBEND_RANGE;
	return powf(2.0f, ((float)note - 36.0f + bend_semitones) / 12.0f);
}

// --- Voice management ---

static uint8_t voice_for_channel(uint8_t midi_ch)
{
	if (midi_ch < MIDI_MAX_VOICES)
		return midi_ch;
	return 0xFF; // invalid
}

static uint8_t midi_data_bytes_for_status(uint8_t status)
{
	switch (status & 0xF0) {
	case MIDI_NOTE_OFF:
	case MIDI_NOTE_ON:
	case MIDI_CC:
	case MIDI_PITCH_BEND:
		return 2;
	case MIDI_PROG_CHANGE:
		return 1;
	default:
		return 0;
	}
}

// --- Note handlers ---

static void midi_handle_note_on_omni(uint8_t note, uint8_t velocity)
{
	if (velocity == 0) {
		for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
			if (midi.voice_active[i] && midi.last_note[i] == note) {
				midi.voice_active[i] = 0;
				midi.last_velocity[i] = 0;
				break;
			}
		}
		return;
	}

	// Find free voice, or steal round-robin
	uint8_t voice = midi.next_voice;
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		if (!midi.voice_active[i]) {
			voice = i;
			break;
		}
	}

	midi.voice_active[voice] = 1;
	midi.last_note[voice] = note;
	midi.last_velocity[voice] = velocity;
	midi.next_voice = (voice + 1) % MIDI_MAX_VOICES;
}

static void midi_handle_note_off_omni(uint8_t note)
{
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		if (midi.voice_active[i] && midi.last_note[i] == note) {
			midi.voice_active[i] = 0;
			midi.last_velocity[i] = 0;
			break;
		}
	}
}

static void midi_handle_note_on_channel(uint8_t midi_ch, uint8_t note, uint8_t velocity)
{
	uint8_t voice = voice_for_channel(midi_ch);
	if (voice == 0xFF) return;

	if (velocity == 0) {
		midi.voice_active[voice] = 0;
		midi.last_velocity[voice] = 0;
		return;
	}

	midi.voice_active[voice] = 1;
	midi.last_note[voice] = note;
	midi.last_velocity[voice] = velocity;
}

static void midi_handle_note_off_channel(uint8_t midi_ch, uint8_t note)
{
	uint8_t voice = voice_for_channel(midi_ch);
	if (voice == 0xFF) return;

	if (midi.last_note[voice] == note) {
		midi.voice_active[voice] = 0;
		midi.last_velocity[voice] = 0;
	}
}

// --- Pitch bend ---

static void midi_handle_pitch_bend(uint8_t midi_ch, uint8_t lsb, uint8_t msb)
{
	int16_t bend = (int16_t)(((uint16_t)msb << 7) | lsb) - 8192;

	if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL) {
		uint8_t voice = voice_for_channel(midi_ch);
		if (voice != 0xFF)
			midi.pitch_bend[voice] = bend;
	} else {
		// Omni: apply to all active voices
		for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++)
			midi.pitch_bend[i] = bend;
	}
}

// --- CC handling ---

static void midi_apply_global_cc(uint8_t cc_num, uint8_t value)
{
	float scaled;

	switch (cc_num) {
	case MIDI_CC_BROWSE:
		params.wt_browse_step_pos_cv = (float)value / 127.0f;
		break;

	case MIDI_CC_TRANSPOSE:
		params.transpose_cv = powf(1.05946309436f, (float)(value - 64));
		break;

	case MIDI_CC_WTSPREAD:
		params.wtsel_spread_cv = value;
		break;

	case MIDI_CC_CHORD:
		params.spread_cv = value * 2;
		break;

	case MIDI_CC_LFO_SPEED:
		// 0-127 → scaled LFO speed (handled by params system)
		break;

	case MIDI_CC_LFO_SHAPE:
		break;

	case MIDI_CC_DISPERSION:
		params.dispersion_cv = (float)value / 127.0f;
		break;

	case MIDI_CC_DISPPAT:
		params.disppatt_cv = (int8_t)(value / 21); // 0-5 patterns
		break;

	default:
		break;
	}
}

static void midi_apply_nav_cc(uint8_t voice, uint8_t cc_num, uint8_t value)
{
	// In channel mode: per-voice sphere navigation
	// In omni mode: global (voice parameter ignored)
	float scaled = (float)value / 127.0f * 2.0f; // 0..2 matching WT_DIM_SIZE-1

	uint8_t dim = 0xFF;
	if (cc_num == MIDI_CC_LATITUDE)       dim = 0;
	else if (cc_num == MIDI_CC_LONGITUDE) dim = 1;
	else if (cc_num == MIDI_CC_DEPTH)     dim = 2;

	if (dim == 0xFF) return;

	if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL && voice < MIDI_MAX_VOICES) {
		midi.voice_nav[dim][voice] = scaled;
	} else {
		params.wt_nav_cv[dim] = scaled;
	}
}

static void midi_apply_nav_cc_14bit(uint8_t voice, uint8_t cc_msb_num, uint16_t value14)
{
	// 14-bit: 0..16383 → 0..2.0 for WT_DIM_SIZE-1
	float scaled = (float)value14 / 16383.0f * 2.0f;

	uint8_t dim = 0xFF;
	if (cc_msb_num == MIDI_CC_LATITUDE_14)       dim = 0;
	else if (cc_msb_num == MIDI_CC_LONGITUDE_14)  dim = 1;
	else if (cc_msb_num == MIDI_CC_DEPTH_14)      dim = 2;

	if (dim == 0xFF) return;

	if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL && voice < MIDI_MAX_VOICES) {
		midi.voice_nav[dim][voice] = scaled;
	} else {
		params.wt_nav_cv[dim] = scaled;
	}
}

static void midi_handle_cc(uint8_t midi_ch, uint8_t cc_num, uint8_t value)
{
	uint8_t voice = voice_for_channel(midi_ch);

	// Check for 14-bit LSB (CCs 32-63 are LSBs for CCs 0-31)
	if (cc_num >= MIDI_CC_14BIT_LSB_OFFSET && cc_num < 64) {
		uint8_t msb_cc = cc_num - MIDI_CC_14BIT_LSB_OFFSET;

		if (msb_cc == MIDI_CC_LATITUDE_14 || msb_cc == MIDI_CC_LONGITUDE_14 || msb_cc == MIDI_CC_DEPTH_14) {
			uint8_t msb_val = (voice < MIDI_MAX_VOICES) ? midi.cc_msb[voice][msb_cc] : midi.cc_msb[0][msb_cc];
			uint16_t val14 = ((uint16_t)msb_val << 7) | value;
			midi_apply_nav_cc_14bit(voice, msb_cc, val14);
			return;
		}
	}

	// Store MSB for potential 14-bit pair
	if (cc_num < 32) {
		uint8_t v = (voice < MIDI_MAX_VOICES) ? voice : 0;
		midi.cc_msb[v][cc_num] = value;
	}

	// Navigation CCs (can be per-voice in channel mode)
	if (cc_num == MIDI_CC_LATITUDE || cc_num == MIDI_CC_LONGITUDE || cc_num == MIDI_CC_DEPTH) {
		midi_apply_nav_cc(voice, cc_num, value);
		return;
	}

	// Global CCs (always shared)
	midi_apply_global_cc(cc_num, value);
}

// --- Clock ---

static void midi_handle_clock(void)
{
	uint32_t now = HAL_GetTick();

	if (midi.clock_count > 0 && midi.clock_last_tick > 0) {
		uint32_t elapsed = now - midi.clock_last_tick;
		if (elapsed > 0) {
			float instant_bpm = 60000.0f / ((float)elapsed * (float)MIDI_CLOCKS_PER_BEAT);
			midi.clock_bpm = midi.clock_bpm * 0.9f + instant_bpm * 0.1f;
		}
	}

	midi.clock_last_tick = now;
	midi.clock_count++;
}

// --- Message dispatch ---

static void midi_dispatch_message(void)
{
	uint8_t type = midi.rx_status & 0xF0;
	uint8_t midi_ch = midi.rx_status & 0x0F;

	// System messages (type >= 0xF0) are always processed
	if (type < 0xF0) {
		// In channel mode: only accept channels 0-5
		if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL) {
			if (midi_ch >= MIDI_MAX_VOICES)
				return;
		}
		// Omni mode accepts all channels
	}

	switch (type) {
	case MIDI_NOTE_ON:
		if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL)
			midi_handle_note_on_channel(midi_ch, midi.rx_data[0], midi.rx_data[1]);
		else
			midi_handle_note_on_omni(midi.rx_data[0], midi.rx_data[1]);
		break;

	case MIDI_NOTE_OFF:
		if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL)
			midi_handle_note_off_channel(midi_ch, midi.rx_data[0]);
		else
			midi_handle_note_off_omni(midi.rx_data[0]);
		break;

	case MIDI_CC:
		midi_handle_cc(midi_ch, midi.rx_data[0], midi.rx_data[1]);
		break;

	case MIDI_PITCH_BEND:
		midi_handle_pitch_bend(midi_ch, midi.rx_data[0], midi.rx_data[1]);
		break;

	default:
		break;
	}
}

// ==================== Public API ====================

void midi_init(void)
{
	midi.enabled = 0;
	midi.voice_mode = MIDI_VOICEMODE_CHANNEL; // default: channel-per-voice
	midi.next_voice = 0;
	midi.rx_status = 0;
	midi.rx_count = 0;
	midi.rx_expected = 0;
	midi.clock_count = 0;
	midi.clock_last_tick = 0;
	midi.clock_bpm = 120.0f;
	midi.clock_running = 0;
	midi.tx_last_time = 0;

	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		midi.voice_active[i] = 0;
		midi.last_note[i] = 0;
		midi.last_velocity[i] = 0;
		midi.pitch_bend[i] = 0;
		for (uint8_t d = 0; d < 3; d++)
			midi.voice_nav[d][i] = 0.0f;
		for (uint8_t c = 0; c < 128; c++)
			midi.cc_msb[i][c] = 0;
	}

	for (uint8_t v = 0; v < 6; v++)
		for (uint8_t c = 0; c < 128; c++)
			midi.tx_cc_last[v][c] = 0xFF;
}

void midi_enable(void)
{
	midi.enabled = 1;
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		midi.voice_active[i] = 0;
		midi.last_note[i] = 0;
		midi.last_velocity[i] = 0;
		midi.pitch_bend[i] = 0;
	}
	midi.next_voice = 0;
	midi.rx_status = 0;
	midi.rx_count = 0;
	midi.clock_count = 0;
	midi.clock_running = 0;
}

void midi_disable(void)
{
	midi.enabled = 0;
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++)
		midi.voice_active[i] = 0;
}

uint8_t midi_is_enabled(void)
{
	return midi.enabled;
}

void midi_set_voice_mode(uint8_t mode)
{
	midi.voice_mode = mode;
	// Reset all voices on mode change
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		midi.voice_active[i] = 0;
		midi.pitch_bend[i] = 0;
	}
	midi.next_voice = 0;
}

uint8_t midi_get_voice_mode(void)
{
	return midi.voice_mode;
}

void midi_process_byte(uint8_t byte)
{
	if (!midi.enabled)
		return;

	// System real-time (single byte, can interleave anywhere)
	if (byte >= 0xF8) {
		switch (byte) {
		case MIDI_CLOCK:
			midi_handle_clock();
			break;
		case MIDI_START:
			midi.clock_running = 1;
			midi.clock_count = 0;
			break;
		case MIDI_CONTINUE:
			midi.clock_running = 1;
			break;
		case MIDI_STOP:
			midi.clock_running = 0;
			break;
		}
		return;
	}

	// Status byte
	if (byte & 0x80) {
		midi.rx_status = byte;
		midi.rx_count = 0;
		midi.rx_expected = midi_data_bytes_for_status(byte);
		return;
	}

	// Data byte
	if (midi.rx_expected == 0)
		return;

	midi.rx_data[midi.rx_count++] = byte;

	if (midi.rx_count >= midi.rx_expected) {
		midi_dispatch_message();
		midi.rx_count = 0; // running status: ready for next message
	}
}

// --- Query functions called from params_update.c ---

float midi_get_voct_override(uint8_t chan, uint8_t *active)
{
	if (!midi.enabled || chan >= MIDI_MAX_VOICES || !midi.voice_active[chan]) {
		*active = 0;
		return 1.0f;
	}

	*active = 1;
	return midi_note_bend_to_voct(midi.last_note[chan], midi.pitch_bend[chan]);
}

float midi_get_velocity_level(uint8_t chan, uint8_t *active)
{
	if (!midi.enabled || chan >= MIDI_MAX_VOICES || !midi.voice_active[chan]) {
		*active = 0;
		return 0.0f;
	}

	*active = 1;
	// Velocity 0-127 → level 0-4095 (matching slider range)
	return (float)midi.last_velocity[chan] / 127.0f * 4095.0f;
}

float midi_get_voice_nav(uint8_t chan, uint8_t dim)
{
	if (!midi.enabled || midi.voice_mode != MIDI_VOICEMODE_CHANNEL)
		return 0.0f;
	if (chan >= MIDI_MAX_VOICES || dim >= 3)
		return 0.0f;
	return midi.voice_nav[dim][chan];
}

uint8_t midi_clock_is_active(void)
{
	return midi.enabled && midi.clock_running;
}

float midi_get_clock_bpm(void)
{
	return midi.clock_bpm;
}

// --- TX echo ---

static void midi_send_cc_on_channel(uint8_t channel, uint8_t cc_num, uint8_t value)
{
	if (channel >= 6 || midi.tx_cc_last[channel][cc_num] == value)
		return;

	midi.tx_cc_last[channel][cc_num] = value;

	tx_buf[0] = MIDI_CC | (channel & 0x0F);
	tx_buf[1] = cc_num & 0x7F;
	tx_buf[2] = value & 0x7F;
	HAL_UART_Transmit(midiUART, tx_buf, 3, 5);
}

void midi_tx_param_state(void)
{
	if (!midi.enabled || midiUART == (UART_HandleTypeDef *)0)
		return;

	uint32_t now = HAL_GetTick();
	if ((now - midi.tx_last_time) < (MIDI_TX_RATE_MS * TICKS_PER_MS))
		return;
	midi.tx_last_time = now;

	if (midi.voice_mode == MIDI_VOICEMODE_CHANNEL) {
		// Per-voice: echo each voice's sphere position on its MIDI channel
		for (uint8_t v = 0; v < MIDI_MAX_VOICES; v++) {
			// wt_pos is the final calculated position (0..WT_DIM_SIZE)
			for (uint8_t d = 0; d < 3; d++) {
				float pos = calc_params.wt_pos[d][v];
				uint8_t val = (uint8_t)(pos / 2.0f * 127.0f);
				if (val > 127) val = 127;
				uint8_t cc = (d == 0) ? MIDI_CC_LATITUDE :
				             (d == 1) ? MIDI_CC_LONGITUDE : MIDI_CC_DEPTH;
				midi_send_cc_on_channel(v, cc, val);
			}
		}
	} else {
		// Omni: echo global nav state on channel 0
		for (uint8_t d = 0; d < 3; d++) {
			float nav = params.wt_nav_cv[d];
			uint8_t val = (uint8_t)(nav / 2.0f * 127.0f);
			if (val > 127) val = 127;
			uint8_t cc = (d == 0) ? MIDI_CC_LATITUDE :
			             (d == 1) ? MIDI_CC_LONGITUDE : MIDI_CC_DEPTH;
			midi_send_cc_on_channel(0, cc, val);
		}
	}
}
