#pragma once
#include "MathHelpers.h"

struct RosslerAttractor {
    float x, y, z;
    float a, b, c;

    const float X_MIN = -15.0f; const float X_MAX = 20.0f;
    const float Y_MIN = -15.0f; const float Y_MAX = 15.0f;
    const float Z_MIN =  0.0f;  const float Z_MAX = 30.0f;

    RosslerAttractor(float init_x = 0.1f, float init_y = 0.0f, float init_z = 0.0f) {
        x = init_x; y = init_y; z = init_z;
        a = 0.2f; b = 0.2f; c = 5.7f;
    }

    void step(float dt) {
        float dx = -y - z;
        float dy = x + (a * y);
        float dz = b + z * (x - c);
        x += dx * dt; y += dy * dt; z += dz * dt;
        // Prevent divergence to NaN/Inf
        x = MathHelpers::clamp(x, -100.0f, 100.0f);
        y = MathHelpers::clamp(y, -100.0f, 100.0f);
        z = MathHelpers::clamp(z, -100.0f, 100.0f);
    }

    void reset(float init_x = 0.1f, float init_y = 0.0f, float init_z = 0.0f) {
        x = init_x; y = init_y; z = init_z;
    }

    // normalized_character 0.0–1.0 maps c from 2.0 (periodic) to 18.0 (highly chaotic)
    void setCharacter(float normalized_character) {
        c = 2.0f + normalized_character * 16.0f;
    }

    float getNormX() const { return MathHelpers::normalize(x, X_MIN, X_MAX); }
    float getNormY() const { return MathHelpers::normalize(y, Y_MIN, Y_MAX); }
    float getNormZ() const { return MathHelpers::normalize(z, Z_MIN, Z_MAX); }
};
