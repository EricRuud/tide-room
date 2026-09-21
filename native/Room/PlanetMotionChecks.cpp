#include "PlanetMotion.h"
#include <iostream>
#include <stdexcept>
using tide::room::PlanetMotion;
void check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
float distance(PlanetMotion::Point a,PlanetMotion::Point b){float q=0;for(int i=0;i<3;++i)q+=(a[(size_t)i]-b[(size_t)i])*(a[(size_t)i]-b[(size_t)i]);return std::sqrt(q);}
int main(){try {
    const PlanetMotion::Positions initial{{{{-.8f,2,1}},{{.8f,2,1}},{{0,5,2}}}};auto crossing=initial;crossing[0][0]=.8f;crossing[1][0]=-.8f;
    for(double sr:{44100.,48000.,96000.}){PlanetMotion p;p.prepare(sr);p.process(initial,0,10,8);bool bounced=false,modulated=false,joined=false;float separation=100;
        for(int tick=0;tick<1440;++tick){p.process(crossing,(int)(sr/480),10,8);const auto& pos=p.positions();separation=std::min(separation,distance(pos[0],pos[1]));
            if(p.impacts()[0]>.03f){modulated=true;joined|=p.connections()[0]>.05f;bounced|=p.velocities()[0][0]<-.05f&&p.velocities()[1][0]>.05f;}
            check(pos[0][0]<pos[1][0],"Crossing targets tunnelled through the other planet");}
        check(separation>2*PlanetMotion::solidRadius-.0001f&&bounced&&modulated&&joined,"Collision did not separate, bounce, join and modulate");
        check(p.impactCount()==1,"Held contact repeatedly retriggered impacts");check(p.impacts()[0]<.001f&&p.connections()[0]<.001f,"Impact or water bridge stuck on");
        p.process(initial,(int)sr*2,10,8);check(p.impacts()[0]<.001f,"Separating planets triggered an impact");
        p.process(crossing,(int)sr,10,8);check(p.impactCount()==2,"Separated contact did not rearm");
        std::cout<<"PASS bounce, nonpenetration, transient CV, water join, contact latch/rearm at "<<sr<<" Hz\n";
    }
    PlanetMotion a,b;a.prepare(48000);b.prepare(48000);a.process(initial,0,10,8);b.process(initial,0,10,8);a.process(crossing,144013,10,8);
    for(int n=0;n<144013;n+=127)b.process(crossing,std::min(127,144013-n),10,8);
    check(a.positions()==b.positions()&&a.impacts()==b.impacts()&&a.impactCount()==b.impactCount(),"Physics clock depends on callback partition");
    PlanetMotion cluster;cluster.prepare(48000);auto target=initial;for(auto& p:target)p={1.6f,.4f,.3f};cluster.process(target,0,4,4,2.5f);
    for(int tick=0;tick<12000;++tick){for(size_t i=0;i<3;++i)target[i]={(float)std::sin(tick*.06+i)*1.6f,.4f+(float)(1+std::sin(tick*.073+i))*1.1f,.3f+(float)(1+std::cos(tick*.043+i))*.95f};cluster.process(target,100,4,4,2.5f);
        for(auto p:cluster.positions())for(float v:p)check(std::isfinite(v),"Nonfinite position in stress");
        for(auto p:cluster.positions())check(std::abs(p[0])<=2-PlanetMotion::waterRadius+.00001f&&p[1]>=PlanetMotion::waterRadius-.00001f&&p[1]<=4-PlanetMotion::waterRadius+.00001f&&p[2]>=PlanetMotion::waterRadius-.00001f&&p[2]<=2.5f-PlanetMotion::waterRadius+.00001f,"Enlarged water shell escaped the case");
        for(auto pair:PlanetMotion::pairs)check(distance(cluster.positions()[(size_t)pair[0]],cluster.positions()[(size_t)pair[1]])>2*PlanetMotion::solidRadius-.001f,"Triple collision penetrated");
    }
    std::cout<<"PASS fixed clock, overlap recovery, bounded fast motion and triple-contact stress\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
