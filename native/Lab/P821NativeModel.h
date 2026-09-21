#pragma once
#include "P821PartitionedBase.h"
#include "P821ChannelAudio.h"

namespace tide::p821lab {

// Complete native-rate fixed-setting research path with optional AC coupling of
// the fast descriptors. Slow gain and width continue to measure the raw input.
// No allocation or file access occurs in process(). Buffer adaptation, sample
// rate conversion, parameter automation and the HQ limiter are not included.
class NativeModel {
public:
    struct SlowParameters { double window=47.,tau=.0223,threshold=-45.1065,slope=.0283,offset=.0047; };
    NativeModel(const FeatureExtractor::FilterBank& features,const ControlModel::Weights& controls,
                const PartitionedBase::Kernel& kernel,const ChannelAudio::Parameters& audio,const SlowParameters& slow,
                bool acEnabled=false,const FeatureExtractor::Coefficients& ac={1.,0.,0.,0.,0.})
        : features_(features,acEnabled,ac),controls_(controls),base_(kernel),audio_(audio),slow_(slow) {
        features_.enableRestingState(controls.resetDerivedAfterSilence);
        slowAlpha_=std::exp(-256./(48000*slow_.tau));reset();
    }
    void reset() noexcept {
        features_.reset();controls_.reset();base_.reset();audio_.reset();previous_={};
        initialized_=false;slowGain_=0.;
    }
    void process(const float* input,double* output) noexcept {
        render(input,[&](const float* prepared,const ControlModel::Result& previous,const ControlModel::Result& current) {
            audio_.process(prepared,output,previous,current);
        });
    }
    void processParts(const float* input,double* pre,double* cap,double* width) noexcept {
        render(input,[&](const float* prepared,const ControlModel::Result& previous,const ControlModel::Result& current) {
            audio_.processParts(prepared,pre,cap,width,previous,current);
        });
    }
    void mixFrame(double left,double right,double delta,double* output) const noexcept {audio_.mixFrame(left,right,delta,output);}
private:
    template<class Render> void render(const float* input,Render&& renderer) noexcept {
        const auto frame=features_.process(input);
        if (!initialized_) {
            FeatureExtractor::Frame initial;
            for (auto& channel:initial.control) {channel.fill(-3.f);channel[14]=frame.control[0][14];}
            initial.width.fill(-3.f);initial.width[25]=0.;previous_=controls_.process(initial);
        }
        const auto current=controls_.process(frame);
        std::array<double,512> linear {};base_.process(input,linear.data());
        const double oldDb=slow_.offset+slowGain_;
        const double target=-slow_.slope*std::max(frame.sharedLevelDb-slow_.threshold,0.);
        slowGain_=initialized_?slowAlpha_*slowGain_+(1-slowAlpha_)*target:target;
        const double newDb=slow_.offset+slowGain_;std::array<float,512> prepared;
        for (int n=0;n<256;++n) {
            const double fraction=(n+1)/256.,gain=std::pow(10.,(oldDb*(1-fraction)+newDb*fraction)/20);
            for (int c=0;c<2;++c) prepared[2*n+c]=static_cast<float>(linear[2*n+c]*gain);
        }
        renderer(prepared.data(),previous_,current);previous_=current;initialized_=true;
    }
    FeatureExtractor features_;
    ControlModel controls_;
    PartitionedBase base_;
    ChannelAudio audio_;
    SlowParameters slow_;
    ControlModel::Result previous_;
    double slowAlpha_=0.,slowGain_=0.;
    bool initialized_=false;
};

} // namespace tide::p821lab
