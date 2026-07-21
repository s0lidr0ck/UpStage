#include "NamAmpProcessor.h"
#include "NamAmpEditor.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

//==============================================================================
// RBJ audio-EQ-cookbook coefficients. Computed on the audio thread when a tone
// knob moved (cheap transcendental math, no allocation).
void NamAmpProcessor::Biquad::makeLowShelf (double sr, double f0, double gainDb) noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * f0 / sr;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
    const double sqA   = 2.0 * std::sqrt (A) * alpha;

    const double a0 =          (A + 1) + (A - 1) * cosw + sqA;
    b0 = (float) ((A *        ((A + 1) - (A - 1) * cosw + sqA)) / a0);
    b1 = (float) ((2 * A *    ((A - 1) - (A + 1) * cosw))       / a0);
    b2 = (float) ((A *        ((A + 1) - (A - 1) * cosw - sqA)) / a0);
    a1 = (float) ((-2.0 *     ((A - 1) + (A + 1) * cosw))       / a0);
    a2 = (float) ((           (A + 1) + (A - 1) * cosw - sqA)   / a0);
}

void NamAmpProcessor::Biquad::makePeaking (double sr, double f0, double q, double gainDb) noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * f0 / sr;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);

    const double a0 = 1 + alpha / A;
    b0 = (float) ((1 + alpha * A)  / a0);
    b1 = (float) ((-2.0 * cosw)    / a0);
    b2 = (float) ((1 - alpha * A)  / a0);
    a1 = (float) ((-2.0 * cosw)    / a0);
    a2 = (float) ((1 - alpha / A)  / a0);
}

void NamAmpProcessor::Biquad::makeHighShelf (double sr, double f0, double gainDb) noexcept
{
    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * f0 / sr;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
    const double sqA   = 2.0 * std::sqrt (A) * alpha;

    const double a0 =          (A + 1) - (A - 1) * cosw + sqA;
    b0 = (float) ((A *        ((A + 1) + (A - 1) * cosw + sqA)) / a0);
    b1 = (float) ((-2 * A *   ((A - 1) + (A + 1) * cosw))       / a0);
    b2 = (float) ((A *        ((A + 1) + (A - 1) * cosw - sqA)) / a0);
    a1 = (float) ((2.0 *      ((A - 1) - (A + 1) * cosw))       / a0);
    a2 = (float) ((           (A + 1) - (A - 1) * cosw - sqA)   / a0);
}

//==============================================================================
NamAmpProcessor::NamAmpProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

NamAmpProcessor::~NamAmpProcessor()
{
    *alive = false;
    loaderPool.removeAllJobs (true, 4000);
    // sides[].model destroyed by member teardown - message thread, per the
    // chain's disposal rule (disposeEntry runs on the message thread).
}

//==============================================================================
void NamAmpProcessor::applySlimSize (nam::DSP* m) const
{
    if (auto* slim = dynamic_cast<nam::SlimmableModel*> (m))
        slim->SetSlimmableSize (useLite.load() ? 0.0 : 1.0);
}

void NamAmpProcessor::loadRig (int side, const juce::String& rigId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    side = juce::jlimit (0, 1, side);
    auto& s = sides[side];

    s.rigId = rigId;
    const int generation = ++loadGeneration[side];

    const auto* entry = AmpLibrary::instance().findById (rigId);
    if (rigId.isEmpty() || entry == nullptr || ! entry->namFile.existsAsFile())
    {
        // Missing rig: drop any loaded model and flag. Audio passes through.
        std::unique_ptr<nam::DSP> old;
        {
            juce::SpinLock::ScopedLockType lk (modelLock);
            old = std::move (s.model);
        }
        s.modelLive = false;
        s.failed    = rigId.isNotEmpty();     // empty id = intentional unload, not a failure
        s.srMismatch = false;
        s.slimmable  = false;
        s.rigName    = rigId.isEmpty() ? juce::String() : "(missing)";
        if (onEngineStateChanged) onEngineStateChanged();
        return;
    }

    s.rigName = entry->name;
    s.failed  = false;

    // If the rig pairs a cab and no explicit cab is set, resolve it now.
    if (s.cabId.isEmpty())
        setCab (side, {});

    const juce::String path = entry->namFile.getFullPathName();
    const double sr = sampleRate;
    const int mb = juce::jmax (16, maxBlock);

    loaderPool.addJob ([this, aliveRef = alive, path, side, generation, sr, mb]
    {
        std::unique_ptr<nam::DSP> newModel;
        std::string error;
        try
        {
            nam::DspLoadOptions options;
            options.prewarm = false;                     // Reset() below prewarms
            // Build the path from UTF-16 so non-ASCII filenames (e.g. "®")
            // survive on Windows; a UTF-8 std::string would be read as ANSI.
           #if JUCE_WINDOWS
            std::filesystem::path fsPath (path.toWideCharPointer());
           #else
            std::filesystem::path fsPath (path.toStdString());
           #endif
            newModel = nam::get_dsp (fsPath, options);
            if (newModel != nullptr && sr > 0)
                newModel->Reset (sr, mb);                // prewarms by default
        }
        catch (const std::exception& e) { error = e.what(); newModel.reset(); }
        catch (...)                     { error = "unknown error"; newModel.reset(); }

        juce::MessageManager::callAsync (
            [this, aliveRef, raw = newModel.release(), side, generation, error]
        {
            std::unique_ptr<nam::DSP> incoming (raw);    // owned again; deleted here if stale
            if (! aliveRef->load())
                return;                                  // processor is gone

            auto& sd = sides[side];
            if (loadGeneration[side].load() != generation)
                return;                                  // superseded by a newer load

            if (incoming == nullptr)
            {
                sd.lastLoadError = juce::String (error);
                juce::Logger::writeToLog ("NamAmp: model load failed: " + sd.lastLoadError);
                sd.failed = true;
                sd.modelLive = false;
            }
            else
            {
                const double expected = incoming->GetExpectedSampleRate();
                sd.srMismatch = (expected > 0 && sampleRate > 0
                                 && std::abs (expected - sampleRate) > 1.0);
                sd.slimmable = (dynamic_cast<nam::SlimmableModel*> (incoming.get()) != nullptr);
                applySlimSize (incoming.get());

                std::unique_ptr<nam::DSP> old;
                {
                    juce::SpinLock::ScopedLockType lk (modelLock);
                    old = std::move (sd.model);
                    sd.model = std::move (incoming);
                }
                sd.modelLive = true;
                sd.failed = false;
                // 'old' destroyed here, outside the lock, on the message thread.
            }

            if (onEngineStateChanged) onEngineStateChanged();
        });
    });
}

void NamAmpProcessor::setCab (int side, const juce::String& cabId)
{
    JUCE_ASSERT_MESSAGE_THREAD
    side = juce::jlimit (0, 1, side);
    auto& s = sides[side];
    s.cabId = cabId;

    // Resolve: explicit cab entry, else the rig's paired cab, else nothing.
    juce::File irFile;
    juce::String cabName;
    auto& lib = AmpLibrary::instance();

    if (cabId.isNotEmpty())
    {
        if (const auto* cab = lib.findById (cabId); cab != nullptr && cab->irFile.existsAsFile())
        {
            irFile  = cab->irFile;
            cabName = cab->name;
        }
    }
    else if (const auto* rig = lib.findById (s.rigId); rig != nullptr && rig->pairedCabId.isNotEmpty())
    {
        if (const auto* cab = lib.findById (rig->pairedCabId); cab != nullptr && cab->irFile.existsAsFile())
        {
            irFile  = cab->irFile;
            cabName = cab->name;
        }
    }

    if (irFile.existsAsFile())
    {
        s.cab.loadImpulseResponse (irFile,
                                   juce::dsp::Convolution::Stereo::no,
                                   juce::dsp::Convolution::Trim::yes,
                                   0,                                   // load whole IR
                                   juce::dsp::Convolution::Normalise::yes);
        s.cabName = cabName;
        s.cabLoaded = true;
    }
    else
    {
        s.cab.reset();
        s.cabName.clear();
        s.cabLoaded = false;
    }

    if (onEngineStateChanged) onEngineStateChanged();
}

void NamAmpProcessor::setCabEnabled (int side, bool enabled)
{
    sides[juce::jlimit (0, 1, side)].cabEnabled = enabled;
    if (onEngineStateChanged) onEngineStateChanged();
}

void NamAmpProcessor::setUseLite (bool lite)
{
    JUCE_ASSERT_MESSAGE_THREAD
    useLite = lite;

    // SetSlimmableSize is thread-safe but not RT-safe: hold the lock so the
    // audio thread's try-lock passes through while the size changes.
    juce::SpinLock::ScopedLockType lk (modelLock);
    for (auto& s : sides)
        if (s.model != nullptr)
            applySlimSize (s.model.get());
}

//==============================================================================
void NamAmpProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock   = samplesPerBlock;

    monoIn.setSize (1, samplesPerBlock);
    sideBuf[0].setSize (1, samplesPerBlock);
    sideBuf[1].setSize (1, samplesPerBlock);

    juce::dsp::ProcessSpec spec { newSampleRate, (juce::uint32) samplesPerBlock, 1 };

    {
        juce::SpinLock::ScopedLockType lk (modelLock);
        for (auto& s : sides)
        {
            s.cab.prepare (spec);
            if (s.model != nullptr)
            {
                s.model->Reset (newSampleRate, samplesPerBlock);
                const double expected = s.model->GetExpectedSampleRate();
                s.srMismatch = (expected > 0 && std::abs (expected - newSampleRate) > 1.0);
            }
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        toneLow[ch].reset();
        toneMid[ch].reset();
        toneHigh[ch].reset();
    }
    toneDirty = true;
}

void NamAmpProcessor::releaseResources() {}

void NamAmpProcessor::updateToneCoeffs() noexcept
{
    if (sampleRate <= 0)
        return;

    const double bassDb   = (bassKnob.load()   - 5.0) / 5.0 * 12.0;
    const double midDb    = (midKnob.load()    - 5.0) / 5.0 * 10.0;
    const double trebleDb = (trebleKnob.load() - 5.0) / 5.0 * 12.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        toneLow[ch].makeLowShelf   (sampleRate, 120.0, bassDb);
        toneMid[ch].makePeaking    (sampleRate, 800.0, 0.7, midDb);
        toneHigh[ch].makeHighShelf (sampleRate, 3200.0, trebleDb);
    }
}

void NamAmpProcessor::renderSide (Side& s, juce::AudioBuffer<float>& out, float trimDb, int n) noexcept
{
    out.copyFrom (0, 0, monoIn, 0, 0, n);
    if (trimDb != 0.0f)
        out.applyGain (0, n, juce::Decibels::decibelsToGain (trimDb));

    if (s.model != nullptr)
    {
        float* p = out.getWritePointer (0);
        float* io[1] = { p };
        s.model->process (io, io, n);       // in-place, mono
    }

    if (s.cabEnabled.load() && s.cabLoaded.load())
    {
        juce::dsp::AudioBlock<float> blk (out.getArrayOfWritePointers(), 1, (size_t) n);
        s.cab.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
}

void NamAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 1 || n > monoIn.getNumSamples())
        return;

    juce::SpinLock::ScopedTryLockType lk (modelLock);
    if (! lk.isLocked())
        return;                                        // mid-swap: clean passthrough

    const bool dual = dualMode.load();
    auto& A = sides[0];
    auto& B = sides[1];

    const bool haveA = (A.model != nullptr);
    const bool haveB = dual && (B.model != nullptr);
    if (! haveA && ! haveB)
        return;                                        // nothing loaded: passthrough

    // Mono sum + input gain.
    monoIn.copyFrom (0, 0, buffer, 0, 0, n);
    if (buffer.getNumChannels() > 1)
    {
        monoIn.addFrom (0, 0, buffer, 1, 0, n);
        monoIn.applyGain (0, n, 0.5f);
    }
    monoIn.applyGain (0, n, juce::Decibels::decibelsToGain (inputGainDb.load()));

    renderSide (A, sideBuf[0], sideTrimDbA.load(), n);
    if (dual)
    {
        renderSide (B, sideBuf[1], sideTrimDbB.load(), n);
        if (polarityFlipB.load())
            sideBuf[1].applyGain (0, n, -1.0f);
    }

    // Blend (equal power) + per-side equal-power pan into stereo.
    const float bl = dual ? blend.load() : 0.0f;
    const float gA = dual ? std::cos (bl * juce::MathConstants<float>::halfPi) : 1.0f;
    const float gB = dual ? std::sin (bl * juce::MathConstants<float>::halfPi) : 0.0f;

    auto panLR = [] (float pan, float& l, float& r) noexcept
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f
                        * juce::MathConstants<float>::pi;
        l = std::cos (a);
        r = std::sin (a);
    };

    float lA, rA, lB, rB;
    panLR (dual ? panA.load() : 0.0f, lA, rA);
    panLR (panB.load(), lB, rB);

    const int outChans = juce::jmin (2, buffer.getNumChannels());
    buffer.clear();
    buffer.addFrom (0, 0, sideBuf[0], 0, 0, n, gA * lA);
    if (outChans > 1) buffer.addFrom (1, 0, sideBuf[0], 0, 0, n, gA * rA);
    if (dual)
    {
        buffer.addFrom (0, 0, sideBuf[1], 0, 0, n, gB * lB);
        if (outChans > 1) buffer.addFrom (1, 0, sideBuf[1], 0, 0, n, gB * rB);
    }

    // Tone stack + output gain.
    if (toneDirty.exchange (false))
        updateToneCoeffs();

    for (int ch = 0; ch < outChans; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = toneHigh[ch].p (toneMid[ch].p (toneLow[ch].p (d[i])));
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (outputGainDb.load()));
}

//==============================================================================
juce::AudioProcessorEditor* NamAmpProcessor::createEditor() { return new NamAmpEditor (*this); }
bool NamAmpProcessor::hasEditor() const { return true; }

void NamAmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("NamAmp");
    xml.setAttribute ("dual",         dualMode.load() ? 1 : 0);
    xml.setAttribute ("inputGainDb",  (double) inputGainDb.load());
    xml.setAttribute ("bass",         (double) bassKnob.load());
    xml.setAttribute ("mid",          (double) midKnob.load());
    xml.setAttribute ("treble",       (double) trebleKnob.load());
    xml.setAttribute ("outputGainDb", (double) outputGainDb.load());
    xml.setAttribute ("blend",        (double) blend.load());
    xml.setAttribute ("panA",         (double) panA.load());
    xml.setAttribute ("panB",         (double) panB.load());
    xml.setAttribute ("polB",         polarityFlipB.load() ? 1 : 0);
    xml.setAttribute ("useLite",      useLite.load() ? 1 : 0);

    for (int i = 0; i < 2; ++i)
    {
        auto* sideXml = xml.createNewChildElement ("Side");
        sideXml->setAttribute ("idx",        i);
        sideXml->setAttribute ("rigId",      sides[i].rigId);
        sideXml->setAttribute ("cabId",      sides[i].cabId);
        sideXml->setAttribute ("cabEnabled", sides[i].cabEnabled.load() ? 1 : 0);
        sideXml->setAttribute ("trimDb",     (double) (i == 0 ? sideTrimDbA : sideTrimDbB).load());
    }

    copyXmlToBinary (xml, destData);
}

void NamAmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("NamAmp"))
        return;

    dualMode      = xml->getIntAttribute ("dual") != 0;
    inputGainDb   = (float) xml->getDoubleAttribute ("inputGainDb", 0.0);
    bassKnob      = (float) xml->getDoubleAttribute ("bass", 5.0);
    midKnob       = (float) xml->getDoubleAttribute ("mid", 5.0);
    trebleKnob    = (float) xml->getDoubleAttribute ("treble", 5.0);
    outputGainDb  = (float) xml->getDoubleAttribute ("outputGainDb", 0.0);
    blend         = (float) xml->getDoubleAttribute ("blend", 0.5);
    panA          = (float) xml->getDoubleAttribute ("panA", 0.0);
    panB          = (float) xml->getDoubleAttribute ("panB", 0.0);
    polarityFlipB = xml->getIntAttribute ("polB") != 0;
    useLite       = xml->getIntAttribute ("useLite") != 0;
    toneDirty     = true;

    for (auto* sideXml : xml->getChildWithTagNameIterator ("Side"))
    {
        const int i = juce::jlimit (0, 1, sideXml->getIntAttribute ("idx"));
        (i == 0 ? sideTrimDbA : sideTrimDbB) = (float) sideXml->getDoubleAttribute ("trimDb", 0.0);
        sides[i].cabEnabled = sideXml->getIntAttribute ("cabEnabled", 1) != 0;

        const auto rigId = sideXml->getStringAttribute ("rigId");
        const auto cabId = sideXml->getStringAttribute ("cabId");
        sides[i].cabId = cabId;
        if (rigId.isNotEmpty())
            loadRig (i, rigId);         // also resolves the cab (setCab) as needed
        if (cabId.isNotEmpty())
            setCab (i, cabId);
    }
}

void NamAmpProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc.name             = "NAM Amp";
    desc.descriptiveName  = "UpStage internal NAM A2 amp modeler";
    desc.pluginFormatName = "Internal";
    desc.category         = "Effect";
    desc.manufacturerName = "UpStage";
    desc.version          = "1.0";
    desc.fileOrIdentifier = kIdentifier;
    desc.isInstrument     = false;
    desc.numInputChannels  = 2;
    desc.numOutputChannels = 2;
    desc.uniqueId          = 0x4e414d31; // 'NAM1'
}
