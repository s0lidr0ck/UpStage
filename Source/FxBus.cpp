#include "FxBus.h"

//==============================================================================
FxBus::FxBus (juce::AudioPluginFormatManager& fm)
    : formatManager (fm)
{
}

FxBus::~FxBus()
{
    juce::ScopedLock sl (chainLock);
    for (auto* e : pluginChain)
        delete e;
    pluginChain.clear();
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
    juce::ScopedLock sl (chainLock);
    if (juce::isPositiveAndBelow (index, pluginChain.size()))
    {
        auto* entry = pluginChain.removeAndReturn (index);
        delete entry;
    }
}

void FxBus::openPluginEditor (int index)
{
    juce::ScopedLock sl (chainLock);
    if (! juce::isPositiveAndBelow (index, pluginChain.size())) return;

    auto* proc = pluginChain[index]->processor.get();
    if (proc == nullptr || ! proc->hasEditor()) return;

    juce::MessageManager::callAsync ([proc]()
    {
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

        new FxEditorWindow (proc);
    });
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

//==============================================================================
void  FxBus::setBypassed (bool b)  { bypassed.store (b); }
bool  FxBus::isBypassed()  const   { return bypassed.load(); }

//==============================================================================
void FxBus::processBlock (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (bypassed.load (std::memory_order_relaxed))
        return;

    juce::ScopedLock sl (chainLock);

    if (pluginChain.isEmpty())
        return;

    juce::MidiBuffer dummyMidi;

    for (auto* entry : pluginChain)
    {
        if (entry->processor == nullptr) continue;
        if (entry->bypassed)             continue;

        auto expected = entry->processor->getTotalNumInputChannels();
        if (expected > buffer.getNumChannels())
        {
            juce::AudioBuffer<float> padded (expected, buffer.getNumSamples());
            padded.clear();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                padded.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
            entry->processor->processBlock (padded, dummyMidi);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, 0, padded, ch, 0, buffer.getNumSamples());
        }
        else
        {
            entry->processor->processBlock (buffer, dummyMidi);
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

        if (entry->processor != nullptr)
            entry->processor->getStateInformation (slot.stateData);

        s.plugins.add (slot);
    }
    return s;
}

void FxBus::setState (const State& s)
{
    bypassed.store (s.bypassed);
}
