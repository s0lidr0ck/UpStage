#pragma once
#include <JuceHeader.h>

class MidiMonitorWindow : public juce::DocumentWindow
{
public:
    MidiMonitorWindow()
        : DocumentWindow ("MIDI Monitor", juce::Colour (0xff1a1a2a), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        setContentOwned (&content, false);
        setResizable (true, false);
        setSize (420, 500);
        centreWithSize (420, 500);
    }

    void closeButtonPressed() override { setVisible (false); }

    void addMessage (const juce::MidiMessage& msg)
    {
        auto ts = juce::Time::getCurrentTime().formatted ("%H:%M:%S.");
        int ms = juce::Time::getCurrentTime().getMilliseconds();
        ts += juce::String (ms).paddedLeft ('0', 3);

        juce::String line = ts + "  ";

        if (msg.isNoteOn())
            line += "Note On   ch=" + juce::String (msg.getChannel())
                  + "  note=" + juce::MidiMessage::getMidiNoteName (msg.getNoteNumber(), true, true, 4)
                  + "  vel=" + juce::String (msg.getVelocity());
        else if (msg.isNoteOff())
            line += "Note Off  ch=" + juce::String (msg.getChannel())
                  + "  note=" + juce::MidiMessage::getMidiNoteName (msg.getNoteNumber(), true, true, 4);
        else if (msg.isController())
            line += "CC        ch=" + juce::String (msg.getChannel())
                  + "  cc=" + juce::String (msg.getControllerNumber())
                  + "  val=" + juce::String (msg.getControllerValue());
        else if (msg.isProgramChange())
            line += "PC        ch=" + juce::String (msg.getChannel())
                  + "  prog=" + juce::String (msg.getProgramChangeNumber());
        else if (msg.isPitchWheel())
            line += "Pitch     ch=" + juce::String (msg.getChannel())
                  + "  val=" + juce::String (msg.getPitchWheelValue());
        else if (msg.isAftertouch())
            line += "Aftertouch ch=" + juce::String (msg.getChannel())
                  + "  note=" + juce::String (msg.getNoteNumber())
                  + "  val=" + juce::String (msg.getAfterTouchValue());
        else if (msg.isChannelPressure())
            line += "ChPressure ch=" + juce::String (msg.getChannel())
                  + "  val=" + juce::String (msg.getChannelPressureValue());
        else if (msg.isSysEx())
            line += "SysEx     len=" + juce::String (msg.getSysExDataSize());
        else if (msg.isMidiClock())
            line += "Clock";
        else if (msg.isMidiStart())
            line += "Start";
        else if (msg.isMidiStop())
            line += "Stop";
        else if (msg.isMidiContinue())
            line += "Continue";
        else if (msg.isActiveSense() || msg.isMidiClock())
            return;
        else
        {
            auto data = msg.getRawData();
            int sz = msg.getRawDataSize();
            line += "Raw       [";
            for (int i = 0; i < juce::jmin (sz, 8); ++i)
                line += juce::String::toHexString (data[i]).paddedLeft ('0', 2) + " ";
            line = line.trimEnd() + "]";
        }

        content.addLine (line);
    }

private:
    struct Content : public juce::Component
    {
        juce::TextEditor editor;
        juce::TextButton clearButton { "Clear" };
        int lineCount = 0;

        Content()
        {
            editor.setMultiLine (true);
            editor.setReadOnly (true);
            editor.setScrollbarsShown (true);
            editor.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0e1a));
            editor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff00dd88));
            editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333344));
            editor.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff444466));
            addAndMakeVisible (editor);

            clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a3a));
            clearButton.onClick = [this]() { editor.clear(); lineCount = 0; };
            addAndMakeVisible (clearButton);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            clearButton.setBounds (area.removeFromBottom (28).reduced (4, 2));
            editor.setBounds (area);
        }

        void addLine (const juce::String& line)
        {
            if (lineCount > 2000)
            {
                editor.clear();
                lineCount = 0;
            }
            editor.moveCaretToEnd();
            editor.insertTextAtCaret (line + "\n");
            lineCount++;
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    Content content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMonitorWindow)
};
