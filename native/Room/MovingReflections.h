#pragma once
#include "Binaural.h"
#include <cmath>

namespace tide::room {
// First and second order rectangular-room image sources. Paths never appear,
// disappear or reorder during motion: only continuous delay, gain and damping
// change. Reverside supplies the separate, stationary late field.
class MovingReflections {
public:
    static constexpr int pathCount=24;
    void prepare(double sampleRate,const HrtfBank& filters) {
        rate=sampleRate;bank=&filters;
        history.assign((size_t)juce::nextPowerOfTwo((int)std::ceil(rate*.5)),0.f);
        mask=(int)history.size()-1;index=0;paths={};initial=true;
        follow=(float)-std::expm1(-1/(rate*.035));
        fade.reset(rate,.08);fade.setCurrentAndTargetValue(0);
    }
    void render(const float* mono,float* left,float* right,int count,
                float x,float y,float z,float width,float depth,float height,
                bool enabled,bool headphones,float listenerElevation=1.5f) {
        if(count<=0)return;
        // Listener: centre in X, 10% of room depth from the rear wall.
        // The caller supplies its shared visual/acoustic elevation.
        const float sx=x+width*.5f,sy=y+depth*.1f,sz=z+listenerElevation;
        const float direct=std::sqrt(x*x+y*y+z*z);
        const std::array<float,3> source{{sx,sy,sz}},size{{width,depth,height}};
        const std::array<float,3> listener{{width*.5f,depth*.1f,listenerElevation}};
        int path=0;
        auto aim=[&](std::array<float,3> image,int order){
            const float dx=image[0]-listener[0],dy=image[1]-listener[1],dz=image[2]-listener[2];
            const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            const float side=dx/std::max(.01f,distance);
            auto& p=paths[(size_t)path++];
            for(int c=0;c<2;++c){const float ear=c==0?-.0875f:.0875f;
                const float d=headphones?std::sqrt((dx-ear)*(dx-ear)+dy*dy+dz*dz):distance;
                auto& e=p[(size_t)c];
                // The direct propagation stage adds the common source distance.
                e.targetDelay=juce::jlimit(8.f,(float)mask-16,(d-direct)*(float)rate/343.f);
                const float pan=c==0?-side:side;
                e.targetGain=(order==1?.95f:.48f)/std::max(1.4f,d)*std::sqrt(1.f+.65f*pan);
                const float shadow=headphones?1.f-.48f*std::max(0.f,-pan):1.f;
                const float cutoff=std::min((float)rate*.22f,(order==1?7500.f:5200.f)*shadow/(1+.018f*d));
                e.targetPole=(float)-std::expm1(-2*juce::MathConstants<double>::pi*cutoff/rate);
                if(initial){e.delay=e.delayAim=e.targetDelay;e.gain=e.targetGain;e.pole=e.targetPole;}
                e.delayStep=(e.targetDelay-e.delayAim)/count;
            }
        };
        for(int a=0;a<3;++a)for(int side=0;side<2;++side){auto image=source;image[(size_t)a]=side?2*size[(size_t)a]-source[(size_t)a]:-source[(size_t)a];aim(image,1);}
        for(int a=0;a<3;++a)for(int side=0;side<2;++side){auto image=source;image[(size_t)a]+=side?2*size[(size_t)a]:-2*size[(size_t)a];aim(image,2);}
        for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)for(int sa=0;sa<2;++sa)for(int sb=0;sb<2;++sb){auto image=source;image[(size_t)a]=sa?2*size[(size_t)a]-source[(size_t)a]:-source[(size_t)a];image[(size_t)b]=sb?2*size[(size_t)b]-source[(size_t)b]:-source[(size_t)b];aim(image,2);}
        jassert(path==pathCount);
        if(initial){fade.setCurrentAndTargetValue(enabled?1.f:0.f);initial=false;}
        fade.setTargetValue(enabled?1.f:0.f);
        for(int n=0;n<count;++n){history[(size_t)index]=mono[n];float sum[2]{};
            for(auto& p:paths)for(int c=0;c<2;++c){auto& e=p[(size_t)c];
                // A bounded read-head speed preserves forward time even at
                // extreme creative LFO settings. This is not a zero-alias claim.
                e.delayAim+=e.delayStep;
                e.delay+=juce::jlimit(-.45,.45,(double)follow*(e.delayAim-e.delay));
                e.gain+=follow*(e.targetGain-e.gain);e.pole+=follow*(e.targetPole-e.pole);
                const int whole=(int)e.delay;const float phase=(float)((e.delay-whole)*1024);
                const int lower=std::min(1023,(int)phase);const float blend=phase-lower;
                const auto* a=bank->delayKernel(lower);const auto* b=bank->delayKernel(lower+1);
                float v=0;const int base=index-whole+5;
                for(int k=0;k<12;++k)v+=(a[k]+blend*(b[k]-a[k]))*history[(size_t)((base-k)&mask)];
                e.low1+=e.pole*(v-e.low1);e.low2+=e.pole*(e.low1-e.low2);
                sum[c]+=e.low2*e.gain;
            }
            const float level=fade.getNextValue();left[n]=sum[0]*level;right[n]=sum[1]*level;index=(index+1)&mask;
        }
    }
private:
    // Double precision read-head state avoids quantizing slow movement when
    // delays are thousands of samples long. Audio history remains float.
    struct Ear {double delay=0,delayAim=0,delayStep=0;float targetDelay=0,gain=0,targetGain=0,pole=0,targetPole=0,low1=0,low2=0;};
    std::array<std::array<Ear,2>,pathCount> paths{};
    std::vector<float> history;
    const HrtfBank* bank=nullptr;
    double rate=48000;float follow=0;
    int index=0,mask=0;bool initial=true;
    juce::SmoothedValue<float> fade;
};
}
