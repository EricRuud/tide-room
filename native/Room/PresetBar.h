#pragma once
#include "RoomPresets.h"
#include "GlassLook.h"

class PresetBar final : public juce::Component,private juce::Timer {
public:
    explicit PresetBar(RoomProcessor& p):presets(p){
        addAndMakeVisible(choice);choice.setName("Scene preset");choice.setTooltip("Recall a complete scene: sound, room, tides, tape and visuals.");
        addAndMakeVisible(save);save.setTooltip("Save this whole scene under a new name. Existing presets are kept.");
        choice.onChange=[this]{const int id=choice.getSelectedId();if(id>0&&id<=(int)items.size()){showResult(presets.load(items[(size_t)id-1]));refresh();}
            else if(id==10001)import();else if(id==10002){auto result=presets.directory().createDirectory();if(result.wasOk())presets.directory().revealToUser();else showResult(result);updateName();}};
        save.onClick=[this]{saveAs();};refresh();startTimerHz(5);
    }
    ~PresetBar()override{stopTimer();if(nameDialog)nameDialog->setLookAndFeel(nullptr);nameDialog.reset();chooser.reset();}
    void resized()override{choice.setBounds(0,19,std::max(70,getWidth()-90),28);save.setBounds(getWidth()-82,19,82,28);}
    void paint(juce::Graphics& g)override{g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(10.f));g.drawText("PRESET",0,0,getWidth(),16,juce::Justification::centredLeft);}
private:
    RoomPresets presets;
    juce::ComboBox choice;juce::TextButton save{"Save as..."};std::vector<RoomPresets::Entry> items;
    std::unique_ptr<juce::AlertWindow> nameDialog;
    std::unique_ptr<juce::FileChooser> chooser;
    void refresh(){items=presets.entries();choice.clear(juce::dontSendNotification);choice.addSectionHeading("Factory");
        for(size_t i=0;i<items.size();++i){if(i==1)choice.addSectionHeading("Saved scenes");choice.addItem(items[i].name,(int)i+1);}choice.addSeparator();choice.addItem("Import preset...",10001);choice.addItem("Open preset folder",10002);updateName();}
    void updateName(){const auto name=presets.currentName()+(presets.modified()?" *":"");if(choice.getText()!=name)choice.setText(name,juce::dontSendNotification);}
    void timerCallback()override{presets.persistMetadata();updateName();}
    void showResult(const juce::Result& result){if(result.failed())juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preset",result.getErrorMessage());}
    void saveAs(){
        if(nameDialog){nameDialog->toFront(true);return;}
        nameDialog=std::make_unique<juce::AlertWindow>("Save preset",juce::String{},juce::MessageBoxIconType::NoIcon,this);nameDialog->setLookAndFeel(&getLookAndFeel());
        const auto suggested=presets.currentName()=="Current scene"?"New scene":presets.currentName()+" copy";
        nameDialog->addTextEditor("name",suggested,"Name");nameDialog->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));nameDialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<PresetBar> safe(this);
        nameDialog->enterModalState(true,juce::ModalCallbackFunction::create([safe](int result){if(!safe)return;
            if(result==1)safe->showResult(safe->presets.saveAs(safe->nameDialog->getTextEditorContents("name")));
            safe->nameDialog->setLookAndFeel(nullptr);safe->nameDialog.reset();safe->refresh();}),false);
    }
    void import(){
        chooser=std::make_unique<juce::FileChooser>("Import Tide Room preset",juce::File{},"*.tideroom");const juce::Component::SafePointer<PresetBar> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& dialog){if(!safe)return;const auto file=dialog.getResult();if(file.existsAsFile())safe->showResult(safe->presets.importFile(file));safe->refresh();});
    }
};
