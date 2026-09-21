#include "RoomProcessor.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <tuple>
#include <vector>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using ScoreEvent=std::tuple<uint64_t,int,int,int,float,bool>;
std::vector<ScoreEvent> score(int block,double rate,uint32_t seed,int length=90){tide::room::Pattern p;p.reset(rate);tide::room::PatternSettings s;s.seed=seed;std::vector<ScoreEvent> events;for(int frame=0;frame<(int)(rate*length);frame+=block){auto notes=p.advance(std::min(block,(int)(rate*length)-frame),s);check(notes.size<128,"Event buffer overflow");for(int i=0;i<notes.size;++i){const auto e=notes.data[(size_t)i];check(e.offset>=0&&e.offset<block&&e.note>=0&&e.note<=127,"Invalid event");events.emplace_back((uint64_t)frame+(uint64_t)e.offset,e.part,e.note,e.step,e.velocity,e.on);}}return events;}
void patternChecks(){for(double sr:{44100.,48000.,96000.}){const auto reference=score(512,sr,31415);for(int block:{17,127,256,2048})check(score(block,sr,31415)==reference,"Pattern depends on buffer partition");check(score(511,sr,42)!=reference,"Variation has no effect");
    std::vector<uint64_t> foundation;for(const auto& e:reference)if(std::get<1>(e)==0&&std::get<5>(e))foundation.push_back(std::get<0>(e));for(size_t i=1;i<foundation.size();++i)check(std::abs((double)(foundation[i]-foundation[i-1])-sr*60/108*.5)<=1,"Foundation pulse drifted");
    auto other=score(512,sr,42);std::vector<ScoreEvent> a,b;for(const auto& e:reference)if(std::get<1>(e)==0)a.push_back(e);for(const auto& e:other)if(std::get<1>(e)==0)b.push_back(e);check(a==b,"Variation changed the anchor");}
    tide::room::Pattern p;p.reset(48000);tide::room::PatternSettings s;for(int i=0;i<1000;++i){s.bpm=i<500?60:160;auto e=p.advance(512,s);for(int n=0;n<e.size;++n)check(e.data[(size_t)n].offset>=0&&e.data[(size_t)n].offset<512,"Tempo change misplaced event");}
    std::cout<<"PASS sample timing, buffer independence, seeded variation, anchored foundation and tempo changes\n";
}
void writeWav(const juce::File& file,juce::AudioBuffer<float>& audio,double rate){auto stream=file.createOutputStream();check(stream!=nullptr,"Cannot write WAV");juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,2,24,{},0));check(writer!=nullptr,"Cannot create WAV writer");check(writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"WAV write failed");}
void stateChecks(RoomProcessor& p){p.set("tempo",123);p.set(RoomProcessor::partId(2,"x"),-.31f);p.set(RoomProcessor::partId(1,"brightness"),.25f);p.set("seed",718);juce::MemoryBlock data;p.getStateInformation(data);p.set("tempo",69);p.set("seed",9);p.setStateInformation(data.getData(),(int)data.getSize());check(std::abs(p.get("tempo")-123)<.01f&&p.get("seed")==718&&std::abs(p.get("p2_x")+.31f)<.001f&&std::abs(p.get("p1_brightness")-.25f)<.001f,"Scene state restore failed");check(!p.isPlaying(),"Restored transport should be stopped");p.set("tempo",108);p.set("seed",31415);p.set("p2_x",.08f);p.selectVoice(1,6);}
void placementStateChecks(RoomProcessor& p){
    p.set("placement",1);p.set("reflections",-12);juce::MemoryBlock data;p.getStateInformation(data);p.set("placement",0);p.set("reflections",0);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("placement")>.5f&&std::abs(p.get("reflections")+12)<.01f,"Placement controls did not restore");
    auto old=p.parameters.copyState();for(const char* id:{"placement","reflections"})old.removeChild(old.getChildWithProperty("id",id),nullptr);if(auto xml=old.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,data);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("placement")<.5f&&std::abs(p.get("reflections"))<.01f,"Old scenes should default to Headphones with unity reflections");
    std::cout<<"PASS placement/reflections state and version 0.1 migration\n";
}
struct RenderStats {double average=0,p99=0,worst=0;float peak=0;double energy=0;};
RenderStats render(RoomProcessor& p,juce::AudioBuffer<float>& audio,double sr,int block) {
    RenderStats stats;std::vector<double> times;juce::MidiBuffer midi;
    for(int offset=0;offset<audio.getNumSamples();offset+=block){const int count=std::min(block,audio.getNumSamples()-offset);juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,offset,count);const auto start=std::chrono::steady_clock::now();p.processBlock(b,midi);const double t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();times.push_back(t);stats.average+=t;stats.worst=std::max(stats.worst,t);for(int c=0;c<2;++c)for(int n=0;n<count;++n){float x=b.getSample(c,n);check(std::isfinite(x),"Non-finite scene audio");stats.peak=std::max(stats.peak,std::abs(x));stats.energy+=(double)x*x;}}
    std::sort(times.begin(),times.end());stats.p99=times[(size_t)(times.size()*.99)]*1000;stats.average/=audio.getNumSamples()/sr;stats.worst*=1000;return stats;
}
void settle(RoomProcessor& p,int blocks=150){juce::AudioBuffer<float> block(2,512);juce::MidiBuffer midi;for(int i=0;i<blocks;++i){block.clear();p.processBlock(block,midi);}}
void positionChecks(tide::room::ReversideRoom& room,const juce::File& out){
    const tide::room::RoomSettings defaults;juce::MidiBuffer midi;
    for(int i=0;i<3;++i){check(room.value(i,"Mix Wet Dry Bal")==1&&room.value(i,"Direct Path 3D Mode")==0&&room.value(i,"D/R Balance")==0,"Headphone mode contains a second direct path");check(std::abs(room.value(i,"Refl 3D Mode")-2.f/3)<.001f,"Wrong ER quality");check(std::abs(room.value(i,"Rev 3D Mode")-.5f)<.001f,"Wrong LR quality");check(std::abs(room.value(i,"Y Offset")-.2f)<.001f,"Shared listener offset mismatch");for(const char* name:{"Size X (m)","Size Y (m)","Size Z (m)","Dst Z (m)","Mic Distance","X Offset","Y Offset","Rotate XY","Rev Time","Mix LR Gain"})check(room.value(i,name)==room.value(0,name),"Shared room parameters differ");}
    std::array<double,2> ratio{};std::array<double,2> late{};
    for(int test=0;test<4;++test){auto s=defaults;s.positions[0]={test==1?.60f:-.60f,test==2?.65f:.2f,test==3?2.2f:1.1f};room.configure(s);room.reset();juce::AudioBuffer<float> empty(2,512);for(int i=0;i<300;++i){empty.clear();room.process(0,empty);juce::Thread::sleep(5);}juce::AudioBuffer<float> audio(2,48000);audio.clear();audio.setSample(0,0,.3f);audio.setSample(1,0,.3f);for(int offset=0;offset<audio.getNumSamples();offset+=512){juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,offset,std::min(512,audio.getNumSamples()-offset));room.process(0,b);}writeWav(out.getChildFile("position-"+juce::String(test)+".wav"),audio,48000);
        double energy[2]={};for(int c=0;c<2;++c)for(int n=0;n<1440;++n)energy[c]+=(double)audio.getSample(c,n)*audio.getSample(c,n);if(test<2)ratio[(size_t)test]=10*std::log10((energy[0]+1.e-20)/(energy[1]+1.e-20));
        if(test==0||test==2){double e=0;for(int c=0;c<2;++c)for(int n=1440;n<24000;++n)e+=(double)audio.getSample(c,n)*audio.getSample(c,n);late[test==0?0:1]=e/(energy[0]+energy[1]+1.e-20);}
    }
    std::cout<<"Position early L/R energy dB: left="<<ratio[0]<<" right="<<ratio[1]<<"; late/early near="<<late[0]<<" far="<<late[1]<<"\n";
    check(ratio[0]*ratio[1]<0&&std::abs(ratio[0]-ratio[1])>1,"Left/right source placement did not reverse stereo energy");check(std::abs(late[0]-late[1])>.02,"Depth did not change reflection balance");room.configure(defaults);room.reset();
    room.set(0,"Wall Absorb","0.56");room.shareEditorChanges();check(room.value(0,"Wall Absorb")==room.value(1,"Wall Absorb")&&room.value(1,"Wall Absorb")==room.value(2,"Wall Absorb"),"Shared editor update failed");room.set(0,"Wall Absorb","0.45");room.shareEditorChanges();room.configure(defaults);
    std::cout<<"PASS shared room, individual positions, direct path and shared editor changes\n";
}
void binauralChecks(const juce::File& out) {
    for(double rate:{44100.,48000.,96000.}){
        tide::room::HrtfBank bank;bank.prepare(rate);std::array<juce::AudioBuffer<float>,5> audio;
        for(int pose=0;pose<5;++pose){auto& a=audio[(size_t)pose];a.setSize(2,4096);a.clear();tide::room::BinauralSource source;source.prepare(rate,bank);std::array<float,512> impulse{};impulse[0]=.2f;
            const float x=pose==0?-3.f:pose==2?3.f:0.f,y=pose==3?1.5f:pose==4?6.f:2.4f;
            for(int offset=0;offset<4096;offset+=512){juce::AudioBuffer<float> b(a.getArrayOfWritePointers(),2,offset,512);source.render(impulse.data(),b.getWritePointer(0),b.getWritePointer(1),512,x,y,0,true);source.propagation(b,true);impulse.fill(0);}
            if(rate==48000)writeWav(out.getChildFile("direct-"+juce::String(pose)+".wav"),a,rate);
        }
        auto energy=[](const juce::AudioBuffer<float>& b,int c){double e=0;for(int n=0;n<b.getNumSamples();++n)e+=(double)b.getSample(c,n)*b.getSample(c,n);return e;};
        const double left=10*std::log10(energy(audio[0],0)/energy(audio[0],1)),right=10*std::log10(energy(audio[2],0)/energy(audio[2],1));
        double error=0;for(int n=0;n<4096;++n){error+=std::abs(audio[0].getSample(0,n)-audio[2].getSample(1,n));error+=std::abs(audio[1].getSample(0,n)-audio[1].getSample(1,n));}
        const double distance=10*std::log10(energy(audio[3],0)/energy(audio[4],0));
        check(left>5&&right<-5&&error<1.e-5,"Measured headphone filters have incorrect direction or symmetry");check(distance>10&&distance<14,"Direct distance falloff failed");
        std::cout<<"BINAURAL "<<rate<<": left_ILD_dB="<<left<<" right_ILD_dB="<<right<<" near_far_dB="<<distance<<" symmetry_error="<<error<<"\n";
        tide::room::BinauralSource moving;moving.prepare(rate,bank);juce::AudioBuffer<float> block(2,127);std::array<float,127> input{};float peak=0;
        for(int k=0;k<500;++k){for(int n=0;n<127;++n)input[(size_t)n]=.1f*(float)std::sin((k*127+n)*.031);moving.render(input.data(),block.getWritePointer(0),block.getWritePointer(1),127,3*(float)std::sin(k*.01),1.5f,1*(float)std::sin(k*.023),true);moving.propagation(block,true);for(int c=0;c<2;++c)for(int n=0;n<127;++n){check(std::isfinite(block.getSample(c,n)),"Moving binaural filter produced non-finite audio");peak=std::max(peak,std::abs(block.getSample(c,n)));}}
        check(peak<.5f,"Moving binaural filter became unstable");
    }
    std::cout<<"PASS measured HRTF directions, distance, symmetry, sample rates and moving filters\n";
}
}
int main(int argc,char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout<<std::unitbuf;
    try {
        patternChecks();if(argc==1)return 0;
        check(argc==3,"Usage: TideRoomCheck --ui|--render /new/output/directory");const juce::File out(argv[2]);check(!out.exists(),"Use a new output directory");out.createDirectory();
        if(juce::String(argv[1])=="--binaural"){binauralChecks(out);return 0;}
        RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);stateChecks(p);placementStateChecks(p);
        if(juce::String(argv[1])=="--ui"){std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto shot=editor->createComponentSnapshot(editor->getLocalBounds());juce::PNGImageFormat png;auto stream=out.getChildFile("room.png").createOutputStream();png.writeImageToStream(shot,*stream);return 0;}
        check(p.loadRoom(juce::File("/Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3")),p.status().toRawUTF8());
        if(juce::String(argv[1])=="--patterns") {
            p.selectWornPreset(0);p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> audio(2,48000*48);
            for(int i=0;i<8;++i){p.set("patternBank",(float)i);juce::AudioBuffer<float> section(audio.getArrayOfWritePointers(),2,i*288000,288000);auto st=render(p,section,48000,i%2?127:512);check(st.peak>.001f&&st.peak<.9f&&p.tapeGuards()==0,"Pattern scene silence, clipping or guards");std::cout<<"PATTERN "<<i<<" "<<tide::room::patternBank[(size_t)i].name<<" peak="<<st.peak<<" CPU="<<st.average*100<<"%\n";}
            writeWav(out.getChildFile("eight-patterns.wav"),audio,48000);p.stop();juce::AudioBuffer<float> tail(2,48000*16);render(p,tail,48000,511);check(tail.getMagnitude(0,tail.getNumSamples()-48000,48000)==0,"Pattern changes left a held voice");p.releaseResources();std::cout<<"PASS all eight patterns through synth, room and tape; finite output and clean final release\n";return 0;
        }
        if(juce::String(argv[1])=="--harmony") {
            p.selectWornPreset(0);p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> audio(2,48000*16);
            const int settings[][3]={{2,1,0},{2,1,3},{2,1,4},{9,1,0},{0,0,0},{0,0,5},{6,0,4},{11,1,6}};
            for(int i=0;i<8;++i){p.set("harmonyKey",(float)settings[i][0]);p.set("harmonyScale",(float)settings[i][1]);p.set("harmonyChord",(float)settings[i][2]);juce::AudioBuffer<float> section(audio.getArrayOfWritePointers(),2,i*96000,96000);auto st=render(p,section,48000,i%2?127:512);check(st.peak>.001f&&st.peak<.9f&&p.tapeGuards()==0,"Harmony scene silence, clipping or guards");std::cout<<"HARMONY key="<<settings[i][0]<<" minor="<<settings[i][1]<<" chord="<<settings[i][2]+1<<" peak="<<st.peak<<" CPU="<<st.average*100<<"%\n";}
            writeWav(out.getChildFile("harmony-changes.wav"),audio,48000);p.stop();juce::AudioBuffer<float> tail(2,48000*16);render(p,tail,48000,511);check(tail.getMagnitude(0,tail.getNumSamples()-48000,48000)==0,"Harmony changes left a held voice");p.releaseResources();std::cout<<"PASS audible harmony changes through synth, room and tape; finite output and clean final release\n";return 0;
        }
        if(juce::String(argv[1])=="--worn-lifecycle") {
            p.selectWornPreset(0);p.set("roomDecay",.6f);p.set("tail",-18);p.set("wornNoise",1);p.updateRoom();settle(p);p.play();
            juce::AudioBuffer<float> music(2,48000*4);auto st=render(p,music,48000,512);check(st.peak>.005f&&p.tapeGuards()==0,"Worn scene silent or guarded");
            p.stop();juce::AudioBuffer<float> tail(2,48000*16);auto ts=render(p,tail,48000,511);check(ts.energy>0,"Worn Stop cut tail");check(tail.getMagnitude(0,tail.getNumSamples()-48000,48000)==0,"Worn hiss prevented Stop from sleeping");
            p.play();check(render(p,music,48000,127).energy>0,"Worn did not resume after Stop");p.quiet();settle(p,4);juce::AudioBuffer<float> quiet(2,4096);check(render(p,quiet,48000,127).peak==0,"Worn Quiet left hiss");
            p.play();check(render(p,music,48000,2048).energy>0,"Worn did not resume after Quiet");check(p.tapeGuards()==0,"Worn lifecycle guards");p.releaseResources();
            std::cout<<"PASS worn Stop drains then sleeps with full hiss, Quiet, resume, oversized callbacks; scene CPU "<<st.average*100<<"%\n";return 0;
        }
        if(juce::String(argv[1]).startsWith("--colour-scene")) {
            p.selectVoice(0,16);p.selectVoice(1,14);p.selectVoice(2,19);p.set("p2_level",-9);p.updateRoom();settle(p);p.play();
            const int deviceBlock=juce::String(argv[1]).endsWith("1024")?1024:512;
            juce::AudioBuffer<float> scene(2,48000*34);juce::AudioBuffer<float> music(scene.getArrayOfWritePointers(),2,0,48000*30);const auto st=render(p,music,48000,deviceBlock);p.stop();juce::AudioBuffer<float> tail(scene.getArrayOfWritePointers(),2,48000*30,48000*4);render(p,tail,48000,deviceBlock);writeWav(out.getChildFile("ring-room-tape.wav"),scene,48000);
            check(p.tapeGuards()==0&&st.peak<.9f,"New scene exceeded numerical/headroom checks");std::cout<<"COLOUR SCENE peak="<<st.peak<<" callback_fraction="<<st.average<<" p99_ms="<<st.p99<<" worst_ms="<<st.worst<<" tape_guards="<<p.tapeGuards()<<"\n";
            p.quiet();settle(p);p.set("tapeOn",0);p.prepareToPlay(48000,512);p.updateRoom();settle(p);p.play();render(p,music,48000,512);p.stop();render(p,tail,48000,512);writeWav(out.getChildFile("ring-room-untaped.wav"),scene,48000);return 0;
        }
        if(juce::String(argv[1])=="--compare") {
            for(int mode=0;mode<2;++mode){p.set("placement",(float)mode);p.set("tempo",120);p.set("evolution",0);p.selectVoice(0,6);p.set("p0_brightness",.9f);p.set("p0_length",.16f);p.set("p0_z",1.5f);p.set("p0_level",-3);p.set("p1_mute",1);p.set("p2_mute",1);
                juce::AudioBuffer<float> comparison(2,48000*16);comparison.clear();
                for(int pose=0;pose<4;++pose){p.stop();p.set("p0_x",pose==0?-.65f:pose==2?.65f:0.f);p.set("p0_y",pose==3?.65f:.2f);p.prepareToPlay(48000,512);p.roomForTest()->reset();p.updateRoom();for(int n=0;n<100;++n){settle(p,1);juce::Thread::sleep(5);}p.play();
                    juce::AudioBuffer<float> section(comparison.getArrayOfWritePointers(),2,pose*48000*4,48000*4);auto st=render(p,section,48000,512);std::cout<<"COMPARE "<<mode<<" position="<<pose<<" peak="<<st.peak<<" cpu="<<st.average<<"\n";
                }
                writeWav(out.getChildFile(mode==0?"headphones-placement.wav":"original-placement.wav"),comparison,48000);
            }
            p.set("placement",0);p.set("tempo",108);p.set("evolution",.45f);p.set("p0_x",-.48f);p.set("p0_y",.30f);p.set("p0_z",1.1f);p.selectVoice(0,13);p.set("p1_mute",0);p.set("p2_mute",0);p.stop();p.prepareToPlay(48000,512);p.roomForTest()->reset();p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> scene(2,48000*28);juce::AudioBuffer<float> music(scene.getArrayOfWritePointers(),2,0,48000*24);auto st=render(p,music,48000,512);p.stop();juce::AudioBuffer<float> tail(scene.getArrayOfWritePointers(),2,48000*24,48000*4);render(p,tail,48000,512);writeWav(out.getChildFile("headphones-scene.wav"),scene,48000);std::cout<<"HEADPHONE SCENE peak="<<st.peak<<" cpu="<<st.average<<" p99_ms="<<st.p99<<" worst_ms="<<st.worst<<"\n";return 0;
        }
        if(juce::String(argv[1])=="--localize") {
            auto& room=*p.roomForTest();
            for(int mode=3;mode<5;++mode)for(int pos=0;pos<5;++pos) {
                auto settings=p.roomSettings();settings.headphones=false;settings.positions[0]={pos==0?-.7f:pos==2?.7f:0.f,pos==3?.1f:pos==4?.65f:.25f,1.5f};room.configure(settings);
                if(mode>=3){room.set(0,"ER DF On","OFF");room.set(0,"D/R Balance","1");room.set(0,"LR On","OFF");if(mode==4)room.set(0,"Direct Path 3D Mode","ON");}
                if(mode==0){room.set(0,"D/R Balance","1");room.set(0,"LR On","OFF");}
                if(mode==1){room.set(0,"D/R Balance","-1");room.set(0,"LR On","OFF");}
                if(mode==2){room.set(0,"LR On","ON");}
                room.reset();juce::AudioBuffer<float> block(2,512);for(int n=0;n<160;++n){block.clear();room.process(0,block);juce::Thread::sleep(5);}
                juce::AudioBuffer<float> ir(2,48000);ir.clear();ir.setSample(0,0,.2f);ir.setSample(1,0,.2f);
                for(int offset=0;offset<48000;offset+=512){juce::AudioBuffer<float> b(ir.getArrayOfWritePointers(),2,offset,std::min(512,48000-offset));room.process(0,b);}
                writeWav(out.getChildFile("mode-"+juce::String(mode)+"-pos-"+juce::String(pos)+".wav"),ir,48000);
                std::cout<<"LOCALIZE mode="<<mode<<" position="<<pos<<" peak="<<ir.getMagnitude(0,ir.getNumSamples())<<"\n";
            }
            return 0;
        }
        if(juce::String(argv[1])=="--stress") {
            for(const auto& voices:std::array<std::array<int,3>,3>{{{{13,6,10}},{{7,8,11}},{{0,3,11}}}}) {
                p.stop();p.prepareToPlay(48000,512);p.set("tempo",160);p.set("evolution",1);p.set("output",0);p.set("roomDecay",5);p.set("tail",0);
                for(int i=0;i<3;++i){p.selectVoice(i,voices[(size_t)i]);p.set(RoomProcessor::partId(i,"level"),0);p.set(RoomProcessor::partId(i,"brightness"),1);p.set(RoomProcessor::partId(i,"length"),2.5f);p.set(RoomProcessor::partId(i,"density"),1);}
                p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> audio(2,48000*8);auto st=render(p,audio,48000,512);std::cout<<"STRESS voices "<<voices[0]<<","<<voices[1]<<","<<voices[2]<<": peak="<<st.peak<<" callback_fraction="<<st.average<<" p99_ms="<<st.p99<<" worst_ms="<<st.worst<<"\n";check(st.peak<1,"Scene controls can clip in stress case");
            }
            p.quiet();settle(p);p.set("output",-3);p.set("roomDecay",1.25f);p.set("tail",-9);p.play();
            juce::AudioBuffer<float> motion(2,512);juce::MidiBuffer midi;float peak=0;
            for(int k=0;k<160;++k){const float f=(float)k/159;p.set("p0_x",-.7f+1.4f*f);p.set("p1_y",.1f+.55f*f);p.set("roomWidth",6+8*f);p.updateRoom();p.processBlock(motion,midi);for(int c=0;c<2;++c)for(int n=0;n<512;++n){check(std::isfinite(motion.getSample(c,n)),"Moving geometry emitted non-finite audio");peak=std::max(peak,std::abs(motion.getSample(c,n)));}}
            check(peak<1,"Moving geometry clipped");std::cout<<"PASS moving source/room geometry, peak="<<peak<<"\n";
            p.set("output",0);p.set("placement",0);for(int i=0;i<3;++i){p.set(RoomProcessor::partId(i,"x"),0);p.set(RoomProcessor::partId(i,"y"),.1f);p.set(RoomProcessor::partId(i,"z"),1.5f);}
            p.updateRoom();juce::AudioBuffer<float> close(2,48000*8);auto st=render(p,close,48000,512);check(st.peak<1,"Nearest source positions clip the mix");std::cout<<"PASS nearest-source stress peak="<<st.peak<<"\n";
            for(int mode:{1,0,1,0}){p.set("placement",(float)mode);p.updateRoom();juce::AudioBuffer<float> transition(2,48000);check(render(p,transition,48000,127).peak<1,"Placement mode transition clipped");}
            stateChecks(p);placementStateChecks(p);std::cout<<"PASS loaded scene state round trip and live placement-mode changes\n";return 0;
        }
        if(juce::String(argv[1])=="--profile") {
            auto& room=*p.roomForTest();juce::AudioBuffer<float> block(2,512);
            for(const auto* er:{"OFF","Q1","Q2"})for(const auto* lr:{"OFF","Q1","Q2"}) {
                for(int i=0;i<3;++i){room.set(i,"Refl 3D Mode",er);room.set(i,"Rev 3D Mode",lr);}
                for(int n=0;n<30;++n){for(int i=0;i<3;++i){block.clear();room.process(i,block);}juce::Thread::sleep(5);}
                double seconds=0;for(int n=0;n<300;++n){const auto start=std::chrono::steady_clock::now();for(int i=0;i<3;++i){block.clear();block.setSample(0,0,.02f);block.setSample(1,0,.02f);room.process(i,block);}seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
                std::cout<<"PROFILE three Reverside ER="<<er<<" LR="<<lr<<" callback_fraction="<<seconds/(300*512/48000.)<<"\n";
            }
            tide::Engine engine;auto settings=tide::room::patches[13].values;settings.space=0;settings.output=0;engine.setSettings(settings);engine.prepare(48000,512);double seconds=0;
            for(int n=0;n<900;++n){if(n%26==0)engine.midi(juce::MidiMessage::noteOn(1,50,.8f));const auto start=std::chrono::steady_clock::now();engine.render(block.getWritePointer(0),block.getWritePointer(1),512);seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
            std::cout<<"PROFILE one Tide engine callback_fraction="<<seconds/(900*512/48000.)<<"\n";return 0;
        }
        std::cout<<p.status()<<"; reported latency="<<p.getLatencySamples()<<"\n";positionChecks(*p.roomForTest(),out);
        p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> audio(2,48000*66);audio.clear();
        auto stats=render(p,audio,48000,512);check(stats.peak>.005f&&stats.peak<.9f,"Scene silent or lacks headroom");writeWav(out.getChildFile("three-in-a-room.wav"),audio,48000);
        std::cout<<"Scene 48k/512: peak="<<stats.peak<<" avg_callback_fraction="<<stats.average<<" p99_ms="<<stats.p99<<" worst_ms="<<stats.worst<<" energy="<<stats.energy<<"\n";
        p.stop();juce::AudioBuffer<float> tail(2,48000*8);auto tailStats=render(p,tail,48000,511);check(tailStats.energy>0,"Stop cut the tail");check(tail.getMagnitude(0,tail.getNumSamples()-4800,4800)<.0001f,"Stop did not settle");writeWav(out.getChildFile("stop-tail.wav"),tail,48000);
        p.play();settle(p,40);p.quiet();settle(p,4);juce::AudioBuffer<float> quiet(2,4096);render(p,quiet,48000,127);check(quiet.getMagnitude(0,quiet.getNumSamples())==0,"Quiet left audible tail");
        p.play();juce::AudioBuffer<float> resumed(2,48000);check(render(p,resumed,48000,2048).energy>0,"Play did not resume after Quiet");
        for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"mute"),1);settle(p,20);check(render(p,quiet,48000,127).peak==0,"Mute did not silence all sources");for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"mute"),0);
        std::cout<<"PASS Stop tails, Quiet, resume, source mutes, oversized blocks and scene state\n";
        for(double sr:{44100.,96000.}){p.stop();p.prepareToPlay(sr,512);p.updateRoom();settle(p);p.play();juce::AudioBuffer<float> test(2,(int)(sr*6));auto st=render(p,test,sr,512);check(st.peak>.005f&&st.peak<.9f,"Sample rate scene failed");std::cout<<"Scene "<<sr<<"/512: peak="<<st.peak<<" avg_callback_fraction="<<st.average<<" p99_ms="<<st.p99<<" worst_ms="<<st.worst<<"\n";}
        p.releaseResources();std::cout<<"PASS Room rendering and transport checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
