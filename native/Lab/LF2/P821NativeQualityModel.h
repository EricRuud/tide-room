#pragma once
#include "P821NativeModel.h"
#include "ContinuousMultistageLimiter.h"

namespace tide::p821lab {
// Replace only the final limiter with the tested quality experiment. Delay the
// stereo-width trajectory by exactly the limiter latency. References must live
// for this object's lifetime; do not process the wrapped model separately.
class NativeQualityModel {
public:
    NativeQualityModel(NativeModel& model,tide::lab::ContinuousMultistageLimiter& limiter)
        :model_(model),limiter_(limiter),width_(limiter.latency()+1,0.) {reset();}
    size_t latency() const noexcept {return limiter_.latency();}
    void reset() noexcept {model_.reset();limiter_.reset();std::fill(width_.begin(),width_.end(),0.);position_=0;}
    void process(const float* input,double* output) noexcept {
        std::array<double,512> pre {},cap {};std::array<double,256> width {};
        model_.processParts(input,pre.data(),cap.data(),width.data());
        for (size_t n=0;n<256;++n) {
            const auto y=limiter_.process({pre[2*n],pre[2*n+1]},{cap[2*n],cap[2*n+1]});
            width_[position_]=width[n];position_=(position_+1)%width_.size();
            model_.mixFrame(y.left,y.right,width_[position_],output+2*n);
        }
    }
private:
    NativeModel& model_;tide::lab::ContinuousMultistageLimiter& limiter_;
    std::vector<double> width_;size_t position_=0;
};
}
