#pragma once
#include <JuceHeader.h>

class Looper
{
public:
    enum class State { Idle, Recording, Playing, Overdubbing };

    Looper();

    void prepare (double sampleRate, int blockSize);
    void processBlock (juce::AudioBuffer<float>& buffer);

    void toggleRecord();
    void togglePlay();
    void stop();
    void clear();

    State getState() const { return state.load(); }

    void setVolume (float v)  { volume.store (juce::jlimit (0.0f, 1.0f, v)); }
    float getVolume() const   { return volume.load(); }

    double getPositionNormalised() const;
    int    getLoopLengthSamples() const { return loopLength; }

private:
    std::atomic<State> state { State::Idle };
    std::atomic<float> volume { 1.0f };

    juce::AudioBuffer<float> loopBuffer;
    int loopLength   = 0;
    int writePos     = 0;
    int readPos      = 0;

    double sampleRate = 44100.0;

    static constexpr int MAX_LOOP_SECONDS = 300;
};
