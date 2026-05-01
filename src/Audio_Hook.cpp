#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "analog_conditioning.h"
#include "adc_interface.h"
#include "params_lfo.h"
#include "envout_pwm.h"
#include "ui_modes.h"
#include "gpio_pins.h"
#include "math_util.h"
#include "chaos_interface.h"
}

ChaosModulator chaosManager;
volatile uint8_t current_chaos_mode = CHAOS_OFF;
volatile uint8_t chaos_reset_pending = 0;

// Encoder-controlled parameters (midpoint defaults)
static float chaos_speed_enc = 0.5f;
static float chaos_character = 0.5f;
static float chaos_spread = 0.0f;
static float chaos_gain = 1.0f;

// Freeze state
static volatile uint8_t chaos_frozen = 0;

// Fade-in state for smooth transitions
static float chaos_xfade = 0.0f;

// Clock sync state
static uint8_t prev_clk_state = 0;

// Speed display notification
static volatile uint16_t chaos_speed_display_timer = 0;

static void adjust_param(float *param, int16_t encoder_turn, uint8_t fine,
                          float inc_fine, float inc_coarse) {
    float inc = fine ? inc_fine : inc_coarse;
    *param = _CLAMP_F(*param + encoder_turn * inc, 0.0f, 1.0f);
}

extern "C" {

extern o_lfos lfos;
extern o_analog analog[NUM_ANALOG_ELEMENTS];
extern enum UI_Modes ui_mode;

void chaos_adjust_speed(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_speed_enc, encoder_turn, fine, 0.01f, 0.05f);
    chaos_speed_display_timer = CHAOS_SPEED_DISPLAY_TICKS;
}

void chaos_adjust_character(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_character, encoder_turn, fine, 0.005f, 0.03f);
    chaosManager.setCharacter(current_chaos_mode, chaos_character);
}

void chaos_adjust_spread(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_spread, encoder_turn, fine, 0.01f, 0.05f);
}

void chaos_adjust_gain(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_gain, encoder_turn, fine, 0.01f, 0.05f);
}

void chaos_toggle_freeze(void) {
    chaos_frozen = !chaos_frozen;
}

float chaos_get_speed(void) { return chaos_speed_enc; }

float chaos_get_modulation(uint8_t mode, int channel) {
    (void)mode; // mode already baked into channel_mod by processBlock
    return chaosManager.getModulation(channel);
}

uint16_t chaos_get_speed_display_timer(void) { return chaos_speed_display_timer; }
void chaos_decrement_speed_display_timer(void) { if (chaos_speed_display_timer > 0) chaos_speed_display_timer--; }
uint8_t chaos_is_frozen(void) { return chaos_frozen; }

uint8_t process_chaos_lfos(void) {
    uint8_t mode = current_chaos_mode;
    uint8_t chaos_active = (mode != CHAOS_OFF) && !UIMODE_IS_WT_RECORDING_EDITING(ui_mode);

    // Manage fade-in/fade-out for smooth transitions
    if (chaos_active && chaos_xfade < 1.0f)
        chaos_xfade = _CLAMP_F(chaos_xfade + 0.01f, 0.0f, 1.0f);
    if (!chaos_active && chaos_xfade > 0.0f)
        chaos_xfade = _CLAMP_F(chaos_xfade - 0.01f, 0.0f, 1.0f);

    if (chaos_xfade <= 0.0f) return 0;

    if (chaos_reset_pending) {
        chaosManager.resetAll();
        chaos_reset_pending = 0;
    }

    // Clock-synced reset on rising edge
    uint8_t clk_now = CLK_IN();
    if (clk_now && !prev_clk_state && jack_plugged(CLK_SENSE)) {
        chaosManager.resetAll();
    }
    prev_clk_state = clk_now;

    // Step attractors (unless frozen)
    if (!chaos_frozen) {
        float speed_cv = (float)analog[LFO_CV].bracketed_val / 4095.0f;
        float combined_speed = _CLAMP_F(chaos_speed_enc + speed_cv, 0.0f, 1.0f);

        chaosManager.processBlock(mode, combined_speed, chaos_spread);
    }

    // Write chaos modulation into LFO preload buffer
    float gain_scaled = chaos_gain * chaos_xfade * (float)PWM_MAX;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (lfos.muted[i]) {
            lfos.preload[i] = 0;
            continue;
        }
        lfos.preload[i] = chaosManager.getModulation(i) * gain_scaled;
    }

    return 1;
}

} // extern "C"
