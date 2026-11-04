# SkillIssue: audio plugins (VST3 / AU)

Six plugins for Ableton and other DAWs, on Windows and macOS. JUCE 8, C++17. The look follows the skillissue.site style: dark iOS-like glass, rose accent #f43f5e, Plus Jakarta Sans font.

| Plugin | What it is |
|---|---|
| **SI Riser** | Reverse-reverb riser in the style of AtmosRiser: capture → render → drag the WAV onto a track |
| **SI Reverb** | Algorithmic reverb in the style of VintageVerb: 22 modes, 70s/80s/Now color, tempo AUTO |
| **SI EQ** | 8-band dynamic EQ (Infinity EQ 2 / Pro-Q style): dynamic range drawn on the curve, analyzer, M/S/L/R per band, presets incl. "Moje Clean" |
| **SI Tune** | Real-time autotune, **0 samples latency** (Zero Latency), Input Type, Tracking, Flex Tune, Natural Vibrato, Humanize, Note Stabilizer, orb visual, scale on a keyboard |
| **SI 76** | FET compressor in the style of the 1176 / CLA-76: **BLUE** (smooth) and **BLACK** (aggressive, for rap) |
| **SI Saturn** | Multiband distortion in the style of Saturn 2: 28 styles (Tube/Tape/Saturation/Amp/Transformer/FX), up to 6 bands (+ to split, 6/12/24/48 dB crossovers), Feedback, Dynamics, Tone, modulation (XLFO, Envelope, Follower, Transients, Slider: drag onto a knob), HQ up to 8x, M/S |

## Installer for a friend (Windows)
`dist\SkillIssue-Plugins-Setup-1.2.0.exe`: one file. You pick which plugins to install, they go into `C:\Program Files\Common Files\VST3`, and an uninstaller is included.
To rebuild it: `build-win.bat` → `installer\build-installer.bat` (requires Inno Setup 6: `winget install JRSoftware.InnoSetup`).

**macOS:** `./build-mac.sh` on a Mac (universal VST3 + AU), or GitHub Actions (`.github/workflows/build.yml`), which builds Windows + Mac and tests every plugin.

## Tests
`build\SITests_artefacts\Release\SITests.exe <paths to .vst3>` checks:
- reverb: RT60 and level for 22 modes × 3 colors;
- riser: length, level and FX;
- EQ: measured response matches the curve to 0.1 dB, plus dynamics;
- Tune: 450 Hz → 440 Hz, scales and transposition, exact to 0.1 cent, latency 0;
- Saturn: flat crossovers, 16 stable styles, auto gain;
- every plugin in a VST3 host: 3 sample rates × 3 buffer sizes, automation, NaN check, state save.

`SI_UI_SELFTEST=<folder>` makes each plugin write a PNG of its UI and a layout lint.

## Licenses
JUCE 8 (AGPLv3 or commercial license; selling these plugins needs a JUCE license). The Plus Jakarta Sans font is under the SIL OFL 1.1 (`resources/fonts`).
