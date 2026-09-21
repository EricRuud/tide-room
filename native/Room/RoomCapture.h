#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <stdexcept>
#include <vector>

// Opt-in developer capture. All memory is allocated before publication to the
// callback; audio workers only copy into disjoint channels. No file I/O in DSP.
struct RoomCapture {
    struct Frame { int sample=0; std::array<float,9> target{},hosted{}; std::array<double,3> engineTime{}; float load=0; };
    struct Note {int sample=0,part=0,note=0;float velocity=0;bool on=false;};
    RoomCapture(const juce::File& directory,double sr):folder(directory),rate(sr),audio(36,(int)(sr*12)) {
        audio.clear();frames.resize((size_t)(audio.getNumSamples()/512+8));notes.resize(4096);
    }
    void tap(int channel,const juce::AudioBuffer<float>& b,int count) {
        const int n=std::min(count,audio.getNumSamples()-offset);
        if(n>0)for(int c=0;c<2;++c)audio.copyFrom(channel+c,offset,b,c,0,n);
    }
    void note(int position,int part,int pitch,float velocity,bool on) {
        if(offset+position<audio.getNumSamples()&&noteCount<notes.size())notes[noteCount++]={offset+position,part,pitch,velocity,on};
    }
    void source(int index,bool after,const juce::AudioBuffer<float>& b) {
        const int n=std::min(b.getNumSamples(),audio.getNumSamples()-offset);
        if(n<=0)return;
        for(int c=0;c<2;++c)audio.copyFrom(2+index*4+(after?2:0)+c,offset,b,c,0,n);
    }
    void output(const juce::AudioBuffer<float>& b,Frame frame) {
        const int n=std::min(b.getNumSamples(),audio.getNumSamples()-offset);
        if(n<=0)return;
        for(int c=0;c<2;++c)audio.copyFrom(c,offset,b,c,0,n);
        frame.sample=offset;if(frameCount<frames.size())frames[frameCount++]=frame;
        offset+=n;if(offset==audio.getNumSamples())complete.store(true);
    }
    void early(int index,const juce::AudioBuffer<float>& b,int count) {
        const int n=std::min(count,audio.getNumSamples()-offset);
        if(n>0)for(int c=0;c<2;++c)audio.copyFrom(14+2*index+c,offset,b,c,0,n);
    }
    void save() {
        folder.createDirectory();
        juce::WavAudioFormat format;
        auto stream=folder.getChildFile("live.wav").createOutputStream();
        if(!stream||!stream->setPosition(0)||stream->truncate().failed())throw std::runtime_error("Cannot create diagnostic WAV");
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,36,32,{},0));
        if(!writer||!writer->writeFromAudioSampleBuffer(audio,0,offset))throw std::runtime_error("Cannot save diagnostic capture");
        writer.reset();
        juce::String csv="sample,load";
        for(const char* prefix:{"target","hosted"})for(int i=0;i<9;++i)csv+=","+juce::String(prefix)+juce::String(i);
        for(int i=0;i<3;++i)csv+=",engineTime"+juce::String(i);
        csv+="\n";
        for(size_t k=0;k<frameCount;++k){const auto& f=frames[k];csv+=juce::String(f.sample)+","+juce::String(f.load,9);for(const auto* values:{&f.target,&f.hosted})for(float x:*values)csv+=","+juce::String(x,9);for(double time:f.engineTime)csv+=","+juce::String(time,12);csv+="\n";}
        folder.getChildFile("positions.csv").replaceWithText(csv);
        csv="sample,part,note,velocity,on\n";
        for(size_t k=0;k<noteCount;++k){const auto& n=notes[k];csv+=juce::String(n.sample)+","+juce::String(n.part)+","+juce::String(n.note)+","+juce::String(n.velocity,9)+","+juce::String(n.on?1:0)+"\n";}
        folder.getChildFile("notes.csv").replaceWithText(csv);
        folder.getChildFile("parameters-start.xml").replaceWithText(parametersStart);
        folder.getChildFile("parameters-end.xml").replaceWithText(parametersEnd);
        folder.getChildFile("performance.json").replaceWithText(performance);
#if TIDE_NATIVE_WOOD
        folder.getChildFile("channels.txt").replaceWithText("Sample rate: "+juce::String(rate)+"\nChannels (zero indexed): 0/1 final output; 2/3, 6/7, 10/11 dry synth inputs; 4/5, 8/9, 12/13 positioned/propagated sends before source gain into the ONE shared native reverb; 14/15, 16/17, 18/19 geometric early reflections; 20/21, 22/23, 24/25 binaural direct before propagation; 26/27, 28/29, 30/31 direct+early after propagation before source gain; 32/33 mixed direct+early+shared tail before tape; 34/35 after tape before master. Hosted coordinates are zero because no external reverb is loaded.\n");
#else
        folder.getChildFile("channels.txt").replaceWithText("Sample rate: "+juce::String(rate)+"\nChannels (zero indexed): 0/1 final output; 2/3 source 1 input; 4/5 source 1 Reverside output; 6/7 source 2 input; 8/9 source 2 Reverside output; 10/11 source 3 input; 12/13 source 3 Reverside output; 14/15 source 1 moving reflections; 16/17 source 2 moving reflections; 18/19 source 3 moving reflections; 20/21, 22/23, 24/25 source direct paths before common propagation; 26/27, 28/29, 30/31 source sums after propagation before source gains; 32/33 mixed bus before tape; 34/35 after tape before master. Captured in the running device callback before the operating system output.\n");
#endif
    }
    juce::File folder;double rate;
    juce::AudioBuffer<float> audio;
    std::vector<Frame> frames;
    std::vector<Note> notes;
    size_t noteCount=0;
    juce::String parametersStart,parametersEnd,performance;
    size_t frameCount=0;int offset=0;
    std::atomic<bool> complete{false};
};
