#pragma once
#include "LorenzAttractor.h"
#include "RosslerAttractor.h"

#define NUM_SWN_CHANNELS 6

class ChaosModulator {
private:
    LorenzAttractor lorenz[2];
    RosslerAttractor rossler[2];
    float channel_lorenz_mod[NUM_SWN_CHANNELS];
    float channel_rossler_mod[NUM_SWN_CHANNELS];

public:
    ChaosModulator() { resetAll(); }

    void processBlock(float speed_cv) {
        float dt = 0.0001f + (speed_cv * 0.02f);

        for(int i=0; i<2; i++) {
            lorenz[i].step(dt);
            rossler[i].step(dt);
        }

        channel_lorenz_mod[0] = lorenz[0].getNormX();
        channel_lorenz_mod[1] = lorenz[0].getNormY();
        channel_lorenz_mod[2] = lorenz[0].getNormZ();
        channel_lorenz_mod[3] = lorenz[1].getNormX();
        channel_lorenz_mod[4] = lorenz[1].getNormY();
        channel_lorenz_mod[5] = lorenz[1].getNormZ();

        channel_rossler_mod[0] = rossler[0].getNormX();
        channel_rossler_mod[1] = rossler[0].getNormY();
        channel_rossler_mod[2] = rossler[0].getNormZ();
        channel_rossler_mod[3] = rossler[1].getNormX();
        channel_rossler_mod[4] = rossler[1].getNormY();
        channel_rossler_mod[5] = rossler[1].getNormZ();
    }

    void resetAll() {
        for(int i=0; i<2; i++) {
            lorenz[i].reset(0.1f + (float)i * 0.05f, 0.0f, 0.0f);
            rossler[i].reset(0.1f + (float)i * 0.05f, 0.0f, 0.0f);
        }
    }

    float getLorenzModulation(int channel) { return channel_lorenz_mod[channel]; }
    float getRosslerModulation(int channel) { return channel_rossler_mod[channel]; }
};
