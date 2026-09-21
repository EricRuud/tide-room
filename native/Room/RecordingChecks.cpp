#include "RoomEditor.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void waitFor(OutputRecorder& recorder){for(int i=0;i<1000&&!recorder.finished();++i)juce::Thread::sleep(5);check(recorder.finished(),"Writer did not finish");}
void waitFor(RoomProcessor& p){for(int i=0;i<1000&&!p.recordingState().finished;++i)juce::Thread::sleep(5);check(p.recordingState().finished,"Processor recording did not finish");check(p.recordingState().error.isEmpty(),"Processor recording error");}
void compare(const juce::File& file,const juce::AudioBuffer<float>& expected,double rate){
    juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
    check(reader!=nullptr,"Cannot read saved recording");
    check(reader->usesFloatingPointData&&reader->bitsPerSample==32&&reader->numChannels==2,"Not stereo 32-bit float WAV");
    check(reader->sampleRate==rate&&reader->lengthInSamples==expected.getNumSamples(),"Wrong rate or take length");
    juce::AudioBuffer<float> actual(2,expected.getNumSamples());check(reader->read(&actual,0,actual.getNumSamples(),0,true,true),"Cannot read take samples");
    for(int c=0;c<2;++c)for(int n=0;n<actual.getNumSamples();++n)check(actual.getSample(c,n)==expected.getSample(c,n),"Recorded sample differs from output");
}
void snapshot(RoomProcessor& p,const juce::File& file){RoomEditor editor(p);juce::PNGImageFormat png;auto stream=file.createOutputStream();check(stream&&png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()),*stream),"Recorder UI snapshot");}
struct FaultStream final : juce::OutputStream {
    explicit FaultStream(std::atomic<bool>& fail):broken(fail){}
    void flush() override {}
    juce::int64 getPosition() override {return data.getPosition();}
    bool setPosition(juce::int64 p) override {return data.setPosition(p);}
    bool write(const void* p,size_t n) override {return !broken.load()&&data.write(p,n);}
    std::atomic<bool>& broken;juce::MemoryOutputStream data;
};
void recorderChecks(const juce::File& out){
    juce::String error;
    for(double rate:{44100.,48000.,96000.}){
        const int count=(int)rate*3+317;juce::AudioBuffer<float> audio(2,count);
        for(int n=0;n<count;++n){audio.setSample(0,n,2.5f*(float)std::sin(n*.0137));audio.setSample(1,n,-1.75f*(float)std::cos(n*.031));}
        const auto file=out.getChildFile("float-"+juce::String((int)rate)+".wav");auto recorder=OutputRecorder::create(file,rate,error);check(recorder!=nullptr,"Start float take");
        for(int n=0;n<count;){const int size=std::min(count-n,n%3==0?17:n%3==1?127:2048);juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,n,size);recorder->push(block);n+=size;}
        recorder->requestStop();waitFor(*recorder);check(recorder->state().error.isEmpty(),"Unexpected recorder error");compare(file,audio,rate);
        const auto size=file.getSize();check(!OutputRecorder::create(file,rate,error)&&file.getSize()==size,"Existing take overwritten");
    }
    const auto file=out.getChildFile("quit.wav");juce::AudioBuffer<float> audio(2,1027);audio.clear();audio.setSample(1,1026,1.25f);
    {auto recorder=OutputRecorder::create(file,48000,error);check(recorder!=nullptr,"Quit take start");recorder->push(audio);}compare(file,audio,48000);
    auto overflow=OutputRecorder::createStream(std::make_unique<juce::MemoryOutputStream>(),{},48000,32,error);check(overflow!=nullptr,"Overflow fixture");overflow->push(audio);waitFor(*overflow);check(overflow->state().error.contains("fell behind")&&overflow->state().seconds==0,"Overflow not reported");
    std::atomic<bool> broken{false};auto failure=OutputRecorder::createStream(std::make_unique<FaultStream>(broken),{},48000,4096,error);check(failure!=nullptr,"Disk-failure fixture");broken=true;failure->push(audio);waitFor(*failure);check(failure->state().error.contains("write failed"),"Disk failure not reported");
    check(!OutputRecorder::create(out.getChildFile("missing/take.wav"),48000,error),"Missing folder should fail");
    std::cout<<"PASS float WAV exact samples (including levels above 0 dBFS), stereo order, rates, irregular blocks, finalisation, no overwrite, FIFO and disk failures\n";
}
void render(RoomProcessor& p,juce::AudioBuffer<float>& audio,int begin,int count,bool changing=false){
    juce::MidiBuffer midi;
    for(int n=0;n<count;){const int size=std::min(count-n,n%3==0?127:n%3==1?512:2048);
        if(changing){p.set("output",-9+(float)((n/10000)%3)*3);p.set("harmonyChord",(float)((n/10000)%7));p.set("patternBank",(float)((n/10000)%8));}
        juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(),2,begin+n,size);{const juce::ScopedLock lock(p.getCallbackLock());p.processBlock(block,midi);}n+=size;
    }
}
void integration(const juce::File& out){
    RoomProcessor p(false);p.setRateAndBufferSizeDetails(48000,512);p.prepareToPlay(48000,512);p.set("tapeOn",0);
    std::cout<<"Checking full bus, live controls, tails and sleeping silence...\n";
    for(int mode=0;mode<2;++mode){
        p.set("tapeOn",(float)mode);p.set("tapeModel",4);p.set("wornNoise",.3f);p.set("lfo0_on",1);p.set("lfo0_depth",.8f);p.updateRoom();
        const auto file=out.getChildFile(mode?"full-mix-tape.wav":"full-mix.wav");juce::String error;
        check(p.startRecording(file,error),"Cannot start processor recording");
        check(!p.startRecording(out.getChildFile("duplicate.wav"),error),"Allowed simultaneous takes");
        juce::AudioBuffer<float> audio(2,48000*14);render(p,audio,0,48000);
        p.play();render(p,audio,48000,48000*3,true);snapshot(p,out.getChildFile(mode?"recording.png":"recording-dry.png"));
        check(p.recordingState().recording&&p.isPlaying(),"Editor disrupted recording");
        p.stop();render(p,audio,48000*4,48000*10);check(p.recordingState().recording,"Transport Stop ended recording");
        check(audio.getMagnitude(0,48000,48000*3)>.001f,"Music missing");check(audio.getMagnitude(0,48000*4,48000)>.00001f,"Reverb tail missing");
        check(p.sleepingForTest()&&audio.getMagnitude(0,48000*13,48000)==0,"Dormant silence fixture failed");
        p.stopRecording();waitFor(p);compare(file,audio,48000);snapshot(p,out.getChildFile(mode?"saved-tape.png":"saved-dry.png"));
    }
    juce::String error;const auto deviceFile=out.getChildFile("device-stop.wav");check(p.startRecording(deviceFile,error),"Device stop start");
    juce::AudioBuffer<float> audio(2,2048);render(p,audio,0,2048);p.releaseResources();waitFor(p);check(p.recordingState().deviceStopped,"Device change reason missing");compare(deviceFile,audio,48000);
    check(!p.startRecording(out.getChildFile("device-unavailable.wav"),error),"Recorded without audio device");
    p.setRateAndBufferSizeDetails(44100,512);p.prepareToPlay(44100,512);check(p.startRecording(out.getChildFile("new-rate.wav"),error),"Restart after rate change");p.play();render(p,audio,0,2048);p.stopRecording();waitFor(p);compare(out.getChildFile("new-rate.wav"),audio,44100);
    juce::MemoryBlock state;p.getStateInformation(state);RoomProcessor restored(false);restored.setStateInformation(state.getData(),(int)state.getSize());check(!restored.recordingState().recording,"Recording resumed on scene load");
    std::cout<<"PASS exact full-mix capture with tape off/on, moving sources, output/key/pattern changes, tails, dormant silence, editor lifecycle and device-rate restart\n";
}
}
int main(int argc,char* argv[]){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;
    try{check(argc==2,"Usage: TideRecordingCheck NEW_OUTPUT");juce::File out(argv[1]);check(!out.exists(),"Use a fresh test directory");check(out.createDirectory().wasOk(),"Create test directory");recorderChecks(out);integration(out);return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
