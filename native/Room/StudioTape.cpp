#include "StudioTape.h"
namespace tide::room {
namespace {constexpr double pi=juce::MathConstants<double>::pi;constexpr std::array<double,3> weights{.52,.33,.15};constexpr std::array<double,3> sensitivity{1.12,.93,.72};}
double StudioTape::random(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return double(s)*(2./4294967296.)-1;}
double StudioTape::remanence(double x){const double x2=x*x;return std::tanh(x*(1+x2*(.207573964+.00109834909*x2)));}
double StudioTape::firstIntegral(double x) const {
 const double a=std::abs(x);if(a>=16)return a+tailC1;
 const double index=a*512;const int i=(int)index;const double t=index-i,h=1./512;const auto& l=integrals[(size_t)i];const auto& r=integrals[(size_t)i+1];
 return (2*t*t*t-3*t*t+1)*l[1]+(t*t*t-2*t*t+t)*h*l[2]+(-2*t*t*t+3*t*t)*r[1]+(t*t*t-t*t)*h*r[2];
}
double StudioTape::secondIntegral(double x) const {
 const double a=std::abs(x);if(a>=16)return std::copysign(.5*a*a+tailC1*a+tailC2,x);
 const double index=a*512;const int i=(int)index;const double t=index-i,h=1./512;
 const auto& l=integrals[(size_t)i];const auto& r=integrals[(size_t)i+1];
 // Quintic Hermite: value, first and second derivative at both endpoints.
 const double c0=l[0],c1=h*l[1],c2=.5*h*h*l[2];
 const double A=r[0]-c0-c1-c2,B=h*r[1]-c1-2*c2,C=h*h*r[2]-2*c2;
 const double c3=10*A-4*B+.5*C,c4=-15*A+7*B-C,c5=6*A-3*B+.5*C;
 return std::copysign(c0+t*(c1+t*(c2+t*(c3+t*(c4+t*c5)))),x);
}
double StudioTape::divided(double a,double b,double fa,double fb) const {return std::abs(a-b)<1e-4?firstIntegral(.5*(a+b)):(fa-fb)/(a-b);}
void StudioTape::setSettings(StudioSettings s){settings=s;const double values[]={std::pow(10.,s.drive/20.),s.cream,s.bias,s.motion,s.noise,std::pow(10.,s.trim/20.)};for(size_t i=0;i<6;++i)controls[i].setTargetValue(values[i]);}
void StudioTape::prepare(double sr,int block){
 rate=sr;maximum=block;guards=0;modulation.setSize(4,block);activeQuality=juce::jlimit(0,2,settings.quality);
 for(int q=0;q<3;++q){const int order=referenceOrder?referenceOrder:q+2;auto& os=oversamplers[(size_t)q];os=std::make_unique<juce::dsp::Oversampling<float>>(2);os->clearOversamplingStages();for(int i=0;i<order;++i)os->addOversamplingStage(juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,i==0?.08f:.15f,-112.f,i==0?.08f:.15f,-112.f);os->setUsingIntegerLatency(false);os->initProcessing((size_t)block);}
 oversampler=oversamplers[(size_t)activeQuality].get();innerRate=rate*oversampler->getOversamplingFactor();
 for(auto& c:controls)c.reset(innerRate,.05);setSettings(settings);for(auto& c:controls)c.setCurrentAndTargetValue(c.getTargetValue());
 // Integrate a monotone, symmetric switching curve twice using four-node
 // Gauss quadrature. Analytic tails and derivative-matched interpolation keep
 // divided differences stable without evaluating ultrasonic magnetic states.
 integrals[0]={0,0,0};constexpr double nodes[]={-.8611363115940526,-.3399810435848563,.3399810435848563,.8611363115940526};constexpr double quad[]={.3478548451374538,.6521451548625461,.6521451548625461,.3478548451374538};
 for(size_t i=1;i<integrals.size();++i){const double h=1./512,lo=double(i-1)*h;double integral=0,moment=0;for(int k=0;k<4;++k){const double u=.5*h*(1+nodes[k]),f=remanence(lo+u);integral+=quad[k]*f;moment+=quad[k]*(h-u)*f;}integrals[i]={integrals[i-1][0]+h*integrals[i-1][1]+h*.5*moment,integrals[i-1][1]+h*.5*integral,remanence(lo+h)};}
 tailC1=integrals.back()[1]-16;tailC2=integrals.back()[0]-128-tailC1*16;
 // Windowed-sinc fractional readout. Table interpolation is common to both
 // tracks; no independent left/right chorus and no control-rate delay steps.
 for(size_t p=0;p<sinc.size();++p){const double f=double(p)/1024;double sum=0;for(int k=0;k<96;++k){double d=k-47-f;const double win=std::abs(d)<48?.42+.5*std::cos(pi*d/48)+.08*std::cos(2*pi*d/48):0;double h=std::abs(d)<1e-12?1:std::sin(pi*d)/(pi*d);sinc[p][(size_t)k]=(float)(h*win);sum+=h*win;}for(auto& h:sinc[p])h=(float)(h/sum);}
 coefficients();reset();
}
void StudioTape::coefficients(){
 const double g=std::tan(pi*27456.0470/innerRate);pole=g/(1+g);dcPole=std::exp(-2*pi*4/innerRate);
 noisePole=std::exp(-2*pi*10000/rate);grainPole=std::exp(-2*pi*220/rate);
 // Three finite write-zone/depth states, a causal reduced-order approximation.
 for(size_t j=0;j<3;++j)dwell[j]=1/(1+2*innerRate*(1.0+2.0*j)*1e-6);
 pad=latencySamples-double(oversampler->getLatencyInSamples())-1./oversampler->getOversamplingFactor();jassert(pad>100&&pad<1700);
 for(auto& c:channels){auto peak=[&](Biquad& f,double hz,double q,double db){const double a=std::pow(10.,db/40),w=2*pi*hz/rate,alpha=std::sin(w)/(2*q),n=1+alpha/a;f.b0=(1+alpha*a)/n;f.b1=-2*std::cos(w)/n;f.b2=(1-alpha*a)/n;f.a1=f.b1;f.a2=(1-alpha/a)/n;};peak(c.bump,52,.8,1.05);
  auto hp=[&](Biquad& f,double hz){const double w=2*pi*hz/rate,cs=std::cos(w),alpha=std::sin(w)/(2*.7071067811865476),n=1+alpha;f.b0=(1+cs)*.5/n;f.b1=-(1+cs)/n;f.b2=f.b0;f.a1=-2*cs/n;f.a2=(1-alpha)/n;};hp(c.highpass,14);
  // Gentle, minimum-phase replay bandwidth; not the overload softening.
  const double hz=std::min(21000.,rate*.455),w=2*pi*hz/rate,cs=std::cos(w),alpha=std::sin(w)/(2*.7071067811865476),n=1+alpha;
  c.air.b0=(1-cs)*.5/n;c.air.b1=(1-cs)/n;c.air.b2=c.air.b0;c.air.a1=-2*cs/n;c.air.a2=(1-alpha)/n;
 }
}
void StudioTape::reset(){for(auto& os:oversamplers)if(os)os->reset();for(auto& h:history)h.fill(0);write=0;phase1=0;phase2=.7;phase3=1.3;wander=wander2=0;transportRandom=0x3416743;for(size_t i=0;i<2;++i){auto& c=channels[i];c.pre=c.postX=c.postY=c.dc=c.noiseLP=c.grain=0;c.depth.fill(0);c.lastField.fill(0);c.lastIntegral.fill(0);c.olderField.fill(0);c.lastDivided.fill(0);c.bump.clear();c.highpass.clear();c.air.clear();c.random=i==0?0x62e4311:0x7251193;}}
float StudioTape::read(int channel,double delay) const {
 const double at=double(write)+2048-delay;const int base=(int)std::floor(at);const double phase=(at-base)*1024;const int p=std::min(1023,(int)phase);const double f=phase-p;double y=0;for(int k=0;k<96;++k)y+=history[(size_t)channel][(size_t)((base+k-47)&2047)]*(sinc[(size_t)p][(size_t)k]+f*(sinc[(size_t)p+1][(size_t)k]-sinc[(size_t)p][(size_t)k]));return(float)y;
}
void StudioTape::process(juce::AudioBuffer<float>& buffer){
 juce::ScopedNoDenormals denormals;
 if(settings.quality!=activeQuality){activeQuality=juce::jlimit(0,2,settings.quality);oversampler=oversamplers[(size_t)activeQuality].get();innerRate=rate*oversampler->getOversamplingFactor();for(auto& c:controls){const double v=c.getCurrentValue(),target=c.getTargetValue();c.reset(innerRate,.05);c.setCurrentAndTargetValue(v);c.setTargetValue(target);}coefficients();reset();}
 for(int offset=0;offset<buffer.getNumSamples();offset+=maximum){const int n=std::min(maximum,buffer.getNumSamples()-offset);auto low=juce::dsp::AudioBlock<float>(buffer).getSubBlock((size_t)offset,(size_t)n);auto high=oversampler->processSamplesUp(low);
  for(size_t i=0;i<high.getNumSamples();++i){const double drive=controls[0].getNextValue(),cream=controls[1].getNextValue(),bias=controls[2].getNextValue();const double motionNow=controls[3].getNextValue(),noiseNow=controls[4].getNextValue(),trim=controls[5].getNextValue();
   const int factor=(int)oversampler->getOversamplingFactor();if(i%(size_t)factor==0){const int at=(int)i/factor;modulation.setSample(0,at,(float)motionNow);modulation.setSample(1,at,(float)noiseNow);modulation.setSample(2,at,(float)trim);modulation.setSample(3,at,(float)drive);}
   // Pre-emphasis raises short-wavelength field before ALL depth nonlinearities.
   // Its exact inverse acts on the recorded result, including new harmonics.
   preGain=5.48164974+4*cream+.55*bias;postB0=preGain+(1-preGain)*pole;postB1=preGain*(2*pole-1)+(1-preGain)*pole;
   const double k=recordScale*(1+.07*bias),normal=k*(weights[0]*sensitivity[0]+weights[1]*sensitivity[1]+weights[2]*sensitivity[2]);
   for(size_t ch=0;ch<2;++ch){auto& c=channels[ch];double x=high.getSample((int)ch,(int)i);if(!std::isfinite(x)){x=0;++guards;}if(std::abs(x)>16){x=juce::jlimit(-16.,16.,x);++guards;}const double preDelta=(x-c.pre)*pole,preLow=preDelta+c.pre;c.pre=preLow+preDelta;const double field=(preGain*x+(1-preGain)*preLow)*drive;
    double recorded=0;for(size_t j=0;j<3;++j){const double v=k*sensitivity[j]*field,previous=c.lastField[j],older=c.olderField[j];
     const double integral=secondIntegral(v),dd=divided(v,previous,integral,c.lastIntegral[j]);
     double h=0;const double span=std::max({v,previous,older})-std::min({v,previous,older});
     if(std::min({v,previous,older})>4)h=1;
     else if(std::max({v,previous,older}) < -4)h=-1;
     else if(span<.01){const double mean=(v+previous+older)/3,t=remanence(mean),m2=mean*mean,gp=1+3*.207573964*m2+5*.00109834909*m2*m2,gpp=6*.207573964*mean+20*.00109834909*mean*m2;const double variance=((v-mean)*(v-mean)+(previous-mean)*(previous-mean)+(older-mean)*(older-mean))/12;h=t+.5*(1-t*t)*(gpp-2*t*gp*gp)*variance;}
     else if(std::abs(v-older)<1e-4){const double midpoint=.5*(v+older),F=secondIntegral(midpoint);h=2*(firstIntegral(midpoint)-divided(midpoint,previous,F,c.lastIntegral[j]))/(midpoint-previous);}
     else h=2*(dd-c.lastDivided[j])/(v-older);
     c.olderField[j]=previous;c.lastField[j]=v;c.lastIntegral[j]=integral;c.lastDivided[j]=dd;
     const double d=(h-c.depth[j])*dwell[j],m=d+c.depth[j];c.depth[j]=m+d;recorded+=weights[j]*m;}recorded/=normal*drive;
    const double replay=(recorded+(2*pole-1)*c.postX-postB1*c.postY)/postB0;c.postX=recorded;c.postY=replay;c.dc=(1-dcPole)*replay+dcPole*c.dc;
    high.setSample((int)ch,(int)i,(float)((replay-c.dc)*trim));
   }
  }
  oversampler->processSamplesDown(low);
  // Transport and noise live at physical output rate. No fixed block-size LFO.

  const double slowPole=std::exp(-2*pi*.35/rate),wScale=std::sqrt(3*(1+slowPole)/(1-slowPole));
  for(int i=0;i<n;++i){const double motion=modulation.getSample(0,i),noise=modulation.getSample(1,i),trim=modulation.getSample(2,i),drive=modulation.getSample(3,i);wander=slowPole*wander+(1-slowPole)*random(transportRandom);wander2=slowPole*wander2+(1-slowPole)*wander;
   const double fluctuation=motion*(.000032*std::sin(phase1)+.000006*std::sin(phase2)+.0000012*std::sin(phase3)+.000012*std::tanh(wander2*wScale));
   phase1+=2*pi*.53/rate;phase2+=2*pi*4.7/rate;phase3+=2*pi*31.3/rate;for(auto* p:{&phase1,&phase2,&phase3})if(*p>2*pi)*p-=2*pi;
   for(int ch=0;ch<2;++ch){auto& c=channels[(size_t)ch];double x=c.air.tick(c.bump.tick(c.highpass.tick(buffer.getSample(ch,offset+i))));const double white=random(c.random);c.noiseLP=noisePole*c.noiseLP+(1-noisePole)*white;c.grain=grainPole*c.grain+(1-grainPole)*random(c.random);
    // Additive medium hiss + weak signal-dependent granularity. Noise=0 is exact.
    x+=noise*(.0002462*c.noiseLP*trim/drive+.02*c.grain*x);
    history[(size_t)ch][(size_t)write]=(float)x;buffer.setSample(ch,offset+i,read(ch,pad+rate*fluctuation));
   }write=(write+1)&2047;
  }
 }
}
}
