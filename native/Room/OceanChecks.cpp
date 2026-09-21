#include "RoomProcessor.h"
#include "RoomEditor.h"
#include "ImmersiveView.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace tide::room;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void unitChecks() {
    OceanSettings settings;Ocean::Positions planets{{{{-2,0,1}},{{0,0,1}},{{2,0,1}}}};
    Ocean ocean;ocean.prepare(48000);auto values=ocean.process(planets,settings,48000*20);
    check(values[0]>.1f&&std::abs(values[2])<1.e-7f&&std::abs(values[0]+values[4])<1.e-7f,"Symmetric gravity failed");
    for(int i:{1,3,5})check(values[(size_t)i]==0,"Gravity leaked across axes");
    const auto settled=values;
    settings.mass={0,0,0};ocean.prepare(48000);values=ocean.process(planets,settings,48000*20);
    for(auto v:values)check(v==0,"Zero mass still pulled water");
    settings.mass={1,1,1};const auto original=planets;for(auto& p:planets)p[0]*=2;
    ocean.prepare(48000);values=ocean.process(planets,settings,48000*20);check(values[0]<settled[0],"Distance did not reduce gravity");
    planets=original;std::swap(planets[0],planets[2]);ocean.prepare(48000);values=ocean.process(planets,settings,48000*20);check(values[0]==settled[4]&&values[4]==settled[0],"Planet indexing changed gravity");
    std::swap(planets[0][0],planets[0][1]);std::swap(planets[1][0],planets[1][1]);std::swap(planets[2][0],planets[2][1]);
    ocean.prepare(48000);values=ocean.process(planets,settings,48000*20);check(std::abs(values[1]-settled[4])<1.e-7f,"Rotating space did not rotate CV");
    for(double sr:{44100.,48000.,96000.}) {
        Ocean a,b;a.prepare(sr);b.prepare(sr);const int count=(int)sr*7+113;
        const auto expected=a.process(original,settings,count);
        for(int offset=0;offset<count;offset+=127)b.process(original,settings,std::min(127,count-offset));
        check(expected==b.values(),"Ocean clock depends on callback partition");
        Ocean stress;stress.prepare(sr);OceanSettings extreme{4,1.5f,.15f,{{4,4,4}}};
        for(int n=0;n<3000;++n){auto moving=original;for(size_t i=0;i<3;++i)moving[i]={(float)std::sin(n*.03+i)*.01f,(float)std::cos(n*.04+i)*.01f,1};
            if(n%100==0)moving={};const auto v=stress.process(moving,extreme,511);for(float x:v)check(std::isfinite(x)&&std::abs(x)<=1,"Near collision made invalid CV");}
    }
    ocean.prepare(48000);ocean.process(original,settings,48000*20);settings.gravity=0;
    values=ocean.process(original,settings,200);check(values[0]>.1f,"Ocean had no inertia");
    values=ocean.process(original,settings,48000*40);check(std::abs(values[0])<1.e-6f,"Ocean failed to settle after pull removed");
    std::array<OceanRoute,oceanRoutes> routes{};routes[0]={0,5,.5f};routes[1]={0,5,-.5f};
    auto mod=oceanModulation(settled,routes,true);check(mod==OceanModulation{},"Signed routes did not cancel");
    routes[1].target=0;mod=oceanModulation(settled,routes,true);check(mod[1][0]>0&&mod[0]==std::array<float,4>{}&&mod[2]==std::array<float,4>{},"Cross-planet routing leaked");
    check(oceanModulation(settled,routes,false)==OceanModulation{},"Disabled patches emitted modulation");
    std::cout<<"PASS gravity symmetry, rotation, distance, zero mass, inertia, settling, overlapping planets, CV bounds, signed routing, sample rates and callback partitions\n";
}
void stateChecks() {
    auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.oceanAudition();p.set("oceanMass1",2.37f);p.set("oceanDamping",.84f);
    juce::MemoryBlock data;p.getStateInformation(data);auto restoredProcessor=std::make_unique<RoomProcessor>(false);auto& restored=*restoredProcessor;restored.setStateInformation(data.getData(),(int)data.getSize());
    for(auto* parameter:p.getParameters())if(auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))if(named->paramID.startsWith("ocean")){const float delta=std::abs(restored.get(named->paramID)-p.get(named->paramID));if(delta>0)std::cout<<"STATE "<<named->paramID<<" delta="<<delta<<"\n";check(delta<1.e-6f,"Ocean state round trip failed");}
    auto old=p.parameters.copyState();for(int i=old.getNumChildren();--i>=0;)if(old.getChild(i).getProperty("id").toString().startsWith("ocean"))old.removeChild(i,nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(),data);restored.setStateInformation(data.getData(),(int)data.getSize());
    check(restored.get("oceanOn")==0&&!restored.isPlaying(),"Old scene enabled ocean patches or transport");
    for(int i=0;i<oceanRoutes;++i)check(restored.get(RoomProcessor::oceanId(i,"target"))==0,"Old scene retained an ocean route");
    check(restored.get("oceanMass1")==1&&std::abs(restored.get("oceanDamping")-.38f)<.001f,"Old scene inherited ocean controls");
    std::cout<<"PASS complete ocean state round trip and old-scene migration\n";
}
void saveWav(const juce::File& file,const juce::AudioBuffer<float>& audio,double rate) {
    juce::WavAudioFormat format;auto stream=file.createOutputStream();check(stream!=nullptr,"WAV stream failed");
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,2,24,{},0));
    check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"WAV write failed");
}
juce::AudioBuffer<float> render(double rate,int target,bool enabled,const juce::File& snapshot={}) {
    auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(rate,512);p.set("tapeOn",0);p.set("output",-6);p.set("reflections",-12);p.set("clearTail",-18);
    p.selectVoice(0,14);p.set("p1_mute",1);p.set("p2_mute",1);p.set("lfo0_on",1);p.set("lfo0_rate",.23f);p.set("oceanGravity",2);
    p.set("oceanOn",enabled?1.f:0.f);p.set(RoomProcessor::oceanId(0,"source"),0);p.set(RoomProcessor::oceanId(0,"target"),(float)target);p.set(RoomProcessor::oceanId(0,"amount"),-.9f);
    p.prepareToPlay(rate,512);check(p.ready(),"Ocean integration check needs native-room build");p.play();
    juce::AudioBuffer<float> result(2,(int)rate*4);juce::MidiBuffer midi;
    for(int offset=0;offset<result.getNumSamples();offset+=512){juce::AudioBuffer<float> block(result.getArrayOfWritePointers(),2,offset,std::min(512,result.getNumSamples()-offset));p.processBlock(block,midi);}
    float peak=0;for(int c=0;c<2;++c)for(int n=0;n<result.getNumSamples();++n){const float v=result.getSample(c,n);check(std::isfinite(v),"Invalid audio with ocean patches");peak=std::max(peak,std::abs(v));}
    check(peak>.001f&&peak<1&&p.tapeGuards()==0,"Ocean audio silence, clipping or recovery");
    check(std::abs(p.get("p0_brightness")-patches[14].values.timbre)<1.e-6f,"Modulation overwrote base brightness");
    if(snapshot!=juce::File{}){
        p.oceanAudition();std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());juce::PNGImageFormat png;
        auto room=snapshot.getChildFile("room.png").createOutputStream();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*room);
        auto panel=snapshot.getChildFile("oceans.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->oceanPanelSnapshot(),*panel);
    }
    // Switching off returns to the base sound without changing the patch.
    p.set("oceanOn",0);p.stop();juce::AudioBuffer<float> tail(2,512);
    for(int n=0;n<100;++n)p.processBlock(tail,midi);
    p.releaseResources();return result;
}
double difference(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b) {
    double sum=0;for(int c=0;c<2;++c)for(int n=0;n<a.getNumSamples();++n){const double d=a.getSample(c,n)-b.getSample(c,n);sum+=d*d;}
    return std::sqrt(sum/(2*a.getNumSamples()));
}
void auditionCheck(const juce::File& out) {
    auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(48000,512);p.oceanAudition();p.set("tapeOn",0);p.set("output",-6);
    p.prepareToPlay(48000,512);p.play();juce::MidiBuffer midi;juce::AudioBuffer<float> audio(2,48000*16);
    float minRing=1,maxRing=0,minBrightness=1,maxBrightness=0;
    for(int offset=0;offset<audio.getNumSamples();offset+=512) {
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,offset,std::min(512,audio.getNumSamples()-offset));p.processBlock(block,midi);
        const float ring=std::clamp(patches[14].values.modDepth-p.oceanSignals[4].load(),0.f,1.f);
        const float brightness=std::clamp(patches[6].values.timbre-.8f*p.oceanSignals[0].load(),0.f,1.f);
        if(offset>48000){minRing=std::min(minRing,ring);maxRing=std::max(maxRing,ring);minBrightness=std::min(minBrightness,brightness);maxBrightness=std::max(maxBrightness,brightness);}
        for(int c=0;c<2;++c)for(int n=0;n<block.getNumSamples();++n)check(std::isfinite(block.getSample(c,n))&&std::abs(block.getSample(c,n))<1,"Orbit audition invalid or clipped");
    }
    check(maxRing-minRing>.05f&&maxBrightness-minBrightness>.05f,"Orbit demo pinned a destination at its limit");
    check(audio.getMagnitude(0,audio.getNumSamples())>.001f,"Orbit audition silent");
    std::cout<<"PASS orbit audition: ring_depth="<<minRing<<".."<<maxRing<<" brightness="<<minBrightness<<".."<<maxBrightness<<" peak="<<audio.getMagnitude(0,audio.getNumSamples())<<"\n";
    saveWav(out.getChildFile("orbit-audition.wav"),audio,48000);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());juce::PNGImageFormat png;
    auto room=out.getChildFile("room.png").createOutputStream();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*room);
    auto panel=out.getChildFile("oceans.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->oceanPanelSnapshot(),*panel);
    p.quiet();juce::AudioBuffer<float> quiet(2,512);for(int n=0;n<150;++n)p.processBlock(quiet,midi);check(quiet.getMagnitude(0,512)==0,"Quiet failed with ocean routes");
    p.play();p.processBlock(quiet,midi);check(p.isPlaying(),"Orbit audition failed to resume");p.releaseResources();
}
void collisionChecks(const juce::File& out) {
    Ocean::Signals zero{};std::array<OceanRoute,oceanRoutes> routes{};routes[0]={6,2,.5f};
    auto mod=oceanModulation(zero,routes,true,{1,0,0});check(mod[0][1]==.5f&&mod[1][1]==0,"Impact route targets wrong voice");
    routes[0].amount=-.5f;check(oceanModulation(zero,routes,true,{1,0,0})[0][1]==-.5f,"Impact cannot invert modulation");
    check(oceanModulation(zero,routes,false,{1,1,1})==OceanModulation{},"Disabled impact route leaks");
    auto stateProcessor=std::make_unique<RoomProcessor>(false);auto& state=*stateProcessor;for(int i=0;i<oceanRoutes;++i)state.set(RoomProcessor::oceanId(i,"source"),(float)i);
    juce::MemoryBlock data;state.getStateInformation(data);auto restoredProcessor=std::make_unique<RoomProcessor>(false);auto& restored=*restoredProcessor;restored.setStateInformation(data.getData(),(int)data.getSize());
    for(int i=0;i<oceanRoutes;++i)check(restored.get(RoomProcessor::oceanId(i,"source"))==i,"Existing X/Y source indices changed");
    state.set(RoomProcessor::oceanId(0,"source"),8);state.getStateInformation(data);restored.setStateInformation(data.getData(),(int)data.getSize());
    check(restored.get(RoomProcessor::oceanId(0,"source"))==8,"Impact source does not recall");
    const auto renderCollision=[&](bool patched){auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(48000,512);p.collisionAudition();p.set("oceanOn",patched?1.f:0.f);p.set("tapeOn",0);p.set("output",-9);p.prepareToPlay(48000,512);p.play();
        juce::AudioBuffer<float> audio(2,48000*8);juce::MidiBuffer midi;float impact=0,join=0,minDistance=100;
        for(int offset=0;offset<audio.getNumSamples();offset+=512){juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,offset,std::min(512,audio.getNumSamples()-offset));p.processBlock(block,midi);
            impact=std::max(impact,p.impactSignals[0].load());join=std::max(join,p.waterConnections[0].load());const auto a=p.movingPosition(0),b=p.movingPosition(1);
            const float dx=(a.lateral-b.lateral)*p.get("roomWidth")*.5f,dy=(a.depth-b.depth)*p.get("roomDepth"),dz=a.height-b.height;
            minDistance=std::min(minDistance,std::sqrt(dx*dx+dy*dy+dz*dz));
            for(int channel=0;channel<2;++channel)for(int n=0;n<block.getNumSamples();++n)check(std::isfinite(block.getSample(channel,n))&&std::abs(block.getSample(channel,n))<1,"Impact audio is nonfinite or clipped");}
        check(impact>.05f&&join>.05f&&minDistance>2*PlanetMotion::solidRadius-.001f,"Audio-clocked collision did not bounce, join or emit CV");
        check(std::abs(p.get("p0_brightness")-patches[14].values.timbre)<1.e-6f,"Impact overwrote base parameter");
        p.quiet();juce::AudioBuffer<float> quiet(2,512);for(int i=0;i<150;++i)p.processBlock(quiet,midi);check(quiet.getMagnitude(0,512)==0,"Impact patch did not become quiet");
        p.releaseResources();std::cout<<"COLLISION patched="<<patched<<" impact="<<impact<<" join="<<join<<" min_distance="<<minDistance<<" peak="<<audio.getMagnitude(0,audio.getNumSamples())<<"\n";return audio;};
    const auto dry=renderCollision(false),patched=renderCollision(true);const double delta=difference(dry,patched);check(delta>1.e-5,"Impact did not change sound");
    saveWav(out.getChildFile("collision-unpatched.wav"),dry,48000);saveWav(out.getChildFile("collision-patched.wav"),patched,48000);
    std::cout<<"PASS impact routing/polarity/bypass, old X/Y source recall, new impact recall, collision audio, finite/unclipped output and Quiet; rms_difference="<<delta<<"\n";
}
void noteLightChecks() {
    for(double rate:{44100.,48000.,96000.}) {
        auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;
        p.setRateAndBufferSizeDetails(rate,512);p.set("tapeOn",0);p.prepareToPlay(rate,512);
        tide::room::Pattern reference;reference.reset(rate);PatternSettings settings;
        settings.bpm=p.get("tempo");settings.evolution=p.get("evolution");settings.seed=(uint32_t)p.get("seed");
        settings.harmony={(int)p.get("harmonyKey"),(int)p.get("harmonyScale"),(int)p.get("harmonyChord")};settings.bank=(int)p.get("patternBank");
        for(int i=0;i<3;++i)settings.density[(size_t)i]=p.get(RoomProcessor::partId(i,"density"));
        std::array<std::array<float,128>,3> expectedPitches{};
        std::array<float,3> previous{};std::array<int,3> seen{};juce::MidiBuffer midi;p.play();
        for(int offset=0;offset<(int)rate*4;offset+=512){const int count=std::min(512,(int)rate*4-offset);const auto events=reference.advance(count,settings);
            juce::AudioBuffer<float> block(2,count);p.processBlock(block,midi);std::array<bool,3> on{};
            for(int n=0;n<events.size;++n)if(events.data[(size_t)n].on)on[(size_t)events.data[(size_t)n].part]=true;
            for(size_t voice=0;voice<3;++voice){
                for(int pitch=0;pitch<128;++pitch){float expected=expectedPitches[voice][(size_t)pitch]*std::exp(-(float)count/(float)(rate*.18));
                    for(int n=0;n<events.size;++n){const auto event=events.data[(size_t)n];if(event.on&&event.part==(int)voice&&event.note==pitch)expected=std::sqrt(event.velocity)*std::exp(-(float)(count-event.offset)/(float)(rate*.18));}
                    if(expected<.0001f)expected=0;expectedPitches[voice][(size_t)pitch]=expected;
                    check(std::abs(p.pitchLights[voice][(size_t)pitch].load()-expected)<.000003f,"A plate lit for the wrong pitch or at the wrong time");}
                const float light=p.noteLights[voice].load();check(std::isfinite(light)&&light>=0&&light<=1,"Invalid note light");
                if(on[voice]){check(light>.4f,"Played note did not light its planet");++seen[voice];}
                else check(light<=previous[voice]+1.e-6f,"Planet lit without a note-on");previous[voice]=light;}
        }
        for(int count:seen)check(count>2,"Did not exercise repeated notes on every planet");
        p.set("p1_mute",1);juce::AudioBuffer<float> block(2,512);for(int i=0;i<100;++i){p.processBlock(block,midi);check(p.noteLights[1].load()==0,"Muted tower lit up");for(auto& light:p.pitchLights[1])check(light.load()==0,"Muted plate retained its light");}
        p.stop();for(int i=0;i<(int)(rate/512*2);++i)p.processBlock(block,midi);for(auto& light:p.noteLights)check(light.load()==0,"Note glow did not settle after Stop");for(auto& voice:p.pitchLights)for(auto& light:voice)check(light.load()==0,"Plate did not fade after Stop");
        p.play();p.processBlock(block,midi);check(p.noteLights[0].load()>.4f,"Note glow did not resume");
        p.quiet();p.processBlock(block,midi);for(auto& light:p.noteLights)check(light.load()==0,"Quiet left a note glow");for(auto& voice:p.pitchLights)for(auto& light:voice)check(light.load()==0,"Quiet left a plate lit");
        p.releaseResources();std::cout<<"PASS exact pitch-specific plate lighting, repeated notes, rests, per-voice mute, Stop/Play and Quiet at "<<rate<<" Hz\n";
    }
}
void plateAudioChecks(){
    for(double rate:{44100.,48000.,96000.}){
        tide::Engine reference,separated;auto settings=tide::patches[10].values;settings.space=0;settings.output=0;
        reference.setSettings(settings);separated.setSettings(settings);reference.prepare(rate,512,false);separated.prepare(rate,512,false,true);
        juce::AudioBuffer<float> expected(2,512),stems(8,512);std::array<float*,8> outputs{};for(int v=0;v<8;++v)outputs[(size_t)v]=stems.getWritePointer(v);
        float error=0,peak=0;
        for(int block=0;block<500;++block){
            if(block%13==0){auto note=juce::MidiMessage::noteOn(1,48+(block/13)%18,.7f);reference.midi(note);separated.midi(note);}
            if(block%17==0){auto note=juce::MidiMessage::noteOff(1,48+(block/17)%18);reference.midi(note);separated.midi(note);}
            if(block==470){reference.panic();separated.panic();}
            const int count=block%3==0?127:512;reference.render(expected.getWritePointer(0),expected.getWritePointer(1),count);separated.renderVoices(outputs,count);
            for(int n=0;n<count;++n){float sum=0;for(int v=0;v<8;++v)sum+=stems.getSample(v,n);error=std::max(error,std::abs(sum-expected.getSample(0,n)));peak=std::max(peak,std::abs(sum));}
        }
        check(peak>.01f&&error<.00001f,"Separated notes changed synthesizer sound or timing");
        auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.set("tapeOn",0);p.set("evolution",1);p.set("roomHeight",4.7f);p.set("tempo",160);
        for(int i=0;i<3;++i)p.set(RoomProcessor::partId(i,"density"),1);
        p.prepareToPlay(rate,512);p.play();juce::MidiBuffer midi;juce::AudioBuffer<float> audio(2,512);double totalMs=0;int mapped=0,overlap=0;
        for(int block=0;block<(int)(rate*5/512);++block){const auto started=juce::Time::getMillisecondCounterHiRes();p.processBlock(audio,midi);totalMs+=juce::Time::getMillisecondCounterHiRes()-started;
            for(int c=0;c<2;++c)for(int n=0;n<512;++n)check(std::isfinite(audio.getSample(c,n))&&std::abs(audio.getSample(c,n))<1,"Per-plate spatial audio is nonfinite or clipped");
            for(int i=0;i<3;++i){const auto notes=p.towerNotes(i);int ringing=0;
                for(int plate=0;plate<notes.count;++plate){const int pitch=notes.pitches[(size_t)plate];if(p.pitchLights[(size_t)i][(size_t)pitch].load()<.05f)continue;
                    const float elevation=p.pitchHeights[(size_t)i][(size_t)pitch].load();check(std::abs(elevation-notes.worldElevation(plate,4.7f))<.00001f,"A played note is spatialized at the wrong plate height");++mapped;++ringing;}
                if(ringing>1)++overlap;
            }
        }
        check(mapped>100&&overlap>0,"Height test did not exercise overlapping notes");p.releaseResources();
        std::cout<<"PLATE AUDIO rate="<<rate<<" summed_stem_error="<<error<<" mapped="<<mapped<<" overlapping="<<overlap<<" realtime_cpu_ratio="<<totalMs/5000<<"\n";
    }
    std::cout<<"PASS independent polyphonic stems preserve tone, timing, note-off, voice steals and panic; every sounding pitch matches its rendered plate elevation\n";
}
void presetAudioCheck(const juce::File& out,const juce::File& preset){
    auto xml=juce::XmlDocument::parse(preset);check(xml!=nullptr,"Cannot read audition preset");
    auto p=std::make_unique<RoomProcessor>(false);juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(*xml,state);p->setStateInformation(state.getData(),(int)state.getSize());
    constexpr double rate=48000;constexpr int block=1024;
    p->prepareToPlay(rate,block);p->play();juce::AudioBuffer<float> rendered(2,(int)rate*12);juce::MidiBuffer midi;std::vector<double> times;float peak=0;
    for(int offset=0;offset<rendered.getNumSamples();offset+=block){const int count=std::min(block,rendered.getNumSamples()-offset);juce::AudioBuffer<float> audio(rendered.getArrayOfWritePointers(),2,offset,count);
        const double start=juce::Time::getMillisecondCounterHiRes();p->processBlock(audio,midi);const double ms=juce::Time::getMillisecondCounterHiRes()-start;
        if(offset>(int)rate&&count==block)times.push_back(ms);
        for(int c=0;c<2;++c)for(int n=0;n<count;++n){check(std::isfinite(audio.getSample(c,n)),"Preset generated nonfinite audio");peak=std::max(peak,std::abs(audio.getSample(c,n)));}
    }
    const double budget=block/rate*1000;double total=0;int late=0;for(double t:times){total+=t;if(t>budget)++late;}std::sort(times.begin(),times.end());
    std::cout<<"PRESET rate="<<rate<<" block="<<block<<" cpu_ratio="<<total/times.size()/budget<<" p99_ms="<<times[(size_t)(times.size()*.99)]<<" worst_ms="<<times.back()<<" over_budget="<<late<<" peak="<<peak<<"\n";
    check(total/times.size()<budget*.85,"Preset has insufficient real-time CPU headroom");check(peak>.001f&&peak<=1,"Preset is silent or clipped");saveWav(out.getChildFile("scene.wav"),rendered,rate);
    RoomEditor editor(*p);editor.advanceOceanPreview(.3);{juce::PNGImageFormat png;auto stream=out.getChildFile("room.png").createOutputStream();check(stream&&png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()),*stream),"Cannot save preset preview");}
    p->releaseResources();std::cout<<"PASS existing preset with per-note elevation, tape, room tails and finite output\n";
}
void glassDragChecks() {
    for(bool lit:{false,true}){
    auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);
    RoomEditor editor(p);const auto view=editor.roomView();const auto position=p.movingPosition(0);const auto target=view.project(view.position(position.lateral,position.depth,position.height));
    NoteLens::Lights lights{};if(lit){p.noteLights[0].store(1);lights[0]={target,view.radius(tide::room::PlanetMotion::waterRadius,view.position(position.lateral,position.depth,position.height)),1,PlanetRenderer::noteTint(0)};editor.createComponentSnapshot(editor.getLocalBounds());}
    auto visible=target;for(int i=0;i<6;++i)visible+=target-GlassCaseRenderer::trace(view,visible).sample;
    const auto event=[&](juce::Point<float> point,bool shift=false){const auto now=juce::Time::getCurrentTime();return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(shift?juce::ModifierKeys::shiftModifier:0)),1,0,0,0,0,&editor,&editor,now,visible,now,1,false);};
    const float x=p.get("p0_x"),y=p.get("p0_y"),z=p.get("p0_z");
    editor.mouseDown(event(visible));editor.mouseDrag(event(visible));editor.mouseUp(event(visible));
    check(std::abs(p.get("p0_x")-x)<.0011f&&std::abs(p.get("p0_y")-y)<.0011f&&p.get("p0_z")==z,"Clicking a refracted planet jumped its anchor");
    editor.mouseDown(event(visible));editor.mouseDrag(event(visible+juce::Point<float>{10,-10}));editor.mouseUp(event(visible));
    std::cout<<"DRAG lit="<<lit<<" from="<<x<<","<<y<<","<<z<<" to="<<p.get("p0_x")<<","<<p.get("p0_y")<<","<<p.get("p0_z")<<"\n";
    check(p.get("p0_x")>x+.001f&&p.get("p0_y")>y+.015f&&p.get("p0_z")==z,"Drag through glass did not move the intended axes");
    const auto projected=view.project(view.position(p.get("p0_x"),p.get("p0_y"),p.towerHeight()));
    const auto expected=view.project(view.position(x,y,p.towerHeight()))+GlassCaseRenderer::trace(view,visible+juce::Point<float>{10,-10}).sample-GlassCaseRenderer::trace(view,visible).sample;
    check(projected.getDistanceFrom(expected)<.3f,"Tower does not follow the pointer on its horizontal plane");
    editor.mouseDown(event(visible,true));editor.mouseDrag(event(visible+juce::Point<float>{0,-15},true));editor.mouseUp(event(visible,true));
    check(p.get("p0_y")>y+.02f,"Shift-drag through glass did not change depth");
    const float depth=p.get("p0_y");juce::MouseWheelDetails wheel;wheel.deltaY=.2f;editor.mouseWheelMove(event(visible),wheel);
    check(p.get("p0_y")>depth+.02f,"Wheel did not hit the refracted planet");p.releaseResources();
    }
    std::cout<<"PASS resting and illuminated picking, click without jump, floor-plane dragging, Shift floor-plane dragging and wheel depth\n";
}
void glassChecks() {
    using Vec=SphericalWater::Vec;
    for(int degree=0;degree<89;++degree){const float a=(float)degree*.01745329252f;const Vec incoming{std::sin(a),0,-std::cos(a)};Vec ray;
        check(tide::optics::refract(incoming,{0,0,1},1.f/1.333f,ray),"Air-to-water ray unexpectedly reflected internally");
        check(std::abs(ray.length()-1)<1.e-5f&&std::abs(ray.x*1.333f-incoming.x)<1.e-5f,"Water refraction violates Snell's law");
        Vec reversed;check(tide::optics::refract(ray*-1,{0,0,-1},1.333f,reversed)&&(reversed+incoming).length()<1.e-4f,"Water ray is not reciprocal");
    }
    Vec trapped;check(!tide::optics::refract({.9396926f,0,-.3420201f},{0,0,1},1.333f,trapped),"Total internal reflection missing");
    check(std::abs(tide::optics::fresnel(1,1,1.333f)-.0203732f)<1.e-5f,"Water normal-incidence reflectance is wrong");
    for(int i=0;i<=2000;++i){const float depth=(float)i*.001f;check(std::abs(tide::optics::absorption(depth)-std::exp(-depth))<.000043f,"Clear-water absorption approximation exceeded error bound");}
    ListenerSpace view;
    for(float width:{4.f,10.f,18.f})for(float depth:{4.f,8.f,18.f})for(float height:{2.5f,4.f,8.f})for(float z:{.3f,1.1f,1.5f,2.2f})for(float x:{-.8f,0.f,.8f})for(float y:{.1f,.4f,.68f}){
        view.width=width;view.depth=depth;view.height=height;const auto p=view.position(x,y,z);const auto q=view.unproject(view.project(p),p.y);
        check((q-p).length()<1.e-5f,"Listener-view drag does not invert projection");
        Vec right,up,back;view.basis(p,right,up,back);
        check(std::abs(right.dot(up))<1.e-5f&&std::abs(up.dot(back))<1.e-5f&&std::abs(back.length()-1)<1.e-5f,"Listener camera basis is not orthonormal");
    }
    check(view.project(view.target()).getDistanceFrom(view.centre)<.001f,"Camera target is not centred");
    for(float width:{4.f,10.f,18.f})for(float depth:{4.f,8.f,18.f})for(float height:{2.5f,4.f,8.f}){
        view.width=width;view.depth=depth;view.height=height;
        for(float x:{-width*.5f,width*.5f})for(float y:{0.f,depth})for(float z:{0.f,height})
            check(view.bounds.contains(view.project({x,y,z})),"Wide camera cropped the glass case");
    }
    check(view.radius(.28f,{0,2,1.5f})>view.radius(.28f,{0,4,1.5f}),"Perspective does not scale with distance");
    for(float width:{4.f,10.f,18.f})for(float depth:{4.f,8.f,18.f})for(float height:{2.5f,4.f,8.f}){
        view.width=width;view.depth=depth;view.height=height;
        for(int x=260;x<950;x+=47)for(int y=250;y<620;y+=37){const juce::Point<float> screen{(float)x,(float)y};
            const auto pass=GlassCaseRenderer::trace(view,screen,1.f),zero=GlassCaseRenderer::trace(view,screen,1.517f,0),glass=GlassCaseRenderer::trace(view,screen);
            check(pass.sample.getDistanceFrom(screen)<.001f&&zero.sample.getDistanceFrom(screen)<.001f,"Index-matched or zero-thickness glass bends rays");
            check(std::isfinite(glass.sample.x)&&std::isfinite(glass.sample.y)&&glass.reflection>=0&&glass.reflection<=1,"Nonfinite pane refraction or invalid Fresnel weight");
            check(glass.sample.getDistanceFrom(screen)<45,"Unbounded pane refraction");
        }
    }
    for(bool immersive:{false,true}){view=ListenerSpace{};view.fromListener=immersive;const auto mesh=GlassFrame::mesh(view);
        check(mesh.size()==(immersive?80:120),"Glass frame is missing beveled solid edges");
        for(const auto& face:mesh){if(immersive)check(!face.foreground,"Rear glass can cover nearer planets in the listener view");check(std::abs(face.normal.length()-1)<1.e-5f,"Glass facet normal is invalid");
            for(const auto point:face.vertices){const auto projected=view.project(point);check(view.cameraDepth(point)>0&&std::isfinite(projected.x)&&std::isfinite(projected.y),"Glass geometry crosses the camera plane");}}
    }
    view=ListenerSpace{};const auto front=view.project({0,0,1.5f});const auto red=GlassCaseRenderer::trace(view,front,1.509f),blue=GlassCaseRenderer::trace(view,front,1.526f);
    check(red.sample.getDistanceFrom(front)>.05f&&red.sample.getDistanceFrom(blue.sample)>.001f,"Glass has no refraction or dispersion");
    std::cout<<"PASS finite-slab refraction, zero-thickness/index-matched identity, wavelength dispersion and bounded rays across room sizes\n";
    std::cout<<"PASS Snell refraction, water Fresnel, internal reflection, optical reciprocity, wide perspective, whole-case framing and drag across room sizes\n";

}
void lensChecks(const juce::File& out) {
    ListenerSpace view;view.fromListener=true;view.bounds={0,0,640,360};view.centre=view.bounds.getCentre();
    const float horizontalFov=2*std::atan(view.bounds.getWidth()*.5f/view.focal())*180/juce::MathConstants<float>::pi;
    check(horizontalFov>128&&horizontalFov<140,"Immersive lens did not widen");
    const auto left=view.project({-2.75f,1.76f,1.1f}),right=view.project({2.75f,1.76f,1.1f});
    check(view.bounds.reduced(20).contains(left)&&view.bounds.reduced(20).contains(right),"Wider listener view still crops nearby lateral sources");
    GlassCaseRenderer lens;NoteLens::Lights lights{};lights[0]={{140,180},28,0,{1,1,1}};
    const auto shot=[&](float strength){lights[0].strength=strength;auto& canvas=lens.begin(view);
        {juce::Graphics g(canvas);g.fillAll(juce::Colours::black);lens.drawBackEdges(g);g.addTransform(lens.transform(view));g.setColour(juce::Colours::white);g.fillRect(128,100,4,160);g.fillRect(318,100,4,160);}
        juce::Image result(juce::Image::ARGB,640,360,true);{juce::Graphics g(result);lens.draw(g,view,lights);}return result;};
    const auto resting=shot(0),peak=shot(1),decay=shot(.25f),settled=shot(0);
    for(int y=0;y<360;++y)for(int x=0;x<640;++x){
        check(resting.getPixelAt(x,y)==settled.getPixelAt(x,y),"Note glare left a persistent trail");
        if(x>280)check(resting.getPixelAt(x,y)==peak.getPixelAt(x,y),"Note changed the distant room or glass");
    }
    const auto spill=resting.getPixelAt(328,180);check(spill.getRed()+spill.getGreen()+spill.getBlue()<=6&&resting.getPixelAt(320,180).getRed()>210,"Resting scene is still too blurred");
    // Glare can brighten a marker but cannot move or split its channels.
    for(int y=100;y<260;++y)for(int x=110;x<155;++x){const auto a=resting.getPixelAt(x,y),b=peak.getPixelAt(x,y);
        check(std::abs(((int)b.getRed()-a.getRed())-((int)b.getBlue()-a.getBlue()))<4,"A note still separates scene colour channels");}
    for(float radius:{1.f,28.f,2000.f}){for(auto& light:lights)light={{320,180},radius,1,{1,1,1}};const NoteLens overlapping(lights);
        for(int x=-3000;x<4000;x+=91)for(int y=-3000;y<4000;y+=127){const auto point=juce::Point<float>{(float)x,(float)y};const auto e=overlapping.at(point),sliced=overlapping.inRows((float)y,(float)y).at(point);
            check((e-sliced).length()<1.e-6f&&std::isfinite(e.x)&&e.x<=90.01f,"Note glare is unbounded or row-dependent");}}
    // Exercise the actual planet colour compositor with a neutral globe, so
    // chromatic fringes can be measured independently of its changing light.
    juce::Image globe(juce::Image::ARGB,384,384,true);{juce::Graphics g(globe);g.setColour(juce::Colours::white);g.fillEllipse(78,78,228,228);}
    const auto globeShot=[&](float strength,int identity){juce::Image result;PlanetColour::render(globe,result,strength,identity);return result;};
    const auto noSplit=globeShot(0,0),strongSplit=globeShot(1,0),softSplit=globeShot(.25f,0),returned=globeShot(0,0);
    const auto chroma=[](const juce::Image& image){double total=0;juce::Image::BitmapData data(image,juce::Image::BitmapData::readOnly);
        for(int y=0;y<image.getHeight();++y){const auto* row=reinterpret_cast<const juce::PixelARGB*>(data.getLinePointer(y));for(int x=0;x<image.getWidth();++x)total+=std::max({row[x].getRed(),row[x].getGreen(),row[x].getBlue()})-std::min({row[x].getRed(),row[x].getGreen(),row[x].getBlue()});}return total/(image.getWidth()*image.getHeight());};
    const auto offSplit=chroma(noSplit),onSplit=chroma(strongSplit),fadingSplit=chroma(softSplit);
    check(offSplit<.01&&onSplit>12&&fadingSplit>1&&fadingSplit<onSplit*.5,"Planet separation does not strongly follow the note envelope");
    for(int identity=0;identity<3;++identity){const auto split=globeShot(1,identity);for(int y=0;y<split.getHeight();++y)for(int x=0;x<split.getWidth();++x){
        check(noSplit.getPixelAt(x,y)==returned.getPixelAt(x,y),"Planet colour left a persistent trail");
        if(x<24||y<24||x>=split.getWidth()-24||y>=split.getHeight()-24)check(split.getPixelAt(x,y).getAlpha()==0,"Planet colours escaped their local texture");}}
    juce::PNGImageFormat png;juce::Image comparison(juce::Image::RGB,1280,720,true);{juce::Graphics g(comparison);g.drawImageAt(resting,0,0);g.drawImageAt(peak,640,0);g.drawImageAt(decay,0,360);g.drawImageAt(settled,640,360);}
    auto lensFile=out.getChildFile("stable-scene.png").createOutputStream();png.writeImageToStream(comparison,*lensFile);
    juce::Image colours(juce::Image::RGB,noSplit.getWidth()*3,noSplit.getHeight(),true);{juce::Graphics g(colours);g.fillAll(tide::glass::background);g.drawImageAt(noSplit,0,0);g.drawImageAt(strongSplit,noSplit.getWidth(),0);g.drawImageAt(softSplit,noSplit.getWidth()*2,0);}
    auto colourFile=out.getChildFile("planet-colours.png").createOutputStream();png.writeImageToStream(colours,*colourFile);
    PlanetRenderer renderer;juce::Image detail(juce::Image::RGB,1440,480,true);juce::Graphics g(detail);g.fillAll(tide::glass::background);
    std::array<juce::Image,3> phases;
    for(int frame=0;frame<3;++frame){renderer.advancePreview(0,0,0,frame==0?0:.6);phases[(size_t)frame]=juce::Image(juce::Image::RGB,480,480,true);juce::Graphics phase(phases[(size_t)frame]);phase.fillAll(tide::glass::background);renderer.draw(phase,{240,225},150,0,0,0,42+frame,.12f);g.drawImageAt(phases[(size_t)frame],480*frame,0);g.setColour(tide::glass::text);g.setFont(juce::FontOptions(15.f));g.drawText(juce::String((float)frame*.6f,1)+" s",480*frame,440,480,25,juce::Justification::centred);}
    double waterChange=0;for(int y=80;y<370;++y)for(int x=80;x<400;++x){const auto a=phases[0].getPixelAt(x,y),b=phases[1].getPixelAt(x,y);waterChange+=std::abs((int)a.getRed()-b.getRed())+std::abs((int)a.getGreen()-b.getGreen())+std::abs((int)a.getBlue()-b.getBlue());}
    check(waterChange/(290*320*3)>2,"Still-water surface does not visibly evolve");
    auto waterFile=out.getChildFile("water-detail.png").createOutputStream();png.writeImageToStream(detail,*waterFile);
    juce::Image tides(juce::Image::ARGB,960,320,true);juce::Graphics tg(tides);tg.fillAll(tide::glass::background);
    std::array<float,3> massCentre{},width{};
    for(int side=0;side<3;++side){const float pull=(float)(side-1)*.55f;renderer.preview(0,pull,0,8);
        juce::Image water(juce::Image::ARGB,320,320,true);{juce::Graphics wg(water);renderer.draw(wg,{160,160},82,0,pull,0,100);}
        double mass=0,moment=0;int leftEdge=320,rightEdge=0;
        for(int y=0;y<320;++y)for(int x=0;x<320;++x){const auto alpha=water.getPixelAt(x,y).getAlpha();if(alpha>200){mass+=1;moment+=x;leftEdge=std::min(leftEdge,x);rightEdge=std::max(rightEdge,x);}
            if(x==0||y==0||x==319||y==319)check(alpha==0,"Deformed liquid was clipped by its texture");}
        massCentre[(size_t)side]=(float)(moment/mass);width[(size_t)side]=(float)(rightEdge-leftEdge);tg.drawImageAt(water,320*side,0);
    }
    check(massCentre[0]<massCentre[1]-14&&massCentre[2]>massCentre[1]+14,"Gravity does not visibly shift the liquid in both directions");
    check(width[0]>width[1]*1.13f&&width[2]>width[1]*1.13f,"Gravity does not stretch the water silhouette");
    auto tideFile=out.getChildFile("gravity-detail.png").createOutputStream();png.writeImageToStream(tides,*tideFile);
    std::cout<<"PASS gravity-directed water shift, elongation and unclipped silhouettes. centres="<<massCentre[0]<<","<<massCentre[1]<<","<<massCentre[2]<<" widths="<<width[0]<<","<<width[1]<<","<<width[2]<<"\n";
    std::cout<<"PASS wide listener coverage; planet-only note colour separation, stationary room and glass, fading glare, sharp resting image and no trails; animated water at zero tide. FOV="<<horizontalFov<<" chroma_off="<<offSplit<<" chroma_peak="<<onSplit<<" chroma_decay="<<fadingSplit<<" water_delta="<<waterChange/(290*320*3)<<"\n";
}
void immersiveChecks(const juce::File& out) {
    ListenerSpace view;view.fromListener=true;
    check((view.eye()-view.listener()).length()<1.e-7f,"Immersive camera is not at the acoustic listener");
    check((view.forward()-SphericalWater::Vec{0,1,0}).length()<1.e-7f,"Immersive camera does not face the acoustic front");
    for(auto size:std::array<juce::Point<int>,4>{{{1280,800},{1920,1080},{3840,2160},{800,1200}}}){
        view.bounds={0,0,(float)size.x,(float)size.y};view.centre=view.bounds.getCentre();
        for(float width:{4.f,10.f,18.f})for(float depth:{4.f,8.f,18.f})for(float height:{2.5f,4.f,8.f}){
            view.width=width;view.depth=depth;view.height=height;
            check(std::abs(view.listener().z-height*tide::room::towerHeightRatio-1.10f)<1.e-6f,"Listener is not just above the stacks");
            check(view.listener().z<height&&view.listener().z>0,"Listener is outside the case");
            for(float x:{-.8f,0.f,.8f})for(float y:{.1f,.4f,.68f}){
                const auto p=view.position(x,y,1.1f);check((view.unproject(view.project(p),p.y)-p).length()<1.e-5f,"Immersive projection does not invert");
            }
            check(view.project({0,2,view.listener().z}).getDistanceFrom(view.centre)<.001f,"Acoustic front is not at screen centre");
            const auto screen=view.centre+juce::Point<float>{100,50};
            check(GlassCaseRenderer::trace(view,screen).sample.getDistanceFrom(screen)<.001f,"Inside camera incorrectly refracts through the front pane");
        }
        GlassCaseRenderer glass;auto& image=glass.begin(view);
        check(image.getWidth()*image.getHeight()<4005000,"Fullscreen optical buffer exceeds pixel budget");
    }
    auto processor=std::make_unique<RoomProcessor>(false),reference=std::make_unique<RoomProcessor>(false);
    for(auto* p:{processor.get(),reference.get()}){p->setRateAndBufferSizeDetails(48000,512);p->prepareToPlay(48000,512);p->oceanAudition();}
    auto& p=*processor;auto editor=std::make_unique<RoomEditor>(p);
    const auto state=p.parameters.copyState().toXmlString();
    // Exercise the actual entry control, native fullscreen, Escape and button.
    juce::Button* enter=nullptr;for(auto* child:editor->getChildren())if(auto* b=dynamic_cast<juce::Button*>(child))if(b->getButtonText()=="Fullscreen immersive")enter=b;
    check(enter!=nullptr,"Fullscreen entry control missing");enter->onClick();
    check(editor->isImmersiveViewOpen()&&!p.isPlaying(),"Entering fullscreen changed stopped transport");
    auto* immersive=dynamic_cast<ImmersiveView*>(editor->immersiveComponent());check(immersive!=nullptr,"Immersive scene missing");
    auto* fullscreen=juce::Desktop::getInstance().getKioskModeComponent();
    check(fullscreen==immersive->getTopLevelComponent(),"Immersive window did not enter native fullscreen");
    const auto* display=juce::Desktop::getInstance().getDisplays().getDisplayForRect(fullscreen->getScreenBounds());
    check(display&&fullscreen->getScreenBounds()==display->totalArea,"Immersive window does not fill its display");
    check(immersive->getScreenBounds()==fullscreen->getScreenBounds(),"Fullscreen scene has a window border");
    check(immersive->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Escape not handled");
    check(!editor->isImmersiveViewOpen()&&juce::Desktop::getInstance().getKioskModeComponent()==nullptr,"Escape failed to restore desktop");
    p.play();reference->play();juce::MidiBuffer midi;std::vector<double> timings;juce::PNGImageFormat png;
    GlassLook previewLook;ImmersiveView preview(p,[]{});preview.setLookAndFeel(&previewLook);preview.setSize(1280,800);preview.createComponentSnapshot(preview.getLocalBounds());
    for(int frame=0;frame<50;++frame){
        if(frame==8)enter->onClick();
        if(frame==24){juce::Button* exit=nullptr;for(auto* child:immersive->getChildren())if(auto* b=dynamic_cast<juce::Button*>(child))if(b->getName()=="Exit fullscreen")exit=b;check(exit!=nullptr,"Fullscreen exit control missing");exit->onClick();check(!editor->isImmersiveViewOpen(),"Exit button did not leave fullscreen");}
        if(frame==36)enter->onClick();
        for(int offset=0;offset<1920;offset+=512){const int count=std::min(512,1920-offset);juce::AudioBuffer<float> a(2,count),b(2,count);p.processBlock(a,midi);reference->processBlock(b,midi);check(difference(a,b)<1.e-12,"View switching changed or restarted the audio");}
        const auto start=juce::Time::getMillisecondCounterHiRes();preview.advancePreview(.04);auto shot=preview.createComponentSnapshot(preview.getLocalBounds());timings.push_back(juce::Time::getMillisecondCounterHiRes()-start);
        if(frame==0||frame==30){auto file=out.getChildFile(frame==0?"immersive-notes.png":"immersive.png").createOutputStream();png.writeImageToStream(shot,*file);}
    }
    check(p.isPlaying()&&p.parameters.copyState().toXmlString()==state,"Fullscreen altered transport or patch parameters");
    editor.reset();check(juce::Desktop::getInstance().getKioskModeComponent()==nullptr,"Closing the editor left the desktop in fullscreen");
    preview.setSize(1920,1080);preview.advancePreview(.04);auto wide=out.getChildFile("immersive-1080p.png").createOutputStream();png.writeImageToStream(preview.createComponentSnapshot(preview.getLocalBounds()),*wide);
    std::sort(timings.begin(),timings.end());double total=0;for(auto t:timings)total+=t;
    std::cout<<"IMMERSIVE 1280x800 mean_ms="<<total/timings.size()<<" p95_ms="<<timings[47]<<" max_ms="<<timings.back()<<"\n";
    p.releaseResources();reference->releaseResources();
    std::cout<<"PASS listener camera, all room sizes, aspect ratios and 4K render budget; fullscreen entry, Escape, exit button, repeat entry and destructor cleanup; audio identical through view switches\n";
}
void visualPreview(const juce::File& out) {
    glassChecks();auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);p.oceanAudition();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto shot=editor->createComponentSnapshot(editor->getLocalBounds()); // Warm texture caches.
    std::vector<double> timings;p.play();juce::MidiBuffer midi;
    for(int frame=0;frame<75;++frame) {
        for(int offset=0;offset<1920;offset+=512){juce::AudioBuffer<float> audio(2,std::min(512,1920-offset));p.processBlock(audio,midi);}
        const auto start=juce::Time::getMillisecondCounterHiRes();
        static_cast<RoomEditor*>(editor.get())->advanceOceanPreview(.04);
        shot=editor->createComponentSnapshot(editor->getLocalBounds());
        timings.push_back(juce::Time::getMillisecondCounterHiRes()-start);
        if(frame==0){juce::PNGImageFormat png;auto on=out.getChildFile("notes-on.png").createOutputStream();png.writeImageToStream(shot,*on);}
    }
    std::sort(timings.begin(),timings.end());double total=0;for(auto value:timings)total+=value;
    std::cout<<"VISUAL full-editor snapshot mean_ms="<<total/timings.size()<<" p95_ms="<<timings[71]<<" max_ms="<<timings.back()<<" frame_budget_ms=40\n";
    juce::PNGImageFormat png;auto room=out.getChildFile("room.png").createOutputStream();png.writeImageToStream(shot,*room);
    auto retina=out.getChildFile("room-2x.png").createOutputStream();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(),true,2.f),*retina);
    auto controls=out.getChildFile("oceans.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->oceanPanelSnapshot(),*controls);
    juce::Image comparisons(juce::Image::RGB,900,590,true);juce::Graphics g(comparisons);g.fillAll(juce::Colour(0xff0b1016));PlanetRenderer renderer;
    const float displacement[]={-.45f,0,.45f};
    for(int row=0;row<3;++row)for(int planet=0;planet<3;++planet){renderer.preview(planet,displacement[row],.15f,4);renderer.draw(g,{150.f+300.f*planet,100.f+190.f*row},69,planet,displacement[row],.15f,42);
        g.setColour(juce::Colour(0xffd7e2e8));g.setFont(juce::FontOptions(14.f));g.drawText("Planet "+juce::String(planet+1)+" / Tide X "+juce::String(displacement[row],1),planet*300,176+row*190,300,22,juce::Justification::centred);}
    auto contact=out.getChildFile("tide-surfaces.png").createOutputStream();png.writeImageToStream(comparisons,*contact);
    auto tape=out.getChildFile("tape.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->tapePanelSnapshot(),*tape);
    auto motion=out.getChildFile("motion.png").createOutputStream();png.writeImageToStream(static_cast<RoomEditor*>(editor.get())->motionPanelSnapshot(),*motion);
    p.quiet();juce::AudioBuffer<float> silence(2,512);p.processBlock(silence,midi);static_cast<RoomEditor*>(editor.get())->advanceOceanPreview(0);
    auto off=out.getChildFile("notes-off.png").createOutputStream();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),*off);
    p.releaseResources();
}
void motionPreview(const juce::File& out,bool collision=false,bool immersive=false) {
    auto processor=std::make_unique<RoomProcessor>(false);auto& p=*processor;p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);if(collision)p.collisionAudition();else p.oceanAudition();p.play();
    RoomEditor editor(p);GlassLook look;ImmersiveView listenerView(p,[]{});listenerView.setLookAndFeel(&look);listenerView.setSize(1280,800);juce::PNGImageFormat png;juce::MidiBuffer midi;juce::AudioBuffer<float> soundtrack(2,240*1920);
    for(int frame=0;frame<240;++frame){
        for(int offset=0;offset<1920;offset+=512){juce::AudioBuffer<float> audio(soundtrack.getArrayOfWritePointers(),2,frame*1920+offset,std::min(512,1920-offset));p.processBlock(audio,midi);}
        editor.advanceOceanPreview(.04);
        juce::Image detail;
        if(immersive){listenerView.advancePreview(.04);detail=listenerView.createComponentSnapshot(listenerView.getLocalBounds());}
        else {const auto shot=editor.createComponentSnapshot(editor.getLocalBounds(),true,2.f);detail=shot.getClippedImage({476,464,1448,788});}
        auto file=out.getChildFile("frame-"+juce::String(frame).paddedLeft('0',4)+".png").createOutputStream();png.writeImageToStream(detail,*file);
    }
    saveWav(out.getChildFile("soundtrack.wav"),soundtrack,48000);
    p.releaseResources();std::cout<<"PASS 240 wide-view frames driven by audio-clocked notes, motion, collisions and water\n";

}

}
int main(int argc,char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;
    try {
        check(argc==2||argc==3||argc==4,"Usage: TideOceanCheck NEW_OUTPUT_DIRECTORY [--audition|--visuals|--motion|--collisions|--collision-motion|--note-lights|--glass-controls|--immersive|--immersive-motion|--lens]");const juce::File out(argv[1]);check(!out.exists(),"Use a new output directory");out.createDirectory();
        if(argc==4&&juce::String(argv[2])=="--preset-audio"){presetAudioCheck(out,juce::File(argv[3]));return 0;}
        if(argc==3&&juce::String(argv[2])=="--collisions"){unitChecks();stateChecks();collisionChecks(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--audition"){stateChecks();auditionCheck(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--collision-motion"){motionPreview(out,true);return 0;}
        if(argc==3&&juce::String(argv[2])=="--immersive-motion"){motionPreview(out,false,true);return 0;}
        if(argc==3&&juce::String(argv[2])=="--motion"){unitChecks();motionPreview(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--visuals"){visualPreview(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--plate-audio"){plateAudioChecks();return 0;}
        if(argc==3&&juce::String(argv[2])=="--note-lights"){noteLightChecks();return 0;}
        if(argc==3&&juce::String(argv[2])=="--lens"){lensChecks(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--immersive"){immersiveChecks(out);return 0;}
        if(argc==3&&juce::String(argv[2])=="--glass-controls"){glassDragChecks();return 0;}
        unitChecks();stateChecks();
        for(double rate:{44100.,48000.,96000.}) {
            const auto reference=render(rate,0,false),empty=render(rate,0,true),bypass=render(rate,2,false);
            check(difference(reference,empty)==0&&difference(reference,bypass)==0,"Empty or disabled ocean routes changed sound");
            for(int target=1;target<=4;++target){const auto patched=render(rate,target,true,rate==48000&&target==2?out:juce::File{});const double delta=difference(reference,patched);
                check(delta>1.e-5,"Ocean destination did not change audio");std::cout<<"AUDIO rate="<<rate<<" target="<<target<<" rms_difference="<<delta<<"\n";
                if(rate==48000)saveWav(out.getChildFile("ocean-target-"+juce::String(target)+".wav"),patched,rate);
            }
            if(rate==48000)saveWav(out.getChildFile("unpatched.wav"),reference,rate);
        }
        std::cout<<"PASS all four destinations change audio; off and unpatched are bit-identical; 44.1/48/96 kHz; finite and unclipped renders\n";
        auditionCheck(out);
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
