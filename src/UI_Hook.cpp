#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "UI_conditioning.h"
#include "led_cont.h"
#include "led_colors.h"
#include "led_map.h"
#include "chaos_interface.h"
}

extern ChaosModulator chaosManager;

// Mode change flash counter for inner ring indicator
static volatile uint16_t mode_change_flash = 0;

// Breathing counter for freeze indicator
static float freeze_breath_phase = 0.0f;

extern "C" {

extern o_led_cont led_cont;

void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held) {
    if (lfo_type_just_pressed && fine_is_held) {
        current_chaos_mode++;
        if (current_chaos_mode > 2) current_chaos_mode = 0;

        if (current_chaos_mode > 0) chaos_reset_pending = 1;

        mode_change_flash = 500;
    }
}

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
    // Persistent mode indicator
    else if (current_chaos_mode != 0) {
        uint8_t frozen = chaos_is_frozen();

        if (frozen) {
            // Freeze: all inner ring LEDs breathe in mode color
            freeze_breath_phase += 0.02f;
            if (freeze_breath_phase > 6.2832f) freeze_breath_phase -= 6.2832f;
            float breath = freeze_breath_phase / 3.14159f;
            if (breath > 1.0f) breath = 2.0f - breath;
            if (breath < 0.0f) breath = 0.0f;
            float brightness = 0.1f + breath * 0.6f;

            for (int i = 0; i < NUM_LED_INRING; i++) {
                set_rgb_color_brightness(&led_cont.inring[i], mode_color, brightness);
            }
        } else {
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
    uint8_t mode = current_chaos_mode;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float val = (mode == 1) ?
            chaosManager.getLorenzModulation(i) :
            chaosManager.getRosslerModulation(i);
        set_rgb_color_brightness(&led_cont.array[i], mode_color, val);
    }
}

} // extern "C"
