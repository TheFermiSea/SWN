# Chaos Modulator — SWN Extension

The Chaos Modulator replaces the standard LFO engine with outputs from Lorenz and Rossler strange attractors, generating complex, never-repeating modulation patterns across all 6 channels.

## Activation

Press **LFO Mode + Fine** simultaneously to cycle through modes:

| Mode | Attractor | LED Color |
|------|-----------|-----------|
| 0 | Factory LFOs (default) | Normal |
| 1 | Lorenz | Purple |
| 2 | Rossler | Gold |

A persistent dim LED on inner ring position 0 confirms the active mode.

## Controls (Chaos Mode Only)

All encoders are remapped while chaos is active:

| Encoder | Function | Fine Mode |
|---------|----------|-----------|
| LFO Speed (turn) | Chaos speed | ±0.01 per click |
| LFO Shape (turn) | Character / turbulence | ±0.005 per click |
| LFO Shape (press) | Freeze / unfreeze | — |
| LFO Gain (turn) | Output depth | ±0.01 per click |
| LFO Phase (turn) | Instance spread | ±0.01 per click |

### Speed
Controls the integration timestep. Lower = slower, more languid chaos. Higher = faster, more active. CV input on the LFO jack adds to the encoder value.

### Character
Morphs the attractor parameters that control how chaotic the system is:
- **Lorenz**: adjusts `rho` from 15 (periodic orbits) to 45 (highly turbulent)
- **Rossler**: adjusts `c` from 2 (periodic) to 18 (chaotic bursts)
- Default midpoint (0.5) gives classic attractor behavior

### Freeze
Press the LFO Shape encoder to freeze all chaos outputs at their current values. The attractors stop integrating but held values continue driving the LFO outputs. Inner ring LEDs breathe slowly while frozen. Press again to resume.

### Gain
Scales the chaos output amplitude from 0 (silent) to 1 (full range). Useful for subtle modulation or full-range CV.

### Spread
Offsets the timestep between the two attractor instances. At 0, both instances evolve identically. At 1, instance 2 runs at 2x speed, decorrelating channels 4-6 from channels 1-3.

## Channel Mapping

Each attractor type runs 2 instances, providing 6 independent outputs (3 dimensions × 2 instances):

| Channel | Source |
|---------|--------|
| 1 | Instance 1, X axis |
| 2 | Instance 1, Y axis |
| 3 | Instance 1, Z axis |
| 4 | Instance 2, X axis |
| 5 | Instance 2, Y axis |
| 6 | Instance 2, Z axis |

## Clock Sync

When an external clock is patched into the CLK IN jack, attractors automatically reset to their initial conditions on each rising clock edge. This creates rhythmic chaos patterns that lock to your tempo.

## Visual Feedback

- **Array LEDs**: Track real-time chaos modulation values per channel (purple or gold)
- **Inner ring LED 0**: Dim persistent glow shows active chaos mode
- **Inner ring (all)**: Breathing animation while frozen
- **Outer ring**: Brief bar graph showing speed level after encoder adjustment
- **Inner ring flash**: 500ms burst on mode change for confirmation

## Interactions with Factory Features

- Channel **mute** is respected — muted channels output 0V
- Chaos is automatically disabled during **wavetable recording/editing**
- **LFO→VCA** routing still works with chaos outputs
- Smooth **fade-in/fade-out** on mode transitions prevents audio clicks

## Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────┐
│ UI_Hook.cpp │────>│ chaos_interface.h │<────│ ui_modes.c  │
│ (LEDs, mode)│     │ (shared header)   │     │ (button     │
└─────────────┘     └──────────────────┘     │  combo)     │
                            │                 └─────────────┘
                            v
┌─────────────────────────────────────────┐
│           Audio_Hook.cpp                │
│  ┌─────────────────────────────────┐    │
│  │      ChaosModulator             │    │
│  │  ┌──────────┐  ┌──────────┐     │    │
│  │  │ Lorenz×2 │  │Rossler×2 │     │    │
│  │  └──────────┘  └──────────┘     │    │
│  └─────────────────────────────────┘    │
│         │                               │
│         v                               │
│   lfos.preload[0..5]                    │
└─────────────────────────────────────────┘
         │
         v
   ┌──────────┐
   │ envout   │──> hardware PWM outputs
   │ _pwm.c   │
   └──────────┘
```

## Testing

Host-side unit tests (no hardware required):

```bash
cd tests/
make        # compiles and runs 35 tests
```

Tests cover: MathHelpers edge cases, attractor stability at all parameter extremes, normalization bounds, long-duration runs (~10 min simulated), spread differentiation, freeze behavior, and out-of-bounds safety.
