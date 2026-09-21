#include "RoomEditor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void save(const juce::File& file,const juce::AudioBuffer<float>& audio,double rate){
    juce::WavAudioFormat format;auto stream=file.createOutputStream();check(stream!=nullptr,"WAV stream");
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,2,24,{},0));
    check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"WAV write");
}
void png(const juce::File& file,const juce::Image& image){juce::PNGImageFormat format;auto stream=file.createOutputStream();check(stream&&format.writeImageToStream(image,*stream),"PNG write");}
struct Stats{double peak=0,rms=0,average=0,p99=0;};
Stats render(RoomProcessor& p,juce::AudioBuffer<float>& audio,double rate,int block){
    juce::MidiBuffer midi;Stats s;std::vector<double> times;double elapsed=0,energy=0;
    for(int offset=0;offset<audio.getNumSamples();offset+=block){const int count=std::min(block,audio.getNumSamples()-offset);
        juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,offset,count);const auto start=std::chrono::steady_clock::now();p.processBlock(b,midi);
        const double duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();elapsed+=duration;times.push_back(duration*rate/count);
        for(int c=0;c<2;++c)for(int n=0;n<count;++n){const double v=b.getSample(c,n);check(std::isfinite(v),"Non-finite room sample");s.peak=std::max(s.peak,std::abs(v));energy+=v*v;}
    }
    s.rms=std::sqrt(energy/(2*audio.getNumSamples()));s.average=elapsed/(audio.getNumSamples()/rate);std::sort(times.begin(),times.end());s.p99=times[(times.size()-1)*99/100];return s;
}
void report(const char* title,const Stats& s){std::cout<<title<<" peak="<<s.peak<<" rms="<<s.rms<<" callback_average="<<100*s.average<<"% p99="<<100*s.p99<<"%\n";}
void restoreSettings(RoomProcessor& p,const juce::File& file){
    if(!file.existsAsFile())return;auto xml=juce::XmlDocument::parse(file);check(xml!=nullptr,"Settings XML");
    for(auto* child:xml->getChildIterator())if(child->getStringAttribute("name")=="filterState"){
        juce::MemoryBlock state;check(state.fromBase64Encoding(child->getStringAttribute("val")),"Settings decode");p.setStateInformation(state.getData(),(int)state.getSize());return;
    }throw std::runtime_error("No filterState in settings");
}
void core(const juce::File& out){
    for(double rate:{44100.,48000.,96000.})for(float sustain:{0.f,.5f,1.f}){
        tide::room::WoodRoom room;room.prepare(rate,sustain,.25f);juce::AudioBuffer<float> silence(2,2048);silence.clear();room.process(silence,sustain,.25f,0);
        juce::AudioBuffer<float> audio(2,(int)(rate*12));audio.clear();audio.setSample(0,0,.25f);audio.setSample(1,0,.25f);
        double energy=0,late=0;float peak=0;
        for(int offset=0;offset<audio.getNumSamples();offset+=127){juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,offset,std::min(127,audio.getNumSamples()-offset));room.process(b,sustain,.25f,0);}
        for(int c=0;c<2;++c)for(int n=0;n<audio.getNumSamples();++n){const float v=audio.getSample(c,n);check(std::isfinite(v),"Core impulse nonfinite");energy+=v*v;if(n>rate*.3)late+=v*v;peak=std::max(peak,std::abs(v));}
        check(energy>1.e-8&&peak<1&&room.guardCount()==0,"Core impulse silent, unstable or guarded");
        check(audio.getMagnitude(0,audio.getNumSamples()-(int)rate,(int)rate)<1.e-6,"Core tail does not settle");
        std::cout<<"CORE rate="<<rate<<" sustain="<<sustain<<" peak="<<peak<<" energy="<<energy<<" energy_after_300ms="<<late<<"\n";
        if(rate==48000)save(out.getChildFile("impulse-sustain-"+juce::String(sustain,1)+".wav"),audio,rate);
    }
    // A held tone isolates discontinuities under the only two exposed changes.
    tide::room::WoodRoom moving;moving.prepare(48000,.5f,.25f);juce::AudioBuffer<float> tone(2,48000*6);double phase=0;
    for(int offset=0;offset<tone.getNumSamples();offset+=127){const int count=std::min(127,tone.getNumSamples()-offset);juce::AudioBuffer<float> b(tone.getArrayOfWritePointers(),2,offset,count);
        for(int n=0;n<count;++n){phase+=2*juce::MathConstants<double>::pi*997/48000;for(int c=0;c<2;++c)b.setSample(c,n,.1f*(float)std::sin(phase));}
        const float t=(float)offset/tone.getNumSamples();moving.process(b,.5f+.5f*(float)std::sin(t*30),.5f+.5f*(float)std::cos(t*24),-3);
    }
    check(moving.guardCount()==0&&tone.getMagnitude(0,tone.getNumSamples())<1,"Modulated room instability");save(out.getChildFile("sustain-warmth-motion.wav"),tone,48000);
    std::cout<<"PASS native core at three rates and sustain extremes; continuous sustain/warmth controls\n";
}
void lifecycle(RoomProcessor& p){
    p.set("tapeOn",0);p.set("woodRegen",1);p.set("woodTail",0);p.updateRoom();p.prepareToPlay(48000,512);p.play();
    juce::AudioBuffer<float> music(2,48000*3);check(render(p,music,48000,127).peak>.001,"Native room not ready without external plugin");
    p.stop();juce::AudioBuffer<float> tail(2,48000*24);check(render(p,tail,48000,511).rms>0,"Stop cut room tail");std::cout<<"STOP last_second_peak="<<tail.getMagnitude(0,tail.getNumSamples()-48000,48000)<<" last_second_rms="<<tail.getRMSLevel(0,tail.getNumSamples()-48000,48000)<<" sleeping="<<p.sleepingForTest()<<"\n";check(p.sleepingForTest()&&tail.getMagnitude(0,tail.getNumSamples()-48000,48000)==0,"Stop does not sleep");
    p.play();check(render(p,music,48000,2048).peak>.001,"Resume after sleep");p.quiet();juce::AudioBuffer<float> quiet(2,48000);render(p,quiet,48000,127);check(quiet.getMagnitude(0,24000,24000)==0,"Quiet did not silence");
    p.selectWornPreset(0);p.set("wornNoise",1);p.set("woodRegen",.5f);p.play();render(p,music,48000,512);p.stop();render(p,tail,48000,511);check(p.sleepingForTest()&&tail.getMagnitude(0,tail.getNumSamples()-48000,48000)==0,"Worn hiss prevented sleep");
    p.play();render(p,music,48000,512);for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"mute"),1);render(p,quiet,48000,127);check(quiet.getMagnitude(0,24000,24000)==0,"All source mutes left sound");
    check(p.tapeGuards()==0&&p.roomGuards()==0,"Lifecycle safety guards");
    std::cout<<"PASS native loading, Stop tails, sleep, Quiet, resume, mutes, Worn hiss and oversized callbacks\n";
}
}
int main(int argc,char* argv[]){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;
    try{check(argc>=2,"Usage: TideWoodCheck NEW_OUTPUT [SAVED_SETTINGS]");juce::File out(argv[1]);check(!out.exists(),"Use a new output directory");out.createDirectory();
        if(argc>3&&juce::String(argv[3])=="--placement"){
            RoomProcessor p(false);p.set("tapeOn",0);p.set("output",-3);p.set("reflections",-6);p.set("woodTail",-6);p.set("p1_mute",1);p.set("p2_mute",1);p.selectVoice(0,6);p.set("p0_length",.16f);p.set("p0_brightness",.8f);
            for(int mode=0;mode<2;++mode){p.set("placement",(float)mode);std::array<double,4> ratios{},energies{};juce::AudioBuffer<float> audio(2,48000*16);
                for(int pose=0;pose<4;++pose){p.stop();p.set("p0_x",pose==0?-.6f:pose==1?.6f:0);p.set("p0_y",pose==2?.1f:pose==3?.65f:.25f);p.updateRoom();p.prepareToPlay(48000,512);p.play();juce::AudioBuffer<float> section(audio.getArrayOfWritePointers(),2,pose*192000,192000);const auto st=render(p,section,48000,127);check(st.peak>.001&&st.peak<1,"Placement scene peak");double e[2]{};for(int c=0;c<2;++c)for(int n=0;n<section.getNumSamples();++n)e[c]+=(double)section.getSample(c,n)*section.getSample(c,n);ratios[(size_t)pose]=10*std::log10(e[0]/e[1]);energies[(size_t)pose]=e[0]+e[1];}
                const double nearFar=10*std::log10(energies[2]/energies[3]);check(ratios[0]>.3&&ratios[1]<-.3&&nearFar>3,"Placement cues did not follow source");std::cout<<"PLACEMENT mode="<<mode<<" left_LR_dB="<<ratios[0]<<" right_LR_dB="<<ratios[1]<<" near_far_dB="<<nearFar<<"\n";save(out.getChildFile(mode?"stereo-positions.wav":"headphone-positions.wav"),audio,48000);
            }
            p.set("placement",0);p.set("lfo0_on",1);p.set("lfo1_on",1);p.set("lfo0_rate",1);p.set("lfo1_rate",1);p.set("lfo0_depth",.7f);p.set("lfo1_depth",.7f);p.updateRoom();p.prepareToPlay(48000,512);p.play();juce::AudioBuffer<float> motion(2,48000*6);const auto st=render(p,motion,48000,1024);check(st.peak<1&&p.roomGuards()==0,"Headphone motion safety");save(out.getChildFile("headphone-motion.wav"),motion,48000);std::cout<<"PASS headphone/stereo direction and distance cues, moving headphone scene\n";return 0;
        }
        if(argc>3&&juce::String(argv[3])=="--lifecycle"){RoomProcessor p(false);if(argc>2)restoreSettings(p,juce::File(argv[2]));lifecycle(p);return 0;}core(out);
        RoomProcessor p(false);if(argc>2)restoreSettings(p,juce::File(argv[2]));
        juce::MemoryBlock original;p.getStateInformation(original);p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);p.updateRoom();
        check(p.ready()&&p.roomForTest()==nullptr,"External reverb loaded or required");
        {RoomEditor editor(p);png(out.getChildFile("wood-room.png"),editor.createComponentSnapshot(editor.getLocalBounds()));png(out.getChildFile("motion.png"),editor.motionPanelSnapshot());}
        for(int tapeMode=0;tapeMode<2;++tapeMode){p.setStateInformation(original.getData(),(int)original.getSize());if(tapeMode==0)p.set("tapeOn",0);p.updateRoom();p.prepareToPlay(48000,512);p.play();
            juce::AudioBuffer<float> audio(2,48000*30);juce::AudioBuffer<float> music(audio.getArrayOfWritePointers(),2,0,48000*24);const auto stats=render(p,music,48000,1024);check(stats.peak>.001&&stats.peak<1,"Scene silent or clipped");report(tapeMode?"SAVED TAPE":"TAPE OFF",stats);
            p.stop();juce::AudioBuffer<float> tail(audio.getArrayOfWritePointers(),2,48000*24,48000*6);render(p,tail,48000,1024);save(out.getChildFile(tapeMode?"wood-room-saved-tape.wav":"wood-room-tape-off.wav"),audio,48000);
        }
        p.setStateInformation(original.getData(),(int)original.getSize());p.set("tapeOn",0);for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"mute"),0);p.updateRoom();p.prepareToPlay(48000,512);p.play();
        juce::AudioBuffer<float> patterns(2,48000*32);for(int i=0;i<8;++i){p.set("patternBank",(float)i);juce::AudioBuffer<float> section(patterns.getArrayOfWritePointers(),2,i*192000,192000);const auto st=render(p,section,48000,i%2?127:1024);check(st.peak>.0001&&st.peak<1,"Pattern room failed");report(tide::room::patternBank[(size_t)i].name,st);}save(out.getChildFile("eight-patterns.wav"),patterns,48000);
        p.set("woodRegen",.82f);p.set("woodWarmth",.61f);juce::MemoryBlock state;p.getStateInformation(state);RoomProcessor restored(false);restored.setStateInformation(state.getData(),(int)state.getSize());check(std::abs(restored.get("woodRegen")-.82f)<.001&&std::abs(restored.get("woodWarmth")-.61f)<.001&&!restored.isPlaying(),"Wood settings recall");
        for(double rate:{44100.,96000.}){p.stop();p.set("woodRegen",1);p.set("woodTail",0);p.set("output",0);p.set("evolution",1);p.set("tempo",160);for(int i=0;i<3;++i){p.set(RoomProcessor::partId(i,"level"),0);p.set(RoomProcessor::partId(i,"density"),1);p.set(RoomProcessor::partId(i,"length"),2.5f);}for(int i=0;i<6;++i){p.set(RoomProcessor::lfoId(i,"on"),1);p.set(RoomProcessor::lfoId(i,"rate"),i%2?4.f:.01f);p.set(RoomProcessor::lfoId(i,"depth"),1);}p.updateRoom();p.prepareToPlay(rate,512);p.play();juce::AudioBuffer<float> audio(2,(int)rate*6);const auto st=render(p,audio,rate,1024);check(st.peak>.001&&st.peak<1,"Rate/motion/sustain stress clipping");report(rate==44100?"STRESS 44.1k":"STRESS 96k",st);}
        p.setStateInformation(original.getData(),(int)original.getSize());lifecycle(p);p.releaseResources();
        std::cout<<"PASS eight patterns, saved scene, wood-state recall, 44.1/48/96 kHz and six-LFO extremes\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
