# SkillIssue — DESIGN.md v3 "Rose Glass" (plugin UI)

> v3 (2026-10-19): palette and type follow skillissue.site — bg #050505, rose #f43f5e (gradient #e11d48 → #f43f5e → #ff8fa3), text on accent white, muted #a3a3a3, lavender #9e9eff for info, glass = rgba(20,20,20,.6) + 145° white 8%→1% + 1px white 8% border, radius 20/24, Plus Jakarta Sans (ExtraBold titles with white→pink→rose gradient). Knobs: clean filling ring, no tick marks. Faint "SKILL ISSUE" pattern in the background like the site. Sections below describe v2 structure; colours are superseded by this note.

The tokens in code live in `shared/SIStyle.h` (`si::col::*`, `si::font`). The UI is built in JUCE, so every "CSS" value below is a JUCE draw call.

## 1. Visual theme
Neon Glass: near-black with soft lime and teal glows, fine grain and a vignette. Panels are borderless glass. The lime→teal gradient appears only on what is active, what you can grab, or what is moving (value arcs, toggles, visualizers).

## 2. Colours
| Token | Value | Role |
|---|---|---|
| bg / bg2 | #07080b / #0c0e13 | window gradient |
| panel | white 5.5%→1.8% on bg 55% | glass panels |
| stroke | white 13%→2.5% | panel edge (lit from the top) |
| text / label / hint | #f2f4f7 @ 100 / 55 / 32 % | values / names / hints |
| accent → accent2 | #9fe83a → #22d3b4 | brand gradient |
| accentHi | #c6ff6b | hover, synced readouts |
| rec / warn / info | #ff4d5e / #ffa928 / #4cc3ff | capture, manual tempo + freeze, frozen + damping |

## 3. Type
Poppins (OFL, embedded): SemiBold for the name (19 px), buttons (10–11 px, uppercase, +0.08 tracking) and section labels (10 px, +0.18); Medium for values (12 px); Regular for body text.

## 4. Components
- **Knob**: tick ring (29/41 ticks, lit ticks get the gradient), a thin track, a gradient value arc with a 2-layer glow, a dark disc with a top-lit edge, and an indicator dot with glow. Double-click resets it, Alt-drag gives fine control. The value sits below the knob and the label below that.
- **Pill button**: off = white 5% fill + 9% stroke. Hover = 8.5%. On = gradient + halo + top highlight. CTA = lime outline. Rec = red. Icons are vector (`si::Icon`).
- **Segmented / tabs**: a dark pill with a gradient thumb that slides at 60 Hz.
- **Preset bar**: one pill: ‹ name ▾ › | save.
- **Tempo chip**: BPM, source and a dot that pulses on every beat.

## 5. Layout
Header 80 px (logo tile, preset pill, tempo chip). Reverb is 900×620: hero (mode + Space view), knob panel, tabbed side panel (Tempo / EQ / Mod). Riser is 920×650: hero (waveform + transport), then Source / Reverb / Shape / FX. It scales from 75% to 150%.

## 6. Depth
Panels have a 3-step soft shadow. Visualizers sit in black 35% insets with a top inner shadow.

## 7. Motion
Space view at 60 Hz: rings from input transients, speed from Size, fade from RT60, particles that breathe with the wet level, and a bar playhead. Waveform at 60 Hz: playhead, REC pulse, empty-state pulse. Segment thumbs slide. Everything else repaints only on change.

## 8. Do's and don'ts
- Do use the gradient only for state or interaction.
- Do keep the UI in English, with short labels.
- Do keep everything vector, crisp at any scale.
- Don't use blur filters (software renderer). Glow = stacked translucent strokes.
- Don't add a third accent hue (info blue is only for frozen/damping).
- Don't use emoji.
- Don't use pure #000 or #fff surfaces.
- Don't make knobs smaller than 44 px.

## 9. Verification
`SI_UI_SELFTEST=<dir>` makes each editor write PNG snapshots plus a layout lint (empty / outside / overlapping children). SITests sets `SI_UI_SELFTEST_AUDIO`, so the Riser snapshot also includes a rendered riser.
