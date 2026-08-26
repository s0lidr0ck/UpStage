# Audio Dropout Investigation — 2026-08-26

Handoff notes. Written mid-investigation so the state survives a restart.

## Where we are right now

**Open question:** the NAM amp row takes ~6 ms per block (seen 5–11 ms). At 256
samples / 44.1 kHz the whole block budget is **5.8 ms**, so a single amp row
consumes the entire budget. This is the remaining cause of dropouts with the
Lead project loaded.

**Next step:** the running Release build reports `WORST: NAM:model` or
`WORST: NAM:cab` in the status bar, splitting that ~6 ms into model inference
vs cab convolution. Read that number first — the two have completely different
fixes:

- `NAM:cab` dominant → `NamAmpProcessor::Side::cab` is a default-constructed
  `juce::dsp::Convolution`, which resolves to `Convolution (Latency { 0 })` —
  JUCE's most CPU-expensive mode. Its own docs say a fixed non-zero latency
  reduces CPU. Switch to `NonUniform { 256 }`: keeps zero *added* latency and is
  much cheaper for longer IRs. Contained fix in `NamAmpProcessor.h`.
- `NAM:model` dominant → the inference is genuinely that heavy. Check which
  capture is loaded (a Full WaveNet is far heavier than Standard/Lite), whether
  dual mode is running two models, and what `Side::srMismatch` actually does
  when a model's expected rate differs from 44.1 kHz (it is set, but it is not
  clear anything acts on it).

## What was actually wrong (resolved)

### 1. My regression — device-loss watchdog (the big one)

`ab93e9f` added a watchdog that called `restartLastAudioDevice()`. That makes
`AudioDeviceManager` broadcast a change, which lands back in
`changeListenerCallback` → `checkAudioDeviceHealth()`; and mid-restart
`isPlaying()` is false, so the device still looks unhealthy and it restarts
again. **A self-sustaining loop, each iteration an audible glitch.**

Invisible to every counter, because the callback itself stayed fast.

Symptom: ~10 pops/second. Baseline was ~1 pop/3 seconds. **Currently stubbed out
with an early `return` in `checkAudioDeviceHealth()` — must be properly removed
or rewritten without the restart loop.**

### 2. System power settings (real, fixed)

Worst gap between audio callbacks was **27.7 ms** against a 5.8 ms block period
— the driver simply was not calling us. Fixed by:

- USB selective suspend → **disabled** (AC and DC)
- Minimum processor state → **30%** (was 0%)
- User created a **High Performance** power plan

Result: worst gap **27.7 ms → ~6.1 ms**. Late-callback count still climbs
slowly, so delivery is not perfect, but it is no longer the dominant issue.

### 3. Per-plugin heap allocation on the audio thread (real, fixed)

`ChannelStrip::processBlock` and `FxBus::processBlock` each did, **per plugin,
per block**:

```cpp
juce::MidiBuffer pluginMidi (midi);
juce::AudioBuffer<float> padded (expected, buffer.getNumSamples());
```

With 16 active plugins that is up to 32 `malloc`s per 5.8 ms, on the audio
thread. Pre-existing — present in the baseline build too. Now pre-allocated
members (`rtPluginMidi`, `rtPadded`) sized in `prepare()`.

### 4. Autosave bypassing the plugin chain (real, fixed — `7dab084`)

`getState()` held `chainLock` across `getStateInformation()` on every plugin.
The audio thread's `ScopedTryLock` then failed for hundreds of consecutive
blocks, and its fallback passes audio through the inserts **unprocessed** — so
each autosave produced two gross discontinuities. Fixed by snapshotting the
chain under the lock and releasing it before the slow calls.

## Ruled out (do not re-chase)

- **Plugin count / CPU load.** Pops occurred with a **totally blank project**,
  0.3% CPU, `BLK 0.0/5.8ms`, `OVR 0`.
- **Debug vs Release.** Release changed nothing; NAM still ~6 ms. The
  `_DEBUG` / `_ITERATOR_DEBUG_LEVEL=2` theory was wrong.
- **PluginModuleKeeper.** Disabling it made no difference to the dropouts.
- **DPC latency (after the power fix).** An `xperf -on Latency` trace produced
  an empty DPC section.
- **Lock contention.** `LOCK` counter stayed at 0 throughout.

## Instrumentation added (keep — this is what solved it)

Status bar now shows `CPU / BLK / PLG / OVR / GAP / WORST`:

| Field | Meaning |
|---|---|
| `BLK: x/y ms` | worst whole-callback time vs the block budget |
| `PLG` | worst time inside plugin `processBlock` calls |
| `OVR` | blocks that exceeded the budget — genuine CPU overload |
| `LOCK` | blocks passed through unprocessed due to lock contention |
| `GAP: x ms/n` | worst gap between callbacks, and count of late deliveries |
| `WORST` | slowest single plugin, by name |

`GAP` is the one that broke the case open. Everything else measured how long
*we* took; nothing measured whether the driver was calling us on time.

**Note:** JUCE's `getCpuUsage()` is a low-pass filtered average *and* is clamped
to 100%, so it can never show an overrun. Do not trust it for peaks.

## Tree state — NEEDS CLEANUP

Two changes are stubbed out with early `return`s and must be resolved:

1. **`MainComponent::checkAudioDeviceHealth()`** — my regression. Recommend
   removing the auto-restart entirely; detecting and reporting device loss is
   worth keeping, automatically restarting the device is not.
2. **`PluginModuleKeeper::keep()`** — not the cause, but it keeps every plugin's
   threads alive forever to prevent a crash that had not recently recurred.
   Recommend dropping it and addressing `.vst3_unloaded` crashes another way.

Also present: a baseline worktree at `C:/projects/A18/upstage-baseline`
(detached at `8099514`, the last commit before this work) for A/B testing.
Delete with `git worktree remove` when no longer needed.

## Diagnostics reference

- Crash dumps: `C:\projects\A18\UpStage\CrashDumps` (WER, full dumps, gitignored)
- Faulting module for past crashes: Windows Application event log
- DPC tracing: `xperf` from the Windows Performance Toolkit is installed;
  LatencyMon does **not** work here because Core Isolation (HVCI) is on and
  blocks its kernel driver
- **Core Isolation is still ON** — a known DPC-latency contributor, untested as
  a fix, requires a reboot and is a genuine security tradeoff
