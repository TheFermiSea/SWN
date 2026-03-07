#include "ChaosModulator.h"

extern "C" {
#include "globals.h"
#include "hardware_controls.h"
#include "analog_conditioning.h"
#include "adc_interface.h"
#include "params_lfo.h"
#include "envout_pwm.h"
}

ChaosModulator chaosManager;
extern volatile uint8_t current_chaos_mode;

extern "C" {

extern o_lfos lfos;
extern o_analog analog[NUM_ANALOG_ELEMENTS];

// Called from params_lfo.c
uint8_t process_chaos_lfos(void) {
    if (current_chaos_mode == 0) return 0; // Let factory firmware run

    // 1. Read conditioned LFO CV jack input as speed control (0.0 - 1.0)
    float speed_knob_val = (float)analog[LFO_CV].bracketed_val / 4095.0f;

    // 2. Step attractors
    chaosManager.processBlock(speed_knob_val);

    // 3. Write chaos modulation into LFO preload buffer
    //    update_envout_pwm() will process preload into envout_pwm and out_lpf,
    //    then write to hardware PWM registers
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float mod_val = (current_chaos_mode == 1) ?
                        chaosManager.getLorenzModulation(i) :
                        chaosManager.getRosslerModulation(i);

        lfos.preload[i] = mod_val * (float)PWM_MAX;
    }

    return 1; // Chaos handled the LFO frame
}

} // extern "C"
