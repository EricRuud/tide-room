#include <juce_audio_utils/juce_audio_utils.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

void check(bool b,const char* message) {if(!b) throw std::runtime_error(message);}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        if(argc==4&&juce::String(argv[1])=="--position-probe") {
            juce::VST3PluginFormat f;juce::OwnedArray<juce::PluginDescription> descriptions;f.findAllTypesForFile(descriptions,argv[2]);check(!descriptions.isEmpty(),"No plugin found");
            const juce::File out(argv[3]);check(!out.exists(),"Choose a new probe directory");out.createDirectory();
            for(float distance:{.1f,.2f,.4f})for(float offset:{0.f,-.6f}) {
                juce::String error;auto p=f.createInstanceFromDescription(*descriptions[0],48000,256,error);check(p!=nullptr,error.toRawUTF8());
                auto set=[&](const char* name,const juce::String& value){for(auto* q:p->getParameters())if(q->getName(100)==name){q->setValueNotifyingHost(q->getValueForText(value));return;}throw std::runtime_error(name);};
                set("Size Mode","ABS");set("Size X (m)","30");set("Size Y (m)","12");set("Size Z (m)","30");set("Z Pos Mode","ABS");set("Src Z (m)","15");set("Dst Z (m)","15");
                set("Distance",juce::String(distance));set("Y Offset",juce::String(offset));set("GeomPan","0");set("Mic Distance","0");set("Shape","0");set("Fill","0");
                set("Direct Path 3D Mode","ON");set("Refl 3D Mode","OFF");set("Mix Wet Dry Bal","1");set("LR On","OFF");set("ER DF On","OFF");set("ER Mat Flt On","OFF");set("Mod On(ER)","OFF");
                set("Refl Num","50");set("ER Pre-Delay Mode","ABS(F)");set("ER Pre-Delay (ms)","0");
                p->setRateAndBufferSizeDetails(48000,256);p->prepareToPlay(48000,256);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
                for(int i=0;i<200;++i){block.clear();p->processBlock(block,midi);juce::Thread::sleep(5);}
                juce::AudioBuffer<float> audio(2,16384);audio.clear();audio.setSample(0,0,.1f);audio.setSample(1,0,.1f);
                for(int pos=0;pos<audio.getNumSamples();pos+=256){juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,pos,256);p->processBlock(b,midi);}
                auto stream=out.getChildFile(juce::String(distance,1)+"-"+juce::String(offset,1)+".wav").createOutputStream();juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatWriter>w(wav.createWriterFor(stream.release(),48000,2,24,{},0));w->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples());p->releaseResources();
            }
            return 0;
        }
        if(argc==4&&juce::String(argv[1])=="--describe-effect") {
            juce::VST3PluginFormat f;juce::OwnedArray<juce::PluginDescription> descriptions;f.findAllTypesForFile(descriptions,argv[2]);
            check(!descriptions.isEmpty(),"No plugin found");juce::String error;auto p=f.createInstanceFromDescription(*descriptions[0],48000,256,error);
            check(p!=nullptr,error.toRawUTF8());juce::Array<juce::var> rows;
            for(auto* parameter:p->getParameters()) {
                juce::DynamicObject::Ptr row=new juce::DynamicObject;row->setProperty("name",parameter->getName(100));row->setProperty("value",parameter->getValue());row->setProperty("steps",parameter->getNumSteps());
                juce::Array<juce::var> labels;for(float value:{0.f,.25f,.5f,.75f,1.f})labels.add(parameter->getText(value,100));row->setProperty("labels",labels);
                row->setProperty("parse1.5",parameter->getValueForText("1.5"));rows.add(juce::var(row.get()));
            }
            check(juce::File(argv[3]).replaceWithText(juce::JSON::toString(rows,true)),"Cannot write metadata");return 0;
        }
        if(argc==3&&juce::String(argv[1])=="--effect") {
            juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile(descriptions,argv[2]);
            check(descriptions.size()>0,"Effect scan found no plugin");
            auto& desc=*descriptions[0];juce::String error;
            std::cout<<"Found "<<desc.name<<" by "<<desc.manufacturerName<<" version "<<desc.version<<" inputs="<<desc.numInputChannels<<" outputs="<<desc.numOutputChannels<<"\n";
            auto p=format.createInstanceFromDescription(desc,48000,256,error);
            if(!p) throw std::runtime_error(error.toStdString());
            p->enableAllBuses();p->setRateAndBufferSizeDetails(48000,256);p->prepareToPlay(48000,256);
            juce::AudioBuffer<float> b(2,256);juce::MidiBuffer midi;double wetEnergy=0;float peak=0;
            for(int block=0;block<938;++block) {
                for(int c=0;c<2;++c) for(int i=0;i<256;++i) {const int n=block*256+i;b.setSample(c,i,n<48000?.05f*(float)std::sin(2*juce::MathConstants<double>::pi*220*n/48000):0);}
                p->processBlock(b,midi);midi.clear();
                for(int c=0;c<2;++c) for(int i=0;i<256;++i) {float x=b.getSample(c,i);check(std::isfinite(x),"Effect emitted non-finite audio");peak=std::max(peak,std::abs(x));if(block>200)wetEnergy+=(double)x*x;}
            }
            juce::MemoryBlock state;p->getStateInformation(state);
            std::cout<<"Rendered: peak="<<peak<<" tail_energy="<<wetEnergy<<" latency="<<p->getLatencySamples()<<" saved_state_bytes="<<state.getSize()<<"\n";
            for(auto* param:p->getParameters()) std::cout<<"PARAM "<<param->getName(100)<<" = "<<param->getValue()<<"\n";
            p->releaseResources();return 0;
        }
        check(argc==2,"Pass the absolute VST3 bundle path");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions,argv[1]);
        check(descriptions.size()==1,"VST3 scan should discover exactly one instrument");
        for(double rate:{44100.,48000.,96000.}) for(int frames:{64,127,512}) {
            juce::String error;
            auto plugin=format.createInstanceFromDescription(*descriptions[0],rate,frames,error);
            if(!plugin) throw std::runtime_error(error.toStdString());
            plugin->setRateAndBufferSizeDetails(rate,frames);
            plugin->prepareToPlay(rate,frames);
            check(plugin->getTotalNumOutputChannels()==2,"Expected stereo output");
            check(plugin->getLatencySamples()==192,"Host latency mismatch");
            plugin->setCurrentProgram(3);
            juce::MemoryBlock state;plugin->getStateInformation(state);
            plugin->setCurrentProgram(0);plugin->setStateInformation(state.getData(),(int)state.getSize());
            check(plugin->getCurrentProgram()==3,"VST3 state restore failed");
            juce::AudioBuffer<float> block(2,frames);juce::MidiBuffer midi;
            float peak=0;double sum=0,elapsed=0,worst=0;
            const int blocks=(int)(rate*2/frames);
            for(int b=0;b<blocks;++b) {
                block.clear();midi.clear();
                if(b==0) for(int n:{36,43,48,52,55,60,64,67}) midi.addEvent(juce::MidiMessage::noteOn(1,n,.9f),0);
                const auto start=std::chrono::steady_clock::now();
                plugin->processBlock(block,midi);
                const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                elapsed+=seconds;worst=std::max(worst,seconds);
                for(int c=0;c<2;++c) for(int i=0;i<frames;++i) {
                    const float x=block.getSample(c,i);check(std::isfinite(x),"Non-finite host audio");
                    peak=std::max(peak,std::abs(x));sum+=(double)x*x;
                }
            }
            check(peak>.01f&&peak<1,"Host output missing or clipping");
            check(sum>1,"VST3 MIDI failed to produce audio");
            std::cout<<"PASS VST3 "<<rate<<" Hz / "<<frames<<" frames: peak="<<peak
                <<" average_cpu_percent="<<100*elapsed/(blocks*frames/rate)
                <<" worst_callback_ms="<<worst*1000<<"\n";
            auto editor=std::unique_ptr<juce::AudioProcessorEditor>(plugin->createEditor());
            check(editor&&editor->getWidth()>800,"VST3 editor failed");
            editor.reset();plugin->releaseResources();
        }
        std::cout<<"PASS VST3 scan, instantiation, state, MIDI, audio, latency, editor lifecycle\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
