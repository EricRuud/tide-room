// SPDX-License-Identifier: GPL-3.0-only
// Adapted in 2026 from Jatin Chowdhury's CHOW Tape Jiles–Atherton model.
// See ../third_party/chowtape/NOTICE.md and LICENSE for provenance and terms.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tide::room {
class TapeMagnet {
public:
    void reset(bool clearCounters=true){magnet=previousField=olderField=0;if(clearCounters)failures=0;}
    void configure(double saturation,double bias) {
        ms=.5+1.5*(1-std::clamp(saturation,0.,1.));
        a=ms/(.01+6*.65);c=std::sqrt(std::clamp(bias,.25,.95))-.01;
    }
    double smallSignalGain() const {const double g=c*ms/(3*a);return g/(1-alpha*g);}
    uint64_t guardCount() const {return failures;}
    double process(double field) noexcept {
        if(!std::isfinite(field)){reset(false);++failures;return 0;}
        field=std::clamp(field,-12.,12.);
        // A sampled sinusoid's true extremum usually falls between samples.
        // Recover an in-segment turning point from a quadratic interpolant,
        // then integrate each monotonic portion in field space. Missing these
        // reversals creates modulation of the hysteresis loop at high pitches.
        const double curvature=.5*(field-2*previousField+olderField);
        const double tangent=field-previousField-curvature;
        if(std::abs(curvature)>1.e-12){const double turn=-tangent/(2*curvature);
            if(turn>0&&turn<1){const double extremum=previousField+tangent*turn+curvature*turn*turn;advance(previousField,extremum);advance(extremum,field);}
            else advance(previousField,field);
        }else advance(previousField,field);
        olderField=previousField;previousField=field;return magnet;
    }
private:
    static constexpr double alpha=.0016,k=.47875;
    double ms=1.25,a=1.25/3.91,c=.7962257748,magnet=0,previousField=0,olderField=0;
    uint64_t failures=0;
    void advance(double from,double to) noexcept {
        const double step=to-from;if(std::abs(step)<1.e-16)return;
        const double direction=step>=0?1.:-1.;
        const int count=std::clamp((int)std::ceil(std::abs(step)/(.2*a)),1,32);
        const double h=step/count;double unused=0;
        for(int n=0;n<count;++n){const double start=from+h*n;
            const double k1=slope(magnet,start,direction,unused);
            const double k2=slope(magnet+.5*h*k1,start+.5*h,direction,unused);
            const double k3=slope(magnet+.5*h*k2,start+.5*h,direction,unused);
            const double k4=slope(magnet+h*k3,start+h,direction,unused);
            magnet+=h*(k1+2*k2+2*k3+k4)/6;
            if(!std::isfinite(magnet)||std::abs(magnet)>2*ms){magnet=0;++failures;}
        }
    }
    static void langevin(double q,double& value,double& first,double& second) noexcept {
        if(std::abs(q)<.1){const double q2=q*q;
            // Smooth series avoids cancellation and the slope discontinuity
            // of switching directly between q/3 and coth(q)-1/q.
            value=q*(1./3+q2*(-1./45+q2*(2./945+q2*(-1./4725+q2*2./93555))));
            first=1./3+q2*(-1./15+q2*(2./189+q2*(-1./675+q2*2./10395)));
            second=q*(-2./15+q2*(8./189+q2*(-2./225+q2*16./10395)));
        }else{const double inverse=1/q,ct=1/std::tanh(q),csch2=ct*ct-1;
            value=ct-inverse;first=inverse*inverse-csch2;second=2*ct*csch2-2*inverse*inverse*inverse;
        }
    }
    double slope(double m,double h,double direction,double& prime) const noexcept {
        double l=0,lp=0,lpp=0;langevin((h+alpha*m)/a,l,lp,lpp);
        const double diff=ms*l-m,nc=1-c;
        const double irreversible=direction*diff>0?nc:0;
        const double denominator=nc*direction*k-alpha*diff;
        const double f1=irreversible*diff/denominator,f2=c*ms/a*lp,f3=1-alpha*f2;
        const double diffPrime=alpha*ms/a*lp-1;
        const double f1Prime=irreversible*diffPrime*(1/denominator+alpha*diff/(denominator*denominator));
        const double f2Prime=c*ms*alpha/(a*a)*lpp,f3Prime=-alpha*f2Prime;
        const double f=(f1+f2)/f3;
        prime=(f1Prime+f2Prime-f*f3Prime)/f3;return f;
    }
};
}
