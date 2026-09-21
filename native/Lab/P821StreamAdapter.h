#pragma once
#include "P821NativeModel.h"

namespace tide::p821lab {
// Present the fixed 256-sample analysis frames to hosts with any buffer size.
// Precisely 256 samples of latency are added. Processing is single-threaded;
// the referenced model must outlive this adapter. No allocation/file I/O.
// Sample rate remains 48 kHz. This is not a sample-rate converter.
template<class Model> class BasicStreamAdapter {
public:
    static constexpr size_t latencySamples=256;
    explicit BasicStreamAdapter(Model& model):model_(model) {reset();}
    void reset() noexcept {model_.reset();input_.fill(0.f);output_.fill(0.);position_=0;}
    // Input/output may alias, including ordinary in-place host buffers.
    void processInterleaved(const float* input,float* output,size_t frames) noexcept {
        for (size_t n=0;n<frames;++n) {
            const float l=input[2*n],r=input[2*n+1];
            output[2*n]=static_cast<float>(output_[2*position_]);
            output[2*n+1]=static_cast<float>(output_[2*position_+1]);
            push(l,r);
        }
    }
    void process(const float* left,const float* right,float* outLeft,float* outRight,size_t frames) noexcept {
        for (size_t n=0;n<frames;++n) {
            const float l=left[n],r=right[n];
            outLeft[n]=static_cast<float>(output_[2*position_]);
            outRight[n]=static_cast<float>(output_[2*position_+1]);
            push(l,r);
        }
    }
private:
    void push(float left,float right) noexcept {
        input_[2*position_]=left;input_[2*position_+1]=right;
        if (++position_==256) {model_.process(input_.data(),output_.data());position_=0;}
    }
    Model& model_;
    std::array<float,512> input_ {};
    std::array<double,512> output_ {};
    size_t position_=0;
};
using StreamAdapter=BasicStreamAdapter<NativeModel>;
}
