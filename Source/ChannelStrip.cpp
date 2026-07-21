#include "ChannelStrip.h"
#include "NamAmpProcessor.h"

ChannelStrip::ChannelStrip (int index, juce::AudioPluginFormatManager& fm)
    : channelIndex (index), formatManager (fm)
{
    name = "Channel " + juce::String (index + 1);
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
        if (entry->processor != nullptr)
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
        if (entry->processor != nullptr)
            entry->processor->releaseResources();
}

//==============================================================================
bool ChannelStrip::addPlugin (const juce::PluginDescription& desc,
                              std::function<void(bool)> callback)
{
    // Plugin loading is async — do it on the message thread
    formatManager.createPluginInstanceAsync (
        desc, currentSampleRate, currentBlockSize,
        [this, callback, desc] (std::unique_ptr<juce::AudioPluginInstance> instance,
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

            {
                juce::ScopedLock sl (chainLock);
                pluginChain.add (entry);
            }

            if (callback) callback (true);
        });
    return true;
}

void ChannelStrip::addAmp (std::function<void(bool)> callback)
{
    juce::MessageManager::callAsync ([this, callback]
    {
        auto amp = std::make_unique<NamAmpProcessor>();

        if (currentSampleRate > 0)
        {
            amp->setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
            amp->prepareToPlay (currentSampleRate, currentBlockSize);
        }

        if (playHead != nullptr)
            amp->setPlayHead (playHead);

        auto* entry = new PluginEntry();
        entry->processor  = std::move (amp);
        entry->identifier = NamAmpProcessor::kIdentifier;
        entry->bypassed   = false;

        {
            juce::ScopedLock sl (chainLock);
            pluginChain.add (entry);
        }

        if (callback) callback (true);
    });
}

void ChannelStrip::removePlugin (int chainIndex)
{
    PluginEntry* entry = nullptr;
    {
        juce::ScopedLock sl (chainLock);
        if (juce::isPositiveAndBelow (chainIndex, pluginChain.size()))
            entry = pluginChain.removeAndReturn (chainIndex);
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
    }
    for (auto* e : doomed)
        disposeEntry (e);
}

void ChannelStrip::movePlugin (int fromIndex, int toIndex)
{
    juce::ScopedLock sl (chainLock);
    pluginChain.move (fromIndex, toIndex);
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

void ChannelStrip::setPluginBypassed (int chainIndex, bool bypassed)
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (chainIndex, pluginChain.size()))
        pluginChain[chainIndex]->bypassed = bypassed;
}

bool ChannelStrip::isPluginBypassed (int chainIndex) const
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (chainIndex, pluginChain.size()))
        return pluginChain[chainIndex]->bypassed;
    return false;
}

int ChannelStrip::getNumPlugins() const
{
    juce::ScopedLock sl (chainLock);
    return pluginChain.size();
}

juce::AudioProcessor* ChannelStrip::getPlugin (int chainIndex) const
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (chainIndex, pluginChain.size()))
        return pluginChain[chainIndex]->processor.get();
    return nullptr;
}

const ChannelStrip::PluginEntry& ChannelStrip::getPluginEntry (int chainIndex) const
{
    juce::ScopedLock sl (chainLock);
    return *pluginChain[chainIndex];
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
ChannelState ChannelStrip::getState() const
{
    ChannelState state;
    state.name        = name;
    state.inputGain   = inputGainTarget.load();
    state.outputGain  = outputGainTarget.load();
    state.pan         = pan.load();

    juce::ScopedLock sl (chainLock);
    for (auto* entry : pluginChain)
    {
        PluginSlotState slot;
        slot.pluginIdentifier = entry->identifier;
        slot.pluginName       = entry->processor ? entry->processor->getName() : "";
        slot.isBypassed       = entry->bypassed;

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

    // Restore plugin states and bypass for existing chain
    juce::ScopedLock sl (chainLock);
    int numToRestore = juce::jmin (pluginChain.size(), state.plugins.size());
    for (int i = 0; i < numToRestore; ++i)
    {
        const auto& slot = state.plugins.getReference (i);
        pluginChain[i]->bypassed = slot.isBypassed;

        if (pluginChain[i]->processor != nullptr && slot.stateData.getSize() > 0)
            pluginChain[i]->processor->setStateInformation (
                slot.stateData.getData(), (int) slot.stateData.getSize());
    }
}
