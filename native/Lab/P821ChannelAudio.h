#pragma once
#include "P821ControlModel.h"

namespace tide::p821lab {

// Native-rate identification renderer. Its fast nonlinear stage can alias;
// the separately tested quality renderer is not yet included in this class.
// Input is the already prepared quiet linear path, including gentle gain.
class ChannelAudio {
public:
    struct Parameters {
        std::array<double,4> frequency {}, q {};
        double release=.005;
        std::array<double,4> widthMatrix {};
        double widthScale=1.;
        bool gainDependentBandwidth=false;
        std::array<double,4> bandwidthShape {};
        bool stateVariable=false;
    };
    explicit ChannelAudio(const Parameters& parameters) : p_(parameters) {
        alpha_=std::exp(-1./(48000*std::clamp(p_.release,.0001,.05))); reset();
    }
    void reset() noexcept { state_={}; envelope_={}; }

    void process(const float* input, double* output, const ControlModel::Result& previous,
                 const ControlModel::Result& current) noexcept {
        std::array<double,512> pre {},cap {};std::array<double,256> width {};
        processParts(input,pre.data(),cap.data(),width.data(),previous,current);
        for (int n=0;n<256;++n) {
            std::array<double,2> frame {};
            for (int c=0;c<2;++c) {
                const double x=pre[2*n+c],level=std::abs(x)/std::max(cap[2*n+c],1.e-9);
                envelope_[c]=level>envelope_[c]?level:alpha_*envelope_[c]+(1-alpha_)*level;
                frame[c]=x/std::max(1.,envelope_[c]);
            }
            mixFrame(frame[0],frame[1],width[n],output+2*n);
        }
    }

    // The same moving filters, exposed before limiting for the separate HQ path.
    void processParts(const float* input,double* pre,double* cap,double* width,
                      const ControlModel::Result& previous,const ControlModel::Result& current) noexcept {
        std::array<std::array<std::array<double,5>,4>,2> left {},right {};
        if (!p_.stateVariable) for (int c=0;c<2;++c) for (int j=0;j<4;++j) {
            left[c][j]=coefficients(j,previous.values[c][j],previous.bandwidthOffset[c][j]);
            right[c][j]=coefficients(j,current.values[c][j],current.bandwidthOffset[c][j]);
        }
        for (int n=0;n<256;++n) {
            const double fraction=(n+1)/256.;
            for (int c=0;c<2;++c) {
                double x=input[2*n+c];
                for (int j=0;j<4;++j) {
                    if (p_.stateVariable) {
                        const double value=previous.values[c][j]*(1-fraction)+current.values[c][j]*fraction;
                        const double offset=bandwidthOffset(j,previous.values[c][j],previous.bandwidthOffset[c][j])*(1-fraction)
                            +bandwidthOffset(j,current.values[c][j],current.bandwidthOffset[c][j])*fraction;
                        x=stateVariable(j,x,value,offset,state_[c][j]);continue;
                    }
                    std::array<double,5> b;
                    for (int k=0;k<5;++k) b[k]=left[c][j][k]*(1-fraction)+right[c][j][k]*fraction;
                    auto& s=state_[c][j];double y=b[0]*x;
                    y += b[1]*s[0]-b[3]*s[2];y += b[2]*s[1]-b[4]*s[3];
                    s[1]=s[0];s[0]=x;s[3]=s[2];s[2]=y;x=y;
                }
                const double gainDb=previous.values[c][4]*(1-fraction)+current.values[c][4]*fraction;
                x *= std::pow(10.,gainDb/20);
                pre[2*n+c]=x;cap[2*n+c]=previous.ceiling[c]*(1-fraction)+current.ceiling[c]*fraction;
            }
            width[n]=previous.widthDelta*(1-fraction)+current.widthDelta*fraction;
        }
    }

    void mixFrame(double left,double right,double delta,double* output) const noexcept {
        const double l=left*p_.widthMatrix[0]+right*p_.widthMatrix[1];
        const double r=left*p_.widthMatrix[2]+right*p_.widthMatrix[3];
        const double mid=.5*(l+r),side=.5*(l-r)*std::pow(10.,delta*p_.widthScale/20);
        output[0]=mid+side;output[1]=mid-side;
    }

private:
    double bandwidthOffset(int j,double value,double offset) const noexcept {
        return offset+(p_.gainDependentBandwidth?2*std::tanh(p_.bandwidthShape[j])*std::tanh((std::sqrt(value*value+.01)-.1)/12):0.);
    }
    // Trapezoidal SVF bell/shelf equations: Andrew Simper, Cytomic,
    // https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
    double stateVariable(int j,double input,double value,double offset,std::array<double,4>& state) const noexcept {
        constexpr double pi=3.1415926535897932384626433832795;
        const double A=std::pow(10.,value/40),q=std::clamp(p_.q[j]*std::exp(offset),.2,8.);
        double g=std::tan(pi*std::clamp(p_.frequency[j],20.,18000.)/48000),k=1/q,m1,m2;
        if (j==0) {g/=std::sqrt(A);m1=k*(A-1);m2=A*A-1;}
        else {k/=A;m1=k*(A*A-1);m2=0.;}
        const double a1=1/(1+g*(g+k)),a2=g*a1,a3=g*a2;
        const double v3=input-state[1],v1=a1*state[0]+a2*v3,v2=state[1]+a2*state[0]+a3*v3;
        state[0]=2*v1-state[0];state[1]=2*v2-state[1];return input+m1*v1+m2*v2;
    }
    std::array<double,5> coefficients(int j,double value,double bandwidthOffset) const noexcept {
        constexpr double pi=3.1415926535897932384626433832795;
        if (p_.gainDependentBandwidth) bandwidthOffset+=2*std::tanh(p_.bandwidthShape[j])*std::tanh((std::sqrt(value*value+.01)-.1)/12);
        const double f=std::clamp(p_.frequency[j],20.,18000.),q=std::clamp(p_.q[j]*std::exp(bandwidthOffset),.2,8.);
        const double A=std::pow(10.,value/40),omega=2*pi*f/48000;
        const double alpha=std::sin(omega)/(2*q),c=std::cos(omega);
        if (j==0) {
            const double beta=2*std::sqrt(A)*alpha,a0=(A+1)+(A-1)*c+beta;
            return {A*((A+1)-(A-1)*c+beta)/a0,2*A*((A-1)-(A+1)*c)/a0,
                    A*((A+1)-(A-1)*c-beta)/a0,-2*((A-1)+(A+1)*c)/a0,
                    ((A+1)+(A-1)*c-beta)/a0};
        }
        const double a0=1+alpha/A,b1=-2*c/a0;
        return {(1+alpha*A)/a0,b1,(1-alpha*A)/a0,b1,(1-alpha/A)/a0};
    }
    Parameters p_;
    std::array<std::array<std::array<double,4>,4>,2> state_ {};
    std::array<double,2> envelope_ {};
    double alpha_=0.;
};

} // namespace tide::p821lab
