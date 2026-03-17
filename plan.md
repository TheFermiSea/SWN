# MIDI I/O Mode — Implementation Plan

## Overview

Add a full bidirectional MIDI mode to the SWN, using the existing UART5 hardware
(RX on PD2, TX on PC12 — currently unused). When activated, incoming MIDI notes
control oscillator pitch, CCs map to module parameters, the module echoes its
parameter state as MIDI CC TX, and MIDI clock can sync the LFO.

## Activation Combo

**Hold LFOVCA + LFOMODE + FINE simultaneously for 2 seconds (MED_PRESS)**

Rationale:
- LFOVCA + LFOMODE is already the "key mode" combo, but adding FINE + med-press
  makes it impossible to trigger accidentally
- No existing combo uses all three together
- Exit: same combo again (toggle), or power cycle

Visual feedback: all 6 channel LEDs flash a distinct color (e.g. cyan) when
entering MIDI mode, then the inner ring shows MIDI activity.

---

## File Changes

### 1. `inc/drivers/uart_driver.h` — Add TX pin defines

```c
#define UART_TX_GPIO GPIOC
#define UART_TX_PIN GPIO_PIN_12
#define UART_TX_PIN_AF GPIO_AF8_UART5
```

Add declaration:
```c
void UART_Transmit(uint8_t *pData, uint16_t Size);
```

### 2. `src/drivers/uart_driver.c` — Enable TX, add transmit function

- Add PC12 GPIO init (AF8, push-pull, high speed)
- Change `UART_MODE_RX` → `UART_MODE_TX_RX`
- Add `UART_Transmit()` using `HAL_UART_Transmit_IT()`

### 3. `inc/midi.h` — New header (MIDI protocol definitions)

```c
#pragma once
#include <stdint.h>

// MIDI Status bytes
#define MIDI_NOTE_ON        0x90
#define MIDI_NOTE_OFF       0x80
#define MIDI_CC             0xB0
#define MIDI_PROG_CHANGE    0xC0
#define MIDI_PITCH_BEND     0xE0
#define MIDI_CLOCK          0xF8
#define MIDI_START          0xFA
#define MIDI_STOP           0xFC

// CC numbers for SWN parameters
#define MIDI_CC_BROWSE      1   // Mod wheel → browse/wt select
#define MIDI_CC_LATITUDE    2   // Breath → latitude
#define MIDI_CC_LONGITUDE   3   // → longitude
#define MIDI_CC_DEPTH       4   // → depth
#define MIDI_CC_WTSPREAD    5   // → wavetable spread
#define MIDI_CC_TRANSPOSE   6   // → transpose
#define MIDI_CC_CHORD       7   // Volume → chord
#define MIDI_CC_LFO_SPEED   14  // → LFO speed
#define MIDI_CC_LFO_SHAPE   15  // → LFO shape
#define MIDI_CC_LFO_DEPTH   16  // → LFO depth

#define MIDI_NUM_CHANNELS   6   // SWN has 6 oscillator channels

// MIDI mode state
typedef struct {
    uint8_t  enabled;
    uint8_t  midi_channel;       // 0-15, default 0 (omni listen on all)
    uint8_t  last_note[6];       // last note per SWN channel
    uint8_t  last_velocity[6];
    uint32_t clock_count;        // for LFO sync
    uint8_t  clock_running;
} MidiState;

void midi_init(void);
void midi_enable(void);
void midi_disable(void);
uint8_t midi_is_enabled(void);
void midi_process_byte(uint8_t byte);
void midi_send_cc(uint8_t channel, uint8_t cc_num, uint8_t value);
void midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void midi_send_note_off(uint8_t channel, uint8_t note);
void midi_tx_param_state(void);     // periodic echo of knob state
void midi_process_clock(void);
```

### 4. `src/midi.c` — New file (MIDI engine)

Core implementation:
- **RX parser**: Running-status-aware MIDI byte parser. On Note On ch N (N=0-5),
  convert MIDI note → frequency and write to the channel's pitch parameter,
  bypassing the normal V/Oct ADC path. On CC, scale 0-127 → parameter range
  and write to the appropriate parameter.
- **TX engine**: `midi_tx_param_state()` called from main loop at ~50Hz.
  Reads current knob/CV values, scales to 0-127, sends CC if value changed
  since last send (delta tracking to avoid MIDI floods).
- **Clock**: Count MIDI clock ticks (24 ppqn), derive BPM, write to LFO
  rate parameter when clock sync is active.
- **Note allocation**: Round-robin or lowest-channel-first assignment of
  incoming notes to the 6 oscillator channels.

### 5. `inc/key_combos.h` — Add MIDI mode combo

```c
static inline uint8_t key_combo_enter_midi_mode(void) {
    return (button_med_pressed(butm_LFOVCA_BUTTON)
         && button_med_pressed(butm_LFOMODE_BUTTON)
         && switch_pressed(FINE_BUTTON));
}
```

### 6. `inc/ui_modes.h` — Add MIDI_MODE enum value

```c
enum UI_Modes {
    // ... existing ...
    MIDI_MODE,          // <-- add before NUM_UI_MODES
    NUM_UI_MODES
};
```

### 7. `src/ui_modes.c` — Add MIDI mode transition

In `check_ui_mode_requests()`, when `ui_mode == PLAY`:
```c
else if (key_combo_enter_midi_mode()) { arm_ui = MIDI_MODE; }
```

And the release/activation handler:
```c
else if (!key_combo_enter_midi_mode() && (arm_ui == MIDI_MODE)) {
    if (midi_is_enabled()) {
        midi_disable();
        // restore normal parameter routing
    } else {
        midi_enable();
        // LED feedback
    }
    arm_ui = UI_NONE;
}
```

### 8. `src/sel_bus.c` — Route bytes through MIDI engine

In `UART5_IRQHandler()`, when MIDI mode is enabled, pass bytes to
`midi_process_byte()` instead of (or in addition to) the existing
SelBus preset handler. The existing preset save/recall continues to
work — MIDI mode just adds note/CC/clock handling.

### 9. `src/params_update.c` — MIDI parameter injection

Add a check in the parameter update paths: when MIDI mode is enabled,
override the ADC-derived values with MIDI-derived values for the
mapped parameters. This is the trickiest integration point — need to
understand which specific variables to write to.

Key integration points:
- Pitch: write to the per-channel frequency/pitch variable that normally
  comes from the V/Oct ADC + transpose + octave processing
- CCs: write to the same variables that the knob/CV conditioning produces
  (e.g., `params.wt_browse_step_pos`, `params.latitude`, etc.)

### 10. `src/main.c` — Init and main loop integration

- Add `#include "midi.h"` and call `midi_init()` during startup
- In main loop, add `midi_tx_param_state()` call (throttled to ~50Hz)

---

## Note-to-Pitch Strategy

MIDI note 0-127 → frequency. The SWN already has pitch calculation
infrastructure (V/Oct). The cleanest approach:

1. MIDI note → float voltage equivalent: `voltage = (note - 60) / 12.0`
2. Feed this into the existing pitch calculation path that converts
   voltage → frequency, reusing all the existing tuning/octave logic

This way transpose, fine tune, and octave controls still work on top
of the MIDI pitch — they become offsets rather than being overridden.

---

## CC Mapping Detail

| MIDI CC | SWN Parameter | Notes |
|---------|--------------|-------|
| 1 (Mod) | Browse/WT select | 0-127 → full sphere range |
| 2 | Latitude | Sphere Z-axis |
| 3 | Longitude | Sphere X-axis |
| 4 | Depth | Sphere Y-axis |
| 5 | WT Spread | Per-channel spread |
| 6 | Transpose | Semitone offset |
| 7 (Vol) | Chord | Chord selection |
| 14 | LFO Speed | Rate control |
| 15 | LFO Shape | Shape selection |
| 16 | LFO Depth | Modulation amount |

CC numbers are provisional — can be remapped. Standard assignments
(mod wheel, volume) are used where semantically appropriate.

---

## MIDI Clock Sync

- 24 MIDI clocks per quarter note (standard)
- On receiving MIDI Start (0xFA): reset clock counter, begin syncing
- On each Clock (0xF8): increment counter, derive tempo every 24 ticks
- Derived BPM → LFO rate parameter (overrides knob when clock is active)
- On MIDI Stop (0xFC): release LFO back to knob control

---

## TX Echo Behavior

When MIDI mode is active, the module sends CC messages reflecting its
current parameter state:
- On mode entry: send all current values as CC dump
- Ongoing: send CC only when a parameter changes by ≥1 (7-bit quantized)
- Prevents feedback loops: ignore CC values that match what we just sent

---

## Implementation Order

1. UART TX hardware enable (uart_driver.c/h) — smallest, most testable change
2. MIDI protocol header and basic parser (midi.h, midi.c)
3. Key combo and UI mode integration
4. Note-to-pitch injection into params_update.c
5. CC-to-parameter mapping
6. TX echo engine
7. MIDI clock sync
8. LED feedback for MIDI activity

---

## Risk Assessment

- **Low risk**: UART TX enable (PC12 is unused, no hardware conflict)
- **Low risk**: MIDI parser (new code, doesn't touch existing paths)
- **Medium risk**: Parameter injection in params_update.c (large file, central to everything)
- **Mitigation**: All MIDI parameter writes gated behind `midi_is_enabled()` check,
  so when MIDI mode is off, zero code paths are affected
