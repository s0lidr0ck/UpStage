# UI Usability Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make UpStage's UI safer and more readable for live performance and surface power features that are currently hidden, without changing any audio or gesture behavior.

**Architecture:** Eight UI-only improvements, drawn mostly in `MainComponent::paint()`/`resized()` and the relevant panels. A small amount of shared plumbing (cached strip bounds, a slider→paramID registry, a couple of read-only `MidiLearnManager` helpers) is built first so later tasks reuse it. No new timers — animations piggyback on the existing `timerCallback`.

**Tech Stack:** C++17, JUCE 7/8, Visual Studio 2022 (MSBuild), Windows. No unit-test framework — verification is **build-clean + launch + visual check**.

## Global Constraints

- **No behavior changes:** we only *surface* existing gestures; never change what a click/key/CC does. (Exception explicitly in scope: Task 9 adds a right-click "Learn MIDI" menu item that arms the already-existing `MidiLearnManager::beginLearning`, which currently has no UI trigger.)
- **No new timers:** reuse the existing `timerCallback` tick. (verbatim from spec)
- **Skeuomorphic aesthetic:** dark metallic / hardware-console look; new elements must match (recessed panels, subtle gradients, dim accents). (verbatim from spec)
- **Build command (verbatim):** `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:/projects/A18/UpStage/Builds/VisualStudio2022/UpStage.sln" -p:Configuration=Debug -p:Platform=x64 -m -v:minimal`
- **Risk order (verbatim from spec):** #3, #2, #1, #5, #4, #6, #8, #7. This plan sequences tasks accordingly, with shared plumbing folded into the first task that needs it.
- **Commit message footer (verbatim):** end every commit body with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

---

## File Structure

- `Source/MainComponent.h` — new members: `channelStripBounds[NUM_CHANNELS]`, `knobReadout`, `heldSceneProgress`, the slider→paramID registry, helper declarations.
- `Source/MainComponent.cpp` — most work: `resized()` (cache bounds, routing-pill width), `paint()` (active frame, scene countdown, master divider), constructor (button text/colours, tooltips, knob readout wiring, learn menu), `mouseDown()` (learn menu), `timerCallback()` (countdown progress).
- `Source/MidiLearnManager.h/.cpp` — read-only helpers `getCcForParam`, `getChannelForParam`; a learn-state-changed notification.
- `Source/FxBusPanel.cpp` — master bezel/header (Task 11).
- `Source/MixerLookAndFeel.h` — optional shared glyph helper for right-click affordance (Task 7).

A note on shared plumbing: Tasks 3 (cache bounds) and 4 (paramID registry) build reusable infrastructure consumed by later tasks. They are kept as their own tasks because each ends in an independently verifiable build.

---

## Task 1: Routing mode pill (#3)

**Files:**
- Modify: `Source/MainComponent.cpp` (routingModeButton creation in constructor; the toggle handler; `resized()` width)

**Interfaces:**
- Consumes: existing `juce::TextButton routingModeButton;` and `bool parallelRouting;` (already declared).
- Produces: nothing new for later tasks.

- [ ] **Step 1: Find the current routing button setup and toggle handler**

Run: search `routingModeButton` in `Source/MainComponent.cpp`. Current code sets text `">>"`/`">"` (around the toggle near line 2738) and a colour. Note both the constructor setup and the click/toggle site.

- [ ] **Step 2: Replace the text + colour in both the constructor and the toggle handler**

In the constructor where `routingModeButton` is configured, and in the handler that flips `parallelRouting`, replace the `>>`/`>` logic with explicit labels and distinct colours. Use this exact helper pattern at each site:

```cpp
// Parallel = all channels sum (teal); Single = only active channel (amber).
routingModeButton.setButtonText (parallelRouting ? "PARALLEL" : "SINGLE");
routingModeButton.setColour (juce::TextButton::buttonColourId,
    parallelRouting ? juce::Colour (0xff2a4a4a)   // teal
                    : juce::Colour (0xff4a3a1a));  // amber
routingModeButton.setTooltip (parallelRouting
    ? "Routing: PARALLEL - all channels play and sum to master. Click for SINGLE."
    : "Routing: SINGLE - only the active channel plays. Click for PARALLEL.");
```

- [ ] **Step 3: Widen the button in resized() so the word fits**

In `resized()`, find where `routingModeButton.setBounds(...)` is called within the transport row. It currently takes a ~24–28px square. Change the width to fit text, e.g. `transport.removeFromLeft (78)` (match the surrounding `removeFromLeft` style and spacing already used for labelled transport buttons). Keep height as-is.

- [ ] **Step 4: Build**

Run the Global-Constraints build command.
Expected: `UpStage_App.vcxproj -> ...UpStage.exe`, no `error C`/`error MSB`.

- [ ] **Step 5: Launch and visually verify**

Launch `Builds/VisualStudio2022/x64/Debug/App/UpStage.exe`. Confirm the routing button reads `PARALLEL`/`SINGLE`, toggles text+colour on click, and the tooltip appears. Close the app.

- [ ] **Step 6: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "UI: explicit PARALLEL/SINGLE routing pill

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Knob value readout (#2)

**Files:**
- Modify: `Source/MainComponent.h` (add readout label + helper decl)
- Modify: `Source/MainComponent.cpp` (create label, wire knob callbacks)

**Interfaces:**
- Consumes: existing rotary sliders `inputTrimKnobs[NUM_CHANNELS]`, `outputGainKnobs[NUM_CHANNELS]` (pan), `inputDirectKnob`, and the master fader (in FxBusPanel — skip master here; this task covers the MainComponent-owned knobs).
- Produces: `void showKnobReadout (juce::Component& near, const juce::String& text);` and `void hideKnobReadout();` — reused by no later task (self-contained).

- [ ] **Step 1: Add members to MainComponent.h**

In the private members of `MainComponent.h`, add:

```cpp
// Shared floating value readout shown while turning a knob (#2 usability).
std::unique_ptr<juce::Label> knobReadout;
void showKnobReadout (juce::Component& nearComp, const juce::String& text);
void hideKnobReadout();
```

- [ ] **Step 2: Create the readout label in the constructor**

In the `MainComponent` constructor (after other component setup), add:

```cpp
knobReadout = std::make_unique<juce::Label>();
knobReadout->setJustificationType (juce::Justification::centred);
knobReadout->setColour (juce::Label::backgroundColourId, juce::Colour (0xee101010));
knobReadout->setColour (juce::Label::textColourId,       juce::Colour (0xffd0f0ff));
knobReadout->setColour (juce::Label::outlineColourId,    juce::Colour (0xff3a3a3a));
knobReadout->setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
knobReadout->setInterceptsMouseClicks (false, false);
knobReadout->setVisible (false);
addChildComponent (knobReadout.get());  // child but hidden until shown
```

- [ ] **Step 3: Implement show/hide helpers**

Add these definitions in `MainComponent.cpp`:

```cpp
void MainComponent::showKnobReadout (juce::Component& nearComp, const juce::String& text)
{
    if (knobReadout == nullptr) return;
    knobReadout->setText (text, juce::dontSendNotification);
    auto b = getLocalArea (&nearComp, nearComp.getLocalBounds());
    const int w = 56, h = 18;
    knobReadout->setBounds (b.getCentreX() - w / 2,
                            b.getY() - h - 2,   // just above the knob
                            w, h);
    knobReadout->setVisible (true);
    knobReadout->toFront (false);
}

void MainComponent::hideKnobReadout()
{
    if (knobReadout != nullptr) knobReadout->setVisible (false);
}
```

- [ ] **Step 4: Wire the knob callbacks**

For each MainComponent-owned rotary, set drag-start/end and the existing value change to drive the readout. Add after each knob's existing setup (pan knobs `outputGainKnobs[i]`, trim knobs `inputTrimKnobs[i]`, and `inputDirectKnob`). Example for the per-channel knobs inside their setup loop:

```cpp
// Pan readout (outputGainKnobs[i] is the pan control)
outputGainKnobs[i].onDragStart = [this, i] {
    showKnobReadout (outputGainKnobs[i], panText (outputGainKnobs[i].getValue())); };
outputGainKnobs[i].onValueChange = [this, i] {
    /* keep existing body if any, then: */
    showKnobReadout (outputGainKnobs[i], panText (outputGainKnobs[i].getValue())); };
outputGainKnobs[i].onDragEnd = [this] { hideKnobReadout(); };

// Input trim readout (-24..+24 dB)
inputTrimKnobs[i].onDragStart = [this, i] {
    showKnobReadout (inputTrimKnobs[i], juce::String (inputTrimKnobs[i].getValue(), 1) + " dB"); };
inputTrimKnobs[i].onDragEnd = [this] { hideKnobReadout(); };
```

If a knob already has an `onValueChange`, append the `showKnobReadout` call to the existing lambda body rather than overwriting it. For `inputDirectKnob` use `juce::String (juce::roundToInt (inputDirectKnob.getValue() * 100.0)) + "%"`.

- [ ] **Step 5: Add the panText helper**

Add a small static helper near the top of `MainComponent.cpp`:

```cpp
static juce::String panText (double v)
{
    int p = juce::roundToInt (v * 100.0);
    if (p == 0)  return "C";
    return (p < 0 ? "L" : "R") + juce::String (std::abs (p));
}
```

- [ ] **Step 6: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 7: Launch and verify**

Launch the app. Turn a pan knob, a trim knob, the direct knob — confirm a small dark readout appears above the knob with `L20/C/R35`, `-3.5 dB`, `42%` respectively, and disappears on release. Close.

- [ ] **Step 8: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "UI: floating value readout while turning knobs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Cache channel strip bounds + active-channel frame (#1)

**Files:**
- Modify: `Source/MainComponent.h` (add `channelStripBounds`)
- Modify: `Source/MainComponent.cpp` (`resized()` store bounds; `paint()` draw frame)

**Interfaces:**
- Consumes: `int activeChannel;`, `bool channelMuted[NUM_CHANNELS];` (existing).
- Produces: `juce::Rectangle<int> channelStripBounds[NUM_CHANNELS];` populated in `resized()` — **reused by Task 5 (scene countdown is on scene buttons, not strips — independent) and Task 12 (scribble strips)**. Specifically Task 12 reads these bounds.

- [ ] **Step 1: Add the member to MainComponent.h**

```cpp
// Screen bounds of each channel strip, cached in resized() for paint-time
// overlays (active-channel frame #1, scribble strips #7).
juce::Rectangle<int> channelStripBounds[NUM_CHANNELS];
```

- [ ] **Step 2: Populate in resized()**

In `resized()`, inside the `for (int i = 0; i < NUM_CHANNELS; ++i)` strip loop, capture the strip rectangle right after it is computed. The loop starts with `auto strip = area.removeFromLeft (stripWidth).reduced (stripPadding);`. Immediately after that line add:

```cpp
channelStripBounds[i] = area.removeFromLeft (0).withX (strip.getX() - stripPadding)
                            .withWidth (stripWidth).withY (strip.getY() - 28)
                            .withHeight (strip.getHeight() + 28);
```

Simpler and less error-prone: capture the full pre-reduced strip. Replace the loop's first line with:

```cpp
auto fullStrip = area.removeFromLeft (stripWidth);
channelStripBounds[i] = fullStrip;
auto strip = fullStrip.reduced (stripPadding);
```

Use the second form. (It records the exact column the strip occupies.)

- [ ] **Step 3: Draw the active frame in paint()**

In `MainComponent::paint()`, after the existing strip background drawing, add:

```cpp
// Active-channel frame (#1): unmistakable highlight on the selected strip.
if (juce::isPositiveAndBelow (activeChannel, NUM_CHANNELS))
{
    auto r = channelStripBounds[activeChannel].toFloat().reduced (2.0f);
    const bool muted = channelMuted[activeChannel];
    juce::Colour accent = muted ? juce::Colour (0xffcc4444)
                                : juce::Colour (0xff5fd06a);
    // soft outer glow
    g.setColour (accent.withAlpha (0.18f));
    g.drawRoundedRectangle (r.expanded (2.0f), 6.0f, 4.0f);
    // crisp inner frame
    g.setColour (accent.withAlpha (0.95f));
    g.drawRoundedRectangle (r, 6.0f, 2.0f);
}
```

- [ ] **Step 4: Ensure paint() runs after channel changes**

Confirm `updateActiveIndicators()` already calls `repaint()` (it does, at its end). No change needed — switching channels repaints. Verify by reading the end of `updateActiveIndicators()`.

- [ ] **Step 5: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 6: Launch and verify**

Launch. Confirm the active channel has a clear green frame; switch channels with keys 1–4 and clicking labels — frame follows. Mute the active channel — frame turns red. Close.

- [ ] **Step 7: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "UI: cache strip bounds; draw active-channel frame

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Scene-save hold countdown (#5)

**Files:**
- Modify: `Source/MainComponent.h` (add `heldSceneProgress`)
- Modify: `Source/MainComponent.cpp` (`timerCallback()` set progress; `paint()` draw arc; clear on release)

**Interfaces:**
- Consumes: existing `int heldSceneIndex;`, `juce::uint32 holdStartMs;`, `juce::TextButton sceneButtons[NUM_SCENES];`, and the existing timer block at ~line 2320.
- Produces: nothing for later tasks.

- [ ] **Step 1: Add the member**

In `MainComponent.h`:

```cpp
// 0..1 progress of the current numpad scene-save hold (#5). -1 idle handled
// via heldSceneIndex < 0.
float heldSceneProgress = 0.0f;
```

- [ ] **Step 2: Set progress in timerCallback()**

In the existing `timerCallback()` block that handles `heldSceneIndex >= 0` (around line 2320, where `heldMs` is computed), set progress before the existing `if (heldMs >= 3000)` branch:

```cpp
heldSceneProgress = juce::jlimit (0.0f, 1.0f, (float) heldMs / 3000.0f);
```

In the branches that end the hold (the `heldMs >= 3000` save branch and the key-released branch that sets `heldSceneIndex = -1`), add `heldSceneProgress = 0.0f;`. Ensure a `repaint();` happens each tick while holding — add `repaint();` inside the `heldSceneIndex >= 0` block if not already covered by the timer's existing repaint.

- [ ] **Step 3: Draw the arc in paint()**

In `MainComponent::paint()`, after scene buttons are otherwise handled (they paint themselves; we overlay), add:

```cpp
// Scene-save hold countdown (#5): radial fill over the held scene button.
if (heldSceneIndex >= 0 && heldSceneIndex < NUM_SCENES && heldSceneProgress > 0.0f)
{
    auto b = sceneButtons[heldSceneIndex].getBounds().toFloat();
    juce::Path arc;
    auto centre = b.getCentre();
    float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    arc.addPieSegment (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f,
                       0.0f,
                       juce::MathConstants<float>::twoPi * heldSceneProgress,
                       0.0f);
    g.setColour (juce::Colour (0xff88ff88).withAlpha (0.55f));
    g.fillPath (arc);
}
```

Note: `sceneButtons[i].getBounds()` are in this component's coordinate space already (they are direct children laid out in `resized()`), so no `getLocalArea` conversion is needed.

- [ ] **Step 4: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 5: Launch and verify**

Launch. Hold a numpad 1–8 key; confirm a green pie fills over that scene button across ~3s, then the button flashes (existing save confirm). Tap-and-release quickly: arc should not complete and scene should recall (existing behavior). Close.

- [ ] **Step 6: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "UI: radial countdown while holding to save a scene

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Right-click affordance — tooltips (#4 part 1)

**Files:**
- Modify: `Source/MainComponent.cpp` (constructor: add tooltips to controls with right-click menus)

**Interfaces:**
- Consumes: `sceneButtons[]`, `loopRecButton`, `metronomeButton` (confirm exact name), `recordButton`, `stopRecordButton` (existing).
- Produces: nothing.

- [ ] **Step 1: Confirm the metronome button member name**

Run: search `metronome` button setup in the constructor (the toggle whose right-click opens the BPM/meter menu). Note its exact variable name (e.g. `metronomeButton` or `metroButton`).

- [ ] **Step 2: Add tooltips in the constructor**

Where each control is set up, add a `setTooltip` describing the right-click action:

```cpp
for (int i = 0; i < NUM_SCENES; ++i)
    sceneButtons[i].setTooltip ("Click: recall scene. Right-click: save / rename / clear. "
                                "Hold numpad 1-8 (3s): save.");

loopRecButton.setTooltip ("Click: cycle record/overdub/play. Double-click: overdub. "
                          "Right-click: count-in, meter, length, capture, export.");
recordButton.setTooltip ("Record dry + wet to disk. Right-click: reveal recordings folder.");
stopRecordButton.setTooltip ("Stop recording. Right-click: reveal recordings folder.");
// Use the exact metronome button name confirmed in Step 1:
<metronomeButtonName>.setTooltip ("Click: toggle metronome. Right-click: BPM, time sig, "
                                  "subdivision, sound, volume.");
```

- [ ] **Step 3: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 4: Launch and verify**

Launch. Hover scenes, looper, metro, record buttons; confirm tooltips appear naming the right-click actions. Close.

- [ ] **Step 5: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "UI: tooltips naming right-click actions on scenes/transport

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Right-click affordance — glyph (#4 part 2)

**Files:**
- Modify: `Source/MainComponent.cpp` (`paint()`: draw a dim corner glyph on scene buttons)

**Interfaces:**
- Consumes: `sceneButtons[]` bounds.
- Produces: nothing.

- [ ] **Step 1: Draw the glyph in paint()**

In `MainComponent::paint()`, add a loop drawing a tiny dim three-dot mark in the top-right corner of each scene button to hint "has a menu":

```cpp
// Right-click affordance hint (#4): subtle corner marker on scene buttons.
g.setColour (juce::Colours::white.withAlpha (0.18f));
for (int i = 0; i < NUM_SCENES; ++i)
{
    auto b = sceneButtons[i].getBounds().toFloat();
    float x = b.getRight() - 6.0f;
    float y = b.getY() + 3.0f;
    for (int d = 0; d < 3; ++d)
        g.fillEllipse (x, y + d * 2.2f, 1.4f, 1.4f);  // vertical 3-dot
}
```

- [ ] **Step 2: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 3: Launch and verify**

Launch. Confirm a faint vertical 3-dot mark sits in the corner of each scene button — visible but not distracting against the skeuomorphic background. Adjust alpha if too strong/weak. Close.

- [ ] **Step 4: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "UI: subtle 3-dot right-click hint on scene buttons

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: MidiLearnManager read-only helpers (#6 plumbing)

**Files:**
- Modify: `Source/MidiLearnManager.h` (declare helpers)
- Modify: `Source/MidiLearnManager.cpp` (implement)

**Interfaces:**
- Consumes: existing `juce::Array<Binding> bindings;`, `mutable juce::CriticalSection lock;` (lock added in the prior stability pass).
- Produces:
  - `int getCcForParam (const juce::String& paramID) const;` — returns CC number 0–127, or `-1` if unbound.
  - `int getChannelForParam (const juce::String& paramID) const;` — returns MIDI channel, or `-1` if unbound.

- [ ] **Step 1: Declare in MidiLearnManager.h**

In the public Bindings section (near `getBindings()`):

```cpp
/** CC number bound to paramID, or -1 if not bound. Thread-safe. */
int getCcForParam (const juce::String& paramID) const;
/** MIDI channel bound to paramID (1-16), or -1 if not bound. Thread-safe. */
int getChannelForParam (const juce::String& paramID) const;
```

- [ ] **Step 2: Implement in MidiLearnManager.cpp**

```cpp
int MidiLearnManager::getCcForParam (const juce::String& paramID) const
{
    juce::ScopedLock sl (lock);
    for (const auto& b : bindings)
        if (b.paramID == paramID)
            return b.ccNumber;
    return -1;
}

int MidiLearnManager::getChannelForParam (const juce::String& paramID) const
{
    juce::ScopedLock sl (lock);
    for (const auto& b : bindings)
        if (b.paramID == paramID)
            return b.midiChannel;
    return -1;
}
```

(Confirm the `Binding` field names `ccNumber` and `midiChannel` by reading the struct in `MidiLearnManager.h` first; they were referenced in the prior stability pass's save/load. Adjust if different.)

- [ ] **Step 3: Build**

Run the build command. Expected: links, no errors. (No visual change yet.)

- [ ] **Step 4: Commit**

```bash
git add Source/MidiLearnManager.h Source/MidiLearnManager.cpp
git commit -m "MidiLearn: add read-only getCcForParam/getChannelForParam

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Slider→paramID registry (#2/#6 plumbing)

**Files:**
- Modify: `Source/MainComponent.h` (registry member + helper decl)
- Modify: `Source/MainComponent.cpp` (populate after knob setup)

**Background (verified):** `beginLearning()` exists but is **not wired to any UI** — the "MIDI Learn handled in mouseDown" comments are stale. The registry built here is what Task 9 (learn menu) and Task 10 (badges) need to map a control to its registered paramID. ParamIDs already registered in the constructor: `loopVolume`, `gateThresh`, `inputTrim`, `chFader{i}`, `chPan{i}`, `chMute{i}`, `masterFader`, `fxBypass`, `metroToggle`.

**Interfaces:**
- Consumes: rotary/slider members, the registered paramID strings above.
- Produces: `std::map<juce::Component*, juce::String> learnableControls;` and `juce::String paramIdForComponent (juce::Component*) const;` (returns empty if none).

- [ ] **Step 1: Add members to MainComponent.h**

```cpp
// Maps a learnable on-screen control to its MidiLearnManager paramID (#2/#6).
std::map<juce::Component*, juce::String> learnableControls;
juce::String paramIdForComponent (juce::Component* c) const;
```

- [ ] **Step 2: Populate after the knobs/sliders are constructed**

At the end of the constructor (after all knob setup and `registerParameter` calls), add:

```cpp
learnableControls[&inputTrimSlider] = "inputTrim";
learnableControls[&loopVolumeSlider] = "loopVolume";
learnableControls[&gateThreshSlider] = "gateThresh";
for (int i = 0; i < NUM_CHANNELS; ++i)
{
    learnableControls[&outputFaders[i]]    = "chFader" + juce::String (i);
    learnableControls[&outputGainKnobs[i]] = "chPan"   + juce::String (i);
}
// masterFader / fxBypass live in FxBusPanel; covered there if needed later.
```

(Use the exact slider member names; confirm `gateThreshSlider`, `loopVolumeSlider`, `inputTrimSlider`, `outputFaders[]`, `outputGainKnobs[]` by reading the header. `chMute{i}`/`metroToggle` are buttons — include only if they are distinct learnable Components.)

- [ ] **Step 3: Implement the lookup**

```cpp
juce::String MainComponent::paramIdForComponent (juce::Component* c) const
{
    auto it = learnableControls.find (c);
    return it != learnableControls.end() ? it->second : juce::String();
}
```

- [ ] **Step 4: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 5: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "UI: registry mapping learnable controls to paramIDs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Right-click "Learn MIDI" menu (#6 trigger)

**Files:**
- Modify: `Source/MainComponent.cpp` (`mouseDown()`: add a learn/clear menu for registered controls)

**Interfaces:**
- Consumes: `paramIdForComponent()` (Task 8), `midiLearnManager.beginLearning(paramID)`, `midiLearnManager.clearBinding(paramID)`, `midiLearnManager.getCcForParam(paramID)` (Task 7).
- Produces: nothing.

- [ ] **Step 1: Add a learn menu branch in mouseDown()**

In `MainComponent::mouseDown()`, after the existing fader colour / record / looper right-click branches, add a generic right-click handler for any registered learnable control. Place it so the existing fader colour-picker (which also right-clicks `outputFaders[i]`) still wins for faders — i.e. show a combined menu for faders, or gate the learn menu behind a modifier. Simplest non-conflicting approach: only attach the learn menu to controls **not** already claimed (the pan/trim knobs and gateThresh/loopVolume sliders), and add a "Learn MIDI" item to the fader colour menu separately. Implement the standalone branch:

```cpp
if (e.mods.isRightButtonDown())
{
    auto pid = paramIdForComponent (e.eventComponent);
    if (pid.isNotEmpty())
    {
        const int existingCc = midiLearnManager.getCcForParam (pid);
        juce::PopupMenu m;
        m.addItem (1, midiLearnManager.isLearning() && midiLearnManager.getLearningParam() == pid
                        ? "Listening for CC..." : "Learn MIDI");
        m.addItem (2, "Clear MIDI binding", existingCc >= 0);
        m.showMenuAsync ({}, [this, pid] (int r)
        {
            if (r == 1) { midiLearnManager.beginLearning (pid); repaint(); }
            else if (r == 2) { midiLearnManager.clearBinding (pid); repaint(); }
        });
        return;
    }
}
```

- [ ] **Step 2: Add "Learn MIDI" to the fader colour menu**

Faders already open `showKnobColorMenu` on right-click. To avoid losing learn access on faders, read `showKnobColorMenu` and add two items ("Learn MIDI" / "Clear MIDI binding") that call `beginLearning`/`clearBinding` using `paramIdForComponent(component)`. If `showKnobColorMenu` doesn't receive enough context, instead change the fader right-click branch in `mouseDown` to show a small combined menu (colour submenu + learn items). Keep behavior: colour still reachable.

- [ ] **Step 3: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 4: Launch and verify**

Launch. Right-click a pan knob → "Learn MIDI"; click it, wiggle a MIDI CC (if a controller is connected) → binding is created; right-click again shows "Clear MIDI binding" enabled. Right-click a fader → colour options still present plus learn. Close. (If no controller is available, verify the menu items appear and "Listening for CC..." shows while armed.)

- [ ] **Step 5: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "UI: right-click Learn MIDI / Clear binding on controls

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: MIDI-learn badges + listening pulse (#6 visuals)

**Files:**
- Modify: `Source/MainComponent.cpp` (`paint()`: badge on bound controls, pulse on armed control)
- Modify: `Source/MainComponent.h` (only if a pulse-phase member is needed)

**Interfaces:**
- Consumes: `learnableControls` (Task 8), `getCcForParam`/`getChannelForParam` (Task 7), `isLearning()`/`getLearningParam()`.
- Produces: nothing.

- [ ] **Step 1: Draw badges in paint()**

In `MainComponent::paint()`, iterate the registry and mark bound controls:

```cpp
// MIDI-learn state (#6): badge on bound controls; pulse on the armed one.
for (const auto& kv : learnableControls)
{
    auto* comp = kv.first;
    if (comp == nullptr || ! comp->isVisible()) continue;
    auto area = getLocalArea (comp, comp->getLocalBounds()).toFloat();

    const bool armed = midiLearnManager.isLearning()
                     && midiLearnManager.getLearningParam() == kv.second;
    if (armed)
    {
        g.setColour (juce::Colour (0xffffaa00).withAlpha (0.85f));
        g.drawRoundedRectangle (area.reduced (1.0f), 4.0f, 2.0f);
    }
    else if (midiLearnManager.getCcForParam (kv.second) >= 0)
    {
        // small blue "MIDI" dot in the control's top-left
        g.setColour (juce::Colour (0xff3a78ff).withAlpha (0.9f));
        g.fillEllipse (area.getX() + 2.0f, area.getY() + 2.0f, 5.0f, 5.0f);
    }
}
```

- [ ] **Step 2: Add CC info to tooltips for bound controls**

After populating `learnableControls` (or in `timerCallback` on a low cadence to avoid churn), set tooltips on bound controls, e.g.:

```cpp
// In timerCallback, throttled (e.g. every ~15 ticks) to reflect new bindings:
for (const auto& kv : learnableControls)
{
    int cc = midiLearnManager.getCcForParam (kv.second);
    if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (kv.first))
        s->setTooltip (cc >= 0
            ? "MIDI: CC " + juce::String (cc) + " ch "
                + juce::String (midiLearnManager.getChannelForParam (kv.second))
            : juce::String());
}
```

Keep this cheap; do not allocate per tick beyond these short strings, and gate with a counter so it runs a few times per second at most.

- [ ] **Step 3: Repaint while armed**

The existing timer repaints regularly; confirm the armed frame animates/refreshes. If `paint` isn't called while idle-armed, add `if (midiLearnManager.isLearning()) repaint();` in `timerCallback`.

- [ ] **Step 4: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 5: Launch and verify**

Launch a project that has MIDI bindings (or create one via Task 9). Confirm bound controls show a blue dot and a "MIDI: CC x ch y" tooltip; arming a control draws an amber frame that clears once learned/cancelled. Close.

- [ ] **Step 6: Commit**

```bash
git add Source/MainComponent.cpp Source/MainComponent.h
git commit -m "UI: MIDI-learn badges and armed-control pulse

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Master section framing (#8)

**Files:**
- Modify: `Source/FxBusPanel.cpp` (`paint()`: bezel + MASTER header accent)
- Modify: `Source/MainComponent.cpp` (`paint()`: divider line left of the master strip)

**Interfaces:**
- Consumes: FxBusPanel's own bounds; `channelStripBounds` is not required (master strip is the rightmost `fxArea`).
- Produces: nothing.

- [ ] **Step 1: Read FxBusPanel::paint() and the MASTER header**

Open `Source/FxBusPanel.cpp`; locate `paint()` and where the "MASTER" label/header is drawn. Note the existing background treatment so the bezel matches.

- [ ] **Step 2: Add a distinct bezel + header accent in FxBusPanel::paint()**

At the start of `paint()` (before content), draw a brighter metallic frame and an accent header bar:

```cpp
auto full = getLocalBounds().toFloat();
// Brighter bezel to distinguish master from channel strips (#8).
g.setColour (juce::Colour (0xff3a3a42));
g.drawRoundedRectangle (full.reduced (1.5f), 5.0f, 2.0f);
// Accent header band along the top.
auto header = full.removeFromTop (24.0f).reduced (3.0f, 2.0f);
g.setGradientFill (juce::ColourGradient (
    juce::Colour (0xff4a3a5a), header.getX(), header.getY(),
    juce::Colour (0xff2a2230), header.getX(), header.getBottom(), false));
g.fillRoundedRectangle (header, 3.0f);
```

Ensure existing MASTER text still renders on top (draw order: this header band first, then the existing label paint). If the panel already draws its own header, only add the bezel and accent gradient, not a duplicate label.

- [ ] **Step 3: Draw a divider in MainComponent::paint()**

In `MainComponent::paint()`, draw a recessed vertical groove just left of the master strip. The master area is the rightmost `stripWidth`. Compute its left edge from the same geometry used in `resized()`:

```cpp
// Divider separating the channel block from the master section (#8).
if (fxBusPanel != nullptr)
{
    auto mb = fxBusPanel->getBounds();
    float x = (float) mb.getX() - 2.0f;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawVerticalLine ((int) x, (float) mb.getY(), (float) mb.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawVerticalLine ((int) x + 1, (float) mb.getY(), (float) mb.getBottom());
}
```

- [ ] **Step 4: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 5: Launch and verify**

Launch. Confirm the master/FX strip now has a brighter frame + accent header and a groove separating it from CH4 — it reads as a distinct section, not "channel 5". Close.

- [ ] **Step 6: Commit**

```bash
git add Source/FxBusPanel.cpp Source/MainComponent.cpp
git commit -m "UI: frame and divider to distinguish the master section

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: Scribble strips (#7)

**Files:**
- Modify: `Source/MainComponent.cpp` (`paint()`: LCD-style name over each channel label region)
- Possibly modify: `Source/MainComponent.cpp` constructor (style `channelLabels[i]` as LCD) — choose ONE rendering path (see Step 1).

**Interfaces:**
- Consumes: `channelStripBounds` (Task 3), `channelLabels[]`, `channels[i]->getNumPlugins()` / `getPlugin(0)` for the first plugin name, the nickname/appearance map if accessible.
- Produces: nothing.

- [ ] **Step 1: Choose the rendering path**

The spec folds the scribble strip into the existing 28px label region (no re-layout). Decision: **restyle `channelLabels[i]` as the LCD** (channel name) and draw the optional first-plugin subtext beneath it within the same 28px band via `paint()`. This avoids adding a component. Read the constructor setup of `channelLabels[i]` and `updateActiveIndicators()` (which sets label colours) so the LCD styling cooperates with active/mute tinting rather than fighting it.

- [ ] **Step 2: Restyle the channel label as backlit LCD**

In the constructor where `channelLabels[i]` are set up, give them an LCD feel (small, slightly monospaced, recessed). Keep `updateActiveIndicators()` in control of the background/text *colours* (active/mute), but set font and justification here:

```cpp
channelLabels[i].setFont (juce::Font (juce::FontOptions().withHeight (14.0f)
                                                         .withStyle ("Bold")));
channelLabels[i].setJustificationType (juce::Justification::centred);
```

- [ ] **Step 3: Draw first-plugin subtext in paint()**

In `MainComponent::paint()`, using `channelStripBounds[i]`, draw a dim secondary line just under the label area showing the first plugin name (or "—"). Compute the label band from the cached bounds (label occupies the top 28px of the strip per `resized()`):

```cpp
g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
for (int i = 0; i < NUM_CHANNELS; ++i)
{
    auto strip = channelStripBounds[i];
    juce::String fx = "—";
    if (channels[i]->getNumPlugins() > 0)
        if (auto* p = channels[i]->getPlugin (0))
            fx = p->getName();
    // place subtext just below the 28px label band
    juce::Rectangle<int> sub (strip.getX() + 4, strip.getY() + 28,
                              strip.getWidth() - 8, 11);
    g.setColour (juce::Colour (0xff66ccaa).withAlpha (0.5f));
    g.drawText (fx, sub, juce::Justification::centred, true);
}
```

Note: `getPlugin(0)` acquires `chainLock` internally (confirmed in the stability pass) and returns a raw pointer valid only briefly — call it on the message thread (paint is message thread) and use immediately, as done here. Do not store the pointer.

- [ ] **Step 4: Verify label band offset matches resized()**

Confirm in `resized()` that the label is the top 28px of the strip and that 2px gaps mean the subtext at `+28` doesn't overlap the plugin panel (plugin panel starts at label(28)+2). If it overlaps, reduce subtext height or move it into the 2px gap; adjust the `+28`/height to fit cleanly without colliding with `channelStripPanels[i]`.

- [ ] **Step 5: Build**

Run the build command. Expected: links, no errors.

- [ ] **Step 6: Launch and verify**

Launch. Confirm each channel shows its name LCD-style with a dim first-plugin line beneath; add/remove a plugin and confirm the subtext updates (it repaints via the timer). Active channel's label brightens (from Task 3 frame + existing tint). Close.

- [ ] **Step 7: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "UI: scribble-strip channel name + first-plugin subtext

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review notes

- **Spec coverage:** #1→Task 3, #2→Task 2, #3→Task 1, #4→Tasks 5+6, #5→Task 4, #6→Tasks 7+8+9+10, #7→Task 12, #8→Task 11. All eight covered.
- **Discovery captured:** `beginLearning` was never UI-wired (stale comments); Task 9 adds the trigger so #6's "listening" state is reachable. This is a noted, in-scope behavior addition (surfacing existing capability).
- **Shared plumbing:** `channelStripBounds` (Task 3) reused by Task 12; `learnableControls` registry (Task 8) + manager helpers (Task 7) reused by Tasks 9–10. Built before consumers.
- **Type consistency:** helper names used consistently — `showKnobReadout/hideKnobReadout`, `paramIdForComponent`, `getCcForParam/getChannelForParam`, `panText`, member `heldSceneProgress`, `channelStripBounds`.
- **Verification:** every task ends with build + launch + visual check (no test framework exists); this is the documented project norm.
- **Confirm-before-coding flags:** metronome button name (Task 5 Step 1), `Binding` field names (Task 7 Step 2), exact slider member names (Task 8 Step 2), label band geometry (Task 12 Step 4) — each task instructs the implementer to read the real code first.
