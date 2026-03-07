#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "analog_conditioning.h"
#include "adc_interface.h"
#include "params_lfo.h"
#include "envout_pwm.h"
#include "ui_modes.h"
}

ChaosModulator chaosManager;
extern volatile uint8_t current_chaos_mode;
extern volatile uint8_t chaos_reset_pending;

// Encoder-controlled speed accumulator (midpoint default)
static float chaos_speed_enc = 0.5f;

// Fade-in state for smooth transitions
static float chaos_xfade = 0.0f;

extern "C" {

extern o_lfos lfos;
extern o_analog analog[NUM_ANALOG_ELEMENTS];
extern enum UI_Modes ui_mode;

// Called from params_lfo.c to adjust chaos speed via encoder
void chaos_adjust_speed(int16_t encoder_turn, uint8_t fine) {
    float inc = fine ? 0.01f : 0.05f;
    chaos_speed_enc += encoder_turn * inc;
    if (chaos_speed_enc < 0.0f) chaos_speed_enc = 0.0f;
    if (chaos_speed_enc > 1.0f) chaos_speed_enc = 1.0f;
}

// Called from params_lfo.c
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

    // Combine encoder speed + CV jack input
    float speed_cv = (float)analog[LFO_CV].bracketed_val / 4095.0f;
    float combined_speed = chaos_speed_enc + speed_cv;
    if (combined_speed > 1.0f) combined_speed = 1.0f;

    // Step attractors
    chaosManager.processBlock(combined_speed);

    // Write chaos modulation into LFO preload buffer
    for (int i = 0; i < NUM_CHANNELS; i++) {
        // Respect mute state
        if (lfos.muted[i]) {
            lfos.preload[i] = 0;
            continue;
        }

        float mod_val = (current_chaos_mode == 1) ?
                        chaosManager.getLorenzModulation(i) :
                        chaosManager.getRosslerModulation(i);

        float chaos_val = mod_val * (float)PWM_MAX;

        // Apply fade for smooth transitions
        if (chaos_xfade < 1.0f) {
            chaos_val *= chaos_xfade;
        }

        lfos.preload[i] = chaos_val;
    }

    return 1; // Chaos handled the LFO frame
}

} // extern "C"
