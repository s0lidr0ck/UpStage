#include "FxBus.h"

//==============================================================================
FxBus::FxBus (juce::AudioPluginFormatManager& fm)
    : formatManager (fm)
{
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
        if (entry->processor != nullptr)
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
        if (entry->processor != nullptr)
            entry->processor->releaseResources();
}

//==============================================================================
bool FxBus::addPlugin (const juce::PluginDescription& desc,
                       std::function<void(bool)> callback)
{
    formatManager.createPluginInstanceAsync (
        desc, currentSampleRate, currentBlockSize,
        [this, callback, desc] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                const juce::String& errorMessage)
        {
            if (instance == nullptr)
            {
                juce::Logger::writeToLog ("FxBus: failed to load plugin: " + errorMessage);
                if (callback) callback (false);
                return;
            }

            {
                juce::ScopedLock sl (chainLock);
                if (pluginChain.size() >= MAX_FX_SLOTS)
                {
                    if (callback) callback (false);
                    return;
                }
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

            {
                juce::ScopedLock sl (chainLock);
                pluginChain.add (entry);
            }

            if (callback) callback (true);
        });

    return true;
}

void FxBus::removePlugin (int index)
{
    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (juce::isPositiveAndBelow (index, pluginChain.size()))
            entry = pluginChain.removeAndReturn (index);
    }
    disposeEntry (entry);
}

void FxBus::clearAllPlugins()
{
    juce::Array<PluginEntry*> doomed;
    {
        juce::ScopedLock sl (chainLock);
        doomed.swapWith (pluginChain);
    }
    for (auto* e : doomed)
        disposeEntry (e);
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

void FxBus::setPluginBypassed (int index, bool b)
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (index, pluginChain.size()))
        pluginChain[index]->bypassed = b;
}

bool FxBus::isPluginBypassed (int index) const
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (index, pluginChain.size()))
        return pluginChain[index]->bypassed;
    return false;
}

int FxBus::getNumPlugins() const
{
    juce::ScopedLock sl (chainLock);
    return pluginChain.size();
}

juce::AudioProcessor* FxBus::getPlugin (int index) const
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (index, pluginChain.size()))
        return pluginChain[index]->processor.get();
    return nullptr;
}

juce::String FxBus::getPluginIdentifier (int index) const
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (index, pluginChain.size()))
        return pluginChain[index]->identifier;
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

    if (pluginChain.isEmpty())
        return;

    for (auto* entry : pluginChain)
    {
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
    for (auto* entry : pluginChain)
    {
        PluginSlotState slot;
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
    int numToRestore = juce::jmin (pluginChain.size(), s.plugins.size());
    for (int i = 0; i < numToRestore; ++i)
    {
        const auto& slot = s.plugins.getReference (i);
        pluginChain[i]->bypassed = slot.isBypassed;
        pluginChain[i]->tint     = slot.tint;
        pluginChain[i]->nickname = slot.nickname;

        if (pluginChain[i]->processor != nullptr && slot.stateData.getSize() > 0)
            pluginChain[i]->processor->setStateInformation (
                slot.stateData.getData(), (int) slot.stateData.getSize());
    }
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
