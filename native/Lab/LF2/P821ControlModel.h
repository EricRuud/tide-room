#pragma once
#include "P821FeatureExtractor.h"

// Small, allocation-free recurrent controller for our fixed-setting research
// model. Weights come only from our training checkpoints. Uses float network
// arithmetic and double DSP controls to match the Python model's division.
namespace tide::p821lab {

template<int Inputs, int Hidden> struct GRU {
    std::array<float, 3*Hidden*Inputs> inputWeights {};
    std::array<float, 3*Hidden*Hidden> recurrentWeights {};
    std::array<float, 3*Hidden> inputBias {}, recurrentBias {};
    using State = std::array<float, Hidden>;

    State process(const float* input, State& state) const noexcept {
        std::array<float, 3*Hidden> x {}, h {};
        for (int j=0;j<3*Hidden;++j) {
            float a=0.f,b=0.f;
            for (int k=0;k<Inputs;++k) a += inputWeights[j*Inputs+k]*input[k];
            for (int k=0;k<Hidden;++k) b += recurrentWeights[j*Hidden+k]*state[k];
            x[j]=a+inputBias[j];h[j]=b+recurrentBias[j];
        }
        State next;
        for (int j=0;j<Hidden;++j) {
            const float reset=1.f/(1.f+std::exp(-(x[j]+h[j])));
            const float update=1.f/(1.f+std::exp(-(x[Hidden+j]+h[Hidden+j])));
            const float value=std::tanh(x[2*Hidden+j]+reset*h[2*Hidden+j]);
            next[j]=value+update*(state[j]-value);
        }
        state=next;return next;
    }
};

template<int Inputs,int Outputs> struct Dense {
    std::array<float, Inputs*Outputs> weights {};
    std::array<float, Outputs> bias {};
    std::array<float, Outputs> process(const std::array<float,Inputs>& input) const noexcept {
        std::array<float,Outputs> result;
        for (int j=0;j<Outputs;++j) {
            float value=0.f;
            for (int k=0;k<Inputs;++k) value += weights[j*Inputs+k]*input[k];
            result[j]=value+bias[j];
        }
        return result;
    }
};

class ControlModel {
public:
    struct Weights {
        GRU<61,48> channel;
        Dense<48,6> output;
        Dense<48,5> memory, crest; // crest all zero for v23/v24
        GRU<25,32> width;
        Dense<32,1> widthOutput;
        std::array<double,5> tau {};
        std::array<double,2> growth {};
        double ceilingTau=.005, widthTau=.0223;
        bool dynamicBandwidth=false;
        Dense<48,4> bandwidth;
        std::array<double,4> bandwidthTau {};
        bool settleFastAfterSilence=false,settleWidthAfterSilence=false;
        bool resetDerivedAfterSilence=false;
        GRU<61,48>::State quietFastState {};
        GRU<25,32>::State quietWidthState {};
        bool lowFrequencyCorrection=false;
        Dense<109,4> lowFrequencyHead;
        double lowFrequencyThreshold=.55,lowFrequencyAmount=1.;
    };
    struct Result {
        std::array<std::array<double,5>,2> values {};
        std::array<double,2> ceiling {};
        std::array<std::array<double,4>,2> bandwidthOffset {};
        double widthDelta=0.;
        std::array<double,2> lowFrequencyMorph {};
    };

    explicit ControlModel(const Weights& weights) : weights_(weights) {
        for (int j=0;j<5;++j) alpha_[j]=std::exp(-256./(48000*std::clamp(weights_.tau[j],.003,.2)));
        ceilingAlpha_=std::exp(-256./(48000*std::clamp(weights_.ceilingTau,.001,.1)));
        widthAlpha_=std::exp(-256./(48000*std::clamp(weights_.widthTau,.003,.2)));
        for (int j=0;j<4;++j) bandwidthAlpha_[j]=std::exp(-256./(48000*std::clamp(weights_.bandwidthTau[j],.003,.2)));
        reset();
    }

    void reset() noexcept {
        channelState_={};widthState_={};remembered_={};last_={};quietFrames_={};quietWidthFrames_=0;
        lowFrequencyState_={};lowFrequencyMorph_={};
    }

    Result process(const FeatureExtractor::Frame& frame) noexcept {
        for (int c=0;c<2;++c) {
            const auto& f=frame.control[c];
            quietFrames_[c]=(f[0]<=-2.99999f&&f[6]<=-2.99999f)?std::min(188,quietFrames_[c]+1):0;
            const bool resting=weights_.resetDerivedAfterSilence&&quietFrames_[c]>=188;
            if (resting) remembered_[c]={};
            GRU<61,48>::State h;
            if (weights_.settleFastAfterSilence&&quietFrames_[c]>=188) h=channelState_[c]=weights_.quietFastState;
            else h=weights_.channel.process(f.data(),channelState_[c]);
            auto raw=weights_.output.process(h);auto memory=weights_.memory.process(h);auto crest=weights_.crest.process(h);
            const double own=amplitude(f[0]),other=amplitude(f[6]);
            const double activity=gate(own),shared=gate(std::max(own,other));
            const double level=positive(db(f[0])+12),low=positive(db(f[2])+16);
            const double shelf=positive(db(f[1])+14),mid=positive(db(f[12])+12);
            const std::array<double,5> initial {.4*shelf,.5*low,-.74*level,-.44*level,.20*level-.08*mid};
            const std::array<double,4> current {own,other,amplitude(f[15]),amplitude(f[21])};
            for (int j=0;j<4;++j) remembered_[c][j]=std::max(current[j],historyDecay_*remembered_[c][j]);
            const double memoryOwn=gate(remembered_[c][0]);
            const double memoryShared=gate(std::max(remembered_[c][0],remembered_[c][1]));
            const double crestOwn=positive(gate(remembered_[c][2])-memoryOwn);
            const double crestShared=positive(gate(std::max(remembered_[c][2],remembered_[c][3]))-memoryShared);
            for (int j=0;j<5;++j) {
                double value=std::clamp(initial[j]+18*std::tanh(static_cast<double>(raw[j]))*(j==4?shared:activity),-24.,24.);
                value=std::clamp(value+12*std::tanh(static_cast<double>(memory[j]))*(j==4?memoryShared:memoryOwn),-30.,30.);
                value=std::clamp(value+18*std::tanh(static_cast<double>(crest[j]))*(j==4?crestShared:crestOwn),-30.,30.);
                last_.values[c][j]=(1-alpha_[j])*value+alpha_[j]*last_.values[c][j];
                if (resting) last_.values[c][j]=0.;
            }
            if (weights_.dynamicBandwidth) {
                const auto bandwidth=weights_.bandwidth.process(h);
                for (int j=0;j<4;++j) {
                    const double value=2*std::tanh(static_cast<double>(bandwidth[j]));
                    last_.bandwidthOffset[c][j]=resting?value:(1-bandwidthAlpha_[j])*value+bandwidthAlpha_[j]*last_.bandwidthOffset[c][j];
                }
            }
            const double slowLevel=positive(db(f[14])+45.1065);
            const double ceiling=.795+(weights_.growth[0]*slowLevel+weights_.growth[1]*slowLevel*slowLevel)
                                         *std::exp(std::tanh(static_cast<double>(raw[5]))*shared);
            last_.ceiling[c]=(1-ceilingAlpha_)*ceiling+ceilingAlpha_*last_.ceiling[c];
            if (resting) last_.ceiling[c]=.795;
            if (weights_.lowFrequencyCorrection) {
                std::array<float,109> features;
                std::copy(f.begin(),f.end(),features.begin());std::copy(h.begin(),h.end(),features.begin()+61);
                const auto adjustment=weights_.lowFrequencyHead.process(features);
                for (int j=0;j<4;++j) {
                    const double value=std::tanh(static_cast<double>(adjustment[j]))*activity*(j<2?8.:1.);
                    lowFrequencyState_[c][j]=(1-lowFrequencyAlpha_)*value+lowFrequencyAlpha_*lowFrequencyState_[c][j];
                }
                const double ratio=std::pow(10.,(static_cast<double>(f[1])-static_cast<double>(f[0]))*30./20.);
                const double value=std::clamp((ratio-weights_.lowFrequencyThreshold)/.2,0.,1.)*std::clamp((own-.08)/.04,0.,1.);
                lowFrequencyMorph_[c]=(1-lowFrequencyAlpha_)*value+lowFrequencyAlpha_*lowFrequencyMorph_[c];
            }
        }
        quietWidthFrames_=(frame.width[0]<=-2.99999f&&frame.width[6]<=-2.99999f)?std::min(188,quietWidthFrames_+1):0;
        GRU<25,32>::State width;
        if (weights_.settleWidthAfterSilence&&quietWidthFrames_>=188) width=widthState_=weights_.quietWidthState;
        else width=weights_.width.process(frame.width.data(),widthState_);
        const double widthValue=std::clamp(static_cast<double>(weights_.widthOutput.process(width)[0]),-.05,.4)*frame.width[25];
        last_.widthDelta=(1-widthAlpha_)*widthValue+widthAlpha_*last_.widthDelta;
        if (weights_.resetDerivedAfterSilence&&quietWidthFrames_>=188) last_.widthDelta=0.;
        auto result=last_;
        if (weights_.lowFrequencyCorrection) for (int c=0;c<2;++c) {
            const double g=lowFrequencyMorph_[c]*weights_.lowFrequencyAmount;result.lowFrequencyMorph[c]=g;
            for (int j=0;j<2;++j) {
                result.values[c][j]+=lowFrequencyState_[c][j]*g;
                result.bandwidthOffset[c][j]+=lowFrequencyState_[c][j+2]*g;
            }
        }
        return result;
    }

private:
    static double db(float value) noexcept { return static_cast<double>(value)*30-30; }
    static double amplitude(float value) noexcept { return std::pow(10.,db(value)/20); }
    static double positive(double x) noexcept { return std::max(0.,x); }
    static double gate(double x) noexcept { return std::clamp((x-.1)/(.1+x),0.,1.); }
    Weights weights_;
    std::array<GRU<61,48>::State,2> channelState_ {};
    GRU<25,32>::State widthState_ {};
    std::array<std::array<double,4>,2> remembered_ {};
    std::array<double,5> alpha_ {};
    std::array<double,4> bandwidthAlpha_ {};
    double ceilingAlpha_=0.,widthAlpha_=0.;
    const double historyDecay_=std::exp(-256./(48000*.25));
    Result last_;
    std::array<int,2> quietFrames_ {};
    int quietWidthFrames_=0;
    std::array<std::array<double,4>,2> lowFrequencyState_ {};
    std::array<double,2> lowFrequencyMorph_ {};
    const double lowFrequencyAlpha_=std::exp(-256./(48000*.03));
};

} // namespace tide::p821lab
