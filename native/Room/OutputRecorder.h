#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// One take. push() and requestStop() run under the processor's callback lock;
// all file operations after construction belong to the background thread.
class OutputRecorder final : private juce::Thread {
public:
    enum class Error { none, overflow, disk };
    struct State {
        bool recording=false, saving=false, finished=false, deviceStopped=false;
        double seconds=0;
        juce::File file;
        juce::String error;
    };
    static std::unique_ptr<OutputRecorder> create(const juce::File&,double,juce::String&);
    // Stream entry point also permits deterministic disk-failure/FIFO checks.
    static std::unique_ptr<OutputRecorder> createStream(std::unique_ptr<juce::OutputStream>,
                                                       const juce::File&,double,int,juce::String&);
    ~OutputRecorder() override;
    void push(const juce::AudioBuffer<float>&) noexcept;
    void requestStop(bool deviceStopped=false) noexcept;
    State state() const;
    bool finished() const noexcept {return complete.load(std::memory_order_acquire);}
private:
    OutputRecorder(const juce::File&,double,int);
    void run() override;
    void fail(Error) noexcept;
    const juce::File file;
    const double rate;
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::OutputStream* stream=nullptr; // Owned by writer until run() finalises it.
    std::atomic<bool> stopping{false},complete{false},deviceChanged{false};
    std::atomic<Error> error{Error::none};
    std::atomic<int64_t> accepted{0},written{0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRecorder)
};
