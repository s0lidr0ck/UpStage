#pragma once
#include <JuceHeader.h>

class StereoSpreadMeter : public juce::Component,
                          private juce::Timer
{
public:
    StereoSpreadMeter() { startTimerHz (30); }
    ~StereoSpreadMeter() override { stopTimer(); }

    void pushStereo (float midEnergy, float sideEnergy)
    {
        float total = midEnergy + sideEnergy;
        float spread = (total > 1e-8f) ? sideEnergy / total : 0.0f;
        float corr = (total > 1e-8f) ? (midEnergy - sideEnergy) / total : 1.0f;

        targetSpread.store (spread, std::memory_order_relaxed);
        targetCorrelation.store (corr, std::memory_order_relaxed);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (bounds, 3.0f);

        g.setColour (juce::Colour (0xff999999));
        g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f).withStyle ("Bold")));
        g.drawText ("SPREAD", bounds.removeFromBottom (13.0f), juce::Justification::centred, false);

        auto display = bounds.reduced (4.0f, 3.0f);
        display.removeFromBottom (1.0f);

        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRoundedRectangle (display.expanded (1.0f), 2.0f);
        g.setColour (juce::Colour (0xff1a1a2a));
        g.fillRoundedRectangle (display, 1.5f);

        float cx = display.getCentreX();

        // Center line
        g.setColour (juce::Colour (0xff444466));
        g.drawVerticalLine ((int) cx, display.getY() + 2, display.getBottom() - 2);

        // M/S labels
        g.setColour (juce::Colour (0xff666688));
        g.setFont (juce::Font (juce::FontOptions().withHeight (8.0f)));
        auto labelArea = display;
        g.drawText ("M", labelArea.removeFromLeft (12), juce::Justification::centred, false);
        g.drawText ("S", labelArea.removeFromRight (12), juce::Justification::centred, false);

        // Scale marks
        g.setColour (juce::Colour (0xff333355));
        float dispW = display.getWidth();
        for (float f = 0.25f; f < 1.0f; f += 0.25f)
        {
            int x1 = (int)(cx + f * dispW * 0.45f);
            int x2 = (int)(cx - f * dispW * 0.45f);
            g.drawVerticalLine (x1, display.getBottom() - 4, display.getBottom() - 1);
            g.drawVerticalLine (x2, display.getBottom() - 4, display.getBottom() - 1);
        }

        // Spread bar — from center outward, proportional to side energy
        float barH = display.getHeight() * 0.4f;
        float barY = display.getCentreY() - barH * 0.5f;
        float scaledSpread = juce::jmin (1.0f, displaySpread * 2.0f);
        float halfW = scaledSpread * dispW * 0.45f;

        if (halfW > 0.5f)
        {
            juce::Colour barCol = (displayCorrelation > 0.3f) ? juce::Colour (0xff4466aa)
                                : (displayCorrelation > 0.0f) ? juce::Colour (0xffaaaa44)
                                : juce::Colour (0xffaa4444);

            juce::ColourGradient barGrad (
                barCol.withAlpha (0.3f), cx, barY,
                barCol, cx + halfW, barY, false);
            g.setGradientFill (barGrad);
            g.fillRect (cx, barY, halfW, barH);

            juce::ColourGradient barGradL (
                barCol.withAlpha (0.3f), cx, barY,
                barCol, cx - halfW, barY, false);
            g.setGradientFill (barGradL);
            g.fillRect (cx - halfW, barY, halfW, barH);
        }

        // Correlation indicator at top (+1 = right = mono, -1 = left = anti-phase)
        float corrX = cx + displayCorrelation * dispW * 0.4f;

        float dotR = 3.0f;
        juce::Colour dotCol = (displayCorrelation > 0.3f) ? juce::Colour (0xff44cc66)
                            : (displayCorrelation > 0.0f) ? juce::Colour (0xffcccc44)
                            : juce::Colour (0xffcc4444);
        g.setColour (dotCol);
        g.fillEllipse (corrX - dotR, display.getY() + 2, dotR * 2, dotR * 2);

        // +1/-1 labels
        g.setColour (juce::Colour (0xff555577));
        g.setFont (juce::Font (juce::FontOptions().withHeight (7.0f)));
        g.drawText ("-1", (int)(display.getX()), (int)(display.getY()), 14, 10, juce::Justification::centred, false);
        g.drawText ("+1", (int)(display.getRight() - 14), (int)(display.getY()), 14, 10, juce::Justification::centred, false);

        // Glass
        {
            juce::ColourGradient glass (
                juce::Colour (0x12ffffff), cx, display.getY(),
                juce::Colour (0x00ffffff), cx, display.getY() + display.getHeight() * 0.4f, false);
            g.setGradientFill (glass);
            g.fillRoundedRectangle (display, 1.5f);
        }

        g.setColour (juce::Colour (0xff333333));
        g.drawRoundedRectangle (display, 1.5f, 1.0f);
    }

    void resized() override { repaint(); }
    static int preferredHeight() { return 38; }

private:
    std::atomic<float> targetSpread { 0.0f };
    std::atomic<float> targetCorrelation { 1.0f };
    float displaySpread = 0.0f;
    float displayCorrelation = 1.0f;

    void timerCallback() override
    {
        float ts = targetSpread.load (std::memory_order_relaxed);
        float tc = targetCorrelation.load (std::memory_order_relaxed);
        displaySpread += (ts - displaySpread) * 0.2f;
        displayCorrelation += (tc - displayCorrelation) * 0.2f;
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoSpreadMeter)
};
