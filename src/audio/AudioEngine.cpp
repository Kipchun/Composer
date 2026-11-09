#include "AudioEngine.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
constexpr int kPreviewFrames = 16000;
constexpr float kClickGain = 0.28f;
constexpr int kClickFrames = 1100;
}  // namespace

AudioEngine::AudioEngine(int tracks)
    : tracks_(tracks),
      instruments_(tracks),
      events_(tracks),
      active_(tracks),
      mixL_(kBlock),
      mixR_(kBlock),
      trackL_(kBlock),
      trackR_(kBlock),
      peaks_(new std::atomic<float>[tracks]) {
    for (int t = 0; t < tracks; ++t) {
        peaks_[t] = 0;
        active_[t].fill(false);
        events_[t].reserve(256);
    }
    std::promise<void> ready;
    std::future<void> done = ready.get_future();
    thread_ = std::thread(&AudioEngine::ThreadMain, this, std::move(ready));
    done.wait();  // the sample rate is known once this returns
}

AudioEngine::~AudioEngine() {
    running_ = false;
    thread_.join();
}

void AudioEngine::ThreadMain(std::promise<void> ready) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    bool signalled = false;
    OpenAndRun(ready, signalled);
    hasDevice_ = false;
    if (!signalled) ready.set_value();

    // No usable device: keep time so that playback and the playhead still work.
    const DWORD sleepMs = DWORD(1000.0 * kBlock / sampleRate_);
    while (running_) {
        RenderBlock();
        Sleep(sleepMs);
    }

    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

bool AudioEngine::OpenAndRun(std::promise<void>& ready, bool& signalled) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(client.GetAddressOf()))))
        return false;

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix))) return false;
    sampleRate_ = mix->nSamplesPerSec;
    CoTaskMemFree(mix);

    // Always ask for 32-bit float stereo and let Windows convert if it must.
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = DWORD(sampleRate_);
    format.wBitsPerSample = 32;
    format.nBlockAlign = 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    constexpr REFERENCE_TIME kBufferDuration = 300000;  // 30 ms
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, &format,
                                  nullptr)))
        return false;

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT32 bufferFrames = 0;
    if (!event || FAILED(client->SetEventHandle(event)) ||
        FAILED(client->GetBufferSize(&bufferFrames)) ||
        FAILED(client->GetService(IID_PPV_ARGS(&render))) || FAILED(client->Start())) {
        if (event) CloseHandle(event);
        return false;
    }

    hasDevice_ = true;
    ready.set_value();
    signalled = true;

    while (running_) {
        WaitForSingleObject(event, 200);
        UINT32 padding = 0;
        BYTE* data = nullptr;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        const UINT32 available = bufferFrames - padding;
        if (available == 0) continue;
        if (FAILED(render->GetBuffer(available, &data))) break;

        auto* out = reinterpret_cast<float*>(data);
        for (UINT32 i = 0; i < available; ++i) {
            if (blockRead_ >= size_t(kBlock)) {
                RenderBlock();
                blockRead_ = 0;
            }
            *out++ = mixL_[blockRead_];
            *out++ = mixR_[blockRead_];
            ++blockRead_;
        }
        if (FAILED(render->ReleaseBuffer(available, 0))) break;
    }

    client->Stop();
    CloseHandle(event);
    return true;
}

void AudioEngine::Emit(int track, int offset, int pitch, bool on, float velocity) {
    if (pitch < 0 || pitch > 127) return;
    events_[track].push_back({offset, pitch, on, velocity});
    active_[track][pitch] = on;
}

void AudioEngine::RenderBlock() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& list : events_) list.clear();

    if (allOff_) {
        for (int t = 0; t < tracks_; ++t)
            for (int p = 0; p < 128; ++p)
                if (active_[t][p]) Emit(t, 0, p, false);
        allOff_ = false;
    }

    for (auto it = previews_.begin(); it != previews_.end();) {
        it->framesLeft -= kBlock;
        if (it->framesLeft <= 0) {
            Emit(it->track, 0, it->pitch, false);
            it = previews_.erase(it);
        } else {
            ++it;
        }
    }

    for (const Sounding& preview : newPreviews_) {
        Emit(preview.track, 0, preview.pitch, true);
        previews_.push_back(preview);
    }
    newPreviews_.clear();

    const int64_t start = position_;
    std::fill(mixL_.begin(), mixL_.end(), 0.f);
    std::fill(mixR_.begin(), mixR_.end(), 0.f);

    if (playing_) {
        const int64_t end = start + kBlock;
        while (cursor_ < schedule_.size() && schedule_[cursor_].sample < end) {
            const Scheduled& s = schedule_[cursor_++];
            Emit(s.track, int(std::max<int64_t>(0, s.sample - start)), s.pitch, s.on, s.velocity);
        }
        position_ = end;
        if (end >= totalSamples_) {
            position_ = 0;
            cursor_ = 0;
            allOff_ = true;
            if (!loop_) playing_ = false;
        }
    }

    // Instruments may not assume events arrive in time order otherwise.
    for (auto& list : events_)
        std::stable_sort(list.begin(), list.end(),
                         [](const MidiEvent& a, const MidiEvent& b) { return a.offset < b.offset; });

    Mix();
    if (click_ && playing_) AddClick(start);
    for (int i = 0; i < kBlock; ++i) {
        mixL_[i] = std::clamp(mixL_[i], -1.f, 1.f);
        mixR_[i] = std::clamp(mixR_[i], -1.f, 1.f);
    }
}

void AudioEngine::Mix() {
    for (int t = 0; t < tracks_; ++t) {
        if (!instruments_[t]) continue;
        instruments_[t]->Process(events_[t].data(), int(events_[t].size()), trackL_.data(),
                                 trackR_.data(), kBlock);
        float peak = 0;
        for (int i = 0; i < kBlock; ++i) {
            mixL_[i] += trackL_[i];
            mixR_[i] += trackR_[i];
            peak = std::max({peak, std::abs(trackL_[i]), std::abs(trackR_[i])});
        }
        if (peak > peaks_[t]) peaks_[t] = peak;
    }
}

void AudioEngine::AddClick(int64_t start) {
    const auto next = std::lower_bound(clicks_.begin(), clicks_.end(), start);
    const int startFrame = next != clicks_.end() && *next < start + kBlock ? int(*next - start) : kBlock;

    for (int i = 0; i < kBlock; ++i) {
        if (i == startFrame) {
            clickFrames_ = kClickFrames;
            clickPhase_ = 0;
        }
        if (clickFrames_ > 0) {
            const float env = float(clickFrames_) / kClickFrames;
            const float s = float(std::sin(clickPhase_)) * env * env * kClickGain;
            clickPhase_ += 6.283185307 * 1600.0 / sampleRate_;
            --clickFrames_;
            mixL_[i] += s;
            mixR_[i] += s;
        }
    }
}

std::unique_ptr<Instrument> AudioEngine::SetInstrument(int track,
                                                       std::unique_ptr<Instrument> instrument) {
    std::lock_guard<std::mutex> guard(lock_);
    if (instrument) instrument->SetTempo(tempo_);
    std::swap(instruments_[track], instrument);
    active_[track].fill(false);
    return instrument;
}

void AudioEngine::MoveCursor() {
    const int64_t position = position_;
    cursor_ = size_t(std::lower_bound(schedule_.begin(), schedule_.end(), position,
                                      [](const Scheduled& s, int64_t p) { return s.sample < p; }) -
                     schedule_.begin());
}

void AudioEngine::SetSchedule(const std::vector<PlayNote>& notes, double totalBeats,
                              const std::vector<double>& clickBeats) {
    const double perBeat = SamplesPerBeat();
    std::vector<Scheduled> schedule;
    schedule.reserve(notes.size() * 2);
    for (const PlayNote& n : notes) {
        const int64_t on = int64_t(n.beat * perBeat);
        const int64_t off = on + int64_t(n.length * 0.95 * perBeat);
        schedule.push_back({on, n.staff, n.midi, true, n.velocity});
        schedule.push_back({off, n.staff, n.midi, false, 0.f});
    }
    // At the same instant, releases go first so repeated notes retrigger.
    std::stable_sort(schedule.begin(), schedule.end(), [](const Scheduled& a, const Scheduled& b) {
        return a.sample != b.sample ? a.sample < b.sample : !a.on && b.on;
    });

    std::vector<int64_t> clicks;
    clicks.reserve(clickBeats.size());
    for (double beat : clickBeats) clicks.push_back(int64_t(std::llround(beat * perBeat)));

    std::lock_guard<std::mutex> guard(lock_);
    schedule_ = std::move(schedule);
    clicks_ = std::move(clicks);
    totalSamples_ = int64_t(totalBeats * perBeat);
    MoveCursor();
    if (playing_) allOff_ = true;  // a sounding note's release may no longer exist
}

void AudioEngine::Play() {
    std::lock_guard<std::mutex> guard(lock_);
    MoveCursor();
    playing_ = true;
}

void AudioEngine::Pause() {
    std::lock_guard<std::mutex> guard(lock_);
    playing_ = false;
    allOff_ = true;
}

void AudioEngine::Stop() {
    std::lock_guard<std::mutex> guard(lock_);
    playing_ = false;
    position_ = 0;
    cursor_ = 0;
    allOff_ = true;
}

void AudioEngine::Seek(double beats) {
    std::lock_guard<std::mutex> guard(lock_);
    position_ = int64_t(beats * SamplesPerBeat());
    MoveCursor();
    allOff_ = true;
}

void AudioEngine::SetLoop(bool on) {
    std::lock_guard<std::mutex> guard(lock_);
    loop_ = on;
}

void AudioEngine::SetClick(bool on) {
    std::lock_guard<std::mutex> guard(lock_);
    click_ = on;
}

void AudioEngine::SetTempo(double quartersPerMinute) {
    std::lock_guard<std::mutex> guard(lock_);
    // Stay at the same musical position when the speed changes.
    position_ = int64_t(position_ * tempo_ / quartersPerMinute);
    tempo_ = quartersPerMinute;
    for (auto& instrument : instruments_)
        if (instrument) instrument->SetTempo(tempo_);
}

void AudioEngine::Preview(int track, int pitch) {
    std::lock_guard<std::mutex> guard(lock_);
    if (pitch < 0 || pitch > 127 || !instruments_[track]) return;
    newPreviews_.push_back({track, pitch, kPreviewFrames});
}

double AudioEngine::Beats() const { return position_ / SamplesPerBeat(); }

float AudioEngine::TakePeak(int track) { return peaks_[track].exchange(0.f); }
