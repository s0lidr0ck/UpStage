# UpStage 🎸

A custom Windows desktop audio performance app. Think Amplitube + Bome MIDI Translator + a lightweight DAW.

---

## Features

- **4 Channel Strips** — each holds an unlimited VST3 plugin chain (your effects)
- **1 Input Source** routed to whichever channel is active
  - Live guitar via ASIO (low latency)
  - Loop file player (WAV/MP3/AIFF) for tone-tweaking without playing
- **MIDI Translator** — map CC/PC/Note messages to any other type (bi-directional)
- **Channel Switching** — click UI tabs or send a MIDI PC message (PC 0–3 = Ch 1–4)
- **Recording** — capture dry guitar, wet processed output, or both simultaneously as timestamped WAV files
- **Project Files (.upstage)** — saves everything: VST3 chains, all plugin states, MIDI rules, audio settings, loop file path

---

## Getting Started

### 1. Prerequisites
- **Windows 10/11**
- **Visual Studio 2022** (Community is free) — [download](https://visualstudio.microsoft.com/)
- **JUCE** — [download](https://juce.com/get-juce/) and extract somewhere (e.g. `C:\JUCE`)
- Your ASIO driver installed (comes with your audio interface)

### 2. Open in Projucer
1. Open **Projucer** (inside the JUCE folder)
2. Open `UpStage.jucer`
3. In Projucer → **Global Paths** → set "Path to JUCE" to your JUCE folder
4. Click **Save and Open in IDE** — this generates the Visual Studio 2022 project

### 3. Build
- In Visual Studio, select **Debug** or **Release** → **Build Solution**
- First build takes a few minutes while it compiles JUCE modules

### 4. First Run
1. Launch UpStage
2. Go to **Settings → ASIO Device Settings** → select your audio interface
3. Set input channel (guitar), output channels (L/R), sample rate, buffer size
4. Go to **Settings → Scan for VST3 Plugins** — finds all plugins in:
   - `C:\Program Files\Common Files\VST3`
   - `C:\Program Files (x86)\Common Files\VST3`
5. Click **+ Add VST3** on a channel strip to load a plugin
6. Click the plugin name to open its editor

---

## Project File Format

Projects are saved as `.upstage` (XML). Contains:
- Project name and active channel
- Per-channel: plugin list + full plugin state blobs (base64-encoded)
- MIDI translation rules
- ASIO device settings
- Loop file path and recorder settings

---

## MIDI Channel Switching

By default:
- MIDI PC 0 → Channel 1 active
- MIDI PC 1 → Channel 2 active
- MIDI PC 2 → Channel 3 active
- MIDI PC 3 → Channel 4 active

Use the MIDI Translator to remap anything else to these PC messages.

---

## Architecture

```
Source/
├── Main.cpp             — App entry point
├── MainComponent.h/.cpp — Top-level UI + audio/MIDI host
├── ChannelStrip.h/.cpp  — VST3 chain per channel + state serialization
├── MidiTranslator.h/.cpp — MIDI rules engine (CC/PC/Note translation)
├── InputRouter.h/.cpp   — Guitar live input OR loop file player
├── Recorder.h/.cpp      — Dual-stream WAV capture (dry + wet)
└── ProjectState.h/.cpp  — Save/load .upstage project files
```

---

## Roadmap / Next Steps

- [ ] Drag-to-reorder plugins within a channel strip
- [ ] Per-channel level meters
- [ ] MIDI-triggered record start/stop
- [ ] Loop file scrubber / position display
- [ ] Preset scenes (multiple saved channel states, switch by PC)
- [ ] Tuner panel
- [ ] CLAP plugin support (via clap-juce-extensions)
- [ ] Dark/light theme toggle
