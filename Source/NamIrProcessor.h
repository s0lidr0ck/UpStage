#pragma once

#include <JuceHeader.h>
#include "AmpLibrary.h"

namespace nam { class DSP; }

/**
 * NamIrProcessor - a standalone IR row for the plugin chain.
 *
 * Two roles sharing one engine:
 *   cab   - speaker sim after a head row; 100% wet by default.
 *   space - room/reverb IR anywhere in the chain; dry/wet mix (default 30%).
 *
 * Backs onto whichever file the library entry holds: wav IRs run through
 * stereo juce::dsp::Convolution; NAM cab captures (.nam) run through the NAM
 * engine (mono, duplicated to stereo) with the same mix/output controls.
 */
class NamIrProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr const char* kIdentifier = "UPSTAGE_INTERNAL:NAM_IR";

    enum class Role { cab, space };

    explicit NamIrProcessor (Role r = Role::cab);
    ~NamIrProcessor() override;

    Role getRole() const { return role; }

    /** Stable per-instance id for MIDI-learn param addressing (see MidiLearnHooks). */
    juce::String getInstanceId() const { return instanceId; }
    juce::String midiParamId (const juce::String& knob) const { return "nam:" + instanceId + ":" + knob; }

    //==========================================================================
    void setIr (const juce::String& entryId);         // message thread

    juce::String getIrId() const   { return irId; }
    juce::String getIrName() const { return irName; }
    bool hasIr() const             { return irLoaded.load() || namLive.load(); }
    bool isMissing() const         { return missing.load(); }

    std::atomic<float> mix { 1.0f };            // 0 = dry, 1 = fully wet
    std::atomic<float> outputGainDb { 0.0f };

    std::function<void()> onEngineStateChanged;

    //==========================================================================
    const juce::String getName() const override { return role == Role::space ? "Space IR" : "Cab IR"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        juce::XmlElement xml ("NamIr");
        xml.setAttribute ("uid", instanceId);
        xml.setAttribute ("role", role == Role::space ? "space" : "cab");
        xml.setAttribute ("irId", irId);
        xml.setAttribute ("mix", (double) mix.load());
        xml.setAttribute ("outputGainDb", (double) outputGainDb.load());
        copyXmlToBinary (xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName ("NamIr"))
            return;
        if (xml->hasAttribute ("uid"))
            instanceId = xml->getStringAttribute ("uid");
        role = xml->getStringAttribute ("role", "cab") == "space" ? Role::space : Role::cab;
        mix = (float) xml->getDoubleAttribute ("mix", role == Role::space ? 0.3 : 1.0);
        outputGainDb = (float) xml->getDoubleAttribute ("outputGainDb", 0.0);
        const auto id = xml->getStringAttribute ("irId");
        if (id.isNotEmpty())
            setIr (id);
    }

    void fillInPluginDescription (juce::PluginDescription& desc) const override
    {
        desc.name             = getName();
        desc.descriptiveName  = "UpStage internal IR loader";
        desc.pluginFormatName = "Internal";
        desc.category         = "Effect";
        desc.manufacturerName = "UpStage";
        desc.version          = "1.0";
        desc.fileOrIdentifier = kIdentifier;
        desc.isInstrument     = false;
        desc.numInputChannels  = 2;
        desc.numOutputChannels = 2;
        desc.uniqueId          = 0x4e414d32; // 'NAM2'
    }

private:
    Role role;
    juce::String instanceId = juce::Uuid().toString();
    juce::String irId, irName;                  // message thread
    std::atomic<bool> irLoaded { false };       // wav convolution path live
    std::atomic<bool> missing  { false };
    juce::dsp::Convolution conv;
    juce::AudioBuffer<float> dryBuf, monoBuf;

    // NAM cab-capture path (mirrors NamAmpProcessor's swap discipline).
    std::unique_ptr<nam::DSP> namModel;         // swapped under modelLock
    std::atomic<bool> namLive { false };
    juce::SpinLock modelLock;
    juce::ThreadPool loaderPool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<int> loadGeneration { 0 };
    double sampleRate = 0.0;
    int maxBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamIrProcessor)
};
