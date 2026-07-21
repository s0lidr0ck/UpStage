# NAM A2 Amp Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed a NAM A2 amp modeler in UpStage as an internal plugin-chain row, backed by a user-filled local amp/cab library with drag-drop import, a card-wall browser window, a skeuomorphic amp editor, and dual-amp mode.

**Architecture:** The amp is a `juce::AudioPluginInstance` subclass (`NamAmpProcessor`) so it slots into the existing `PluginEntry` chain (`ChannelStrip.h:58-70`) and inherits bypass/reorder/editor-window/serialization machinery unchanged. A message-thread-only `AmpLibrary` singleton manages `Documents/UpStage/Amp Library/` (rig + cab entries, JSON sidecars). Model loads run on a background `ThreadPool` and atomic-swap into the audio path under a try-locked `SpinLock`. Cab IRs use `juce::dsp::Convolution` (zero-latency default).

**Tech Stack:** JUCE (Projucer project, C++17, VS2025-preview toolset v145), NeuralAmpModelerCore v0.5.4 (MIT; deps Eigen + nlohmann/json), new module `juce_dsp`.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-21-nam-amp-library-design.md`. A2 models only; A1 rejected at import with message pointing at Tone3000's A2 badge.
- Build command (verbatim, Bash tool):
  `"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:/projects/A18/UpStage/Builds/VisualStudio2022/UpStage.sln" -p:Configuration=Debug -p:Platform=x64 -m -v:minimal`
- **No unit-test framework exists** (repo convention per `docs/superpowers/plans/2026-06-27-ui-usability-pass.md:9`): every task verifies by clean build + app launch + logged/visual/audible check. Use `juce::Logger::writeToLog` for programmatic checks; app runs via `Builds\VisualStudio2022\x64\Debug\App\UpStage.exe`.
- New source files must be registered in `UpStage.jucer` (`<GROUP id="Source">`, `UpStage.jucer:9-93`) and the project resaved with `C:\JUCE\Projucer.exe --resave C:\projects\A18\UpStage\UpStage.jucer` before building. Resave regenerates the `.vcxproj` files.
- Real-time rules (existing house style): audio thread never blocks — `juce::ScopedTryLock` on chain/model locks with clean passthrough on miss; never allocate or delete on the audio thread; dispose models/entries on the message thread only.
- `NAM_SAMPLE` is defined `=float` project-wide so NAM buffers interop with JUCE float buffers.
- Amp identity in project XML is the identifier string `UPSTAGE_INTERNAL:NAM_AMP` riding the existing `PluginSlotState` (`ProjectState.h:17-23`); rig/cab references inside the state blob are library entry **ids**, never absolute paths.
- Stage/commit ONLY files the current task touched (working tree carries unrelated in-progress work). Commit after every task.
- Known v1 deviations from spec (approved direction, note in code comments): picture attach is file-browse/drag only (JUCE clipboard is text-only); sample-rate mismatch between model and device shows a warning badge instead of resampling.

---

### Task 0: Commit pre-existing work + vendor dependencies + build plumbing

**Files:**
- Create: `ThirdParty/NeuralAmpModelerCore/` (git clone, pinned v0.5.4, NOT a submodule — copied files, `.git` removed)
- Modify: `UpStage.jucer` (modules, search paths, preprocessor defs, NAM source files)
- Modify: `.gitignore` (if needed, ensure ThirdParty is tracked)

**Interfaces:**
- Consumes: nothing.
- Produces: compilable includes `NAM/dsp.h`, `NAM/get_dsp.h`, namespace `nam` (`nam::get_dsp(...) -> std::unique_ptr<nam::DSP>`); `juce::dsp::Convolution` available via JuceHeader; macros `NAM_SAMPLE=float`, `NAM_ENABLE_A2_FAST=1` defined project-wide.

- [ ] **Step 1: Commit the pre-existing uncommitted work as its own commit.** The tree has `M Source/MainComponent.cpp`, `M Source/MainComponent.h` and untracked `Source/CassetteDeckWindow.h`, `Source/HardwareModuleWindow.h`, `Source/LoopStationWindow.h`, `Source/MetronomeWindow.h`, `Source/ReelRecorderWindow.h` from the toolbar-module-windows session. Verify with `git status` + `git diff --stat`, then:

```bash
git add Source/MainComponent.cpp Source/MainComponent.h Source/CassetteDeckWindow.h Source/HardwareModuleWindow.h Source/LoopStationWindow.h Source/MetronomeWindow.h Source/ReelRecorderWindow.h
git commit -m "Feature: toolbar module pop-out windows (cassette/looper/metronome/recorder)"
```

If the `.jucer` was already updated for those headers, include it; if not, they are header-only (no compile entries needed) — leave `.jucer` alone in this step.

- [ ] **Step 2: Vendor NeuralAmpModelerCore v0.5.4 with its dependencies.**

```bash
cd /c/projects/A18/UpStage
mkdir -p ThirdParty
git clone --depth 1 --branch v0.5.4 --recurse-submodules https://github.com/sdatkinson/NeuralAmpModelerCore ThirdParty/NeuralAmpModelerCore
rm -rf ThirdParty/NeuralAmpModelerCore/.git ThirdParty/NeuralAmpModelerCore/Dependencies/eigen/.git ThirdParty/NeuralAmpModelerCore/Dependencies/nlohmann/.git 2>/dev/null || true
find ThirdParty/NeuralAmpModelerCore -name ".git" -exec rm -rf {} + 2>/dev/null || true
ls ThirdParty/NeuralAmpModelerCore/NAM/
```

Expected: `NAM/` contains `dsp.h`, `dsp.cpp`, `get_dsp.h`, `get_dsp.cpp`, `wavenet.*`, `lstm.*`, `convnet.*`, `activations.*`, `version.h` (names may vary slightly — list them and record the actual `.cpp` set; every `.cpp` under `NAM/` gets compiled). Confirm Eigen at `ThirdParty/NeuralAmpModelerCore/Dependencies/eigen/Eigen/Dense` and nlohmann at `Dependencies/nlohmann/json.hpp` (check actual layout with `ls`; adjust search paths in Step 3 to match reality).

- [ ] **Step 3: Edit `UpStage.jucer`.** Three edits (use Edit tool on the XML):

(a) Add `juce_dsp` to `<MODULES>` (`UpStage.jucer:95-109`), mirroring an existing line:

```xml
<MODULE id="juce_dsp" showAllCode="1" useLocalCopy="0" useGlobalPath="1"/>
```

(b) In the `VS2022` exporter (`UpStage.jucer:111-119`), append to `extraSearchPaths` (semicolon- or newline-separated, match existing format):

```
../../ThirdParty/NeuralAmpModelerCore
../../ThirdParty/NeuralAmpModelerCore/Dependencies/eigen
../../ThirdParty/NeuralAmpModelerCore/Dependencies/nlohmann
```

(Paths are relative to the generated project dir `Builds/VisualStudio2022/`; if the existing entries are absolute, use `C:\projects\A18\UpStage\ThirdParty\...` absolute paths instead — match whichever style is present.)

(c) Extend each per-config `preprocessorDefs` that currently has `JUCE_PLUGINHOST_VST3=1` to:

```
JUCE_PLUGINHOST_VST3=1&#10;NAM_SAMPLE=float&#10;NAM_ENABLE_A2_FAST=1
```

(d) Add a `ThirdParty` group under `<MAINGROUP>` with `<FILE>` entries: every `NAM/*.cpp` found in Step 2 with `compile="1"`, every `NAM/*.h` with `compile="0"`. Follow the exact `<FILE id="..." name="..." compile="1" resource="0" file="ThirdParty/NeuralAmpModelerCore/NAM/dsp.cpp"/>` shape used by existing entries (ids are any unique 6-char base62 strings).

- [ ] **Step 4: Resave and build.**

```bash
"/c/JUCE/Projucer.exe" --resave "C:/projects/A18/UpStage/UpStage.jucer"
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "C:/projects/A18/UpStage/Builds/VisualStudio2022/UpStage.sln" -p:Configuration=Debug -p:Platform=x64 -m -v:minimal
```

Expected: build succeeds. Likely first-build issues and their fixes: (1) Eigen alignment asserts/crashes on MSVC — if they appear at runtime later, add `EIGEN_MAX_ALIGN_BYTES=0` to preprocessorDefs (documented NAM sharp edge; accept the perf cost only if actually needed); (2) `std::min/max` vs `NOMINMAX` — add `NOMINMAX=1` to defs if windows.h macro clashes appear; (3) missing C++ latest features — toolset is v145/C++17, NAM Core targets C++17, should be fine.

- [ ] **Step 5: Smoke-test the library links.** Temporarily add to `MainComponent`'s constructor (remove in the same task after verifying):

```cpp
juce::Logger::writeToLog ("NAM core linked, latest supported file version: "
                          + juce::String (NAM_LATEST_SUPPORTED_VERSION)); // or nam::version constant — check NAM/get_dsp.h for the actual constant name, e.g. kLatestSupportedVersion
```

If no such constant exists, instead call `nam::is_version_supported(nam::Version{0,7,0})` (verify exact signature in the vendored `get_dsp.h`) and log the bool. Build, launch `Builds/VisualStudio2022/x64/Debug/App/UpStage.exe`, confirm the log line appears and the app runs. Remove the temp line, rebuild.

- [ ] **Step 6: Commit.**

```bash
git add ThirdParty UpStage.jucer Builds/VisualStudio2022
git commit -m "Build: vendor NeuralAmpModelerCore v0.5.4, add juce_dsp module"
```

---

### Task 1: AmpLibrary backend (entries, sidecars, import, A2 validation)

**Files:**
- Create: `Source/AmpLibrary.h`, `Source/AmpLibrary.cpp`
- Modify: `UpStage.jucer` (register both files; resave)

**Interfaces:**
- Consumes: `juce::JSON`, `juce::Uuid`, `juce::File`.
- Produces (used by every later task):

```cpp
struct AmpLibraryEntry
{
    enum class Kind { rig, cab };
    juce::String id;                 // Uuid string, stable forever
    Kind         kind = Kind::rig;
    juce::String name, creator, notes;
    juce::StringArray tags;
    juce::File   folder;             // this entry's subfolder
    juce::File   namFile;            // rigs only
    juce::File   irFile;             // cabs: the IR wav; rigs: unused
    juce::File   pictureFile;        // optional (invalid File if none)
    juce::String pairedCabId;        // rigs: optional cab entry id
    bool         cabBakedIn = false; // rigs: capture includes cabinet
    juce::int64  dateAddedMs = 0;
};

class AmpLibrary
{
public:
    static AmpLibrary& instance();                       // message thread only
    juce::File getRootFolder() const;                    // Documents/UpStage/Amp Library
    void rescan();                                       // re-reads all sidecars
    const juce::OwnedArray<AmpLibraryEntry>& getEntries() const;
    const AmpLibraryEntry* findById (const juce::String& id) const;
    bool updateEntry (const AmpLibraryEntry& e);         // rewrite sidecar (rename/tags/pairing)

    // Both return the new entry id, or {} with errorOut set.
    juce::String importNamFile (const juce::File& src, const juce::String& name,
                                const juce::File& picture, const juce::StringArray& tags,
                                bool cabBakedIn, const juce::File& pairedIrToImport,
                                juce::String& errorOut);
    juce::String importIrFile  (const juce::File& src, const juce::String& name,
                                const juce::File& picture, const juce::StringArray& tags,
                                juce::String& errorOut);

    static bool isA2NamFile (const juce::File& f, juce::String& errorOut);

    std::function<void()> onLibraryChanged;              // fired after import/update/rescan
    // Rendezvous for "pick an entry" requests (browser wiring in Task 5):
    std::function<void (AmpLibraryEntry::Kind,
                        std::function<void (juce::String /*entryId*/)>)> onPickRequested;
    void requestPick (AmpLibraryEntry::Kind k,
                      std::function<void (juce::String)> cb)
    { if (onPickRequested) onPickRequested (k, std::move (cb)); }
};
```

- [ ] **Step 1: Implement `AmpLibrary`.** Meyers singleton (`static AmpLibrary& instance() { static AmpLibrary l; return l; }`). Root = `juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("UpStage").getChildFile ("Amp Library")`, `createDirectory()` on first access (matches the existing Documents convention, `MainComponent.cpp:2301`). On-disk layout: one subfolder per entry named `<id>`; inside: the copied `.nam` or IR `.wav`, optional picture (copied, original extension kept, named `picture.<ext>`), and `entry.json` sidecar written with `juce::JSON::toString`:

```json
{ "id": "...", "kind": "rig", "name": "...", "creator": "", "notes": "",
  "tags": ["clean","fender"], "namFile": "capture.nam", "irFile": "",
  "picture": "picture.jpg", "pairedCabId": "", "cabBakedIn": false,
  "dateAddedMs": 0 }
```

File references inside the sidecar are **relative to the entry folder**. `rescan()` iterates child dirs, parses `entry.json` (`juce::JSON::parse`), skips unparseable folders with a log line. `importNamFile`: validate via `isA2NamFile` → create folder from fresh `juce::Uuid().toString()` → copy files (`src.copyFileTo`) → write sidecar → append entry → fire `onLibraryChanged`. If `pairedIrToImport` is a valid file, first import it via `importIrFile` (name = IR filename stem, same tags) and store its id in `pairedCabId`. `importIrFile`: validate it's a readable WAV (`juce::WavAudioFormat` reader non-null via `AudioFormatManager` with `registerBasicFormats`), same folder/sidecar flow. Failures set `errorOut` (human-readable) and clean up any half-created folder.

- [ ] **Step 2: Implement `isA2NamFile`.** Parse the file with `juce::JSON::parse(f)`; reject unparseable ("This file isn't a readable NAM model."). Read `"version"` string, split on `.` into major/minor. Predicate: **accept iff `major > 0 || minor >= 7`** — A2 files are format version 0.7.x+ (NAM Core v0.5.4 supports file versions 0.5.0–0.7.0; 0.5/0.6 are A1-era). Reject message: `"This is an A1-era NAM model (file version X). UpStage supports A2 models only — look for the A2 badge on tone3000.com."`. **Isolate the predicate in one function** — Step 4 verifies it against a real file and it may need one-line adjustment (e.g. if A2 files also carry an architecture marker, prefer that).

- [ ] **Step 3: Register files in `.jucer`, resave, build.** Add `<FILE>` entries for `AmpLibrary.h` (`compile="0"`) and `AmpLibrary.cpp` (`compile="1"`) to the Source group; run the Projucer resave + MSBuild commands from Global Constraints. Expected: clean build.

- [ ] **Step 4: Verify against real files.** Create `C:\Users\alex\AppData\Local\Temp\claude\...\scratchpad\fake_a1.nam` containing `{"version": "0.5.2", "architecture": "WaveNet", "config": {}, "weights": []}`. Add a temporary log-only test in `MainComponent`'s constructor:

```cpp
juce::String err;
bool a1ok = AmpLibrary::isA2NamFile (juce::File ("<scratchpad>/fake_a1.nam"), err);
juce::Logger::writeToLog ("A1 reject test: " + juce::String (a1ok ? "FAIL (accepted!)" : "PASS — " + err));
AmpLibrary::instance().rescan();
juce::Logger::writeToLog ("Amp library entries: " + juce::String (AmpLibrary::instance().getEntries().size()));
```

Launch, confirm `PASS` and `entries: 0`, and that `Documents/UpStage/Amp Library/` was created. **If a real A2 `.nam` file is obtainable** (check `~/Downloads` for any `*.nam`; inspect its JSON `version`/`architecture` fields with the Read tool), confirm the predicate accepts it and adjust if its version scheme differs from the assumption — this is the one externally-unverifiable assumption in the plan, so leave the log-test in place until Task 8 hardening if no A2 file is available yet, and flag it to the user in the task summary. Remove temp test code (or keep behind `#if JUCE_DEBUG` until Task 8), rebuild.

- [ ] **Step 5: Commit.**

```bash
git add Source/AmpLibrary.h Source/AmpLibrary.cpp UpStage.jucer Builds/VisualStudio2022
git commit -m "Feature: amp library backend (rig/cab entries, sidecars, A2-only import)"
```

---

### Task 2: NamAmpProcessor engine (single amp, background load, tone stack, cab)

**Files:**
- Create: `Source/NamAmpProcessor.h`, `Source/NamAmpProcessor.cpp`
- Modify: `UpStage.jucer` (register; resave)

**Interfaces:**
- Consumes: `AmpLibrary::findById`, `nam::get_dsp (const char* path)` / `nam::get_dsp (const std::string&)` (check exact overload in vendored `get_dsp.h`) returning `std::unique_ptr<nam::DSP>`; `nam::DSP::Reset (double sampleRate, int maxBufferSize)`, `prewarm()`, `process (NAM_SAMPLE** in, NAM_SAMPLE** out, int numFrames)`; `nam::get_sample_rate_from_nam_file`; `juce::dsp::Convolution`.
- Produces (used by Tasks 3, 4, 7):

```cpp
class NamAmpProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr const char* kIdentifier = "UPSTAGE_INTERNAL:NAM_AMP";
    NamAmpProcessor();
    ~NamAmpProcessor() override;

    // Sides: 0 = A, 1 = B (side B unused until Task 7 enables dual mode UI).
    void loadRig (int side, const juce::String& rigId);   // message thread; async load
    void setCab  (int side, const juce::String& cabId);   // message thread
    void setCabEnabled (int side, bool enabled);
    juce::String getRigId (int side) const;
    juce::String getRigName (int side) const;             // display name of loaded rig
    juce::String getCabId (int side) const;
    bool isCabEnabled (int side) const;
    bool hasModel (int side) const;                       // model loaded & live
    bool didModelFail (int side) const;                   // last load failed / file missing
    bool hasSampleRateMismatch (int side) const;

    // Parameters (audio thread reads, message thread writes) — knob range 0..10 for tone.
    std::atomic<float> inputGainDb { 0.0f }, bassKnob { 5.0f }, midKnob { 5.0f },
                       trebleKnob { 5.0f }, outputGainDb { 0.0f },
                       blend { 0.5f }, panA { 0.0f }, panB { 0.0f },
                       sideTrimDbA { 0.0f }, sideTrimDbB { 0.0f };
    std::atomic<bool>  dualMode { false }, polarityFlipB { false }, useLite { false };

    std::function<void()> onEngineStateChanged;  // editor refresh hook (model arrived/failed)

    // juce::AudioProcessor / AudioPluginInstance overrides:
    // getName() returns "NAM Amp" (STABLE — appearance map is keyed by name);
    // prepareToPlay, releaseResources, processBlock, getStateInformation,
    // setStateInformation, createEditor (returns nullptr until Task 4), hasEditor,
    // fillInPluginDescription, acceptsMidi/producesMidi=false, getTailLengthSeconds=0,
    // program stubs (1 program, no-ops).
};
```

- [ ] **Step 1: Write the header** exactly per the interface above, plus private internals:

```cpp
private:
    struct Side
    {
        juce::String rigId, cabId;      // cabId empty => rig's pairedCabId (resolved at setCab/load time)
        bool cabEnabled = true;
        std::unique_ptr<nam::DSP> model;            // swapped under modelLock
        juce::dsp::Convolution cab;                 // its own internal bg loader; RT-safe
        std::atomic<bool> cabLoaded { false };
        std::atomic<bool> failed { false };
        std::atomic<bool> srMismatch { false };
        juce::String rigName;                       // message-thread copy for UI
    };
    Side sides[2];
    juce::SpinLock modelLock;                       // try-locked in processBlock
    juce::ThreadPool loaderPool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    double sampleRate = 0; int maxBlock = 0;
    juce::AudioBuffer<float> monoIn, sideBuf[2];    // preallocated in prepareToPlay
    struct Biquad { float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
                    inline float p (float x) noexcept
                    { float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
                      x2=x1; x1=x; y2=y1; y1=y; return y; }
                    void reset() noexcept { x1=x2=y1=y2=0; } };
    Biquad toneLow[2], toneMid[2], toneHigh[2];     // per output channel
    std::atomic<bool> toneDirty { true };
    float lastBass = -1, lastMid = -1, lastTreble = -1;
    void updateToneCoeffs();                        // RBJ shelf/peak math, no allocation
```

- [ ] **Step 2: Implement lifecycle + load path.** Constructor: `AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo()))`. Destructor: `*alive = false; loaderPool.removeAllJobs (true, 4000);` — then models destroyed by member teardown (message thread, per the existing disposal rule).

`loadRig (side, rigId)`: resolve entry via `AmpLibrary::findById`. Missing/`namFile` gone → set `failed=true`, clear model under `ScopedLockType (modelLock)`, fire `onEngineStateChanged`, return (row keeps passing audio — clean passthrough is the "bypassed + flagged" behavior, flag surfaces in the editor as MISSING). Otherwise capture `path = entry->namFile.getFullPathName().toStdString()`, `rigId`, `alive`, `this`, current `sampleRate`/`maxBlock`, and add a `loaderPool` job:

```cpp
loaderPool.addJob ([this, aliveRef = alive, path, rigId, side, sr = sampleRate, mb = maxBlock]
{
    std::unique_ptr<nam::DSP> newModel;
    juce::String error;
    try {
        newModel = nam::get_dsp (path.c_str());       // adjust to actual overload
        if (newModel != nullptr && sr > 0) { newModel->Reset (sr, mb); newModel->prewarm(); }
    } catch (const std::exception& e) { error = e.what(); }
    juce::MessageManager::callAsync ([this, aliveRef, m = newModel.release(), rigId, side, error]() mutable
    {
        std::unique_ptr<nam::DSP> owned (m);
        if (! aliveRef->load()) return;               // processor died; model deleted here (message thread)
        auto& s = sides[side];
        if (s.rigId != rigId) return;                 // superseded by a newer load
        if (owned == nullptr) { s.failed = true; }
        else {
            s.srMismatch = /* compare nam::get_sample_rate_from_nam_file vs sampleRate; -1 == unknown => no mismatch */ false;
            juce::SpinLock::ScopedLockType lk (modelLock);
            s.model = std::move (owned);              // old model destroyed after lock released? NO —
        }                                             // move old out first, destroy outside the lock:
        s.failed = (m == nullptr);
        if (onEngineStateChanged) onEngineStateChanged();
    });
});
```

**Swap detail (write it exactly this way):** inside the callAsync, do `std::unique_ptr<nam::DSP> old; { juce::SpinLock::ScopedLockType lk (modelLock); old = std::move (s.model); s.model = std::move (owned); }` then let `old` destruct after the scope — never delete inside the lock. Set `s.rigId`/`s.rigName` on the message thread in `loadRig` itself (before the job), so `getRigId` is immediately correct and the stale-load guard works.

`setCab (side, cabId)`: resolve File — explicit `cabId` → cab entry's `irFile`; empty `cabId` → rig's `pairedCabId`'s `irFile`; none → unload (`cab.reset()`, `cabLoaded=false`). Load with `sides[side].cab.loadImpulseResponse (irFile, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0, juce::dsp::Convolution::Normalise::yes);` and set `cabLoaded=true`. Convolution handles its own background loading and RT-safe crossfade swap.

`prepareToPlay (sr, blockSize)`: store; size `monoIn`/`sideBuf` to `(1, blockSize)`; `juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 1 }; sides[i].cab.prepare (spec);` and under `ScopedLockType` call `model->Reset (sr, blockSize)` for any loaded model (message thread, audio not running during prepare — same guarantee ChannelStrip::prepare relies on, `ChannelStrip.cpp:39-58`); reset biquads; `toneDirty = true`.

- [ ] **Step 3: Implement `processBlock`.** Shape (single mode; dual finished in Task 7 but write the full dual path now — it's the same code):

```cpp
void NamAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 1) return;
    juce::SpinLock::ScopedTryLockType lk (modelLock);
    if (! lk.isLocked()) return;                       // mid-swap: clean passthrough
    const bool dual = dualMode.load();
    auto& A = sides[0]; auto& B = sides[1];
    if (A.model == nullptr && ! (dual && B.model != nullptr)) return;  // nothing loaded: passthrough

    // mono sum input
    monoIn.copyFrom (0, 0, buffer, 0, 0, n);
    if (buffer.getNumChannels() > 1) { monoIn.addFrom (0, 0, buffer, 1, 0, n); monoIn.applyGain (0.5f); }
    monoIn.applyGain (juce::Decibels::decibelsToGain (inputGainDb.load()));

    auto renderSide = [&] (Side& s, juce::AudioBuffer<float>& out, float trimDb)
    {
        out.copyFrom (0, 0, monoIn, 0, 0, n);
        out.applyGain (juce::Decibels::decibelsToGain (trimDb));
        if (s.model != nullptr) {
            float* p = out.getWritePointer (0);
            float* io[1] = { p };
            s.model->process (io, io, n);              // in-place is how the official plugin runs it
        }
        if (s.cabEnabled && s.cabLoaded.load()) {
            juce::dsp::AudioBlock<float> blk (out.getArrayOfWritePointers(), 1, (size_t) n);
            s.cab.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
    };
    renderSide (A, sideBuf[0], sideTrimDbA.load());
    if (dual) { renderSide (B, sideBuf[1], sideTrimDbB.load());
                if (polarityFlipB.load()) sideBuf[1].applyGain (-1.0f); }

    // blend + pan into stereo output (equal-power pan per side)
    const float bl = dual ? blend.load() : 0.0f;
    const float gA = dual ? std::cos (bl * juce::MathConstants<float>::halfPi) : 1.0f;
    const float gB = dual ? std::sin (bl * juce::MathConstants<float>::halfPi) : 0.0f;
    auto panLR = [] (float pan, float& l, float& r)
    { const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
      l = std::cos (a); r = std::sin (a); };
    float lA, rA, lB, rB; panLR (dual ? panA.load() : 0.0f, lA, rA); panLR (panB.load(), lB, rB);
    buffer.clear();
    buffer.addFrom (0, 0, sideBuf[0], 0, 0, n, gA * lA);
    buffer.addFrom (1, 0, sideBuf[0], 0, 0, n, gA * rA);
    if (dual) { buffer.addFrom (0, 0, sideBuf[1], 0, 0, n, gB * lB);
                buffer.addFrom (1, 0, sideBuf[1], 0, 0, n, gB * rB); }

    // tone stack (post-blend) + output gain
    if (toneDirty.exchange (false)) updateToneCoeffs();
    for (int ch = 0; ch < 2; ++ch) {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = toneHigh[ch].p (toneMid[ch].p (toneLow[ch].p (d[i])));
    }
    buffer.applyGain (juce::Decibels::decibelsToGain (outputGainDb.load()));
}
```

`updateToneCoeffs()`: RBJ formulas (no allocation) — low shelf f0=120 Hz, gain `(bass-5)/5 * 12` dB; peaking f0=800 Hz Q=0.7, gain `(mid-5)/5 * 10` dB; high shelf f0=3200 Hz, gain `(treble-5)/5 * 12` dB. Knob setters (or the editor) set `toneDirty = true` after writing the atomics.

- [ ] **Step 4: Implement state + plugin plumbing.** `getStateInformation` → XML → binary via `copyXmlToBinary`:

```xml
<NamAmp dual="0" inputGainDb="0" bass="5" mid="5" treble="5" outputGainDb="0"
        blend="0.5" panA="0" panB="0" polB="0" useLite="0">
  <Side idx="0" rigId="" cabId="" cabEnabled="1" trimDb="0"/>
  <Side idx="1" rigId="" cabId="" cabEnabled="1" trimDb="0"/>
</NamAmp>
```

`setStateInformation`: parse (`getXmlFromBinary`), write all atomics, `toneDirty=true`, then for each side with non-empty `rigId`: `loadRig (idx, rigId); setCab (idx, cabId); setCabEnabled (idx, en)`. `fillInPluginDescription`: name "NAM Amp", `pluginFormatName = "Internal"`, `fileOrIdentifier = kIdentifier`, `manufacturerName = "UpStage"`, `category = "Effect"`, `uniqueId = 0x4e414d31`. `getName()` returns `"NAM Amp"` — **stable**, because `PluginAppearanceState` and the clipboard are keyed by name (`ChannelStripPanel` map). `createEditor()` returns `nullptr` and `hasEditor()` false for now (Task 4 flips it).

- [ ] **Step 5: Register in `.jucer`, resave, build.** Expected: clean build (this is where NAM template/Eigen compile issues surface — fix per Task 0 Step 4 notes).

- [ ] **Step 6: Commit.**

```bash
git add Source/NamAmpProcessor.h Source/NamAmpProcessor.cpp UpStage.jucer Builds/VisualStudio2022
git commit -m "Feature: NamAmpProcessor engine (A2 model load, tone stack, cab convolution, dual-ready)"
```

---

### Task 3: Chain + persistence integration ("Add NAM Amp" row, project load, scenes)

**Files:**
- Modify: `Source/ChannelStrip.h`, `Source/ChannelStrip.cpp` (add `addAmp`)
- Modify: `Source/ChannelStripPanel.h`, `Source/ChannelStripPanel.cpp` (empty-slot menu item + callback)
- Modify: `Source/MainComponent.cpp` (wire callback; special-case project-load re-instantiation)

**Interfaces:**
- Consumes: `NamAmpProcessor` (Task 2), `PluginEntry`/chain internals (`ChannelStrip.h:58-70,123-124`), existing load path.
- Produces: `int ChannelStrip::addAmp()` — synchronously creates a `NamAmpProcessor`, appends under lock, returns its slot index (-1 on failure); `ChannelStripPanel::onAddAmpClicked` callback (`std::function<void()>`, fired from the empty-slot menu).

- [ ] **Step 1: `ChannelStrip::addAmp`.** Mirror `addPlugin`'s post-instantiation config (`ChannelStrip.cpp:69-112` — bus layout, `setRateAndBufferSizeDetails`, `prepareToPlay`, `setPlayHead`, adapting to the actual private member names for rate/block/playhead found there):

```cpp
int ChannelStrip::addAmp()
{
    auto amp = std::make_unique<NamAmpProcessor>();
    amp->enableAllBuses();
    if (/* stored sample rate */ > 0) {
        amp->setRateAndBufferSizeDetails (storedRate, storedBlock);
        amp->prepareToPlay (storedRate, storedBlock);
    }
    // setPlayHead if addPlugin does
    auto* entry = new PluginEntry();
    entry->processor  = std::move (amp);
    entry->identifier = NamAmpProcessor::kIdentifier;
    const juce::ScopedLock sl (chainLock);
    pluginChain.add (entry);
    return pluginChain.size() - 1;
}
```

No changes needed to `processBlock`, bypass, reorder, `getState`/`setState` — the amp rides them via `AudioPluginInstance`. Verify `getState` (`ChannelStrip.cpp:334-377`) pulls `entry->identifier` (it does) so the saved slot carries `UPSTAGE_INTERNAL:NAM_AMP`.

- [ ] **Step 2: Panel menu.** In `ChannelStripPanel.h` add `std::function<void()> onAddAmpClicked;` next to `onAddPluginClicked` (`ChannelStripPanel.h:26-27`). In the **empty-slot** context menu and the empty-slot click flow (`ChannelStripPanel.cpp:208-210, 282-446`): add item **"Add NAM Amp"** after "Add Plugin…" firing the callback. (Filled-slot menu needs no change — Open Editor/bypass/move/remove all work.)

- [ ] **Step 3: Wire in MainComponent.** Where each panel's `onAddPluginClicked` is wired (grep `onAddPluginClicked` in `MainComponent.cpp`), add alongside for each channel strip panel:

```cpp
panel.onAddAmpClicked = [this, i] { channels[i]->addAmp(); refreshChannelPanel (i); /* use the actual rebuild call used after addPlugin */ };
```

- [ ] **Step 4: Project-load special case.** Find the re-instantiation loop (grep `addPlugin` / `pluginIdentifier` in `MainComponent.cpp` project-load path). Before the VST3 lookup/instantiation for a slot, branch:

```cpp
if (slot.pluginIdentifier == NamAmpProcessor::kIdentifier)
{
    strip.addAmp();          // synchronous — no async pending count for this slot
    /* mark as done in whatever pending-callback accounting the loop uses */
}
```

Then the existing `ChannelStrip::setState` pass restores bypass + `setStateInformation` (which triggers rig/cab loads). **Check the async accounting carefully** — if the loop counts outstanding `createPluginInstanceAsync` callbacks before calling `setState`, the synchronous amp must be counted as immediately complete or `setState` may run early/never.

- [ ] **Step 5: Scenes check.** Grep `SceneManager` for how scenes snapshot plugin state: if scenes reuse `getStateInformation`/`PluginSlotState`, amps ride free — confirm by reading the capture/apply code; if scenes only capture gains/pans, note that amp knobs are NOT scene-captured yet and log it in the task summary (spec wants scene capture; add the same state-blob capture the plugins get if the hook exists cheaply, else defer with a note).

- [ ] **Step 6: Build, launch, verify.** Build clean. Launch. Right-click an empty slot on channel 1 → "Add NAM Amp" → a row named "NAM Amp" appears with green bypass dot; audio passes through unchanged (no model loaded); bypass toggle, move up/down, remove all behave. Save project As `scratch-nam-test.upstage`, reopen it → the amp row is restored. (Sound-through-a-model comes in Task 4/5 once a rig can be loaded.)

- [ ] **Step 7: Commit.**

```bash
git add Source/ChannelStrip.h Source/ChannelStrip.cpp Source/ChannelStripPanel.h Source/ChannelStripPanel.cpp Source/MainComponent.cpp
git commit -m "Feature: internal NAM amp rows in channel chains with project persistence"
```

---

### Task 4: Amp editor (skeuomorphic faceplate, single mode)

**Files:**
- Create: `Source/NamAmpEditor.h` (header-only, like the module windows)
- Modify: `Source/NamAmpProcessor.cpp` (`createEditor`/`hasEditor`)
- Modify: `UpStage.jucer` (register header; resave)

**Interfaces:**
- Consumes: `NamAmpProcessor` params/queries (Task 2), `MixerLookAndFeel` (`Source/MixerLookAndFeel.h` — rotary via `drawRotarySlider`, readout labels via `componentID "readout"`, dot-matrix via `"strip_label"`), `AmpLibrary` (entry lookup for name/picture), `AmpLibrary::requestPick` (browser arrives Task 5 — until then the pick callback simply never fires; guard everything on it).
- Produces: `class NamAmpEditor : public juce::AudioProcessorEditor, private juce::Timer` — created by `NamAmpProcessor::createEditor()`; opened through the **existing** `openPluginEditor` window path (`ChannelStrip.cpp:144-206`) with zero changes there.

- [ ] **Step 1: Build the editor component.** Fixed size 620×320 (single mode; dual resizes in Task 7). Owns a `MixerLookAndFeel lnf;` member, `setLookAndFeel (&lnf)` in ctor, `setLookAndFeel (nullptr)` in dtor **before** children are destroyed (declare `lnf` first so it outlives them). Layout:

- Left: rig zone — picture well (recessed, drawn in `paint` with the entry's `pictureFile` via `juce::ImageCache::getFromFile`, or a dark "NO RIG" placeholder), rig name on a dot-matrix strip (`juce::Label` with `componentID "strip_label"`), a `BROWSE…` `TextButton` → `AmpLibrary::instance().requestPick (Kind::rig, [safeThis = juce::Component::SafePointer<NamAmpEditor>(this)] (juce::String id) { if (safeThis != nullptr) safeThis->proc.loadRig (0, id); });` and a small **MISSING** / **48k!** warning legend painted when `didModelFail(0)` / `hasSampleRateMismatch(0)`.
- Center: five rotary `juce::Slider`s (INPUT −24..+24 dB, BASS/MID/TREBLE 0..10, OUTPUT −24..+24 dB), each `onValueChange` writes the matching processor atomic + `toneDirty` via a processor setter. Double-click-reset default values (0 dB / 5.0) — `setDoubleClickReturnValue`.
- Right: cab zone — cab name readout (`componentID "readout"`), `CAB` on/off `TextButton` (toggles `setCabEnabled(0, …)`, backlit style), `BROWSE…` → `requestPick (Kind::cab, …)` calling `setCab (0, id)`, and a `FULL/LITE` toggle writing `useLite` (no engine effect until Task 8 investigation; keep it hidden with `setVisible(false)` until then).

`timerCallback` (10 Hz) + `onEngineStateChanged` both call a `refresh()` that re-reads rig/cab names, warning flags, and repaints — covers async model arrival. Ctor sets knob values **from** the processor atomics (state restore path).

- [ ] **Step 2: Flip the processor.** `hasEditor() → true`; `createEditor() → new NamAmpEditor (*this)`. The existing `PluginEditorWindow` in `openPluginEditor` handles windowing/lifetime untouched.

- [ ] **Step 3: Register header in `.jucer`, resave, build, launch.** Add amp row → double-click → faceplate opens with knobs styled by `MixerLookAndFeel`; knobs move and (once a rig loads, Task 5/6) affect sound; window close/reopen and project-switch-with-window-open don't crash (the SafePointer editor-window rule covers it).

- [ ] **Step 4: Commit.**

```bash
git add Source/NamAmpEditor.h Source/NamAmpProcessor.cpp UpStage.jucer Builds/VisualStudio2022
git commit -m "Feature: skeuomorphic NAM amp editor (knobs, rig/cab zones, warnings)"
```

---

### Task 5: Amp library browser window (card wall + pick flow + toolbar)

**Files:**
- Create: `Source/AmpLibraryBrowserWindow.h` (header-only: content + window classes, follows `HardwareModuleWindow` pattern)
- Modify: `Source/MainComponent.h`, `Source/MainComponent.cpp` (own window, toolbar button, `onPickRequested` handler)
- Modify: `UpStage.jucer` (register; resave)

**Interfaces:**
- Consumes: `AmpLibrary` (entries, `onLibraryChanged`, `onPickRequested`), `HardwareModuleWindow` (`HardwareModuleWindow.h:16-42`), `MixerLookAndFeel`.
- Produces: `class AmpLibraryBrowserWindow : public HardwareModuleWindow` with content exposing `void enterPickMode (AmpLibraryEntry::Kind, std::function<void(juce::String)> cb)` and `void enterManageMode()`; `std::function<void(juce::File)> onImportRequested` (wired to the Task 6 dialog; until then the Import button opens a `juce::FileChooser` and calls it — stub logs).

- [ ] **Step 1: Content component.** Top bar: search `TextEditor` (placeholder "Search…"), tag filter `ComboBox` (populated from union of all entry tags + "All tags"), `IMPORT…` `TextButton`, and a mode legend ("PICK A RIG" / "PICK A CAB" / "LIBRARY") on a dot-matrix label. Body: `juce::Viewport` over a grid component of **rig cards** then a "CABINETS" divider and cab cards. Card = custom-painted child (150×120): picture (or placeholder amp silhouette), name strip, small badges — `FULL RIG` when `cabBakedIn`, tag chips (first 2). Filtering: name/creator/tags contains search text (case-insensitive) AND tag matches combo. `onLibraryChanged` → rebuild. Card click: in pick mode → fire callback with entry id, then `getTopLevelComponent()->setVisible(false)` (window hides, matching `HardwareModuleWindow` close behavior); in manage mode → right-click menu: Rename…, Edit Tags…, Set Picture…, (rigs) Toggle "cab baked in", Pair Cab IR… (sub-list of cab entries), Delete… (confirm `AlertWindow`, deletes entry folder via `folder.deleteRecursively()` then rescan). Rename/tags use `juce::AlertWindow` text input; all mutations go through `AmpLibrary::updateEntry`.

- [ ] **Step 2: Window + MainComponent wiring.** `AmpLibraryBrowserWindow (AmpLibrary&)` sized ~720×520, same lazy-`unique_ptr` pattern as the other modules (`MainComponent.h:341-344`, construction at `MainComponent.cpp:2258-2322`). Add a toolbar button labelled `AMPS` next to the existing module buttons (mirror the cassette button's creation/wiring exactly) → construct-if-needed + `enterManageMode()` + `toggleVisible()`. In MainComponent startup, install the rendezvous:

```cpp
AmpLibrary::instance().onPickRequested = [this] (AmpLibraryEntry::Kind k, std::function<void(juce::String)> cb)
{
    ensureAmpBrowserExists();
    ampBrowserWindow->getContent().enterPickMode (k, std::move (cb));
    ampBrowserWindow->setVisible (true); ampBrowserWindow->toFront (true);
};
```

- [ ] **Step 3: Build, launch, verify.** `AMPS` toolbar button opens the (empty) library window; search/tag controls render; Import button opens a FileChooser (import lands Task 6 — selecting a file may just log for now). From an amp editor, `BROWSE…` opens the window in PICK A RIG mode. Manually drop a valid A2 `.nam` through a temporary direct `importNamFile` call if one is available, else defer functional pick-verification to Task 6.

- [ ] **Step 4: Commit.**

```bash
git add Source/AmpLibraryBrowserWindow.h Source/MainComponent.h Source/MainComponent.cpp UpStage.jucer Builds/VisualStudio2022
git commit -m "Feature: amp library browser window (card wall, pick mode, toolbar button)"
```

---

### Task 6: Import flow (drag-drop + import dialog + rejections)

**Files:**
- Create: `Source/AmpImportDialog.h` (header-only)
- Modify: `Source/MainComponent.h`, `Source/MainComponent.cpp` (`juce::FileDragAndDropTarget`)
- Modify: `Source/AmpLibraryBrowserWindow.h` (Import button → dialog)
- Modify: `UpStage.jucer` (register; resave)

**Interfaces:**
- Consumes: `AmpLibrary::importNamFile` / `importIrFile` / `isA2NamFile` (Task 1).
- Produces: `static void AmpImportDialog::show (const juce::File& source, std::function<void()> onDone)` — modal-style `juce::DialogWindow::LaunchOptions` flow; auto-detects kind by extension (`.nam` → rig, `.wav` → cab).

- [ ] **Step 1: Dialog content.** Fields: Name (`TextEditor`, prefilled with filename stem), Picture well (click → `FileChooser` for `*.png;*.jpg;*.jpeg`; also a `FileDragAndDropTarget` so an image can be dropped on it; preview-painted), Tags (`TextEditor`, comma-separated, hint "clean, crunch, marshall…"), and for `.nam` sources: "Capture includes cabinet" `ToggleButton` (default off) + "Pair cab IR" row (`ComboBox` of existing cab entries + "Browse for WAV…" + "None", default None). Buttons IMPORT / CANCEL. IMPORT calls the matching `AmpLibrary::import*`; on failure shows `juce::AlertWindow::showMessageBoxAsync (WarningIcon, "Import failed", errorOut)` and keeps the dialog open; on success closes and runs `onDone`.

- [ ] **Step 2: OS drag-drop onto the app.** `MainComponent` gains `public juce::FileDragAndDropTarget` (none exists today — verified):

```cpp
bool isInterestedInFileDrag (const juce::StringArray& files) override
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".nam") || f.endsWithIgnoreCase (".wav")) return true;
    return false;
}
void filesDropped (const juce::StringArray& files, int, int) override { importFileQueue (files, 0); }
```

`importFileQueue` chains one `AmpImportDialog::show` per matching file via the `onDone` continuation (imports N dropped files one at a time). Non-matching extensions are skipped silently; a dropped A1 file flows into the dialog and gets the A1 rejection message from `importNamFile` on IMPORT — additionally pre-check in `filesDropped` with `isA2NamFile` and surface the rejection immediately (skip the dialog) so the "quick and easy" path fails fast with the Tone3000-A2-badge message.

- [ ] **Step 3: Wire the browser's IMPORT button** to a `FileChooser` (`*.nam;*.wav`) → `AmpImportDialog::show`.

- [ ] **Step 4: Build, launch, verify end-to-end.** (a) Drag the Task 1 `fake_a1.nam` onto the app → immediate A1 rejection message, no dialog. (b) Drag any `.wav` → dialog → name/tag it → appears under CABINETS in the browser. (c) If a real A2 `.nam` is available: drag → dialog → import → card appears; open an amp row editor → BROWSE → pick it → model loads (log line / editor stops showing NO RIG) → **audible check with live input**; save + reopen project → rig restored. If no A2 file is on hand, complete (a)+(b) and flag (c) to the user as the remaining verification.

- [ ] **Step 5: Commit.**

```bash
git add Source/AmpImportDialog.h Source/MainComponent.h Source/MainComponent.cpp Source/AmpLibraryBrowserWindow.h UpStage.jucer Builds/VisualStudio2022
git commit -m "Feature: amp/cab import flow (drag-drop, import dialog, A2-only rejection)"
```

---

### Task 7: Dual amp mode (UI + state; engine shipped in Task 2)

**Files:**
- Modify: `Source/NamAmpEditor.h` (SINGLE/DUAL switch, side-B faceplate, blend/pan/polarity section)
- Modify: `Source/NamAmpProcessor.cpp` (only if engine gaps surfaced)

**Interfaces:**
- Consumes: everything from Tasks 2 & 4; `blend/panA/panB/polarityFlipB/dualMode/sideTrimDbB` atomics; side-1 variants of `loadRig/setCab/...`.
- Produces: complete dual-mode UX; editor width 620 (single) ↔ 980 (dual) via `setSize` on mode toggle (the host `DocumentWindow` uses `setContentOwned` sizing and follows).

- [ ] **Step 1: Editor dual layout.** A `SINGLE | DUAL` two-state button (backlit legend) top-center writing `dualMode`. In DUAL: side-A rig/cab zone compacts left, a mirrored side-B zone (own picture, rig strip, BROWSE, cab slot + defeat, MISSING/48k legends, TRIM mini-knob → `sideTrimDbB`) appears right, and a center **BLEND** rotary (0..1, bipolar-style A↔B), **PAN A** / **PAN B** mini rotaries (−1..1, `componentID` containing "pan" for the bipolar arc, `MixerLookAndFeel.h:226-348`), and **Ø B** polarity toggle. Shared INPUT/tone/OUTPUT knobs stay centered. Mode switch triggers relayout + `setSize`.

- [ ] **Step 2: Verify state round-trip.** Dual state already serializes (Task 2 XML covers both sides + blend/pans/polB). Launch: add amp → DUAL → load different rigs A/B (or same rig twice if only one available) → set blend/pans/flip → save project → reopen → editor reopens in DUAL with everything restored. Audible: blend sweeps between sides; Ø B audibly thins the blended sound when both sides carry the same rig (classic phase check).

- [ ] **Step 3: Build, launch, verify, commit.**

```bash
git add Source/NamAmpEditor.h Source/NamAmpProcessor.cpp
git commit -m "Feature: dual amp mode (A/B rigs, blend, per-side pan, polarity flip)"
```

---

### Task 8: Hardening + Full/Lite investigation + final verification

**Files:**
- Modify: `Source/NamAmpProcessor.cpp`, `Source/NamAmpEditor.h`, `Source/AmpLibrary.cpp` (fixes as found)
- Modify: `docs/superpowers/specs/2026-07-21-nam-amp-library-design.md` (record any approved deviations)

**Interfaces:** consumes everything; produces the shippable feature.

- [ ] **Step 1: Full/Lite investigation.** Grep the vendored NAM Core for the slimmable-width API: `grep -ri "slim\|lite\|width\|a2" ThirdParty/NeuralAmpModelerCore/NAM/ --include=*.h`. If `get_dsp`/`DspLoadOptions` exposes a width/lite selector, wire `useLite` through `loadRig` (reload on toggle) and unhide the FULL/LITE switch (Task 4 left it hidden). If no API exists in v0.5.4, leave the switch hidden and note "A2-Full only; Lite pending upstream API" in the spec deviations.

- [ ] **Step 2: Failure-path sweep (exercise each, fix what breaks).**
  - Delete a rig's folder on disk → rescan → open project referencing it → row loads, passthrough audio, editor shows MISSING, no crash.
  - Corrupt sidecar (`entry.json` → garbage) → rescan skips folder with log, app fine.
  - Corrupt `.nam` (truncate a copy) → `loadRig` reports failure via `onEngineStateChanged`, passthrough, editor MISSING.
  - Sample-rate mismatch: if device offers 44.1k, switch to it with a 48k model loaded → 48k! badge shows, audio still runs.
- [ ] **Step 3: Stress sweep.**
  - Rapid rig swaps while audio runs (click different cards quickly in pick mode) — no glitches beyond the accepted passthrough blip, no crash, stale-load guard holds (final selection wins).
  - Project switch with amp editor open → window closes safely (existing disposal rule).
  - Remove an amp row while its editor is open → same.
  - Two amps on one channel + dual mode on both → CPU/audio stable (watch the app's CPU in Task Manager; log if >1 core).
- [ ] **Step 4: A2 predicate confirmation** (if still outstanding from Task 1/6): confirm with a real Tone3000 A2 download; adjust `isA2NamFile` if the version scheme differs; remove any remaining `#if JUCE_DEBUG` test scaffolding.
- [ ] **Step 5: Final full verification.** Clean build; launch; the Task 6(c) end-to-end pass; save/reopen round-trip with single + dual amps across two channels; update spec's deviation notes; update memory file `project_upstage.md` status.
- [ ] **Step 6: Commit.**

```bash
git add -A -- Source docs UpStage.jucer Builds/VisualStudio2022
git commit -m "Polish: NAM amp hardening (failure paths, stress, Full/Lite decision)"
```
