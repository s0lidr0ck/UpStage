#include "ChannelStrip.h"
#include "NamAmpProcessor.h"
#include "NamIrProcessor.h"

ChannelStrip::ChannelStrip (int index, juce::AudioPluginFormatManager& fm)
    : channelIndex (index), formatManager (fm)
{
    name = "Channel " + juce::String (index + 1);

    // The rack is a fixed size from here on - every slot exists, most are empty.
    pluginChain.insertMultiple (0, nullptr, kNumSlots);
}

ChannelStrip::~ChannelStrip()
{
    // Detach the chain under the lock so the audio thread can no longer reach
    // any entry, then dispose each one (close editor window, delete plugin)
    // outside the lock — plugin/editor teardown can block and must never stall
    // the audio thread.
    juce::Array<PluginEntry*> doomed;
    {
        juce::ScopedLock sl (chainLock);
        doomed.swapWith (pluginChain);
    }
    for (auto* e : doomed)
        disposeEntry (e);
}

void ChannelStrip::disposeEntry (PluginEntry* entry)
{
    if (entry == nullptr)
        return;

    // Close (and delete) the editor window first so it can never reference a
    // destroyed processor. The window self-deletes via closeButtonPressed, so
    // only delete it here if it is still open.
    if (entry->editorWindow != nullptr)
        delete entry->editorWindow.getComponent();

    delete entry;
}

//==============================================================================
void ChannelStrip::prepare (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

    inputGainSmoothed.reset (sampleRate, 0.03);  // 30ms ramp
    outputGainSmoothed.reset (sampleRate, 0.03);
    inputGainSmoothed.setCurrentAndTargetValue (inputGainTarget.load());
    outputGainSmoothed.setCurrentAndTargetValue (outputGainTarget.load());

    juce::ScopedLock sl (chainLock);
    for (auto* entry : pluginChain)
    {
        if (entry != nullptr && entry->processor != nullptr)
        {
            entry->processor->setRateAndBufferSizeDetails (sampleRate, blockSize);
            entry->processor->prepareToPlay (sampleRate, blockSize);
        }
    }
}

void ChannelStrip::releaseResources()
{
    juce::ScopedLock sl (chainLock);
    for (auto* entry : pluginChain)
        if (entry != nullptr && entry->processor != nullptr)
            entry->processor->releaseResources();
}

//==============================================================================
bool ChannelStrip::isSlotEmpty (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (! juce::isPositiveAndBelow (slot, pluginChain.size())) return true;
    return pluginChain[slot] == nullptr;
}

int ChannelStrip::findFirstFreeSlot() const
{
    juce::ScopedLock sl (chainLock);
    for (int i = 0; i < pluginChain.size(); ++i)
        if (pluginChain[i] == nullptr)
            return i;
    return -1;
}

bool ChannelStrip::placeEntry (PluginEntry* entry, int slot,
                               const std::function<void(bool)>& callback)
{
    bool placed = false;
    {
        juce::ScopedLock sl (chainLock);

        if (slot < 0)
        {
            for (int i = 0; i < pluginChain.size(); ++i)
                if (pluginChain[i] == nullptr) { slot = i; break; }
        }

        if (juce::isPositiveAndBelow (slot, pluginChain.size())
            && pluginChain[slot] == nullptr)
        {
            pluginChain.set (slot, entry);
            placed = true;
        }
    }

    if (! placed)
    {
        // No room, or the caller asked for a slot that is already taken. Better
        // to refuse than to hold a plugin the rack cannot show.
        juce::Logger::writeToLog ("ChannelStrip: no free slot for plugin");
        disposeEntry (entry);
    }

    if (callback) callback (placed);
    return placed;
}

//==============================================================================
bool ChannelStrip::addPlugin (const juce::PluginDescription& desc,
                              int slot,
                              std::function<void(bool)> callback)
{
    // Plugin loading is async — do it on the message thread
    formatManager.createPluginInstanceAsync (
        desc, currentSampleRate, currentBlockSize,
        [this, callback, desc, slot] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                      const juce::String& errorMessage)
        {
            if (instance == nullptr)
            {
                juce::Logger::writeToLog ("ChannelStrip: Failed to load plugin: " + errorMessage);
                if (callback) callback (false);
                return;
            }

            // Configure stereo bus layout
            auto layout = instance->getBusesLayout();
            if (instance->getBus (true, 0) != nullptr)
                layout.getChannelSet (true, 0) = juce::AudioChannelSet::stereo();
            if (instance->getBus (false, 0) != nullptr)
                layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();
            instance->setBusesLayout (layout);

            instance->setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
            instance->prepareToPlay (currentSampleRate, currentBlockSize);

            if (playHead != nullptr)
                instance->setPlayHead (playHead);

            auto* entry = new PluginEntry();
            entry->processor  = std::move (instance);
            entry->identifier = desc.createIdentifierString();
            entry->bypassed   = false;

            placeEntry (entry, slot, callback);
        });
    return true;
}

void ChannelStrip::addInternalRow (int kind, int slot, std::function<void(bool)> callback)
{
    juce::MessageManager::callAsync ([this, kind, slot, callback]
    {
        std::unique_ptr<juce::AudioPluginInstance> proc;
        const char* identifier = nullptr;

        switch (kind)
        {
            default:
            case 0: proc = std::make_unique<NamAmpProcessor> (NamAmpProcessor::Role::amp);
                    identifier = NamAmpProcessor::kIdentifier; break;
            case 1: proc = std::make_unique<NamAmpProcessor> (NamAmpProcessor::Role::pedal);
                    identifier = NamAmpProcessor::kIdentifier; break;
            case 2: proc = std::make_unique<NamIrProcessor> (NamIrProcessor::Role::cab);
                    identifier = NamIrProcessor::kIdentifier; break;
            case 3: proc = std::make_unique<NamIrProcessor> (NamIrProcessor::Role::space);
                    identifier = NamIrProcessor::kIdentifier; break;
        }

        if (currentSampleRate > 0)
        {
            proc->setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
            proc->prepareToPlay (currentSampleRate, currentBlockSize);
        }

        if (playHead != nullptr)
            proc->setPlayHead (playHead);

        auto* entry = new PluginEntry();
        entry->processor  = std::move (proc);
        entry->identifier = identifier;
        entry->bypassed   = false;

        placeEntry (entry, slot, callback);
    });
}

void ChannelStrip::removePlugin (int slot)
{
    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (juce::isPositiveAndBelow (slot, pluginChain.size()))
        {
            entry = pluginChain[slot];
            // Empty the slot in place - neighbours keep their positions.
            pluginChain.set (slot, nullptr);
        }
    }
    // Dispose outside the lock: closing the editor and destroying the plugin
    // can block, and must not stall the audio thread waiting on chainLock.
    disposeEntry (entry);
}

void ChannelStrip::clearAllPlugins()
{
    juce::Array<PluginEntry*> doomed;
    {
        juce::ScopedLock sl (chainLock);
        doomed.swapWith (pluginChain);
        // Rebuild the empty rack; the array is never left short.
        pluginChain.insertMultiple (0, nullptr, kNumSlots);
    }
    for (auto* e : doomed)
        disposeEntry (e);
}

void ChannelStrip::swapSlots (int slotA, int slotB)
{
    juce::ScopedLock sl (chainLock);
    if (slotA == slotB) return;
    if (! juce::isPositiveAndBelow (slotA, pluginChain.size())) return;
    if (! juce::isPositiveAndBelow (slotB, pluginChain.size())) return;

    auto* a = pluginChain[slotA];
    pluginChain.set (slotA, pluginChain[slotB]);
    pluginChain.set (slotB, a);
}

void ChannelStrip::openPluginEditor (int chainIndex)
{
    // Must run on the message thread — it touches the GUI and the per-entry
    // editorWindow pointer, which only the message thread owns.
    JUCE_ASSERT_MESSAGE_THREAD

    // Resolve the entry under the lock, but do NOT build the window while
    // holding it (createEditor can be slow and would stall the audio thread).
    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (! juce::isPositiveAndBelow (chainIndex, pluginChain.size())) return;
        entry = pluginChain[chainIndex];
    }

    auto* proc = entry->processor.get();
    if (proc == nullptr || ! proc->hasEditor()) return;

    // Already open — just bring it to the front.
    if (entry->editorWindow != nullptr)
    {
        entry->editorWindow->toFront (true);
        return;
    }

    // PluginEditorWindow — a DocumentWindow that deletes itself when closed.
    struct PluginEditorWindow : public juce::DocumentWindow
    {
        PluginEditorWindow (juce::AudioProcessor* p)
            : juce::DocumentWindow (p->getName(),
                                    juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton,
                                    true /* addToDesktop */)
        {
            if (auto* editor = p->createEditorIfNeeded())
            {
                setContentOwned (editor, true);
                setResizable (true, false);
                centreWithSize (editor->getWidth(), editor->getHeight());
            }
            else
            {
                // Plugin has no visual editor — show a simple placeholder.
                auto* label = new juce::Label ("noEditor",
                                               "This plugin has no editor.");
                label->setSize (260, 60);
                label->setJustificationType (juce::Justification::centred);
                setContentOwned (label, true);
                centreWithSize (260, 60);
            }
            setVisible (true);
        }

        // Called by JUCE when the user clicks the window's close button.
        // Deleting 'this' nulls the owning entry's SafePointer automatically.
        void closeButtonPressed() override { delete this; }
    };

    // The entry's SafePointer tracks the window so it can be force-closed
    // (and never outlive the plugin) when the plugin is removed or the
    // project is switched.
    entry->editorWindow = new PluginEditorWindow (proc);
}

void ChannelStrip::setPluginBypassed (int slot, bool bypassed)
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        entry->bypassed = bypassed;
}

bool ChannelStrip::isPluginBypassed (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        return entry->bypassed;
    return false;
}

int ChannelStrip::getNumPlugins() const
{
    juce::ScopedLock sl (chainLock);
    int n = 0;
    for (auto* entry : pluginChain)
        if (entry != nullptr) ++n;
    return n;
}

juce::AudioProcessor* ChannelStrip::getPlugin (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        return entry->processor.get();
    return nullptr;
}

const ChannelStrip::PluginEntry* ChannelStrip::getPluginEntry (int slot) const
{
    juce::ScopedLock sl (chainLock);
    return pluginChain[slot];
}

//==============================================================================
void ChannelStrip::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Apply smoothed input gain
    inputGainSmoothed.setTargetValue (inputGainTarget.load());
    if (inputGainSmoothed.isSmoothing())
    {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float g = inputGainSmoothed.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[s] *= g;
        }
    }
    else
    {
        buffer.applyGain (inputGainSmoothed.getTargetValue());
    }

    {
        // Try-lock, never block: the message thread may be holding chainLock
        // while doing slow plugin state I/O (get/setStateInformation) during a
        // scene recall or project load. The audio thread must never wait on
        // that — if the chain is busy, pass this block through the inserts
        // untouched rather than freezing.
        juce::ScopedTryLock sl (chainLock);
        if (sl.isLocked())
        {
            for (auto* entry : pluginChain)
            {
                if (entry == nullptr)            continue;   // empty slot
                if (entry->processor == nullptr) continue;
                if (entry->bypassed)             continue;

                juce::MidiBuffer pluginMidi (midi);
                auto expected = entry->processor->getTotalNumInputChannels();
                if (expected > buffer.getNumChannels())
                {
                    juce::AudioBuffer<float> padded (expected, buffer.getNumSamples());
                    padded.clear();
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        padded.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
                    entry->processor->processBlock (padded, pluginMidi);
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.copyFrom (ch, 0, padded, ch, 0, buffer.getNumSamples());
                }
                else
                {
                    entry->processor->processBlock (buffer, pluginMidi);
                }
            }
        }
    }

    // Apply smoothed output gain
    outputGainSmoothed.setTargetValue (outputGainTarget.load());
    if (outputGainSmoothed.isSmoothing())
    {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            float g = outputGainSmoothed.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[s] *= g;
        }
    }
    else
    {
        buffer.applyGain (outputGainSmoothed.getTargetValue());
    }

    // Apply pan (equal-power)
    float p = pan.load();
    if (std::abs (p) > 0.001f && buffer.getNumChannels() >= 2)
    {
        float angle = (p + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
        float gainL = std::cos (angle);
        float gainR = std::sin (angle);
        buffer.applyGain (0, 0, buffer.getNumSamples(), gainL);
        buffer.applyGain (1, 0, buffer.getNumSamples(), gainR);
    }
}

//==============================================================================
void ChannelStrip::setActive (bool shouldBeActive)
{
    active = shouldBeActive;
}

void ChannelStrip::setInputGain  (float g) { inputGainTarget.store (g); }
void ChannelStrip::setOutputGain (float g) { outputGainTarget.store (g); }

//==============================================================================
void ChannelStrip::setPluginAppearance (int slot, juce::Colour tint, const juce::String& nickname)
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
    {
        entry->tint = tint;
        entry->nickname = nickname;
    }
}

ChannelState ChannelStrip::getState() const
{
    ChannelState state;
    state.name        = name;
    state.inputGain   = inputGainTarget.load();
    state.outputGain  = outputGainTarget.load();
    state.pan         = pan.load();

    juce::ScopedLock sl (chainLock);
    for (int i = 0; i < pluginChain.size(); ++i)
    {
        auto* entry = pluginChain[i];
        if (entry == nullptr) continue;   // empty slot - saved as a gap

        PluginSlotState slot;
        slot.slotIndex        = i;
        slot.pluginIdentifier = entry->identifier;
        slot.pluginName       = entry->processor ? entry->processor->getName() : "";
        slot.isBypassed       = entry->bypassed;
        slot.tint             = entry->tint;
        slot.nickname         = entry->nickname;

        if (entry->processor != nullptr)
            entry->processor->getStateInformation (slot.stateData);

        state.plugins.add (slot);
    }
    return state;
}

void ChannelStrip::setState (const ChannelState& state)
{
    name = state.name;
    setInputGain  (state.inputGain);
    setOutputGain (state.outputGain);
    setPan        (state.pan);

    juce::ScopedLock sl (chainLock);

    // Restore by plugin IDENTITY, not by chain position. A saved state blob only
    // means anything to the plugin it came from - handing it to a different
    // plugin is undefined behaviour and crashes some VST3s outright. Chains
    // drift all the time (a pedal added, the chain reordered, a whole channel
    // pasted over) while scenes keep the chain they were captured with, so
    // position alone cannot be trusted.
    juce::Array<bool> claimed;
    claimed.insertMultiple (0, false, pluginChain.size());

    // Prefer the same index - the overwhelmingly common case, and it keeps two
    // instances of the same plugin in their own slots - then fall back to any
    // unclaimed slot holding that plugin, so a reordered chain still restores.
    auto occupiedBy = [&] (int i, const juce::String& id)
    {
        return juce::isPositiveAndBelow (i, pluginChain.size())
            && ! claimed[i]
            && pluginChain[i] != nullptr
            && pluginChain[i]->identifier == id;
    };

    auto findMatch = [&] (const juce::String& id, int preferredIndex) -> int
    {
        if (occupiedBy (preferredIndex, id))
            return preferredIndex;

        for (int i = 0; i < pluginChain.size(); ++i)
            if (occupiedBy (i, id))
                return i;

        return -1;
    };

    int skipped = 0;

    for (int i = 0; i < state.plugins.size(); ++i)
    {
        const auto& slot = state.plugins.getReference (i);

        // Prefer the slot this plugin was actually saved in; fall back to
        // position for projects saved before slots were recorded.
        const int preferred = slot.slotIndex >= 0 ? slot.slotIndex : i;
        const int target = findMatch (slot.pluginIdentifier, preferred);
        if (target < 0)
        {
            // That plugin is no longer in this chain. Leave whatever is here
            // alone rather than corrupting it with a stranger's state.
            ++skipped;
            continue;
        }

        claimed.set (target, true);
        pluginChain[target]->bypassed = slot.isBypassed;
        pluginChain[target]->tint     = slot.tint;
        pluginChain[target]->nickname = slot.nickname;

        if (pluginChain[target]->processor != nullptr && slot.stateData.getSize() > 0)
            pluginChain[target]->processor->setStateInformation (
                slot.stateData.getData(), (int) slot.stateData.getSize());
    }

    if (skipped > 0)
        DBG ("ChannelStrip::setState: skipped " + juce::String (skipped)
             + " saved slot(s) whose plugin is no longer in the chain");
}
