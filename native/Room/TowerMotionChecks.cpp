#include "PlanetMotion.h"
#include "TowerNotes.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace tide::room;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    for(float width:{4.f,10.f,18.f})for(float depth:{4.f,8.f,18.f})for(float height:{2.5f,4.7f,8.f}){
        PlanetMotion motion;motion.prepare(48000);const float elevation=height*towerHeightRatio;
        PlanetMotion::Positions target{{{{0,depth*.4f,elevation}},{{0,depth*.4f,elevation}},{{0,depth*.4f,elevation}}}};
        for(int block=0;block<360;++block){for(int i=0;i<3;++i){target[(size_t)i][0]=width*.45f*std::sin((float)block*.10f+(float)i*.17f);target[(size_t)i][1]=depth*(.38f+.4f*std::sin((float)block*.08f));target[(size_t)i][2]=(float)i;}
            const auto& p=motion.process(target,512,width,depth,height,.89f,.25f,elevation);
            for(int i=0;i<3;++i){check(p[(size_t)i][2]==elevation&&motion.velocities()[(size_t)i][2]==0,"Collision or stale target changed fixed height");
                check(std::abs(p[(size_t)i][0])<=width*.5f-towerEnvelopeRadius+.001f,"Tower left side walls");check(p[(size_t)i][1]>=towerEnvelopeRadius-.001f&&p[(size_t)i][1]<=depth-towerEnvelopeRadius+.001f,"Tower left front/back walls");}
            for(auto pair:PlanetMotion::pairs){auto a=p[(size_t)pair[0]],b=p[(size_t)pair[1]];float dx=a[0]-b[0],dy=a[1]-b[1];check(std::sqrt(dx*dx+dy*dy)>2*PlanetMotion::solidRadius-.002f,"Solid plates interpenetrated");}
        }
    }
    PatternSettings settings;settings.evolution=1;
    for(int bank=0;bank<8;++bank)for(int key=0;key<12;++key)for(int minor=0;minor<2;++minor)for(int chord=0;chord<7;++chord){settings.bank=bank;settings.harmony={key,minor,chord};
        for(int part=0;part<3;++part){auto notes=TowerNotes::forPattern(settings,part);check(notes.count>0&&notes.count<=16,"Invalid plate count");
            for(int i=1;i<notes.count;++i){check(notes.pitches[(size_t)i]>notes.pitches[(size_t)i-1],"Duplicate or unsorted pitches");check(notes.radius(i)<notes.radius(i-1),"Higher note is not a smaller plate");check(notes.elevation(i)>notes.elevation(i-1),"Higher note is not above lower note");}
            for(float height:{2.5f,4.7f,8.f}){
                check(notes.worldElevation(0,height)-notes.halfThickness()*notes.roomScale(height)*towerRadius-.14f>.07f,"Water clips the floor");
                check(notes.worldElevation(notes.count-1,height)+notes.halfThickness()*notes.roomScale(height)*towerRadius<listenerHeight(height),"Listener is below the top plate");
            }}
    }
    std::cout<<"PASS constrained collisions across 27 room sizes at fixed golden height; distinct, ordered pitch plates across all patterns, keys, scales and chords\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
