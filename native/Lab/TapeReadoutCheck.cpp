#include "../Room/TapeReadout.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
int main(){try{
    tide::room::TapeReadout readout;readout.prepare();constexpr double pi=3.14159265358979323846;
    std::array<std::array<float,769>,2> history{};double worstDb=0,worstResidual=0;
    for(double hz:{20.,1000.,12000.,18000.,20000.})for(double fraction:{0.,.125,.333333,.5,.75,.999}){
        history={};int write=0;double energy=0,error=0,reference=0;const double omega=2*pi*hz/48000.;
        for(int n=0;n<24000;++n){const float x=(float)std::sin(omega*n);history[0][(size_t)write]=x;history[1][(size_t)write]=x;
            const auto y=readout.read(history,write,166,fraction);
            if(y[0]!=y[1])throw std::runtime_error("Stereo read position mismatch");
            if(n>769){const double target=std::sin(omega*(n-166-fraction));energy+=y[0]*y[0];error+=(y[0]-target)*(y[0]-target);reference+=target*target;
                if(fraction==0&&y[0]!=history[0][(size_t)((write+769-166)%769)])throw std::runtime_error("Integer path changed");}
            write=(write+1)%769;
        }
        const double db=10*std::log10(energy/reference),residual=std::sqrt(error/reference);worstDb=std::max(worstDb,std::abs(db));worstResidual=std::max(worstResidual,residual);
        if(std::abs(db)>.03||residual>.004)throw std::runtime_error("Readout treble/phase fidelity failed");
    }
    std::cout<<"PASS exact integer bypass, mono coherence, 20 Hz-20 kHz fractional read: worst gain error "<<worstDb<<" dB; worst residual "<<20*std::log10(worstResidual)<<" dBr\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
