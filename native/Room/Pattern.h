#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "Harmony.h"
#include "PatternBank.h"

namespace tide::room {
constexpr int partCount=3;
struct PatternSettings {double bpm=108;float evolution=.45f;uint32_t seed=31415;std::array<float,3> density{{1,.88f,.78f}};Harmony harmony;int bank=0;};
struct NoteEvent {int offset=0,part=0,note=60,step=0;float velocity=0;bool on=false;};
struct Events {std::array<NoteEvent,128> data{};int size=0;};
// Sample deadlines and a seeded cycle hash make the score independent of the
// host buffer size. Only the two supporting parts change their motif/phase.
class Pattern {
public:
    void reset(double sr) {rate=sr;frame=0;next.fill(0);step.fill(0);off.fill(-1);notes.fill(0);previousBpm=108;}
    Events advance(int frames,const PatternSettings& s) {
        Events result;const int bank=std::clamp(s.bank,0,7);const auto& selected=selectedPattern(bank);
        if(s.bpm!=previousBpm) {for(auto& n:next)n=(double)frame+(n-(double)frame)*previousBpm/s.bpm;previousBpm=s.bpm;}
        const uint64_t end=frame+(uint64_t)frames;
        for(;;) {
            int part=-1;bool isOff=false;int64_t when=INT64_MAX;
            for(int i=0;i<3;++i) {
                const auto t=(int64_t)std::llround(next[(size_t)i]);
                if(t<when){when=t;part=i;isOff=false;}
                if(off[(size_t)i]>=0&&off[(size_t)i]<=when){when=off[(size_t)i];part=i;isOff=true;}
            }
            if(when>=(int64_t)end||part<0)break;
            const auto p=(size_t)part;const int offset=std::max(0,(int)(when-(int64_t)frame));
            if(isOff){emit(result,{offset,part,notes[p],0,0,false});off[p]=-1;continue;}
            const auto& line=selected.parts[p];const int length=line.length;const uint64_t cycle=step[p]/(uint64_t)length;
            const int rotation=part==0?0:(int)((cycle/8)*(uint64_t)(s.evolution>.25f?1:0))%length;
            const int index=((int)(step[p]%(uint64_t)length)+rotation)%length;
            const uint32_t random=hash(s.seed^(uint32_t)(cycle*131+index*19+part*7919));
            const float probability=part==0?1.f:1-s.evolution*.12f;
            const bool hit=(line.mask&(1u<<index))!=0&&(float)(random&65535)/65536.f<s.density[p]*probability;
            if(hit) {
                // Keep the original motif indexing exactly. Other banks follow
                // scheduled onsets, so sparse rhythms can use the whole melody;
                // density drops notes without moving the remaining pitches.
                uint64_t phrase=step[p]/2;
                if(bank==0){if(part>0)phrase+=cycle/(part==1?4u:6u);}
                else phrase=cycle*(uint64_t)countHits(line.mask)+(uint64_t)countHits(line.mask&((1u<<index)-1));
                const int degree=line.degrees[(size_t)(phrase%(uint64_t)line.noteCount)];
                int note=s.harmony.note(degree);
                if(part>0&&s.evolution>.65f&&((random>>16)%7)==0)note-=12;
                const float accent=index%selected.accentPeriod==0?1.f:.82f;
                const float velocity=part==0?.66f*accent:(part==1?.76f:.62f)*accent*(.88f+.20f*(float)((random>>16)&255)/255.f);
                // A faster tempo can bring an adjacent onset ahead of the old
                // gate deadline. Release its actual pitch before replacing it.
                if(off[p]>=0)emit(result,{offset,part,notes[p],0,0,false});
                emit(result,{offset,part,note,index,velocity,true});notes[p]=note;
                off[p]=when+(int64_t)std::llround(rate*60/s.bpm*.125);
            }
            ++step[p];const double drift=part==2?1+selected.drift*s.evolution:1.;
            next[p]+=rate*60/(s.bpm*4*drift);
        }
        frame=end;return result;
    }
    uint64_t position() const {return frame;}
private:
    static uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
    static void emit(Events& events,NoteEvent event){if(events.size<(int)events.data.size())events.data[(size_t)events.size++]=event;}
    double rate=48000,previousBpm=108;
    uint64_t frame=0;
    std::array<double,3> next{};
    std::array<uint64_t,3> step{};
    std::array<int64_t,3> off{{-1,-1,-1}};
    std::array<int,3> notes{};
};
}
