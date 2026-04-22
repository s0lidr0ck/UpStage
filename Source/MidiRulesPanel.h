#pragma once
#include <JuceHeader.h>
#include "ProjectState.h"

class MidiRulesPanel : public juce::Component,
                       private juce::Timer
{
public:
    MidiRulesPanel (const juce::Array<MidiRule>& existingRules)
        : rules (existingRules)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&ruleContainer, false);
        viewport.setScrollBarsShown (true, false);

        addBtn.setButtonText ("+ Add Rule");
        addBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a4a2a));
        addBtn.onClick = [this] { startLearning(); };
        addAndMakeVisible (addBtn);

        removeBtn.setButtonText ("Remove");
        removeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4a2a2a));
        removeBtn.onClick = [this] { removeSelected(); };
        addAndMakeVisible (removeBtn);

        applyBtn.setButtonText ("Apply");
        applyBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a4a));
        applyBtn.onClick = [this] { if (!applied && onClose) { onClose(); applied = true; } };
        addAndMakeVisible (applyBtn);

        saveBtn.setButtonText ("Save Map");
        saveBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a3a));
        saveBtn.onClick = [this] { savePreset(); };
        addAndMakeVisible (saveBtn);

        loadBtn.setButtonText ("Load Map");
        loadBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a3a));
        loadBtn.onClick = [this] { loadPreset(); };
        addAndMakeVisible (loadBtn);

        learnLabel.setText ("", juce::dontSendNotification);
        learnLabel.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
        learnLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffcc44));
        learnLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (learnLabel);

        rebuildCards();
        setSize (520, 450);
    }

    ~MidiRulesPanel() override
    {
        stopTimer();
        if (!applied && onClose) onClose();
    }

    std::function<void()> onClose;
    juce::Array<MidiRule> getRules() const { return rules; }

    void incomingMidiMessage (const juce::MidiMessage& msg)
    {
        if (!learning) return;
        if (msg.isMidiClock() || msg.isActiveSense()) return;

        MidiRule rule;

        if (msg.isController())
        {
            rule.inType = MidiRule::InputType::CC;
            rule.inNumber = msg.getControllerNumber();
            rule.inChannel = msg.getChannel();
            rule.inValue = -1;
            rule.outType = MidiRule::OutputType::CC;
            rule.outNumber = msg.getControllerNumber();
            rule.outChannel = msg.getChannel();
            rule.outValue = -1;
        }
        else if (msg.isProgramChange())
        {
            rule.inType = MidiRule::InputType::PC;
            rule.inNumber = msg.getProgramChangeNumber();
            rule.inChannel = msg.getChannel();
            rule.outType = MidiRule::OutputType::PC;
            rule.outNumber = msg.getProgramChangeNumber();
            rule.outChannel = msg.getChannel();
        }
        else if (msg.isNoteOn())
        {
            rule.inType = MidiRule::InputType::NoteOn;
            rule.inNumber = msg.getNoteNumber();
            rule.inChannel = msg.getChannel();
            rule.inValue = -1;
            rule.outType = MidiRule::OutputType::CC;
            rule.outNumber = 0;
            rule.outChannel = msg.getChannel();
            rule.outValue = -1;
        }
        else if (msg.isNoteOff())
        {
            rule.inType = MidiRule::InputType::NoteOff;
            rule.inNumber = msg.getNoteNumber();
            rule.inChannel = msg.getChannel();
            rule.outType = MidiRule::OutputType::NoteOff;
            rule.outNumber = msg.getNoteNumber();
            rule.outChannel = msg.getChannel();
        }
        else return;

        rule.description = describeInput (rule);
        rules.add (rule);
        learning = false;
        stopTimer();
        learnLabel.setText ("", juce::dontSendNotification);
        selectedRule = rules.size() - 1;
        rebuildCards();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto top = area.removeFromTop (28);
        addBtn.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (4);
        removeBtn.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (4);
        saveBtn.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (4);
        loadBtn.setBounds (top.removeFromLeft (80));
        applyBtn.setBounds (top.removeFromRight (70));
        learnLabel.setBounds (top.reduced (4, 0));

        area.removeFromTop (6);
        viewport.setBounds (area);
        layoutCards();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a1a2a));
    }

private:
    juce::Array<MidiRule> rules;
    int selectedRule = -1;
    bool learning = false;
    bool applied = false;

    juce::Viewport viewport;
    juce::Component ruleContainer;

    juce::TextButton addBtn, removeBtn, applyBtn, saveBtn, loadBtn;
    juce::Label learnLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    struct RuleCard : public juce::Component
    {
        MidiRule* rule = nullptr;
        int index = -1;
        bool selected = false;
        std::function<void(int)> onSelect;
        std::function<void()> onChanged;

        juce::ComboBox inTypeBox, outTypeBox, modeBox, curveBox;
        juce::Label inChLabel, inNumLabel, outChLabel, outNumLabel;
        juce::Label exprMinLabel, exprMaxLabel;
        juce::TextEditor descEditor;

        RuleCard()
        {
            auto setupCombo = [this](juce::ComboBox& cb) {
                cb.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a3a));
                cb.setColour (juce::ComboBox::textColourId, juce::Colour (0xffccccdd));
                addAndMakeVisible (cb);
            };

            inTypeBox.addItem ("CC", 1); inTypeBox.addItem ("PC", 2);
            inTypeBox.addItem ("NoteOn", 3); inTypeBox.addItem ("NoteOff", 4);
            setupCombo (inTypeBox);
            inTypeBox.onChange = [this] { if (rule) { rule->inType = static_cast<MidiRule::InputType>(inTypeBox.getSelectedId() - 1); notify(); } };

            outTypeBox.addItem ("CC", 1); outTypeBox.addItem ("PC", 2);
            outTypeBox.addItem ("NoteOn", 3); outTypeBox.addItem ("NoteOff", 4);
            setupCombo (outTypeBox);
            outTypeBox.onChange = [this] { if (rule) { rule->outType = static_cast<MidiRule::OutputType>(outTypeBox.getSelectedId() - 1); notify(); } };

            modeBox.addItem ("Normal", 1); modeBox.addItem ("Toggle", 2); modeBox.addItem ("Expression", 3);
            setupCombo (modeBox);
            modeBox.onChange = [this] { if (rule) { rule->mode = static_cast<MidiRule::Mode>(modeBox.getSelectedId() - 1); notify(); resized(); } };

            curveBox.addItem ("Linear", 1); curveBox.addItem ("Log", 2); curveBox.addItem ("Exp", 3);
            setupCombo (curveBox);
            curveBox.onChange = [this] { if (rule) { rule->exprCurve = static_cast<MidiRule::Curve>(curveBox.getSelectedId() - 1); notify(); } };

            auto setupLabel = [this](juce::Label& lb) {
                lb.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
                lb.setColour (juce::Label::textColourId, juce::Colour (0xffaaaacc));
                lb.setColour (juce::Label::backgroundColourId, juce::Colour (0xff222233));
                lb.setColour (juce::Label::outlineColourId, juce::Colour (0xff333355));
                lb.setEditable (true);
                addAndMakeVisible (lb);
            };

            setupLabel (inChLabel);
            setupLabel (inNumLabel);
            setupLabel (outChLabel);
            setupLabel (outNumLabel);
            setupLabel (exprMinLabel);
            setupLabel (exprMaxLabel);

            inChLabel.onTextChange  = [this] { if (rule) { rule->inChannel  = inChLabel.getText().getIntValue(); notify(); } };
            inNumLabel.onTextChange = [this] { if (rule) { rule->inNumber   = inNumLabel.getText().getIntValue(); notify(); } };
            outChLabel.onTextChange = [this] { if (rule) { rule->outChannel = outChLabel.getText().getIntValue(); notify(); } };
            outNumLabel.onTextChange= [this] { if (rule) { rule->outNumber  = outNumLabel.getText().getIntValue(); notify(); } };
            exprMinLabel.onTextChange = [this] { if (rule) { rule->exprMin = exprMinLabel.getText().getIntValue(); notify(); } };
            exprMaxLabel.onTextChange = [this] { if (rule) { rule->exprMax = exprMaxLabel.getText().getIntValue(); notify(); } };

            descEditor.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            descEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff222233));
            descEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffaaaacc));
            descEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333355));
            descEditor.onFocusLost = [this] { if (rule) { rule->description = descEditor.getText().trim(); notify(); } };
            addAndMakeVisible (descEditor);
        }

        void setRule (MidiRule* r, int idx)
        {
            rule = r;
            index = idx;
            if (!rule) return;

            inTypeBox.setSelectedId ((int)rule->inType + 1, juce::dontSendNotification);
            outTypeBox.setSelectedId ((int)rule->outType + 1, juce::dontSendNotification);
            modeBox.setSelectedId ((int)rule->mode + 1, juce::dontSendNotification);
            curveBox.setSelectedId ((int)rule->exprCurve + 1, juce::dontSendNotification);

            inChLabel.setText (juce::String (rule->inChannel), juce::dontSendNotification);
            inNumLabel.setText (juce::String (rule->inNumber), juce::dontSendNotification);
            outChLabel.setText (juce::String (rule->outChannel), juce::dontSendNotification);
            outNumLabel.setText (juce::String (rule->outNumber), juce::dontSendNotification);
            exprMinLabel.setText (juce::String (rule->exprMin), juce::dontSendNotification);
            exprMaxLabel.setText (juce::String (rule->exprMax), juce::dontSendNotification);
            descEditor.setText (rule->description, juce::dontSendNotification);
        }

        void notify() { if (onChanged) onChanged(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onSelect) onSelect (index);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (selected ? juce::Colour (0xff2a3a4a) : juce::Colour (0xff1e1e2e));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (selected ? juce::Colour (0xff5577aa) : juce::Colour (0xff333355));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

            auto f = juce::Font (juce::FontOptions().withHeight (10.0f));
            g.setFont (f);
            g.setColour (juce::Colour (0xff666688));

            int y1 = 4, y2 = 38;
            g.drawText ("In Type", 6, y1, 60, 12, juce::Justification::centredLeft);
            g.drawText ("Ch",     72, y1, 30, 12, juce::Justification::centredLeft);
            g.drawText ("#",     108, y1, 30, 12, juce::Justification::centredLeft);
            g.drawText ("Mode", 150, y1, 50, 12, juce::Justification::centredLeft);
            g.drawText ("Out Type", 6, y2, 60, 12, juce::Justification::centredLeft);
            g.drawText ("Ch",     72, y2, 30, 12, juce::Justification::centredLeft);
            g.drawText ("#",     108, y2, 30, 12, juce::Justification::centredLeft);

            bool isExpr = (rule && rule->mode == MidiRule::Mode::Expression);
            if (isExpr)
            {
                g.drawText ("Min",  150, y2, 30, 12, juce::Justification::centredLeft);
                g.drawText ("Max",  200, y2, 30, 12, juce::Justification::centredLeft);
                g.drawText ("Curve",250, y2, 40, 12, juce::Justification::centredLeft);
            }
        }

        void resized() override
        {
            int y1 = 16, y2 = 50, h = 20;
            inTypeBox.setBounds (6, y1, 62, h);
            inChLabel.setBounds (72, y1, 32, h);
            inNumLabel.setBounds (108, y1, 36, h);
            modeBox.setBounds (150, y1, 90, h);
            descEditor.setBounds (248, y1, getWidth() - 256, h);

            outTypeBox.setBounds (6, y2, 62, h);
            outChLabel.setBounds (72, y2, 32, h);
            outNumLabel.setBounds (108, y2, 36, h);

            bool isExpr = (rule && rule->mode == MidiRule::Mode::Expression);
            exprMinLabel.setVisible (isExpr);
            exprMaxLabel.setVisible (isExpr);
            curveBox.setVisible (isExpr);

            if (isExpr)
            {
                exprMinLabel.setBounds (150, y2, 40, h);
                exprMaxLabel.setBounds (198, y2, 40, h);
                curveBox.setBounds (246, y2, 80, h);
            }
        }

        static int preferredHeight() { return 78; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RuleCard)
    };

    juce::OwnedArray<RuleCard> cards;

    void startLearning()
    {
        learning = true;
        learnLabel.setText ("Send a MIDI message to capture...", juce::dontSendNotification);
        startTimer (5000);
    }

    void timerCallback() override
    {
        learning = false;
        stopTimer();
        learnLabel.setText ("", juce::dontSendNotification);
    }

    void removeSelected()
    {
        if (juce::isPositiveAndBelow (selectedRule, rules.size()))
        {
            rules.remove (selectedRule);
            selectedRule = -1;
            rebuildCards();
        }
    }

    void rebuildCards()
    {
        cards.clear();
        for (int i = 0; i < rules.size(); ++i)
        {
            auto* card = cards.add (new RuleCard());
            card->setRule (&rules.getReference (i), i);
            card->selected = (i == selectedRule);
            card->onSelect = [this](int idx) { selectedRule = idx; updateSelection(); };
            card->onChanged = [this] { repaint(); };
            ruleContainer.addAndMakeVisible (card);
        }
        layoutCards();
    }

    void updateSelection()
    {
        for (int i = 0; i < cards.size(); ++i)
        {
            cards[i]->selected = (i == selectedRule);
            cards[i]->repaint();
        }
    }

    void layoutCards()
    {
        int w = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 12 : 0);
        if (w < 100) w = getWidth() - 24;
        int y = 0;
        for (auto* card : cards)
        {
            card->setBounds (0, y, w, RuleCard::preferredHeight());
            y += RuleCard::preferredHeight() + 4;
        }
        ruleContainer.setSize (w, juce::jmax (y, 1));
    }

    static juce::String describeInput (const MidiRule& r)
    {
        juce::String s;
        switch (r.inType)
        {
            case MidiRule::InputType::CC:      s = "CC " + juce::String (r.inNumber); break;
            case MidiRule::InputType::PC:      s = "PC " + juce::String (r.inNumber); break;
            case MidiRule::InputType::NoteOn:  s = "Note " + juce::MidiMessage::getMidiNoteName (r.inNumber, true, true, 4); break;
            case MidiRule::InputType::NoteOff: s = "NoteOff " + juce::MidiMessage::getMidiNoteName (r.inNumber, true, true, 4); break;
        }
        s += " ch" + juce::String (r.inChannel);
        return s;
    }

    static juce::File getPresetsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("UpStage").getChildFile ("MidiMaps");
    }

    void savePreset()
    {
        auto folder = getPresetsFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> (
            "Save MIDI Map", folder, "*.midimap");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;

                if (! file.hasFileExtension (".midimap"))
                    file = file.withFileExtension ("midimap");

                auto root = std::make_unique<juce::XmlElement> ("MidiMap");
                for (const auto& r : rules)
                    root->addChildElement (ProjectState::ruleToXml (r));

                root->writeTo (file);
            });
    }

    void loadPreset()
    {
        auto folder = getPresetsFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> (
            "Load MIDI Map", folder, "*.midimap");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File() || ! file.existsAsFile()) return;

                auto xml = juce::XmlDocument::parse (file);
                if (xml == nullptr || ! xml->hasTagName ("MidiMap")) return;

                rules.clear();
                for (auto* el : xml->getChildIterator())
                    rules.add (ProjectState::xmlToRule (el));

                selectedRule = -1;
                rebuildCards();
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRulesPanel)
};
