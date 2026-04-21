#include <JuceHeader.h>
#include "MainComponent.h"
#include "MixerLookAndFeel.h"

//==============================================================================
class UpStageApplication : public juce::JUCEApplication
{
public:
    UpStageApplication() {}

    const juce::String getApplicationName() override    { return "UpStage"; }
    const juce::String getApplicationVersion() override { return "0.5.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // Use JUCE LookAndFeel_V4 with dark theme
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // TODO: prompt save-unsaved-project dialog before quitting
        quit();
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Colours::darkgrey,
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            try {
                auto* mainComp = new MainComponent();
                setContentOwned (mainComp, true);
                setResizable (true, true);

                // Position at left edge of screen, full height
                auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                if (display != nullptr)
                {
                    auto area = display->userArea; // Excludes taskbar
                    setBounds (area.getX(), area.getY(), 640, area.getHeight());
                }
                else
                {
                    centreWithSize (640, 1080);
                }

                setVisible (true);
            }
            catch (const std::exception& e) {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Fatal Error",
                    juce::String("Failed to create main window: ") + e.what(),
                    "OK");
            }
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    MixerLookAndFeel lookAndFeel;
};

START_JUCE_APPLICATION (UpStageApplication)
