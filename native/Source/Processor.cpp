#include "Processor.h"
#include "Editor.h"

namespace {
constexpr const char* ids[]={"timbre","drive","motion","attack","decay","sustain","release","space","output","spaceMode","articulation"};
}
juce::AudioProcessorValueTreeState::ParameterLayout TideProcessor::layout() {
    using F=juce::AudioParameterFloat;
    using R=juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<F>(juce::ParameterID{"timbre",1},"Timbre",R(0,1,.001f),.5f));
    p.add(std::make_unique<F>(juce::ParameterID{"drive",1},"Drive",R(0,1,.001f),0));
    p.add(std::make_unique<F>(juce::ParameterID{"motion",1},"Motion",R(0,1,.001f),.7f));
    p.add(std::make_unique<F>(juce::ParameterID{"attack",1},"Attack",R(.002f,2,.001f,.3f),.006f));
    p.add(std::make_unique<F>(juce::ParameterID{"decay",1},"Decay",R(.06f,4,.001f,.4f),.30f));
    p.add(std::make_unique<F>(juce::ParameterID{"sustain",1},"Sustain",R(0,1,.001f),0));
    p.add(std::make_unique<F>(juce::ParameterID{"release",1},"Release",R(.04f,4,.001f,.4f),.35f));
    p.add(std::make_unique<F>(juce::ParameterID{"space",1},"Space amount",R(0,1,.001f),.25f));
    p.add(std::make_unique<F>(juce::ParameterID{"output",1},"Output",R(-36,0,.1f),-3));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"spaceMode",1},"Space character",juce::StringArray{"Close","Bloom"},0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"articulation",1},"Voice response",juce::StringArray{"Flow","Pluck","Knock","Metal"},0));
    return p;
}
TideProcessor::TideProcessor():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),
    parameters(*this,nullptr,"TideState",layout()) {
    for(size_t i=0;i<values.size();++i) values[i]=parameters.getRawParameterValue(ids[i]);
}
tide::Settings TideProcessor::readSettings() const {
    return {values[0]->load(),values[1]->load(),values[2]->load(),values[3]->load(),values[4]->load(),
            values[5]->load(),values[6]->load(),values[7]->load(),values[8]->load(),(int)values[9]->load(),(int)values[10]->load()};
}
bool TideProcessor::programModified() const {
    const auto a=readSettings(), b=tide::patches[(size_t)program.load()].values;
    return std::abs(a.timbre-b.timbre)>.002f||std::abs(a.drive-b.drive)>.002f||std::abs(a.motion-b.motion)>.002f
        ||std::abs(a.attack-b.attack)>.002f||std::abs(a.decay-b.decay)>.002f||std::abs(a.sustain-b.sustain)>.002f
        ||std::abs(a.release-b.release)>.002f||std::abs(a.space-b.space)>.002f||std::abs(a.output-b.output)>.11f||a.spaceMode!=b.spaceMode||a.articulation!=b.articulation;
}
void TideProcessor::prepareToPlay(double sr,int block) {
#if TIDE_EFFECT_HOST
    auto s=readSettings();finalGain=juce::Decibels::decibelsToGain(s.output);
    finalGainCoefficient=(float)(-std::expm1(-1/(sr*.025)));
    s.output=0;if(!effects.usesInternalSpace())s.space=0;
    engine.setSettings(s);engine.prepare(sr,std::max(32,block));effects.prepare(sr,std::max(32,block));
#else
    engine.setSettings(readSettings()); engine.prepare(sr,std::max(32,block));
#endif
    setLatencySamples(tide::Engine::latency);
}
void TideProcessor::releaseResources() {
#if TIDE_EFFECT_HOST
    effects.releaseResources();
#endif
}
bool TideProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainInputChannelSet().isDisabled();
}
void TideProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    b.clear();
    if(b.getNumChannels()<2) return;
    auto s=readSettings();
#if TIDE_EFFECT_HOST
    const float gainTarget=juce::Decibels::decibelsToGain(s.output);
    s.output=0;if(!effects.usesInternalSpace())s.space=0;
#endif
    engine.setSettings(s);
    keyboardState.processNextMidiBuffer(midi,0,b.getNumSamples(),true);
    if(panicRequested.exchange(false)) {
        engine.panic(); midi.clear();
#if TIDE_EFFECT_HOST
        effects.quiet();
#endif
    }
    int position=0;
    for(const auto event:midi) {
        const int next=juce::jlimit(position,b.getNumSamples(),event.samplePosition);
        engine.render(b.getWritePointer(0)+position,b.getWritePointer(1)+position,next-position);
#if TIDE_EFFECT_HOST
        if(event.getMessage().isNoteOn())effects.resumeOnNote();
#endif
        engine.midi(event.getMessage()); position=next;
    }
    engine.render(b.getWritePointer(0)+position,b.getWritePointer(1)+position,b.getNumSamples()-position);
#if TIDE_EFFECT_HOST
    effects.process(b);
    for(int i=0;i<b.getNumSamples();++i) {
        finalGain+=finalGainCoefficient*(gainTarget-finalGain);
        b.setSample(0,i,b.getSample(0,i)*finalGain);b.setSample(1,i,b.getSample(1,i)*finalGain);
    }
#endif
    sounding.store(engine.activeVoices());
    const float measured=b.getMagnitude(0,b.getNumSamples());
    peak.store(std::max(measured,peak.load()*.93f));
    midi.clear();
}
void TideProcessor::setCurrentProgram(int i) {
    i=juce::jlimit(0,tide::patchCount-1,i); program.store(i);
    const auto s=tide::patches[(size_t)i].values;
    const float data[]={s.timbre,s.drive,s.motion,s.attack,s.decay,s.sustain,s.release,s.space,s.output,(float)s.spaceMode,(float)s.articulation};
    for(size_t k=0;k<values.size();++k) {
        auto* p=parameters.getParameter(ids[k]);
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(data[k])); p->endChangeGesture();
    }
}
void TideProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto state=parameters.copyState(); state.setProperty("program",program.load(),nullptr);
#if TIDE_EFFECT_HOST
    state.removeChild(state.getChildWithName("ExternalEffect"),nullptr);
    state.addChild(effects.saveState(),-1,nullptr);
#endif
    if(auto xml=state.createXml()) copyXmlToBinary(*xml,data);
}
void TideProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(parameters.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);
        program.store(juce::jlimit(0,tide::patchCount-1,(int)state.getProperty("program",0)));
        // Earlier session files must restore the original voice, even when
        // loaded over a new percussion patch in the current session.
        if(!state.getChildWithProperty("id","articulation").isValid()) {
            juce::ValueTree voice("PARAM");voice.setProperty("id","articulation",nullptr);voice.setProperty("value",0,nullptr);state.addChild(voice,-1,nullptr);
        }
        parameters.replaceState(state);
#if TIDE_EFFECT_HOST
        auto effect=state.getChildWithName("ExternalEffect");
        effects.restoreState(effect.isValid()?effect:juce::ValueTree("ExternalEffect"));
#endif
    }
}
juce::AudioProcessorEditor* TideProcessor::createEditor() {return new TideEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {return new TideProcessor();}
