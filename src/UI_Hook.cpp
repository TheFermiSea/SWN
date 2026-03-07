extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "UI_conditioning.h"
#include "led_cont.h"
#include "led_colors.h"
#include "led_map.h"
#include "chaos_interface.h"
}

// Mode change flash counter
static volatile uint16_t mode_change_flash = 0;

// Breathing phase for freeze indicator
static float freeze_breath_phase = 0.0f;

extern "C" {

extern o_led_cont led_cont;

void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held) {
    if (lfo_type_just_pressed && fine_is_held) {
        current_chaos_mode++;
        if (current_chaos_mode >= NUM_CHAOS_MODES) current_chaos_mode = CHAOS_OFF;

        if (current_chaos_mode != CHAOS_OFF) chaos_reset_pending = 1;

        mode_change_flash = CHAOS_MODE_FLASH_TICKS;
    }
}

void override_chaos_leds(void) {
    uint8_t mode = current_chaos_mode;

    if (mode == CHAOS_OFF && mode_change_flash == 0) return;

    uint8_t mode_color = (mode == CHAOS_LORENZ) ? ledc_PURPLE : ledc_GOLD;

    // Flash inner ring on mode change
    if (mode_change_flash > 0) {
        mode_change_flash--;
        uint8_t flash_color = ledc_OFF;
        if (mode == CHAOS_LORENZ)      flash_color = ledc_PURPLE;
        else if (mode == CHAOS_ROSSLER) flash_color = ledc_GOLD;

        for (int i = 0; i < NUM_LED_INRING; i++) {
            set_rgb_color(&led_cont.inring[i], flash_color);
        }
    }
    // Persistent mode indicator
    else if (mode != CHAOS_OFF) {
        if (chaos_is_frozen()) {
            // Breathing animation while frozen
            freeze_breath_phase += CHAOS_BREATH_RATE;
            if (freeze_breath_phase > 6.2832f) freeze_breath_phase -= 6.2832f;
            float breath = freeze_breath_phase * 0.31831f; // 1/pi
            if (breath > 1.0f) breath = 2.0f - breath;
            if (breath < 0.0f) breath = 0.0f;
            float brightness = CHAOS_BREATH_MIN + breath * CHAOS_BREATH_RANGE;

            for (int i = 0; i < NUM_LED_INRING; i++) {
                set_rgb_color_brightness(&led_cont.inring[i], mode_color, brightness);
            }
        } else {
            set_rgb_color_brightness(&led_cont.inring[0], mode_color, CHAOS_INDICATOR_DIM);
        }
    }

    if (mode == CHAOS_OFF) return;

    // Outer ring speed bar (brief display after encoder turn)
    uint16_t speed_timer = chaos_get_speed_display_timer();
    if (speed_timer > 0) {
        chaos_decrement_speed_display_timer();
        float speed = chaos_get_speed();
        int num_leds = (int)(speed * (float)NUM_LED_OUTRING);
        if (num_leds > NUM_LED_OUTRING) num_leds = NUM_LED_OUTRING;

        for (int i = 0; i < NUM_LED_OUTRING; i++) {
            if (i < num_leds)
                set_rgb_color_brightness(&led_cont.outring[i], mode_color, CHAOS_SPEED_BAR_BRIGHT);
            else {
                set_rgb_color(&led_cont.outring[i], ledc_OFF);
                led_cont.outring[i].brightness = 0;
            }
        }
    }

    // Array LEDs track chaos modulation
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float val = chaos_get_modulation(mode, i);
        set_rgb_color_brightness(&led_cont.array[i], mode_color, val);
    }
}

} // extern "C"
