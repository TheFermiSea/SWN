/*
 * midi.c - MIDI I/O engine for SWN
 *
 * Handles MIDI RX parsing (notes, CCs, clock), parameter mapping,
 * and TX echo of module state.
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

// MIDI note 69 = A4 = 440Hz
// voct multiplier = 2^((note - 69) / 12) * (440 / F_BASE_FREQ)
// F_BASE_FREQ = 16.35 (C0)
// So: freq = F_BASE_FREQ * voct * transposition * octave
// We want voct to produce the right pitch when oct=INIT_OCT (3), transposition=1.0
// At oct=3: freq = F_BASE_FREQ * voct * 8
// For MIDI note 60 (C4, ~261.63Hz): voct = 261.63 / (16.35 * 8) = 2.0
// General: voct = 2^((note - 36) / 12.0) where 36 = C3 (since oct=3 adds 3 octaves)
// Actually: freq = F_BASE_FREQ * transposition * voct * 2^oct
// With oct=INIT_OCT=3, transposition=1.0:
//   freq = 16.35 * voct * 8
//   For note 60 (C4=261.63): voct = 261.63 / (16.35*8) = 2.0
//   For note N: freq_N = 440 * 2^((N-69)/12)
//     voct = freq_N / (16.35 * 8) = 440*2^((N-69)/12) / 130.8
//     = (440/130.8) * 2^((N-69)/12)
//     = 3.3639... * 2^((N-69)/12)
// Simplify: voct = 2^((N - 36) / 12.0)  since 16.35*2^3 = 130.8 ~ C3

static float midi_note_to_voct(uint8_t note)
{
	return powf(2.0f, ((float)note - 36.0f) / 12.0f);
}

static uint8_t midi_channel_match(uint8_t status)
{
	if (midi.midi_channel == 0)
		return 1; // omni
	return ((status & 0x0F) + 1) == midi.midi_channel;
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

static void midi_handle_note_on(uint8_t note, uint8_t velocity)
{
	if (velocity == 0) {
		// Note on with velocity 0 = note off
		for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
			if (midi.voice_active[i] && midi.last_note[i] == note) {
				midi.voice_active[i] = 0;
				midi.last_velocity[i] = 0;
				break;
			}
		}
		return;
	}

	// Find a free voice, or steal the next round-robin slot
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

static void midi_handle_note_off(uint8_t note)
{
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		if (midi.voice_active[i] && midi.last_note[i] == note) {
			midi.voice_active[i] = 0;
			midi.last_velocity[i] = 0;
			break;
		}
	}
}

static void midi_handle_cc(uint8_t cc_num, uint8_t value)
{
	float scaled;

	switch (cc_num) {
	case MIDI_CC_LATITUDE:
		scaled = (float)value / 127.0f * 2.0f; // 0..2 range matching WT_DIM_SIZE-1
		for (uint8_t i = 0; i < NUM_CHANNELS; i++)
			params.wt_nav_cv[0] = scaled;
		break;

	case MIDI_CC_LONGITUDE:
		scaled = (float)value / 127.0f * 2.0f;
		params.wt_nav_cv[1] = scaled;
		break;

	case MIDI_CC_DEPTH:
		scaled = (float)value / 127.0f * 2.0f;
		params.wt_nav_cv[2] = scaled;
		break;

	case MIDI_CC_BROWSE:
		params.wt_browse_step_pos_cv = (float)value / 127.0f;
		break;

	case MIDI_CC_TRANSPOSE:
		// 0-127 mapped to -63..+64 semitones
		params.transpose_cv = powf(1.05946309436f, (float)(value - 64));
		break;

	case MIDI_CC_WTSPREAD:
		params.wtsel_spread_cv = value;
		break;

	case MIDI_CC_CHORD:
		params.spread_cv = value * 2; // 0-254 range
		break;

	default:
		break;
	}
}

static void midi_handle_clock(void)
{
	uint32_t now = HAL_GetTick();

	if (midi.clock_count > 0 && midi.clock_last_tick > 0) {
		uint32_t elapsed = now - midi.clock_last_tick;
		if (elapsed > 0) {
			// BPM = 60000 / (elapsed_ms * 24)
			// But smooth it: use running average over 24 clocks
			float instant_bpm = 60000.0f / ((float)elapsed * (float)MIDI_CLOCKS_PER_BEAT);
			midi.clock_bpm = midi.clock_bpm * 0.9f + instant_bpm * 0.1f;
		}
	}

	midi.clock_last_tick = now;
	midi.clock_count++;
}

static void midi_dispatch_message(void)
{
	uint8_t type = midi.rx_status & 0xF0;

	if (type != 0xF0 && !midi_channel_match(midi.rx_status))
		return;

	switch (type) {
	case MIDI_NOTE_ON:
		midi_handle_note_on(midi.rx_data[0], midi.rx_data[1]);
		break;

	case MIDI_NOTE_OFF:
		midi_handle_note_off(midi.rx_data[0]);
		break;

	case MIDI_CC:
		midi_handle_cc(midi.rx_data[0], midi.rx_data[1]);
		break;

	default:
		break;
	}
}

// --- Public API ---

void midi_init(void)
{
	midi.enabled = 0;
	midi.midi_channel = 0; // omni
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
	}

	for (uint8_t i = 0; i < 128; i++)
		midi.tx_cc_last[i] = 0xFF; // force first send
}

void midi_enable(void)
{
	midi.enabled = 1;
	// Reset voice state on enable
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++) {
		midi.voice_active[i] = 0;
		midi.last_note[i] = 0;
		midi.last_velocity[i] = 0;
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
	// Release all voices
	for (uint8_t i = 0; i < MIDI_MAX_VOICES; i++)
		midi.voice_active[i] = 0;
}

uint8_t midi_is_enabled(void)
{
	return midi.enabled;
}

void midi_process_byte(uint8_t byte)
{
	if (!midi.enabled)
		return;

	// System real-time messages (single byte, can appear anywhere)
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
		return; // real-time bytes don't affect running status
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
		return; // no valid status, ignore

	midi.rx_data[midi.rx_count++] = byte;

	if (midi.rx_count >= midi.rx_expected) {
		midi_dispatch_message();
		midi.rx_count = 0; // ready for running status
	}
}

float midi_get_voct_override(uint8_t chan, uint8_t *active)
{
	if (!midi.enabled || chan >= MIDI_MAX_VOICES || !midi.voice_active[chan]) {
		*active = 0;
		return 1.0f;
	}

	*active = 1;
	return midi_note_to_voct(midi.last_note[chan]);
}

uint8_t midi_clock_is_active(void)
{
	return midi.enabled && midi.clock_running;
}

float midi_get_clock_bpm(void)
{
	return midi.clock_bpm;
}

static void midi_send_byte(uint8_t byte)
{
	tx_buf[0] = byte;
	HAL_UART_Transmit(midiUART, tx_buf, 1, 2);
}

static void midi_send_cc_msg(uint8_t channel, uint8_t cc_num, uint8_t value)
{
	if (midi.tx_cc_last[cc_num] == value)
		return; // no change, skip

	midi.tx_cc_last[cc_num] = value;

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

	uint8_t ch = (midi.midi_channel > 0) ? (midi.midi_channel - 1) : 0;

	// Echo current navigation state as CCs
	uint8_t lat_val = (uint8_t)(params.wt_nav_cv[0] / 2.0f * 127.0f);
	uint8_t lon_val = (uint8_t)(params.wt_nav_cv[1] / 2.0f * 127.0f);
	uint8_t dep_val = (uint8_t)(params.wt_nav_cv[2] / 2.0f * 127.0f);

	if (lat_val > 127) lat_val = 127;
	if (lon_val > 127) lon_val = 127;
	if (dep_val > 127) dep_val = 127;

	midi_send_cc_msg(ch, MIDI_CC_LATITUDE, lat_val);
	midi_send_cc_msg(ch, MIDI_CC_LONGITUDE, lon_val);
	midi_send_cc_msg(ch, MIDI_CC_DEPTH, dep_val);
}
