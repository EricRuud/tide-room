#include "RoomEditor.h"
#include "ImmersiveView.h"
#include "VisualEffectsPanel.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>

namespace {
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void save(const juce::Image& image,const juce::File& out,const juce::String& name){juce::PNGImageFormat png;auto stream=out.getChildFile(name+".png").createOutputStream();check(stream&&png.writeImageToStream(image,*stream),"PNG write failed");}
struct Difference {double mean=0,largeFraction=0;int maximum=0;};
Difference difference(const juce::Image& a,const juce::Image& b){
    check(a.getBounds()==b.getBounds(),"Image size differs");juce::Image::BitmapData aa(a,juce::Image::BitmapData::readOnly),bb(b,juce::Image::BitmapData::readOnly);Difference d;uint64_t count=0,large=0;
    for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth()*4;++x){int e=std::abs((int)aa.getLinePointer(y)[x]-(int)bb.getLinePointer(y)[x]);d.mean+=e;d.maximum=std::max(d.maximum,e);++count;if(e>8)++large;}
    d.mean/=(double)count;d.largeFraction=(double)large/(double)count;return d;
}
void report(const char* label,const Difference& d){std::cout<<label<<" mean_byte_error="<<d.mean<<" max="<<d.maximum<<" fraction_over_8="<<d.largeFraction<<"\n";}
void backend(bool metal){if(metal)unsetenv("TIDE_DISABLE_METAL");else setenv("TIDE_DISABLE_METAL","1",1);}
juce::Image planet(bool metal,int identity,float light,bool listener){
    backend(metal);PlanetRenderer renderer;ListenerSpace view;view.fromListener=listener;auto position=view.position(.22f,.44f,1.5f);
    renderer.setContext(identity,position,.42f,view);renderer.preview(identity,.045f,-.035f,.8);
    juce::Image image(juce::Image::ARGB,768,768,true);juce::Graphics g(image);renderer.draw(g,{384,384},768.f/3.36f,identity,.045f,-.035f,3.25,light,false,.02f);return image;
}
juce::Image glass(bool metal,bool listener,const tide::gpu::Effects& effects={}){
    backend(metal);ListenerSpace view;view.bounds={0,0,560,340};view.centre=view.bounds.getCentre();view.fromListener=listener;
    GlassCaseRenderer renderer;auto& source=renderer.begin(view);{
        juce::Graphics g(source);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff141d29),0,0,juce::Colour(0xff9a5f36),(float)source.getWidth(),(float)source.getHeight(),false));g.fillAll();
        g.setColour(juce::Colour(0xffbedad3));for(int i=0;i<12;++i)g.fillEllipse((float)(i*83),float((i*67)%source.getHeight()),43,53);
    }
    NoteLens::Lights lights{};lights[0]={{230,200},27,.7f,{1,.88f,.67f}};
    juce::Image image(juce::Image::ARGB,560,340,true);juce::Graphics g(image);renderer.draw(g,view,lights,effects);return image;
}
void towerChecks(const juce::File& out){
    auto scene=std::make_unique<RoomProcessor>(false);scene->set("evolution",1);
    tide::gpu::TowerPass pass;
    for(int identity=0;identity<3;++identity)for(bool inside:{false,true}){
        ListenerSpace view;view.fromListener=inside;const auto position=view.position(.1f,.4f,scene->towerHeight());
        const auto notes=scene->towerNotes(identity);check(notes.count>1,"Tower has no musical plates");int active=notes.count/2;
        scene->pitchLights[(size_t)identity][(size_t)notes.pitches[(size_t)active]].store(.85f);
        auto p=TowerRenderer::parameters(*scene,identity,position,view,.8f,{.5f,-.3f,0});p.settings.x=384;
        juce::Image cpu,gpu,quiet;TowerRenderer::renderCPU(p,cpu);check(pass.render(p,gpu),"Tower GPU pass failed");
        auto d=difference(cpu,gpu);report("TOWER",d);check(d.mean<.5&&d.largeFraction<.004,"Tower CPU/Metal optics differ");
        auto dark=p;dark.plates[active].w=0;check(pass.render(dark,quiet),"Tower resting GPU pass failed");check(difference(quiet,gpu).mean>.2,"Played plate did not visibly light");
        int changed=0,unlit=0,liquidOnly=0;
        for(int y=0;y<160;++y)for(int x=0;x<160;++x){float u=((float)x+.5f-80)*tide::tower::imageSpan/160,v=(80-(float)y-.5f)*tide::tower::imageSpan/160;
            const auto a=tide::tower::sample(p,u,v),b=tide::tower::sample(dark,u,v);
            check(a.alpha==b.alpha&&a.plate==b.plate,"A note changed physical glass geometry");
            check(b.emission.length()==0,"Unlit glass emits light");
            if(a.plate<0){if(a.alpha>0)++liquidOnly;continue;}
            const float delta=(a.colour-b.colour).length();if(delta>.1f)++changed;else ++unlit;
        }
        check(changed>5&&unlit>50&&liquidOnly>30,"Missing lit plate, dark glass or shared water envelope");
        auto moved=dark;moved.flow={-.8f,.7f,0,0};moved.settings.w+=.8f;juce::Image flowing;check(pass.render(moved,flowing),"Water animation render failed");
        check(difference(quiet,flowing).mean>.1,"Water flow has no visible effect");
        save(gpu,out,"tower-"+juce::String(identity)+(inside?"-listener":"-overview"));save(quiet,out,"tower-"+juce::String(identity)+(inside?"-listener-rest":"-overview-rest"));
        scene->set(RoomProcessor::partId(identity,"mute"),1);auto muted=TowerRenderer::parameters(*scene,identity,position,view,.8f,{.5f,-.3f,0});muted.settings.x=384;
        juce::Image off;check(pass.render(muted,off)&&difference(off,quiet).maximum==0,"Muted plate still lights");scene->set(RoomProcessor::partId(identity,"mute"),0);
        scene->pitchLights[(size_t)identity][(size_t)notes.pitches[(size_t)active]].store(0);
    }
    for(int bank=0;bank<8;++bank)for(int key:{0,4,11})for(int minor:{0,1})for(int chord:{0,3,6}){
        tide::room::PatternSettings settings;settings.bank=bank;settings.evolution=1;settings.harmony={key,minor,chord};settings.density={1,1,1};
        std::array<tide::room::TowerNotes,3> notes;for(int i=0;i<3;++i){notes[(size_t)i]=tide::room::TowerNotes::forPattern(settings,i);check(notes[(size_t)i].count<=16,"Tower exceeded GPU plate capacity");}
        tide::room::Pattern pattern;pattern.reset(48000);
        for(int block=0;block<240;++block){auto events=pattern.advance(4096,settings);for(int e=0;e<events.size;++e){auto note=events.data[(size_t)e];if(!note.on)continue;const auto& tower=notes[(size_t)note.part];check(std::find(tower.pitches.begin(),tower.pitches.begin()+tower.count,note.note)!=tower.pitches.begin()+tower.count,"Played pitch has no plate (including octave evolution)");}}
    }
    std::cout<<"PASS tower ray tracing CPU/Metal parity; isolated per-pitch illumination, mute, both cameras and all eight patterns including octave variants\n";
}
void parity(const juce::File& out){
    for(int i=0;i<3;++i)for(float light:{0.f,.8f}){auto cpu=planet(false,i,light,i==2),gpu=planet(true,i,light,i==2);const auto d=difference(cpu,gpu);report("PLANET",d);check(d.mean<.35&&d.largeFraction<.002,"Planet shader differs from reference");if(light>0){save(cpu,out,"planet-cpu-"+juce::String(i));save(gpu,out,"planet-metal-"+juce::String(i));}}
    for(bool listener:{false,true}){auto cpu=glass(false,listener),gpu=glass(true,listener);const auto d=difference(cpu,gpu);report("GLASS",d);check(d.mean<1.2&&d.largeFraction<.01,"Glass shader differs from reference");save(cpu,out,listener?"inside-cpu":"glass-cpu");save(gpu,out,listener?"inside-metal":"glass-metal");}
    auto clean=glass(true,false);tide::gpu::Effects fx;fx.controls={0,1,1,6};fx.animation={1.3f,.2f,1,.7f};
    for(int i=0;i<5;++i){fx.layers={};fx.controls.x=0;
        if(i==0)fx.layers.x=.7f;if(i==1)fx.layers.y=.7f;if(i==2)fx.layers.z=.7f;if(i==3)fx.layers.w=.7f;if(i==4)fx.controls.x=.7f;
        auto active=glass(true,false,fx);const auto d=difference(clean,active);report("EFFECT",d);check(d.mean>.15,"Effect layer has no visible effect");save(active,out,"effect-"+juce::String(i));
        fx.controls.y=0;check(difference(clean,glass(true,false,fx)).maximum==0,"Zero master mix changed the room");fx.controls.y=1;
        fx.animation.w=0;auto quietLayer=glass(true,false,fx);fx.animation.w=1;check(difference(quietLayer,glass(true,false,fx)).mean>.01,"Layer does not respond to audio");fx.animation.w=.7f;
    }
    fx.layers={.7f,.7f,.2f,.4f};fx.controls.x=.25f;fx.animation.w=0;auto quiet=glass(true,false,fx);fx.animation.w=1;auto loud=glass(true,false,fx);
    check(difference(quiet,loud).mean>.4,"Audio energy did not change effects");save(loud,out,"effects-combined");
    fx.animation.z=0;auto noResponse=glass(true,false,fx);fx.animation.w=0;check(difference(noResponse,glass(true,false,fx)).maximum==0,"Zero audio response still reacts");
    // Check picking against GPU-transformed image features, independently of
    // the shader's implementation (nearest pixel allows small sampling error).
    const auto inside=glass(true,true);fx.layers={};fx.controls={.7f,1,1,6};fx.animation={1.3f,.2f,1,.7f};
    const auto flowed=glass(true,true,fx);double mappedError=0;int samples=0;
    for(int y=10;y<330;y+=11)for(int x=10;x<550;x+=11){const auto q=tide::visual::sourcePoint({(float)x+.5f,(float)y+.5f},{0,0,560,340},fx);
        const auto a=flowed.getPixelAt(x,y),b=inside.getPixelAt(juce::jlimit(0,559,(int)q.x),juce::jlimit(0,339,(int)q.y));
        mappedError+=std::abs((int)a.getRed()-b.getRed())+std::abs((int)a.getGreen()-b.getGreen())+std::abs((int)a.getBlue()-b.getBlue());samples+=3;}
    check(mappedError/samples<2,"Picking does not track GPU flow");std::cout<<"PICKING mean_byte_error="<<mappedError/samples<<"\n";
    // A GPU-prepared case can fall back after device availability changes.
    GlassCaseRenderer renderer;ListenerSpace view;auto& source=renderer.begin(view);source.clear(source.getBounds(),juce::Colour(0xff123456));backend(false);
    juce::Image fallback(juce::Image::ARGB,1200,900,true);juce::Graphics g(fallback);renderer.draw(g,view);check(fallback.getPixelAt(600,400).getAlpha()>0,"Runtime CPU fallback is blank");backend(true);
    std::cout<<"PASS GPU/CPU appearance, independent layers, exact bypass, audio response and runtime fallback\n";
}
void stateAndAudio(const juce::File& out){
    auto ownedP=std::make_unique<RoomProcessor>(false);auto& p=*ownedP;for(const auto& c:tide::visual::controls)p.set(c.id,c.low+(c.high-c.low)*.7f);p.set("visualEnabled",0);
    juce::MemoryBlock data;p.getStateInformation(data);auto ownedRestored=std::make_unique<RoomProcessor>(false);auto& restored=*ownedRestored;restored.setStateInformation(data.getData(),(int)data.getSize());
    for(const auto& c:tide::visual::controls)check(std::abs(restored.get(c.id)-p.get(c.id))<.0001f,"Visual state was not restored");check(restored.get("visualEnabled")==0,"Visual bypass was not restored");
    auto old=p.parameters.copyState();for(const auto& c:tide::visual::controls)old.removeChild(old.getChildWithProperty("id",c.id),nullptr);old.removeChild(old.getChildWithProperty("id","visualEnabled"),nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(),data);restored.setStateInformation(data.getData(),(int)data.getSize());
    for(const auto& c:tide::visual::controls)check(std::abs(restored.get(c.id)-c.initial)<.0001f,"Legacy scene inherited effects");check(restored.get("visualEnabled")==1,"Legacy scene bypass default incorrect");
    auto ownedA=std::make_unique<RoomProcessor>(false),ownedB=std::make_unique<RoomProcessor>(false);auto& a=*ownedA;auto& b=*ownedB;a.set("tapeOn",0);b.set("tapeOn",0);a.prepareToPlay(48000,512);b.prepareToPlay(48000,512);a.play();b.play();juce::MidiBuffer midi;juce::AudioBuffer<float> aa(2,512),bb(2,512);float error=0,peak=0;
    for(int k=0;k<160;++k){for(const auto& c:tide::visual::controls)b.set(c.id,c.low+(c.high-c.low)*(float)(k%10)/9);a.processBlock(aa,midi);b.processBlock(bb,midi);
        for(int ch=0;ch<2;++ch)for(int n=0;n<512;++n){error=std::max(error,std::abs(aa.getSample(ch,n)-bb.getSample(ch,n)));peak=std::max(peak,std::abs(aa.getSample(ch,n)));}}
    check(peak>.001f&&error==0,"Visual controls changed audio");a.releaseResources();b.releaseResources();
    GlassLook look;VisualEffectsPanel panel(restored);panel.setLookAndFeel(&look);save(panel.createComponentSnapshot(panel.getLocalBounds()),out,"controls");
    for(auto* child:panel.getChildren())if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()=="Filaments")button->onClick();
    check(restored.get("visualFractal")>.6f,"Visual preset failed");
    for(auto* child:panel.getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getName()=="Mesh")slider->setValue(.82,juce::sendNotificationSync);
    check(std::abs(restored.get("visualMesh")-.82f)<.001f,"Live slider failed to update parameter");
    for(auto* child:panel.getChildren())if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()=="Clean")button->onClick();
    check(restored.get("visualFractal")==0&&restored.get("visualMesh")==0,"Clean preset failed");panel.setLookAndFeel(nullptr);
    std::cout<<"PASS save/restore, legacy defaults, presets, live slider and unchanged audio (max error="<<error<<")\n";
}
juce::Image sceneImage(RoomProcessor& p){RoomSceneRenderer renderer;ListenerSpace view;juce::Image shot(juce::Image::ARGB,1200,900,true);renderer.advancePreview(p,view,.3);juce::Graphics g(shot);renderer.draw(g,p,view);return shot;}
void prismOnly(){auto p=std::make_unique<RoomProcessor>(false);p->set("visualPrism",.7f);p->noteLights[0].store(.8f);const auto before=sceneImage(*p);
    for(const auto* id:{"visualFractal","visualMesh","visualKaleidoscope","visualFlow"})p->set(id,1);p->set("visualScale",3.2f);p->set("visualHue",.8f);p->set("visualDetail",11);
    check(difference(before,sceneImage(*p)).maximum==0,"Old preset restored a removed camera layer");p->set("visualPrism",0);check(difference(before,sceneImage(*p)).mean>.02,"Prism no longer affects the scene");std::cout<<"PASS prism retained; removed layers and their tuning cannot alter the scene\n";
}
void preview(const juce::File& out){
    RoomProcessor p(false);p.set("tapeOn",0);p.prepareToPlay(48000,1024);p.play();juce::AudioBuffer<float> audio(2,1024);juce::MidiBuffer midi;
    for(int i=0;i<60;++i)p.processBlock(audio,midi);
    RoomEditor editor(p);editor.advanceOceanPreview(.3);save(editor.createComponentSnapshot(editor.getLocalBounds()),out,"room-clean");
    p.set("visualFractal",.65f);p.set("visualMesh",.45f);p.set("visualPrism",.2f);p.set("visualFlow",.15f);p.set("visualScale",1.4f);p.set("visualHue",.58f);
    editor.advanceOceanPreview(.04);save(editor.createComponentSnapshot(editor.getLocalBounds()),out,"room-effects");
    ImmersiveView immersive(p,[]{});immersive.setSize(1280,800);std::vector<double> timings;
    for(int i=0;i<36;++i){p.processBlock(audio,midi);immersive.advancePreview(.04);const double start=juce::Time::getMillisecondCounterHiRes();auto frame=immersive.createComponentSnapshot(immersive.getLocalBounds());const auto duration=juce::Time::getMillisecondCounterHiRes()-start;if(i>=6)timings.push_back(duration);if(i==30)save(frame,out,"immersive-effects");}
    double total=0;for(auto t:timings)total+=t;std::sort(timings.begin(),timings.end());std::cout<<"EFFECTS 1280x800 mean_ms="<<total/timings.size()<<" p95_ms="<<timings[28]<<" max_ms="<<timings.back()<<"\n";
    editor.openImmersiveView();auto* live=editor.immersiveComponent();check(live!=nullptr,"Fullscreen view missing");
    juce::Button* effectsButton=nullptr;for(auto* child:live->getChildren())if(auto* button=dynamic_cast<juce::Button*>(child))if(button->getButtonText()=="Visual effects...")effectsButton=button;
    check(effectsButton==nullptr,"Removed visual-effects menu is present in fullscreen");save(live->createComponentSnapshot(live->getLocalBounds()),out,"fullscreen-controls");
    editor.closeImmersiveView();check(juce::Desktop::getInstance().getKioskModeComponent()==nullptr,"Fullscreen did not exit");
    p.releaseResources();std::cout<<"PASS fullscreen without effect menu, exit and cleanup\n";
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;try{
    check(argc==2,"Usage: TideGpuCheck NEW_OUTPUT_DIRECTORY");juce::File out(argv[1]);check(!out.exists(),"Use a new output directory");out.createDirectory();backend(true);
    std::cout<<tide::gpu::description()<<"\n";check(tide::gpu::available(),"No Metal device: this test must exercise real GPU kernels");
    towerChecks(out);parity(out);stateAndAudio(out);prismOnly();preview(out);const auto stats=tide::gpu::statistics();
    std::cout<<"METAL planets="<<stats.planets<<" glass="<<stats.glasses<<" effects="<<stats.effects<<" failures="<<stats.failures<<"\n";
    check(stats.planets>0&&stats.glasses>0&&stats.effects>0&&stats.failures==0,"GPU pass failed or silently used fallback");return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
