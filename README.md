# Echojam

[Airwindows Echojam](https://www.airwindows.com/echojam/) for
[Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.
Module id `echojam`.

An echo made from a “failed reverb”: 16 delay lines, Householder mixing, and
Bezier undersampling. High Regen turns the slap into a wash. Select (0–16)
picks among 17 scatter patterns — not a tempo grid.

## Controls

- **Select** — 0 to 16, different delay-length sets / stereo scatter
- **Regen** — regeneration (high values become reverb)
- **Speed** — sample-locked Bezier undersampling (lower = slower, darker)
- **Dry/Wet** — mix

There is no millisecond Time control and no division sync. Delay times are
whatever the selected program and Speed happen to be at 44.1 kHz.

Measure on the Move before stacking many instances: quote the **~900 µs DSP
budget**, not the 2.902 ms block period.

## Building

```bash
./scripts/build.sh      # ARM64 via Docker
./scripts/install.sh    # deploy to move.local
```

Installs to `/data/UserData/move-anything/modules/audio_fx/echojam/`.
Does not replace TapeDelay.

## Credits

Echojam DSP by **Airwindows** (Chris Johnson), MIT.
Ported to the Schwung audio FX API in this tree.
