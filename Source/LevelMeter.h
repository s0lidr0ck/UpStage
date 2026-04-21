#pragma once
#include <JuceHeader.h>

/**
 * LevelMeter
 *
 * Vertical or horizontal VU meter with:
 *   - Green / yellow / red zones
 *   - Peak hold line (3-second hold before decay)
 *   - dBFS scale
 *
 * Usage:
 *   1. Add to your component layout
 *   2. Call pushLevel() from the audio thread with the RMS level (0.0–1.0 linear)
 *   3. The component repaints itself via a Timer
 *
 * Supports stereo: left and right channels shown side-by-side.
 */
class LevelMeter : public juce::Component,
                   public juce::TooltipClient,
                   private juce::Timer
{
public:
    enum class Orientation { Vertical, Horizontal };

    enum class ColourMode { Green, Amber };

    explicit LevelMeter (Orientation o = Orientation::Vertical, ColourMode cm = ColourMode::Green);
    ~LevelMeter() override;

    void setColourMode (ColourMode cm) { colourMode = cm; }

    /** Feed new peak level (0.0–1.0 linear). Call from audio thread. */
    void pushLevel (float leftLinear, float rightLinear = -1.0f);

    void paint  (juce::Graphics& g) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    Orientation orientation;
    ColourMode colourMode = ColourMode::Green;

    std::atomic<float> leftLevel  { 0.0f };
    std::atomic<float> rightLevel { 0.0f };

    float  displayLeft    = 0.0f;
    float  displayRight   = 0.0f;
    float  peakLeft       = 0.0f;
    float  peakRight      = 0.0f;
    int    peakHoldFrames = 0;

    static constexpr int   PEAK_HOLD_FRAMES = 60;   // ~2 sec at 30fps
    static constexpr float DECAY_RATE       = 0.05f; // per frame

    void timerCallback() override;
    void paintBar (juce::Graphics& g, juce::Rectangle<float> area,
                   float level, float peak) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};
