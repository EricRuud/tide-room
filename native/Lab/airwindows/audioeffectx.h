// SPDX-License-Identifier: MIT
// Minimal original offline shim, not a VST SDK implementation.
// Keeps upstream ToTape9 constructor and DSP source unchanged for reference renders.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
using VstInt32=int32_t;
using audioMasterCallback=void*;
enum VstPlugCategory {kPlugCategEffect};
constexpr int kVstMaxProgNameLen=24,kVstMaxParamStrLen=8,kVstMaxProductStrLen=64,kVstMaxVendorStrLen=64;
inline void vst_strncpy(char* dst,const char* src,int length){std::strncpy(dst,src,(size_t)length);dst[length]=0;}
inline void float2string(float value,char* dst,int length){std::snprintf(dst,(size_t)length,"%.4f",value);}
class AudioEffect {public:virtual ~AudioEffect()=default;};
class AudioEffectX:public AudioEffect {
public:
    AudioEffectX(audioMasterCallback,int,int){}
    double getSampleRate() const {return sampleRate;}
    void setSampleRate(double r){sampleRate=r;}
    void setNumInputs(int){} void setNumOutputs(int){} void setUniqueID(unsigned long){}
    void canProcessReplacing(){} void canDoubleReplacing(){} void programsAreChunks(bool){}
private:double sampleRate=44100;
};
