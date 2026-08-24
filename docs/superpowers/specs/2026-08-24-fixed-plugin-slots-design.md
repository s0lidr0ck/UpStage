# Fixed Plugin Slots

**Date:** 2026-08-24
**Status:** Approved for implementation

## Problem

Deleting a plugin slides every plugin below it up one position. A strip's chain
is a dense, packed list, so removing slot 3 makes the old slot 4 become slot 3,
and so on.

Three things follow from that:

1. **It doesn't match the mental model.** The panel already draws a fixed rack
   of slots (`rebuildSlots` clamps to `kMaxVisibleSlots` regardless of how many
   plugins exist). The display is a pedalboard; the model underneath is a list.
2. **It silently re-points MIDI bindings.** Slot on/off bindings are addressed
   by position (`slotBypass:ch0:3`). Deleting a plugin above slot 3 moves a
   different plugin under that footswitch with no indication.
3. **Plugins can go invisible.** Nothing caps the chain at `kMaxVisibleSlots`.
   A 9th plugin is appended, processes audio, and is never drawn.

## Decisions

Settled with the user before writing this:

| Question | Decision |
|---|---|
| Drag onto an occupied slot | **Swap the two.** Nothing else moves. |
| Where a new plugin lands | **The slot you clicked.** The `+ Add Plugin` button fills the first free slot. |
| Which strips | **All three kinds** — the 4 channel strips, the pre-FX input channel, the master FX bus. |
| Slots per strip | **12**, up from 8. Both of the user's main channels are at 8 of 8 today. |
| Where the extra height comes from | **The fader.** Plugin rows keep their current height. |

## Design

### Model

`pluginChain` becomes exactly `kNumSlots` (12) entries per strip, with `nullptr`
meaning an empty slot. Delete nulls its slot and leaves every neighbour alone.

`ChannelStrip::processBlock` already skips entries whose `processor` is null, so
the audio path needs one added `entry == nullptr` guard and nothing more. The
try-lock behaviour and the rest of the block are untouched.

12 slots becomes a real, enforced cap. An add with no free slot is refused and
reported, rather than creating a plugin the UI cannot show.

### API

Today's index means "position in a packed list". It becomes "slot number". The
accessors split so the two jobs `getNumPlugins()` currently does — bounding a
loop, and testing emptiness — stop being the same call.

| Member | Meaning |
|---|---|
| `getNumSlots()` | Always `kNumSlots`. The bound for every loop and range check. |
| `getPlugin(slot)` | The plugin, or `nullptr` when the slot is empty. |
| `isSlotEmpty(slot)` | The emptiness test. |
| `getNumPlugins()` | Kept, now meaning "how many slots are occupied". Only for "is this chain empty" questions. |
| `swapSlots(a, b)` | Replaces `movePlugin(from, to)`. |
| `addPlugin(desc, slot, cb)` | `slot = -1` keeps today's "first free slot" behaviour. |
| `addInternalRow(kind, slot, cb)` | Same. |

23 `getNumPlugins()` call sites need auditing against this split, most of them in
the two panel classes. `FxBus` gets the identical treatment.

### Serialization

Each saved `<Plugin>` gains a `slot` attribute. `PluginSlotState` gains a
matching `slotIndex` field. Empty slots are not written at all — they are gaps in
the `slot` sequence.

**Migration:** a `<Plugin>` with no `slot` attribute is placed at the next
sequential position. Every existing project therefore loads exactly as it does
today, packed from the top, and is written back out with explicit slots on the
next save. No project file needs converting by hand.

`slotIndex` also lets scene restore prefer a plugin's original slot, tightening
the identity-based matching added in `f3a3f98`.

### UI behaviour

- **Delete** clears its slot; neighbours stay put.
- **Drag** onto an occupied slot swaps the two; onto an empty slot, moves there.
- **Move Up / Move Down** swap with the neighbouring slot, enabled at the strip
  edges only when there is a slot to swap with.
- **Right-click an empty slot → Add Plugin / Add NAM Amp / ...** targets that
  slot. This requires `ChannelStripPanel::onAddPluginClicked` and
  `onAddInternalRow` to carry the slot index, which they currently do not.
- **`+ Add Plugin` button** fills the first free slot.

### Layout

`ChannelStripPanel::getPreferredHeight()` already derives from the slot count,
so it follows `kNumSlots` on its own. The hardcoded `strip.removeFromTop (230)`
in `MainComponent::resized()` becomes the panel's preferred height
(12 × 28 + 2 = 338).

The meters/fader row is sized as `strip.getHeight() - 38`, so it absorbs the
change with no further edit — the fader shrinks by the 108px the rack gains. At
the 640×1080 window this leaves roughly 440px of fader, down from roughly 550px.

## Files

- `ChannelStrip.h/.cpp` — model, API, slot-aware add/remove/swap, serialization
- `FxBus.h/.cpp` — the same treatment
- `ChannelStripPanel.h/.cpp` — slot-aware display, drag-to-swap, add-at-slot
- `FxBusPanel.h/.cpp` — the same
- `ProjectState.h` — `PluginSlotState::slotIndex`
- `SceneManager.cpp` — read/write the slot attribute
- `MainComponent.cpp` — `restoreChainInto`, add-plugin call sites, `resized()`

## Verification

No test harness exists in this repo, so verification is build plus in-app checks:

1. Delete a middle plugin — the ones below stay put, the slot reads empty.
2. Drag a plugin onto an occupied slot — the two swap, nothing else moves.
3. Right-click empty slot 7, add a plugin — it lands in slot 7.
4. Bind a footswitch to slot 5, delete slot 2, confirm the switch still controls
   what is in slot 5.
5. Load `Lead.upstage` (saved in the old format) — chains appear exactly as
   before, packed from the top.
6. Save, reload — slots round-trip including the gaps.
7. Fill all 12 slots, attempt a 13th — refused with a message, not silently
   dropped.
8. Switch scenes — restore still lands on the right plugins.
