#include <algorithm>
#include <cmath>
#include <vector>

#include "Instrument.h"

namespace {

constexpr double kTwoPi = 6.283185307179586;

struct Timbre {
    double second, third;  // harmonic levels
    double sustain;        // level the note decays to; 0 for plucked or struck sounds
    double decaySeconds;
};

// Flute, oboe, violin, cello, piano RH, piano LH.
const Timbre kTimbres[] = {
    {0.25, 0.05, 0.75, 0.3}, {0.7, 0.5, 0.75, 0.2}, {0.5, 0.3, 0.8, 0.4},
    {0.6, 0.3, 0.8, 0.4},    {0.45, 0.15, 0.0, 1.4}, {0.45, 0.15, 0.0, 1.8},
};

class PlaceholderSynth : public Instrument {
public:
    PlaceholderSynth(int staff, double sampleRate)
        : timbre_(kTimbres[std::clamp(staff, 0, 5)]), sampleRate_(sampleRate), voices_(24) {}

    void Process(const MidiEvent* events, int count, float* left, float* right,
                 int frames) override {
        std::fill(left, left + frames, 0.f);
        std::fill(right, right + frames, 0.f);

        int next = 0;
        for (int frame = 0; frame < frames; ++frame) {
            while (next < count && events[next].offset <= frame) Handle(events[next++]);

            double sample = 0;
            for (Voice& v : voices_)
                if (v.active) sample += Render(v);
            left[frame] = right[frame] = float(sample * kVoiceGain);
        }
    }

private:
    static constexpr double kVoiceGain = 0.11;

    struct Voice {
        bool active = false, released = false;
        int pitch = 0;
        double phase = 0, step = 0, level = 0, velocity = 0, age = 0;
    };

    void Handle(const MidiEvent& e) {
        if (e.on) {
            auto it = std::find_if(voices_.begin(), voices_.end(),
                                   [](const Voice& v) { return !v.active; });
            if (it == voices_.end()) it = voices_.begin();
            *it = Voice{};
            it->active = true;
            it->pitch = e.pitch;
            it->velocity = e.velocity;
            it->step = kTwoPi * 440.0 * std::pow(2.0, (e.pitch - 69) / 12.0) / sampleRate_;
        } else {
            for (Voice& v : voices_)
                if (v.active && !v.released && v.pitch == e.pitch) v.released = true;
        }
    }

    double Render(Voice& v) {
        const double attack = 0.008 * sampleRate_;
        const double decay = timbre_.decaySeconds * sampleRate_;
        double target;
        if (v.released) {
            v.level *= 1.0 - 1.0 / (0.09 * sampleRate_);
            if (v.level < 0.0005) v.active = false;
            target = v.level;
        } else {
            const double env = v.age < attack
                                   ? v.age / attack
                                   : timbre_.sustain + (1 - timbre_.sustain) *
                                                           std::exp(-(v.age - attack) / decay);
            v.level = env;
            target = env;
        }
        v.age += 1;
        v.phase = std::fmod(v.phase + v.step, kTwoPi);
        return target * v.velocity *
               (std::sin(v.phase) + timbre_.second * std::sin(2 * v.phase) +
                timbre_.third * std::sin(3 * v.phase));
    }

    Timbre timbre_;
    double sampleRate_;
    std::vector<Voice> voices_;
};

}  // namespace

std::unique_ptr<Instrument> MakePlaceholder(int staff, double sampleRate) {
    return std::make_unique<PlaceholderSynth>(staff, sampleRate);
}
