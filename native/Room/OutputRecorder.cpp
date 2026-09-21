#include "OutputRecorder.h"
#include <cmath>

OutputRecorder::OutputRecorder(const juce::File& f,double sr,int capacity)
    :Thread("Tide recording writer"),file(f),rate(sr),fifo(capacity+1),buffer(2,capacity+1) {}

std::unique_ptr<OutputRecorder> OutputRecorder::create(const juce::File& file,double rate,juce::String& error) {
    if(!std::isfinite(rate)||rate<8000||rate>384000){error="Cannot record with this audio sample rate.";return {};}
    if(file.exists()){error="That file already exists. Choose a new filename.";return {};}
    if(!file.getParentDirectory().isDirectory()){error="The recording folder is unavailable.";return {};}
    auto stream=file.createOutputStream();
    if(!stream||stream->failedToOpen()){error="Cannot open the recording file. Check the folder and available disk space.";return {};}
    return createStream(std::move(stream),file,rate,(int)std::ceil(rate*8),error);
}

std::unique_ptr<OutputRecorder> OutputRecorder::createStream(std::unique_ptr<juce::OutputStream> stream,
    const juce::File& file,double rate,int capacity,juce::String& error) {
    if(!stream||!std::isfinite(rate)||rate<8000||rate>384000||capacity<1){error="Cannot record with these audio settings.";return {};}
    auto result=std::unique_ptr<OutputRecorder>(new OutputRecorder(file,rate,capacity));
    result->stream=stream.get();
    juce::WavAudioFormat format;
    result->writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withChannelLayout(juce::AudioChannelSet::stereo())
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if(!result->writer||!result->writer->isFloatingPoint()||!result->writer->flush()){
        error="Could not create the stereo WAV file.";return {};
    }
    if(!result->startThread(juce::Thread::Priority::normal)){error="Could not start the recording writer.";return {};}
    error.clear();return result;
}

OutputRecorder::~OutputRecorder(){requestStop();notify();waitForThreadToExit(-1);}

void OutputRecorder::fail(Error problem) noexcept {
    auto expected=Error::none;error.compare_exchange_strong(expected,problem);
    stopping.store(true,std::memory_order_release);
}

void OutputRecorder::requestStop(bool deviceStopped) noexcept {
    if(deviceStopped&&!stopping.load())deviceChanged.store(true);
    stopping.store(true,std::memory_order_release);
}

void OutputRecorder::push(const juce::AudioBuffer<float>& audio) noexcept {
    if(stopping.load(std::memory_order_acquire)||audio.getNumChannels()<2)return;
    const int count=audio.getNumSamples();
    if(count>fifo.getFreeSpace()){fail(Error::overflow);return;}
    int start1=0,size1=0,start2=0,size2=0;
    fifo.prepareToWrite(count,start1,size1,start2,size2);
    for(int c=0;c<2;++c){buffer.copyFrom(c,start1,audio,c,0,size1);if(size2>0)buffer.copyFrom(c,start2,audio,c,size1,size2);}
    fifo.finishedWrite(size1+size2);accepted.fetch_add(count,std::memory_order_relaxed);
}

void OutputRecorder::run() {
    int64_t sinceFlush=0;
    for(;;){
        // Check stopping before inspecting the FIFO: after a stop it is stable.
        const bool stop=stopping.load(std::memory_order_acquire);
        if(error.load()==Error::disk)break;
        const int count=std::min(8192,fifo.getNumReady());
        if(count==0){if(stop)break;wait(5);continue;}
        int start1=0,size1=0,start2=0,size2=0;fifo.prepareToRead(count,start1,size1,start2,size2);
        auto write=[&](int start,int size){
            if(size==0)return true;
            const float* channels[]={buffer.getReadPointer(0,start),buffer.getReadPointer(1,start)};
            if(!writer->writeFromFloatArrays(channels,2,size)){fail(Error::disk);return false;}
            written.fetch_add(size,std::memory_order_relaxed);sinceFlush+=size;return true;
        };
        const bool ok=write(start1,size1)&&write(start2,size2);fifo.finishedRead(count);
        if(!ok)break;
        if(sinceFlush>=(int64_t)rate){
            if(!writer->flush()){fail(Error::disk);break;}
            stream->flush();
            if(auto* fileStream=dynamic_cast<juce::FileOutputStream*>(stream);fileStream&&fileStream->getStatus().failed()){fail(Error::disk);break;}
            sinceFlush=0;
        }
    }
    if(!writer->flush())fail(Error::disk);
    stream->flush();
    if(auto* fileStream=dynamic_cast<juce::FileOutputStream*>(stream);fileStream&&fileStream->getStatus().failed())fail(Error::disk);
    writer.reset();stream=nullptr;
    complete.store(true,std::memory_order_release);
}

OutputRecorder::State OutputRecorder::state() const {
    State result;result.file=file;result.finished=finished();result.recording=!stopping.load();result.saving=!result.recording&&!result.finished;
    result.deviceStopped=deviceChanged.load();result.seconds=(double)(result.finished?written.load():accepted.load())/rate;
    switch(error.load()){
        case Error::none:break;
        case Error::overflow:result.error="Recording stopped: disk writer fell behind. The partial take was kept.";break;
        case Error::disk:result.error="Recording stopped: file write failed. Check disk space; the take may be incomplete.";break;
    }
    return result;
}
