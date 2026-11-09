#include "director/ScoreText.h"

#include <wx/tokenzr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {

constexpr double kEps = 1e-6;
constexpr int kMaxStep = 16;
const char kLetters[] = "CDEFGAB";

int AlterationOf(char accidental) {
    switch (accidental) {
        case '#': return 1;
        case 'b': return -1;
        case 'x': return 2;
        case 'B': return -2;
        default: return 0;  // 'n'
    }
}

char AccidentalFor(int alteration) {
    switch (alteration) {
        case 1: return '#';
        case -1: return 'b';
        case 2: return 'x';
        case -2: return 'B';
        default: return 'n';
    }
}

wxString DurationName(double beats) {
    static const struct { double beats; const char* name; } kNames[] = {
        {4, "w"}, {3, "h."}, {2, "h"}, {1.5, "q."}, {1, "q"}, {0.75, "e."}, {0.5, "e"}, {0.25, "s"}};
    for (const auto& entry : kNames)
        if (std::abs(entry.beats - beats) < kEps) return entry.name;
    return wxString();
}

bool ParseDuration(const wxString& text, double& beats) {
    const wxString name = text.Lower();
    static const std::map<wxString, double> kValues = {{"w", 4},   {"h.", 3},   {"h", 2},   {"q.", 1.5},
                                                       {"q", 1},   {"e.", 0.75}, {"e", 0.5}, {"s", 0.25}};
    const auto found = kValues.find(name);
    if (found == kValues.end()) return false;
    beats = found->second;
    return true;
}

wxString PitchName(Clef clef, int step, int alteration) {
    const int diatonic = MiddleLine(clef) + step;
    wxString name(kLetters[diatonic % 7]);
    if (alteration == 1) name += "#";
    else if (alteration == 2) name += "x";
    else if (alteration == -1) name += "b";
    else if (alteration == -2) name += "bb";
    return name + wxString::Format("%d", diatonic / 7);
}

bool ParsePitch(const wxString& text, int& diatonic, int& alteration) {
    if (text.length() < 2) return false;
    const wxString upper = text.Left(1).Upper();
    const char* letter = strchr(kLetters, char(upper[0].GetValue()));
    if (!letter || !*letter) return false;

    size_t pos = 1;
    wxString accidental;
    while (pos < text.length() && (text[pos] < '0' || text[pos] > '9')) accidental += text[pos++];
    long octave = 0;
    if (pos >= text.length() || !text.Mid(pos).ToLong(&octave) || octave < 0 || octave > 9) return false;

    if (accidental.empty()) alteration = 0;
    else if (accidental == "#") alteration = 1;
    else if (accidental == "x" || accidental == "##") alteration = 2;
    else if (accidental == "b") alteration = -1;
    else if (accidental == "bb") alteration = -2;
    else return false;

    diatonic = int(octave) * 7 + int(letter - kLetters);
    return true;
}

bool Fail(wxString* error, const wxString& why) {
    if (error) *error = why;
    return false;
}

}  // namespace

wxString BarToText(const Project& project, int staff, int barIndex, const Bar& bar) {
    const double barBeats = project.BarBeats(barIndex);
    if (bar.size() == 1 && bar[0].rest && std::abs(bar[0].beats - barBeats) < kEps) return "R";

    const Clef clef = project.staves[staff].clef;
    const int fifths = project.settings.fifths;
    std::map<int, int> alterations;  // accidentals last until the end of the bar

    wxString out;
    for (const Event& ev : bar) {
        if (!out.empty()) out += ' ';
        wxString duration = DurationName(ev.beats);
        if (duration.empty()) duration = wxString::FromCDouble(ev.beats);
        out += duration + ":";
        if (ev.rest) {
            out += "r";
            continue;
        }
        for (size_t n = 0; n < ev.steps.size(); ++n) {
            const int step = ev.steps[n];
            if (ev.accidentals[n]) alterations[step] = AlterationOf(ev.accidentals[n]);
            const auto it = alterations.find(step);
            const int letter = (MiddleLine(clef) + step) % 7;
            const int alteration = it != alterations.end() ? it->second : KeyAlteration(fifths, letter);
            if (n) out += ',';
            out += PitchName(clef, step, alteration);
        }
        if (!ev.articulations.empty()) out += "/" + wxString(ev.articulations);
    }
    return out;
}

bool ParseBarText(const Project& project, int staff, int barIndex, const wxString& text, Bar& out, wxString* error) {
    out.clear();
    const double barBeats = project.BarBeats(barIndex);
    const wxString trimmed = wxString(text).Trim().Trim(false);
    if (trimmed == "R") {
        out = FullBarRest(barBeats);
        return true;
    }

    const Clef clef = project.staves[staff].clef;
    const int fifths = project.settings.fifths;
    std::map<int, int> alterations;
    double total = 0;
    bool allRests = true;

    wxStringTokenizer tokens(trimmed, " \t\r\n");
    while (tokens.HasMoreTokens()) {
        const wxString token = tokens.GetNextToken();
        const wxString duration = token.BeforeFirst(':');
        wxString body = token.AfterFirst(':');
        const wxString articulations = body.AfterFirst('/');
        body = body.BeforeFirst('/');
        Event ev;
        for (const wxUniChar c : articulations) {
            if (wxString(".>-^f").Find(c) == wxNOT_FOUND)
                return Fail(error, "\"" + token + "\" has an unknown articulation; use . > - ^ or f");
            ev.articulations += char(c.GetValue());
        }
        if (body.empty() || !ParseDuration(duration, ev.beats))
            return Fail(error, "\"" + token + "\" is not a valid token; expected duration:pitches, like q:Eb4");
        ev.dotted = ev.beats == 3.0 || ev.beats == 1.5 || ev.beats == 0.75;
        total += ev.beats;

        if (body.Lower() == "r") {
            ev.rest = true;
        } else {
            allRests = false;
            wxStringTokenizer pitches(body, ",");
            while (pitches.HasMoreTokens()) {
                const wxString pitch = pitches.GetNextToken();
                int diatonic = 0, alteration = 0;
                if (!ParsePitch(pitch, diatonic, alteration))
                    return Fail(error, "\"" + pitch + "\" is not a valid pitch; expected a name like Eb4 or F#5");
                const int step = diatonic - MiddleLine(clef);
                if (step < -kMaxStep || step > kMaxStep)
                    return Fail(error, pitch + " is too far from this staff's range to write");
                if (std::find(ev.steps.begin(), ev.steps.end(), step) != ev.steps.end())
                    return Fail(error, pitch + " appears twice in one chord");

                // Only mark an accidental when the note differs from what the reader expects.
                const auto it = alterations.find(step);
                const int current = it != alterations.end() ? it->second : KeyAlteration(fifths, diatonic % 7);
                char accidental = 0;
                if (alteration != current) {
                    accidental = AccidentalFor(alteration);
                    alterations[step] = alteration;
                }
                ev.steps.push_back(step);
                ev.accidentals.push_back(accidental);
            }
            if (ev.steps.empty()) return Fail(error, "\"" + token + "\" has no pitches");
        }
        out.push_back(std::move(ev));
    }

    if (out.empty()) return Fail(error, "the bar is empty; use R for a whole bar of rest");
    if (std::abs(total - barBeats) > kEps)
        return Fail(error, wxString::Format("the notes add up to %g quarter notes, but this bar holds %g", total, barBeats));
    if (allRests) out = FullBarRest(barBeats);
    return true;
}

wxString DescribeProject(const Project& project) {
    const ProjectSettings& s = project.settings;
    wxString out = "Title: " + s.title + "\n";
    if (!s.subtitle.empty()) out += "Subtitle: " + s.subtitle + "\n";
    if (!s.composer.empty()) out += "Composer: " + s.composer + "\n";
    out += "Key: " + PlainKeyLabel(s.fifths, s.minor) + "\n";
    out += wxString::Format("Time signature: %d/%d\n", s.timeTop, s.timeBottom);
    if (!project.meters.empty()) {
        out += "Time signature changes:";
        for (const MeterChange& m : project.meters) out += wxString::Format(" bar %d to %d/%d;", m.bar + 1, m.top, m.bottom);
        out += "\n";
    }
    out += wxString::Format("Tempo: quarter = %d", s.tempo);
    if (!s.tempoText.empty()) out += " (" + s.tempoText + ")";
    if (!project.tempos.empty()) {
        out += "\nTempo changes:";
        for (const TempoChange& t : project.tempos)
            out += wxString::Format(" bar %d to %d", t.bar + 1, t.bpm) + (t.text.empty() ? wxString() : " (" + t.text + ")") + ";";
    }
    if (!project.sections.empty()) {
        out += "\nRehearsal marks:";
        for (const Section& section : project.sections) out += " " + section.label + wxString::Format(" at bar %d;", section.bar + 1);
    }
    out += wxString::Format("\nBars: %d (%d per line)\nStaves:", s.bars, s.barsPerLine);
    for (int staff = 0; staff < int(project.staves.size()); ++staff) out += "\n- " + StaffLabel(project, staff);
    return out;
}

wxString BarsToText(const Project& project, int firstBar, int lastBar, const std::vector<int>& staves,
                    bool skipEmpty) {
    std::vector<int> list = staves;
    if (list.empty())
        for (int staff = 0; staff < int(project.staves.size()); ++staff) list.push_back(staff);

    wxString out;
    for (int bar = firstBar; bar <= lastBar; ++bar) {
        wxString lines;
        for (int staff : list) {
            const wxString text = BarToText(project, staff, bar, project.score[staff][bar]);

            wxString marks;
            for (const Marking& m : project.markings)
                if (m.staff == staff && m.bar == bar) marks += " " + m.text + wxString::Format("@beat%g", m.beat + 1);
            for (const Hairpin& h : project.hairpins)
                if (h.staff == staff && h.bar == bar)
                    marks += wxString(h.crescendo ? " crescendo" : " diminuendo") +
                             wxString::Format("@beat%g-to-bar%d-beat%g", h.beat + 1, h.endBar + 1, h.endBeat + 1);

            if (skipEmpty && text == "R" && marks.empty()) continue;
            lines += "  " + StaffLabel(project, staff) + ": " + text + (marks.empty() ? wxString() : " |" + marks) + "\n";
        }
        int top, bottom;
        project.MeterAt(bar, top, bottom);
        out += wxString::Format("Bar %d (%d/%d, quarter = %d)\n", bar + 1, top, bottom, project.TempoAt(bar)) + (lines.empty() ? wxString("  (all resting)\n") : lines);
    }
    return out;
}

int FindStaffByName(const Project& project, const wxString& name) {
    for (int staff = 0; staff < int(project.staves.size()); ++staff)
        if (StaffLabel(project, staff).CmpNoCase(name) == 0) return staff;
    return -1;
}
