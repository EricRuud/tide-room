#include "SurfaceWater.h"
#include <iostream>
#include <stdexcept>

void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() {
    try {
        for(int identity=0;identity<3;++identity) {
            SurfaceWater water;water.prepare(identity);const auto rest=water.centroid();const double volume=water.volume();
            for(int step=0;step<1200;++step)water.step(0,0);
            check(water.maximumSpeed()<1.e-5f,"A level resting lake generated currents");
            for(int step=0;step<1440;++step)water.step(.8f,0);
            const auto right=water.centroid();check(right[0]>rest[0]+.03f,"Positive force did not move water right");
            for(int step=0;step<1440;++step)water.step(-.8f,0);
            const auto left=water.centroid();check(left[0]<right[0]-.06f,"Water did not flow back after force reversal");
            for(int step=0;step<12000;++step){water.step((float)std::sin(step*.023)*1.f,(float)std::cos(step*.031)*1.f);check(std::isfinite(water.volume())&&water.minimumDepth()>-1.e-7f,"Nonfinite or negative water during force stress");}
            const double error=std::abs(water.volume()-volume)/volume;
            check(error<1.e-5,"Closed surface lost or created water");
            for(int step=0;step<6000;++step)water.step(0,0);
            check(water.maximumSpeed()<.004f,"Water did not settle after forces stopped");
            std::cout<<"SURFACE "<<identity<<" volume_relative_error="<<error<<" x_rest="<<rest[0]<<" x_right="<<right[0]<<" x_left="<<left[0]<<" settled_speed="<<water.maximumSpeed()<<"\n";
        }
        SurfaceWater a,b;a.prepare(0);b.prepare(0);
        for(int i=0;i<300;++i)a.advance(1./30,.5f,-.3f);
        for(int i=0;i<1000;++i)b.advance(.01,.5f,-.3f);
        check(a.volume()==b.volume()&&a.centroid()==b.centroid(),"Fixed-step flow depends on render frame partition");
        std::cout<<"PASS resting equilibrium, force direction/reversal, conserved volume, wet/dry positivity, finite stress, settling and frame partitions\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
