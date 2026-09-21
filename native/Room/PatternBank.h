#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <initializer_list>

namespace tide::room {
constexpr uint32_t rhythm(std::initializer_list<int> hits) {
    uint32_t mask=0;for(int step:hits)mask|=uint32_t{1}<<step;return mask;
}
constexpr int countHits(uint32_t mask) {int count=0;while(mask){mask&=mask-1;++count;}return count;}
struct PatternPart {
    int length;uint32_t mask;
    std::array<int,8> degrees;int noteCount;
    const char* role;
};
struct PatternDefinition {
    const char* name;char shortcut;const char* description;
    std::array<PatternPart,3> parts;
    double drift;int accentPeriod;
};
inline constexpr std::array<PatternDefinition,8> patternBank{{
    {"Original",'Q',"The original anchored pulse, interlocking pluck and slowly phasing upper line.",{{
        {8,rhythm({0,2,4,6}),{{0,4,7,4}},4,"An anchored pulse"},
        {12,rhythm({0,3,5,8,10}),{{7,9,11,8,10,9}},6,"Interlocking accents"},
        {16,rhythm({1,4,7,10,14}),{{14,11,13,15,10,14,13}},7,"A slow phase drift"}
    }},.003,4},
    {"Offbeat",'W',"Displaced bass accents and short, answering syncopations.",{{
        {16,rhythm({0,6,8,14}),{{0,4,0,7}},4,"A displaced pulse"},
        {16,rhythm({2,5,9,12,15}),{{7,11,9,7,10,8}},6,"Offbeat fragments"},
        {16,rhythm({1,7,10,13}),{{14,13,11,9,12,10,11,13}},8,"Answering accents"}
    }},.002,4},
    {"Spiral",'R',"Rising melodic fragments crossing different cycle lengths.",{{
        {12,rhythm({0,4,7,10}),{{0,4,7,4}},4,"A turning foundation"},
        {15,rhythm({0,3,6,9,12}),{{7,8,9,11,12,11,9,8}},8,"A rising fragment"},
        {20,rhythm({2,5,9,12,16,18}),{{10,11,12,13,14,15,16,14}},8,"A longer ascent"}
    }},.004,4},
    {"Answer",'T',"Open bass notes, a short call and a reply in the gaps; no additional pattern phase drift.",{{
        {16,rhythm({0,8}),{{0,7,4,0}},4,"An open foundation"},
        {16,rhythm({0,3,6,8,11,14}),{{7,9,11,9,8,7}},6,"A repeated call"},
        {16,rhythm({4,7,12,15}),{{14,13,11,10}},4,"A reply in the gaps"}
    }},0,4},
    {"Threefold",'Y',"Dotted pulses and alternating groups over twelve, eighteen and twenty-four steps.",{{
        {12,rhythm({0,3,6,9}),{{0,4,7,4}},4,"A dotted pulse"},
        {18,rhythm({0,4,6,10,12,16}),{{7,9,11,8,10,12}},6,"Alternating groups"},
        {24,rhythm({2,5,8,11,14,17,20,23}),{{14,11,13,10,12,9}},6,"A displaced dotted line"}
    }},.0015,3},
    {"Sparse",'U',"Longer spaces, isolated plucks and a widely spaced upper melody.",{{
        {16,rhythm({0,8}),{{0,4}},2,"A spacious pulse"},
        {20,rhythm({3,11,17}),{{9,7,11,8,10,7}},6,"Isolated fragments"},
        {24,rhythm({6,15,22}),{{14,11,13,9}},4,"A distant reply"}
    }},.001,4},
    {"Cascade",'I',"Descending figures, paired pickups and a faster-moving upper cycle.",{{
        {16,rhythm({0,4,8,12}),{{0,7,4,0}},4,"A steady foundation"},
        {12,rhythm({0,2,3,6,8,9}),{{14,12,11,9,8,7}},6,"Descending pickups"},
        {18,rhythm({1,4,7,10,13,16}),{{18,16,14,13,11,10}},6,"A falling upper line"}
    }},.005,4},
    {"Crossings",'O',"Three contrasting cycles with interwoven rising and falling intervals.",{{
        {16,rhythm({0,3,8,11}),{{0,4,7,4}},4,"Uneven paired accents"},
        {15,rhythm({0,3,6,9,12}),{{7,11,8,12,9,13,11}},7,"Crossing intervals"},
        {21,rhythm({1,5,8,12,15,19}),{{14,11,15,12,16,13,17}},7,"An interwoven reply"}
    }},.0035,4}
}};
inline constexpr const PatternDefinition& selectedPattern(int index) noexcept {return patternBank[(std::size_t)std::clamp(index,0,7)];}
}
