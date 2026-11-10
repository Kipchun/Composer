#include "MicRecorder.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>

using Microsoft::WRL::ComPtr;

namespace {
int64_t NowHns() {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return int64_t(double(now.QuadPart) * 1e7 / double(frequency.QuadPart));
}
}  // namespace

MicRecorder::~MicRecorder() { Stop(); }

bool MicRecorder::Start(std::string* error) {
    Stop();
    {
        std::lock_guard<std::mutex> guard(lock_);
        samples_.clear();
    }
    hasStart_ = false;
    stop_ = false;
    ready_ = false;
    opened_ = false;
    openError_.clear();
    running_ = true;
    thread_ = std::thread(&MicRecorder::ThreadMain, this);
    while (!ready_) Sleep(5);
    if (!opened_) {
        if (error) *error = openError_;
        Stop();
        return false;
    }
    return true;
}

void MicRecorder::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

size_t MicRecorder::Read(size_t from, std::vector<float>& out) {
    std::lock_guard<std::mutex> guard(lock_);
    if (from < samples_.size()) out.insert(out.end(), samples_.begin() + from, samples_.end());
    return samples_.size();
}

size_t MicRecorder::Count() {
    std::lock_guard<std::mutex> guard(lock_);
    return samples_.size();
}

bool MicRecorder::StartTime(int64_t& hns) const {
    if (!hasStart_) return false;
    hns = startHns_;
    return true;
}

void MicRecorder::Append(const float* mono, size_t frames) {
    float peak = 0;
    for (size_t i = 0; i < frames; ++i) peak = std::max(peak, std::abs(mono[i]));
    if (peak > peak_) peak_ = peak;
    std::lock_guard<std::mutex> guard(lock_);
    samples_.insert(samples_.end(), mono, mono + frames);
}

void MicRecorder::ThreadMain() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string error;
    if (!Capture(error) && !opened_) openError_ = error;
    ready_ = true;
    running_ = false;
    CoUninitialize();
}

bool MicRecorder::Capture(std::string& error) {
    // For testing without a microphone: raw 32-bit float mono at 48 kHz, played in real time.
    if (const char* mock = std::getenv("COMPOSER_SING_MOCK"); mock && *mock) {
        FILE* file = nullptr;
        if (fopen_s(&file, mock, "rb") || !file) {
            error = "The test recording could not be opened.";
            return false;
        }
        std::vector<float> data;
        float buffer[4096];
        size_t got;
        while ((got = fread(buffer, sizeof(float), 4096, file)) > 0) data.insert(data.end(), buffer, buffer + got);
        fclose(file);
        sampleRate_ = 48000;
        opened_ = true;
        ready_ = true;
        constexpr size_t kPacket = 480;
        const int64_t start = NowHns();
        startHns_ = start;
        hasStart_ = true;
        size_t pos = 0;
        std::vector<float> silence(kPacket, 0.f);
        while (!stop_) {
            const int64_t due = start + int64_t(double(pos) * 1e7 / 48000);
            while (NowHns() < due && !stop_) Sleep(1);
            if (pos < data.size()) {
                const size_t n = std::min(kPacket, data.size() - pos);
                Append(data.data() + pos, n);
            } else {
                Append(silence.data(), kPacket);
            }
            pos += kPacket;
        }
        return true;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) {
        error = "No microphone was found. Connect one, or choose a default recording device in Windows sound settings.";
        return false;
    }
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(client.GetAddressOf())))) {
        error = "The microphone could not be opened.";
        return false;
    }

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix))) {
        error = "The microphone's format could not be read.";
        return false;
    }
    sampleRate_ = mix->nSamplesPerSec;
    const int channels = std::max<int>(1, mix->nChannels);
    CoTaskMemFree(mix);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = WORD(channels);
    format.nSamplesPerSec = DWORD(sampleRate_.load());
    format.wBitsPerSample = 32;
    format.nBlockAlign = WORD(4 * channels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr);
    if (hr == E_ACCESSDENIED) {
        error = "Windows is blocking microphone access. Turn on \"Let desktop apps access your microphone\" in "
                "Settings > Privacy > Microphone.";
        return false;
    }
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(hr) || !event || FAILED(client->SetEventHandle(event)) ||
        FAILED(client->GetService(IID_PPV_ARGS(&capture))) || FAILED(client->Start())) {
        if (event) CloseHandle(event);
        error = "The microphone could not be started. Another program may be using it exclusively.";
        return false;
    }

    opened_ = true;
    ready_ = true;
    std::vector<float> mono;
    bool failed = false;
    while (!stop_ && !failed) {
        WaitForSingleObject(event, 100);
        for (;;) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD bufferFlags = 0;
            UINT64 position = 0, qpc = 0;
            hr = capture->GetBuffer(&data, &frames, &bufferFlags, &position, &qpc);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            if (FAILED(hr)) {
                failed = true;
                break;
            }
            if (!hasStart_ && frames > 0) {
                // qpc is when this packet's first frame was captured; later packets follow on.
                startHns_ = int64_t(qpc) - int64_t(double(Count()) * 1e7 / sampleRate_);
                hasStart_ = true;
            }
            mono.assign(frames, 0.f);
            if (!(bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                const float* in = reinterpret_cast<const float*>(data);
                for (UINT32 i = 0; i < frames; ++i) {
                    float sum = 0;
                    for (int c = 0; c < channels; ++c) sum += in[i * channels + c];
                    mono[i] = sum / channels;
                }
            }
            Append(mono.data(), frames);
            capture->ReleaseBuffer(frames);
        }
    }
    client->Stop();
    CloseHandle(event);
    return true;
}
