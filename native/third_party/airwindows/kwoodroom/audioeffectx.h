// SPDX-License-Identifier: MIT
// Original minimal native adapter, not a VST SDK. The Airwindows sources are
// compiled unchanged; Tide calls their DSP directly and never loads a plugin.
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
