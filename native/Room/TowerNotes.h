#pragma once
#include "Pattern.h"
#include "RoomGeometry.h"

namespace tide::room {
// Pitches, not step numbers: repeated pitches share a plate; octave variants
// have their own plates. Sorting makes low notes the larger, lower discs.
struct TowerNotes {
    static constexpr int capacity=16;
    std::array<int,capacity> pitches{};
    int count=0;
    void add(int pitch){
        if(pitch<0||pitch>127||std::find(pitches.begin(),pitches.begin()+count,pitch)!=pitches.begin()+count)return;
        if(count<capacity){pitches[(size_t)count++]=pitch;std::sort(pitches.begin(),pitches.begin()+count);}
    }
    static TowerNotes forPattern(const PatternSettings& settings,int part){
        TowerNotes notes;const auto& phrase=selectedPattern(settings.bank).parts[(size_t)part];
        for(int i=0;i<phrase.noteCount;++i){const int pitch=settings.harmony.note(phrase.degrees[(size_t)i]);notes.add(pitch);if(part>0&&settings.evolution>.65f)notes.add(pitch-12);}
        return notes;
    }
    float spacing()const{return std::min(.50f,2.64f/(float)std::max(1,count));}
    float elevation(int plate)const{return ((float)plate-(float)(count-1)*.5f)*spacing();}
    float radius(int plate)const{return count<=1?1.f:1.f-.62f*(float)plate/(float)(count-1);}
    int index(int pitch)const{const auto found=std::find(pitches.begin(),pitches.begin()+count,pitch);return found==pitches.begin()+count?-1:(int)(found-pitches.begin());}
    float roomScale(float roomHeight)const {
        const float halfHeight=(elevation(std::max(0,count-1))+halfThickness())*towerRadius;
        return std::clamp((roomHeight*towerHeightRatio-.22f)/std::max(.01f,halfHeight),.1f,1.f);
    }
    float worldElevation(int plate,float roomHeight)const{return roomHeight*towerHeightRatio+elevation(plate)*roomScale(roomHeight)*towerRadius;}
    float halfThickness()const{return std::min(.156f,spacing()*.32f);}
};
}
