#include "ContinuousMultistageLimiter.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

bool readDoubles(const std::string& path,std::vector<double>& values) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);const auto size=f.tellg();
    if (size<=0||size%sizeof(double)!=0) return false;values.resize(static_cast<size_t>(size)/sizeof(double));f.seekg(0);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(values.data()),size));
}
int main(int argc,char** argv) {
    using namespace tide::lab;
    if (argc!=6) return 2;const int factor=std::stoi(argv[1]);const double release=std::stod(argv[2]);const std::string folder=argv[3];
    std::vector<double> longFIR,shortFIR,compensation,input;
    if (!readDoubles(folder+"/long.bin",longFIR)||!readDoubles(folder+"/short.bin",shortFIR)
        ||!readDoubles(folder+"/compensation-"+std::to_string(factor)+".bin",compensation)||!readDoubles(argv[4],input)||input.size()%4!=0) return 3;
    ContinuousMultistageLimiter model(factor,release,longFIR,shortFIR,compensation);
    const auto n=input.size()/4;std::vector<double> output(n*2);const auto start=std::chrono::steady_clock::now();
    double peak=0.;
    for (size_t k=0;k<n+model.latency();++k) {
        const auto source=std::min(k,n-1);
        const auto result=model.process(k<n?StereoSample{input[k*4],input[k*4+1]}:StereoSample{},
                                        {input[source*4+2],input[source*4+3]});
        if (k>=model.latency()) {const auto index=(k-model.latency())*2;output[index]=result.left;output[index+1]=result.right;
            peak=std::max(peak,std::max(std::abs(result.left),std::abs(result.right)));}
    }
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream f(argv[5],std::ios::binary);if (!f.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(double))) return 4;
    model.reset();
    for (size_t k=0;k<n+model.latency();++k) {
        const auto source=std::min(k,n-1);const auto result=model.process(k<n?StereoSample{input[k*4],input[k*4+1]}:StereoSample{},
                                        {input[source*4+2],input[source*4+3]});
        if (k>=model.latency()) {const auto index=(k-model.latency())*2;if (result.left!=output[index]||result.right!=output[index+1]) return 5;}
    }
    std::cout<<"{\"factor\":"<<factor<<",\"latency_samples\":"<<model.latency()<<",\"seconds\":"<<seconds
             <<",\"audio_duration\":"<<n/48000.<<",\"peak\":"<<peak<<",\"reset_repeat_exact\":true}\n";
}
