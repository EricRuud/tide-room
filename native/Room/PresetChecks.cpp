#include "RoomEditor.h"
#include "TidesPanel.h"
#include "WornTapePanel.h"
#include "AdvancedTidesPanel.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void close(float a,float b,const char* message){check(std::abs(a-b)<.00002f,message);}
void restore(RoomProcessor& p,const juce::ValueTree& tree){juce::MemoryBlock data;juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),data);p.setStateInformation(data.getData(),(int)data.getSize());}
void image(const juce::Image& value,const juce::File& file){juce::PNGImageFormat png;auto stream=file.createOutputStream();check(stream&&png.writeImageToStream(value,*stream),"Snapshot could not be written");}
template<class T>T* named(juce::Component& parent,const juce::String& name){for(auto* child:parent.getChildren()){if(auto* c=dynamic_cast<T*>(child);c&&c->getName()==name)return c;if(auto* result=named<T>(*child,name))return result;}return nullptr;}
juce::Button* button(juce::Component& parent,const juce::String& text){for(auto* child:parent.getChildren()){if(auto* b=dynamic_cast<juce::Button*>(child);b&&b->getButtonText()==text)return b;if(auto* result=button(*child,text))return result;}return nullptr;}
void sameParameters(RoomProcessor& p,const juce::ValueTree& saved){for(const auto& value:saved)if(value.hasType("PARAM")){const auto id=value.getProperty("id").toString();close(p.get(id),(float)value.getProperty("value"),("Recall changed "+id).toRawUTF8());}}
void pump(RoomProcessor& p,int samples=512){juce::AudioBuffer<float> audio(2,samples);juce::MidiBuffer midi;p.processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=0;n<samples;++n)check(std::isfinite(audio.getSample(c,n))&&std::abs(audio.getSample(c,n))<=1,"Invalid audio during preset change");}

void library(const juce::File& out){
    auto p=std::make_unique<RoomProcessor>(false);p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);
    const auto folder=out.getChildFile("presets");RoomPresets store(*p,folder);
    check(store.entries().size()==1,"Factory library is missing Slow tides");check(store.load(store.entries()[0]).wasOk(),"Factory load failed");
    const auto factory=RoomPresets::slowTides();check(factory.getNumChildren()==167,"Live scene capture lost parameters");sameParameters(*p,factory);
    for(const auto& c:tide::tides::controls)close(p->get(c.id),c.initial,"Factory macro is not neutral");
    check(store.currentName()=="Slow tides"&&!store.modified()&&!p->isPlaying(),"Factory metadata or transport failed");
    close(p->get("visualPrism"),1,"Current prism setting lost");close(p->get("wornDrive"),7,"Current Worn tape setting lost");
    p->useSimpleInterface();sameParameters(*p,factory);check(!store.modified(),"Simple interface changed current scene");
    p->set("tempo",116);p->set("p2_brightness",.34f);p->set("visualMesh",.63f);p->set("wornAge",.51f);p->set("tideSpeed",1.45f);p->set("tideTravel",.42f);p->set("tideBounce",.2f);p->set("oceanOn",1);
    check(store.modified(),"Parameter edit did not mark preset modified");
    check(store.saveAs("My scene").wasOk()&&!store.modified(),"Save as failed");
    const auto saved=p->parameters.copyState();auto entries=store.entries();check(entries.size()==2&&entries[1].name=="My scene","Saved preset missing from library");
    check(store.saveAs("My scene").wasOk()&&store.entries().size()==3,"Save overwrote an existing preset");check(store.currentName()!="My scene","Duplicate preset names are ambiguous");
    check(store.load(entries[0]).wasOk(),"Factory recall failed");sameParameters(*p,factory);
    p->play();check(p->isPlaying(),"Test processor failed to play");pump(*p);
    check(store.load(entries[1]).wasOk()&&p->isPlaying(),"Recall interrupted playing transport");sameParameters(*p,saved);pump(*p);
    p->stop();check(store.load(entries[0]).wasOk()&&!p->isPlaying(),"Recall started a stopped scene");
    const auto before=p->parameters.copyState();
    for(int kind=0;kind<4;++kind){auto bad=factory.createCopy();
        if(kind==0)bad.getChild(0).setProperty("value","nan",nullptr);
        if(kind==1)bad.addChild(bad.getChild(0).createCopy(),-1,nullptr);
        if(kind==2)bad.getChildWithProperty("id","tempo").setProperty("value",999999,nullptr);
        if(kind==3)bad=juce::ValueTree("WrongRoot");
        const auto file=out.getChildFile("invalid-"+juce::String(kind)+".tideroom");file.replaceWithText(bad.toXmlString());
        check(store.importFile(file).failed(),"Invalid preset was accepted");sameParameters(*p,before);check(store.entries().size()==3,"Invalid import wrote a library file");}
    check(store.importFile(entries[1].file).wasOk()&&store.entries().size()==4,"Preset import failed");sameParameters(*p,saved);
    auto legacy=factory.createCopy();legacy.removeProperty("presetName",nullptr);legacy.removeProperty("presetId",nullptr);restore(*p,legacy);
    for(const auto& c:tide::tides::controls)close(p->get(c.id),c.initial,"Legacy scene inherited edited macro");
    {RoomPresets recognized(*p,out.getChildFile("recognition"));check(recognized.currentName()=="Slow tides"&&!recognized.modified(),"Existing live scene not recognized as Slow tides");}
    p->releaseResources();std::cout<<"PASS captured 167-parameter scene; complete recall, import validation, non-destructive Save as, metadata, transport and legacy macros\n";
    auto production=std::make_unique<RoomProcessor>();sameParameters(*production,factory);close(production->get("tapeModel"),4,"Production engine is not Worn tape");
    std::cout<<"PASS clean application startup uses Slow tides and Worn tape\n";
}
void panels(const juce::File& out){
    auto p=std::make_unique<RoomProcessor>(false);RoomPresets store(*p,out.getChildFile("ui-presets"));store.load(store.entries()[0]);p->useSimpleInterface();
    p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);p->play();for(int n=0;n<120;++n)pump(*p);
    RoomEditor editor(*p);editor.advanceOceanPreview(.7);
    auto* preset=named<juce::ComboBox>(editor,"Scene preset");check(preset&&preset->getText()=="Slow tides","Main preset selector missing");
    auto* tides=button(editor,"Advanced...");check(tides&&tides->isVisible(),"Advanced Tides button missing");
    const auto bounds=editor.roomView().bounds;auto* fullscreen=button(editor,"Fullscreen immersive");check(fullscreen&&bounds.contains(fullscreen->getBounds().toFloat()),"Fullscreen button is outside room");
    check(button(editor,"Visual effects...")==nullptr,"Removed effect menu remains visible");
    for(int i=1;i<=3;++i){auto* voice=named<juce::ComboBox>(editor,"Instrument "+juce::String(i));check(voice&&voice->getBottom()<bounds.getY(),"Tone controls are not above the room");}
    check(!named<juce::Slider>(editor,"Height"),"Room height control still appears in UI");
    auto* master=named<juce::Slider>(editor,"Master volume");check(master&&master->getSliderStyle()==juce::Slider::LinearVertical&&master->getY()>bounds.getBottom()&&master->getX()>1000,"Master is not the final post-processing fader");
    int oldVisible=0;for(auto* child:editor.getChildren())if(auto* b=dynamic_cast<juce::Button*>(child))if(b->isVisible()&&(b->getButtonText().contains("LFO")||b->getButtonText().contains("Oceans")))++oldVisible;
    check(oldVisible==0,"Legacy motion/ocean buttons remain visible");
    image(editor.createComponentSnapshot(editor.getLocalBounds()),out.getChildFile("room.png"));image(editor.tidesPanelSnapshot(),out.getChildFile("tides.png"));image(editor.tapePanelSnapshot(),out.getChildFile("worn-tape.png"));
    auto* masterControl=named<juce::Slider>(editor,"Master volume");masterControl->setValue(-9,juce::sendNotificationSync);close(p->get("output"),-9,"Master fader is not attached");
    constexpr const char* tapeNames[]={"Drive","Age","Transport","Contact loss","Dip depth","Hiss / grain","Tape output","Mix"};
    constexpr const char* tapeIds[]={"wornDrive","wornAge","wornMotion","wornDamage","wornDips","wornNoise","wornTrim","tapeMix"};
    for(int i=0;i<8;++i){auto* slider=named<juce::Slider>(editor,tapeNames[i]);check(slider,"Inline tape control missing");slider->setValue(slider->getMinimum()+(slider->getMaximum()-slider->getMinimum())*.43,juce::sendNotificationSync);close(p->get(tapeIds[i]),(float)slider->getValue(),"Inline tape control not attached");}
    const auto beforeAdvanced=p->parameters.copyState();editor.openAdvancedTides();bool found=false;
    for(int i=0;i<juce::TopLevelWindow::getNumTopLevelWindows();++i)if(auto* w=dynamic_cast<juce::DocumentWindow*>(juce::TopLevelWindow::getTopLevelWindow(i)))if(auto* advanced=dynamic_cast<AdvancedTidesPanel*>(w->getContentComponent())){
        found=true;auto* tabs=named<juce::TabbedComponent>(*advanced,"Tides sections");check(tabs&&tabs->getNumTabs()==4,"Full Tides sections missing");
        for(int tab=0;tab<4;++tab){tabs->setCurrentTabIndex(tab);image(advanced->createComponentSnapshot(advanced->getLocalBounds()),out.getChildFile("advanced-"+juce::String(tab)+".png"));}
        tabs->setCurrentTabIndex(1);
        for(int route=1;route<=6;++route){auto* destination=named<juce::ComboBox>(*tabs->getTabContentComponent(1),"LFO "+juce::String(route)+" destination");check(destination&&destination->getNumItems()==7,"Height LFO destination still exposed");for(int item=0;item<destination->getNumItems();++item)check(!destination->getItemText(item).containsIgnoreCase("height"),"Height destination remains");}
        check(named<juce::Slider>(*tabs->getTabContentComponent(1),"LFO 6 rate"),"Full LFO controls missing");
        tabs->setCurrentTabIndex(2);check(named<juce::Slider>(*tabs->getTabContentComponent(2),"Ocean patch 6 amount"),"Full ocean routing missing");
        tabs->setCurrentTabIndex(3);check(named<juce::Slider>(*tabs->getTabContentComponent(3),"Tower 3 Depth"),"Tower depth controls missing");w->setVisible(false);}
    check(found,"Advanced Tides window missing");sameParameters(*p,beforeAdvanced);
    TidesPanel panel(*p);constexpr const char* names[]={"Current speed","Travel","Inertia","Bounce","Gravity","Liquid response","Viscosity","Sound coupling"};
    constexpr const char* ids[]={"tideSpeed","tideTravel","tideInertia","tideBounce","oceanGravity","oceanRate","oceanDamping","tideSound"};
    for(int i=0;i<8;++i){auto* slider=named<juce::Slider>(panel,names[i]);check(slider,"Tides slider missing");const auto value=slider->getMinimum()+(slider->getMaximum()-slider->getMinimum())*.37;slider->setValue(value,juce::sendNotificationSync);close(p->get(ids[i]),(float)slider->getValue(),"Tides slider not attached");check(panel.getLocalBounds().contains(slider->getBounds()),"Tides control clipped");}
    check(!named<juce::Slider>(panel,"Buoyancy"),"Buoyancy control still appears");
    auto* sound=named<juce::ToggleButton>(panel,"Tides sound modulation");check(sound,"Sound switch missing");sound->setToggleState(true,juce::sendNotificationSync);close(p->get("oceanOn"),1,"Sound switch not attached");
    WornTapePanel tape(*p);int combos=0;for(auto* child:tape.getChildren())if(dynamic_cast<juce::ComboBox*>(child))++combos;check(combos==1&&named<juce::ComboBox>(tape,"Worn tape medium"),"Hidden tape engine selector is exposed");
    p->set(RoomProcessor::lfoId(0,"target"),3);{
        MotionPanel motion(*p);auto* destination=named<juce::ComboBox>(motion,"LFO 1 destination");check(destination&&destination->getSelectedId()==1,"Legacy height route did not display as inactive");close(p->get(RoomProcessor::lfoId(0,"target")),3,"Opening Motion overwrote a legacy route");destination->setSelectedId(3,juce::sendNotificationSync);close(p->get(RoomProcessor::lfoId(0,"target")),2,"Filtered destination menu changed the wrong axis");}
    p->releaseResources();std::cout<<"PASS preset header, hidden legacy GUI, Worn controls, all Tides attachments and layout snapshots\n";
}
void tapeSelection(const juce::File& out){
    auto p=std::make_unique<RoomProcessor>(false);restore(*p,RoomPresets::slowTides());p->useSimpleInterface();
    p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);
    RoomPresets store(*p,out.getChildFile("tape-presets"));RoomEditor editor(*p);
    auto* model=named<juce::ComboBox>(editor,"Tape model");check(model&&model->getNumItems()==2&&model->getSelectedId()==5,"Tape selector did not retain Worn default");
    check(model->getItemId(0)==5&&model->getItemId(1)==4,"Tape selector exposes legacy engines or uses wrong IDs");
    const auto worn=p->parameters.copyState();model->setSelectedId(4,juce::sendNotificationSync);close(p->get("tapeModel"),3,"821 selector selected the wrong DSP");
    for(const auto& value:worn)if(value.hasType("PARAM")&&value.getProperty("id")!="tapeModel")close(p->get(value.getProperty("id").toString()),(float)value.getProperty("value"),"Model selection reset another setting");
    check(!named<juce::ComboBox>(editor,"Worn tape medium")->isVisible(),"Worn controls remain visible in 821 mode");
    const char* names[]={"821 drive","821 wow","821 flutter","821 output","Mix"};const char* ids[]={"eightDrive","eightWow","eightFlutter","eightTrim","tapeMix"};
    const float values[]={7.5f,.6f,1.4f,-2.5f,.73f};
    for(int i=0;i<5;++i){auto* s=named<juce::Slider>(editor,names[i]);check(s&&s->isVisible()&&s->getParentComponent()->getLocalBounds().contains(s->getBounds()),"821 slider missing or clipped");s->setValue(values[i],juce::sendNotificationSync);close(p->get(ids[i]),values[i],"821 slider targets the wrong parameter");}
    auto* calibration=named<juce::ComboBox>(editor,"821 formula and speed");auto* quality=named<juce::ComboBox>(editor,"821 limiter quality");auto* motion=named<juce::ToggleButton>(editor,"821 transport motion");
    check(calibration&&quality&&motion&&calibration->isVisible()&&quality->isVisible()&&motion->isVisible(),"821 mode controls missing");
    calibration->setSelectedId(2,juce::sendNotificationSync);quality->setSelectedId(3,juce::sendNotificationSync);motion->setToggleState(true,juce::sendNotificationSync);
    close(p->get("eightCalibration"),1,"821 calibration attachment");close(p->get("eightQuality"),2,"821 quality attachment");close(p->get("eightMotionOn"),1,"821 motion attachment");
    check(store.saveAs("821 scene").wasOk(),"821 preset save failed");const auto eight=p->parameters.copyState();const auto entry=store.entries().back();
    p->play();pump(*p);model->setSelectedId(5,juce::sendNotificationSync);pump(*p);close(p->get("tapeModel"),4,"Worn selector selected wrong DSP");
    for(const char* id:{"wornDrive","wornAge","wornMotion","wornDamage","wornDips","wornNoise","wornTrim","wornMedium"})close(p->get(id),(float)worn.getChildWithProperty("id",id).getProperty("value"),"821 selection changed Worn settings");
    check(store.load(entry).wasOk()&&p->isPlaying(),"821 preset recall interrupted playback");sameParameters(*p,eight);pump(*p);check(model->getSelectedId()==4&&calibration->isVisible(),"821 preset did not update visible controls");
    juce::MemoryBlock saved;p->getStateInformation(saved);p->set("tapeModel",4);p->set("eightDrive",0);p->setStateInformation(saved.getData(),(int)saved.getSize());sameParameters(*p,eight);check(model->getSelectedId()==4,"Session restore forced Worn tape");
    editor.advanceOceanPreview(.7);image(editor.createComponentSnapshot(editor.getLocalBounds()),out.getChildFile("room-821.png"));image(editor.tapePanelSnapshot(),out.getChildFile("821-panel.png"));
    restore(*p,worn);sameParameters(*p,worn);check(model->getSelectedId()==5&&named<juce::ComboBox>(editor,"Worn tape medium")->isVisible(),"Worn preset recall failed");
    p->releaseResources();p->set("tapeModel",3);p->setRateAndBufferSizeDetails(44100,512);p->prepareToPlay(44100,512);
    PostProcessingStrip unsupported(*p);auto* status=named<juce::Label>(unsupported,"821 status");check(status&&status->isVisible()&&status->getText().contains("48 kHz"),"Unsupported 821 rate is not explained in the UI");
    image(unsupported.createComponentSnapshot(unsupported.getLocalBounds()),out.getChildFile("821-unsupported.png"));p->releaseResources();
    std::cout<<"PASS Worn/821 selection, independent settings, both calibration/quality controls, complete preset/session recall and visible 48 kHz requirement\n";
}
void physicalMacros(){
    using tide::room::PlanetMotion;const PlanetMotion::Positions initial{{{{-.8f,2,1}},{{.8f,2,1}},{{0,5,2}}}};auto crossed=initial;crossed[0][0]=.8f;crossed[1][0]=-.8f;
    PlanetMotion soft,hard;soft.prepare(48000);hard.prepare(48000);soft.process(initial,0,10,8);hard.process(initial,0,10,8);float softRebound=0,hardRebound=0;
    for(int n=0;n<100;++n){soft.process(crossed,100,10,8,4,0);hard.process(crossed,100,10,8,4,1);softRebound=std::max(softRebound,-soft.velocities()[0][0]);hardRebound=std::max(hardRebound,-hard.velocities()[0][0]);}
    check(hardRebound>softRebound+1,"Bounce has no physical effect");
    PlanetMotion light,heavy;light.prepare(48000);heavy.prepare(48000);light.process(initial,0,10,8);heavy.process(initial,0,10,8);auto moved=initial;moved[2][0]=1;
    light.process(moved,4800,10,8,4,.68f,.25f);heavy.process(moved,4800,10,8,4,.68f,3);check(light.positions()[2][0]>heavy.positions()[2][0]+.3f,"Inertia has no physical effect");
    auto p=std::make_unique<RoomProcessor>(false);restore(*p,RoomPresets::slowTides());p->useSimpleInterface();p->set("tapeOn",0);p->set("tideTravel",0);p->set("p0_z",1.1f);p->set("tideBuoyancy",.3f);
    p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);p->play();for(int i=0;i<200;++i)pump(*p);for(int voice=0;voice<3;++voice)close(p->movingPosition(voice).height,p->towerHeight(),"Tower is not on the golden-ratio plane");
    for(int voice=0;voice<3;++voice){p->set(RoomProcessor::partId(voice,"z"),.3f+(float)voice*.7f);p->set(RoomProcessor::lfoId(voice,"on"),1);p->set(RoomProcessor::lfoId(voice,"target"),(float)((voice+1)*3));}
    p->set("tideBuoyancy",-.5f);
    for(float roomHeight:{2.5f,4.7f,8.f}){p->set("roomHeight",roomHeight);for(int block=0;block<10;++block){pump(*p);for(int voice=0;voice<3;++voice)close(p->movingPosition(voice).height,roomHeight*tide::room::towerHeightRatio,"Legacy height automation or collisions moved a tower vertically");}}
    for(int route=0;route<6;++route)p->set(RoomProcessor::lfoId(route,"target"),(float)RoomPresets::slowTides().getChildWithProperty("id",RoomProcessor::lfoId(route,"target")).getProperty("value"));
    auto stationary=p->movingPosition(0);for(int i=0;i<100;++i)pump(*p);close(p->movingPosition(0).lateral,stationary.lateral,"Zero Travel still moves planets");
    p->set("tideTravel",1);float span=0;for(int i=0;i<200;++i){pump(*p);span=std::max(span,std::abs(p->movingPosition(0).lateral-stationary.lateral));}check(span>.1f,"Travel did not restore motion");
    for(int i=0;i<6;++i)close(p->get(RoomProcessor::lfoId(i,"rate")),(float)RoomPresets::slowTides().getChildWithProperty("id",RoomProcessor::lfoId(i,"rate")).getProperty("value"),"Macro overwrote individual timing");
    p->releaseResources();std::cout<<"PASS physical Bounce/Inertia, fixed height, Travel and retained individual timings\n";
}
juce::AudioBuffer<float> coupledAudio(bool enabled,float amount){
    auto p=std::make_unique<RoomProcessor>(false);restore(*p,RoomPresets::slowTides());p->set("tapeOn",0);p->set("oceanOn",enabled?1.f:0.f);p->set("tideSound",amount);
    p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);p->play();juce::AudioBuffer<float> audio(2,48000*3);juce::MidiBuffer midi;
    for(int n=0;n<audio.getNumSamples();n+=512){juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,n,std::min(512,audio.getNumSamples()-n));p->processBlock(block,midi);}
    p->releaseResources();return audio;
}
void coupling(){const auto disabled=coupledAudio(false,1),zero=coupledAudio(true,0),full=coupledAudio(true,1);double delta=0;
    for(int c=0;c<2;++c)for(int n=0;n<disabled.getNumSamples();++n){check(disabled.getSample(c,n)==zero.getSample(c,n),"Zero Sound coupling did not fully disconnect modulation");const float v=full.getSample(c,n);check(std::isfinite(v)&&std::abs(v)<1,"Coupled sound clipped");delta+=std::pow(v-zero.getSample(c,n),2);}
    check(delta>.01,"Sound coupling has no audible effect");std::cout<<"PASS Sound coupling: sample-exact zero/bypass and finite, audible water modulation\n";
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;try{check(argc==2,"TidePresetCheck NEW_OUTPUT");juce::File out(argv[1]);check(!out.exists(),"Output must be new");out.createDirectory();library(out);panels(out);tapeSelection(out);physicalMacros();coupling();return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}}
