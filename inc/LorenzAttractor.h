#pragma once
#include "MathHelpers.h"

struct LorenzAttractor {
    float x, y, z;
    float sigma, rho, beta;

    const float X_MIN = -20.0f; const float X_MAX = 20.0f;
    const float Y_MIN = -30.0f; const float Y_MAX = 30.0f;
    const float Z_MIN =  0.0f;  const float Z_MAX = 50.0f;

    LorenzAttractor(float init_x = 0.1f, float init_y = 0.0f, float init_z = 0.0f) {
        x = init_x; y = init_y; z = init_z;
        sigma = 10.0f; rho = 28.0f; beta = 2.6666667f;
    }

    void step(float dt) {
        float dx = sigma * (y - x);
        float dy = x * (rho - z) - y;
        float dz = (x * y) - (beta * z);
        x += dx * dt; y += dy * dt; z += dz * dt;
    }

    void reset(float init_x = 0.1f, float init_y = 0.0f, float init_z = 0.0f) {
        x = init_x; y = init_y; z = init_z;
    }

    float getNormX() const { return MathHelpers::normalize(x, X_MIN, X_MAX); }
    float getNormY() const { return MathHelpers::normalize(y, Y_MIN, Y_MAX); }
    float getNormZ() const { return MathHelpers::normalize(z, Z_MIN, Z_MAX); }
};
