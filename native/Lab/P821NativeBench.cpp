#include "P821NativeConfiguration.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    using namespace tide::p821lab;
    if (argc!=4) return 2;
    NativeConfiguration configuration;if (!configuration.load(argv[1])) return 3;
    std::ifstream inputFile(argv[2],std::ios::binary|std::ios::ate);const auto size=inputFile.tellg();
    if (size<=0||size%(512*sizeof(float))!=0) return 11;
    std::vector<float> input(static_cast<size_t>(size)/sizeof(float));inputFile.seekg(0);
    if (!inputFile.read(reinterpret_cast<char*>(input.data()),size)) return 12;
    NativeModel model(configuration.features,configuration.controls,configuration.kernel,configuration.audio,
                      configuration.slow,configuration.acEnabled,configuration.ac);
    std::vector<double> output(input.size()),durations(input.size()/512);
    const auto start=std::chrono::steady_clock::now();
    for (size_t a=0;a<input.size();a+=512) {
        const auto blockStart=std::chrono::steady_clock::now();model.process(input.data()+a,output.data()+a);
        durations[a/512]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-blockStart).count();
    }
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream result(argv[3],std::ios::binary);
    if (!result.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(double))) return 13;
    std::sort(durations.begin(),durations.end());
    std::cout<<"{\"seconds\":"<<seconds<<",\"audio_duration\":"<<input.size()/96000.
             <<",\"median_block_ms\":"<<durations[durations.size()/2]<<",\"p99_block_ms\":"<<durations[durations.size()*99/100]
             <<",\"maximum_block_ms\":"<<durations.back()<<"}\n";
}
