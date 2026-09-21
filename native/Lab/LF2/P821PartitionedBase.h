#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace tide::p821lab {

// Uniform 256-sample partitioned 2x2 convolution. All storage and transforms of
// the measured kernel are prepared in the constructor. process() allocates
// nothing, and retains the complete measured FIR rather than a modal fit.
class PartitionedBase {
public:
    static constexpr int block=256,fftSize=512,bins=257;
    struct Complex { double re=0.,im=0.; };
    using Spectrum=std::array<Complex,bins>;
    using Kernel=std::array<std::vector<double>,4>; // output-major, then input

    explicit PartitionedBase(const Kernel& kernel) {
        constexpr double pi=3.1415926535897932384626433832795;
        for (int n=0;n<fftSize;++n) {
            unsigned value=n,reversed=0;
            for (int j=0;j<9;++j) { reversed=(reversed<<1)|(value&1);value>>=1; }
            bitReverse_[n]=reversed;
            twiddle_[n]={std::cos(2*pi*n/fftSize),-std::sin(2*pi*n/fftSize)};
        }
        size_t length=0;for (const auto& k:kernel) length=std::max(length,k.size());
        partitions_=std::max<size_t>(1,(length+block-1)/block);
        for (auto& bank:kernel_) bank.resize(partitions_);
        for (auto& bank:history_) bank.resize(partitions_);
        for (int matrix=0;matrix<4;++matrix) for (size_t p=0;p<partitions_;++p) {
            std::array<Complex,fftSize> time {};
            for (int n=0;n<block && p*block+n<kernel[matrix].size();++n) time[n].re=kernel[matrix][p*block+n];
            fft(time,false);std::copy_n(time.begin(),bins,kernel_[matrix][p].begin());
        }
        reset();
    }

    void reset() noexcept {
        for (auto& channel:history_) std::fill(channel.begin(),channel.end(),Spectrum{});
        overlap_={};position_=0;
    }

    void process(const float* input,double* output) noexcept {
        for (int c=0;c<2;++c) {
            std::array<Complex,fftSize> time {};
            for (int n=0;n<block;++n) time[n].re=input[2*n+c];
            fft(time,false);std::copy_n(time.begin(),bins,history_[c][position_].begin());
        }
        for (int c=0;c<2;++c) {
            std::array<Complex,fftSize> transformed {};
            for (size_t p=0;p<partitions_;++p) {
                const size_t past=(position_+partitions_-p)%partitions_;
                const auto& x0=history_[0][past];const auto& x1=history_[1][past];
                const auto& h0=kernel_[2*c][p];const auto& h1=kernel_[2*c+1][p];
                for (int k=0;k<bins;++k) {
                    transformed[k].re += h0[k].re*x0[k].re-h0[k].im*x0[k].im;
                    transformed[k].re += h1[k].re*x1[k].re-h1[k].im*x1[k].im;
                    transformed[k].im += h0[k].re*x0[k].im+h0[k].im*x0[k].re;
                    transformed[k].im += h1[k].re*x1[k].im+h1[k].im*x1[k].re;
                }
            }
            for (int k=1;k<bins-1;++k) transformed[fftSize-k]={transformed[k].re,-transformed[k].im};
            fft(transformed,true);
            for (int n=0;n<block;++n) {
                output[2*n+c]=transformed[n].re+overlap_[c][n];overlap_[c][n]=transformed[n+block].re;
            }
        }
        position_=(position_+1)%partitions_;
    }

private:
    void fft(std::array<Complex,fftSize>& values,bool inverse) const noexcept {
        for (int n=0;n<fftSize;++n) if (static_cast<unsigned>(n)<bitReverse_[n]) std::swap(values[n],values[bitReverse_[n]]);
        for (int length=2;length<=fftSize;length*=2) {
            const int half=length/2,stride=fftSize/length;
            for (int a=0;a<fftSize;a+=length) for (int j=0;j<half;++j) {
                const auto u=values[a+j],v=values[a+j+half],w=twiddle_[j*stride];
                const double wi=inverse?-w.im:w.im;
                const Complex product {v.re*w.re-v.im*wi,v.re*wi+v.im*w.re};
                values[a+j]={u.re+product.re,u.im+product.im};values[a+j+half]={u.re-product.re,u.im-product.im};
            }
        }
        if (inverse) for (auto& v:values) {v.re/=fftSize;v.im/=fftSize;}
    }
    std::array<unsigned,fftSize> bitReverse_ {};
    std::array<Complex,fftSize> twiddle_ {};
    std::array<std::vector<Spectrum>,4> kernel_;
    std::array<std::vector<Spectrum>,2> history_;
    std::array<std::array<double,block>,2> overlap_ {};
    size_t partitions_=0,position_=0;
};

} // namespace tide::p821lab
