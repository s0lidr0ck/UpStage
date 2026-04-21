#include "InputRouter.h"

InputRouter::InputRouter() : juce::Thread ("InputRouter")
{
    formatManager.registerBasicFormats(); // WAV, AIFF, MP3 (via OS decoders)
    transportSource.addChangeListener (this);
}

InputRouter::~InputRouter()
{
    transportSource.removeChangeListener (this);
    transportSource.stop();
    transportSource.setSource (nullptr);
}

//==============================================================================
void InputRouter::prepare (double sr, int bs)
{
    sampleRate = sr;
    blockSize  = bs;
    resamplingSource.prepareToPlay (bs, sr);
    transportSource.prepareToPlay  (bs, sr);
}

void InputRouter::releaseResources()
{
    transportSource.stop();
    resamplingSource.releaseResources();
    transportSource.releaseResources();
}

//==============================================================================
void InputRouter::fillNextBlock (juce::AudioBuffer<float>& buffer)
{
    buffer.clear();

    if (mode == Mode::Loop)
    {
        if (! loopPlaying) return;

        juce::AudioSourceChannelInfo info (&buffer, 0, buffer.getNumSamples());
        resamplingSource.getNextAudioBlock (info);
        buffer.applyGain (loopVolume);

        // If transport reached the end, loop it back
        if (transportSource.hasStreamFinished())
        {
            transportSource.setPosition (0.0);
            transportSource.start();
        }
    }
    // Mode::Live — audio is injected by MainComponent directly from ASIO callback
    // (buffer will be filled before processBlock is called)
}

//==============================================================================
void InputRouter::setMode (Mode m)
{
    mode = m;
    if (m == Mode::Live)
    {
        transportSource.stop();
        loopPlaying = false;
    }
}

bool InputRouter::loadLoopFile (const juce::File& file)
{
    juce::ScopedLock sl (loaderLock);

    transportSource.stop();
    transportSource.setSource (nullptr);
    readerSource.reset();

    auto* reader = formatManager.createReaderFor (file);
    if (reader == nullptr)
        return false;

    loopFileName = file.getFileName();
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transportSource.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    resamplingSource.setResamplingRatio (reader->sampleRate / sampleRate);
    return true;
}

void InputRouter::setLoopPlaying (bool shouldPlay)
{
    loopPlaying = shouldPlay;
    if (shouldPlay && mode == Mode::Loop)
        transportSource.start();
    else
        transportSource.stop();
}

bool InputRouter::isLoopPlaying() const
{
    return loopPlaying && mode == Mode::Loop;
}

juce::String InputRouter::getLoopFileName() const
{
    return loopFileName;
}

double InputRouter::getLoopPosition() const
{
    double len = transportSource.getLengthInSeconds();
    return (len > 0.0) ? (transportSource.getCurrentPosition() / len) : 0.0;
}

void InputRouter::setLoopPosition (double normPos)
{
    double len = transportSource.getLengthInSeconds();
    if (len > 0.0)
        transportSource.setPosition (normPos * len);
}
