#include "sing/Melody.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-6;

float Median(std::vector<float> values) {
    if (values.empty()) return 0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values[mid];
}

float Percentile(std::vector<float> values, double fraction) {
    if (values.empty()) return 0;
    const size_t at = std::min(values.size() - 1, size_t(fraction * values.size()));
    std::nth_element(values.begin(), values.begin() + at, values.end());
    return values[at];
}

const int kNaturalPc[7] = {0, 2, 4, 5, 7, 9, 11};

// Pitch classes of the key's scale.
std::vector<int> ScalePcs(int fifths) {
    std::vector<int> pcs;
    for (int letter = 0; letter < 7; ++letter) pcs.push_back((kNaturalPc[letter] + KeyAlteration(fifths, letter) + 12) % 12);
    return pcs;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Pitch tracking

PitchTracker::PitchTracker(double sampleRate) : inputRate_(sampleRate) {
    decimate_ = std::max(1, int(std::lround(sampleRate / 16000)));
    rate_ = sampleRate / decimate_;
    window_ = int(rate_ * 0.032);
    hop_ = int(rate_ * 0.01);
    tauMin_ = int(rate_ / 1100);  // highest pitch about C6
    tauMax_ = int(rate_ / 65);    // lowest about C2

    // RBJ low-pass at 40% of the new Nyquist rate.
    const double cutoff = 0.4 * rate_ / 2, q = 0.707;
    const double w = 2 * kPi * cutoff / inputRate_, alpha = std::sin(w) / (2 * q), c = std::cos(w);
    const double a0 = 1 + alpha;
    b0_ = (1 - c) / 2 / a0;
    b1_ = (1 - c) / a0;
    b2_ = b0_;
    a1_ = -2 * c / a0;
    a2_ = (1 - alpha) / a0;
    diff_.resize(tauMax_ + 2);
}

void PitchTracker::Feed(const float* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const double x = samples[i];
        const double y = decimate_ == 1 ? x : b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        if (phase_++ % decimate_ == 0) down_.push_back(float(y));
    }
    while (next_ + window_ + tauMax_ + 1 <= down_.size()) {
        Analyze(next_);
        next_ += hop_;
    }
}

void PitchTracker::Analyze(size_t start) {
    const float* x = down_.data() + start;
    double energy = 0;
    for (int j = 0; j < window_; ++j) energy += double(x[j]) * x[j];

    PitchFrame frame;
    frame.time = (double(start) + window_ / 2.0) / rate_;
    frame.level = float(std::sqrt(energy / window_));
    if (frame.level < 1e-4f) {
        frames_.push_back(frame);
        return;
    }

    // Cumulative mean normalised difference (YIN steps 2 and 3).
    diff_[0] = 1;
    double running = 0;
    for (int tau = 1; tau <= tauMax_; ++tau) {
        double sum = 0;
        for (int j = 0; j < window_; ++j) {
            const double d = double(x[j]) - x[j + tau];
            sum += d * d;
        }
        running += sum;
        diff_[tau] = running > 0 ? float(sum * tau / running) : 1.f;
    }

    // The first dip under the threshold, followed down to its minimum.
    constexpr float kThreshold = 0.15f;
    int best = -1;
    for (int tau = std::max(2, tauMin_); tau < tauMax_; ++tau) {
        if (diff_[tau] < kThreshold) {
            while (tau + 1 < tauMax_ && diff_[tau + 1] < diff_[tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        // No clear period: accept the global minimum only when it's fairly convincing.
        best = std::max(2, tauMin_);
        for (int tau = best; tau < tauMax_; ++tau)
            if (diff_[tau] < diff_[best]) best = tau;
        if (diff_[best] > 0.3f) {
            frames_.push_back(frame);
            return;
        }
    }

    double period = best;
    if (best > 1 && best < tauMax_) {
        const double a = diff_[best - 1], b = diff_[best], c = diff_[best + 1];
        const double denom = a - 2 * b + c;
        if (std::abs(denom) > 1e-12) period += 0.5 * (a - c) / denom;
    }
    const double hz = rate_ / period;
    frame.midi = float(69 + 12 * std::log2(hz / 440));
    frames_.push_back(frame);
}

// ---------------------------------------------------------------------------------------------
// Note finding

std::vector<SungNote> FindNotes(const std::vector<PitchFrame>& frames, double frameSeconds, double offsetSeconds) {
    std::vector<SungNote> notes;
    const int n = int(frames.size());
    if (n == 0) return notes;

    // A gate relative to the room's noise and the singer's level.
    std::vector<float> levels;
    levels.reserve(n);
    for (const PitchFrame& f : frames) levels.push_back(f.level);
    const float floor = Percentile(levels, 0.1), loud = Percentile(levels, 0.95);
    const float gate = std::max({0.003f, floor * 2.5f, loud * 0.08f});

    std::vector<char> voiced(n);
    for (int i = 0; i < n; ++i) voiced[i] = frames[i].midi > 0 && frames[i].level >= gate;

    // Median of nearby voiced frames removes single-frame octave slips.
    std::vector<float> pitch(n, 0.f);
    std::vector<float> near;
    for (int i = 0; i < n; ++i) {
        if (!voiced[i]) continue;
        near.clear();
        for (int j = std::max(0, i - 2); j <= std::min(n - 1, i + 2); ++j)
            if (voiced[j]) near.push_back(frames[j].midi);
        pitch[i] = Median(near);
    }

    struct Current {
        bool active = false;
        int first = 0, last = 0;
        std::vector<float> pitches;
        float peak = 0;
    } cur;
    const auto close = [&] {
        if (!cur.active) return;
        cur.active = false;
        const double start = frames[cur.first].time - frameSeconds / 2;
        const double end = frames[cur.last].time + frameSeconds / 2;
        if (end - start < 0.08) return;
        // Skip the scoop into the note when there's enough of it to judge.
        std::vector<float> body(cur.pitches.begin() + (cur.pitches.size() > 8 ? 3 : 0), cur.pitches.end());
        notes.push_back({start - offsetSeconds, end - offsetSeconds, Median(body)});
    };
    const auto open = [&](int i) {
        cur = Current{};
        cur.active = true;
        cur.first = cur.last = i;
    };

    int gap = 0, deviant = 0, deviantStart = -1, dipStart = -1;
    for (int i = 0; i < n; ++i) {
        if (!voiced[i]) {
            if (cur.active && ++gap > 3) close();
            continue;
        }
        gap = 0;
        if (!cur.active) {
            open(i);
            deviant = 0;
            dipStart = -1;
        } else {
            // A sustained move to another pitch starts a new note.
            const size_t from = cur.pitches.size() > 20 ? cur.pitches.size() - 20 : 0;
            const float centre = Median(std::vector<float>(cur.pitches.begin() + from, cur.pitches.end()));
            if (std::abs(pitch[i] - centre) > 0.8f) {
                if (deviant++ == 0) deviantStart = i;
                if (deviant >= 4) {
                    const int restart = deviantStart;
                    cur.last = std::max(cur.first, restart - 1);
                    cur.pitches.resize(std::max<size_t>(1, cur.pitches.size() - (i - restart)));
                    close();
                    open(restart);
                    for (int j = restart; j <= i; ++j)
                        if (voiced[j]) cur.pitches.push_back(pitch[j]);
                    cur.last = i;
                    deviant = 0;
                    dipStart = -1;
                    continue;
                }
            } else {
                deviant = 0;
            }

            // A dip in loudness and a rise again is a repeated note.
            const float level = frames[i].level;
            if (level < 0.35f * cur.peak) {
                if (dipStart < 0) dipStart = i;
            } else if (dipStart >= 0 && level > 0.6f * cur.peak && i - cur.first > 6) {
                cur.last = std::max(cur.first, dipStart - 1);
                const size_t keep = std::max<size_t>(1, cur.pitches.size() - (i - dipStart));
                cur.pitches.resize(std::min(cur.pitches.size(), keep));
                close();
                open(dipStart);
                dipStart = -1;
            } else if (level >= 0.35f * cur.peak) {
                dipStart = -1;
            }
        }
        cur.pitches.push_back(pitch[i]);
        cur.peak = std::max(cur.peak, frames[i].level);
        cur.last = i;
    }
    close();
    return notes;
}

// ---------------------------------------------------------------------------------------------
// Time and quantizing

TempoClock::TempoClock(std::vector<std::pair<double, double>> points) : points_(std::move(points)) {
    if (points_.empty() || points_[0].first > 0) points_.insert(points_.begin(), {0.0, 96.0});
    double seconds = 0;
    for (size_t i = 0; i < points_.size(); ++i) {
        if (i > 0) seconds += (points_[i].first - points_[i - 1].first) * 60 / points_[i - 1].second;
        seconds_.push_back(seconds);
    }
}

double TempoClock::BeatToSeconds(double beat) const {
    size_t i = 0;
    while (i + 1 < points_.size() && points_[i + 1].first <= beat) ++i;
    return seconds_[i] + (beat - points_[i].first) * 60 / points_[i].second;
}

double TempoClock::SecondsToBeat(double seconds) const {
    size_t i = 0;
    while (i + 1 < seconds_.size() && seconds_[i + 1] <= seconds) ++i;
    return points_[i].first + (seconds - seconds_[i]) * points_[i].second / 60;
}

void SortAndTrim(std::vector<PatternNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const PatternNote& a, const PatternNote& b) {
        return a.beat != b.beat ? a.beat < b.beat : a.length > b.length;
    });
    std::vector<PatternNote> out;
    for (const PatternNote& note : notes) {
        if (note.length < kEps) continue;
        if (!out.empty() && std::abs(out.back().beat - note.beat) < kEps) continue;  // one line: keep the longer
        if (!out.empty() && out.back().beat + out.back().length > note.beat) out.back().length = note.beat - out.back().beat;
        out.push_back(note);
    }
    notes = std::move(out);
}

void SnapNotes(std::vector<PatternNote>& notes, double grid, bool selectedOnly) {
    for (PatternNote& note : notes) {
        if (selectedOnly && !note.selected) continue;
        const double start = std::round(note.beat / grid) * grid;
        double end = std::round((note.beat + note.length) / grid) * grid;
        if (end <= start + kEps) end = start + grid;
        note.beat = start;
        note.length = end - start;
    }
    SortAndTrim(notes);
}

std::vector<PatternNote> QuantizeNotes(const std::vector<SungNote>& sung, const TempoClock& clock, double startBeat,
                                       double grid, int fifths, bool keepToKey, int semitones, double* tuning) {
    // Singers often sit a little sharp or flat throughout; measure that and allow for it.
    std::vector<float> deviations;
    for (const SungNote& note : sung) {
        const double weight = std::min(8.0, (note.end - note.start) / 0.1);
        for (int k = 0; k < int(std::max(1.0, weight)); ++k)
            deviations.push_back(float(note.midi - std::round(note.midi)));
    }
    const double offset = deviations.size() >= 3 ? Median(deviations) : 0.0;
    if (tuning) *tuning = offset * 100;

    const std::vector<int> scale = ScalePcs(fifths);
    const double origin = clock.BeatToSeconds(startBeat);
    std::vector<PatternNote> notes;
    for (const SungNote& s : sung) {
        PatternNote note;
        const double beat = clock.SecondsToBeat(origin + s.start);
        const double end = clock.SecondsToBeat(origin + s.end);
        if (beat < startBeat - grid / 2) continue;  // sung during the count-in
        note.beat = std::max(startBeat, beat);
        note.length = end - note.beat;

        const double pitch = s.midi - offset;
        int midi = int(std::lround(pitch));
        if (keepToKey && std::find(scale.begin(), scale.end(), ((midi % 12) + 12) % 12) == scale.end()) {
            // Out of the key: take whichever neighbour in the key is closer to what was sung.
            const int up = midi + 1, down = midi - 1;
            const bool upInKey = std::find(scale.begin(), scale.end(), up % 12) != scale.end();
            const bool downInKey = std::find(scale.begin(), scale.end(), down % 12) != scale.end();
            if (upInKey && (!downInKey || std::abs(up - pitch) < std::abs(down - pitch))) midi = up;
            else if (downInKey) midi = down;
        }
        note.midi = std::clamp(midi + semitones, 12, 120);
        notes.push_back(note);
    }

    // Singers let go of a note a moment before the next one: a short gap is still legato.
    SortAndTrim(notes);
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
        const double gap = notes[i + 1].beat - (notes[i].beat + notes[i].length);
        if (gap > 0 && gap <= 0.5 && gap <= notes[i].length) notes[i].length += gap;
    }
    SnapNotes(notes, grid, false);
    return notes;
}

// ---------------------------------------------------------------------------------------------
// Writing out

wxString MidiName(int midi, int fifths) {
    static const char kLetters[] = "CDEFGAB";
    const int pc = ((midi % 12) + 12) % 12;
    int letter = -1, alteration = 0;
    // A note of the key is spelled as the key spells it.
    for (int l = 0; l < 7 && letter < 0; ++l) {
        const int alt = KeyAlteration(fifths, l);
        if ((kNaturalPc[l] + alt + 12) % 12 == pc) {
            letter = l;
            alteration = alt;
        }
    }
    // Otherwise sharps in sharp keys and C, flats in flat keys; naturals over double accidentals.
    if (letter < 0) {
        const int preferred = fifths >= 0 ? 1 : -1;
        for (int alt : {0, preferred, -preferred}) {
            for (int l = 0; l < 7 && letter < 0; ++l) {
                if ((kNaturalPc[l] + alt + 12) % 12 == pc) {
                    letter = l;
                    alteration = alt;
                }
            }
            if (letter >= 0) break;
        }
    }
    const int octave = (midi - alteration) / 12 - 1;
    wxString name(kLetters[letter]);
    if (alteration == 1) name += "#";
    else if (alteration == -1) name += "b";
    return name + wxString::Format("%d", octave);
}

namespace {

struct NoteValue {
    double beats;
    const char* name;
};
const NoteValue kNoteValues[] = {{4, "w"}, {3, "h."}, {2, "h"}, {1.5, "q."}, {1, "q"}, {0.75, "e."}, {0.5, "e"}, {0.25, "s"}};

// The single written value closest to a sung length, no longer than room allows. Without ties,
// one attack per sung note sounds closer to the singing than splitting it into repeated notes.
const NoteValue& ClosestValue(double length, double room) {
    const NoteValue* below = &kNoteValues[7];
    const NoteValue* above = nullptr;
    for (const NoteValue& value : kNoteValues) {
        if (value.beats <= length + kEps) {
            below = &value;
            break;
        }
        if (value.beats <= room + kEps) above = &value;
    }
    if (above && above->beats - length < length - below->beats) return *above;
    return *below;
}

// Breaks a length into writable values. pos is the position within the bar.
void AppendPieces(wxString& out, double pos, double length, const wxString& pitch) {
    static const struct { double beats; const char* name; double align; } kValues[] = {
        {4, "w", 1}, {3, "h.", 1}, {2, "h", 1}, {1.5, "q.", 1}, {1, "q", 1},
        {0.75, "e.", 0.5}, {0.5, "e", 0.5}, {0.25, "s", 0.25}};
    while (length > kEps) {
        bool placed = false;
        for (const auto& value : kValues) {
            const double phase = std::fmod(pos + kEps, value.align);
            if (value.beats <= length + kEps && phase < 2 * kEps) {
                if (!out.empty()) out += ' ';
                out += wxString(value.name) + ":" + pitch;
                pos += value.beats;
                length -= value.beats;
                placed = true;
                break;
            }
        }
        if (!placed) break;  // only shorter than a sixteenth is left
    }
}

}  // namespace

std::vector<wxString> PatternToBarTexts(const Project& project, const std::vector<PatternNote>& source, int firstBar,
                                        int lastBar) {
    std::vector<PatternNote> notes = source;
    SnapNotes(notes, 0.25, false);  // the score writes nothing shorter than a sixteenth
    const int fifths = project.settings.fifths;

    std::vector<wxString> texts;
    for (int bar = firstBar; bar <= lastBar; ++bar) {
        const double start = project.BarStart(bar), length = project.BarBeats(bar), end = start + length;
        wxString text;
        double cursor = 0;
        bool any = false;
        for (const PatternNote& note : notes) {
            const double a = std::max(note.beat, start) - start;
            const double b = std::min(note.beat + note.length, end) - start;
            if (b <= a + kEps || a < cursor - kEps) continue;
            if (a > cursor + kEps) AppendPieces(text, cursor, a - cursor, "r");
            // Room runs to the next note in this bar, or the barline.
            double room = length - a;
            for (const PatternNote& other : notes)
                if (other.beat - start > a + kEps && other.beat - start < a + room) room = other.beat - start - a;
            const NoteValue& value = ClosestValue(b - a, room);
            if (!text.empty()) text += ' ';
            text += wxString(value.name) + ":" + MidiName(note.midi, fifths);
            cursor = a + value.beats;
            any = true;
        }
        if (!any) {
            texts.push_back("R");
            continue;
        }
        if (cursor < length - kEps) AppendPieces(text, cursor, length - cursor, "r");
        texts.push_back(text);
    }
    return texts;
}
