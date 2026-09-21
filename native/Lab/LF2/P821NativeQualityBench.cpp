#include "P821NativeConfiguration.h"
#include "P821NativeQualityModel.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

static thread_local bool trackAllocations=false;
static thread_local size_t allocations=0;
void* operator new(std::size_t n) {if (trackAllocations) ++allocations;if (void* p=std::malloc(n?n:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
bool readDoubles(const std::string& path,std::vector<double>& values) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if (size<=0||size%sizeof(double)!=0) return false;values.resize(static_cast<size_t>(size)/sizeof(double));file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(values.data()),size));
}
int main(int argc,char** argv) {
    using namespace tide::p821lab;
    if (argc!=6) return 2;NativeConfiguration config;if (!config.load(argv[1])) return 3;
    const std::string folder=argv[2];const int factor=std::stoi(argv[3]);std::vector<double> longFIR,shortFIR,compensation;
    if (!readDoubles(folder+"/long.bin",longFIR)||!readDoubles(folder+"/short.bin",shortFIR)
        ||!readDoubles(folder+"/compensation-"+std::to_string(factor)+".bin",compensation)) return 4;
    std::ifstream source(argv[4],std::ios::binary|std::ios::ate);const auto size=source.tellg();
    if (size<=0||size%(512*sizeof(float))!=0) return 5;
    std::vector<float> input(static_cast<size_t>(size)/sizeof(float));source.seekg(0);
    if (!source.read(reinterpret_cast<char*>(input.data()),size)) return 6;
    NativeModel base(config.features,config.controls,config.kernel,config.audio,config.slow,config.acEnabled,config.ac);
    tide::lab::ContinuousMultistageLimiter limiter(factor,std::clamp(config.audio.release,.0001,.05),longFIR,shortFIR,compensation);
    NativeQualityModel model(base,limiter);std::vector<double> output(input.size()),durations(input.size()/512);
    const auto start=std::chrono::steady_clock::now();
    for (size_t a=0;a<input.size();a+=512) {
        const auto begin=std::chrono::steady_clock::now();trackAllocations=true;model.process(input.data()+a,output.data()+a);trackAllocations=false;
        durations[a/512]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    }
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream result(argv[5],std::ios::binary);
    if (!result.write(reinterpret_cast<const char*>(output.data()),output.size()*sizeof(double))) return 7;
    model.reset();std::array<double,512> repeated;
    for (size_t a=0;a<input.size();a+=512) {
        trackAllocations=true;model.process(input.data()+a,repeated.data());trackAllocations=false;
        for (size_t n=0;n<512;++n) if (!std::isfinite(repeated[n])||repeated[n]!=output[a+n]) return 8;
    }
    if (allocations) return 9;std::sort(durations.begin(),durations.end());
    std::cout<<"{\"factor\":"<<factor<<",\"latency_samples\":"<<model.latency()<<",\"seconds\":"<<seconds
             <<",\"audio_duration\":"<<input.size()/96000.<<",\"p99_block_ms\":"<<durations[durations.size()*99/100]
             <<",\"maximum_block_ms\":"<<durations.back()<<",\"callback_cpp_allocations\":"<<allocations
             <<",\"reset_repeat_exact\":true}\n";
}
