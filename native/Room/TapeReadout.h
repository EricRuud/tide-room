#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace tide::room {
// A shared, band-limited fractional read position for the two tape tracks.
// The table is prepared off the audio thread. Integer reads remain exact.
class TapeReadout {
public:
    static constexpr int taps=48,phases=1024;
    void prepare() {
        constexpr double pi=3.14159265358979323846;
        for(int p=0;p<=phases;++p){const double fraction=double(p)/phases;double sum=0;
            for(int k=0;k<taps;++k){const double d=k-(taps/2-1)-fraction;
                const double window=std::abs(d)<taps/2?.42+.5*std::cos(pi*d/(taps/2))+.08*std::cos(2*pi*d/(taps/2)):0;
                const double h=(std::abs(d)<1e-12?1:std::sin(pi*d)/(pi*d))*window;
                table[(size_t)p][(size_t)k]=(float)h;sum+=h;
            }
            for(auto& h:table[(size_t)p])h=(float)(h/sum);
        }
    }
    template<size_t Size>
    std::array<float,2> read(const std::array<std::array<float,Size>,2>& history,int write,int delay,double motion) const noexcept {
        if(motion==0){const auto index=(size_t)((write+(int)Size-delay)%(int)Size);return {history[0][index],history[1][index]};}
        const double at=write+2*(int)Size-delay-motion;const int base=(int)std::floor(at);
        const double phase=(at-base)*phases;const int p=std::min(phases-1,(int)phase);const double f=phase-p;
        std::array<double,2> result{};
        for(int k=0;k<taps;++k){const double h=table[(size_t)p][(size_t)k]+f*(table[(size_t)p+1][(size_t)k]-table[(size_t)p][(size_t)k]);
            const auto index=(size_t)((base+k-(taps/2-1))%(int)Size);
            result[0]+=history[0][index]*h;result[1]+=history[1][index]*h;
        }
        return {(float)result[0],(float)result[1]};
    }
private:
    std::array<std::array<float,taps>,phases+1> table{};
};
}
