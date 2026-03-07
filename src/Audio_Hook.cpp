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
#include "chaos_interface.h"
}

// Compile-time check: chaos channel mapping assumes NUM_CHANNELS == NUM_SWN_CHANNELS
static_assert(NUM_CHANNELS == NUM_SWN_CHANNELS,
    "NUM_CHANNELS and NUM_SWN_CHANNELS must match for chaos modulation routing");

ChaosModulator chaosManager;
volatile uint8_t current_chaos_mode = 0;
volatile uint8_t chaos_reset_pending = 0;
volatile uint8_t chaos_frozen = 0;

// Encoder-controlled parameters (midpoint defaults)
static float chaos_speed_enc = 0.5f;
static float chaos_character = 0.5f;
static float chaos_spread = 0.0f;
static float chaos_gain = 1.0f;

// Fade-in state for smooth transitions
static float chaos_xfade = 0.0f;

// Clock sync state
static uint8_t prev_clk_state = 0;

// Speed display notification flag (read by UI_Hook for outer ring display)
static volatile uint16_t chaos_speed_display_timer = 0;

static void adjust_param(float *param, int16_t encoder_turn, uint8_t fine,
                          float inc_fine, float inc_coarse) {
    float inc = fine ? inc_fine : inc_coarse;
    *param += encoder_turn * inc;
    if (*param < 0.0f) *param = 0.0f;
    if (*param > 1.0f) *param = 1.0f;
}

extern "C" {

extern o_lfos lfos;
extern o_analog analog[NUM_ANALOG_ELEMENTS];
extern enum UI_Modes ui_mode;

void chaos_adjust_speed(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_speed_enc, encoder_turn, fine, 0.01f, 0.05f);
    chaos_speed_display_timer = 700;
}

void chaos_adjust_character(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_character, encoder_turn, fine, 0.005f, 0.03f);
    chaosManager.setCharacter(chaos_character);
}

void chaos_adjust_spread(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_spread, encoder_turn, fine, 0.01f, 0.05f);
}

void chaos_adjust_gain(int16_t encoder_turn, uint8_t fine) {
    adjust_param(&chaos_gain, encoder_turn, fine, 0.01f, 0.05f);
}

float chaos_get_speed(void) { return chaos_speed_enc; }
uint16_t chaos_get_speed_display_timer(void) { return chaos_speed_display_timer; }
void chaos_decrement_speed_display_timer(void) { if (chaos_speed_display_timer > 0) chaos_speed_display_timer--; }
uint8_t chaos_is_frozen(void) { return chaos_frozen; }

uint8_t process_chaos_lfos(void) {
    uint8_t chaos_active = (current_chaos_mode != 0) && !UIMODE_IS_WT_RECORDING_EDITING(ui_mode);

    // Manage fade-in/fade-out for smooth transitions
    if (chaos_active && chaos_xfade < 1.0f) {
        chaos_xfade += 0.01f;
        if (chaos_xfade > 1.0f) chaos_xfade = 1.0f;
    }
    if (!chaos_active && chaos_xfade > 0.0f) {
        chaos_xfade -= 0.01f;
        if (chaos_xfade < 0.0f) chaos_xfade = 0.0f;
    }

    // Fully faded out — let factory firmware run
    if (chaos_xfade <= 0.0f) return 0;

    // Handle pending reset from UI thread (ISR-safe)
    if (chaos_reset_pending) {
        chaosManager.resetAll();
        chaos_reset_pending = 0;
    }

    // Clock-synced reset: reset attractors on rising edge of external clock
    uint8_t clk_now = CLK_IN();
    if (clk_now && !prev_clk_state && jack_plugged(CLK_SENSE)) {
        chaosManager.resetAll();
    }
    prev_clk_state = clk_now;

    // Step attractors (unless frozen)
    if (!chaos_frozen) {
        // Combine encoder speed + CV jack input
        float speed_cv = (float)analog[LFO_CV].bracketed_val / 4095.0f;
        float combined_speed = chaos_speed_enc + speed_cv;
        if (combined_speed > 1.0f) combined_speed = 1.0f;

        chaosManager.processBlock(combined_speed, chaos_spread);
    }

    // Write chaos modulation into LFO preload buffer
    uint8_t mode = current_chaos_mode;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (lfos.muted[i]) {
            lfos.preload[i] = 0;
            continue;
        }

        float mod_val = (mode == 1) ?
                        chaosManager.getLorenzModulation(i) :
                        chaosManager.getRosslerModulation(i);

        float chaos_val = mod_val * chaos_gain * (float)PWM_MAX;

        if (chaos_xfade < 1.0f) {
            chaos_val *= chaos_xfade;
        }

        lfos.preload[i] = chaos_val;
    }

    return 1;
}

} // extern "C"
