#pragma once
#include <JuceHeader.h>

/**
 * ChannelColourButton  v0.4
 *
 * A small clickable swatch that lets the user pick a per-channel accent colour.
 * The chosen colour is used by:
 *   - The channel tab background tint
 *   - The LevelMeter bar colour
 *
 * Usage:
 *   ChannelColourButton swatch;
 *   swatch.onColourChanged = [this](juce::Colour c) { ... };
 *   addAndMakeVisible(swatch);
 *   swatch.setBounds(...);
 */
class ChannelColourButton : public juce::Component
{
public:
    // Default palette  (6 colours, user picks one by clicking)
    static juce::Array<juce::Colour> getDefaultPalette()
    {
        return {
            juce::Colour (0xff4040c0),  // blue (default)
            juce::Colour (0xff40a040),  // green
            juce::Colour (0xffcc3333),  // red
            juce::Colour (0xffaa6600),  // amber
            juce::Colour (0xff8830cc),  // purple
            juce::Colour (0xff20a0a0),  // teal
        };
    }

    std::function<void (juce::Colour)> onColourChanged;

    ChannelColourButton()
    {
        current = getDefaultPalette()[0];
        setTooltip ("Click to change channel colour");
    }

    void setColour (juce::Colour c) { current = c; repaint(); }
    juce::Colour getColour() const  { return current; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (current);
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawRoundedRectangle (b, 3.0f, 1.0f);

        // Small pencil icon hint
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x9c\x8f"), getLocalBounds(),
                    juce::Justification::centred, false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto palette = getDefaultPalette();
        juce::PopupMenu menu;
        for (int i = 0; i < palette.size(); ++i)
        {
            menu.addCustomItem (i + 1,
                std::make_unique<ColourSwatchItem> (palette[i]),
                nullptr,
                palette[i].toDisplayString (false));
        }
        menu.addSeparator();
        menu.addItem (100, "Custom colour...");

        menu.showMenuAsync ({}, [this, palette] (int result)
        {
            if (result >= 1 && result <= palette.size())
            {
                current = palette[result - 1];
                repaint();
                if (onColourChanged) onColourChanged (current);
            }
            else if (result == 100)
            {
#if JUCE_MODAL_LOOPS_PERMITTED
                juce::ColourSelector selector;
                selector.setCurrentColour (current);
                selector.setSize (300, 300);
                juce::DialogWindow::LaunchOptions opts;
                opts.content.setNonOwned (&selector);
                opts.dialogTitle = "Channel Colour";
                opts.launchAsync();
                // Note: for non-modal use, wire up ColourSelector::Listener instead
#endif
            }
        });
    }

private:
    juce::Colour current;

    // Inline swatch for PopupMenu
    struct ColourSwatchItem : public juce::PopupMenu::CustomComponent
    {
        juce::Colour col;
        explicit ColourSwatchItem (juce::Colour c) : col (c) {}

        void getIdealSize (int& w, int& h) override { w = 120; h = 22; }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (col);
            g.fillRoundedRectangle (b.reduced (3.0f, 2.0f), 3.0f);
        }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelColourButton)
};
