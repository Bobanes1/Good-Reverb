# Good Reverb

A free, open-source algorithmic stereo reverb plugin (VST3 / AU / LV2 / Standalone) in the spirit of
the classic "mode + era" algorithmic reverbs: pick a space, pick a colour, and shape it with the
decay, diffusion, modulation and decay EQ controls.

Good Reverb is an original implementation written from scratch. It is not affiliated with, endorsed
by, or derived from the code of any commercial reverb vendor.

## Features

- **12 reverb modes**: Hall, Bright Hall, Plate, Smooth Plate, Room, Chamber, Ambience,
  Random Hall, Chorus Hall, Dirty Hall, Nonlinear (gated / reverse) and Shimmer (octave-up feedback).
- **3 colour eras**:
  - *Vintage*: band-limited (~7 kHz in the tank), slightly saturated, linear interpolation, more motion
  - *Classic*: gentle band-limiting and extra random modulation
  - *Modern*: full bandwidth, cubic interpolation, clean
- **Accurate decay**: the Decay knob is the real RT60. Measured RT60 stays within about 5% of the
  setting in every linear mode, at every sample rate from 44.1 to 192 kHz.
- **3-band decay EQ**: Bass Mult / Bass Xover set the low-frequency decay time, High Shelf / High
  Freq make the highs die away faster. High Cut / Low Cut filter the wet signal.
- **Decay display** showing predelay, build-up and the low/mid/high decay slopes as you turn knobs.
- 14 factory presets, a resizable UI, full host automation, mono and stereo layouts.
- Light on CPU: one instance runs about 30–40x faster than realtime on a single core.

## Controls

| Section   | Control      | What it does |
|-----------|--------------|--------------|
| Space     | Mix          | Equal-power dry/wet balance |
|           | Predelay     | 0–500 ms before the reverb starts |
|           | Decay        | RT60, 0.2–30 s (in Nonlinear mode: the gate length) |
|           | Size         | Scales the delay network: small rooms to huge spaces |
|           | Width        | Stereo width of the wet signal (0% = mono) |
| Character | Attack       | Build-up shape: low = immediate, high = slow swell (Nonlinear: decaying → gated → reverse) |
|           | Early Diff   | Density of the onset / early reflections |
|           | Late Diff    | Density of the tail |
|           | Mod Rate     | Speed of delay modulation |
|           | Mod Depth    | Amount of delay modulation (chorusing / smearing of the tail) |
| Decay EQ  | High Cut     | Lowpass on the wet signal |
|           | Low Cut      | Highpass on the wet signal |
|           | Bass Mult    | Low-frequency decay time as a multiple of Decay |
|           | Bass Xover   | Crossover between the low and mid decay bands |
|           | High Shelf   | How much faster the high band decays (0 dB = same as mid) |
|           | High Freq    | Crossover between the mid and high decay bands |

## How it works

```
in ─► predelay ─► early diffusion (4 allpasses/ch) ─┬─► early reflections ───────────────┐
                                                    └─► attack build-up taps             │
                                                           │                             ▼
                        ┌─────────────── 16-line FDN ──────▼────────────┐            mix, width,
                        │ modulated delay ─► 3-band decay ─► colour ─►   │ ─► wet ─►  cuts, colour
                        │ in-loop allpass ─► Hadamard feedback matrix    │            ─► dry/wet
                        └────────────────────────────────────────────────┘
```

- The late reverb is a 16-line feedback delay network with an orthonormal Hadamard feedback matrix.
  Each line has its own LFO (sine, smoothed random or a blend, depending on mode and colour), an
  in-loop allpass for late diffusion, and a 3-band decay filter whose per-band gains are computed
  from the band's RT60 and that line's loop length.
- The Attack control redistributes the input over a set of build-up taps that feed different lines,
  so the reverb can bloom immediately or swell in.
- Nonlinear mode replaces the network with a dense cloud of taps whose envelope follows Attack.
- Shimmer pitch-shifts the tank output up an octave and feeds it back. The feedback gain is scaled
  from the decay time and network size, with a soft limiter, so it can't run away.
- Mode changes crossfade the wet signal (15 ms) while the buffers are rebuilt, so switching modes
  during playback doesn't click or blow up.

The DSP lives in `Source/dsp/` and is plain C++17 with no JUCE dependency.

## Building

Requirements: CMake 3.22+, a C++17 compiler. JUCE 8.0.4 is downloaded automatically. Alternatively,
pass `-DJUCE_DIR=/path/to/JUCE` to use a local checkout.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure   # DSP tests
```

On Linux, install the JUCE dependencies first:

```bash
sudo apt-get install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
```

Built plugins end up in `build/GoodReverb_artefacts/Release/`. Copy them into your plugin folder:

| Format | Windows | macOS | Linux |
|--------|---------|-------|-------|
| VST3   | `C:\Program Files\Common Files\VST3` | `~/Library/Audio/Plug-Ins/VST3` | `~/.vst3` |
| AU     | — | `~/Library/Audio/Plug-Ins/Components` | — |
| LV2    | — | — | `~/.lv2` |

Every push also builds Windows, macOS (universal) and Linux binaries on GitHub Actions. Download
them from the run's **Artifacts** section. The macOS builds are unsigned, so you may need to run
`xattr -cr "Good Reverb.vst3"` (or `.component`) before your DAW will load them.

To build only the DSP and tests (no JUCE download): `-DGOOD_REVERB_BUILD_PLUGIN=OFF`.

## Tests

`GoodReverbTests` checks:

- RT60 accuracy per mode and decay setting, and across sample rates
- stability with maximum decay, extreme EQ and full modulation (no runaway, finite output)
- that mix = 0 is bit-exact, that stereo decorrelation and mono width work, and that the
  nonlinear gate closes
- random automation with mode switches every few blocks
- loudness consistency between modes, and performance

Run `GoodReverbTests --render <dir>` to also write a 5-second impulse response WAV for every mode.

The plugin also passes [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10.

## License

GPL-3.0. See [LICENSE](LICENSE). JUCE is used under its AGPLv3 open-source licence.
