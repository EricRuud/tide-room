#include "P821ControlModel.h"
#include "P821ChannelAudio.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

template<class Array> bool readArray(std::istream& file, Array& a) {
    return static_cast<bool>(file.read(reinterpret_cast<char*>(a.data()),sizeof(a)));
}
template<class Model> bool readGRU(std::istream& file, Model& m) {
    return readArray(file,m.inputWeights)&&readArray(file,m.recurrentWeights)&&readArray(file,m.inputBias)&&readArray(file,m.recurrentBias);
}
template<class Model> bool readDense(std::istream& file, Model& m) {
    return readArray(file,m.weights)&&readArray(file,m.bias);
}
int main(int argc,char** argv) {
    using namespace tide::p821lab;
    if (argc!=4 && argc!=7) return 2;
    ControlModel::Weights w;std::ifstream weights(argv[1],std::ios::binary);
    if (!(readGRU(weights,w.channel)&&readDense(weights,w.output)&&readDense(weights,w.memory)&&readDense(weights,w.crest)
        &&readGRU(weights,w.width)&&readDense(weights,w.widthOutput)&&readArray(weights,w.tau)&&readArray(weights,w.growth))) return 3;
    if (!weights.read(reinterpret_cast<char*>(&w.ceilingTau),sizeof(double))||!weights.read(reinterpret_cast<char*>(&w.widthTau),sizeof(double))) return 4;
    std::ifstream input(argv[2],std::ios::binary|std::ios::ate);const auto bytes=input.tellg();
    if (bytes<=0||bytes%(148*sizeof(float))!=0) return 5;
    std::vector<FeatureExtractor::Frame> frames(static_cast<size_t>(bytes)/(148*sizeof(float)));input.seekg(0);
    for (auto& frame:frames) {
        for (auto& channel:frame.control) if (!readArray(input,channel)) return 6;
        if (!readArray(input,frame.width)) return 6;
    }
    ControlModel model(w);std::vector<ControlModel::Result> result(frames.size());
    const auto start=std::chrono::steady_clock::now();
    for (size_t j=0;j<frames.size();++j) result[j]=model.process(frames[j]);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream output(argv[3],std::ios::binary);
    for (const auto& frame:result) {
        for (const auto& channel:frame.values) output.write(reinterpret_cast<const char*>(channel.data()),sizeof(channel));
        output.write(reinterpret_cast<const char*>(frame.ceiling.data()),sizeof(frame.ceiling));
        output.write(reinterpret_cast<const char*>(&frame.widthDelta),sizeof(double));
    }
    if (!output) return 7;
    double audioSeconds=0.;
    if (argc==7) {
        ChannelAudio::Parameters parameters;std::ifstream params(argv[4],std::ios::binary);
        if (!(readArray(params,parameters.frequency)&&readArray(params,parameters.q)
            &&params.read(reinterpret_cast<char*>(&parameters.release),sizeof(double))
            &&readArray(params,parameters.widthMatrix)&&params.read(reinterpret_cast<char*>(&parameters.widthScale),sizeof(double)))) return 8;
        std::ifstream base(argv[5],std::ios::binary|std::ios::ate);const auto count=base.tellg();
        if (count != static_cast<std::streamoff>((result.size()-1)*512*sizeof(float))) return 9;
        std::vector<float> audio(static_cast<size_t>(count)/sizeof(float));base.seekg(0);
        if (!base.read(reinterpret_cast<char*>(audio.data()),count)) return 10;
        std::vector<double> rendered(audio.size());ChannelAudio path(parameters);
        const auto audioStart=std::chrono::steady_clock::now();
        for (size_t j=0;j+1<result.size();++j) path.process(audio.data()+j*512,rendered.data()+j*512,result[j],result[j+1]);
        audioSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-audioStart).count();
        std::ofstream audioOutput(argv[6],std::ios::binary);
        if (!audioOutput.write(reinterpret_cast<const char*>(rendered.data()),rendered.size()*sizeof(double))) return 11;
    }
    std::cout<<"{\"frames\":"<<frames.size()<<",\"seconds\":"<<seconds<<",\"audio_seconds\":"<<audioSeconds<<"}\n";
}
