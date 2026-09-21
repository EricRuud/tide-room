#include "RoomProcessor.h"
#include "RoomEditor.h"
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

class TideRoomApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "Tide Room";}
    const juce::String getApplicationVersion() override {return JucePlugin_VersionString;}
    bool moreThanOneInstanceAllowed() override {return false;}
    void anotherInstanceStarted(const juce::String&) override {if(window)window->toFront(true);}
    void initialise(const juce::String& command) override {
        juce::PropertiesFile::Options options;options.applicationName="Tide Room";options.filenameSuffix=".settings";options.osxLibrarySubFolder="Application Support";properties.setStorageParameters(options);
        juce::AudioDeviceManager::AudioDeviceSetup preferred;preferred.sampleRate=48000;preferred.bufferSize=1024;
        window=std::make_unique<juce::StandaloneFilterWindow>("Tide Room",juce::Colour(0xff0b1016),properties.getUserSettings(),false,juce::String{},&preferred);window->setVisible(true);
        const auto args=juce::StringArray::fromTokens(command,true);
        if(args.contains("--worn-tape"))if(auto* p=dynamic_cast<RoomProcessor*>(window->pluginHolder->processor.get())){
            p->selectWornPreset(0);p->request821Audition();
            if(auto* editor=dynamic_cast<RoomEditor*>(p->getActiveEditor()))editor->openTapeControls();
        }
        if((args.contains("--821")||args.contains("--90030")))if(auto* p=dynamic_cast<RoomProcessor*>(window->pluginHolder->processor.get())){
            p->set("tapeModel",3);p->set("tapeOn",1);p->set("tapeMix",1);p->set("eightDrive",0);p->set("eightTrim",0);p->set("eightQuality",0);if(args.contains("--90030"))p->set("eightCalibration",1);p->request821Audition();
            if(args.contains("--821-motion"))p->set("eightMotionOn",1);
            if(auto* editor=dynamic_cast<RoomEditor*>(p->getActiveEditor()))editor->openTapeControls();
        }
        if(args.size()>=2&&args[0]=="--capture-motion")
            if(auto* p=dynamic_cast<RoomProcessor*>(window->pluginHolder->processor.get())){
                p->requestDiagnosticCapture(juce::File(args[1].unquoted()));
                if(args.contains("--studio-tape")){p->set("tapeModel",2);p->set("tapeOn",1);p->set("tapeMix",1);}
                if(args.contains("--enable-motion")){p->set("lfo0_on",1);p->set("lfo1_on",1);}
                if(args.contains("--disable-motion"))for(int i=0;i<6;++i)p->set(RoomProcessor::lfoId(i,"on"),0);
            }
    }
    void systemRequestedQuit() override {if(window)window->pluginHolder->savePluginState();if(juce::ModalComponentManager::getInstance()->cancelAllModalComponents())juce::Timer::callAfterDelay(100,[]{if(auto* a=juce::JUCEApplication::getInstance())a->systemRequestedQuit();});else quit();}
    void shutdown() override {window.reset();properties.saveIfNeeded();}
private:
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};
START_JUCE_APPLICATION(TideRoomApplication)
