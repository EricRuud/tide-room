#include "Binaural.h"
#include <RoomHrtfAssets.h>
#include <cmath>
#include <stdexcept>
#if JUCE_MAC
#include <Accelerate/Accelerate.h>
#endif

namespace tide::room {
namespace {
float dot(const float* a,const float* b,int count) {
#if JUCE_MAC
    float result=0;vDSP_dotpr(a,1,b,1,&result,(vDSP_Length)count);return result;
#else
    float result=0;for(int i=0;i<count;++i)result+=a[i]*b[i];return result;
#endif
}
}
void HrtfBank::prepare(double rate) {
    // Fractional propagation delay uses a normalized, windowed-sinc table.
    // Interpolating its coefficients avoids the high-frequency loss of a
    // two-sample crossfade while retaining continuous position changes.
    for(int phase=0;phase<=1024;++phase){const double fraction=phase/1024.;double sum=0;
        for(int k=0;k<12;++k){const double delta=k-5-fraction,angle=juce::MathConstants<double>::pi*delta;
            const double sinc=std::abs(delta)<1.e-12?1.:std::sin(angle)/angle;
            const double window=.42+.5*std::cos(angle/6)+.08*std::cos(angle/3);
            delayTable[(size_t)phase][(size_t)k]=(float)(sinc*window);sum+=sinc*window;
        }
        for(auto& value:delayTable[(size_t)phase])value/=(float)sum;
    }
    juce::MemoryInputStream input(RoomHrtfAssets::kemar_bin,RoomHrtfAssets::kemar_binSize,false);
    if(input.readInt()!=0x48525446)throw std::runtime_error("Invalid headphone filter bank");
    const int count=input.readInt(),originalTaps=input.readInt();const double originalRate=input.readInt();
    if(count!=368||originalTaps!=128)throw std::runtime_error("Invalid headphone filter dimensions");
    taps=(int)std::ceil(originalTaps*rate/originalRate);
    if(taps>maxTaps||taps<1)throw std::runtime_error("Unsupported headphone sample rate");
    directions.clear();directions.resize((size_t)count);rows.clear();
    const double cutoff=std::min(1.,rate/originalRate);
    for(int i=0;i<count;++i) {
        auto& d=directions[(size_t)i];d.elevation=input.readFloat();d.azimuth=input.readFloat();
        if(rows.empty()||std::abs(rows.back().elevation-d.elevation)>.01f)rows.push_back({d.elevation,i,0});++rows.back().count;
        for(int c=0;c<2;++c){std::array<float,128> original{};for(int n=0;n<originalTaps;++n)original[(size_t)n]=input.readFloat();
            for(int n=0;n<taps;++n){if(std::abs(rate-originalRate)<.01){d.kernel[(size_t)c][(size_t)n]=original[(size_t)n];continue;}
                const double position=n*originalRate/rate;double value=0;
                for(int k=std::max(0,(int)std::floor(position)-32);k<std::min(originalTaps,(int)std::floor(position)+33);++k){const double delta=position-k,angle=juce::MathConstants<double>::pi*delta*cutoff;
                    const double sinc=std::abs(angle)<1.e-12?1.:std::sin(angle)/angle;
                    const double window=.5+.5*std::cos(juce::MathConstants<double>::pi*delta/33.);
                    value+=original[(size_t)k]*sinc*window*cutoff;
                }
                d.kernel[(size_t)c][(size_t)n]=(float)(value*originalRate/rate);
            }
        }
    }
}
void HrtfBank::interpolateRow(const Row& row,float azimuth,float weight,bool mirror,Kernel& out) const {
    int hi=1;while(hi<row.count-1&&directions[(size_t)(row.start+hi)].azimuth<azimuth)++hi;
    const auto& a=directions[(size_t)(row.start+std::max(0,hi-1))];const auto& b=directions[(size_t)(row.start+std::min(row.count-1,hi))];
    const float t=b.azimuth>a.azimuth?juce::jlimit(0.f,1.f,(azimuth-a.azimuth)/(b.azimuth-a.azimuth)):0;
    for(int c=0;c<2;++c){const size_t channel=(size_t)(mirror?1-c:c);for(int n=0;n<taps;++n)out[(size_t)c][(size_t)n]+=weight*((1-t)*a.kernel[channel][(size_t)n]+t*b.kernel[channel][(size_t)n]);}
}
void HrtfBank::interpolate(float azimuth,float elevation,Kernel& out) const {
    for(auto& c:out)c.fill(0);
    int hi=1;while(hi<(int)rows.size()-1&&rows[(size_t)hi].elevation<elevation)++hi;
    const auto& a=rows[(size_t)hi-1];const auto& b=rows[(size_t)hi];
    const float t=juce::jlimit(0.f,1.f,(elevation-a.elevation)/(b.elevation-a.elevation));
    interpolateRow(a,std::abs(azimuth),1-t,azimuth<0,out);interpolateRow(b,std::abs(azimuth),t,azimuth<0,out);
}
void BinauralSource::prepare(double sr,const HrtfBank& filters) {
    rate=sr;bank=&filters;index=delayIndex=0;history.fill(0);for(auto& c:delayed)c.fill(0);initial=true;azimuth=elevation=1000;
    gain.reset(sr,.025);gain.setCurrentAndTargetValue(0);mix.reset(sr,.025);mix.setCurrentAndTargetValue(0);delaySmoothing=-std::expm1(-1/(sr*.05));
}
void BinauralSource::render(const float* mono,float* left,float* right,int count,float x,float y,float z,bool enabled,bool headphones) {
    if(count<=0)return;
    const float horizontal=std::sqrt(x*x+y*y),distance=std::sqrt(horizontal*horizontal+z*z);
    const float a=juce::radiansToDegrees(std::atan2(x,y)),e=juce::radiansToDegrees(std::atan2(z,horizontal));
    if(headphones!=previousHeadphones||std::abs(a-azimuth)>.001f||std::abs(e-elevation)>.001f){
        if(headphones)bank->interpolate(a,e,target);
        else {for(auto& c:target)c.fill(0);const float side=x/std::max(.01f,distance);target[0][0]=std::sqrt(.5f*(1-side));target[1][0]=std::sqrt(.5f*(1+side));}
        azimuth=a;elevation=e;previousHeadphones=headphones;
    }
    targetDelay=enabled?juce::jlimit(0.f,(float)delaySize-8,(float)(distance/343.*rate)):0.f;
    const float amplitude=2.4f/std::max(1.4f,distance);
    if(initial){current=target;gain.setCurrentAndTargetValue(amplitude);mix.setCurrentAndTargetValue(enabled?1.f:0.f);delay=delayAim=targetDelay;initial=false;}
    // Reconstruct a continuous target between geometry updates. Feeding a
    // held target straight into the one-pole creates modulation images at
    // sampleRate / blockSize even when the position LFO itself is very slow.
    delayStep=(targetDelay-delayAim)/count;
    gain.setTargetValue(amplitude);mix.setTargetValue(enabled?1.f:0.f);
    previous=current;const float follow=(float)(-std::expm1(-count/(rate*.02)));float change=0;
    for(int c=0;c<2;++c)for(int n=0;n<bank->length();++n){const float delta=follow*(target[(size_t)c][(size_t)n]-current[(size_t)c][(size_t)n]);current[(size_t)c][(size_t)n]+=delta;change=std::max(change,std::abs(delta));}
    const bool moving=change>1.e-7f;const int length=bank->length();
    for(int n=0;n<count;++n){if(--index<0)index=length-1;history[(size_t)index]=history[(size_t)(index+length)]=mono[n];const float level=gain.getNextValue()*mix.getNextValue();
        const float t=(float)(n+1)/(float)count;
        for(int c=0;c<2;++c){float value=dot(history.data()+index,current[(size_t)c].data(),length);if(moving){const float old=dot(history.data()+index,previous[(size_t)c].data(),length);value=old+t*(value-old);} (c==0?left:right)[n]=value*level;}
    }
}
void BinauralSource::propagation(juce::AudioBuffer<float>& b,bool enabled,juce::AudioBuffer<float>* send) {
    for(int n=0;n<b.getNumSamples();++n){delayAim+=delayStep;delay+=delaySmoothing*(delayAim-delay);if(!enabled&&delay<.001)delay=0;
        const int whole=(int)delay;const float fraction=(float)(delay-whole);const int a=(delayIndex-whole+delaySize)%delaySize,before=(a+delaySize-1)%delaySize;
        const float phase=fraction*1024;const int lower=std::min(1023,(int)phase);const float blend=phase-lower;
        const auto* first=bank->delayKernel(lower);const auto* second=bank->delayKernel(lower+1);
        for(int c=0;c<(send?4:2);++c){auto& audio=c<2?b:*send;const int channel=c%2;delayed[(size_t)c][(size_t)delayIndex]=audio.getSample(channel,n);float value=0;
            if(whole>=6)for(int k=0;k<12;++k)value+=((1-blend)*first[k]+blend*second[k])*delayed[(size_t)c][(size_t)((a+5-k+delaySize)%delaySize)];
            else value=(1-fraction)*delayed[(size_t)c][(size_t)a]+fraction*delayed[(size_t)c][(size_t)before];
            audio.setSample(channel,n,value);
        }
        delayIndex=(delayIndex+1)%delaySize;
    }
}
}
