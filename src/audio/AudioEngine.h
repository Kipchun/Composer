// Real-time output through WASAPI, plus the sequencer that drives each track.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
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

    explicit AudioEngine(int tracks);
    ~AudioEngine();

    double SampleRate() const { return sampleRate_; }
    bool HasDevice() const { return hasDevice_; }

    // Returns the previous instrument so the UI thread, not the audio thread, frees it.
    std::unique_ptr<Instrument> SetInstrument(int track, std::unique_ptr<Instrument> instrument);

    // tempoPoints are (quarter-note position, bpm), sorted, starting at position 0.
    void SetSchedule(const std::vector<PlayNote>& notes, double totalBeats, const std::vector<double>& clickBeats,
                     const std::vector<std::pair<double, double>>& tempoPoints);
    void Play();
    void Pause();
    void Stop();
    void Seek(double beats);
    void SetLoop(bool on);
    void SetClick(bool on);
    void Preview(int track, int pitch);

    double Beats();

    // Plays from startBeat to stopBeat, after countInClicks clicks spaced secondsPerClick apart.
    // Used for recording takes; the click setting applies after the count-in.
    void PlayTake(double startBeat, double stopBeat, int countInClicks, double secondsPerClick);
    // When the take's first beat reached the speakers, in 100 ns performance-counter units.
    // False until it has.
    bool TakeStartTime(int64_t& hns) const;
    bool InCountIn() const { return preroll_ > 0; }

    // Renders the piece once into interleaved stereo: the full mix plus one stem per track.
    // Runs at playback speed if any plugin is in use, otherwise as fast as it can.
    // progress gets 0 to 1 and returns false to cancel, which makes this return false.
    bool Bounce(std::vector<float>& mix, std::vector<std::vector<float>>& stems,
                const std::function<bool(double)>& progress);
    bool BounceIsRealTime() const;
    bool IsPlaying() const { return playing_; }
    float TakePeak(int track);  // level since the last call, 0 to 1

private:
    struct Scheduled {
        int64_t sample;
        int track;
        int pitch;
        bool on;
        float velocity;
    };
    struct Sounding {
        int track, pitch, framesLeft;
    };

    void ThreadMain(std::promise<void> ready);
    bool OpenAndRun(std::promise<void>& ready, bool& signalled);
    void RenderBlock();
    void Emit(int track, int offset, int pitch, bool on, float velocity = 0.8f);
    void Mix();
    void AddClick(int64_t start, const std::vector<int64_t>& clicks);
    void MoveCursor();
    struct TempoSegment {
        double beat, sample, samplesPerBeat, bpm;
    };
    double BeatToSample(double beat) const;
    double SampleToBeat(double sample) const;

    int tracks_;
    double sampleRate_ = 48000;
    std::atomic<bool> running_{true}, hasDevice_{false}, playing_{false}, offline_{false};
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
    std::vector<TempoSegment> tempoMap_;
    std::vector<std::vector<float>>* captureStems_ = nullptr;
    double currentBpm_ = 96;
    std::vector<int64_t> clicks_;
    std::vector<int64_t> countInClicks_;  // positions within the count-in
    std::atomic<int64_t> preroll_{0};     // count-in frames left; a multiple of kBlock
    int64_t prerollTotal_ = 0;
    int64_t stopSample_ = INT64_MAX;
    bool markTake_ = false;
    std::atomic<bool> hasTakeTime_{false};
    std::atomic<int64_t> takeHns_{0};
    // Output stream bookkeeping, used to time takes against the device clock.
    int64_t blockStreamFrame_ = 0;
    std::atomic<int64_t> takeStreamFrame_{0};
    std::atomic<bool> takeFromStream_{false};

    std::vector<float> mixL_, mixR_, trackL_, trackR_;
    std::unique_ptr<std::atomic<float>[]> peaks_;
    size_t blockRead_ = kBlock;  // frames of the current block already written out
};
