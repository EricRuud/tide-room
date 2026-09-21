#include "SpatialTape.h"

namespace tide::room {
struct SpatialTape::Worker final:juce::Thread {
    explicit Worker(SpatialTape& p):Thread("Tide spatial right"),owner(p){}
    ~Worker() override {signalThreadShouldExit();wake.signal();stopThread(-1);}
    void run() override {juce::WorkgroupToken token;for(;;){wake.wait();if(threadShouldExit())return;owner.group.join(token);owner.renderChannel(1);done.signal();}}
    juce::WaitableEvent wake,done;SpatialTape& owner;
};
SpatialTape::SpatialTape()=default;
SpatialTape::~SpatialTape(){release();}
void SpatialTape::release(){prepared=false;worker.reset();}
void SpatialTape::prepare(double sr){
    release();rate=sr;write=phase=dryWrite=0;guards=limited=0;maxResidual=0;currentQuality=juce::jlimit(0,2,settings.quality);
    for(auto& c:channels){c.history.fill(0);c.input.fill(0);c.result.fill(0);c.queued.fill(0);for(int q=0;q<3;++q)c.solvers[(size_t)q].prepare(frameSize,sr,1<<q);}
    driveHistory.fill(std::pow(10.,settings.drive/20));for(auto& d:dry)d.fill(0);window.fill(1);
    for(int i=0;i<frameSize/4;++i){const double w=std::pow(std::sin(juce::MathConstants<double>::halfPi*i/(frameSize/4)),2);window[(size_t)i]=w;window[(size_t)(frameSize-1-i)]=w;}
    drive.reset(sr,.04);drive.setCurrentAndTargetValue(std::pow(10.,settings.drive/20));
    softness.reset(sr,.04);softness.setCurrentAndTargetValue(settings.softness);
    trim.reset(sr,.04);trim.setCurrentAndTargetValue(std::pow(10.,settings.trim/20));
    blend.reset(sr,.025);blend.setCurrentAndTargetValue(settings.enabled?settings.mix:0);
    // On a fresh start both queues and the latency-aligned dry path are silent.
    // Start wet immediately so the first transient cannot escape saturation.
    // Later quality changes and bypass transitions still fade through dry.
    qualityBlend.reset(sr,.012);qualityBlend.setCurrentAndTargetValue(settings.enabled&&settings.mix>0?1:0);
    if(parallel){auto w=std::make_unique<Worker>(*this);const double period=hop/sr*1000;
        if(w->startRealtimeThread(juce::Thread::RealtimeOptions{}.withProcessingTimeMs(period*.25).withMaximumProcessingTimeMs(period).withPeriodMs(period))||w->startThread(juce::Thread::Priority::high))worker=std::move(w);}
    prepared=true;
}
void SpatialTape::setSettings(SpatialSettings s){
    s.drive=juce::jlimit(0.f,36.f,s.drive);s.softness=juce::jlimit(0.f,(float)SpatialSolver::maximumSoftness,s.softness);s.trim=juce::jlimit(-18.f,12.f,s.trim);s.mix=juce::jlimit(0.f,1.f,s.mix);s.quality=juce::jlimit(0,2,s.quality);settings=s;
    if(!prepared)return;drive.setTargetValue(std::pow(10.,s.drive/20));softness.setTargetValue(s.softness);trim.setTargetValue(std::pow(10.,s.trim/20));blend.setTargetValue(s.enabled?s.mix:0);
    if(s.quality!=currentQuality)qualityBlend.setTargetValue(0);
}
void SpatialTape::renderChannel(int index){
    juce::ScopedNoDenormals denormals;auto& c=channels[(size_t)index];double peak=0;for(double x:c.input)peak=std::max(peak,std::abs(x));
    if(peak<1e-10){std::copy(c.input.begin(),c.input.end(),c.result.begin());c.info={};return;}
    c.info=c.solvers[(size_t)currentQuality].solve(c.input.data(),c.result.data(),frameSoftness);
}
void SpatialTape::renderFrame(){
    if(currentQuality!=settings.quality&&qualityBlend.getCurrentValue()==0){currentQuality=settings.quality;}
    if(blend.getCurrentValue()==0&&!blend.isSmoothing()){qualityBlend.setCurrentAndTargetValue(0);return;}
    for(auto& c:channels)for(int i=0;i<frameSize;++i)c.input[(size_t)i]=c.history[(size_t)((write+i)%frameSize)]*window[(size_t)i];
    frameSoftness=softness.getCurrentValue();
    if(worker)worker->wake.signal();renderChannel(0);if(worker)worker->done.wait();else renderChannel(1);
    constexpr int margin=(frameSize-hop)/2;
    for(auto& c:channels){maxResidual=std::max(maxResidual,c.info.residual);if(c.info.residual>2e-7)++limited;if(!c.info.finite)++guards;
        for(int i=0;i<hop;++i){const double field=c.info.finite?c.result[(size_t)(margin+i)]:c.input[(size_t)(margin+i)];
            const double v=field/driveHistory[(size_t)((write+margin+i)%frameSize)];
            c.queued[(size_t)i]=std::isfinite(v)?(float)v:0;}
    }
    if(currentQuality==settings.quality)qualityBlend.setTargetValue(1);
}
void SpatialTape::process(juce::AudioBuffer<float>& b){
    juce::ScopedNoDenormals denormals;if(!prepared)return;
    for(int i=0;i<b.getNumSamples();++i){const double push=drive.getNextValue(),makeup=trim.getNextValue(),wet=blend.getNextValue()*qualityBlend.getNextValue();softness.getNextValue();driveHistory[(size_t)write]=push;
        for(int ch=0;ch<2;++ch){auto& c=channels[(size_t)ch];float input=b.getSample(ch,i);if(!std::isfinite(input)){input=0;++guards;}c.history[(size_t)write]=push*input;dry[(size_t)ch][(size_t)dryWrite]=input;
            const float original=dry[(size_t)ch][(size_t)((dryWrite+1)%(latencySamples+1))];const double coloured=c.queued[(size_t)phase]*makeup;
            b.setSample(ch,i,(float)(original+wet*(coloured-original)));}
        write=(write+1)%frameSize;dryWrite=(dryWrite+1)%(latencySamples+1);
        if(++phase==hop){phase=0;renderFrame();}
    }
}
}
