#include "FxBus.h"

//==============================================================================
FxBus::FxBus (juce::AudioPluginFormatManager& fm)
    : formatManager (fm)
{
    // Fixed rack: every slot exists from here on, most are empty.
    pluginChain.insertMultiple (0, nullptr, MAX_FX_SLOTS);
}

FxBus::~FxBus()
{
    // Detach under the lock, dispose outside it — see ChannelStrip for rationale.
    juce::Array<PluginEntry*> doomed;
    {
        juce::ScopedLock sl (chainLock);
        doomed.swapWith (pluginChain);
    }
    for (auto* e : doomed)
        disposeEntry (e);
}

void FxBus::disposeEntry (PluginEntry* entry)
{
    if (entry == nullptr)
        return;

    // Close the editor window first so it can never reference a destroyed
    // processor. The window self-deletes via closeButtonPressed, so only
    // delete it here if it is still open.
    if (entry->editorWindow != nullptr)
        delete entry->editorWindow.getComponent();

    delete entry;
}

//==============================================================================
void FxBus::prepare (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = blockSize;

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

void FxBus::releaseResources()
{
    juce::ScopedLock sl (chainLock);
    for (auto* entry : pluginChain)
        if (entry != nullptr && entry->processor != nullptr)
            entry->processor->releaseResources();
}

//==============================================================================
bool FxBus::isSlotEmpty (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (! juce::isPositiveAndBelow (slot, pluginChain.size())) return true;
    return pluginChain[slot] == nullptr;
}

int FxBus::findFirstFreeSlot() const
{
    juce::ScopedLock sl (chainLock);
    for (int i = 0; i < pluginChain.size(); ++i)
        if (pluginChain[i] == nullptr)
            return i;
    return -1;
}

bool FxBus::placeEntry (PluginEntry* entry, int slot,
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
        juce::Logger::writeToLog ("FxBus: no free slot for plugin");
        disposeEntry (entry);
    }

    if (callback) callback (placed);
    return placed;
}

bool FxBus::addPlugin (const juce::PluginDescription& desc,
                       int slot,
                       std::function<void(bool)> callback)
{
    formatManager.createPluginInstanceAsync (
        desc, currentSampleRate, currentBlockSize,
        [this, callback, desc, slot] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                      const juce::String& errorMessage)
        {
            if (instance == nullptr)
            {
                juce::Logger::writeToLog ("FxBus: failed to load plugin: " + errorMessage);
                if (callback) callback (false);
                return;
            }

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

            auto* entry       = new PluginEntry();
            entry->processor  = std::move (instance);
            entry->identifier = desc.createIdentifierString();
            entry->bypassed   = false;

            placeEntry (entry, slot, callback);
        });

    return true;
}

void FxBus::removePlugin (int slot)
{
    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (juce::isPositiveAndBelow (slot, pluginChain.size()))
        {
            entry = pluginChain[slot];
            pluginChain.set (slot, nullptr);   // empty in place
        }
    }
    disposeEntry (entry);
}

void FxBus::clearAllPlugins()
{
    juce::Array<PluginEntry*> doomed;
    {
        juce::ScopedLock sl (chainLock);
        doomed.swapWith (pluginChain);
        pluginChain.insertMultiple (0, nullptr, MAX_FX_SLOTS);
    }
    for (auto* e : doomed)
        disposeEntry (e);
}

void FxBus::swapSlots (int slotA, int slotB)
{
    juce::ScopedLock sl (chainLock);
    if (slotA == slotB) return;
    if (! juce::isPositiveAndBelow (slotA, pluginChain.size())) return;
    if (! juce::isPositiveAndBelow (slotB, pluginChain.size())) return;

    auto* a = pluginChain[slotA];
    pluginChain.set (slotA, pluginChain[slotB]);
    pluginChain.set (slotB, a);
}

void FxBus::openPluginEditor (int index)
{
    // Message thread only — touches the GUI and the per-entry editorWindow.
    JUCE_ASSERT_MESSAGE_THREAD

    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (! juce::isPositiveAndBelow (index, pluginChain.size())) return;
        entry = pluginChain[index];
    }

    auto* proc = entry->processor.get();
    if (proc == nullptr || ! proc->hasEditor()) return;

    if (entry->editorWindow != nullptr)
    {
        entry->editorWindow->toFront (true);
        return;
    }

    struct FxEditorWindow : public juce::DocumentWindow
    {
        FxEditorWindow (juce::AudioProcessor* p)
            : juce::DocumentWindow ("Master: " + p->getName(),
                                    juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton,
                                    true)
        {
            if (auto* editor = p->createEditorIfNeeded())
            {
                setContentOwned (editor, true);
                setResizable (true, false);
                centreWithSize (editor->getWidth(), editor->getHeight());
            }
            setVisible (true);
        }
        void closeButtonPressed() override { delete this; }
    };

    entry->editorWindow = new FxEditorWindow (proc);
}

void FxBus::setPluginBypassed (int slot, bool b)
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        entry->bypassed = b;
}

bool FxBus::isPluginBypassed (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        return entry->bypassed;
    return false;
}

int FxBus::getNumPlugins() const
{
    juce::ScopedLock sl (chainLock);
    int n = 0;
    for (auto* entry : pluginChain)
        if (entry != nullptr) ++n;
    return n;
}

juce::AudioProcessor* FxBus::getPlugin (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        return entry->processor.get();
    return nullptr;
}

juce::String FxBus::getPluginIdentifier (int slot) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[slot])
        return entry->identifier;
    return {};
}

//==============================================================================
void  FxBus::setBypassed (bool b)  { bypassed.store (b); }
bool  FxBus::isBypassed()  const   { return bypassed.load(); }

//==============================================================================
void FxBus::processBlock (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::MidiBuffer empty;
    processBlock (buffer, numSamples, empty);
}

void FxBus::processBlock (juce::AudioBuffer<float>& buffer, int numSamples, juce::MidiBuffer& midi)
{
    if (bypassed.load (std::memory_order_relaxed))
        return;

    // Try-lock, never block: the message thread may hold chainLock while doing
    // slow plugin state I/O during a scene recall or project load. Pass the
    // block through untouched rather than stalling the audio thread.
    juce::ScopedTryLock sl (chainLock);
    if (! sl.isLocked())
        return;

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

//==============================================================================
FxBus::State FxBus::getState() const
{
    State s;
    s.bypassed = bypassed.load();

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

        s.plugins.add (slot);
    }
    return s;
}

void FxBus::setState (const State& s)
{
    bypassed.store (s.bypassed);

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

    for (int i = 0; i < s.plugins.size(); ++i)
    {
        const auto& slot = s.plugins.getReference (i);

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
        DBG ("FxBus::setState: skipped " + juce::String (skipped)
             + " saved slot(s) whose plugin is no longer in the chain");
}

void FxBus::setPluginAppearance (int chainIndex, juce::Colour tint, const juce::String& nickname)
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[chainIndex])
    {
        entry->tint = tint;
        entry->nickname = nickname;
    }
}

juce::Colour FxBus::getPluginTint (int chainIndex) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[chainIndex])
        return entry->tint;
    return juce::Colour (0x00000000);
}

juce::String FxBus::getPluginNickname (int chainIndex) const
{
    juce::ScopedLock sl (chainLock);
    if (auto* entry = pluginChain[chainIndex])
        return entry->nickname;
    return {};
}
