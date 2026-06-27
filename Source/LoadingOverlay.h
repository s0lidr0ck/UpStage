#pragma once
#include <JuceHeader.h>

/**
 * LoadingOverlay — semi-transparent overlay with animated progress indicator.
 * Shows "Loading..." text and a sweeping bar while plugins load.
 * Stays visible for a minimum duration so the user always sees feedback.
 */
class LoadingOverlay : public juce::Component,
                       public juce::Timer
{
public:
    LoadingOverlay()
    {
        setInterceptsMouseClicks (true, true);
        setAlwaysOnTop (true);
    }

    void show (juce::Component* parent, const juce::String& message = "Loading...")
    {
        text = message;
        phase = 0.0f;
        showTimeMs = juce::Time::getMillisecondCounter();
        dismissRequested = false;
        setBounds (parent->getLocalBounds());
        parent->addAndMakeVisible (this);
        toFront (false);
        startTimerHz (30);
    }

    void dismiss()
    {
        dismissRequested = true;
        auto elapsed = juce::Time::getMillisecondCounter() - showTimeMs;
        if (elapsed >= minDisplayMs)
            doDismiss();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xcc0a0a0a));
        g.fillRect (bounds);

        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();

        auto box = juce::Rectangle<float> (0, 0, 260, 70).withCentre ({ cx, cy });
        g.setColour (juce::Colour (0xff1a1816));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (juce::Colour (0xff3a3632));
        g.drawRoundedRectangle (box, 6.0f, 1.0f);

        g.setColour (juce::Colour (0xffccbb99));
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
        g.drawText (text, box.removeFromTop (38), juce::Justification::centred);

        auto barArea = box.reduced (20, 8).removeFromTop (6);

        g.setColour (juce::Colour (0xff0a0a08));
        g.fillRoundedRectangle (barArea, 3.0f);

        float barW = barArea.getWidth() * 0.3f;
        float sweep = barArea.getX() + phase * (barArea.getWidth() - barW);
        g.setColour (juce::Colour (0xff44aa55));
        g.fillRoundedRectangle (sweep, barArea.getY(), barW, barArea.getHeight(), 3.0f);
    }

    void timerCallback() override
    {
        phase += 0.025f;
        if (phase > 1.0f) phase = 0.0f;
        repaint();

        if (dismissRequested)
        {
            auto elapsed = juce::Time::getMillisecondCounter() - showTimeMs;
            if (elapsed >= minDisplayMs)
                doDismiss();
        }
    }

private:
    void doDismiss()
    {
        stopTimer();
        setVisible (false);
        if (auto* p = getParentComponent())
            p->removeChildComponent (this);
    }

    juce::String text;
    float phase = 0.0f;
    juce::int64 showTimeMs = 0;
    bool dismissRequested = false;
    static constexpr juce::int64 minDisplayMs = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoadingOverlay)
};
