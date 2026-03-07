#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "UI_conditioning.h"
#include "led_cont.h"
#include "led_colors.h"
#include "led_map.h"
}

// Global state: 0 = Factory, 1 = Lorenz, 2 = Rossler
volatile uint8_t current_chaos_mode = 0;
// ISR-safe reset flag: set by UI thread, consumed by ISR
volatile uint8_t chaos_reset_pending = 0;
extern ChaosModulator chaosManager;

// Mode change flash counter for inner ring indicator
static uint16_t mode_change_flash = 0;

extern "C" {

extern o_led_cont led_cont;

// Called from ui_modes.c
void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held) {
    if (lfo_type_just_pressed && fine_is_held) {
        current_chaos_mode++;
        if (current_chaos_mode > 2) current_chaos_mode = 0;

        // Request reset in ISR context to avoid race with processBlock()
        if (current_chaos_mode > 0) chaos_reset_pending = 1;

        // Trigger inner ring flash for mode indication
        mode_change_flash = 500;
    }
}

// Called from led_cont.c
void override_chaos_leds(void) {
    if (current_chaos_mode == 0 && mode_change_flash == 0) return;

    // Flash inner ring on mode change for visual confirmation
    if (mode_change_flash > 0) {
        mode_change_flash--;
        uint8_t flash_color;
        if (current_chaos_mode == 1)
            flash_color = ledc_PURPLE;
        else if (current_chaos_mode == 2)
            flash_color = ledc_GOLD;
        else
            flash_color = ledc_OFF;

        for (int i = 0; i < NUM_LED_INRING; i++) {
            set_rgb_color(&led_cont.inring[i], flash_color);
        }
    }

    // Override array LEDs with chaos modulation values
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
