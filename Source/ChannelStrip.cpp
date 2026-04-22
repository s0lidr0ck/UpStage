#include "ChannelStrip.h"

ChannelStrip::ChannelStrip (int index, juce::AudioPluginFormatManager& fm)
    : channelIndex (index), formatManager (fm)
{
    name = "Channel " + juce::String (index + 1);
}

ChannelStrip::~ChannelStrip()
{
    juce::ScopedLock sl (chainLock);
    for (auto* e : pluginChain)
        delete e;
    pluginChain.clear();
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

void ChannelStrip::removePlugin (int chainIndex)
{
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (chainIndex, pluginChain.size()))
    {
        auto* entry = pluginChain.removeAndReturn (chainIndex);
        delete entry;
    }
}

void ChannelStrip::movePlugin (int fromIndex, int toIndex)
{
    juce::ScopedLock sl (chainLock);
    pluginChain.move (fromIndex, toIndex);
}

void ChannelStrip::openPluginEditor (int chainIndex)
{
    juce::ScopedLock sl (chainLock);
    if (! juce::isPositiveAndBelow (chainIndex, pluginChain.size())) return;

    auto* proc = pluginChain[chainIndex]->processor.get();
    if (proc == nullptr || ! proc->hasEditor()) return;

    // Schedule editor creation on the message thread; the window self-destructs
    // on close so no external owner is needed.
    juce::MessageManager::callAsync ([proc]()
    {
        // PluginEditorWindow — a DocumentWindow that deletes itself when closed.
        // Defined locally so it is tightly scoped to the async callback.
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
            // Deleting 'this' is safe here because DocumentWindow handles
            // the desktop-removal lifecycle correctly.
            void closeButtonPressed() override { delete this; }
        };

        new PluginEditorWindow (proc); // self-managing; no owner needed
    });
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
        juce::ScopedLock sl (chainLock);
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
