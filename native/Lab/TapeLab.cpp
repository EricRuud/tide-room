#include "HybridTape.h"
#include "CreamTape.h"
#include "../Room/Tape.h"
#include "airwindows/ToTape9.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
using Buffer=juce::AudioBuffer<float>;
void finite(const Buffer& b){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)require(std::isfinite(b.getSample(c,i)),"Non-finite audio");}
void write(const juce::File& path,const Buffer& b,double rate){
    finite(b);require(b.getMagnitude(0,b.getNumSamples())<1,"WAV would clip");
    auto stream=path.createOutputStream();require(stream!=nullptr,"Cannot open WAV");juce::WavAudioFormat f;
    std::unique_ptr<juce::AudioFormatWriter> w(f.createWriterFor(stream.release(),rate,2,32,{},0));
    require(w&&w->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"Cannot write WAV");
}
const char* names[]={"dry","tide","airwindows","hybrid"};
struct Result {Buffer audio;double cpu=0,work=0,p99=0,worst=0,field=0;int latency=0;uint64_t guards=0;};
Result processCream(const Buffer& input,double rate,int variant,int block=1024){
    tide::lab::CreamTape tape;tape.prepare(rate,512,tide::lab::creamVoicings[(size_t)variant]);
    Result r;r.latency=tape.latency();r.audio.setSize(2,input.getNumSamples()+512);r.audio.clear();
    for(int c=0;c<2;++c)r.audio.copyFrom(c,0,input,c,0,input.getNumSamples());
    for(int offset=0;offset<r.audio.getNumSamples();offset+=block){
        Buffer b(r.audio.getArrayOfWritePointers(),2,offset,std::min(block,r.audio.getNumSamples()-offset));tape.process(b);
    }
    finite(r.audio);r.guards=tape.guardCount();r.field=tape.maximumField();
    require(r.guards==0,"Cream tape used numerical recovery");require(r.field<12,"Cream tape reached the magnetic field safety clamp");
    return r;
}
Result process(const Buffer& input,double rate,int mode,float drive,int block=1024,bool headBump=true,int order=5,tide::lab::BrightnessSettings brightness={}){
    tide::room::Tape tape;tide::room::TapeSettings t;t.enabled=true;t.drive=drive;tape.setSettings(t);
    tide::lab::HybridTape hybrid;tide::lab::TapeSettings h;h.enabled=true;h.drive=drive;h.soften=1;h.brightness=brightness;hybrid.setSettings(h);
    std::unique_ptr<ToTape9> aw;Result result;
    if(mode==1){tape.prepare(rate,512,order);result.latency=tape.latency();}
    if(mode==3){hybrid.prepare(rate,512,order);result.latency=hybrid.latency();}
    if(mode==2){std::srand(4129);aw=std::make_unique<ToTape9>(nullptr);aw->setSampleRate(rate);
        aw->setParameter(kParamA,(float)(.5*std::sqrt(std::pow(10.,drive/20.))));
        aw->setParameter(kParamD,0);aw->setParameter(kParamG,headBump?.5f:0.f);
        result.latency=1; // The original ClipOnly3 loop returns previous input at these rates.
    }
    result.audio.setSize(2,input.getNumSamples()+512);result.audio.clear();
    for(int c=0;c<2;++c)result.audio.copyFrom(c,0,input,c,0,input.getNumSamples());
    juce::AudioBuffer<double> doubles(2,block);std::vector<double> times;
    const auto cpuStart=std::clock();
    for(int offset=0;offset<result.audio.getNumSamples();offset+=block){const int n=std::min(block,result.audio.getNumSamples()-offset);
        Buffer b(result.audio.getArrayOfWritePointers(),2,offset,n);
        auto begin=std::chrono::steady_clock::now();
        if(mode==1)tape.process(b);else if(mode==3)hybrid.process(b);
        else if(mode==2){for(int c=0;c<2;++c)for(int i=0;i<n;++i)doubles.setSample(c,i,b.getSample(c,i));
            double* channels[]={doubles.getWritePointer(0),doubles.getWritePointer(1)};
            aw->processDoubleReplacing(channels,channels,n);
            const double trim=std::pow(10.,-drive/20.);
            for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,(float)(trim*doubles.getSample(c,i)));
        }
        times.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count());
    }
    result.work=((double)(std::clock()-cpuStart)/CLOCKS_PER_SEC)/(result.audio.getNumSamples()/rate);
    result.guards=mode==1?tape.guardCount():mode==3?hybrid.guardCount():0;
    result.field=mode==3?hybrid.maximumField():0;
    finite(result.audio);require(result.guards==0,"Magnetic model used numerical recovery");
    double total=0;for(double seconds:times)total+=seconds;
    std::sort(times.begin(),times.end());result.cpu=total/(result.audio.getNumSamples()/rate);
    result.p99=times[(size_t)(.99*(times.size()-1))]*1000;result.worst=times.back()*1000;
    return result;
}
const char* dynamicNames[]={"original","responsive","deep"};
const std::array<tide::lab::BrightnessSettings,3> dynamicSettings{{
    {4500,.12,.0005},{1800,.30,.0005},{1200,.45,.0005}
}};
Result processDynamic(const Buffer& input,double rate,int mode,int block=1024){
    // Same +30 dB drive, tape controls, 64x quality, and topology in all cases.
    // Only the brightness cutoff and detector threshold change.
    const float gain=std::pow(10.f,18.f/20.f);Buffer source;source.makeCopyOf(input);source.applyGain(gain);
    auto r=process(source,rate,3,12,block,true,6,dynamicSettings[(size_t)mode]);
    r.audio.applyGain(1.f/gain);require(r.field<12,"Dynamic hybrid reached the magnetic field safety clamp");return r;
}
void renderDynamic(const juce::File& file,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader&&reader->numChannels==2,"Expected a stereo source WAV");
    Buffer source(2,(int)reader->lengthInSamples);reader->read(&source,0,source.getNumSamples(),0,true,true);
    for(int mode=0;mode<3;++mode){auto r=processDynamic(source,reader->sampleRate,mode);
        Buffer aligned(r.audio.getArrayOfWritePointers(),2,r.latency,source.getNumSamples());
        const auto& s=dynamicSettings[(size_t)mode];write(out.getChildFile(juce::String(dynamicNames[mode])+".wav"),aligned,reader->sampleRate);
        std::cout<<"DYNAMIC "<<dynamicNames[mode]<<" frequency="<<s.frequency<<" threshold="<<s.threshold<<" response_seconds="<<s.responseSeconds<<" peak="<<aligned.getMagnitude(0,aligned.getNumSamples())<<" field="<<r.field<<" guards="<<r.guards<<" latency="<<r.latency<<"\n";
    }
}
void benchmark(const juce::File& file,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader&&reader->numChannels==2,"Expected a stereo WAV");const int size=std::min((int)reader->lengthInSamples,(int)(reader->sampleRate*6));
    Buffer input(2,size);reader->read(&input,0,size,0,true,true);juce::Array<juce::var> rows;
    for(int round=0;round<3;++round)for(float drive:{6.f,12.f})for(int i=0;i<3;++i){const int mode=round%2?3-i:1+i;
        auto r=process(input,reader->sampleRate,mode,drive);juce::DynamicObject::Ptr row=new juce::DynamicObject;
        row->setProperty("round",round);row->setProperty("model",names[mode]);row->setProperty("drive_db",drive);
        row->setProperty("wall_fraction",r.cpu);row->setProperty("process_cpu_fraction",r.work);row->setProperty("p99_ms",r.p99);row->setProperty("worst_ms",r.worst);rows.add(juce::var(row.get()));
        std::cout<<"BENCH round="<<round<<" "<<names[mode]<<" drive="<<drive<<" wall="<<r.cpu<<" process_cpu="<<r.work<<"\n";
    }
    out.getChildFile("timing.json").replaceWithText(juce::JSON::toString(rows,true));
}
void render(const juce::File& file,const juce::File& out,bool hybridOnly=false){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader&&reader->numChannels==2,"Expected a stereo source WAV");
    Buffer source(2,(int)reader->lengthInSamples);reader->read(&source,0,source.getNumSamples(),0,true,true);
    for(float drive:{6.f,12.f})for(int mode=0;mode<4;++mode){if(mode==0&&drive==12)continue;
        if(hybridOnly&&mode!=3)continue;
        auto r=process(source,reader->sampleRate,mode,drive);
        Buffer aligned(r.audio.getArrayOfWritePointers(),2,r.latency,source.getNumSamples());
        const auto name=juce::String(names[mode])+"-"+juce::String((int)drive)+".wav";write(out.getChildFile(name),aligned,reader->sampleRate);
        std::cout<<"RENDER "<<name<<" peak="<<aligned.getMagnitude(0,aligned.getNumSamples())<<" cpu="<<r.cpu<<" p99_ms="<<r.p99<<" worst_ms="<<r.worst<<" latency="<<r.latency<<" guards="<<r.guards<<"\n";
    }
}
void renderDriven(const juce::File& file,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader&&reader->numChannels==2,"Expected a stereo source WAV");
    Buffer source(2,(int)reader->lengthInSamples);reader->read(&source,0,source.getNumSamples(),0,true,true);
    write(out.getChildFile("dry.wav"),source,reader->sampleRate);
    // +18 dB externally and +12 dB on each model: eight times the input of
    // the previous +12 dB audition, without exceeding ToTape9's control range.
    // Compensate externally after processing so PCM export cannot add clipping.
    const float preGain=std::pow(10.f,18.f/20.f);
    source.applyGain(preGain);
    for(int mode:{1,2,3}){
        auto r=process(source,reader->sampleRate,mode,12);
        Buffer aligned(r.audio.getArrayOfWritePointers(),2,r.latency,source.getNumSamples());
        aligned.applyGain(1.f/preGain);
        const auto name=juce::String(names[mode])+"-30.wav";write(out.getChildFile(name),aligned,reader->sampleRate);
        std::cout<<"DRIVEN "<<name<<" total_drive_db=30 external_pregain_db=18 model_drive_db=12 peak="<<aligned.getMagnitude(0,aligned.getNumSamples())<<" latency="<<r.latency<<" guards="<<r.guards<<"\n";
    }
}
void renderCream(const juce::File& file,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    require(reader&&reader->numChannels==2,"Expected a stereo source WAV");
    Buffer source(2,(int)reader->lengthInSamples);reader->read(&source,0,source.getNumSamples(),0,true,true);
    for(int mode=0;mode<3;++mode){auto r=processCream(source,reader->sampleRate,mode);
        Buffer aligned(r.audio.getArrayOfWritePointers(),2,r.latency,source.getNumSamples());
        const auto& v=tide::lab::creamVoicings[(size_t)mode];write(out.getChildFile(juce::String(v.name)+".wav"),aligned,reader->sampleRate);
        std::cout<<"CREAM "<<v.name<<" drive_db="<<v.driveDb<<" pre_darken_db="<<v.preDarkenDb<<" post_pole_hz="<<v.postPoleHz<<" saturation="<<v.saturation<<" peak="<<aligned.getMagnitude(0,aligned.getNumSamples())<<" field="<<r.field<<" guards="<<r.guards<<"\n";
    }
}
// Extend the coherent tone beyond the latency-aligned measurement window so
// neither oversampling-filter ringing nor the signal's stop enters the FFT.
Buffer signal(double rate,int bin,float amplitude,int length=41984){
    Buffer b(2,length);for(int i=0;i<length;++i){const float x=amplitude*(float)std::sin(2*juce::MathConstants<double>::pi*bin*i/8192.);b.setSample(0,i,x);b.setSample(1,i,x);}return b;
}
void dynamicProbes(const juce::File& out){
    for(int rate:{44100,48000})for(int bin:{17,1021,3001})for(float amp:{.0005f,.002f,.008f,.032f,.128f,.2f})for(int mode=0;mode<3;++mode){
        auto input=signal(rate,bin,amp);auto r=processDynamic(input,rate,mode,512);
        Buffer tail(r.audio.getArrayOfWritePointers(),2,32768+r.latency,8192);
        const auto name=juce::String(dynamicNames[mode])+"-"+juce::String(rate)+"-"+juce::String(bin)+"-"+juce::String(juce::roundToInt(amp*1000000))+".wav";
        write(out.getChildFile(name),tail,rate);
    }
    std::cout<<"PASS 108 input-level/frequency probes: finite, no recovery, below magnetic field clamp\n";
}
void dynamicChecks(){
    for(double rate:{44100.,48000.,96000.})for(int mode=0;mode<3;++mode){
        auto input=signal(rate,137,.19f,16000);auto a=processDynamic(input,rate,mode,127),b=processDynamic(input,rate,mode,512);float error=0;
        for(int c=0;c<2;++c)for(int n=0;n<a.audio.getNumSamples();++n){error=std::max(error,std::abs(a.audio.getSample(c,n)-b.audio.getSample(c,n)));require(a.audio.getSample(0,n)==a.audio.getSample(1,n),"Dynamic hybrid moved centered mono");}
        require(error<2e-6f,"Dynamic hybrid depends on block partition");input.clear(1,0,input.getNumSamples());auto isolated=processDynamic(input,rate,mode,127);
        require(isolated.audio.getMagnitude(1,0,isolated.audio.getNumSamples())==0,"Dynamic hybrid crossfeeds audio");
        std::cout<<"PASS dynamic "<<rate<<" "<<dynamicNames[mode]<<" partition, centered mono, channel isolation, field="<<a.field<<"\n";
    }
    for(int mode=0;mode<3;++mode){auto input=signal(48000,177,.2f,72000);input.clear(0,2048,69952);input.clear(1,2048,69952);auto r=processDynamic(input,48000,mode);
        require(r.audio.getMagnitude(r.audio.getNumSamples()-4096,4096)<1e-7f,"Dynamic hybrid tail did not settle");
    }
    std::cout<<"PASS dynamic burst recovery and quiet tails\n";
}
void creamProbes(const juce::File& out){
    for(int rate:{44100,48000})for(int bin:{17,177,563,1403,3001})for(float amp:{.1f,.2f})for(int mode=0;mode<3;++mode){
        auto input=signal(rate,bin,amp);auto r=processCream(input,rate,mode,512);
        Buffer tail(r.audio.getArrayOfWritePointers(),2,32768+r.latency,8192);
        const auto name=juce::String(tide::lab::creamVoicings[(size_t)mode].name)+"-"+juce::String(rate)+"-"+juce::String(bin)+"-"+juce::String((int)(amp*100))+".wav";
        write(out.getChildFile(name),tail,rate);
    }
    std::cout<<"PASS 60 strong-drive stationary probes: finite, no recovery, below magnetic field clamp\n";
}
void creamChecks(){
    for(double rate:{44100.,48000.,96000.})for(int mode=0;mode<3;++mode){
        auto input=signal(rate,137,.19f,16000);auto a=processCream(input,rate,mode,127),b=processCream(input,rate,mode,512);float error=0;
        for(int c=0;c<2;++c)for(int n=0;n<a.audio.getNumSamples();++n){error=std::max(error,std::abs(a.audio.getSample(c,n)-b.audio.getSample(c,n)));require(a.audio.getSample(0,n)==a.audio.getSample(1,n),"Cream moved centered mono");}
        require(error<2e-6f,"Cream depends on block partition");input.clear(1,0,input.getNumSamples());auto isolated=processCream(input,rate,mode,127);
        require(isolated.audio.getMagnitude(1,0,isolated.audio.getNumSamples())==0,"Cream crossfeeds audio");
        std::cout<<"PASS cream "<<rate<<" "<<tide::lab::creamVoicings[(size_t)mode].name<<" partition, centered mono, channel isolation, field="<<a.field<<"\n";
    }
    for(int mode=0;mode<3;++mode){auto input=signal(48000,177,.2f,72000);input.clear(0,2048,69952);input.clear(1,2048,69952);auto r=processCream(input,48000,mode);
        require(r.audio.getMagnitude(r.audio.getNumSamples()-4096,4096)<1e-7f,"Cream tail did not settle");
    }
    std::cout<<"PASS cream burst recovery and quiet tails\n";
}
void probes(const juce::File& out,bool hybridOnly=false){
    for(int rate:{44100,48000})for(int bin:{17,177,563,1403,3001})for(float drive:{0.f,6.f,12.f})for(int mode:{1,2,3}){
        if(hybridOnly&&mode!=3)continue;
        auto input=signal(rate,bin,.3f);auto r=process(input,rate,mode,drive,512,false);
        Buffer tail(r.audio.getArrayOfWritePointers(),2,32768+r.latency,8192);
        const auto name=juce::String(names[mode])+"-"+juce::String(rate)+"-"+juce::String(bin)+"-"+juce::String((int)drive)+".wav";
        write(out.getChildFile(name),tail,rate);
    }
    for(int bin:{34,1024})for(float amplitude:{.02f,.2f})for(int mode:{1,2,3}){
        if(hybridOnly&&mode!=3)continue;
        auto input=signal(48000,bin,amplitude);auto r=process(input,48000,mode,6,512,false);Buffer tail(r.audio.getArrayOfWritePointers(),2,32768+r.latency,8192);
        write(out.getChildFile("dynamic-"+juce::String(names[mode])+"-"+juce::String(bin)+"-"+juce::String((int)(amplitude*100))+".wav"),tail,48000);
    }
    std::cout<<"PASS "<<(hybridOnly?30:90)<<" spectral probes and "<<(hybridOnly?4:12)<<" dynamic-response probes rendered\n";
}
void checks(){
    for(double rate:{44100.,48000.,96000.}){
        tide::room::Tape tape;tide::room::TapeSettings settings;settings.enabled=true;tape.setSettings(settings);tape.prepare(rate,512);
        tide::lab::HybridTape hybrid;tide::lab::TapeSettings hs;hs.enabled=true;hs.soften=0;hybrid.setSettings(hs);hybrid.prepare(rate,512);
        auto input=signal(rate,317,.18f,16000);Buffer a,b;a.makeCopyOf(input);b.makeCopyOf(input);
        tape.process(a);hybrid.process(b);float error=0;for(int c=0;c<2;++c)for(int i=0;i<a.getNumSamples();++i)error=std::max(error,std::abs(a.getSample(c,i)-b.getSample(c,i)));
        require(error==0,"Zero-softening hybrid changed Tide 0.3 audio");
        auto ref=process(input,rate,3,12,512,false);auto partitioned=process(input,rate,3,12,127,false);error=0;
        for(int i=0;i<ref.audio.getNumSamples();++i){error=std::max(error,std::abs(ref.audio.getSample(0,i)-partitioned.audio.getSample(0,i)));require(ref.audio.getSample(0,i)==ref.audio.getSample(1,i),"Hybrid moved centered mono");}
        require(error<2e-6f,"Hybrid depends on block partition");
        input.clear(1,0,input.getNumSamples());auto isolated=process(input,rate,3,12);require(isolated.audio.getMagnitude(1,0,isolated.audio.getNumSamples())==0,"Hybrid crossfeeds audio");
        std::cout<<"PASS "<<rate<<" zero-softening exact, partition, centered mono, channel isolation\n";
    }
    tide::lab::HybridTape hybrid;tide::lab::TapeSettings settings;settings.enabled=true;hybrid.setSettings(settings);hybrid.prepare(48000,512);
    Buffer b(2,127);float peak=0;
    for(int k=0;k<1800;++k){if(k%100==0){settings.enabled=(k/100)%3!=0;settings.soften=(k/100)%2?1.f:0.f;settings.drive=(k/100)%2?18.f:0.f;settings.saturation=(k/100)%2?1.f:0.f;settings.bias=(k/100)%2?.25f:.95f;hybrid.setSettings(settings);}
        for(int i=0;i<127;++i){double t=(k*127+i)/48000.;b.setSample(0,i,.3f*(float)(std::sin(t*2731)+.3*std::sin(t*18531)));b.setSample(1,i,.2f*(float)std::sin(t*8517));}
        hybrid.process(b);finite(b);peak=std::max(peak,b.getMagnitude(0,b.getNumSamples()));
    }
    require(peak<1&&hybrid.guardCount()==0,"Hybrid automation exceeded bounds");std::cout<<"PASS hybrid live controls, repeated bypass, extremes peak="<<peak<<" guards="<<hybrid.guardCount()<<"\n";
    // Native-rate reference determinism and its one-sample latency, without flutter.
    auto quiet=signal(48000,100,.0001f,8192);auto a=process(quiet,48000,2,0,127,false);auto b2=process(quiet,48000,2,0,512,false);
    for(int c=0;c<2;++c)for(int i=0;i<a.audio.getNumSamples();++i)require(a.audio.getSample(c,i)==b2.audio.getSample(c,i),"Airwindows reference varies with block partition");
    Buffer impulse(2,64);impulse.clear();impulse.setSample(0,0,.0001f);impulse.setSample(1,0,.0001f);auto ir=process(impulse,48000,2,0,512,false);
    int peakIndex=0;for(int i=1;i<64;++i)if(std::abs(ir.audio.getSample(0,i))>std::abs(ir.audio.getSample(0,peakIndex)))peakIndex=i;
    require(peakIndex==1,"Airwindows low-level impulse latency changed");std::cout<<"PASS native ToTape9 deterministic partitioning and impulse latency\n";
}
}
int main(int argc,char** argv){std::cout<<std::unitbuf;try{
    require(argc>=2,"Use --checks, --probes OUT, --render INPUT OUT; --cream-* and --dynamic-* select auditions");const juce::String mode=argv[1];
    if(mode=="--checks"){checks();return 0;}
    if(mode=="--cream-checks"){creamChecks();return 0;}
    if(mode=="--dynamic-checks"){dynamicChecks();return 0;}
    const bool isProbe=mode=="--probes"||mode=="--hybrid-probes"||mode=="--cream-probes"||mode=="--dynamic-probes";
    const bool isRender=mode=="--render"||mode=="--hybrid-render"||mode=="--driven"||mode=="--cream-render"||mode=="--dynamic-render"||mode=="--bench";
    require((isProbe&&argc==3)||(isRender&&argc==4),"Bad arguments");
    const auto out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[argc-1]);require(!out.exists(),"Use a new output directory");out.createDirectory();
    if(mode=="--cream-probes")creamProbes(out);
    else if(mode=="--dynamic-probes")dynamicProbes(out);
    else if(isProbe)probes(out,mode=="--hybrid-probes");
    else {const auto input=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
        if(mode=="--cream-render")renderCream(input,out);
        else if(mode=="--dynamic-render")renderDynamic(input,out);
        else if(mode=="--bench")benchmark(input,out);
        else if(mode=="--driven")renderDriven(input,out);
        else render(input,out,mode=="--hybrid-render");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
