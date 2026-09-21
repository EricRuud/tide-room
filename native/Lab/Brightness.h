// SPDX-License-Identifier: MIT
// Original experiment inspired by Chris Johnson's TapeHack2 concept:
// blend toward a darker signal according to its departure from a filtered history.
// This uses a continuous-time-scaled one-pole and a smooth squared detector,
// with linked stereo control. It is not the TapeHack2 algorithm or a tape model.
#pragma once
#include <array>
#include <cmath>

namespace tide::lab {
// Fixed audition constants, selected before prepare. Defaults preserve the
// original hybrid; the processing equations and detector topology are unchanged.
struct BrightnessSettings {double frequency=4500,threshold=.12,responseSeconds=.0005;};
class Brightness {
public:
    void prepare(double rate,BrightnessSettings s={}) {pole=std::exp(-2*3.14159265358979323846*s.frequency/rate);detectorPole=std::exp(-1/(s.responseSeconds*rate));thresholdSquared=s.threshold*s.threshold;reset();}
    void reset() {low.fill(0);energy1=energy2=0;}
    void process(std::array<double,2>& x,double amount) {
        const double left=x[0]-low[0],right=x[1]-low[1];
        const double energy=.5*(left*left+right*right);
        // Two smooth detector poles prevent an instantaneous squared/rational
        // control from making sharp waveform features under heavy treble drive.
        // About 1 ms mean delay follows attacks; time constants use seconds.
        energy1=(1-detectorPole)*energy+detectorPole*energy1;
        energy2=(1-detectorPole)*energy1+detectorPole*energy2;
        const double blend=amount*energy2/(energy2+thresholdSquared);
        for(size_t c=0;c<2;++c){low[c]=(1-pole)*x[c]+pole*low[c];x[c]+=blend*(low[c]-x[c]);}
    }
private:
    std::array<double,2> low{};
    double pole=0,detectorPole=0,energy1=0,energy2=0,thresholdSquared=.12*.12;
};
}
