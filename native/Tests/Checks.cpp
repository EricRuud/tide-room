#include "Processor.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <stdexcept>

using Event=std::pair<int,juce::MidiMessage>;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
double rms(const juce::AudioBuffer<float>& b,int start,int n) {
    double sum=0; for(int c=0;c<b.getNumChannels();++c) for(int i=start;i<start+n;++i) sum+=(double)b.getSample(c,i)*b.getSample(c,i);
    return std::sqrt(sum/(n*b.getNumChannels()));
}
juce::AudioBuffer<float> render(tide::Settings s,double sr,int block,int total,const std::vector<Event>& events) {
    tide::Engine e; e.setSettings(s); e.prepare(sr,256);
    require(e.getCloseIRSize()>0&&e.getBloomIRSize()>0,"IR must be ready before the first audio callback");
    juce::AudioBuffer<float> b(2,total); b.clear(); size_t event=0;
    for(int pos=0;pos<total;) {
        while(event<events.size()&&events[event].first==pos) e.midi(events[event++].second);
        int n=std::min(block,total-pos);
        if(event<events.size()) n=std::min(n,events[event].first-pos);
        require(n>0,"Events must be sorted and valid");
        e.render(b.getWritePointer(0)+pos,b.getWritePointer(1)+pos,n); pos+=n;
    }
    for(int c=0;c<2;++c) for(int i=0;i<total;++i) require(std::isfinite(b.getSample(c,i)),"Non-finite render");
    return b;
}
void writeWav(const juce::File& path,const juce::AudioBuffer<float>& b,double sr) {
    path.getParentDirectory().createDirectory();
    auto stream=path.createOutputStream(); require(stream!=nullptr,"Cannot open WAV output");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),sr,(unsigned)b.getNumChannels(),24,{},0));
    require(writer!=nullptr&&writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"Cannot write WAV output");
}
std::vector<Event> phrase(double sr,bool pad=false) {
    std::vector<Event> events;
    if(pad) {
        for(int n:{48,55,60,64}) events.emplace_back(0,juce::MidiMessage::noteOn(1,n,.65f));
        for(int n:{48,55,60,64}) events.emplace_back((int)(2.8*sr),juce::MidiMessage::noteOff(1,n));
    } else {
        const int notes[]={45,52,57,50,55,48}; const float velocities[]={.6f,.85f,.7f,1.f,.65f,.9f};
        for(int i=0;i<6;++i) {
            events.emplace_back((int)(i*.8*sr),juce::MidiMessage::noteOn(1,notes[i],velocities[i]));
            events.emplace_back((int)((i*.8+.62)*sr),juce::MidiMessage::noteOff(1,notes[i]));
        }
    }
    std::stable_sort(events.begin(),events.end(),[](auto&a,auto&b){return a.first<b.first;}); return events;
}
void interactionChecks() {
    TideProcessor p;p.setCurrentProgram(3);p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    p.keyboardState.noteOn(1,60,.8f);
    float peak=0;
    for(int i=0;i<100;++i) {p.processBlock(block,midi);peak=std::max(peak,block.getMagnitude(0,256));}
    require(peak>.01f&&p.sounding.load()==1,"On-screen keyboard injection failed");
    p.keyboardState.noteOff(1,60,0);p.panic();
    for(int i=0;i<5;++i) p.processBlock(block,midi);
    require(block.getMagnitude(0,256)<1.e-12f,"Keyboard panic failed");
    std::cout<<"PASS on-screen keyboard state reaches the audio processor\n";
    tide::Engine a,b;auto s=tide::patches[3].values;s.space=0;
    a.setSettings(s);b.setSettings(s);a.prepare(48000,256);b.prepare(48000,256);
    a.midi(juce::MidiMessage::noteOn(1,60,.8f));b.midi(juce::MidiMessage::noteOn(1,60,.8f));
    juce::AudioBuffer<float> other(2,256);
    for(int i=0;i<100;++i) {a.render(block.getWritePointer(0),block.getWritePointer(1),256);b.render(other.getWritePointer(0),other.getWritePointer(1),256);}
    s.timbre=1;s.drive=1;s.attack=.002f;s.decay=.06f;s.sustain=1;s.space=1;s.spaceMode=1;s.output=0;
    b.setSettings(s);
    a.render(block.getWritePointer(0),block.getWritePointer(1),256);b.render(other.getWritePointer(0),other.getWritePointer(1),256);
    require(std::abs(block.getSample(0,0)-other.getSample(0,0))<.0001f,"Parameter step caused an immediate output jump");
    std::cout<<"PASS smoothed parameter-change boundary (not an aliasing proof)\n";
}
void checks() {
    const int sr=48000;
    auto s=tide::patches[0].values;
    auto silent=render(s,sr,256,sr/2,{}); require(silent.getMagnitude(0,silent.getNumSamples())==0,"Silence must stay silent");
    std::cout<<"PASS silence and synchronous IR readiness\n";
    auto events=phrase(sr); auto base=render(s,sr,256,sr*2,{{0,events[0].second},{sr/2,juce::MidiMessage::noteOff(1,45)}});
    for(int block:{1,17,511,2048}) {
        auto other=render(s,sr,block,sr*2,{{0,events[0].second},{sr/2,juce::MidiMessage::noteOff(1,45)}});
        float error=0; for(int c=0;c<2;++c) for(int i=0;i<sr*2;++i) error=std::max(error,std::abs(base.getSample(c,i)-other.getSample(c,i)));
        require(error<2.e-5f,"Block-size-dependent output");
    }
    std::cout<<"PASS block partition invariance (1, 17, 256, 511, 2048)\n";
    s.space=0;s.sustain=.7f;s.release=.06f;
    auto pedal=render(s,sr,127,sr*2,{{0,juce::MidiMessage::noteOn(1,57,.8f)},
        {sr/10,juce::MidiMessage::controllerEvent(1,64,127)},{sr/5,juce::MidiMessage::noteOff(1,57)},
        {sr,juce::MidiMessage::controllerEvent(1,64,0)}});
    require(rms(pedal,sr/2,sr/4)>.01,"Sustain pedal failed to hold");
    require(rms(pedal,sr*17/10,sr/5)<1.e-5,"Sustain pedal failed to release");
    std::cout<<"PASS sustain-pedal hold and release\n";
    auto repeated=render(s,sr,64,sr,{{0,juce::MidiMessage::noteOn(1,60,.7f)},
        {sr/10,juce::MidiMessage::noteOn(1,60,.7f)},{sr/5,juce::MidiMessage::noteOff(1,60)},
        {sr*2/5,juce::MidiMessage::noteOff(1,60)}});
    require(rms(repeated,sr/4,sr/10)>.01,"Repeated note off released every voice");
    require(rms(repeated,sr*9/10,sr/10)<.0001,"Repeated note stuck");
    std::cout<<"PASS overlapping instances of the same note\n";
    s.release=4;
    auto stopped=render(s,sr,127,sr,{{0,juce::MidiMessage::noteOn(1,60,.8f)},
        {sr/2,juce::MidiMessage::allSoundOff(1)}});
    require(rms(stopped,sr*9/10,sr/10)<1.e-5,"All sound off must bypass long release");
    std::cout<<"PASS MIDI all-sound-off with maximum release\n";
    TideProcessor processor;processor.setCurrentProgram(3);juce::MemoryBlock state;processor.getStateInformation(state);
    processor.setCurrentProgram(1);processor.setStateInformation(state.getData(),(int)state.getSize());
    require(processor.getCurrentProgram()==3&&!processor.programModified(),"Preset state did not round-trip");
    const char bad[]="not a plugin state";processor.setStateInformation(bad,sizeof(bad));
    require(processor.getCurrentProgram()==3,"Malformed state changed preset");
    processor.prepareToPlay(sr,256);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,60,.8f),200);processor.processBlock(block,midi);
    require(block.getMagnitude(0,190)<1.e-7f,"MIDI note rendered before its timestamp");
    std::cout<<"PASS plugin state, invalid state, MIDI timestamp\n";
    for(int i=0;i<100;++i) processor.processBlock(block,midi);
    processor.panic();
    for(int i=0;i<5;++i) processor.processBlock(block,midi);
    require(block.getMagnitude(0,256)==0&&processor.sounding.load()==0,"Quiet must clear voices and space");
    std::cout<<"PASS Quiet fade and tail reset\n";
    for(double rate:{44100.,48000.,96000.}) {
        s=tide::patches[3].values;s.output=0;s.space=1;s.drive=1;s.sustain=1;s.attack=.002f;s.release=.1f;
        std::vector<Event> stress;
        for(int i=0;i<16;++i) stress.emplace_back(i*43,juce::MidiMessage::noteOn(1,36+i*4,1.f));
        stress.emplace_back((int)rate,juce::MidiMessage::allNotesOff(1));
        auto audio=render(s,rate,127,(int)(rate*2),stress);
        const auto peak=audio.getMagnitude(0,audio.getNumSamples());
        std::cout<<"STRESS "<<rate<<" peak "<<peak<<"\n";
        require(peak<.98f,"Extreme polyphony/drive/space clips");
    }
    std::cout<<"PASS voice stealing and extreme settings at 44.1/48/96 kHz\n";
    float unisonPeak=0;
    for(int character:{0,1}) for(float saturation:{0.f,.5f,1.f}) for(int note:{24,36,48,60,72,84,96,108,120,127}) {
        s.spaceMode=character;s.drive=saturation;s.timbre=1;
        std::vector<Event> unison;
        for(int i=0;i<8;++i) unison.emplace_back(0,juce::MidiMessage::noteOn(1,note,1.f));
        auto audio=render(s,48000,256,48000,unison);
        const auto peak=audio.getMagnitude(0,audio.getNumSamples());
        if(peak>unisonPeak) {unisonPeak=peak;std::cout<<"UNISON peak="<<peak<<" note="<<note<<" saturation="<<saturation<<" space="<<character<<"\n";}
    }
    require(unisonPeak<.98f,"Eight-voice unison clips");
    std::cout<<"PASS 60 full-level unison/space/drive stress cases\n";
    for(int character:{0,1}) for(int note=12;note<128;++note) {
        s.spaceMode=character;s.drive=0;s.timbre=1;
        std::vector<Event> unison;
        for(int i=0;i<8;++i) unison.emplace_back(0,juce::MidiMessage::noteOn(1,note,1.f));
        auto audio=render(s,48000,256,48000,unison);
        unisonPeak=std::max(unisonPeak,audio.getMagnitude(0,audio.getNumSamples()));
        require(unisonPeak<.98f,"Chromatic unison sweep clips");
    }
    std::cout<<"PASS 232 chromatic unison stress cases; overall peak="<<unisonPeak<<"\n";
    interactionChecks();
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        if(argc>2&&juce::String(argv[1])=="--render") {
            const juce::File dir=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);dir.createDirectory();
            require(!dir.getChildFile("01-first-light.wav").existsAsFile(),"Refusing to overwrite renders");
            int index=0;
            for(const auto& p:tide::patches) {
                const auto start=std::chrono::steady_clock::now();
                const int length=index==3?24:index==5?20:14;
                auto audio=render(p.values,48000,256,48000*length,phrase(48000,index==3));
                const juce::String name=juce::String(++index).paddedLeft('0',2)+"-"+juce::String(p.name).toLowerCase().replaceCharacter(' ','-')+".wav";
                const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                writeWav(dir.getChildFile(name),audio,48000);
                std::cout<<name<<" peak="<<audio.getMagnitude(0,audio.getNumSamples())<<" render_seconds="<<seconds<<"\n";
            }
        } else if(argc>2&&juce::String(argv[1])=="--probes") {
            const juce::File dir=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);dir.createDirectory();
            for(int sr:{44100,48000}) for(int bin:{17,83,281,887,1583,2411,2813}) for(float drive:{1.3f,3.f,5.5f,9.f}) for(float saturation:{0.f,.5f,1.f}) {
                tide::Engine engine;engine.prepare(sr,256);
                juce::AudioBuffer<float> audio(1,8192);
                const double frequency=(double)sr*bin/8192;
                for(int i=0;i<4096;++i) engine.oscillatorProbe(frequency,drive,saturation);
                for(int i=0;i<8192;++i) audio.setSample(0,i,engine.oscillatorProbe(frequency,drive,saturation));
                auto path=dir.getChildFile(juce::String(sr)+"-"+juce::String(bin)+"-"+juce::String(drive,1)+"-"+juce::String(saturation,1)+".wav");
                writeWav(path,audio,sr);
            }
        } else if(argc>1&&juce::String(argv[1])=="--interactions") interactionChecks();
        else checks();
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
