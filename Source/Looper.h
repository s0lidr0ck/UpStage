#pragma once
#include <JuceHeader.h>

class Looper
{
public:
    enum class State { Idle, CountIn, Recording, Playing, Overdubbing, OverdubPending };
    enum class CapturePoint { Output, Input };

    Looper();

    void prepare (double sampleRate, int blockSize);

    void processBlock (juce::AudioBuffer<float>& buffer);
    void feedInput (const juce::AudioBuffer<float>& inputBuffer, int numSamples);

    void toggleRecord();
    void togglePlayStop();
    void startOverdub();
    void stop();
    void clear();

    State getState() const { return state.load(); }

    void setVolume (float v)  { volume.store (juce::jlimit (0.0f, 1.0f, v)); }
    float getVolume() const   { return volume.load(); }

    double getPositionNormalised() const;
    int    getLoopLengthSamples() const { return loopLength; }
    double getLoopLengthSeconds() const { return loopLength > 0 ? (double) loopLength / sampleRate : 0.0; }
    double getElapsedSeconds() const;

    void setBPM (double bpm)          { currentBPM = bpm; }
    void setCountInBeats (int beats)  { countInBeats = beats; }
    int  getCountInBeats() const      { return countInBeats; }
    void setMeter (int num, int den)  { meterNum = num; meterDen = den; }
    int  getMeterNum() const          { return meterNum; }
    int  getMeterDen() const          { return meterDen; }
    void setLoopBars (int bars)       { loopBars = bars; }
    int  getLoopBars() const          { return loopBars; }

    void setCapturePoint (CapturePoint cp) { capturePoint.store (cp); }
    CapturePoint getCapturePoint() const   { return capturePoint.load(); }

    bool exportToFile (const juce::File& file) const;

    std::function<void()> onRecordingStarted;
    std::function<void()> onRecordingStopped;

private:
    std::atomic<State> state { State::Idle };
    std::atomic<float> volume { 1.0f };
    std::atomic<CapturePoint> capturePoint { CapturePoint::Output };

    juce::AudioBuffer<float> loopBuffer;
    juce::AudioBuffer<float> inputCapture;
    int loopLength   = 0;
    int writePos     = 0;
    int readPos      = 0;

    double sampleRate  = 44100.0;
    double currentBPM  = 120.0;

    int countInBeats   = 0;
    int meterNum       = 4;
    int meterDen       = 4;
    int loopBars       = 0;

    int countInSamplesLeft = 0;
    int countInBeatPos     = 0;
    int samplesPerBeat     = 0;

    int fixedLoopLength    = 0;

    int clickSamplesLeft   = 0;
    double clickPhase      = 0.0;
    double clickPhaseInc   = 0.0;
    bool   isAccentClick   = false;

    static constexpr int MAX_LOOP_SECONDS = 300;
    static constexpr int CLICK_DURATION   = 800;
    static constexpr double ACCENT_FREQ   = 1500.0;
    static constexpr double NORMAL_FREQ   = 1000.0;
    static constexpr float  CLICK_VOLUME  = 0.5f;

    void startCountIn();
    void triggerClick (bool accent);
    float generateClickSample();
    int  calcSamplesPerBeat() const;
    int  calcFixedLoopLength() const;
    void beginRecording();
    void finishRecording();
};
