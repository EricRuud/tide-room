#include "EightTwentyOneTape.h"
#include "../Lab/LF2/P821NativeConfiguration.h"
#include "../Lab/LF2/P821NativeQualityModel.h"
#include "../Lab/LF2/P821StreamAdapter.h"

namespace tide::room {
namespace {
bool readTaps(const juce::File& file,std::vector<double>& values){
    juce::MemoryBlock data;if(!file.loadFileAsData(data)||data.getSize()==0||data.getSize()%sizeof(double))return false;
    values.resize(data.getSize()/sizeof(double));std::memcpy(values.data(),data.getData(),data.getSize());return true;
}
}
struct EightTwentyOneTape::Impl {
    p821lab::NativeConfiguration config;
    std::unique_ptr<p821lab::NativeModel> model;
    std::unique_ptr<p821lab::StreamAdapter> native;
    std::array<std::unique_ptr<lab::ContinuousMultistageLimiter>,2> limiters;
    std::array<std::unique_ptr<p821lab::NativeQualityModel>,2> quality;
    std::array<std::unique_ptr<p821lab::BasicStreamAdapter<p821lab::NativeQualityModel>>,2> streams;
    bool load(const juce::File& folder,int calibration){
        if(!config.load(folder.getChildFile(calibration?"90030":"model").getFullPathName().toStdString()))return false;
        std::vector<double> longTaps,shortTaps;
        if(!readTaps(folder.getChildFile("hq/long.bin"),longTaps)||!readTaps(folder.getChildFile("hq/short.bin"),shortTaps))return false;
        model=std::make_unique<p821lab::NativeModel>(config.features,config.controls,config.kernel,config.audio,config.slow,config.acEnabled,config.ac);
        native=std::make_unique<p821lab::StreamAdapter>(*model);
        for(int i=0;i<2;++i){const int factor=i?8:4;std::vector<double> compensation;
            if(!readTaps(folder.getChildFile("hq/compensation-"+juce::String(factor)+".bin"),compensation))return false;
            limiters[(size_t)i]=std::make_unique<lab::ContinuousMultistageLimiter>(factor,std::clamp(config.audio.release,.0001,.05),longTaps,shortTaps,compensation);
            quality[(size_t)i]=std::make_unique<p821lab::NativeQualityModel>(*model,*limiters[(size_t)i]);
            streams[(size_t)i]=std::make_unique<p821lab::BasicStreamAdapter<p821lab::NativeQualityModel>>(*quality[(size_t)i]);
        }
        return true;
    }
};
EightTwentyOneTape::EightTwentyOneTape()=default;
EightTwentyOneTape::~EightTwentyOneTape()=default;
void EightTwentyOneTape::prepare(double sampleRate,int block,const juce::File& resources){
    available.fill(false);impl=nullptr;for(auto& m:models)m.reset();maximum=std::max(1,block);guards=0;work.setSize(2,maximum);
    drive.reset(sampleRate,.03);trim.reset(sampleRate,.03);motion.prepare(sampleRate);readout.prepare();
    drive.setCurrentAndTargetValue(settings.drive);trim.setCurrentAndTargetValue(settings.trim);
    if(std::abs(sampleRate-48000.)>.1){messages.fill("821 requires 48 kHz. Change the audio sample rate to enable it.");return;}
    const auto folder=resources==juce::File{}?juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Contents/Resources/821"):resources;
    for(int i=0;i<2;++i){try{auto next=std::make_unique<Impl>();if(!next->load(folder,i)){messages[(size_t)i]="821 calibration data is missing. Rebuild the app resources.";continue;}models[(size_t)i]=std::move(next);available[(size_t)i]=true;messages[(size_t)i]=i?"821 / measured model / 900 at 30 ips":"821 / LF2 research model / 456 at 15 ips";}
        catch(const std::exception& e){messages[(size_t)i]="821 unavailable: "+juce::String(e.what());}}
    reset(settings.quality);
}
void EightTwentyOneTape::reset(int quality) noexcept {
    mode=juce::jlimit(0,2,quality);activeCalibration=juce::jlimit(0,1,settings.calibration);impl=models[(size_t)activeCalibration].get();position=0;gainPosition=0;
    motion.reset();
    for(auto& c:alignment)c.fill(0);for(auto& c:gainDelay)c.fill(1);
    drive.setCurrentAndTargetValue(settings.drive);trim.setCurrentAndTargetValue(settings.trim);
    if(!ready(activeCalibration))return;
    if(mode==0){impl->native->reset();extra=latencySamples-256;}
    else{impl->streams[(size_t)mode-1]->reset();extra=latencySamples-256-(int)impl->quality[(size_t)mode-1]->latency();}
}
void EightTwentyOneTape::process(juce::AudioBuffer<float>& b) noexcept {
    if(!ready(activeCalibration))return;
    drive.setTargetValue(settings.drive);trim.setTargetValue(settings.trim);motion.set(settings.motion,settings.wow,settings.flutter);
    for(int offset=0;offset<b.getNumSamples();offset+=maximum){const int n=std::min(maximum,b.getNumSamples()-offset);
        for(int i=0;i<n;++i){const float gain=juce::Decibels::decibelsToGain(drive.getNextValue());
            gainDelay[0][(size_t)gainPosition]=1/gain;
            // Keep compensation aligned to the sample that received drive.
            gainDelay[1][(size_t)gainPosition]=juce::Decibels::decibelsToGain(trim.getNextValue());
            const int read=(gainPosition+gainLength-latencySamples)%gainLength;
            for(int c=0;c<2;++c)work.setSample(c,i,b.getSample(c,offset+i)*gain);
            b.setSample(0,offset+i,gainDelay[0][(size_t)read]*gainDelay[1][(size_t)read]);
            gainPosition=(gainPosition+1)%gainLength;
        }
        auto* l=work.getWritePointer(0);auto* r=work.getWritePointer(1);
        if(mode==0)impl->native->process(l,r,l,r,(size_t)n);else impl->streams[(size_t)mode-1]->process(l,r,l,r,(size_t)n);
        for(int i=0;i<n;++i){float gain=b.getSample(0,offset+i);
            for(int c=0;c<2;++c)alignment[(size_t)c][(size_t)position]=work.getSample(c,i);
            const double excursion=motion.next();
            const auto moved=readout.read(alignment,position,extra,excursion);
            if(excursion!=0){const auto movedGain=readout.read(gainDelay,(gainPosition+gainLength-n+i)%gainLength,latencySamples,excursion);gain=movedGain[0]*movedGain[1];}
            for(int c=0;c<2;++c){float y=moved[(size_t)c]*gain;
                if(!std::isfinite(y)){y=0;++guards;}b.setSample(c,offset+i,y);
            }
            position=(position+1)%(latencySamples+1);
        }
    }
}
}
