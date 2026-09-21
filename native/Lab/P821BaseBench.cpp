#include "P821PartitionedBase.h"
#include <chrono>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    using namespace tide::p821lab;
    if (argc!=4) return 2;
    std::ifstream kernelFile(argv[1],std::ios::binary|std::ios::ate);const auto kernelSize=kernelFile.tellg();
    if (kernelSize<=0||kernelSize%(4*sizeof(double))!=0) return 3;
    const size_t length=static_cast<size_t>(kernelSize)/(4*sizeof(double));kernelFile.seekg(0);PartitionedBase::Kernel kernel;
    for (auto& channel:kernel) {
        channel.resize(length);if (!kernelFile.read(reinterpret_cast<char*>(channel.data()),length*sizeof(double))) return 4;
    }
    std::ifstream inputFile(argv[2],std::ios::binary|std::ios::ate);const auto size=inputFile.tellg();
    if (size<=0||size%(512*sizeof(float))!=0) return 5;
    std::vector<float> input(static_cast<size_t>(size)/sizeof(float));inputFile.seekg(0);
    if (!inputFile.read(reinterpret_cast<char*>(input.data()),size)) return 6;
    std::vector<double> output(input.size());PartitionedBase processor(kernel);
    const auto start=std::chrono::steady_clock::now();
    for (size_t a=0;a<input.size();a+=512) processor.process(input.data()+a,output.data()+a);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream result(argv[3],std::ios::binary);
    if (!result.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(double))) return 7;
    std::cout<<"{\"seconds\":"<<seconds<<",\"audio_duration\":"<<input.size()/96000.<<"}\n";
}
