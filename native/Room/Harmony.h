#pragma once
#include <array>
#include <algorithm>

namespace tide::room {
struct Harmony {
    int key=2,minor=1,chord=0; // D natural minor, first degree: original room
    static constexpr std::array<int,7> majorScale{{0,2,4,5,7,9,11}};
    static constexpr std::array<int,7> minorScale{{0,2,3,5,7,8,10}};
    int interval(int degree) const noexcept {
        const auto& scale=minor?minorScale:majorScale;
        const int octave=degree>=0?degree/7:(degree-6)/7;
        return 12*octave+scale[(std::size_t)(degree-7*octave)];
    }
    int note(int motifDegree) const noexcept {
        return 48+std::clamp(key,0,11)+interval(motifDegree+std::clamp(chord,0,6));
    }
    int chordRoot() const noexcept {return (std::clamp(key,0,11)+interval(std::clamp(chord,0,6)))%12;}
    int third() const noexcept {return interval(chord+2)-interval(chord);}
    int fifth() const noexcept {return interval(chord+4)-interval(chord);}
};
}
