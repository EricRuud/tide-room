#pragma once
#if defined(__APPLE__) && !defined(TIDE_PORTABLE_FFT)
#include <Accelerate/Accelerate.h>
#else
#include "PortableFFT.h"
#endif
#include <complex>
#include <vector>

namespace tide::room {
// Fixed Fourier-Galerkin model with bounded work and no solve-time allocations.
// The retained bandwidth stays fixed while polynomial quadrature uses 1/2/4x.
// 4x prevents polynomial product folding in a window, not all streaming errors.
class SpatialSolver {
public:
    static constexpr double maximumSoftness=384.;
    SpatialSolver()=default;
    ~SpatialSolver();
    SpatialSolver(const SpatialSolver&)=delete;
    SpatialSolver& operator=(const SpatialSolver&)=delete;
    void prepare(int stateSize,double stateRate,int oversampling=4);
    struct Result {double residual=0;int newton=0,cg=0;bool finite=true;};
    Result solve(const double* input,double* output,double softness,int maxNewton=8,int maxCG=18,double tolerance=2e-7);
private:
    using C=std::complex<double>;
    using V=std::vector<double>;
    using S=std::vector<C>;
#if defined(__APPLE__) && !defined(TIDE_PORTABLE_FFT)
    FFTSetupD fft=nullptr;
#else
    PortableFFT fft;
#endif
    int n=0,padded=0,bins=0,baseOrder=0,paddedOrder=0;
    S scratch,hs,m,g,candidate,candidateG,step,residual,z,direction,ap,temporary;
    V h,baseScratch,u,q,diag,qdiag,uv,qv,temp,frequency,pre;
    double amount=24;
    void transform(S&,int,bool inverse);
    void spectrum(const double*,S&);
    void base(const S&,double*);
    void up(const S&,V&,bool applyQ=false);
    void project(const V&,S&);
    double state(const S&,S&);
    void hessian(const S&,S&);
    void precondition(const S&,S&);
    double peak(const S&);
    // Parseval inner product, equal to mean(a(t)*b(t)), including Hermitian weights.
    static double dot(const S&,const S&);
};
}
