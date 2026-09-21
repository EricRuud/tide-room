#include "P821NativeConfiguration.h"
#include "P821StreamAdapter.h"
#include "P821NativeQualityModel.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

// Count C++ allocations within callbacks; not a platform-wide malloc tracer.
static thread_local bool measureAllocations=false;
static thread_local size_t callbackAllocations=0;
void* operator new(std::size_t n) {
    if (measureAllocations) ++callbackAllocations;
    if (void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}

template<class Model> int run(Model& model,const std::vector<float>& input,size_t originalFrames,size_t extraLatency=0) {
    using namespace tide::p821lab;
    const size_t frames=originalFrames+256;
    std::vector<float> expected(frames*2,0.f);std::array<double,512> block;
    for (size_t pos=0;pos<originalFrames;pos+=256) {
        model.process(input.data()+pos*2,block.data());
        for (size_t i=0;i<512;++i) expected[(pos+256)*2+i]=static_cast<float>(block[i]);
    }
    BasicStreamAdapter<Model> stream(model);std::vector<float> output(input.size()),left(frames),right(frames),outLeft(frames),outRight(frames);
    for (size_t n=0;n<frames;++n) {left[n]=input[n*2];right[n]=input[n*2+1];}
    std::vector<std::vector<size_t>> patterns {{1},{64},{256},{1024},{0,17,511,3,1024,63,256,2049}};
    double maxDifference=0.,worstCallback=0.,totalSeconds=0.;size_t callbacks=0;
    for (size_t layout=0;layout<4;++layout) for (const auto& pattern:patterns) {
        stream.reset();output=input;outLeft=left;outRight=right;size_t pos=0,index=0;
        const auto begin=std::chrono::steady_clock::now();
        while (pos<frames) {
            const size_t count=std::min(pattern[index++%pattern.size()],frames-pos);
            const auto start=std::chrono::steady_clock::now();measureAllocations=true;
            if (layout==0) stream.processInterleaved(input.data()+pos*2,output.data()+pos*2,count);
            if (layout==1) stream.processInterleaved(output.data()+pos*2,output.data()+pos*2,count);
            if (layout==2) stream.process(left.data()+pos,right.data()+pos,outLeft.data()+pos,outRight.data()+pos,count);
            if (layout==3) stream.process(outLeft.data()+pos,outRight.data()+pos,outLeft.data()+pos,outRight.data()+pos,count);
            measureAllocations=false;
            worstCallback=std::max(worstCallback,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            ++callbacks;pos+=count;
        }
        totalSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        for (size_t n=0;n<frames;++n) for (size_t c=0;c<2;++c) {
            const float actual=layout<2?output[n*2+c]:(c?outRight[n]:outLeft[n]);
            if (!std::isfinite(actual)) return 6;
            maxDifference=std::max(maxDifference,std::abs(double(actual)-expected[n*2+c]));
        }
    }
    if (maxDifference!=0.||callbackAllocations!=0) return 7;
    std::cout<<"{\"sample_rate\":48000,\"latency_samples\":"<<256+extraLatency<<",\"adapter_latency_samples\":256,\"cases\":20,\"frames_per_case\":"<<frames
             <<",\"max_difference\":"<<maxDifference<<",\"callback_cpp_allocations\":"<<callbackAllocations
             <<",\"callback_count\":"<<callbacks<<",\"total_seconds\":"<<totalSeconds
             <<",\"worst_callback_ms\":"<<worstCallback<<",\"finite\":true,\"reset_repeat_exact\":true}\n";
    return 0;
}

int main(int argc,char** argv) {
    using namespace tide::p821lab;
    if (argc!=3&&argc!=5) return 2;
    NativeConfiguration config;if (!config.load(argv[1])) return 3;
    std::ifstream source(argv[2],std::ios::binary|std::ios::ate);const auto size=source.tellg();
    if (size<=0||size%(512*sizeof(float))!=0) return 4;
    const size_t originalFrames=static_cast<size_t>(size)/(2*sizeof(float));
    std::vector<float> input((originalFrames+256)*2,0.f);source.seekg(0);
    if (!source.read(reinterpret_cast<char*>(input.data()),size)) return 5;
    NativeModel model(config.features,config.controls,config.kernel,config.audio,config.slow,config.acEnabled,config.ac);
    if (argc==3) return run(model,input,originalFrames);
    const std::string folder=argv[3];const int factor=std::stoi(argv[4]);
    std::vector<double> longFIR(513),shortFIR(65),compensation(129);
    for (auto pair:{std::pair<std::string,std::vector<double>*>{"long.bin",&longFIR},{"short.bin",&shortFIR},
                    {"compensation-"+std::to_string(factor)+".bin",&compensation}}) {
        std::ifstream f(folder+"/"+pair.first,std::ios::binary);
        if (!f.read(reinterpret_cast<char*>(pair.second->data()),pair.second->size()*sizeof(double))) return 8;
    }
    tide::lab::ContinuousMultistageLimiter limiter(factor,config.audio.release,longFIR,shortFIR,compensation);
    NativeQualityModel quality(model,limiter);return run(quality,input,originalFrames,quality.latency());
}
