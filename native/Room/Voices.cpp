#include "Voices.h"

namespace tide::room {
const std::array<tide::Patch,patchCount> patches=[] {
    std::array<tide::Patch,patchCount> bank{};
    std::copy(tide::patches.begin(),tide::patches.end(),bank.begin());
    // Ring/AM/complex modes are original synthesis recipes inspired by
    // Buchla's oscillator-modulation and low-pass-gate vocabulary.
    bank[14]={"Ring bronze","A struck bronze ring with clear inharmonic sidebands.",{.78f,.10f,.38f,.002f,.55f,0,.65f,.08f,-3,0,4,1.41421356f,1.f}};
    bank[15]={"Twin chime","Two bright sidebands hang above a quiet low beating tone.",{.92f,.03f,.22f,.003f,1.25f,0,1.4f,.10f,-4,1,4,2.41421356f,1.f}};
    bank[16]={"Sideband wood","A short hollow ring strike with an audible wooden aftertone.",{.54f,.16f,.42f,.002f,.16f,0,.22f,.04f,-2,0,4,.75f,.92f}};
    bank[17]={"Orbit bells","Slowly drifting ring-modulated bells with a long tail.",{.70f,.05f,.85f,.004f,.85f,0,1.f,.12f,-4,1,4,1.51f,.96f}};
    bank[18]={"AM reeds","A pitched body surrounded by nasal amplitude-modulation tones.",{.72f,.12f,.50f,.005f,.48f,0,.55f,.08f,-2,0,5,1.4f,1.f}};
    bank[19]={"Folded gong","Deep oscillator modulation and ring sidebands open into a gong.",{.82f,.08f,.45f,.003f,1.6f,0,1.8f,.10f,-4,1,6,2.73f,1.f}};
    bank[20]={"Cross current","An elastic burst of complex-oscillator sidebands.",{.65f,.14f,.72f,.002f,.24f,0,.32f,.05f,-3,0,6,.61803399f,.88f}};
    bank[21]={"Radio glass","Thin bright ring tones with a fast, brittle attack.",{1.f,.02f,.25f,.002f,.32f,0,.42f,.06f,-3,0,4,3.73f,1.f}};
    return bank;
}();
}
