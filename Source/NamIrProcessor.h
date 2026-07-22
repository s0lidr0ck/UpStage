#pragma once

#include <JuceHeader.h>
#include "AmpLibrary.h"

/**
 * NamIrProcessor - a standalone IR row for the plugin chain.
 *
 * Two roles sharing one engine:
 *   cab   - speaker IR after a head row; 100% wet by default.
 *   space - room/reverb IR anywhere in the chain; dry/wet mix (default 30%).
 *
 * Stereo convolution via juce::dsp::Convolution (its own RT-safe background
 * loader), so mono cab IRs come out dual-mono and stereo space IRs stay wide.
 */
class NamIrProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr const char* kIdentifier = "UPSTAGE_INTERNAL:NAM_IR";

    enum class Role { cab, space };

    explicit NamIrProcessor (Role r = Role::cab)
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          role (r)
    {
        mix = (r == Role::space) ? 0.3f : 1.0f;
    }

    Role getRole() const { return role; }

    //==========================================================================
    void setIr (const juce::String& entryId)          // message thread
    {
        JUCE_ASSERT_MESSAGE_THREAD
        irId = entryId;

        const auto* e = AmpLibrary::instance().findById (entryId);
        if (entryId.isEmpty() || e == nullptr || ! e->irFile.existsAsFile())
        {
            conv.reset();
            irLoaded = false;
            missing = entryId.isNotEmpty();
            irName = entryId.isEmpty() ? juce::String() : "(missing)";
        }
        else
        {
            conv.loadImpulseResponse (e->irFile,
                                      juce::dsp::Convolution::Stereo::yes,
                                      juce::dsp::Convolution::Trim::yes,
                                      0,
                                      juce::dsp::Convolution::Normalise::yes);
            irLoaded = true;
            missing = false;
            irName = e->name;
        }
        if (onEngineStateChanged) onEngineStateChanged();
    }

    juce::String getIrId() const   { return irId; }
    juce::String getIrName() const { return irName; }
    bool hasIr() const             { return irLoaded.load(); }
    bool isMissing() const         { return missing.load(); }

    std::atomic<float> mix { 1.0f };            // 0 = dry, 1 = fully wet
    std::atomic<float> outputGainDb { 0.0f };

    std::function<void()> onEngineStateChanged;

    //==========================================================================
    const juce::String getName() const override { return role == Role::space ? "Space IR" : "Cab IR"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        dryBuf.setSize (2, samplesPerBlock);
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
        conv.prepare (spec);
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (2, buffer.getNumChannels());
        if (n == 0 || chans == 0 || n > dryBuf.getNumSamples() || ! irLoaded.load())
            return;

        const float wet = juce::jlimit (0.0f, 1.0f, mix.load());
        if (wet > 0.0f)
        {
            for (int ch = 0; ch < chans; ++ch)
                dryBuf.copyFrom (ch, 0, buffer, ch, 0, n);

            juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
            conv.process (juce::dsp::ProcessContextReplacing<float> (blk));

            if (wet < 1.0f)
            {
                buffer.applyGain (0, n, wet);
                for (int ch = 0; ch < chans; ++ch)
                    buffer.addFrom (ch, 0, dryBuf, ch, 0, n, 1.0f - wet);
            }
        }

        buffer.applyGain (juce::Decibels::decibelsToGain (outputGainDb.load()));
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;   // defined in NamIrEditor.h include site
    bool hasEditor() const override { return true; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        juce::XmlElement xml ("NamIr");
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
    juce::String irId, irName;                  // message thread
    std::atomic<bool> irLoaded { false };
    std::atomic<bool> missing  { false };
    juce::dsp::Convolution conv;
    juce::AudioBuffer<float> dryBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamIrProcessor)
};
