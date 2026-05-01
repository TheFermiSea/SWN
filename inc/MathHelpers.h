#pragma once

namespace MathHelpers {
    inline float clamp(float value, float min, float max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    inline float normalize(float value, float min, float max) {
        if (max <= min) return 0.0f;
        float mapped = (value - min) / (max - min);
        return clamp(mapped, 0.0f, 1.0f);
    }
}
