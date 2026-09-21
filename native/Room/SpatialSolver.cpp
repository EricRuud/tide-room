#include "SpatialSolver.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tide::room {
SpatialSolver::~SpatialSolver(){if(fft)vDSP_destroy_fftsetupD(fft);}
void SpatialSolver::prepare(int size,double rate,int oversampling){
    if(size<16||(size&(size-1))||rate<=0||(oversampling!=1&&oversampling!=2&&oversampling!=4))throw std::invalid_argument("Invalid spatial FFT size/rate");
    n=size;padded=oversampling*n;bins=n/2+1;baseOrder=(int)std::log2(n);paddedOrder=(int)std::log2(padded);if(fft)vDSP_destroy_fftsetupD(fft);
    fft=vDSP_create_fftsetupD((vDSP_Length)paddedOrder,kFFTRadix2);
    if(!fft)throw std::runtime_error("Cannot allocate spatial FFT");
    scratch.resize((size_t)padded/2+1);
    for(auto* s:{&hs,&m,&g,&candidate,&candidateG,&step,&residual,&z,&direction,&ap,&temporary})s->resize((size_t)bins);
    h.resize((size_t)n);baseScratch.resize((size_t)n);frequency.resize((size_t)bins);pre.resize((size_t)bins);
    for(auto* v:{&u,&q,&diag,&qdiag,&uv,&qv,&temp})v->resize((size_t)padded);
    for(int k=0;k<bins;++k){const double f=k*rate/n;frequency[(size_t)k]=k==n/2?0:f/std::sqrt(f*f+3500.*3500.);}
}
void SpatialSolver::transform(S& a,int size,bool inverse){
    auto* d=reinterpret_cast<double*>(a.data());DSPDoubleSplitComplex split{d,d+1};
    if(inverse)a[0].imag(a[(size_t)size/2].real());
    vDSP_fft_zripD(fft,&split,2,(vDSP_Length)(size==n?baseOrder:paddedOrder),inverse?kFFTDirection_Inverse:kFFTDirection_Forward);
    if(!inverse){const double scale=.5/size;vDSP_vsmulD(d,1,&scale,d,1,(vDSP_Length)size);a[(size_t)size/2]={a[0].imag(),0};a[0].imag(0);}
}
void SpatialSolver::spectrum(const double* input,S& dest){
    std::copy_n(input,n,reinterpret_cast<double*>(scratch.data()));transform(scratch,n,false);
    std::copy_n(scratch.begin(),bins,dest.begin());dest.back()=0;
}
void SpatialSolver::base(const S& a,double* out){
    std::copy(a.begin(),a.end(),scratch.begin());transform(scratch,n,true);
    std::copy_n(reinterpret_cast<double*>(scratch.data()),n,out);
}
void SpatialSolver::up(const S& a,V& out,bool applyQ){
    std::fill(scratch.begin(),scratch.end(),C{});
    for(int k=0;k<n/2;++k)scratch[(size_t)k]=a[(size_t)k]*(applyQ?C(0,frequency[(size_t)k]):C(1,0));
    transform(scratch,padded,true);std::copy_n(reinterpret_cast<double*>(scratch.data()),padded,out.begin());
}
void SpatialSolver::project(const V& x,S& out){
    std::copy_n(x.begin(),padded,reinterpret_cast<double*>(scratch.data()));transform(scratch,padded,false);
    std::copy_n(scratch.begin(),n/2,out.begin());out.back()=0;
}
double SpatialSolver::dot(const S& a,const S& b){
    // DC occurs once; positive-frequency modes represent both conjugate halves.
    double value=a[0].real()*b[0].real();for(size_t k=1;k<a.size()-1;++k)value+=2*(a[k].real()*b[k].real()+a[k].imag()*b[k].imag());return value;
}
double SpatialSolver::peak(const S& a){base(a,baseScratch.data());double value=0;for(double x:baseScratch)value=std::max(value,std::abs(x));return value;}
double SpatialSolver::state(const S& x,S& gradient){
    up(x,u);up(x,q,true);double energy=0;
    for(int i=0;i<padded;++i){const auto j=(size_t)i;const double m2=u[j]*u[j],q2=q[j]*q[j];
        energy+=.5*m2+.375*m2*m2+(1./12)*m2*m2*m2+.25*amount*q2*q2;temp[j]=u[j]*m2*(1.5+.5*m2);
    }
    project(temp,gradient);for(int k=0;k<bins;++k)gradient[(size_t)k]+=x[(size_t)k]-hs[(size_t)k];
    for(int i=0;i<padded;++i)temp[(size_t)i]=amount*q[(size_t)i]*q[(size_t)i]*q[(size_t)i];
    project(temp,temporary);for(int k=0;k<bins;++k)gradient[(size_t)k]+=C(0,-frequency[(size_t)k])*temporary[(size_t)k];
    return energy/padded-dot(hs,x);
}
void SpatialSolver::hessian(const S& v,S& out){
    up(v,uv);up(v,qv,true);
    for(int i=0;i<padded;++i)temp[(size_t)i]=diag[(size_t)i]*uv[(size_t)i];project(temp,out);
    for(int i=0;i<padded;++i)temp[(size_t)i]=qdiag[(size_t)i]*qv[(size_t)i];project(temp,temporary);
    for(int k=0;k<bins;++k)out[(size_t)k]+=C(0,-frequency[(size_t)k])*temporary[(size_t)k];
}
void SpatialSolver::precondition(const S& v,S& out){for(int k=0;k<bins;++k)out[(size_t)k]=v[(size_t)k]/pre[(size_t)k];}
SpatialSolver::Result SpatialSolver::solve(const double* input,double* output,double softness,int maxNewton,int maxCG,double tolerance){
    amount=std::clamp(softness,0.,maximumSoftness);spectrum(input,hs);base(hs,h.data());double scale=1;
    // The minimizer is unchanged; this is only an initial guess. Estimate the
    // spatial term's stiffness from the input's Q-weighted energy. For a single
    // sinusoid Q contributes its gain to the fourth power to the cubic term.
    double weighted=0;for(int k=1;k<bins-1;++k)weighted+=2*std::norm(hs[(size_t)k])*frequency[(size_t)k]*frequency[(size_t)k];
    const double fraction=weighted/std::max(dot(hs,hs),1e-30);
    const double initialCubic=1.5+amount*fraction*fraction;
    for(int i=0;i<n;++i){const auto j=(size_t)i;scale=std::max(scale,1+std::abs(h[j]));double x=h[j];
        if(!std::isfinite(x)||std::abs(x)>64){std::fill_n(output,n,0.);return {1,0,0,false};}
        for(int k=0;k<16;++k){const double previous=x,x2=x*x;x-=(x+x*x2*(initialCubic+.5*x2)-h[j])/(1+3*initialCubic*x2+2.5*x2*x2);if(x==previous)break;}
        baseScratch[j]=x;
    }
    spectrum(baseScratch.data(),m);Result result;double energy=state(m,g);
    // Newton and CG vectors stay in the retained Fourier basis. This eliminates
    // the forward/inverse round trip for every Hessian and preconditioner call.
    for(int it=0;it<maxNewton;++it){
        const double maximum=peak(g);result.residual=maximum/scale;result.newton=it+1;
        if(result.residual<tolerance)break;
        double avg=0,avgQ=0;for(int i=0;i<padded;++i){const auto j=(size_t)i;const double x2=u[j]*u[j];diag[j]=1+4.5*x2+2.5*x2*x2;qdiag[j]=3*amount*q[j]*q[j];avg+=diag[j];avgQ+=qdiag[j];}
        for(int k=0;k<bins;++k){const auto j=(size_t)k;pre[j]=(avg+avgQ*frequency[j]*frequency[j])/padded;step[j]=0;residual[j]=-g[j];}
        precondition(residual,z);direction=z;double rz=dot(residual,z);
        const double initialNorm=dot(residual,residual),cgTolerance=std::min(.035,std::max(1e-5,maximum*.02));
        for(int ci=0;ci<maxCG;++ci){hessian(direction,ap);const double den=dot(direction,ap);if(den<=1e-30/n)break;
            const double alpha=rz/den;for(int k=0;k<bins;++k){const auto j=(size_t)k;step[j]+=alpha*direction[j];residual[j]-=alpha*ap[j];}++result.cg;
            if(dot(residual,residual)<=cgTolerance*cgTolerance*initialNorm)break;
            precondition(residual,z);const double next=dot(residual,z),beta=next/std::max(rz,1e-30/n);rz=next;
            for(int k=0;k<bins;++k)direction[(size_t)k]=z[(size_t)k]+beta*direction[(size_t)k];
        }
        const double slope=dot(g,step);double alpha=1;bool accepted=false;
        for(int ls=0;ls<8;++ls){for(int k=0;k<bins;++k)candidate[(size_t)k]=m[(size_t)k]+alpha*step[(size_t)k];
            const double e=state(candidate,candidateG);
            if(e<=energy+1e-4*alpha*slope||(maximum<1e-5&&peak(candidateG)<maximum)){m=candidate;g=candidateG;energy=e;accepted=true;break;}alpha*=.5;
        }
        if(!accepted)break;
    }
    result.residual=peak(g)/scale;base(m,output);for(int i=0;i<n;++i)if(!std::isfinite(output[i]))result.finite=false;
    return result;
}
}
