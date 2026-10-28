# CLAUDE.md

Instructions for Claude Code when working with this repository.

## Project Overview

Echojam is an audio effect module for Schwung: Airwindows Echojam (echo that
is secretly a reverb). Module id `echojam`.

This repo used to be TapeDelay (`tapedelay`). Echojam is a **new module**, not
a replacement of that id. Do not install over
`/data/UserData/move-anything/modules/audio_fx/tapedelay/`.

## DSP budget (this repo’s history)

For one day (2026-08-22, v1.3.3-v1.3.5) the old `tapedelay` id shipped
**Tape Echo 2**, a component-modelled three-head machine. It was reverted
because it cost **0.37 ms per block, ~41% of the ~900 us DSP budget**, so only
two instances fit.

**Quote the ~900 us budget, not the 2.902 ms block period.** The SPI callback
has ~900 us left after the transfer, covering all four slots, master FX and
mixing. Measuring against the block period is how a 41% module looked like a
14% module.

Echojam is not Tape Echo 2 (ClearCoat-class delay network). Still measure on
the device before stacking many instances.

Tape Echo 2's GPL-3.0 source remains in git history at tags `v1.3.3`-`v1.3.5`.
This tree is MIT.

## Architecture

```
src/
  dsp/
    echojam.c            # Airwindows Echojam port
    audio_fx_api_v1.h    # Audio FX API (from move-anything)
    plugin_api_v1.h      # Plugin API types (from move-anything)
  module.json            # Module metadata
```

## Key Implementation Details

### Audio FX API

Implements Move Anything audio_fx_api_v2:
- `create_instance` / `destroy_instance`
- `process_block`: In-place stereo audio processing
- `set_param`: select, regen, speed, mix
- `get_param`: Returns current parameter values

### DSP Components

Port of Airwindows Echojam (float buffers, ~900 kB per instance):

1. **16 delay lines** per channel, lengths from the 17 Select programs
2. **Householder mixing** between four groups of four delays
3. **Bezier undersampling** (`speed`) — sample-locked, no true time knob
4. **Regen** on the feedback matrix (washes into reverb at high settings)
5. **Dry/Wet** mix

### Signal Chain Integration

Module declares `"chainable": true` and `"component_type": "audio_fx"` in module.json.

Installs to: `/data/UserData/move-anything/modules/audio_fx/echojam/`

## Build Commands

```bash
./scripts/build.sh      # Build for ARM64 via Docker
./scripts/install.sh    # Deploy to Move
```
