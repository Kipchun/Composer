// Real-time output through WASAPI, plus the sequencer that drives each track.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Instrument.h"
#include "PlayNote.h"

class AudioEngine {
public:
    static constexpr int kBlock = 512;
    static constexpr double kTempo = 96;

    explicit AudioEngine(int tracks);
    ~AudioEngine();

    double SampleRate() const { return sampleRate_; }
    bool HasDevice() const { return hasDevice_; }

    // Returns the previous instrument so the UI thread, not the audio thread, frees it.
    std::unique_ptr<Instrument> SetInstrument(int track, std::unique_ptr<Instrument> instrument);

    void SetSchedule(const std::vector<PlayNote>& notes, double totalBeats);
    void Play();
    void Pause();
    void Stop();
    void Seek(double beats);
    void SetLoop(bool on);
    void SetClick(bool on);
    void Preview(int track, int pitch);

    double Beats() const;
    bool IsPlaying() const { return playing_; }
    float TakePeak(int track);  // level since the last call, 0 to 1

private:
    struct Scheduled {
        int64_t sample;
        int track;
        int pitch;
        bool on;
    };
    struct Sounding {
        int track, pitch, framesLeft;
    };

    void ThreadMain(std::promise<void> ready);
    bool OpenAndRun(std::promise<void>& ready, bool& signalled);
    void RenderBlock();
    void Emit(int track, int offset, int pitch, bool on);
    void Mix();
    void AddClick(int64_t start);
    void MoveCursor();
    double SamplesPerBeat() const { return sampleRate_ * 60.0 / kTempo; }

    int tracks_;
    double sampleRate_ = 48000;
    std::atomic<bool> running_{true}, hasDevice_{false}, playing_{false};
    std::atomic<int64_t> position_{0};
    std::thread thread_;

    std::mutex lock_;  // held briefly by the UI; guards everything below
    std::vector<std::unique_ptr<Instrument>> instruments_;
    std::vector<Scheduled> schedule_;
    size_t cursor_ = 0;
    int64_t totalSamples_ = 0;
    bool loop_ = false, click_ = false, allOff_ = false;
    std::vector<std::vector<MidiEvent>> events_;
    std::vector<std::array<bool, 128>> active_;
    std::vector<Sounding> previews_, newPreviews_;
    int clickFrames_ = 0;
    double clickPhase_ = 0;

    std::vector<float> mixL_, mixR_, trackL_, trackR_;
    std::unique_ptr<std::atomic<float>[]> peaks_;
    size_t blockRead_ = kBlock;  // frames of the current block already written out
};
