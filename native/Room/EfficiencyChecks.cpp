#include "RoomProcessor.h"
#include "RoomEditor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>
namespace {
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
double now(){return juce::Time::getMillisecondCounterHiRes()*.001;}
void engineChecks(){
    double originalSeconds=0,optimizedSeconds=0;
    for(double sr:{44100.,48000.,96000.})for(int voice=0;voice<tide::room::patchCount;++voice){
        tide::Engine original,optimized;auto settings=tide::room::patches[(size_t)voice].values;settings.space=0;settings.output=0;original.setSettings(settings);optimized.setSettings(settings);original.prepare(sr,512,true);optimized.prepare(sr,512,false);
        check(original.getCloseIRSize()>0&&optimized.getCloseIRSize()==0&&optimized.getBloomIRSize()==0,"Internal room preparation configuration");juce::AudioBuffer<float> a(2,512),b(2,512);double error=0;
        for(int block=0;block<80;++block){if(block%20==0){const auto note=juce::MidiMessage::noteOn(1,48+block/4,.8f);original.midi(note);optimized.midi(note);}if(block==60){settings.timbre=.97f;settings.decay=.07f;original.setSettings(settings);optimized.setSettings(settings);}if(block==70){original.midi(juce::MidiMessage::allNotesOff(1));optimized.midi(juce::MidiMessage::allNotesOff(1));}
            auto start=now();original.render(a.getWritePointer(0),a.getWritePointer(1),512);originalSeconds+=now()-start;start=now();optimized.render(b.getWritePointer(0),b.getWritePointer(1),512);optimizedSeconds+=now()-start;
            for(int c=0;c<2;++c)for(int i=0;i<512;++i){check(std::isfinite(b.getSample(c,i)),"Non-finite voice");error=std::max(error,(double)std::abs(a.getSample(c,i)-b.getSample(c,i)));}
        }
        check(error==0,"Removing zero-mix convolutions changed audible synthesis");std::cout<<"ENGINE sr="<<sr<<" voice="<<voice<<" max_error="<<error<<"\n";
    }
    std::cout<<"ENGINE_COST original_seconds="<<originalSeconds<<" optimized_seconds="<<optimizedSeconds<<" ratio="<<optimizedSeconds/originalSeconds<<"\n";
}
void writeWav(const juce::File& f,const juce::AudioBuffer<float>& b,double sr){juce::WavAudioFormat fmt;auto stream=f.createOutputStream();check(stream!=nullptr,"WAV stream");std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(stream.release(),sr,2,32,{},0));check(w&&w->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"WAV output");}
struct Stats{double total=0,p99=0,worst=0;int late=0;float peak=0;};
Stats render(RoomProcessor& p,juce::AudioBuffer<float>& a,int block,double sr){Stats s;std::vector<double> times;juce::MidiBuffer midi;
    for(int offset=0;offset<a.getNumSamples();offset+=block){const int n=std::min(block,a.getNumSamples()-offset);juce::AudioBuffer<float> b(a.getArrayOfWritePointers(),2,offset,n);const auto start=now();p.processBlock(b,midi);double duration=now()-start;times.push_back(duration);s.total+=duration;if(duration>n/sr)++s.late;for(int c=0;c<2;++c)for(int i=0;i<n;++i)check(std::isfinite(b.getSample(c,i)),"Non-finite scene");}
    std::sort(times.begin(),times.end());s.p99=times[(size_t)(times.size()*.99)];s.worst=times.back();s.peak=a.getMagnitude(0,a.getNumSamples());return s;
}
void scene(const juce::File& out){
    const double sr=48000;RoomProcessor p(false);p.setRateAndBufferSizeDetails(sr,1024);p.prepareToPlay(sr,1024);check(p.loadRoom(juce::File("/Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3")),p.status().toRawUTF8());
    auto* room=p.roomForTest();auto roomSettings=p.roomSettings();const auto width=room->value(0,"Size X (m)");room->set(0,"Size X (m)","7");room->shareEditorChanges();room->configure(roomSettings);check(room->value(0,"Size X (m)")==width,"Managed editor change escaped cached configure");room->set(0,"Wall Absorb","0.56");room->shareEditorChanges();room->configure(roomSettings);check(room->value(0,"Wall Absorb")==room->value(2,"Wall Absorb"),"Shared room change did not propagate");room->set(0,"Wall Absorb","0.45");room->shareEditorChanges();auto state=room->saveState();roomSettings.width=14;room->configure(roomSettings);room->restoreState(state);room->configure(p.roomSettings());check(room->value(0,"Size X (m)")==width,"Room cache survived state restore incorrectly");std::cout<<"PASS room configuration cache, shared editor changes, managed controls, recall\n";
    juce::AudioBuffer<float> idle(2,(int)sr*2);auto empty=render(p,idle,1024,sr);check(p.sleepingForTest(),"Fresh silent graph did not sleep");check(empty.peak<1e-8,"Fresh graph emitted audio");auto asleep=render(p,idle,1024,sr);std::cout<<"IDLE cpu_fraction="<<asleep.total/2<<" worst_ms="<<asleep.worst*1000<<"\n";
    for(int q=0;q<3;++q){
        p.set("tapeOn",1);p.set("tapeModel",1);p.set("tapeQuality",(float)q);p.set("spatialDrive",36);p.set("spatialSoftness",96);p.set("spatialTrim",6.8f);p.set("evolution",.87f);p.set("tempo",160);p.set("roomWidth",12.9f);p.set("roomDepth",13.6f);p.set("roomHeight",7);p.set("roomDecay",3.48f);p.set("tail",0);
        p.selectVoice(0,13);p.selectVoice(1,6);p.selectVoice(2,11);p.set("p2_length",1.834f);p.set("p2_brightness",1);for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"level"),0);
        p.prepareToPlay(sr,1024);p.updateRoom();juce::AudioBuffer<float> warm(2,(int)sr);render(p,warm,1024,sr);p.play();juce::AudioBuffer<float> music(2,(int)sr*20);auto st=render(p,music,1024,sr);check(!p.sleepingForTest()&&st.peak>.001f&&st.peak<.9f&&p.tapeGuards()==0,"Heavy scene did not wake/render cleanly");writeWav(out.getChildFile("heavy-q"+juce::String(1<<q)+".wav"),music,sr);
        std::cout<<"HEAVY q="<<(1<<q)<<" cpu_fraction="<<st.total/20<<" p99_ms="<<st.p99*1000<<" worst_ms="<<st.worst*1000<<" over_budget="<<st.late<<" peak="<<st.peak<<" guards="<<p.tapeGuards()<<"\n";
    }
    p.stop();juce::AudioBuffer<float> tail(2,(int)sr*25);render(p,tail,1024,sr);check(p.sleepingForTest(),"Stopped graph failed to sleep after tail decay");check(tail.getMagnitude(0,48000)>.0001f,"Stop truncated the audible tail");writeWav(out.getChildFile("stop-tail.wav"),tail,sr);
    p.quiet();render(p,idle,1024,sr);p.play();juce::AudioBuffer<float> resumed(2,(int)sr*2);auto restart=render(p,resumed,1024,sr);check(restart.peak>.001f&&!p.sleepingForTest(),"Quiet on dormant graph prevented restart");p.quiet();render(p,tail,1024,sr);check(p.sleepingForTest(),"Quiet graph failed to sleep");std::cout<<"PASS audible Stop tail, sleep, quiet-while-asleep, restart, quiet sleep\n";
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());juce::PNGImageFormat png;
    for(int on=0;on<2;++on){p.set("tapeOn",(float)on);auto image=static_cast<RoomEditor*>(editor.get())->tapePanelSnapshot();auto stream=out.getChildFile(on?"tape-on.png":"tape-off.png").createOutputStream();png.writeImageToStream(image,*stream);}
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;try{check(argc==3,"Use --engine/--scene NEW_OUTPUT");auto out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);check(!out.exists(),"Output must be new");out.createDirectory();if(juce::String(argv[1])=="--engine")engineChecks();else scene(out);return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
