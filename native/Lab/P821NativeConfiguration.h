#pragma once
#include "P821NativeModel.h"
#include <fstream>
#include <string>

namespace tide::p821lab {
// File access belongs to preparation, never to an audio callback.
struct NativeConfiguration {
    ControlModel::Weights controls;
    FeatureExtractor::FilterBank features;
    PartitionedBase::Kernel kernel;
    ChannelAudio::Parameters audio;
    NativeModel::SlowParameters slow;
    bool acEnabled=false;
    FeatureExtractor::Coefficients ac {1.,0.,0.,0.,0.};
    template<class Array> static bool read(std::istream& f,Array& a) {
        return static_cast<bool>(f.read(reinterpret_cast<char*>(a.data()),sizeof(a)));
    }
    template<class Model> static bool gru(std::istream& f,Model& m) {
        return read(f,m.inputWeights)&&read(f,m.recurrentWeights)&&read(f,m.inputBias)&&read(f,m.recurrentBias);
    }
    template<class Model> static bool dense(std::istream& f,Model& m) {return read(f,m.weights)&&read(f,m.bias);}
    bool load(const std::string& folder) {
        std::ifstream weights(folder+"/control.bin",std::ios::binary);
        if (!(gru(weights,controls.channel)&&dense(weights,controls.output)&&dense(weights,controls.memory)
            &&dense(weights,controls.crest)&&gru(weights,controls.width)&&dense(weights,controls.widthOutput)
            &&read(weights,controls.tau)&&read(weights,controls.growth)
            &&weights.read(reinterpret_cast<char*>(&controls.ceilingTau),sizeof(double))
            &&weights.read(reinterpret_cast<char*>(&controls.widthTau),sizeof(double)))) return false;
        std::ifstream featureFile(folder+"/features.bin",std::ios::binary);
        if (!read(featureFile,features)) return false;
        std::ifstream audioFile(folder+"/audio.bin",std::ios::binary);
        if (!(read(audioFile,audio.frequency)&&read(audioFile,audio.q)
            &&audioFile.read(reinterpret_cast<char*>(&audio.release),sizeof(double))
            &&read(audioFile,audio.widthMatrix)&&audioFile.read(reinterpret_cast<char*>(&audio.widthScale),sizeof(double)))) return false;
        std::array<double,5> gentle;
        std::ifstream slowFile(folder+"/slow.bin",std::ios::binary);
        if (!read(slowFile,gentle)||std::abs(gentle[0]-47.)>1e-6) return false;
        slow={gentle[0],gentle[1],gentle[2],gentle[3],gentle[4]};
        std::ifstream kernelFile(folder+"/base.bin",std::ios::binary|std::ios::ate);
        const auto size=kernelFile.tellg();
        if (size<=0||size%(4*sizeof(double))!=0) return false;
        const size_t length=static_cast<size_t>(size)/(4*sizeof(double));kernelFile.seekg(0);
        for (auto& channel:kernel) {
            channel.resize(length);
            if (!kernelFile.read(reinterpret_cast<char*>(channel.data()),length*sizeof(double))) return false;
        }
        std::ifstream acFile(folder+"/ac.bin",std::ios::binary);acEnabled=static_cast<bool>(acFile);
        ac={1.,0.,0.,0.,0.};if (acEnabled&&!read(acFile,ac)) return false;
        std::ifstream dynamicQFile(folder+"/dynamic-q.bin",std::ios::binary);controls.dynamicBandwidth=static_cast<bool>(dynamicQFile);
        if (controls.dynamicBandwidth&&!(dense(dynamicQFile,controls.bandwidth)&&read(dynamicQFile,controls.bandwidthTau))) return false;
        std::ifstream gainQFile(folder+"/gain-q.bin",std::ios::binary);audio.gainDependentBandwidth=static_cast<bool>(gainQFile);
        if (audio.gainDependentBandwidth&&!read(gainQFile,audio.bandwidthShape)) return false;
        std::ifstream svfFile(folder+"/svf.bin",std::ios::binary);audio.stateVariable=static_cast<bool>(svfFile);
        if (audio.stateVariable) {unsigned char flag=0;if (!svfFile.read(reinterpret_cast<char*>(&flag),1)||flag!=1) return false;}
        std::ifstream quietFastFile(folder+"/quiet-fast-state.bin",std::ios::binary);controls.settleFastAfterSilence=static_cast<bool>(quietFastFile);
        if (controls.settleFastAfterSilence&&!read(quietFastFile,controls.quietFastState)) return false;
        std::ifstream quietWidthFile(folder+"/quiet-width-state.bin",std::ios::binary);controls.settleWidthAfterSilence=static_cast<bool>(quietWidthFile);
        if (controls.settleWidthAfterSilence&&!read(quietWidthFile,controls.quietWidthState)) return false;
        std::ifstream restFile(folder+"/rest.bin",std::ios::binary);controls.resetDerivedAfterSilence=static_cast<bool>(restFile);
        if (controls.resetDerivedAfterSilence) {
            unsigned char flag=0;if (!restFile.read(reinterpret_cast<char*>(&flag),1)||flag!=1
                ||!controls.settleFastAfterSilence||!controls.settleWidthAfterSilence||!controls.dynamicBandwidth) return false;
        }
        return true;
    }
};
}
