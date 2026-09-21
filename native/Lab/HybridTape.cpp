// Experimental fork: dynamic brightness filtering before magnetic saturation.
// SPDX-License-Identifier: GPL-3.0-only
#include "HybridTape.h"

namespace tide::lab {
void HybridTape::prepare(double sr,int block,int order) {
    peakField=0;
    rate=sr;maximum=block;innerRate=sr*(1<<order);
    oversampler=std::make_unique<juce::dsp::Oversampling<float>>(2);oversampler->clearOversamplingStages();
    // Keep image rejection strong at every stage. The library's convenience
    // preset relaxes rejection in later stages, which can feed images into
    // the nonlinearity and fold them back into the audible band.
    for(int stage=0;stage<order;++stage)oversampler->addOversamplingStage(juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,stage==0?.04f:.1f,-120,stage==0?.04f:.1f,-120);
    oversampler->setUsingIntegerLatency(true);
    oversampler->initProcessing((size_t)block);oversampler->reset();latencySamples=juce::roundToInt(oversampler->getLatencyInSamples());
    jassert(latencySamples<4096);dry.setSize(2,block);wet.setSize(2,block);delayIndex=0;processing=false;
    for(auto& c:dryDelay)c.fill(0);preLow.fill(0);postInput.fill(0);postOutput.fill(0);dc.fill(0);toneLow.fill(0);
    prePole=std::exp(-2*juce::MathConstants<double>::pi*2500/innerRate);dcPole=std::exp(-2*juce::MathConstants<double>::pi*5/innerRate);
    for(auto& magnet:magnets){magnet.reset();magnet.configure(settings.saturation,settings.bias);}
    const float values[]={settings.drive,settings.saturation,settings.bias,settings.warmth,settings.mix};
    for(size_t i=0;i<controls.size();++i){controls[i].reset(innerRate,.04);controls[i].setCurrentAndTargetValue(values[i]);}
    brightness.prepare(innerRate,settings.brightness);soften.reset(innerRate,.04);soften.setCurrentAndTargetValue(settings.soften);
    blend.reset(rate,.04);blend.setCurrentAndTargetValue(settings.enabled?settings.mix:0);
}
void HybridTape::setSettings(TapeSettings s) {
    soften.setTargetValue(juce::jlimit(0.f,1.f,s.soften));
    settings=s;const float values[]={juce::jlimit(0.f,18.f,s.drive),juce::jlimit(0.f,1.f,s.saturation),juce::jlimit(.25f,.95f,s.bias),juce::jlimit(0.f,1.f,s.warmth),juce::jlimit(0.f,1.f,s.mix)};
    for(size_t i=0;i<controls.size();++i)controls[i].setTargetValue(values[i]);
    blend.setTargetValue(s.enabled?values[4]:0);
}
void HybridTape::process(juce::AudioBuffer<float>& buffer) {
    juce::ScopedNoDenormals noDenormals;
    for(int offset=0;offset<buffer.getNumSamples();offset+=maximum){const int n=std::min(maximum,buffer.getNumSamples()-offset);
        for(int i=0;i<n;++i){for(int c=0;c<2;++c){const float input=buffer.getSample(c,offset+i);dryDelay[(size_t)c][(size_t)delayIndex]=input;
            dry.setSample(c,i,dryDelay[(size_t)c][(size_t)((delayIndex-latencySamples+4096)%4096)]);wet.setSample(c,i,input);}
            delayIndex=(delayIndex+1)%4096;
        }
        if(blend.getCurrentValue()==0&&!blend.isSmoothing()){
            soften.skip(n*(int)oversampler->getOversamplingFactor());
            for(auto& control:controls)control.skip(n*(int)oversampler->getOversamplingFactor());
            for(int c=0;c<2;++c)buffer.copyFrom(c,offset,dry,c,0,n);processing=false;continue;
        }
        if(!processing){brightness.reset();oversampler->reset();for(auto& magnet:magnets)magnet.reset(false);preLow.fill(0);postInput.fill(0);postOutput.fill(0);dc.fill(0);toneLow.fill(0);processing=true;}
        auto block=juce::dsp::AudioBlock<float>(wet).getSubBlock(0,(size_t)n);auto high=oversampler->processSamplesUp(block);
        const bool changing=controls[0].isSmoothing()||controls[1].isSmoothing()||controls[2].isSmoothing()||controls[3].isSmoothing();
        double drive=1,shelf=1,b0=1,b1=0,tonePole=0,normalization=1;
        auto cook=[&]{drive=std::pow(10.,controls[0].getNextValue()/20.);
            const double saturation=controls[1].getNextValue(),bias=controls[2].getNextValue(),warmth=controls[3].getNextValue();controls[4].getNextValue();
            shelf=1+.8*warmth;b0=1+(shelf-1)*prePole;b1=-shelf*prePole;
            tonePole=std::exp(-2*juce::MathConstants<double>::pi*(20000-12000*warmth)/innerRate);
            for(auto& magnet:magnets)magnet.configure(saturation,bias);normalization=drive*magnets[0].smallSignalGain();
        };
        // Stable controls need one coefficient calculation per block, while
        // automation retains the original per-sample smoothing trajectory.
        if(!changing)cook();
        for(size_t i=0;i<high.getNumSamples();++i){if(changing)cook();
            std::array<double,2> field;
            for(size_t ch=0;ch<2;++ch){double x=high.getSample((int)ch,(int)i);
                preLow[ch]=(1-prePole)*x+prePole*preLow[ch];x=shelf*x+(1-shelf)*preLow[ch];field[ch]=x*drive;}
            brightness.process(field,soften.getNextValue());
            peakField=std::max(peakField,std::max(std::abs(field[0]),std::abs(field[1])));
            for(size_t c=0;c<2;++c){auto& magnet=magnets[c];
                const double recorded=magnet.process(field[c])/normalization;
                // Exact inverse of the record shelf for fixed coefficients.
                const double replay=(recorded-prePole*postInput[c]-b1*postOutput[c])/b0;postInput[c]=recorded;postOutput[c]=replay;
                dc[c]=(1-dcPole)*replay+dcPole*dc[c];const double ac=replay-dc[c];
                toneLow[c]=(1-tonePole)*ac+tonePole*toneLow[c];high.setSample((int)c,(int)i,(float)toneLow[c]);
            }
        }
        oversampler->processSamplesDown(block);
        for(int i=0;i<n;++i){const float amount=blend.getNextValue();for(int c=0;c<2;++c){const float input=dry.getSample(c,i);buffer.setSample(c,offset+i,input+amount*(wet.getSample(c,i)-input));}}
    }
}
}
