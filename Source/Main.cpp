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

    // The out-of-process plugin scanner launches this same exe with
    // --scan-file; those children must be allowed to coexist with the app.
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains ("--scan-file");
    }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--scan-file"))
        {
            runHeadlessScan (commandLine);
            return;
        }

        // Use JUCE LookAndFeel_V4 with dark theme
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    /** Child-process mode: scan ONE plugin file and write the resulting
        descriptions as XML. If the plugin crashes, only this process dies -
        the main app sees a dead child and blacklists the file. */
    void runHeadlessScan (const juce::String& commandLine)
    {
        auto tokens = juce::StringArray::fromTokens (commandLine, true);
        tokens.trim();
        juce::String pluginPath, outPath;
        for (int i = 0; i < tokens.size() - 1; ++i)
        {
            if (tokens[i] == "--scan-file") pluginPath = tokens[i + 1].unquoted();
            if (tokens[i] == "--out")       outPath    = tokens[i + 1].unquoted();
        }

        int returnValue = 1;
        if (pluginPath.isNotEmpty() && outPath.isNotEmpty())
        {
            juce::VST3PluginFormat format;
            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile (found, pluginPath);

            juce::XmlElement root ("SCANRESULT");
            for (auto* d : found)
                root.addChildElement (d->createXml().release());

            if (root.writeTo (juce::File (outPath)))
                returnValue = 0;
        }

        setApplicationReturnValue (returnValue);
        quit();
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
        {
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                if (mc->isProjectDirty())
                {
                    auto options = juce::MessageBoxOptions()
                        .withTitle ("Unsaved Changes")
                        .withMessage ("You have unsaved changes. Save before quitting?")
                        .withButton ("Save")
                        .withButton ("Don't Save")
                        .withButton ("Cancel");

                    juce::AlertWindow::showAsync (options, [this] (int result)
                    {
                        if (result == 1)
                        {
                            if (auto* mc2 = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                                mc2->saveProject();
                            quit();
                        }
                        else if (result == 2)
                        {
                            quit();
                        }
                    });
                    return;
                }
            }
        }
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
            setUsingNativeTitleBar (false);

            try {
                auto* mainComp = new MainComponent();
                setContentOwned (mainComp, true);
                setResizable (true, true);

                // Default position: left edge, full height — overridden by saved settings
                auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                if (display != nullptr)
                {
                    auto area = display->userArea;
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
