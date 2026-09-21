#include "WornTape.h"
namespace tide::room {
namespace {constexpr double pi=juce::MathConstants<double>::pi;}
double WornTape::random(uint32_t& s) noexcept {s^=s<<13;s^=s>>17;s^=s<<5;return double(s)*(2./4294967296.)-1;}
double WornTape::curve(double t) noexcept {t=std::clamp(t,0.,1.);return t*t*t*(10+t*(-15+6*t));}
void WornTape::SmoothRandom::reset(uint32_t s,double low,double high,double sr){seed=s;lo=low;hi=high;a=0;b=random(seed);position=0;length=sr*(lo+(hi-lo)*(.5+.5*random(seed)));}
double WornTape::SmoothRandom::next(double sr){
    if(position>=length){a=b;b=random(seed);position=0;length=sr*(lo+(hi-lo)*(.5+.5*random(seed)));}
    return a+(b-a)*curve(position++/length);
}
void WornTape::Scar::reset(double sr){seed=0x62f821;position=0;attack=hold=release=0;level=0;pan=0;wait=(int)(sr*1.1);}
double WornTape::Scar::next(double sr,double medium){
    if(wait>0){--wait;return 0;}
    if(position>=attack+hold+release){
        position=0;attack=std::max(1,(int)(sr*(.006+.018*(.5+.5*random(seed)))));
        hold=(int)(sr*(.012+.075*(.5+.5*random(seed)))*(1+medium));
        release=(int)(sr*(.085+.32*(.5+.5*random(seed)))*(1+.35*medium));
        level=.35+.65*(.5+.5*random(seed));pan=.4*random(seed);
    }
    const int p=position++;double envelope=p<attack?curve(double(p)/attack):p<attack+hold?1:1-curve(double(p-attack-hold)/release);
    if(position==attack+hold+release)wait=(int)(sr*(1.5+2.8*(.5+.5*random(seed)))/(1+.45*medium));
    return envelope*level;
}
double WornTape::lowpass(double x,double& state,double p) noexcept {const double v=(x-state)*p,y=state+v;state=y+v;return y;}
double WornTape::cutoff(double hz) const noexcept {const double at=std::clamp(hz/rate/.44,0.,1.)*2048;const int i=std::min(2047,(int)at);return cutoffTable[(size_t)i]+(at-i)*(cutoffTable[(size_t)i+1]-cutoffTable[(size_t)i]);}
void WornTape::setSettings(WornSettings s){
    settings=s;const double values[]={std::pow(10.,juce::jlimit(0.f,30.f,s.drive)/20.),juce::jlimit(0.f,1.f,s.age),juce::jlimit(0.f,3.f,s.motion),juce::jlimit(0.f,1.f,s.damage),juce::jlimit(0.f,1.f,s.noise),std::pow(10.,juce::jlimit(-18.f,12.f,s.trim)/20.),double(juce::jlimit(0,1,s.medium))};
    for(size_t i=0;i<controls.size();++i)controls[i].setTargetValue(values[i]);
    dipDepth.setTargetValue(juce::jlimit(0.f,2.f,s.dips));
}
void WornTape::prepare(double sr,int block){
    rate=sr;maximum=std::max(1,block);guards=0;modulation.setSize(7,maximum);
    // Four stages keep foldback from heavily overdriven bright material low;
    // measured against a 64x reference before the deliberately worn playback.
    const int order=referenceOrder?referenceOrder:4;oversampler=std::make_unique<juce::dsp::Oversampling<float>>(2);oversampler->clearOversamplingStages();
    for(int i=0;i<order;++i)oversampler->addOversamplingStage(juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,i==0?.08f:.15f,-112.f,i==0?.08f:.15f,-112.f);
    oversampler->setUsingIntegerLatency(false);oversampler->initProcessing((size_t)maximum);innerRate=rate*oversampler->getOversamplingFactor();
    for(auto& c:controls)c.reset(innerRate,.12);dipDepth.reset(rate,.12);setSettings(settings);for(auto& c:controls)c.setCurrentAndTargetValue(c.getTargetValue());dipDepth.setCurrentAndTargetValue(dipDepth.getTargetValue());
    const double g=std::tan(pi*1550/innerRate);prePole=g/(1+g);dcPole=std::exp(-2*pi*22/innerRate);
    const double depthG=std::tan(pi*18000/innerRate);depthPole=depthG/(1+depthG);
    pad=latencySamples-double(oversampler->getLatencyInSamples())-.5/oversampler->getOversamplingFactor();
    jassert(pad>96&&pad<3900);readout.prepare();ghostHistory.resize((size_t)std::ceil(sr*.437));
    for(size_t i=0;i<cutoffTable.size();++i){const double v=std::tan(pi*.44*double(i)/2048);cutoffTable[i]=v/(1+v);}
    reset();
}
void WornTape::reset(){
    if(oversampler)oversampler->reset();for(auto& h:history)h.fill(0);for(auto& h:ghostHistory)h.fill(0);write=ghostWrite=0;
    channels={};channels[0].random=0x635671;channels[1].random=0x178341;
    slow.reset(0x721309,.7,2.1,rate);fast.reset(0x84201,.13,.43,rate);edge.reset(0x281621,.08,.61,rate);scar.reset(rate);
    phase1=phase2=phase3=previousDelay=largestDelayStep=0;
}
void WornTape::process(juce::AudioBuffer<float>& buffer){
    juce::ScopedNoDenormals noDenormals;
    const int factor=(int)oversampler->getOversamplingFactor();
    for(int offset=0;offset<buffer.getNumSamples();offset+=maximum){
        const int n=std::min(maximum,buffer.getNumSamples()-offset);auto low=juce::dsp::AudioBlock<float>(buffer).getSubBlock((size_t)offset,(size_t)n);auto high=oversampler->processSamplesUp(low);
        for(size_t i=0;i<high.getNumSamples();++i){
            std::array<double,7> v;for(size_t k=0;k<v.size();++k){v[k]=controls[k].getNextValue();if(i%(size_t)factor==0)modulation.setSample((int)k,(int)i/factor,(float)v[k]);}
            const double emphasis=3.4+4*v[1]+1.2*v[6],b0=emphasis+(1-emphasis)*prePole,b1=emphasis*(2*prePole-1)+(1-emphasis)*prePole;
            for(int ch=0;ch<2;++ch){auto& c=channels[(size_t)ch];double x=high.getSample(ch,(int)i);if(!std::isfinite(x)){x=0;++guards;}if(std::abs(x)>16){x=std::clamp(x,-16.,16.);++guards;}
                const double field=1.7*v[0]*(emphasis*x+(1-emphasis)*lowpass(x,c.pre,prePole));
                // Exact, cancellation-free first antiderivative divided difference
                // for f(x)=x/sqrt(1+x*x), with a softer second population.
                double recorded=0;
                for(int j=0;j<2;++j){const double scale=j?.45:1.,z=field*scale,r=std::sqrt(1+z*z);
                    recorded+=(j?.22:.78)*(z+c.last[j])/(r+c.lastRoot[j])/scale;c.last[j]=z;c.lastRoot[j]=r;}
                recorded=lowpass(recorded,c.depth,depthPole)/(1.7*v[0]);
                const double replay=(recorded+(2*prePole-1)*c.postX-b1*c.postY)/b0;c.postX=recorded;c.postY=replay;
                c.dc=(1-dcPole)*replay+dcPole*c.dc;high.setSample(ch,(int)i,(float)(replay-c.dc));
            }
        }
        oversampler->processSamplesDown(low);
        const double hissPole=cutoff(9000),hissHigh=cutoff(240),grainPole=cutoff(170),bassPole=cutoff(190),ghostPole=cutoff(1800);
        for(int i=0;i<n;++i){
            const double dips=dipDepth.getNextValue();
            const double age=modulation.getSample(1,i),motion=modulation.getSample(2,i),damage=modulation.getSample(3,i),noise=modulation.getSample(4,i),trim=modulation.getSample(5,i),medium=modulation.getSample(6,i);
            const double tension=slow.next(rate),rough=fast.next(rate),local=edge.next(rate),crease=scar.next(rate,medium);
            const double wear=damage*(crease+.055*(.5+.5*local));
            // One transport for both tracks. Smooth random tension, roller wow,
            // irregular flutter and drag from the same event that lifts the tape.
            const double raw=rate*motion*(.0027*tension+.00145*std::sin(phase1)+.00022*rough+.000075*std::sin(phase2)+(.000018+.000018*medium)*std::sin(phase3)+.0011*wear);
            const double room=std::min(pad-48,4096-pad-48),desired=room*std::tanh(raw/room);
            // Smoothly bound playback velocity as well as displacement, including
            // during severe creases and automation. The read head never reverses.
            const double movement=previousDelay+.12*std::tanh((desired-previousDelay)/.12);
            largestDelayStep=std::max(largestDelayStep,std::abs(movement-previousDelay));previousDelay=movement;
            phase1+=2*pi*(.57+.05*rough+.05*medium)/rate;phase2+=2*pi*(6.7+1.8*tension+medium*2)/rate;phase3+=2*pi*(28.3+2.1*rough+medium*14)/rate;
            for(auto* phase:{&phase1,&phase2,&phase3})if(*phase>2*pi)*phase-=2*pi;
            const double bandwidth=std::exp(std::log(14000.)+age*std::log(2100./14000.))*(1-.34*medium);
            for(int ch=0;ch<2;++ch){auto& c=channels[(size_t)ch];const double side=ch?1:-1;
                double x=buffer.getSample(ch,offset+i);const double ghost=lowpass(ghostHistory[(size_t)ghostWrite][(size_t)ch],c.ghost,ghostPole);
                ghostHistory[(size_t)ghostWrite][(size_t)ch]=(float)x;x+=.012*age*age*damage*ghost;
                x+=.16*age*lowpass(x,c.bass,bassPole);
                // The new scar's track balance is inaudible until its envelope
                // rises; changing that random balance must not step the bed loss.
                const double contact=std::max(0.,wear+damage*side*(scar.pan*crease+.022*local));
                const double p=cutoff(std::max(350.,bandwidth/(1+5.5*contact)));
                x=lowpass(lowpass(x,c.low1,p),c.low2,p)*std::exp(-1.65*contact*(1+.55*medium)*dips);
                const double white=random(c.random);const double hiss=lowpass(white,c.hissLP,hissPole);const double shaped=hiss-lowpass(hiss,c.hissHP,hissHigh);
                const double grain=lowpass(random(c.random),c.grain,grainPole);
                x+=noise*(.004*(1+.45*age)*shaped+.085*grain*x);
                history[(size_t)ch][(size_t)write]=(float)(x*trim);
            }
            const auto y=readout.read(history,write,0,pad+movement);
            for(int ch=0;ch<2;++ch){float value=y[(size_t)ch];if(!std::isfinite(value)){value=0;++guards;}if(std::abs(value)>8){value=std::clamp(value,-8.f,8.f);++guards;}buffer.setSample(ch,offset+i,value);}
            write=(write+1)&4095;ghostWrite=(ghostWrite+1)%(int)ghostHistory.size();
        }
    }
}
}
