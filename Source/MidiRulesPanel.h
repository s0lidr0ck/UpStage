#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

class MidiRulesPanel : public juce::Component,
                       public juce::TableListBoxModel
{
public:
    MidiRulesPanel (const juce::Array<MidiRule>& existingRules)
        : rules (existingRules)
    {
        table.setModel (this);
        table.setHeaderHeight (24);
        table.setRowHeight (24);

        auto& header = table.getHeader();
        header.addColumn ("In Type",    1, 70);
        header.addColumn ("In Ch",      2, 45);
        header.addColumn ("In #",       3, 45);
        header.addColumn ("In Val",     4, 45);
        header.addColumn ("Out Type",   5, 70);
        header.addColumn ("Out Ch",     6, 45);
        header.addColumn ("Out #",      7, 45);
        header.addColumn ("Out Val",    8, 45);
        header.addColumn ("Description",9, 140);
        addAndMakeVisible (table);

        addButton.setButtonText ("Add Rule");
        addButton.onClick = [this]
        {
            rules.add (MidiRule());
            table.updateContent();
            table.selectRow (rules.size() - 1);
        };
        addAndMakeVisible (addButton);

        removeButton.setButtonText ("Remove");
        removeButton.onClick = [this]
        {
            int sel = table.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, rules.size()))
            {
                rules.remove (sel);
                table.updateContent();
            }
        };
        addAndMakeVisible (removeButton);

        applyButton.setButtonText ("Apply");
        applyButton.onClick = [this]
        {
            if (! applied && onClose) { onClose(); applied = true; }
        };
        addAndMakeVisible (applyButton);

        setSize (620, 400);
    }

    ~MidiRulesPanel() override
    {
        if (! applied && onClose) onClose();
    }

    std::function<void()> onClose;
    juce::Array<MidiRule> getRules() const { return rules; }

    int getNumRows() override { return rules.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int, int,
                             bool selected) override
    {
        g.fillAll (selected ? juce::Colour (0xff334455) : juce::Colour (0xff222222));
    }

    void paintCell (juce::Graphics& g, int row, int col,
                    int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, rules.size())) return;
        const auto& r = rules.getReference (row);

        juce::String text;
        switch (col)
        {
            case 1: text = inputTypeStr (r.inType); break;
            case 2: text = r.inChannel == 0 ? "Any" : juce::String (r.inChannel); break;
            case 3: text = juce::String (r.inNumber); break;
            case 4: text = r.inValue < 0 ? "Any" : juce::String (r.inValue); break;
            case 5: text = outputTypeStr (r.outType); break;
            case 6: text = juce::String (r.outChannel); break;
            case 7: text = juce::String (r.outNumber); break;
            case 8: text = r.outValue < 0 ? "Pass" : juce::String (r.outValue); break;
            case 9: text = r.description; break;
        }

        g.setColour (juce::Colour (0xffcccccc));
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    void cellClicked (int row, int col, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow (row, rules.size())) return;
        auto& r = rules.getReference (row);

        if (col == 1)
        {
            juce::PopupMenu m;
            m.addItem (1, "CC");  m.addItem (2, "PC");
            m.addItem (3, "NoteOn"); m.addItem (4, "NoteOff");
            m.showMenuAsync ({}, [this, row] (int res) {
                if (res > 0) { rules.getReference (row).inType = static_cast<MidiRule::InputType> (res - 1); table.repaint(); }
            });
        }
        else if (col == 5)
        {
            juce::PopupMenu m;
            m.addItem (1, "CC");  m.addItem (2, "PC");
            m.addItem (3, "NoteOn"); m.addItem (4, "NoteOff");
            m.showMenuAsync ({}, [this, row] (int res) {
                if (res > 0) { rules.getReference (row).outType = static_cast<MidiRule::OutputType> (res - 1); table.repaint(); }
            });
        }
        else if (col == 9)
        {
            auto* aw = new juce::AlertWindow ("Description", "Enter description:", juce::AlertWindow::NoIcon);
            aw->addTextEditor ("desc", r.description);
            aw->addButton ("OK", 1);
            aw->addButton ("Cancel", 0);
            aw->enterModalState (true, juce::ModalCallbackFunction::create (
                [this, row, aw] (int res) {
                    if (res == 1) { rules.getReference (row).description = aw->getTextEditorContents ("desc").trim(); table.repaint(); }
                    delete aw;
                }), true);
        }
        else
        {
            int* target = nullptr;
            juce::String title;
            switch (col)
            {
                case 2: target = &r.inChannel;  title = "Input Channel (0=Any)"; break;
                case 3: target = &r.inNumber;   title = "Input Number"; break;
                case 4: target = &r.inValue;    title = "Input Value (-1=Any)"; break;
                case 6: target = &r.outChannel;  title = "Output Channel"; break;
                case 7: target = &r.outNumber;   title = "Output Number"; break;
                case 8: target = &r.outValue;    title = "Output Value (-1=Pass)"; break;
            }
            if (target != nullptr)
            {
                auto* aw = new juce::AlertWindow (title, "Enter value:", juce::AlertWindow::NoIcon);
                aw->addTextEditor ("val", juce::String (*target));
                aw->addButton ("OK", 1);
                aw->addButton ("Cancel", 0);
                aw->enterModalState (true, juce::ModalCallbackFunction::create (
                    [this, row, col, aw] (int res) {
                        if (res == 1)
                        {
                            int val = aw->getTextEditorContents ("val").trim().getIntValue();
                            auto& rule = rules.getReference (row);
                            switch (col)
                            {
                                case 2: rule.inChannel  = val; break;
                                case 3: rule.inNumber   = val; break;
                                case 4: rule.inValue    = val; break;
                                case 6: rule.outChannel  = val; break;
                                case 7: rule.outNumber   = val; break;
                                case 8: rule.outValue    = val; break;
                            }
                            table.repaint();
                        }
                        delete aw;
                    }), true);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto buttons = area.removeFromBottom (30).reduced (4, 2);
        addButton.setBounds (buttons.removeFromLeft (100));
        buttons.removeFromLeft (4);
        removeButton.setBounds (buttons.removeFromLeft (100));
        applyButton.setBounds (buttons.removeFromRight (80));
        table.setBounds (area.reduced (2));
    }

private:
    juce::Array<MidiRule> rules;
    juce::TableListBox table;
    juce::TextButton addButton, removeButton, applyButton;
    bool applied = false;

    static juce::String inputTypeStr (MidiRule::InputType t)
    {
        switch (t) { case MidiRule::InputType::CC: return "CC"; case MidiRule::InputType::PC: return "PC";
                     case MidiRule::InputType::NoteOn: return "NoteOn"; case MidiRule::InputType::NoteOff: return "NoteOff"; }
        return "CC";
    }
    static juce::String outputTypeStr (MidiRule::OutputType t)
    {
        switch (t) { case MidiRule::OutputType::CC: return "CC"; case MidiRule::OutputType::PC: return "PC";
                     case MidiRule::OutputType::NoteOn: return "NoteOn"; case MidiRule::OutputType::NoteOff: return "NoteOff"; }
        return "NoteOn";
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRulesPanel)
};
