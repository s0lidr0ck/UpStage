# NAM A2 Amp Library — Design

**Date:** 2026-07-21
**Status:** Approved concept, not yet planned/implemented

## Concept

A Neural Amp Modeler (NAM) Architecture 2 amp modeler built directly into UpStage — no third-party plugin. It is backed by a local amp library the user fills manually: download a capture from tone3000.com in a web browser, drag it into UpStage, attach a picture and tags, and it becomes a rig card. An amp is a row in a channel's plugin chain, so every channel can run its own amp (or none, or several).

Explicitly out of scope for v1: Tone3000 account sync / API integration, pre-packaged model bundles, A1 model support, training.

## Engine

- **Library:** NeuralAmpModelerCore v0.5.4+ (MIT), embedded as an internal DSP processor.
- **A2 only.** One parser path; enable the `NAM_ENABLE_A2_FAST` optimized path. Importing an A1 file fails with a clear "A2 models only" message pointing at Tone3000's A2 badge.
- **Slimmable width:** default A2-Full; per-instance Full/Lite switch for CPU headroom during shows.
- **Real-time safety:** model loading allocates (Eigen buffers). All loads happen on a background thread; the finished engine instance is swapped into the audio path atomically and the old one disposed off the audio thread — same discipline as the existing chain-mutation rule (see plugin lifetime model in ChannelStrip/FxBus).
- **Build:** handle the documented Eigen/MSVC alignment sharp edge (`EIGEN_MAX_ALIGN_BYTES 0` / `EIGEN_DONT_VECTORIZE` as needed, measured before accepting the perf cost).

## Library on disk

`Documents/UpStage/Amp Library/` owned by UpStage.

- **Rig entries:** one subfolder per rig — the `.nam` file, optional paired cab IR WAV, picture, and a JSON sidecar (id, name, creator, tags, notes, date added, "cab baked in" flag).
- **Cab entries:** standalone IR WAVs import as their own library entries (id, name, picture optional, tags), so cabs can be attached to any amp after the fact — not only at import time.
- Projects/scenes reference entries by id, never by absolute path.

## Import flow (manual only)

Drag-drop a `.nam` or IR WAV anywhere on the app, or use an Import button in the browser window. The import card asks for:

- Name, picture (file browse or clipboard paste), tags.
- For `.nam` files: "capture includes cabinet?" toggle (defaults off; can't be auto-detected reliably). Sets the "cab baked in" badge on the card.
- Optional: pair a cab IR now (browse file or pick an existing cab entry).

## Browser window

Pop-out in the existing module-window family: a wall of rig cards, picture-forward, with search and tag filters, plus a Cabs section for standalone IRs. Clicking a card loads it into the amp row/slot that opened the browser. Cards show the full-rig badge when the capture has its cab baked in.

## Amp row in the channel chain

The chain's entry model gains a second entry kind — **internal amp** — alongside VST3 entries. It rides the existing reorder / bypass / tint / nickname / context-menu UI. Freely positionable among VST3s (drive plugin before, delay after). A chain may contain more than one amp row.

**Serialization:** rig id(s), cab entry id(s), all knob state, single/dual mode, and dual-mode blend settings — captured by projects and scenes. A project referencing a missing rig or IR loads with that row bypassed and visibly flagged; never a crash.

## Amp module window (per row)

Skeuomorphic amp head, opened by double-clicking the row:

- Rig card art on the faceplate.
- Knobs: input gain, bass / mid / treble (NAM standard tone stack), output level.
- Cab slot showing the paired IR — swappable from the cab library, and **defeatable** (for full-rig captures or when a cab plugin follows in the chain). When a rig's card is flagged "cab baked in," the cab slot defaults to off for that load.
- No noise gate in the amp — the channel gate already covers it.

## Dual amp mode

Each amp row can switch between **Single** and **Dual**:

- Dual splits the row's input into two parallel paths, A and B, each a complete rig: its own model, its own cab slot (independently defeatable), its own input trim.
- **Blend knob** (A↔B mix), **per-side pan** for classic stereo dual-amp spread, and a **polarity flip on B** for phase-fighting rigs.
- Both paths run identical processing types (NAM engine + zero-latency convolution), so the paths stay sample-aligned by construction; no delay compensation needed between them.
- The module window in Dual shows two faceplates side by side sharing the blend/pan/polarity section.
- CPU: two A2-Full instances per row is the budget target; Full/Lite switch applies per side.

## Cases explicitly handled

1. **Different amp on each channel** — amp rows are per-channel chain entries; each channel loads any rig independently.
2. **Captures with cab baked in** — flagged at import, badge on card, cab slot auto-defeats.
3. **Amp-only captures** — pair an IR at import or attach any cab entry later from the amp module's cab slot.
4. **Adding cabinet IRs** — IR WAVs import as standalone cab library entries at any time.
5. **Dual amps/cabs on one channel** — Dual mode: two parallel rigs with blend, pan, and polarity controls.

## Error handling

- A1 `.nam` import → rejected with explanatory message.
- Corrupt/unparseable `.nam` or IR → import rejected with message; never loaded into the audio path.
- Missing files at project load → row bypassed + flagged, project loads normally.
- Model load failure at runtime (e.g. file deleted mid-session) → row falls back to bypass, audio passes through.

## Testing

- Build and launch after every change (standing project rule); verify audibly with a real A2 capture.
- Processing test: loaded model output differs from bypassed output (not a silent no-op).
- Round-trip test: save/load a project with single and dual amp rows restores rigs, cabs, knobs, and blend state.
- Stress: rapid rig swaps while audio runs (background load + atomic swap holds up); project switch with amp module windows open (lifetime rule holds up).

## Implementation deviations (as built, 2026-07-21)

- **Pictures:** attach via file browse or drag-drop onto the picture well; no
  clipboard paste (JUCE's clipboard is text-only on Windows).
- **Sample-rate mismatch:** shown as a "48k!" legend on the amp faceplate; no
  resampling in v1. Run the device at the capture's rate (48k for Tone3000).
- **Missing rig at load:** the amp row stays in the chain and passes audio
  through cleanly; the editor shows a MISSING legend. The row is not
  force-bypassed (equivalent audible result, simpler state).
- **Full/Lite:** implemented via NAM Core's `SlimmableModel::SetSlimmableSize`
  (upstream API existed in v0.5.4); LITE button on the faceplate.
- **Amp rows** are available on the four channel strips and the input channel;
  not on the FX bus.
- **Vendored patch:** `Dependencies/nlohmann/json.hpp` defines a throwing
  `JSON_ASSERT` so malformed model files fail as catchable exceptions instead
  of UB / a blocked loader thread (verified against a hand-crafted bad file).
- **A2 gate ground truth:** a real Tone3000 A2 download ("Fender Vibroverb
  1964") is file version 0.7.0, architecture `SlimmableContainer` - the
  version >= 0.7 predicate is confirmed correct.

## Licensing notes

- NeuralAmpModelerCore, the A2 architecture, and training code are MIT — free to embed and ship.
- Individual captures carry their creators' terms. Manual per-user download means UpStage never redistributes models — this is why the library is user-filled rather than pre-packaged.
