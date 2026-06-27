# UpStage UI Usability Pass — Design

**Date:** 2026-06-27
**Status:** Approved for planning
**Author:** Alex + Claude

## Goal

Improve UI usability for live performance, targeting two real on-stage risks
("which channel am I touching?" and "did my scene save?") and surfacing power
features that are currently discoverable only by accident. Aesthetic stays
skeuomorphic / hardware-console (Studio One reference).

This pass covers eight items (#1–#8). A/B bookmarks, editable-label hinting, and
the help-overlay rewrite are explicitly out of scope.

## Scope & non-goals

- **In scope:** the eight items below, UI-layer only. No audio-engine changes.
- **Non-goals:** meter restyling (recently polished), new routing features,
  toolbar input-trim knob, changing any keyboard/MIDI behavior. We only *surface*
  existing behavior, never change what a gesture does.
- **Constraint:** the central timer already runs at ~30–60 Hz
  (`timerCallback`). New animations piggyback on it; no new timers unless noted.

---

## Item designs

### #1 — Unmistakable active-channel highlight

**Problem:** Active channel is shown only by a label tint
(`updateActiveIndicators`, MainComponent.cpp:3998) plus a near-invisible
`activeIndicators[]` button. Hard to read at a glance on stage.

**Design:** Draw a bright accent frame around the *entire active channel strip*
in `MainComponent::paint()`. The strip rectangles are already computed in
`resized()`; cache each channel strip's bounds into a member
`channelStripBounds[NUM_CHANNELS]` during `resized()`, then in `paint()` stroke a
2px rounded rect in the active-green (`0xff3a7040` family, slightly brighter) plus
a soft outer glow. Muted-but-active uses a red-tinted frame. Keep the existing
label tinting (it reinforces, doesn't replace).

**Why this approach:** Reuses existing geometry, no new components, scales with
the strip. One repaint path.

**Touches:** MainComponent.h (add `juce::Rectangle<int> channelStripBounds[NUM_CHANNELS]`),
MainComponent.cpp `resized()` (store bounds) and `paint()` (draw frame).

---

### #2 — Value-on-hover/drag readout for knobs

**Problem:** Pan, input-trim, direct, master knobs give no numeric feedback;
you turn blind. (Faders already have dB labels.)

**Design:** A lightweight shared readout. Add a `setTooltip`-style live value via
JUCE's existing `Slider` text: set each rotary's `textValueSuffix` and use a
`Label`-free approach — show value in a single reusable floating readout
`knobReadout` (a `Label` owned by MainComponent, normally hidden). On any
registered knob's `onДragStart`/`mouseEnter`, position `knobReadout` just above the
knob and show formatted text; hide on drag-end/exit. Formatting per knob type:
pan → `L20 / C / R35`, gains → `-3.5 dB`, direct → `42%`.

**Why this approach:** One shared label beats 11 always-on text boxes (which would
clutter the skeuomorphic faces). Hover + drag both trigger it.

**Touches:** MainComponent.h (`std::unique_ptr<juce::Label> knobReadout`, a helper
`showKnobReadout(juce::Component&, juce::String)`), constructor wiring on the knob
callbacks, MixerLookAndFeel unaffected.

---

### #3 — Routing mode made obvious

**Problem:** `routingModeButton` toggles between `>>` and `>`
(MainComponent.cpp:~2738) — a dramatic audio change shown by one character.

**Design:** Relabel to explicit text — `PARALLEL` vs `SINGLE` — and color-code:
parallel = teal (`0xff2a3a3a` bg already used), single = amber. Widen the button
slightly in `resized()` to fit the word. Add a tooltip explaining each mode.

**Touches:** MainComponent.cpp constructor (button text/colour), the toggle handler
(set text+colour on change), `resized()` (width). Trivial.

---

### #4 — Right-click affordance hints

**Problem:** Scene buttons, faders, looper, metronome, record button all have
context menus with no visible hint.

**Design:** Two-part, both low-risk:
1. A subtle corner glyph (a tiny 3-dot `⋮` or `▸` drawn in the LookAndFeel) on
   controls that have a right-click menu — scene buttons and the looper/metro
   transport buttons. Drawn dim so it reads as texture, not clutter.
2. Tooltips on those same controls naming the right-click action (faders already
   reference "Right-click for MIDI Learn"; extend the pattern to scenes, looper,
   metro, record).

**Why this approach:** Tooltips are zero-risk and immediately helpful; the glyph
is the discoverability nudge. We do glyphs only where menus are non-obvious
(scenes + transport), not on every control.

**Touches:** scene button creation + transport button setup (tooltips), and a
small paint addition. Could use a tiny helper rather than touching LookAndFeel
broadly.

---

### #5 — Hold-to-save scene countdown

**Problem:** Holding numpad 1–8 for 3s saves a scene (MainComponent.cpp:2320–2334),
but there's zero feedback during the hold — you don't know it's working.

**Design:** The timer already computes `heldMs` for the held scene each tick. Add a
member `heldSceneProgress` (0.0–1.0 = `heldMs / 3000`) and `heldSceneIndex`
(exists). In `paint()` (or a small overlay on the scene button), draw a radial/arc
fill or a bottom-up bar on that scene button reflecting progress; clear it on
release/commit. On commit, the existing `flashSceneButton` confirms.

**Why this approach:** Reuses the exact value the timer already has; purely additive
draw.

**Touches:** MainComponent.h (`float heldSceneProgress`), timer (set it), paint of
scene buttons (draw arc). Scene buttons are `TextButton`s in a row — we draw the
overlay in MainComponent::paint() over the button bounds, consistent with #1.

---

### #6 — MIDI-learn visual state

**Problem:** Any knob/fader is MIDI-learnable via right-click, and some are bound,
but nothing shows which are bound or that learning is armed.
`MidiLearnManager` already exposes `getBindings()`, `isLearning()`,
`getLearningParam()`.

**Design:**
- **Bound indicator:** small "MIDI" badge / blue dot drawn near each control whose
  `paramID` has a binding. Add a convenience `int getCcForParam(const juce::String&)
  const` (returns -1 if unbound) to MidiLearnManager so the UI can query + show the
  CC number in the tooltip ("CC 21, ch 1").
- **Learning state:** when `isLearning()` and `getLearningParam()` matches a
  control, that control pulses a highlight ("waiting for CC…"). We already implement
  `Listener::midiLearnParameterChanged`; add lightweight repaint when learn arms/
  disarms (MainComponent observes via the existing listener or a one-line callback).

**Why this approach:** The manager already holds all state; we add one read-only
helper and a draw. No behavior change to learning itself.

**Touches:** MidiLearnManager.h/.cpp (`getCcForParam`), MainComponent paint/tooltips,
a repaint trigger on learn arm/cancel.

---

### #7 — Scribble strips

**Problem:** Channel identity above the fader is just the channel label; very
hardware consoles show a "scribble strip." Strong Studio-One/hardware feel.

**Design:** A thin LCD-style strip rendered above each fader area (between the
knobs row and the fader dB label, or folded into the existing label) showing the
channel name and, space permitting, the first plugin's short name/nickname. Styled
as a recessed dark-LCD rectangle with a subtle backlit tint and small mono-ish
font. For the active channel, backlight brightens (ties into #1).

**Layout note:** `resized()` currently allocates: label(28) + plugins(230) +
knobs(65) + faderLabel(18) + meters/fader + buttons(24). The scribble strip can
reuse/extend the existing 28px label region (render name as LCD) rather than
consuming new vertical space — keeps the layout intact. First-plugin text is
optional/secondary.

**Why this approach:** Folding into the existing label region avoids a layout
rebalance and the risk that comes with it. Pure LookAndFeel/paint work.

**Touches:** channel label rendering (custom paint or a small `ScribbleStrip`
component replacing the plain `Label`), query first plugin name via existing
`ChannelStrip::getPlugin(0)->getName()` / nickname map.

---

### #8 — Master section visual prominence

**Problem:** The master/FX strip is the same width as a channel
(`stripWidth = width / (NUM_CHANNELS+2)`, MainComponent.cpp:1980) and reads as
"channel 5," despite holding all the master metering.

**Design (chosen): visual framing, not re-layout.** Keep equal widths (re-layout
is the risky option) but give the master strip a distinct treatment: a brighter
metallic bezel/frame, a clear "MASTER" header bar with accent color, and a subtle
divider separating it from the channel block. The FX bus panel already owns this
region (`fxBusPanel->setBounds(fxArea)`), so the framing is drawn in
FxBusPanel::paint() plus a divider in MainComponent::paint().

**Alternative considered & rejected:** widening the master strip (e.g. 1.4×) by
changing the width math — more "correct" hardware-wise but forces re-tuning every
strip's internal layout and the input strip; higher regression risk for modest
gain. Revisit later if framing isn't enough.

**Touches:** FxBusPanel::paint() (bezel/header), MainComponent::paint() (divider).

---

## Cross-cutting / shared pieces

- **`channelStripBounds[NUM_CHANNELS]`** cached in `resized()` — shared by #1, #5,
  #7 for drawing overlays at correct positions. Single source of truth.
- **A small "control registry"** mapping each learnable `Slider*`/knob to its
  `paramID` — needed by #2 (which knob am I) and #6 (is it bound). Likely already
  implicit in how right-click MIDI-learn finds the paramID; we formalize a lookup
  so #2/#6 reuse it. *(Plan step will confirm the existing mapping in mouseDown
  before adding anything.)*

## Build / verification

- Build after each item (or each small cluster) with the VS2022 MSBuild path used
  previously; launch the app and visually confirm. No automated UI tests exist;
  verification is build-clean + launch + visual check, per project norms.
- Risk order (do safest first): #3, #2, #1, #5, #4, #6, #8, #7. Scribble strips and
  master framing touch paint most heavily, so they go last when the shared
  `channelStripBounds` plumbing is already proven by #1/#5.

## Open questions (resolved)

- Layout vs framing for master → **framing** (lower risk).
- Scribble strip vertical space → **reuse existing label region** (no re-layout).
- New timers → **none**; reuse the existing timer tick.
