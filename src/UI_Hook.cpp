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

// Breathing counter for freeze indicator
static float freeze_breath_phase = 0.0f;

extern "C" {

extern o_led_cont led_cont;

// Getters from Audio_Hook.cpp
extern float chaos_get_speed(void);
extern uint16_t chaos_get_speed_display_timer(void);
extern void chaos_decrement_speed_display_timer(void);
extern uint8_t chaos_is_frozen(void);

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

    uint8_t mode_color = (current_chaos_mode == 1) ? ledc_PURPLE : ledc_GOLD;

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
    // Persistent mode indicator: dim glow on inner ring LED 0
    else if (current_chaos_mode != 0) {
        uint8_t frozen = chaos_is_frozen();

        if (frozen) {
            // Freeze: all inner ring LEDs breathe in mode color
            freeze_breath_phase += 0.02f;
            if (freeze_breath_phase > 6.2832f) freeze_breath_phase -= 6.2832f;
            // Simple sine approximation: use triangle wave for breathing
            float breath = freeze_breath_phase / 3.14159f;
            if (breath > 1.0f) breath = 2.0f - breath;
            if (breath < 0.0f) breath = 0.0f;
            float brightness = 0.1f + breath * 0.6f;

            for (int i = 0; i < NUM_LED_INRING; i++) {
                set_rgb_color_brightness(&led_cont.inring[i], mode_color, brightness);
            }
        } else {
            // Normal: single dim LED as mode indicator
            set_rgb_color_brightness(&led_cont.inring[0], mode_color, 0.2f);
        }
    }

    if (current_chaos_mode == 0) return;

    // Outer ring speed indicator (brief bar graph after encoder turn)
    uint16_t speed_timer = chaos_get_speed_display_timer();
    if (speed_timer > 0) {
        chaos_decrement_speed_display_timer();
        float speed = chaos_get_speed();
        int num_leds = (int)(speed * (float)NUM_LED_OUTRING);
        if (num_leds > NUM_LED_OUTRING) num_leds = NUM_LED_OUTRING;

        for (int i = 0; i < NUM_LED_OUTRING; i++) {
            if (i < num_leds)
                set_rgb_color_brightness(&led_cont.outring[i], mode_color, 0.7f);
            else {
                set_rgb_color(&led_cont.outring[i], ledc_OFF);
                led_cont.outring[i].brightness = 0;
            }
        }
    }

    // Override array LEDs with chaos modulation values
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float val;

        if (current_chaos_mode == 1) {
            val = chaosManager.getLorenzModulation(i);
        } else {
            val = chaosManager.getRosslerModulation(i);
        }
        set_rgb_color_brightness(&led_cont.array[i], mode_color, val);
    }
}

} // extern "C"
