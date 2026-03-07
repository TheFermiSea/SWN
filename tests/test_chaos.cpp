/*
 * test_chaos.cpp - Host-side unit tests for chaos modulator math
 *
 * Compiles with host g++, no STM32 dependencies.
 * Run: g++ -std=c++11 -O2 -I../inc -o test_chaos test_chaos.cpp && ./test_chaos
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cfloat>

// Include the chaos math headers (pure C++, no hardware deps)
#include "MathHelpers.h"
#include "LorenzAttractor.h"
#include "RosslerAttractor.h"
#include "ChaosModulator.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        tests_run++; \
        printf("  %-50s ", #name); \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", __FILE__, __LINE__, _a, _b, (float)(eps)); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while(0)

static bool is_finite(float v) { return !std::isnan(v) && !std::isinf(v); }

// ============================================================
// MathHelpers tests
// ============================================================

TEST(clamp_within_range) {
    ASSERT_NEAR(MathHelpers::clamp(0.5f, 0.0f, 1.0f), 0.5f, 1e-6f);
}

TEST(clamp_below_min) {
    ASSERT_NEAR(MathHelpers::clamp(-1.0f, 0.0f, 1.0f), 0.0f, 1e-6f);
}

TEST(clamp_above_max) {
    ASSERT_NEAR(MathHelpers::clamp(2.0f, 0.0f, 1.0f), 1.0f, 1e-6f);
}

TEST(normalize_midpoint) {
    ASSERT_NEAR(MathHelpers::normalize(0.0f, -1.0f, 1.0f), 0.5f, 1e-6f);
}

TEST(normalize_at_min) {
    ASSERT_NEAR(MathHelpers::normalize(-20.0f, -20.0f, 20.0f), 0.0f, 1e-6f);
}

TEST(normalize_at_max) {
    ASSERT_NEAR(MathHelpers::normalize(20.0f, -20.0f, 20.0f), 1.0f, 1e-6f);
}

TEST(normalize_below_range_clamps) {
    ASSERT_NEAR(MathHelpers::normalize(-50.0f, -20.0f, 20.0f), 0.0f, 1e-6f);
}

TEST(normalize_above_range_clamps) {
    ASSERT_NEAR(MathHelpers::normalize(50.0f, -20.0f, 20.0f), 1.0f, 1e-6f);
}

TEST(normalize_degenerate_range_no_crash) {
    // min == max should return 0, not NaN/Inf
    float result = MathHelpers::normalize(5.0f, 10.0f, 10.0f);
    ASSERT(is_finite(result));
    ASSERT_NEAR(result, 0.0f, 1e-6f);
}

TEST(normalize_inverted_range_no_crash) {
    // min > max should return 0, not NaN/Inf
    float result = MathHelpers::normalize(5.0f, 20.0f, -20.0f);
    ASSERT(is_finite(result));
    ASSERT_NEAR(result, 0.0f, 1e-6f);
}

// ============================================================
// LorenzAttractor tests
// ============================================================

TEST(lorenz_initial_state) {
    LorenzAttractor l(0.1f, 0.0f, 0.0f);
    ASSERT(is_finite(l.x));
    ASSERT(is_finite(l.y));
    ASSERT(is_finite(l.z));
}

TEST(lorenz_step_produces_finite_output) {
    LorenzAttractor l;
    for (int i = 0; i < 10000; i++) {
        l.step(0.01f);
    }
    ASSERT(is_finite(l.x));
    ASSERT(is_finite(l.y));
    ASSERT(is_finite(l.z));
}

TEST(lorenz_normalization_in_range) {
    LorenzAttractor l;
    for (int i = 0; i < 5000; i++) {
        l.step(0.01f);
        float nx = l.getNormX();
        float ny = l.getNormY();
        float nz = l.getNormZ();
        ASSERT(nx >= 0.0f && nx <= 1.0f);
        ASSERT(ny >= 0.0f && ny <= 1.0f);
        ASSERT(nz >= 0.0f && nz <= 1.0f);
    }
}

TEST(lorenz_reset_restores_state) {
    LorenzAttractor l;
    l.step(0.01f);
    l.step(0.01f);
    l.reset(0.1f, 0.0f, 0.0f);
    ASSERT_NEAR(l.x, 0.1f, 1e-6f);
    ASSERT_NEAR(l.y, 0.0f, 1e-6f);
    ASSERT_NEAR(l.z, 0.0f, 1e-6f);
}

TEST(lorenz_character_low_periodic) {
    LorenzAttractor l;
    l.setCharacter(0.0f);
    ASSERT_NEAR(l.rho, 15.0f, 1e-4f);
}

TEST(lorenz_character_high_chaotic) {
    LorenzAttractor l;
    l.setCharacter(1.0f);
    ASSERT_NEAR(l.rho, 45.0f, 1e-4f);
}

TEST(lorenz_character_default_midpoint) {
    LorenzAttractor l;
    l.setCharacter(0.5f);
    ASSERT_NEAR(l.rho, 30.0f, 1e-4f);
}

TEST(lorenz_stable_at_extreme_character) {
    // Run at max character for extended period — should not diverge
    LorenzAttractor l;
    l.setCharacter(1.0f);
    for (int i = 0; i < 50000; i++) {
        l.step(0.02f); // max dt
    }
    ASSERT(is_finite(l.x));
    ASSERT(is_finite(l.y));
    ASSERT(is_finite(l.z));
    ASSERT(l.getNormX() >= 0.0f && l.getNormX() <= 1.0f);
}

TEST(lorenz_tiny_dt_still_evolves) {
    LorenzAttractor l(0.1f, 0.0f, 0.0f);
    float initial_x = l.x;
    for (int i = 0; i < 1000; i++) {
        l.step(0.0001f); // minimum dt
    }
    ASSERT(l.x != initial_x); // should have moved
}

TEST(lorenz_zero_dt_no_change) {
    LorenzAttractor l(5.0f, 3.0f, 2.0f);
    float x0 = l.x, y0 = l.y, z0 = l.z;
    l.step(0.0f);
    ASSERT_NEAR(l.x, x0, 1e-6f);
    ASSERT_NEAR(l.y, y0, 1e-6f);
    ASSERT_NEAR(l.z, z0, 1e-6f);
}

// ============================================================
// RosslerAttractor tests
// ============================================================

TEST(rossler_initial_state) {
    RosslerAttractor r(0.1f, 0.0f, 0.0f);
    ASSERT(is_finite(r.x));
    ASSERT(is_finite(r.y));
    ASSERT(is_finite(r.z));
}

TEST(rossler_step_produces_finite_output) {
    RosslerAttractor r;
    for (int i = 0; i < 10000; i++) {
        r.step(0.01f);
    }
    ASSERT(is_finite(r.x));
    ASSERT(is_finite(r.y));
    ASSERT(is_finite(r.z));
}

TEST(rossler_normalization_in_range) {
    RosslerAttractor r;
    for (int i = 0; i < 5000; i++) {
        r.step(0.01f);
        float nx = r.getNormX();
        float ny = r.getNormY();
        float nz = r.getNormZ();
        ASSERT(nx >= 0.0f && nx <= 1.0f);
        ASSERT(ny >= 0.0f && ny <= 1.0f);
        ASSERT(nz >= 0.0f && nz <= 1.0f);
    }
}

TEST(rossler_character_low_periodic) {
    RosslerAttractor r;
    r.setCharacter(0.0f);
    ASSERT_NEAR(r.c, 2.0f, 1e-4f);
}

TEST(rossler_character_high_chaotic) {
    RosslerAttractor r;
    r.setCharacter(1.0f);
    ASSERT_NEAR(r.c, 18.0f, 1e-4f);
}

TEST(rossler_stable_at_extreme_character) {
    RosslerAttractor r;
    r.setCharacter(1.0f);
    for (int i = 0; i < 50000; i++) {
        r.step(0.02f);
    }
    ASSERT(is_finite(r.x));
    ASSERT(is_finite(r.y));
    ASSERT(is_finite(r.z));
    ASSERT(r.getNormX() >= 0.0f && r.getNormX() <= 1.0f);
}

// ============================================================
// ChaosModulator tests
// ============================================================

TEST(modulator_init_produces_valid_output) {
    ChaosModulator cm;
    cm.processBlock(0.5f);
    for (int i = 0; i < 6; i++) {
        float lv = cm.getLorenzModulation(i);
        float rv = cm.getRosslerModulation(i);
        ASSERT(is_finite(lv));
        ASSERT(is_finite(rv));
        ASSERT(lv >= 0.0f && lv <= 1.0f);
        ASSERT(rv >= 0.0f && rv <= 1.0f);
    }
}

TEST(modulator_out_of_bounds_channel_returns_zero) {
    ChaosModulator cm;
    cm.processBlock(0.5f);
    ASSERT_NEAR(cm.getLorenzModulation(-1), 0.0f, 1e-6f);
    ASSERT_NEAR(cm.getLorenzModulation(6), 0.0f, 1e-6f);
    ASSERT_NEAR(cm.getRosslerModulation(-1), 0.0f, 1e-6f);
    ASSERT_NEAR(cm.getRosslerModulation(99), 0.0f, 1e-6f);
}

TEST(modulator_reset_produces_consistent_state) {
    ChaosModulator cm;
    cm.processBlock(0.5f);
    cm.processBlock(0.5f);

    ChaosModulator cm2;
    // After reset, two fresh modulators should produce same output
    cm.resetAll();
    cm.processBlock(0.5f);
    cm2.processBlock(0.5f);

    for (int i = 0; i < 6; i++) {
        ASSERT_NEAR(cm.getLorenzModulation(i), cm2.getLorenzModulation(i), 1e-6f);
        ASSERT_NEAR(cm.getRosslerModulation(i), cm2.getRosslerModulation(i), 1e-6f);
    }
}

TEST(modulator_spread_differentiates_instances) {
    ChaosModulator cm1, cm2;

    // Run with no spread
    for (int i = 0; i < 100; i++)
        cm1.processBlock(0.5f, 0.0f);

    // Run with max spread
    for (int i = 0; i < 100; i++)
        cm2.processBlock(0.5f, 1.0f);

    // Channels 3-5 (instance 2) should differ between spread=0 and spread=1
    bool any_different = false;
    for (int i = 3; i < 6; i++) {
        if (fabsf(cm1.getLorenzModulation(i) - cm2.getLorenzModulation(i)) > 0.001f) {
            any_different = true;
            break;
        }
    }
    ASSERT(any_different);
}

TEST(modulator_character_changes_behavior) {
    ChaosModulator cm1, cm2;

    cm1.setCharacter(0.0f); // periodic
    cm2.setCharacter(1.0f); // chaotic

    for (int i = 0; i < 5000; i++) {
        cm1.processBlock(0.5f);
        cm2.processBlock(0.5f);
    }

    // Both should still produce valid output
    for (int i = 0; i < 6; i++) {
        ASSERT(is_finite(cm1.getLorenzModulation(i)));
        ASSERT(is_finite(cm2.getLorenzModulation(i)));
        ASSERT(cm1.getLorenzModulation(i) >= 0.0f && cm1.getLorenzModulation(i) <= 1.0f);
        ASSERT(cm2.getLorenzModulation(i) >= 0.0f && cm2.getLorenzModulation(i) <= 1.0f);
    }
}

TEST(modulator_long_run_stability) {
    // Simulate ~10 minutes of real-time operation at typical call rate
    // process_chaos_lfos called at ~1kHz, so 600,000 iterations = 10 minutes
    ChaosModulator cm;
    cm.setCharacter(0.7f); // high chaos but not max
    for (int i = 0; i < 600000; i++) {
        cm.processBlock(0.5f, 0.3f);
    }
    for (int i = 0; i < 6; i++) {
        float lv = cm.getLorenzModulation(i);
        float rv = cm.getRosslerModulation(i);
        ASSERT(is_finite(lv));
        ASSERT(is_finite(rv));
        ASSERT(lv >= 0.0f && lv <= 1.0f);
        ASSERT(rv >= 0.0f && rv <= 1.0f);
    }
}

TEST(modulator_speed_zero) {
    ChaosModulator cm;
    cm.processBlock(0.0f);
    for (int i = 0; i < 6; i++) {
        ASSERT(is_finite(cm.getLorenzModulation(i)));
    }
}

TEST(modulator_speed_max) {
    ChaosModulator cm;
    for (int i = 0; i < 1000; i++) {
        cm.processBlock(1.0f);
    }
    for (int i = 0; i < 6; i++) {
        ASSERT(is_finite(cm.getLorenzModulation(i)));
        ASSERT(cm.getLorenzModulation(i) >= 0.0f && cm.getLorenzModulation(i) <= 1.0f);
    }
}

TEST(modulator_freeze_holds_values) {
    ChaosModulator cm;
    // Run to some state
    for (int i = 0; i < 100; i++)
        cm.processBlock(0.5f);

    // Record values (simulating freeze: don't call processBlock)
    float frozen_vals[6];
    for (int i = 0; i < 6; i++)
        frozen_vals[i] = cm.getLorenzModulation(i);

    // Without calling processBlock, values should remain the same
    for (int i = 0; i < 6; i++)
        ASSERT_NEAR(cm.getLorenzModulation(i), frozen_vals[i], 1e-6f);
}

// ============================================================
// Main
// ============================================================

int main(void) {
    printf("Chaos Modulator Unit Tests\n");
    printf("==========================\n\n");

    printf("MathHelpers:\n");
    run_clamp_within_range();
    run_clamp_below_min();
    run_clamp_above_max();
    run_normalize_midpoint();
    run_normalize_at_min();
    run_normalize_at_max();
    run_normalize_below_range_clamps();
    run_normalize_above_range_clamps();
    run_normalize_degenerate_range_no_crash();
    run_normalize_inverted_range_no_crash();

    printf("\nLorenzAttractor:\n");
    run_lorenz_initial_state();
    run_lorenz_step_produces_finite_output();
    run_lorenz_normalization_in_range();
    run_lorenz_reset_restores_state();
    run_lorenz_character_low_periodic();
    run_lorenz_character_high_chaotic();
    run_lorenz_character_default_midpoint();
    run_lorenz_stable_at_extreme_character();
    run_lorenz_tiny_dt_still_evolves();
    run_lorenz_zero_dt_no_change();

    printf("\nRosslerAttractor:\n");
    run_rossler_initial_state();
    run_rossler_step_produces_finite_output();
    run_rossler_normalization_in_range();
    run_rossler_character_low_periodic();
    run_rossler_character_high_chaotic();
    run_rossler_stable_at_extreme_character();

    printf("\nChaosModulator:\n");
    run_modulator_init_produces_valid_output();
    run_modulator_out_of_bounds_channel_returns_zero();
    run_modulator_reset_produces_consistent_state();
    run_modulator_spread_differentiates_instances();
    run_modulator_character_changes_behavior();
    run_modulator_long_run_stability();
    run_modulator_speed_zero();
    run_modulator_speed_max();
    run_modulator_freeze_holds_values();

    printf("\n==========================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
