#include "Processor.h"
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

class TideFXApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "Tide FX";}
    const juce::String getApplicationVersion() override {return "0.3.0";}
    bool moreThanOneInstanceAllowed() override {return true;}
    void anotherInstanceStarted(const juce::String&) override {}
    void initialise(const juce::String&) override {
        juce::PropertiesFile::Options options;options.applicationName="Tide FX";options.filenameSuffix=".settings";options.osxLibrarySubFolder="Application Support";
        properties.setStorageParameters(options);
        auto* settings=properties.getUserSettings();
        // Seed the first FX session from the earlier Tide app without changing it.
        if(!settings->containsKey("filterState")) {
            const auto old=juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Application Support/Tide.settings");
            if(auto xml=juce::XmlDocument::parse(old)) for(auto* child:xml->getChildIterator()) {
                const auto key=child->getStringAttribute("name");
                if(key=="filterState"||key=="audioSetup")settings->setValue(key,child->getStringAttribute("val"));
            }
        }
        window=std::make_unique<juce::StandaloneFilterWindow>("Tide FX",juce::Colour(0xffe8e5db),settings,false);
        window->setVisible(true);
    }
    void systemRequestedQuit() override {
        if(window)window->pluginHolder->savePluginState();
        if(juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100,[]{if(auto* a=juce::JUCEApplication::getInstance())a->systemRequestedQuit();});
        } else quit();
    }
    void shutdown() override {window.reset();properties.saveIfNeeded();}
private:
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};
JUCE_CREATE_APPLICATION_DEFINE(TideFXApplication)
int main(int argc,char* argv[]) {
    // Scanner and diagnostics initialise JUCE as a console host, without
    // registering a second desktop application or opening an audio device.
    if(argc==4) {
        const juce::String mode(argv[1]);
        if(mode=="--scan-effect") {
            juce::ScopedJuceInitialiser_GUI initialise;
            const juce::File plugin(argv[2]),output(argv[3]);
            return EffectHost::scanToFile(plugin,output)?0:1;
        }
    }
    juce::JUCEApplicationBase::createInstance=&juce_CreateApplication;
    return juce::JUCEApplicationBase::main(argc,(const char**)argv);
}
