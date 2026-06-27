#pragma once
#include <JuceHeader.h>

/**
 * InputRouter
 *
 * Provides a single audio source to feed into the active channel strip.
 * Two modes:
 *   - LIVE:  audio comes directly from the ASIO input (guitar)
 *   - LOOP:  an audio file is played back in a loop
 *
 * In both modes the output is a single mono buffer (guitar is mono).
 * The active ChannelStrip will expand to stereo internally if needed.
 */
class InputRouter : private juce::ChangeListener
{
public:
    enum class Mode { Live, Loop };

    InputRouter();
    ~InputRouter() override;

    void prepare (double sampleRate, int blockSize);
    void releaseResources();

    /** Fill `buffer` with the next block of input audio.
        Call from the audio thread. */
    void fillNextBlock (juce::AudioBuffer<float>& buffer);

    //==========================================================================
    // Mode switching
    void setMode (Mode m);
    Mode getMode() const { return mode.load(); }

    //==========================================================================
    // Loop file player
    bool loadLoopFile (const juce::File& file);
    void setLoopPlaying (bool shouldPlay);
    bool isLoopPlaying() const;
    juce::String getLoopFileName() const;

    /** 0.0 - 1.0 normalised position in the loop file. */
    double getLoopPosition() const;
    void   setLoopPosition (double normalisedPos);

    std::atomic<float> loopVolume { 1.0f };  // adjust loop playback level

private:
    // mode and loopPlaying are read on the audio thread (fillNextBlock) and
    // written on the message thread, so they must be atomic.
    std::atomic<Mode>  mode { Mode::Live };
    double sampleRate = 44100.0;
    int    blockSize  = 256;

    // Loop file player
    juce::AudioFormatManager        formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource      transportSource;
    juce::ResamplingAudioSource     resamplingSource { &transportSource, false, 2 };

    juce::CriticalSection loaderLock;
    std::atomic<bool>     loopPlaying { false };
    juce::String          loopFileName;

    void changeListenerCallback (juce::ChangeBroadcaster*) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputRouter)
};
