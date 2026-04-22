#pragma once
#include <JuceHeader.h>

class VuMeter : public juce::Component,
                private juce::Timer
{
public:
    enum class Theme { Amber, Black, Blue };

    explicit VuMeter (Theme t = Theme::Amber) : theme (t) { startTimerHz (45); }
    ~VuMeter() override { stopTimer(); }

    void pushLevel (float linearL, float linearR)
    {
        float mono = (linearL + linearR) * 0.5f;
        float db = juce::Decibels::gainToDecibels (mono, -60.0f);
        targetDb.store (db, std::memory_order_relaxed);
    }

    void pushDb (float db)
    {
        targetDb.store (db, std::memory_order_relaxed);
    }

    void setLabel (const juce::String& s) { label = s; repaint(); }
    void setMeterLabel (const juce::String& s) { meterLabel = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (bounds, 3.0f);

        float labelH = 13.0f;
        {
            g.setColour (juce::Colour (0xff999999));
            g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f).withStyle ("Bold")));
            g.drawText (label.isNotEmpty() ? label : "VU",
                        bounds.removeFromBottom (labelH),
                        juce::Justification::centred, false);
        }

        auto display = bounds.reduced (4.0f, 3.0f);
        display.removeFromBottom (1.0f);

        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRoundedRectangle (display.expanded (1.0f), 2.0f);

        // Theme-dependent faceplate
        juce::Colour faceTop, faceMid, faceBot, markCol, glowCol;
        switch (theme)
        {
            case Theme::Black:
                faceTop = juce::Colour (0xff3a3a3a);
                faceMid = juce::Colour (0xff2a2a2a);
                faceBot = juce::Colour (0xff1a1a1a);
                markCol = juce::Colour (0xffcccccc);
                glowCol = juce::Colour (0x15ffffff);
                break;
            case Theme::Blue:
                faceTop = juce::Colour (0xff2a4a7a);
                faceMid = juce::Colour (0xff1a3a6a);
                faceBot = juce::Colour (0xff102a55);
                markCol = juce::Colour (0xffccddff);
                glowCol = juce::Colour (0x1588bbff);
                break;
            case Theme::Amber:
            default:
                faceTop = juce::Colour (0xfff2c94c);
                faceMid = juce::Colour (0xffeebb3a);
                faceBot = juce::Colour (0xffda9b2e);
                markCol = juce::Colour (0xff3a2510);
                glowCol = juce::Colour (0x25ffffff);
                break;
        }

        {
            juce::ColourGradient bg (faceTop, display.getCentreX(), display.getY(),
                                     faceBot, display.getCentreX(), display.getBottom(), false);
            bg.addColour (0.3f, faceMid);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (display, 1.5f);
        }

        {
            juce::ColourGradient glow (glowCol, display.getCentreX(), display.getY() + 2,
                                       juce::Colour (0x00000000), display.getCentreX(), display.getCentreY(), false);
            g.setGradientFill (glow);
            g.fillRoundedRectangle (display, 1.5f);
        }

        // Grain
        {
            juce::Random rng (71);
            juce::uint8 grainAlpha = (theme == Theme::Amber) ? 6 : 4;
            for (int py = (int) display.getY(); py < (int) display.getBottom(); py += 2)
                for (int px = (int) display.getX(); px < (int) display.getRight(); px += 2)
                    if (rng.nextFloat() > 0.82f)
                    {
                        g.setColour (juce::Colour::fromRGBA (0, 0, 0, grainAlpha));
                        g.fillRect (px, py, 1, 1);
                    }
        }

        g.saveState();
        g.reduceClipRegion (display.toNearestInt());

        float cx = display.getCentreX();
        float dispH = display.getHeight();
        float pivotY = display.getBottom() + dispH * 0.13f;
        float radius = dispH * 1.30f;

        float sweepHalf = 0.34f * juce::MathConstants<float>::pi;
        float arcL = -sweepHalf;
        float arcR =  sweepHalf;

        struct ScaleMark { float norm; juce::String text; bool isRed; bool major; };
        ScaleMark scale[] = {
            { 0.00f,  "20", false, true  },
            { 0.15f,  "10", false, true  },
            { 0.23f,  "7",  false, true  },
            { 0.32f,  "5",  false, true  },
            { 0.44f,  "3",  false, true  },
            { 0.60f,  "1",  false, false },
            { 0.71f,  "0",  true,  true  },
            { 0.80f,  "1",  true,  false },
            { 0.88f,  "2",  true,  false },
            { 1.00f,  "3",  true,  true  },
        };

        float minorNorms[] = { 0.07f, 0.19f, 0.27f, 0.38f, 0.52f, 0.66f, 0.75f, 0.84f, 0.94f };

        float tickOuter = radius * 0.82f;
        float tickMajor = radius * 0.72f;
        float tickMinor = radius * 0.76f;
        float textR     = radius * 0.62f;

        juce::Colour redCol = (theme == Theme::Amber) ? juce::Colour (0xffaa1515) : juce::Colour (0xffff4444);

        g.setColour (markCol.withAlpha (0.5f));
        for (float mn : minorNorms)
        {
            float a = arcL + mn * (arcR - arcL);
            g.drawLine (cx + std::sin(a) * tickMinor, pivotY - std::cos(a) * tickMinor,
                        cx + std::sin(a) * tickOuter, pivotY - std::cos(a) * tickOuter, 0.7f);
        }

        float fs = juce::jlimit (7.0f, 10.0f, dispH * 0.22f);
        g.setFont (juce::Font (juce::FontOptions().withHeight (fs).withStyle ("Bold")));

        for (auto& m : scale)
        {
            float a = arcL + m.norm * (arcR - arcL);
            float inner = m.major ? tickMajor : tickMinor;

            g.setColour (m.isRed ? redCol : markCol);
            g.drawLine (cx + std::sin(a) * inner, pivotY - std::cos(a) * inner,
                        cx + std::sin(a) * tickOuter, pivotY - std::cos(a) * tickOuter,
                        m.major ? 1.3f : 0.8f);

            if (m.major)
            {
                float tx = cx + std::sin(a) * textR;
                float ty = pivotY - std::cos(a) * textR;
                g.drawText (m.text, juce::Rectangle<float>(tx - 9, ty - fs * 0.5f, 18, fs),
                            juce::Justification::centred, false);
            }
        }

        float arcRadius = radius * 0.79f;
        float zeroAngle = arcL + 0.71f * (arcR - arcL);
        {
            juce::Path normArc;
            normArc.addCentredArc (cx, pivotY, arcRadius, arcRadius, 0, arcL, zeroAngle, true);
            g.setColour (markCol);
            g.strokePath (normArc, juce::PathStrokeType (1.2f));
        }
        {
            juce::Path redArc;
            redArc.addCentredArc (cx, pivotY, arcRadius, arcRadius, 0, zeroAngle, arcR, true);
            g.setColour (redCol);
            g.strokePath (redArc, juce::PathStrokeType (2.0f));
        }

        // Meter label (VU / PEAK / LUFS)
        {
            float vuFs = juce::jlimit (7.0f, 10.0f, dispH * 0.20f);
            g.setColour (markCol);
            g.setFont (juce::Font (juce::FontOptions().withHeight (vuFs).withStyle ("Bold")));
            float vuY = pivotY - radius * 0.30f;
            g.drawText (meterLabel.isNotEmpty() ? meterLabel : "VU",
                        juce::Rectangle<float>(cx - 18, vuY, 36, vuFs + 2),
                        juce::Justification::centred, false);
        }

        // Needle
        {
            float norm = displayNorm();
            float angle = arcL + norm * (arcR - arcL);
            float tipR = radius * 0.86f;
            float nx = cx + std::sin(angle) * tipR;
            float ny = pivotY - std::cos(angle) * tipR;

            g.setColour (juce::Colour (0x18000000));
            g.drawLine (cx + 0.8f, pivotY + 0.8f, nx + 0.8f, ny + 0.8f, 2.0f);

            float tailR = radius * 0.06f;
            float perpX = std::cos(angle) * 0.9f;
            float perpY = std::sin(angle) * 0.9f;
            float bx = cx - std::sin(angle) * tailR;
            float by = pivotY + std::cos(angle) * tailR;

            juce::Path needle;
            needle.startNewSubPath (nx, ny);
            needle.lineTo (bx + perpX, by + perpY);
            needle.lineTo (bx - perpX, by - perpY);
            needle.closeSubPath();

            juce::Colour needleCol = (theme == Theme::Amber) ? juce::Colour (0xff111111) : juce::Colour (0xffeeeeee);
            g.setColour (needleCol);
            g.fillPath (needle);

            float capR = juce::jlimit (1.5f, 3.5f, dispH * 0.06f);
            g.setColour (theme == Theme::Amber ? juce::Colour (0xff333333) : juce::Colour (0xff888888));
            g.fillEllipse (cx - capR, pivotY - capR, capR * 2, capR * 2);
        }

        // Glass
        {
            juce::ColourGradient glass (
                juce::Colour (0x55ffffff), cx, display.getY(),
                juce::Colour (0x00ffffff), cx, display.getY() + dispH * 0.40f, false);
            g.setGradientFill (glass);
            g.fillRoundedRectangle (display, 1.5f);

            juce::ColourGradient sheen (
                juce::Colour (0x18ffffff), display.getX(), display.getY(),
                juce::Colour (0x00ffffff), display.getRight(), display.getY() + dispH * 0.55f, false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (display, 1.5f);

            juce::ColourGradient bottomShadow (
                juce::Colour (0x00000000), cx, display.getBottom() - dispH * 0.25f,
                juce::Colour (0x20000000), cx, display.getBottom(), false);
            g.setGradientFill (bottomShadow);
            g.fillRoundedRectangle (display, 1.5f);

            juce::Random rng2 (137);
            for (int py = (int) display.getY(); py < (int) display.getBottom(); py += 3)
                for (int px = (int) display.getX(); px < (int) display.getRight(); px += 3)
                    if (rng2.nextFloat() > 0.88f)
                    {
                        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 5));
                        g.fillRect (px, py, 1, 1);
                    }
        }

        g.restoreState();

        g.setColour (juce::Colour (0xff333333));
        g.drawRoundedRectangle (display, 1.5f, 1.0f);
    }

    void resized() override { repaint(); }
    static int preferredHeight() { return 62; }

private:
    Theme theme;
    std::atomic<float> targetDb { -60.0f };
    float displayDb = -60.0f;
    juce::String label;
    juce::String meterLabel;

    void timerCallback() override
    {
        float target = targetDb.load (std::memory_order_relaxed);
        float coeff = (target > displayDb) ? 0.25f : 0.04f;
        displayDb += (target - displayDb) * coeff;
        repaint();
    }

    float displayNorm() const
    {
        float db = juce::jlimit (-20.0f, 3.0f, displayDb);
        if (db <= -20.0f) return 0.0f;
        if (db >= 3.0f)   return 1.0f;
        if (db <= -10.0f) return juce::jmap (db, -20.0f, -10.0f, 0.0f, 0.15f);
        if (db <=  -5.0f) return juce::jmap (db, -10.0f,  -5.0f, 0.15f, 0.32f);
        if (db <=  -3.0f) return juce::jmap (db,  -5.0f,  -3.0f, 0.32f, 0.44f);
        if (db <=   0.0f) return juce::jmap (db,  -3.0f,   0.0f, 0.44f, 0.71f);
        return juce::jmap (db, 0.0f, 3.0f, 0.71f, 1.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeter)
};
