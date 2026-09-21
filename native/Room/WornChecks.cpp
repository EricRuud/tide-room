#include "WornTape.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <chrono>
using namespace tide::room;
namespace {
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void wav(const juce::File& p,const juce::AudioBuffer<float>& b,double sr){juce::WavAudioFormat f;auto stream=p.createOutputStream();require(stream!=nullptr,"WAV stream");stream->setPosition(0);stream->truncate();std::unique_ptr<juce::AudioFormatWriter>w(f.createWriterFor(stream.release(),sr,2,32,{},0));require(w&&w->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"WAV write");}
juce::AudioBuffer<float> render(const juce::AudioBuffer<float>& x,double sr,WornSettings s,int partition=512,int reference=0){
    WornTape tape;tape.setSettings(s);tape.setReferenceOrder(reference);tape.prepare(sr,512);juce::AudioBuffer<float> y;y.makeCopyOf(x);
    const auto start=std::chrono::steady_clock::now();for(int a=0;a<y.getNumSamples();a+=partition){juce::AudioBuffer<float>b(y.getArrayOfWritePointers(),2,a,std::min(partition,y.getNumSamples()-a));tape.process(b);}
    require(tape.guardCount()==0,"Unexpected guard");require(tape.maximumDelayStep()<=.12000001,"Transport velocity bound");
    for(int ch=0;ch<2;++ch)for(int i=0;i<y.getNumSamples();++i)require(std::isfinite(y.getSample(ch,i)),"Nonfinite output");
    std::cout<<"render rate="<<sr<<" partition="<<partition<<" drive="<<s.drive<<" motion="<<s.motion<<" damage="<<s.damage<<" medium="<<s.medium<<" CPU="<<100*std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/(x.getNumSamples()/sr)<<"% peak="<<y.getMagnitude(0,y.getNumSamples())<<" maxDelayStep="<<tape.maximumDelayStep()<<"\n";
    return y;
}
WornSettings preset(int i){return wornPreset(i);}
}
int main(int argc,char**argv){std::cout<<std::unitbuf;try{
    require(argc>=3,"--unit/--render/--alias/--motion OUTPUT [INPUT]");juce::String mode(argv[1]);juce::File folder=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);folder.createDirectory();
    if(mode=="--unit"){
        for(double sr:{44100.,48000.,96000.})for(int medium:{0,1}){
            juce::AudioBuffer<float>x(2,(int)(sr*4));for(int i=0;i<x.getNumSamples();++i){const double t=i/sr;const float v=(float)(.22*std::sin(t*2*juce::MathConstants<double>::pi*199)+.06*std::sin(t*2*juce::MathConstants<double>::pi*6073));x.setSample(0,i,v);x.setSample(1,i,v);}
            WornSettings s;s.drive=30;s.motion=3;s.damage=1;s.medium=medium;s.noise=.3f;auto a=render(x,sr,s,512),b=render(x,sr,s,127);
            for(int c=0;c<2;++c)for(int i=0;i<x.getNumSamples();++i)require(a.getSample(c,i)==b.getSample(c,i),"Partition dependence");
            s.noise=0;s.damage=0;a=render(x,sr,s,17);for(int i=0;i<a.getNumSamples();++i)require(a.getSample(0,i)==a.getSample(1,i),"Unworn mono is not mono");
            x.clear();s.damage=1;a=render(x,sr,s);require(a.getMagnitude(0,a.getNumSamples())==0,"Noise-off silence is not zero");
            std::cout<<"PASS partitions, mono, exact silence, bounds: "<<sr<<" medium "<<medium<<"\n";
        }
        // Abrupt automation remains finite and changes neither callback semantics nor memory safety.
        WornTape t;t.prepare(48000,512);juce::AudioBuffer<float> b(2,512);
        for(int j=0;j<1000;++j){WornSettings s{j%2?30.f:0.f,j%2?1.f:0.f,j%3?3.f:0.f,j%5?1.f:0.f,.1f,0,j%2};s.dips=j%2?2.f:0.f;t.setSettings(s);for(int c=0;c<2;++c)for(int i=0;i<512;++i)b.setSample(c,i,.3f*(float)std::sin((j*512+i)*.07));t.process(b);require(b.getMagnitude(0,512)<2,"Automation spike");}
        require(t.guardCount()==0&&t.maximumDelayStep()<=.12000001,"Automation bounds");std::cout<<"PASS automation stress\n";
    }else if(mode=="--dips"){
        for(int medium:{0,1}){
            juce::AudioBuffer<float>x(2,48000*4);for(int i=0;i<x.getNumSamples();++i)for(int c=0;c<2;++c)x.setSample(c,i,.1f*(float)std::sin(2*juce::MathConstants<double>::pi*100*i/48000.));
            WornSettings s;s.medium=medium;s.damage=1;s.noise=0;s.motion=1.15f;
            double energy[3]{};
            for(int depth=0;depth<=2;++depth){s.dips=(float)depth;auto a=render(x,48000,s,512),b=render(x,48000,s,127);
                for(int c=0;c<2;++c)for(int i=0;i<x.getNumSamples();++i){require(a.getSample(c,i)==b.getSample(c,i),"Dip depth partition dependence");if(i>=48000)energy[depth]+=(double)a.getSample(c,i)*a.getSample(c,i);}
            }
            require(energy[0]>energy[1]*1.05&&energy[1]>energy[2]*1.02,"Dip depth has no ordered audible effect");
            s.damage=0;s.dips=0;auto a=render(x,48000,s);s.dips=2;auto b=render(x,48000,s);
            for(int c=0;c<2;++c)for(int i=0;i<x.getNumSamples();++i)require(a.getSample(c,i)==b.getSample(c,i),"Dip depth changed undamaged sound");
            std::cout<<"PASS independent dip depth, exact partitions and unchanged undamaged sound: medium "<<medium<<" energy "<<energy[0]<<", "<<energy[1]<<", "<<energy[2]<<"\n";
        }
    }else if(mode=="--render"){
        require(argc>=4,"Input WAV required");juce::AudioFormatManager f;f.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>r(f.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3])));require(r!=nullptr,"Input reader");
        juce::AudioBuffer<float>x(2,(int)r->lengthInSamples+1024);x.clear();r->read(&x,0,(int)r->lengthInSamples,0,true,true);
        for(int i=0;i<3;++i){auto y=render(x,r->sampleRate,preset(i));juce::AudioBuffer<float>aligned(y.getArrayOfWritePointers(),2,768,(int)r->lengthInSamples);wav(folder.getChildFile("worn-"+juce::String(i)+".wav"),aligned,r->sampleRate);}
    }else if(mode=="--alias"){
        for(int hz:{997,6073,11003,17003})for(int order:{2,3,4,6}){
            const double sr=48000;juce::AudioBuffer<float>x(2,(int)sr*3);for(int i=0;i<x.getNumSamples();++i)for(int c=0;c<2;++c)x.setSample(c,i,.5f*(float)std::sin(2*juce::MathConstants<double>::pi*hz*i/sr));
            WornSettings s;s.drive=30;s.age=0;s.motion=s.damage=s.noise=s.trim=0;auto y=render(x,sr,s,512,order);wav(folder.getChildFile("alias-"+juce::String(hz)+"-"+juce::String(1<<order)+".wav"),y,sr);
        }
    }else if(mode=="--motion"){
        const double sr=48000;juce::AudioBuffer<float>x(2,(int)sr*8);for(int i=0;i<x.getNumSamples();++i)for(int c=0;c<2;++c)x.setSample(c,i,.05f*(float)std::sin(2*juce::MathConstants<double>::pi*3150*i/sr));
        for(int i=0;i<3;++i){auto s=preset(i);s.noise=0;wav(folder.getChildFile("motion-"+juce::String(i)+".wav"),render(x,sr,s),sr);}
    }else require(false,"Unknown mode");
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
