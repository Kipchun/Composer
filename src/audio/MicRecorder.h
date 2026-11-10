// Records mono audio from the default microphone on its own thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MicRecorder {
public:
    MicRecorder() = default;
    ~MicRecorder();

    // Opens the default input device and starts collecting. On failure, error says why.
    bool Start(std::string* error);
    void Stop();
    bool IsRunning() const { return running_; }

    double SampleRate() const { return sampleRate_; }
    // Appends the samples recorded since `from` to out and returns the new total.
    size_t Read(size_t from, std::vector<float>& out);
    size_t Count();
    float TakePeak() { return peak_.exchange(0.f); }
    // When sample 0 was captured, in 100 ns units of the performance counter. False until known.
    bool StartTime(int64_t& hns) const;

private:
    void ThreadMain();
    bool Capture(std::string& error);
    void Append(const float* mono, size_t frames);

    std::atomic<bool> running_{false}, stop_{false}, ready_{false}, opened_{false};
    std::atomic<double> sampleRate_{48000};
    std::atomic<int64_t> startHns_{0};
    std::atomic<bool> hasStart_{false};
    std::atomic<float> peak_{0.f};
    std::thread thread_;
    std::string openError_;
    std::mutex lock_;
    std::vector<float> samples_;
    bool mock_ = false;
};
