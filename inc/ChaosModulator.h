#pragma once
#include <stdint.h>
#include "LorenzAttractor.h"
#include "RosslerAttractor.h"
#include "globals.h"
#include "chaos_interface.h"

class ChaosModulator {
private:
    LorenzAttractor lorenz[2];
    RosslerAttractor rossler[2];
    float channel_mod[NUM_CHANNELS];

public:
    ChaosModulator() { resetAll(); }

    void processBlock(uint8_t mode, float speed_cv, float spread) {
        float dt = 0.0001f + (speed_cv * 0.02f);
        float dt2 = dt * (1.0f + spread);

        if (mode == CHAOS_LORENZ) {
            lorenz[0].step(dt);
            lorenz[1].step(dt2);
            channel_mod[0] = lorenz[0].getNormX();
            channel_mod[1] = lorenz[0].getNormY();
            channel_mod[2] = lorenz[0].getNormZ();
            channel_mod[3] = lorenz[1].getNormX();
            channel_mod[4] = lorenz[1].getNormY();
            channel_mod[5] = lorenz[1].getNormZ();
        } else {
            rossler[0].step(dt);
            rossler[1].step(dt2);
            channel_mod[0] = rossler[0].getNormX();
            channel_mod[1] = rossler[0].getNormY();
            channel_mod[2] = rossler[0].getNormZ();
            channel_mod[3] = rossler[1].getNormX();
            channel_mod[4] = rossler[1].getNormY();
            channel_mod[5] = rossler[1].getNormZ();
        }
    }

    void resetAll() {
        for (int i = 0; i < 2; i++) {
            lorenz[i].reset(0.1f + (float)i * 0.05f, 0.0f, 0.0f);
            rossler[i].reset(0.1f + (float)i * 0.05f, 0.0f, 0.0f);
        }
    }

    void setCharacter(uint8_t mode, float normalized_character) {
        for (int i = 0; i < 2; i++) {
            if (mode == CHAOS_LORENZ)
                lorenz[i].setCharacter(normalized_character);
            else
                rossler[i].setCharacter(normalized_character);
        }
    }

    float getModulation(int channel) {
        if (channel < 0 || channel >= NUM_CHANNELS) return 0.0f;
        return channel_mod[channel];
    }
};
