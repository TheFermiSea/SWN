#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "UI_conditioning.h"
#include "led_cont.h"
#include "led_colors.h"
}

// Global state: 0 = Factory, 1 = Lorenz, 2 = Rossler
volatile uint8_t current_chaos_mode = 0;
extern ChaosModulator chaosManager;

extern "C" {

extern o_led_cont led_cont;

// Called from ui_modes.c
void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held) {
    if (lfo_type_just_pressed && fine_is_held) {
        current_chaos_mode++;
        if (current_chaos_mode > 2) current_chaos_mode = 0;

        if (current_chaos_mode > 0) chaosManager.resetAll();
    }
}

// Called from led_cont.c
void override_chaos_leds(void) {
    if (current_chaos_mode == 0) return;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        float val;
        uint8_t color;

        if (current_chaos_mode == 1) {
            val = chaosManager.getLorenzModulation(i);
            color = ledc_PURPLE;
        } else {
            val = chaosManager.getRosslerModulation(i);
            color = ledc_GOLD;
        }
        set_rgb_color_brightness(&led_cont.array[i], color, val);
    }
}

} // extern "C"
