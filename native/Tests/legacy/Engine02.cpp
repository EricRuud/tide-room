// Frozen Tide Room 0.2 engine for regression comparisons. Only namespace/include names changed.
#include "Engine02.h"
#include "Constants.h"
#include <TideAssets.h>
#include <algorithm>
#include <cmath>
#if JUCE_MAC
#include <Accelerate/Accelerate.h>
#endif

namespace tide02 {
namespace constants = tide::constants;
const std::array<Patch,patchCount> patches{{
    {"First light", "The original folded pluck, in a close space.", {.5f,0,.7f,.006f,.30f,0,.35f,.25f,-3,0}},
    {"Soft wood", "Short, rounded notes with a little grain.", {.26f,.12f,.22f,.003f,.19f,0,.17f,.18f,-2,0}},
    {"Glass current", "Bright folds that leave a soft, wide trail.", {.82f,.08f,.62f,.009f,.75f,.04f,.65f,.30f,-5,1}},
    {"Slow bloom", "Hold a chord and let the colour unfold.", {.43f,.18f,.58f,.65f,2.1f,.68f,1.6f,.34f,-7,1}},
    {"Low tide", "A steady body with gentle movement. Try low notes.", {.30f,.25f,.24f,.012f,.55f,.32f,.35f,.12f,-3,0}},
    {"Quiet wire", "A fine, slowly changing line with a lingering tail.", {.62f,.07f,.46f,.025f,1.1f,.16f,1.0f,.28f,-5,1}},
    {"LPG ping", "A bright strike that closes into a warm, rounded body.", {.64f,.08f,.32f,.002f,.20f,0,.25f,.08f,-3,0,1}},
    {"Bongo sun", "Hollow, springy hits. Try C2 to C4 and vary your touch.", {.30f,.18f,.22f,.002f,.105f,0,.15f,.04f,-2,0,2}},
    {"Dry twig", "Tight wooden ticks for quick, uneven patterns.", {.72f,.12f,.14f,.002f,.06f,0,.09f,.025f,-3,0,2}},
    {"Rubber pebble", "Low, soft knocks with a little pitch in the attack.", {.15f,.30f,.34f,.003f,.18f,0,.23f,.06f,-2,0,2}},
    {"Copper tine", "A metallic attack settles into a clear, singing note.", {.55f,.08f,.36f,.002f,.42f,0,.48f,.10f,-3,0,3}},
    {"Small bell", "Bright, delicate ringing. Leave space between strikes.", {.88f,.03f,.25f,.003f,.95f,0,1.1f,.10f,-4,1,3}},
    {"Seed sequence", "Fast, rubbery plucks that open up with velocity.", {.48f,.16f,.55f,.002f,.12f,0,.16f,.04f,-2,0,1}},
    {"Hollow marimba", "Round wooden notes with a longer, breathing decay.", {.26f,.07f,.48f,.004f,.38f,0,.45f,.08f,-2,0,1}}
}};

float Engine::fold(float phase, float d) noexcept {
    d=juce::jlimit(0.f,9.9f,d);
    const auto p=d*(2048.f/10.f);
    const auto i=(size_t)p;
    const float j=constants::j0[i]+(p-(float)i)*(constants::j0[i+1]-constants::j0[i]);
    return .35f*(std::sin(d*std::sin(phase)+.12f)-std::sin(.12f)*j);
}
float Engine::saturate(float x, float amount) noexcept {
    // Monotonic cubic soft saturation over the folder's bounded +/-0.392
    // output. A finite-degree curve gives a tractable harmonic tail; the
    // previous tanh chain failed high-register alias tests even at 32x.
    return x-2*amount*x*x*x;
}
float Engine::metal(float phase,float modPhase,float index,float folding) noexcept {
    // Parallel sources keep the cubic saturator's existing input bound.
    // A 7:5 modulation ratio gives non-integer partials relative to the note.
    return .72f*.35f*std::sin(phase+index*std::sin(modPhase))+.28f*fold(phase,folding);
}
void Engine::prepare(double sr,int block) {
    rate=sr; innerRate=sr*oversampling; maximum=std::max(1,block);
    dcAlpha=(float)(-std::expm1(-2*juce::MathConstants<double>::pi*2/sr));
    stealDecay=(float)std::exp(-1/(innerRate*.002));
    workClose.setSize(2,maximum); workBloom.setSize(2,maximum);
    close.loadImpulseResponse(TideAssets::close_wav,TideAssets::close_wavSize,
        juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::no,0,juce::dsp::Convolution::Normalise::no);
    bloom.loadImpulseResponse(TideAssets::bloom_wav,TideAssets::bloom_wavSize,
        juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::no,0,juce::dsp::Convolution::Normalise::no);
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)maximum,2};
    close.prepare(spec); bloom.prepare(spec);
    timbre.init(settings.timbre,innerRate,.025); drive.init(settings.drive,innerRate,.025);
    motion.init(settings.motion,innerRate,.04); attack.init(settings.attack,innerRate,.025);
    decay.init(settings.decay,innerRate,.025); sustain.init(settings.sustain,innerRate,.025);
    release.init(settings.release,innerRate,.025);
    wet.init(settings.space,rate,.04); mode.init((float)settings.spaceMode,rate,.08);
    output.init(juce::Decibels::decibelsToGain(settings.output),rate,.025);
    mod.init(0,innerRate,.04);
    for(auto& b:bend) b.init(0,innerRate,.012);
    reset();
}
void Engine::reset() {
    for(auto& v:voices) v=Voice{};
    pedals.fill(false); ring.fill(0); ringPosition=0; controlPosition=0; dc=0; clock=0; probePhase=probeModPhase=0; serial=0;
    for(auto& b:bend) b.value=b.target=0;
    mod.value=mod.target=0;
    close.reset(); bloom.reset(); panicRemaining=0;
}
void Engine::panic() noexcept {panicLength=std::max(1,(int)(rate*.005)); panicRemaining=panicLength;}
void Engine::setSettings(Settings s) {
    settings=s;
    timbre.target=juce::jlimit(0.f,1.f,s.timbre); drive.target=juce::jlimit(0.f,1.f,s.drive);
    motion.target=juce::jlimit(0.f,1.f,s.motion); attack.target=juce::jlimit(.002f,2.f,s.attack);
    decay.target=juce::jlimit(.06f,4.f,s.decay); sustain.target=juce::jlimit(0.f,1.f,s.sustain);
    release.target=juce::jlimit(.04f,4.f,s.release); wet.target=juce::jlimit(0.f,1.f,s.space);
    mode.target=s.spaceMode==0?0.f:1.f;
    output.target=juce::Decibels::decibelsToGain(juce::jlimit(-36.f,0.f,s.output));
}
void Engine::beginRelease(Voice& v) {
    if(!v.releasing) {v.releasing=true; v.releaseStart=v.env; v.releaseAge=0;}
}
void Engine::midi(const juce::MidiMessage& m) {
    const auto channel=juce::jlimit(1,16,m.getChannel());
    const auto ch=(size_t)(channel-1);
    if(m.isNoteOn()) {
        Voice* selected=nullptr;
        for(auto& v:voices) if(!v.active) {selected=&v; break;}
        if(selected==nullptr) selected=&*std::min_element(voices.begin(),voices.end(),[](auto& a,auto& b){
            if(a.releasing!=b.releasing) return a.releasing;
            return a.env<b.env;
        });
        const float tail=selected->active?selected->last:0;
        *selected=Voice{}; selected->active=selected->held=true;
        selected->articulation=juce::jlimit(0,3,settings.articulation);
        selected->struck=selected->articulation!=0&&settings.sustain<1.e-6f;
        selected->note=m.getNoteNumber(); selected->channel=channel;
        selected->frequency=440*std::exp2((selected->note-69)/12.);
        selected->serial=++serial; selected->velocity=m.getFloatVelocity(); selected->stealTail=tail;
        selected->driftPhase=(float)((serial%997)*.013);
        selected->lifeSine=(float)std::sin(.8*clock+208+selected->driftPhase);
    } else if(m.isNoteOff()) {
        // Release the oldest held instance of a repeated pitch, not every voice.
        Voice* selected=nullptr;
        for(auto& v:voices) if(v.active&&v.held&&v.note==m.getNoteNumber()&&v.channel==channel
            &&(selected==nullptr||v.serial<selected->serial)) selected=&v;
        if(selected) {selected->held=false; selected->pedalHeld=pedals[ch]&&!selected->struck; if(!pedals[ch]&&!selected->struck) beginRelease(*selected);}
    } else if(m.isPitchWheel()) {
        bend[ch].target=2.f*(m.getPitchWheelValue()-8192)/8192.f;
    } else if(m.isController()&&m.getControllerNumber()==1) {
        mod.target=m.getControllerValue()/127.f;
    } else if(m.isController()&&m.getControllerNumber()==64) {
        pedals[ch]=m.getControllerValue()>=64;
        if(!pedals[ch]) for(auto& v:voices) if(v.channel==channel&&v.pedalHeld) {v.pedalHeld=false; beginRelease(v);}
    } else if(m.isAllNotesOff()||m.isAllSoundOff()) {
        pedals[ch]=false;
        for(auto& v:voices) if(v.channel==channel) {
            v.held=v.pedalHeld=false;
            if(m.isAllSoundOff()) {v.releasing=false; v.fastRelease=true;}
            beginRelease(v);
        }
    }
}
void Engine::push(float x) {
    constexpr int n=(int)constants::decimator.size();
    ring[(size_t)ringPosition]=ring[(size_t)(ringPosition+n)]=x;
    if(++ringPosition==n) ringPosition=0;
}
float Engine::down() const {
    float result=0;
#if JUCE_MAC
    vDSP_dotpr(ring.data()+ringPosition,1,constants::decimator.data(),1,&result,constants::decimator.size());
#else
    for(size_t i=0;i<constants::decimator.size();++i) result+=ring[(size_t)ringPosition+i]*constants::decimator[i];
#endif
    return result;
}
float Engine::nextInner() {
    const float t=timbre.next(), d=drive.next(), mv=motion.next(), a=attack.next(), dec=decay.next();
    const float sus=sustain.next(), rel=release.next(), modulation=mod.next();
    std::array<float,16> bends; for(size_t i=0;i<bends.size();++i) bends[i]=bend[i].next();
    float sum=0, energy=0;
    for(auto& v:voices) {
        if(!v.active) continue;
        const float voiceSustain=v.struck?0.f:sus;
        // Envelope and very slow movement are evaluated at twice the host
        // rate, then linearly interpolated through the 16x nonlinear path.
        // The schedule is sample based, independent of host block sizes.
        if(controlPosition==0) {
            constexpr int interval=oversampling/2;
            const double dt=interval/innerRate, age=v.age+dt;
            const float end=v.releasing?v.releaseStart*(float)std::exp(-(v.releaseAge+dt)/(v.fastRelease?.003f:rel))
                :v.velocity*(float)(-std::expm1(-age/a))*(voiceSustain+(1-voiceSustain)*(float)std::exp(-age/dec));
            v.envStep=(end-v.env)/interval;
            v.lifeStep=((float)std::sin(.8*(clock+dt)+208+v.driftPhase)-v.lifeSine)/interval;
            const auto pitchBend=bends[(size_t)(v.channel-1)];
            double frequency=v.frequency*(std::abs(pitchBend)<1.e-12f?1:std::exp2(pitchBend/12.));
            if(v.articulation==2)frequency*=1+.65*v.velocity*std::exp(-age/.007);
            v.phaseIncrement=2*juce::MathConstants<double>::pi*std::min(frequency,rate*.45)/innerRate;
            if(v.articulation!=0) {
                // A perceptual low-pass-gate response, not a circuit model.
                // Two light-memory time constants soften the closing edge.
                const auto follow=[dt](float value,float target,double rise,double fall) {
                    return value+(target-value)*(float)(-std::expm1(-dt/(target>value?rise:fall)));
                };
                v.lightFast=follow(v.lightFast,end,.0008,.010+.020*mv);
                v.lightSlow=follow(v.lightSlow,end,.018,.060+.10*mv);
                const float light=.8f*v.lightFast+.2f*v.lightSlow;
                const float opening=light*light;
                const double cutoff=std::min(rate*.42,110+frequency*(1+(.8+9*t)*opening)+5000*opening);
                const float g=(float)(-std::expm1(-2*juce::MathConstants<double>::pi*cutoff/innerRate));
                v.filterStep=(g-v.filterG)/interval;
                const float index=(.25f+2.2f*t)*end;
                v.metalStep=(index-v.metalIndex)/interval;
            }
        }
        const float life=(mv+modulation*.6f)*.22f*v.lifeSine;
        const float folding=1.1f+(2.9f*v.env)*(.4f+1.2f*t)+life;
        float source=v.articulation==3?metal((float)v.phase,(float)v.modPhase,v.metalIndex,folding):fold((float)v.phase,folding);
        float sample=v.env*saturate(source,d);
        if(v.articulation!=0) {
            v.low1+=v.filterG*(sample-v.low1);v.low2+=v.filterG*(v.low1-v.low2);sample=v.low2;
            v.filterG+=v.filterStep;v.metalIndex+=v.metalStep;
            v.modPhase+=v.phaseIncrement*1.4;
            if(v.modPhase>=2*juce::MathConstants<double>::pi)v.modPhase-=2*juce::MathConstants<double>::pi;
        }
        sample+=v.stealTail; v.stealTail*=stealDecay;
        v.last=sample; sum+=sample; energy+=v.env;
        // Limit the fundamental below Nyquist; the decimator suppresses
        // out-of-band harmonics. No pitch-dependent tone compensation is hidden here.
        v.phase+=v.phaseIncrement;
        if(v.phase>=2*juce::MathConstants<double>::pi) v.phase-=2*juce::MathConstants<double>::pi;
        v.age+=1/innerRate;
        if(v.releasing) v.releaseAge+=1/innerRate;
        v.env=std::max(0.f,v.env+v.envStep); v.lifeSine+=v.lifeStep;
        if(v.env<1.e-7f&&v.age>.01&&(!v.held||voiceSustain<1.e-6f)
            &&(v.articulation==0||std::max(std::abs(v.low1),std::abs(v.low2))<1.e-7f)) v.active=false;
    }
    clock+=1/innerRate;
    if(++controlPosition==oversampling/2) controlPosition=0;
    // Continuous envelope-based headroom, before decimation. This avoids
    // abrupt gain steps when a voice enters or leaves the allocation pool.
    return sum/std::sqrt(1+energy*energy);
}
void Engine::render(float* l,float* r,int count) {
    juce::ScopedNoDenormals noDenormals;
    int offset=0;
    while(offset<count) {
        const int n=std::min(maximum,count-offset);
        for(int i=0;i<n;++i) {
            float mono=0;
            for(int j=0;j<oversampling;++j) {push(nextInner()); if(j==0) mono=down();}
            dc+=dcAlpha*(mono-dc); mono-=dc;
            l[offset+i]=r[offset+i]=mono;
            workClose.setSample(0,i,mono); workClose.setSample(1,i,mono);
            workBloom.setSample(0,i,mono); workBloom.setSample(1,i,mono);
        }
        auto c=juce::dsp::AudioBlock<float>(workClose).getSubBlock(0,(size_t)n);
        auto b=juce::dsp::AudioBlock<float>(workBloom).getSubBlock(0,(size_t)n);
        close.process(juce::dsp::ProcessContextReplacing<float>(c));
        bloom.process(juce::dsp::ProcessContextReplacing<float>(b));
        bool clearAfterBlock=false;
        for(int i=0;i<n;++i) {
            const float blend=mode.next(), wetAmount=wet.next(), amount=wetAmount*4;
            float gain=output.next()/(1+1.5f*wetAmount);
            if(panicRemaining>0) {
                gain*=(float)panicRemaining/(float)panicLength;
                if(--panicRemaining==0) clearAfterBlock=true;
            } else if(clearAfterBlock) gain=0;
            const float nearGain=(1-blend)*.1545857754f, bloomGain=blend*.2245478160f;
            l[offset+i]=(l[offset+i]+amount*(nearGain*c.getSample(0,i)+bloomGain*b.getSample(0,i)))*gain;
            r[offset+i]=(r[offset+i]+amount*(nearGain*c.getSample(1,i)+bloomGain*b.getSample(1,i)))*gain;
        }
        if(clearAfterBlock) reset();
        offset+=n;
    }
}
int Engine::activeVoices() const noexcept {int n=0; for(auto& v:voices) n+=v.active?1:0; return n;}
float Engine::oscillatorProbe(double f,float d,float saturation) {
    float result=0;
    for(int j=0;j<oversampling;++j) {
        push(saturate(fold((float)probePhase,d),saturation)); if(j==0) result=down();
        probePhase+=2*juce::MathConstants<double>::pi*f/innerRate;
        if(probePhase>=2*juce::MathConstants<double>::pi) probePhase-=2*juce::MathConstants<double>::pi;
    }
    return result;
}
float Engine::metalProbe(double f,float index,float folding,float saturation) {
    float result=0;
    for(int j=0;j<oversampling;++j) {
        push(saturate(metal((float)probePhase,(float)probeModPhase,index,folding),saturation));if(j==0)result=down();
        probePhase+=2*juce::MathConstants<double>::pi*f/innerRate;
        probeModPhase+=2*juce::MathConstants<double>::pi*f*1.4/innerRate;
        if(probePhase>=2*juce::MathConstants<double>::pi)probePhase-=2*juce::MathConstants<double>::pi;
        if(probeModPhase>=2*juce::MathConstants<double>::pi)probeModPhase-=2*juce::MathConstants<double>::pi;
    }
    return result;
}
}
