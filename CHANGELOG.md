# Changelog

All notable changes to Good Reverb are listed here. Each `## vX.Y` heading becomes the notes of the
matching GitHub release.

## v0.1 - 2026-10-18

First public release.

### Sound

- 16-line modulated feedback delay network with an orthonormal Hadamard feedback matrix.
- 12 modes: Hall, Bright Hall, Plate, Smooth Plate, Room, Chamber, Ambience, Random Hall,
  Chorus Hall, Dirty Hall, Nonlinear (gated / reverse) and Shimmer (octave-up feedback).
- 3 colour eras: Vintage (band-limited, saturated), Classic and Modern (full bandwidth).
- The Decay knob is the real RT60: measured within about 5% of the setting in every linear mode,
  at sample rates from 44.1 to 192 kHz.
- 3-band decay EQ (Bass Mult / Bass Xover, High Shelf / High Freq) plus High Cut / Low Cut on the
  wet signal.
- Attack, Early / Late Diffusion, Mod Rate / Depth, Predelay (up to 500 ms), Size and Width.
- Click-free mode switching during playback.

### Plugin

- VST3 and Standalone on Windows, macOS and Linux, AU on macOS, LV2 on Linux.
- Mono-to-stereo, stereo and mono layouts.
- Resizable UI with a live decay display.
- 14 factory presets.
- Validated with pluginval at strictness level 10.

### Known limitations

- The macOS builds are not notarized. See the install notes if macOS blocks the plugin.
- Presets are factory-only: your own settings are saved with the DAW project, but there is no
  user preset browser yet.
