#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace tide::room {
// Double-precision real Fourier coefficients, normalized by N on the forward
// transform. Matches SpatialSolver's vDSP convention without callback allocation.
class PortableFFT {
public:
    using Complex=std::complex<double>;
    void prepare(int maximumSize) {
        if(maximumSize<2||(maximumSize&(maximumSize-1)))throw std::invalid_argument("FFT size must be a power of two");
        maximum=maximumSize;work.resize((size_t)maximum);roots.resize((size_t)maximum/2);
        constexpr double tau=6.283185307179586476925286766559;
        for(int k=0;k<maximum/2;++k)roots[(size_t)k]=std::polar(1.,-tau*k/maximum);
    }
    void transform(Complex* packed,int size,bool inverse) {
        if(size<2||size>maximum||(size&(size-1)))throw std::invalid_argument("Unprepared FFT size");
        if(inverse){std::copy_n(packed,size/2+1,work.begin());for(int k=1;k<size/2;++k)work[(size_t)(size-k)]=std::conj(packed[k]);}
        else{const auto* time=reinterpret_cast<const double*>(packed);for(int i=0;i<size;++i)work[(size_t)i]={time[i],0};}
        for(int i=1,j=0;i<size;++i){int bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(work[(size_t)i],work[(size_t)j]);}
        for(int length=2;length<=size;length*=2)for(int start=0;start<size;start+=length)for(int k=0;k<length/2;++k){
            const auto root=roots[(size_t)(k*maximum/length)];const auto a=work[(size_t)(start+k)],b=work[(size_t)(start+k+length/2)]*(inverse?std::conj(root):root);
            work[(size_t)(start+k)]=a+b;work[(size_t)(start+k+length/2)]=a-b;
        }
        if(inverse){auto* time=reinterpret_cast<double*>(packed);for(int i=0;i<size;++i)time[i]=work[(size_t)i].real();}
        else for(int k=0;k<=size/2;++k)packed[k]=work[(size_t)k]/(double)size;
    }
private:
    int maximum=0;std::vector<Complex> work,roots;
};
}
