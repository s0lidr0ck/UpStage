#include <JuceHeader.h>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());

    juce::KnownPluginList list;

    juce::FileSearchPath paths;
    paths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));

    std::cout << "Creating scanner..." << std::endl;
    juce::PluginDirectoryScanner scanner (list, *fm.getFormat(0), paths, true, juce::File());

    std::cout << "Scanning..." << std::endl;
    juce::String name;
    int count = 0;

    while (true)
    {
        auto next = scanner.getNextPluginFileThatWillBeScanned();
        if (next.isEmpty())
            break;

        std::cout << "Will scan: " << next.toStdString() << std::endl;

        if (scanner.scanNextFile (false, name))
        {
            std::cout << "  SUCCESS: " << name.toStdString() << std::endl;
            count++;
        }
        else
        {
            std::cout << "  FAILED or END" << std::endl;
            break;
        }
    }

    std::cout << "\nTotal plugins found: " << list.getNumTypes() << std::endl;
    std::cout << "Plugins scanned: " << count << std::endl;

    return 0;
}
