#include "PortableFFT.h"
#include "SpatialSolver.h"
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void fourier(){
    tide::room::PortableFFT fft;fft.prepare(4096);
    constexpr double tau=6.283185307179586476925286766559;
    double spectrumError=0,roundtripError=0;
    for(int n:{16,64,256,4096})for(int signal=0;signal<4;++signal){
        std::vector<double> input((size_t)n);
        std::vector<std::complex<double>> packed((size_t)n/2+1);
        auto* time=reinterpret_cast<double*>(packed.data());
        for(int i=0;i<n;++i){
            const double x=signal==0?.37:signal==1?(i%2?-.6:.6):signal==2?(i==3?1.:0.):
                .17+.3*std::sin(tau*3*i/n)+.21*std::cos(tau*7*i/n)+.01*std::sin(i*1.2345);
            input[(size_t)i]=time[i]=x;
        }
        fft.transform(packed.data(),n,false);
        if(n<=256)for(int k=0;k<=n/2;++k){
            std::complex<double> expected{};
            for(int i=0;i<n;++i)expected+=input[(size_t)i]*std::polar(1.,-tau*k*i/n)/(double)n;
            spectrumError=std::max(spectrumError,std::abs(packed[(size_t)k]-expected));
        }
        fft.transform(packed.data(),n,true);
        for(int i=0;i<n;++i)roundtripError=std::max(roundtripError,std::abs(time[i]-input[(size_t)i]));
    }
    require(spectrumError<1e-12,"Fourier coefficients differ from independent DFT");
    require(roundtripError<1e-12,"Real Fourier roundtrip failed");
    std::cout<<"Fourier DFT error "<<spectrumError<<", roundtrip "<<roundtripError<<'\n';
}
void solver(std::ostream* samples){
    double worstResidual=0;
    for(int oversampling:{1,2,4})for(int signal=0;signal<3;++signal){
        constexpr int n=256;
        tide::room::SpatialSolver solver;solver.prepare(n,48000,oversampling);
        std::vector<double> input(n),output(n);
        for(int i=0;i<n;++i)input[(size_t)i]=signal==0?0.:signal==1?.7:
            .4*std::sin(6.283185307179586*9*i/n)+.2*std::cos(6.283185307179586*31*i/n);
        const auto result=solver.solve(input.data(),output.data(),24.);
        require(result.finite&&result.residual<2e-7,"Spatial solver failed to converge");
        worstResidual=std::max(worstResidual,result.residual);
        for(double value:output){
            if(signal<2)require(std::abs(value+1.5*value*value*value+.5*std::pow(value,5)-input[0])<1e-10,"Constant-input solver equation failed");
            if(samples)*samples<<std::setprecision(17)<<value<<'\n';
        }
    }
    std::cout<<"Spatial solver worst residual "<<worstResidual<<'\n';
}
}
int main(int argc,char** argv){try{
    fourier();std::ofstream samples;if(argc>1){samples.open(argv[1]);require(samples.good(),"Cannot write samples");}
    solver(argc>1?&samples:nullptr);std::cout<<"PASS portable Fourier and spatial solver\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
