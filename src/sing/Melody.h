// Turns a sung recording into notes: pitch tracking, note finding, quantizing and spelling.
#pragma once

#include <utility>
#include <vector>

#include "Project.h"

// One 10 ms analysis frame. midi is 0 when no pitch was found.
struct PitchFrame {
    double time = 0;  // seconds from the start of the recording, at the window's centre
    float midi = 0;
    float level = 0;  // RMS
};

// Incremental YIN pitch tracker for a mono voice.
class PitchTracker {
public:
    explicit PitchTracker(double sampleRate);
    void Feed(const float* samples, size_t count);
    const std::vector<PitchFrame>& Frames() const { return frames_; }
    double FrameSeconds() const { return hop_ / rate_; }

private:
    void Analyze(size_t start);

    double inputRate_, rate_;
    int decimate_, window_, hop_, tauMin_, tauMax_;
    double b0_, b1_, b2_, a1_, a2_;  // anti-aliasing low-pass
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
    long long phase_ = 0;
    std::vector<float> down_;
    size_t next_ = 0;
    std::vector<float> diff_;
    std::vector<PitchFrame> frames_;
};

// A note as sung: times in seconds from the take's first beat, pitch unrounded.
struct SungNote {
    double start = 0, end = 0;
    double midi = 0;
};
std::vector<SungNote> FindNotes(const std::vector<PitchFrame>& frames, double frameSeconds, double offsetSeconds);

// A note in the pattern editor, in quarter notes from the start of the piece.
struct PatternNote {
    double beat = 0, length = 1;
    int midi = 60;
    bool selected = false;
};

// Converts between seconds and quarter notes using a tempo map of (beat, bpm) points.
class TempoClock {
public:
    explicit TempoClock(std::vector<std::pair<double, double>> points);
    double BeatToSeconds(double beat) const;
    double SecondsToBeat(double seconds) const;

private:
    std::vector<std::pair<double, double>> points_;
    std::vector<double> seconds_;  // when each point starts
};

// Places sung notes on the grid starting at startBeat. Notes before the start are dropped.
// semitones is added to every note. tuning, if given, receives the singer's average offset in cents.
std::vector<PatternNote> QuantizeNotes(const std::vector<SungNote>& sung, const TempoClock& clock, double startBeat,
                                       double grid, int fifths, bool keepToKey, int semitones, double* tuning);
// Snaps starts and lengths to the grid and trims overlaps.
void SnapNotes(std::vector<PatternNote>& notes, double grid, bool selectedOnly);
void SortAndTrim(std::vector<PatternNote>& notes);

// A pitch name such as "F#4", spelled for the key.
wxString MidiName(int midi, int fifths);

// The pattern as one notation string per bar, from firstBar to lastBar. Empty bars are "R".
// Notes crossing a barline are split, since the score has no ties yet.
std::vector<wxString> PatternToBarTexts(const Project& project, const std::vector<PatternNote>& notes, int firstBar,
                                        int lastBar);
