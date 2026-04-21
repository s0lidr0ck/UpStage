#pragma once
#include <JuceHeader.h>

class Metronome
{
public:
    enum class ClickSound { Sine, Tick, Woodblock };

    Metronome();

    void prepare (double sampleRate, int blockSize);
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setEnabled (bool on)
    {
        if (on) { sampleCounter = 0; tickCounter = 0; }
        enabled.store (on);
    }
    bool isEnabled() const      { return enabled.load(); }

    void setBPM (double bpm);
    double getBPM() const       { return currentBPM.load(); }

    void setVolume (float v)    { volume.store (juce::jlimit (0.0f, 1.0f, v)); }
    float getVolume() const     { return volume.load(); }

    bool consumeBeatFlash()     { return beatFlash.exchange (false); }

    void setTimeSignature (int numerator, int denominator);
    int getNumerator() const    { return timeSigNum; }
    int getDenominator() const  { return timeSigDen; }

    void setSubdivision (int sub);
    int  getSubdivision() const { return subdivision; }

    void setAccentFreq (double freq)  { accentFreq.store (freq); }
    double getAccentFreq() const      { return accentFreq.load(); }

    void setNormalFreq (double freq)  { normalFreq.store (freq); }
    double getNormalFreq() const      { return normalFreq.load(); }

    void setClickSound (ClickSound s) { clickSound.store (s); }
    ClickSound getClickSound() const  { return clickSound.load(); }

    void setAccentEnabled (bool on)   { accentOn.store (on); }
    bool isAccentEnabled() const      { return accentOn.load(); }

private:
    std::atomic<bool>       enabled    { false };
    std::atomic<double>     currentBPM { 120.0 };
    std::atomic<float>      volume     { 0.7f };
    std::atomic<bool>       beatFlash  { false };
    std::atomic<bool>       accentOn   { true };
    std::atomic<double>     accentFreq { 1500.0 };
    std::atomic<double>     normalFreq { 1000.0 };
    std::atomic<ClickSound> clickSound { ClickSound::Sine };

    double sampleRate   = 44100.0;
    int    timeSigNum   = 4;
    int    timeSigDen   = 4;
    int    subdivision  = 1;

    int    samplesPerTick   = 0;
    int    sampleCounter    = 0;
    int    tickCounter      = 0;
    int    clickSamplesLeft = 0;
    bool   isAccent_        = false;
    bool   isSubTick        = false;

    static constexpr int CLICK_DURATION = 800;

    double clickPhase    = 0.0;
    double clickPhaseInc = 0.0;

    void recalcTiming();
    float generateClickSample();
};
