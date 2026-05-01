#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Chaos mode selection
enum ChaosMode {
    CHAOS_OFF     = 0,
    CHAOS_LORENZ  = 1,
    CHAOS_ROSSLER = 2,
    NUM_CHAOS_MODES
};

// LED timing constants
#define CHAOS_MODE_FLASH_TICKS    500
#define CHAOS_SPEED_DISPLAY_TICKS 700

// Breathing animation constants
#define CHAOS_BREATH_RATE         0.02f
#define CHAOS_BREATH_MIN          0.1f
#define CHAOS_BREATH_RANGE        0.6f
#define CHAOS_INDICATOR_DIM       0.2f
#define CHAOS_SPEED_BAR_BRIGHT    0.7f

extern volatile uint8_t current_chaos_mode;
extern volatile uint8_t chaos_reset_pending;

// Audio-path functions (called from params_lfo.c)
uint8_t process_chaos_lfos(void);
void chaos_adjust_speed(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_character(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_spread(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_gain(int16_t encoder_turn, uint8_t fine);
void chaos_toggle_freeze(void);

// UI getters (called from UI_Hook.cpp LED routines)
float chaos_get_speed(void);
float chaos_get_modulation(uint8_t mode, int channel);
uint16_t chaos_get_speed_display_timer(void);
void chaos_decrement_speed_display_timer(void);
uint8_t chaos_is_frozen(void);

// UI-path functions (called from ui_modes.c and led_cont.c)
void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held);
void override_chaos_leds(void);

#ifdef __cplusplus
}
#endif
