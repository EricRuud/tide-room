#include "RoomGeometry.h"
#include "ReversideRoom.h"
#include <stdexcept>

namespace tide::room {
struct ReversideRoom::Window final : juce::DocumentWindow {
    Window(juce::AudioPluginInstance& p):DocumentWindow("Reverside - shared room",juce::Colour(0xff273c39),closeButton) {
        setUsingNativeTitleBar(true);auto* editor=p.createEditorIfNeeded();if(!editor)editor=new juce::GenericAudioProcessorEditor(p);
        setContentOwned(editor,true);setResizable(editor->isResizable(),false);centreWithSize(getWidth(),getHeight());setVisible(true);toFront(true);
    }
    void closeButtonPressed() override {setVisible(false);}
};
ReversideRoom::ReversideRoom()=default;
ReversideRoom::~ReversideRoom(){closeEditor();release();}
bool ReversideRoom::load(const juce::File& file,double sr,int block,juce::AudioPlayHead* head,juce::String& error) {
    juce::OwnedArray<juce::PluginDescription> found;format.findAllTypesForFile(found,file.getFullPathName());
    for(auto* d:found)if(d->name.equalsIgnoreCase("REVERSIDE")&&!d->isInstrument){pluginDescription=*d;break;}
    if(pluginDescription.name.isEmpty()){error="Install the Reverside VST3 to use the room.";return false;}
    for(int i=0;i<3;++i) {
        auto p=format.createInstanceFromDescription(pluginDescription,sr,block,error);if(!p)return false;
        p->disableNonMainBuses();auto layout=p->getBusesLayout();if(layout.inputBuses.isEmpty()||layout.outputBuses.isEmpty()){error="Reverside needs stereo buses.";return false;}
        layout.inputBuses.getReference(0)=juce::AudioChannelSet::stereo();layout.outputBuses.getReference(0)=juce::AudioChannelSet::stereo();
        if(!p->setBusesLayout(layout)){error="Cannot configure the Reverside stereo buses.";return false;}
        p->setPlayHead(head);p->setRateAndBufferSizeDetails(sr,block);p->prepareToPlay(sr,block);
        for(auto* parameter:p->getParameters())parameters[(size_t)i].emplace(parameter->getName(100),parameter);
        plugins[(size_t)i]=std::move(p);
    }
    try{
        const char* names[]={"GeomPan","Distance","Src Z (m)","D/R Balance"};
        const float lo[]={-.8f,.1f,.3f,-1.f},hi[]={.8f,.68f,2.2f,1.f};
        for(size_t i=0;i<3;++i)for(size_t k=0;k<4;++k){auto found=parameters[i].find(names[k]);if(found==parameters[i].end())throw std::runtime_error("Missing position parameter");positionControls[i][k].prepare(found->second,lo[k],hi[k]);}
        configure(RoomSettings{},true);
    }catch(const std::exception& e){error=e.what();return false;}
    return true;
}
void ReversideRoom::prepare(double sr,int block){configured.reset();for(auto& p:plugins)if(p){p->releaseResources();p->setRateAndBufferSizeDetails(sr,block);p->prepareToPlay(sr,block);}}
void ReversideRoom::release(){for(auto& p:plugins)if(p)p->releaseResources();}
void ReversideRoom::reset(){for(auto& p:plugins)if(p)p->reset();}
void ReversideRoom::process(int i,juce::AudioBuffer<float>& b){auto& midi=noMidi[(size_t)i];midi.clear();plugins[(size_t)i]->processBlock(b,midi);}
int ReversideRoom::latency() const {int n=0;for(const auto& p:plugins)if(p)n=std::max(n,p->getLatencySamples());return n;}
float ReversideRoom::value(int i,const juce::String& name) const {
    const auto found=parameters[(size_t)i].find(name);if(found==parameters[(size_t)i].end())throw std::runtime_error("Missing Reverside parameter: "+name.toStdString());return found->second->getValue();
}
void ReversideRoom::normalized(int i,const juce::String& name,float value) {
    const auto found=parameters[(size_t)i].find(name);if(found==parameters[(size_t)i].end())throw std::runtime_error("Missing Reverside parameter: "+name.toStdString());
    auto* p=found->second;if(forceWrites||std::abs(p->getValue()-value)>1.e-6f){p->beginChangeGesture();p->setValueNotifyingHost(juce::jlimit(0.f,1.f,value));p->endChangeGesture();}
}
void ReversideRoom::set(int i,const juce::String& name,const juce::String& text) {
    auto found=parameters[(size_t)i].find(name);if(found==parameters[(size_t)i].end())throw std::runtime_error("Missing Reverside parameter: "+name.toStdString());
    normalized(i,name,found->second->getValueForText(text));
}
bool ReversideRoom::managed(const juce::String& name) {
    for(const char* id:{"Size Mode","Size X (m)","Size Y (m)","Size Z (m)","Distance","GeomPan","Src Z (m)","Dst Z (m)","Z Pos Mode","X Offset","Y Offset","Rotate XY","Mic Distance","Direct Path 3D Mode","Refl 3D Mode","Rev 3D Mode","Mix Wet Dry Bal","Rev Time","Rev Time Mode","Mix LR Gain","D/R Balance","Bypass"})if(name==id)return true;
    return false;
}
void ReversideRoom::PositionControl::prepare(juce::AudioProcessorParameter* p,float lo,float hi) {
    parameter=p;minimum=lo;maximum=hi;
    // Query the vendor's mapping once, outside processing. No string conversion,
    // controller lock, host gesture or allocation in the automation path.
    for(size_t k=0;k<values.size();++k)values[k]=p->getValueForText(juce::String(lo+(hi-lo)*(float)k/1024.f,9));
}
void ReversideRoom::PositionControl::apply(float value,bool notify)const {
    const float x=juce::jlimit(0.f,1024.f,(value-minimum)/(maximum-minimum)*1024);
    const int k=std::min(1023,(int)x);const float v=values[(size_t)k]+(x-(float)k)*(values[(size_t)k+1]-values[(size_t)k]);
    if(std::abs(parameter->getValue()-v)>1.e-6f){
        if(notify){parameter->beginChangeGesture();parameter->setValueNotifyingHost(v);parameter->endChangeGesture();}
        else parameter->setValue(v);
    }
}
void ReversideRoom::position(int i,const Position& p,bool headphones,bool notify) {
    const auto& controls=positionControls[(size_t)i];
    controls[0].apply(p.lateral,notify);controls[1].apply(p.depth,notify);controls[2].apply(p.height,notify);
    controls[3].apply(headphones?-1.f:-.05f-.65f*p.depth,notify);
}
bool ReversideRoom::positionParameter(const juce::String& name) {
    return name=="GeomPan"||name=="Distance"||name=="Src Z (m)"||name=="D/R Balance";
}
void ReversideRoom::configure(const RoomSettings& s,bool first) {
    if(!first&&!forceNext&&configured&&*configured==s)return;
    forceWrites=first||forceNext;forceNext=false;
    for(int i=0;i<3;++i) {
        if(first) {
            set(i,"Shape","0.12");set(i,"Fill","0.10");set(i,"Wall Absorb","0.45");set(i,"Decay","0.35");
            set(i,"ER On","ON");set(i,"LR On","ON");set(i,"ER DF On","ON");set(i,"LR DF On","ON");set(i,"Refl Num","60");
            set(i,"ER DF Mix","0.20");set(i,"ER DF Scatter","0.15");set(i,"LR DF Attack","0.10");set(i,"LR DF Density","0.45");
            set(i,"Rev Density","0.70");set(i,"Rev Diffuse","0.72");set(i,"Rev Diff Alg","D3");set(i,"Rev Stereo Width","0.75");
            set(i,"Mix ER-LR Bal","0.40");set(i,"Mix ER Send","0.30");
            set(i,"ER Pre-Delay Mode","AUTO");set(i,"LR Pre-Delay Mode","AUTO-ER");
            set(i,"Mod On(ER)","OFF");set(i,"Mod On(LR)","ON");set(i,"Mod Depth(LR)","0.08");set(i,"Mod Rate(LR)","0.08");
            set(i,"ER Mat Flt On","ON");set(i,"Dcy Flt On","ON");set(i,"Flt_Band0_DCY","0.75");set(i,"Flt_Band1_DCY","1.0");set(i,"Flt_Band2_DCY","0.45");set(i,"Flt_Band2_MAT","0.65");
        }
        set(i,"Bypass","0");set(i,"Size Mode","ABS");set(i,"Size X (m)",juce::String(s.width));set(i,"Size Y (m)",juce::String(s.depth));set(i,"Size Z (m)",juce::String(s.height));
        set(i,"Rev Time Mode","FREE");set(i,"Rev Time",juce::String(s.decay));set(i,"Mix LR Gain",juce::String(juce::Decibels::decibelsToGain(s.tail)));
        set(i,"Z Pos Mode","ABS");set(i,"Dst Z (m)",juce::String(tide::room::listenerHeight(s.height)));
        const bool ownDirect=s.headphones||s.movingReflections;
        if(forceWrites){set(i,"GeomPan",juce::String(s.positions[(size_t)i].lateral,9));set(i,"Distance",juce::String(s.positions[(size_t)i].depth,9));set(i,"Src Z (m)",juce::String(s.positions[(size_t)i].height,9));set(i,"D/R Balance",ownDirect?"-1":juce::String(-.05f-.65f*s.positions[(size_t)i].depth,9));}
        else position(i,s.positions[(size_t)i],ownDirect,true);
        set(i,"X Offset","0");set(i,"Y Offset","-0.6");set(i,"Rotate XY","0");set(i,"Mic Distance",juce::String(.20f/s.width));
        set(i,"Direct Path 3D Mode",ownDirect?"OFF":"3D");set(i,"Refl 3D Mode","Q2");set(i,"Rev 3D Mode","Q2");set(i,"Mix Wet Dry Bal","1");
        if(s.movingReflections&&!smoothConfigured)fixedErGain[(size_t)i]=value(i,"Mix ER Gain");
        if(s.movingReflections)set(i,"Mix ER Gain","0");
        else if(smoothConfigured)normalized(i,"Mix ER Gain",fixedErGain[(size_t)i]);
    }
    for(const auto& p:parameters[0])lastMaster[p.first]=p.second->getValue();
    configured=s;
    smoothConfigured=s.movingReflections;forceWrites=false;
}
void ReversideRoom::shareEditorChanges() {
    for(const auto& p:parameters[0]) {
        const float v=p.second->getValue();auto found=lastMaster.find(p.first);
        if(found!=lastMaster.end()&&std::abs(found->second-v)>1.e-6f){
            if(p.first=="Mix ER Gain"&&smoothConfigured){fixedErGain[0]=v;for(int i=1;i<3;++i)fixedErGain[(size_t)i]=v;configured.reset();}
            else if(positionParameter(p.first))configured.reset(); // Manual editor move: restore the host's anchor.
            else if(managed(p.first))configured.reset();
            else for(int i=1;i<3;++i)normalized(i,p.first,v);
        }
        lastMaster[p.first]=v;
    }
}
void ReversideRoom::openEditor(){if(!window){window=std::make_unique<Window>(*plugins[0]);if(configured){const auto settings=*configured;forceNext=true;configure(settings);}}window->setVisible(true);window->toFront(true);}
void ReversideRoom::closeEditor(){window.reset();}
juce::ValueTree ReversideRoom::saveState(){juce::ValueTree tree("Reverside");tree.setProperty("smoothReflections",smoothConfigured,nullptr);for(int i=0;i<3;++i){tree.setProperty("fixedErGain"+juce::String(i),smoothConfigured?fixedErGain[(size_t)i]:value(i,"Mix ER Gain"),nullptr);juce::MemoryBlock data;plugins[(size_t)i]->getStateInformation(data);tree.setProperty("source"+juce::String(i),data.toBase64Encoding(),nullptr);}return tree;}
void ReversideRoom::restoreState(const juce::ValueTree& tree){configured.reset();forceNext=true;smoothConfigured=(bool)tree.getProperty("smoothReflections",false);for(int i=0;i<3;++i){fixedErGain[(size_t)i]=(float)tree.getProperty("fixedErGain"+juce::String(i),.25f);juce::MemoryBlock data;if(data.fromBase64Encoding(tree.getProperty("source"+juce::String(i)).toString())&&data.getSize()>0)plugins[(size_t)i]->setStateInformation(data.getData(),(int)data.getSize());}}
}
