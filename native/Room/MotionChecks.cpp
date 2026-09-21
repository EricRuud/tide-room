#include "RoomProcessor.h"
#include "RoomEditor.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using tide::room::Motion;
void reflectionChecks(){
    for(double sr:{44100.,48000.,96000.}){
        tide::room::HrtfBank bank;bank.prepare(sr);
        auto render=[&](int block,bool moving,float mirror){
            tide::room::MovingReflections early;early.prepare(sr,bank);const int total=(int)sr*3;
            juce::AudioBuffer<float> result(2,total);std::vector<float> input((size_t)block);
            for(int offset=0;offset<total;offset+=block){const int n=std::min(block,total-offset);const float time=(float)(offset/sr);
                for(int k=0;k<n;++k)input[(size_t)k]=offset+k<(int)sr*2?.1f*(float)std::sin((offset+k)*220*2*juce::MathConstants<double>::pi/sr):0;
                const float x=mirror*(moving?2.5f*std::sin(time*2.3f):1.7f),y=moving?3.f+.8f*std::sin(time*1.7f):3.f;
                early.render(input.data(),result.getWritePointer(0)+offset,result.getWritePointer(1)+offset,n,x,y,-.4f,10.6f,11.1f,5.1f,true,true);
            }return result;
        };
        const auto fixed=render(512,false,1),partitioned=render(127,false,1),mirror=render(512,false,-1),moving=render(512,true,1);
        double difference=0;float maxSecond=0,partitionError=0,symmetryError=0;
        for(int c=0;c<2;++c)for(int n=0;n<fixed.getNumSamples();++n){const float a=fixed.getSample(c,n),b=moving.getSample(c,n);check(std::isfinite(b)&&std::abs(b)<1,"Moving reflections emitted invalid or excessive output");partitionError=std::max(partitionError,std::abs(a-partitioned.getSample(c,n)));symmetryError=std::max(symmetryError,std::abs(a-mirror.getSample(1-c,n)));difference+=(a-b)*(a-b);if(n>(int)(sr*.5)&&n<(int)(sr*1.9))maxSecond=std::max(maxSecond,std::abs(b-2*moving.getSample(c,n-1)+moving.getSample(c,n-2)));}
        check(partitionError<1.e-6f,"Fixed reflection output depends on callback partition");check(symmetryError<2.e-6f,"Reflection ear symmetry failed");check(difference/fixed.getNumSamples()>1.e-5,"Reflection motion did not change sound");check(maxSecond<.002f,"Reflection motion introduced a sharp discontinuity on a smooth tone");check(moving.getMagnitude((int)(sr*2.8),(int)(sr*.1))<1.e-8f,"Reflection history failed to drain");
        std::cout<<"REFLECTIONS sample_rate="<<sr<<" max_second_difference="<<maxSecond<<" partition_error="<<partitionError<<" mirror_error="<<symmetryError<<"\n";
    }
    std::cout<<"PASS moving reflections: signal change, smooth tone, stereo symmetry, partition invariance, 44.1/48/96 kHz, drained tails\n";
}
void unitChecks(){
    const Motion::Values anchors{{{{0,.39f,1.25f}},{{-.2f,.3f,1.1f}},{{.2f,.45f,1.6f}}}};
    Motion::Settings s;for(auto& l:s)l={};s[0]={true,1,0,false,1,0,.5f,0};
    for(double sr:{44100.,48000.,96000.})for(int block:{1,127,512,1024}){
        Motion m;m.prepare(sr);int elapsed=0;while(elapsed<(int)sr){const int n=std::min(block,(int)sr-elapsed);m.process(s,anchors,n,120,true);elapsed+=n;}
        check(std::abs(std::remainder(m.phaseValues()[0],1.))<1.e-10,"Free rate depends on sample rate or callback partition");
        const auto before=m.phaseValues();for(int k=0;k<10;++k)m.process(s,anchors,block,120,false);check(before==m.phaseValues(),"Stop advanced LFO phase");
    }
    Motion m;m.prepare(48000);m.process(s,anchors,12000,120,true);m.process(s,anchors,0,120,true);check(std::abs(m.signalValues()[0]-1)<1.e-6,"Sine quarter cycle");
    m.prepare(48000);s[0].shape=1;m.process(s,anchors,12000,120,true);m.process(s,anchors,0,120,true);check(std::abs(m.signalValues()[0]-1)<1.e-6,"Triangle quarter cycle");
    s[0].sync=true;s[0].division=2;m.prepare(48000);m.process(s,anchors,48000,120,true);check(std::abs(m.phaseValues()[0]-.5)<1.e-12,"Tempo sync period");m.process(s,anchors,48000,60,true);check(std::abs(m.phaseValues()[0]-.75)<1.e-12,"Tempo changes broke phase continuity");m.restart();check(m.phaseValues()[0]==0,"Play restart");
    s[0].sync=false;s[0].shape=0;s[0].phase=90;m.prepare(48000);auto pos=m.process(s,anchors,48000,120,false);check(pos[0][0]>.399f,"Route excursion scale");
    check(pos[1]==anchors[1]&&pos[2]==anchors[2]&&pos[0][1]==anchors[0][1]&&pos[0][2]==anchors[0][2],"Route leaked to another source or axis");
    s[1]=s[0];m.prepare(48000);pos=m.process(s,anchors,48000,120,false);check(pos[0][0]>.799f,"Routes did not sum");
    s[0].enabled=s[1].enabled=false;const auto previous=pos;pos=m.process(s,anchors,512,120,false);check(pos[0][0]<previous[0][0]&&pos[0][0]>.5f,"Disable snapped position");
    for(int n=0;n<200;++n)pos=m.process(s,anchors,512,120,false);check(pos==anchors,"Disabled routes did not return exactly to anchors");
    for(int dest=1;dest<=9;++dest){m.prepare(48000);for(auto& l:s)l={true,dest,0,false,4,0,1,90};for(int k=0;k<400;++k){pos=m.process(s,anchors,127,160,true);for(const auto& source:pos)for(size_t a=0;a<3;++a)check(std::isfinite(source[a])&&source[a]>=tide::room::axisMin[a]&&source[a]<=tide::room::axisMax[a],"Stacked routes escaped position bounds");}}
    std::cout<<"PASS waveforms, free/sync timing, sample rates, partitions, stop/restart, isolation, stacking, smooth disable, bounds\n";
}
void stateChecks(){RoomProcessor p(false);p.set("movingReflections",0);for(int i=0;i<6;++i){p.set(RoomProcessor::lfoId(i,"on"),1);p.set(RoomProcessor::lfoId(i,"target"),(float)(9-i));p.set(RoomProcessor::lfoId(i,"shape"),(float)(i%2));p.set(RoomProcessor::lfoId(i,"sync"),(float)(i%2));p.set(RoomProcessor::lfoId(i,"rate"),.217f+.1f*(float)i);p.set(RoomProcessor::lfoId(i,"division"),(float)i);p.set(RoomProcessor::lfoId(i,"depth"),.37f);p.set(RoomProcessor::lfoId(i,"phase"),145.3f);}
    std::array<std::array<float,8>,6> expected;for(int i=0;i<6;++i)for(size_t k=0;k<8;++k)expected[(size_t)i][k]=p.get(RoomProcessor::lfoId(i,tide::room::lfoFields[k]));
    juce::MemoryBlock data;p.getStateInformation(data);RoomProcessor restored(false);restored.setStateInformation(data.getData(),(int)data.getSize());for(int i=0;i<6;++i)for(size_t k=0;k<8;++k)check(restored.get(RoomProcessor::lfoId(i,tide::room::lfoFields[k]))==expected[(size_t)i][k],"LFO state round trip");
    check(restored.get("movingReflections")==0,"Reflection renderer state round trip");auto old=p.parameters.copyState();old.removeChild(old.getChildWithProperty("id","movingReflections"),nullptr);for(int i=0;i<6;++i)for(const auto* field:tide::room::lfoFields)old.removeChild(old.getChildWithProperty("id",RoomProcessor::lfoId(i,field)),nullptr);
    if(auto xml=old.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,data);restored.setStateInformation(data.getData(),(int)data.getSize());for(int i=0;i<6;++i)check(restored.get(RoomProcessor::lfoId(i,"on"))==0,"Old scene retained enabled modulation");check(!restored.isPlaying(),"Restore started transport");check(restored.get("movingReflections")==1,"Older scene did not migrate to smooth reflections");
    std::cout<<"PASS all 48 LFO controls round trip; pre-LFO scenes migrate with all routes disabled\n";
}
double now(){return juce::Time::getMillisecondCounterHiRes()*.001;}
void wav(const juce::File& f,const juce::AudioBuffer<float>& b){juce::WavAudioFormat fmt;auto stream=f.createOutputStream();check(stream!=nullptr,"WAV stream");std::unique_ptr<juce::AudioFormatWriter> writer(fmt.createWriterFor(stream.release(),48000,2,24,{},0));check(writer&&writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"WAV output");}
void mappingCheck(tide::room::ReversideRoom& room,int i,const tide::room::Position& p,bool headphones){
    const char* names[]={"GeomPan","Distance","Src Z (m)","D/R Balance"};const float physical[]={p.lateral,p.depth,p.height,headphones?-1.f:-.05f-.65f*p.depth};
    for(int a=0;a<4;++a){juce::AudioProcessorParameter* parameter=nullptr;for(auto* candidate:room.instance(i)->getParameters())if(candidate->getName(100)==names[a])parameter=candidate;check(parameter!=nullptr,"Missing position control");const auto expected=parameter->getValueForText(juce::String(physical[a],9));check(std::abs(room.value(i,names[a])-expected)<3.e-6f,"Automation mapping differs from vendor conversion");}
}
void screenshot(RoomProcessor& p,const juce::File& out){std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());juce::PNGImageFormat png;auto stream=out.getChildFile("room.png").createOutputStream();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*stream);auto controls=out.getChildFile("motion.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->motionPanelSnapshot(),*controls);}
void rendererRoutingChecks(){
    // VST3 parameter delivery is deferred until processing. Exercise state and
    // routing changes with intervening callbacks, as the running host does.
    // Queuing all changes and destroying unprocessed Reverside instances exposed
    // a vendor DSP destructor heap error in the first headless harness.
    RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,1024);p.prepareToPlay(48000,1024);check(p.loadRoom(juce::File("/Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3")),"Routing test could not load Reverside");
    auto& room=*p.roomForTest();juce::AudioBuffer<float> audio(2,1024);juce::MidiBuffer midi;auto drain=[&]{for(int k=0;k<8;++k)p.processBlock(audio,midi);};drain();
    for(int i=0;i<3;++i)check(room.value(i,"Mix ER Gain")==0,"Native early field is doubled by Reverside");
    p.set("movingReflections",0);p.updateRoom();drain();
    for(int i=0;i<3;++i)check(room.value(i,"Mix ER Gain")>0,"Fixed Reverside reflection level was not restored");
    room.set(0,"Mix ER Gain","0.72");p.updateRoom();drain();
    const float expected=room.value(0,"Mix ER Gain"),late=room.value(0,"Mix LR Gain");
    p.set("movingReflections",1);p.updateRoom();drain();
    juce::MemoryBlock state;p.getStateInformation(state);p.setStateInformation(state.getData(),(int)state.getSize());drain();
    for(int i=0;i<3;++i)check(room.value(i,"Mix ER Gain")==0,"Restore enabled doubled early field");
    p.set("movingReflections",0);p.updateRoom();drain();
    for(int i=0;i<3;++i)check(std::abs(room.value(i,"Mix ER Gain")-expected)<1.e-6f,"Fixed early level lost across mode/state changes");
    check(std::abs(room.value(0,"Mix LR Gain")-late)<1.e-6f,"Reflection mode changed late-tail level");
    p.set("movingReflections",1);p.set("placement",1);p.updateRoom();drain();
    for(int i=0;i<3;++i)check(room.value(i,"Direct Path 3D Mode")==0&&room.value(i,"D/R Balance")==0,"Moving stereo room contains two direct paths");
    p.set("movingReflections",0);p.updateRoom();drain();
    for(int i=0;i<3;++i)check(room.value(i,"Direct Path 3D Mode")==1,"Legacy fixed stereo direct path not restored");
    std::cout<<"PASS renderer routing: no doubled early/direct paths, fixed ER gain recall, late level retained, mode/state transitions\n";
}
void sceneChecks(const juce::File& out,bool studio=false){
    RoomProcessor p(false);if(studio){p.set("tapeModel",2);p.set("studioQuality",2);p.set("studioNoise",1);}p.setRateAndBufferSizeDetails(48000,1024);p.prepareToPlay(48000,1024);check(p.loadRoom(juce::File("/Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3")),p.status().toRawUTF8());
    auto& room=*p.roomForTest();for(int i=0;i<3;++i)for(int k=0;k<81;++k){const float t=(float)k/80;const tide::room::Position pos{-.8f+1.6f*t,.1f+.58f*t,.3f+1.9f*t};room.position(i,pos,k%2==0);mappingCheck(room,i,pos,k%2==0);}std::cout<<"PASS vendor position mappings: 243 poses / four controls\n";
    p.set("roomWidth",10.6f);p.set("roomDepth",11.1f);p.set("roomHeight",5.1f);p.set("roomDecay",.35f);p.set("tail",-30);p.set("reflections",-6.8f);p.set("evolution",.87f);p.set("spatialDrive",19.8f);p.set("spatialSoftness",96);p.set("spatialTrim",5.8f);p.selectVoice(2,11);p.set("p2_length",1.834f);p.set("p2_brightness",1);for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"level"),0);
    juce::MidiBuffer midi;juce::AudioBuffer<float> warm(2,1024);
    for(int mode=0;mode<4;++mode){const bool enabled=mode!=0,stress=mode==2;const int seconds=stress?12:mode==3?8:24;
        p.stop();p.set("placement",mode==3?1.f:0.f);for(int i=0;i<6;++i){p.set(RoomProcessor::lfoId(i,"on"),enabled?1.f:0.f);p.set(RoomProcessor::lfoId(i,"rate"),stress?4.f:.09f+.021f*(float)i);p.set(RoomProcessor::lfoId(i,"depth"),stress?1.f:.3f);p.set(RoomProcessor::lfoId(i,"shape"),(float)(i%2));}
        p.prepareToPlay(48000,1024);p.updateRoom();for(int k=0;k<50;++k)p.processBlock(warm,midi);p.play();
        juce::AudioBuffer<float> audio(2,48000*seconds);std::vector<double> times;double total=0;int late=0,nextControl=0;float peak=0;std::array<float,3> minX{{1,1,1}},maxX{{-1,-1,-1}};
        for(int offset=0;offset<audio.getNumSamples();offset+=1024){const int n=std::min(1024,audio.getNumSamples()-offset);juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,offset,n);const float heldPosition=room.value(0,"GeomPan");const double start=now();p.processBlock(block,midi);const double duration=now()-start;check(room.value(0,"GeomPan")==heldPosition,"Audio thread wrote Reverside position");total+=duration;times.push_back(duration);if(duration>n/48000.)++late;
            for(int c=0;c<2;++c)for(int k=0;k<n;++k){check(std::isfinite(block.getSample(c,k)),"Non-finite moving room audio");peak=std::max(peak,std::abs(block.getSample(c,k)));}
            for(int i=0;i<3;++i){const auto pos=p.movingPosition(i);minX[(size_t)i]=std::min(minX[(size_t)i],pos.lateral);maxX[(size_t)i]=std::max(maxX[(size_t)i],pos.lateral);}
            if(offset>=nextControl){p.updateRoom();nextControl+=2400;for(int i=0;i<3;++i)mappingCheck(room,i,p.roomSettings().positions[(size_t)i],mode!=3||p.get("movingReflections")>.5f);}
        }
        check(peak>.001f&&peak<1&&p.tapeGuards()==0,"Moving room headroom or solver guards");for(int i=0;i<3;++i)check(enabled?maxX[(size_t)i]-minX[(size_t)i]>.1f:maxX[(size_t)i]==minX[(size_t)i],"Motion state mismatch");
        std::sort(times.begin(),times.end());std::cout<<"SCENE mode="<<mode<<" seconds="<<seconds<<" cpu_fraction="<<total/seconds<<" p99_ms="<<times[(size_t)(times.size()*.99)]*1000<<" worst_ms="<<times.back()*1000<<" late="<<late<<" peak="<<peak<<"\n";
        wav(out.getChildFile(mode==0?"fixed.wav":mode==1?"motion.wav":mode==2?"fast-deep-motion.wav":"original-placement-motion.wav"),audio);if(mode==1)screenshot(p,out);
        if(mode==1){for(int i=0;i<6;++i){p.set(RoomProcessor::lfoId(i,"target"),(float)(i%3+7));p.set(RoomProcessor::lfoId(i,"sync"),1);p.set(RoomProcessor::lfoId(i,"phase"),270);}for(int k=0;k<100;++k){p.processBlock(warm,midi);check(std::isfinite(warm.getMagnitude(0,1024))&&warm.getMagnitude(0,1024)<1,"Live reroute/sync/phase transition");}for(int i=0;i<6;++i){p.set(RoomProcessor::lfoId(i,"target"),(float)(1+(i/2)*3+i%2));p.set(RoomProcessor::lfoId(i,"sync"),0);}}
    }
    p.stop();int tailBlocks=0;for(;tailBlocks<1500&&!p.sleepingForTest();++tailBlocks)p.processBlock(warm,midi);std::cout<<"STOP sleep_after_seconds="<<tailBlocks*1024/48000.<<" final_peak="<<warm.getMagnitude(0,1024)<<"\n";check(p.sleepingForTest(),"Enabled LFOs prevented sleeping after Stop");p.quiet();p.processBlock(warm,midi);p.play();for(int k=0;k<100;++k)p.processBlock(warm,midi);check(!p.sleepingForTest()&&p.peak.load()>.001f,"LFO scene failed to resume");
    std::cout<<"PASS moving render, fixed hosted geometry, no audio-thread position writes, shared-editor isolation, live reroute/sync/phase, original placement, Stop sleep, Quiet and resume\n";
}
#include "StudioIntegrationChecks.h"
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;try{unitChecks();reflectionChecks();stateChecks();check(argc==3,"Use --unit/--scene/--routing NEW_OUTPUT");const auto out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);check(!out.exists(),"Use a new output directory");out.createDirectory();if(juce::String(argv[1])=="--studio")studioIntegrationChecks(out);else if(juce::String(argv[1])=="--studio-scene")sceneChecks(out,true);else if(juce::String(argv[1])=="--scene")sceneChecks(out);else if(juce::String(argv[1])=="--routing")rendererRoutingChecks();else{RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,1024);p.prepareToPlay(48000,1024);screenshot(p,out);}return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
