#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Chaos mode state: 0 = Factory, 1 = Lorenz, 2 = Rossler
extern volatile uint8_t current_chaos_mode;
extern volatile uint8_t chaos_reset_pending;
extern volatile uint8_t chaos_frozen;

// Audio-path functions (called from params_lfo.c)
uint8_t process_chaos_lfos(void);
void chaos_adjust_speed(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_character(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_spread(int16_t encoder_turn, uint8_t fine);
void chaos_adjust_gain(int16_t encoder_turn, uint8_t fine);

// UI getters (called from UI_Hook.cpp LED routines)
float chaos_get_speed(void);
uint16_t chaos_get_speed_display_timer(void);
void chaos_decrement_speed_display_timer(void);
uint8_t chaos_is_frozen(void);

// UI-path functions (called from ui_modes.c and led_cont.c)
void check_chaos_button_combo(uint8_t lfo_type_just_pressed, uint8_t fine_is_held);
void override_chaos_leds(void);

#ifdef __cplusplus
}
#endif
