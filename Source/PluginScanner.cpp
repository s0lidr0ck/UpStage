/*
  PluginScanner.cpp

  Separate process for scanning plugins safely.
  This runs as a child process so if a plugin crashes, it doesn't take down the main app.
*/

#include <JuceHeader.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 1 && juce::String (argv[1]) == "--child")
    {
        // Run as child process scanner
        juce::PluginListComponent::createChildProcessPluginScanner();
        return 0;
    }

    return 1;
}
