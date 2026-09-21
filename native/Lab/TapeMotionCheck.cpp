#include "../Room/TapeMotion.h"
#include "../Room/TapeReadout.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
int main(){try{
    using namespace tide::room;constexpr double pi=3.14159265358979323846;
    TapeReadout readout;readout.prepare();double worstResidual=0,peakSlew=0;const auto started=std::chrono::steady_clock::now();
    for(double hz:{1000.,3000.,18000.,20000.})for(double amount:{.1,1.,4.}){
        TapeMotion motion;motion.prepare(48000);std::array<std::array<float,769>,2> history{};int write=0;double energy=0,error=0,previous=0;
        for(int n=0;n<48000*6;++n){
            if(n==0)motion.set(true,amount,amount);if(n==48000*4)motion.set(false,amount,amount);
            const double excursion=motion.next();peakSlew=std::max(peakSlew,std::abs(excursion-previous));previous=excursion;
            const float x=(float)std::sin(2*pi*hz*n/48000.);history[0][(size_t)write]=x;history[1][(size_t)write]=x;
            const auto y=readout.read(history,write,166,excursion);if(y[0]!=y[1])throw std::runtime_error("Motion changes stereo coherence");
            if(n>48000){const double target=std::sin(2*pi*hz*(n-166-excursion)/48000.);energy+=target*target;error+=(y[0]-target)*(y[0]-target);}
            if(n>48000*4+9600&&(excursion!=0||y[0]!=history[0][(size_t)((write+769-166)%769)]))throw std::runtime_error("Motion off is not exact");
            write=(write+1)%769;
        }
        worstResidual=std::max(worstResidual,std::sqrt(error/energy));
    }
    if(worstResidual>.0006||peakSlew>.02)throw std::runtime_error("Moving readout quality or depth slew failed");
    std::cout<<"PASS 10/100/400% motion, 1/3/18/20 kHz ideal trajectory, stereo coherence and exact settled off; worst residual "<<20*std::log10(worstResidual)<<" dBr; max delay step "<<peakSlew<<" samples; CPU "<<100*std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()/72<<"%\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
