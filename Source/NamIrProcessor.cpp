#include "NamIrProcessor.h"
#include "NamIrEditor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

NamIrProcessor::NamIrProcessor (Role r)
    : juce::AudioPluginInstance (BusesProperties()
                                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      role (r)
{
    mix = (r == Role::space) ? 0.3f : 1.0f;
}

NamIrProcessor::~NamIrProcessor()
{
    *alive = false;
    loaderPool.removeAllJobs (true, 4000);
}

juce::AudioProcessorEditor* NamIrProcessor::createEditor() { return new NamIrEditor (*this); }

//==============================================================================
void NamIrProcessor::setIr (const juce::String& entryId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    irId = entryId;
    const int generation = ++loadGeneration;

    auto dropNamModel = [this]
    {
        std::unique_ptr<nam::DSP> old;
        {
            juce::SpinLock::ScopedLockType lk (modelLock);
            old = std::move (namModel);
        }
        namLive = false;
        // old destroyed here, outside the lock, on the message thread
    };

    const auto* e = AmpLibrary::instance().findById (entryId);

    if (entryId.isEmpty() || e == nullptr
        || (! e->irFile.existsAsFile() && ! e->namFile.existsAsFile()))
    {
        conv.reset();
        irLoaded = false;
        dropNamModel();
        missing = entryId.isNotEmpty();
        irName = entryId.isEmpty() ? juce::String() : "(missing)";
    }
    else if (e->irFile.existsAsFile())
    {
        // wav IR -> convolution path; retire any NAM cab model.
        dropNamModel();
        conv.loadImpulseResponse (e->irFile,
                                  juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::yes,
                                  0,
                                  juce::dsp::Convolution::Normalise::yes);
        irLoaded = true;
        missing = false;
        irName = e->name;
    }
    else
    {
        // NAM cab capture -> model path; retire the convolution.
        conv.reset();
        irLoaded = false;
        missing = false;
        irName = e->name;

        const juce::String path = e->namFile.getFullPathName();
        const double sr = sampleRate;
        const int mb = juce::jmax (16, maxBlock);

        loaderPool.addJob ([this, aliveRef = alive, path, generation, sr, mb]
        {
            std::unique_ptr<nam::DSP> newModel;
            std::string error;
            try
            {
                nam::DspLoadOptions options;
                options.prewarm = false;
               #if JUCE_WINDOWS
                std::filesystem::path fsPath (path.toWideCharPointer());
               #else
                std::filesystem::path fsPath (path.toStdString());
               #endif
                newModel = nam::get_dsp (fsPath, options);
                if (newModel != nullptr && sr > 0)
                    newModel->Reset (sr, mb);
            }
            catch (const std::exception& ex) { error = ex.what(); newModel.reset(); }
            catch (...)                      { error = "unknown error"; newModel.reset(); }

            juce::MessageManager::callAsync (
                [this, aliveRef, raw = newModel.release(), generation, error]() mutable
            {
                std::unique_ptr<nam::DSP> incoming (raw);
                if (! aliveRef->load())
                    return;
                if (loadGeneration.load() != generation)
                    return;                              // superseded

                if (incoming == nullptr)
                {
                    juce::Logger::writeToLog ("NamIr: cab capture load failed: " + juce::String (error));
                    missing = true;
                    namLive = false;
                }
                else
                {
                    std::unique_ptr<nam::DSP> old;
                    {
                        juce::SpinLock::ScopedLockType lk (modelLock);
                        old = std::move (namModel);
                        namModel = std::move (incoming);
                    }
                    namLive = true;
                }
                if (onEngineStateChanged) onEngineStateChanged();
            });
        });
    }

    if (onEngineStateChanged) onEngineStateChanged();
}

//==============================================================================
void NamIrProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock   = samplesPerBlock;
    dryBuf.setSize (2, samplesPerBlock);
    monoBuf.setSize (1, samplesPerBlock);

    juce::dsp::ProcessSpec spec { newSampleRate, (juce::uint32) samplesPerBlock, 2 };
    conv.prepare (spec);

    juce::SpinLock::ScopedLockType lk (modelLock);
    if (namModel != nullptr)
        namModel->Reset (newSampleRate, samplesPerBlock);
}

void NamIrProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || chans == 0 || n > dryBuf.getNumSamples())
        return;

    const bool useNam = namLive.load();
    if (! useNam && ! irLoaded.load())
        return;                                          // nothing loaded: passthrough

    const float wet = juce::jlimit (0.0f, 1.0f, mix.load());
    if (wet > 0.0f)
    {
        for (int ch = 0; ch < chans; ++ch)
            dryBuf.copyFrom (ch, 0, buffer, ch, 0, n);

        if (useNam)
        {
            juce::SpinLock::ScopedTryLockType lk (modelLock);
            if (! lk.isLocked() || namModel == nullptr)
                return;                                  // mid-swap: clean passthrough

            monoBuf.copyFrom (0, 0, buffer, 0, 0, n);
            if (chans > 1)
            {
                monoBuf.addFrom (0, 0, buffer, 1, 0, n);
                monoBuf.applyGain (0, n, 0.5f);
            }
            float* p = monoBuf.getWritePointer (0);
            float* io[1] = { p };
            namModel->process (io, io, n);

            for (int ch = 0; ch < chans; ++ch)
                buffer.copyFrom (ch, 0, monoBuf, 0, 0, n);
        }
        else
        {
            juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
            conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }

        if (wet < 1.0f)
        {
            buffer.applyGain (0, n, wet);
            for (int ch = 0; ch < chans; ++ch)
                buffer.addFrom (ch, 0, dryBuf, ch, 0, n, 1.0f - wet);
        }
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (outputGainDb.load()));
}
