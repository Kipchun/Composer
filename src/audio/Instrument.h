// Anything that can turn note events into stereo audio.
#pragma once

#include <memory>

struct MidiEvent {
    int offset;  // sample position within the block
    int pitch;
    bool on;
    float velocity;
};

class Instrument {
public:
    virtual ~Instrument() = default;
    // Called on the audio thread. Writes exactly `frames` samples to left and right.
    virtual void Process(const MidiEvent* events, int count, float* left, float* right,
                         int frames) = 0;
};

// The built-in sound used when a staff has no plugin. Timbre varies by staff.
std::unique_ptr<Instrument> MakePlaceholder(int staff, double sampleRate);
