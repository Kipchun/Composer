// The document: instruments, settings and notes, independent of the UI.
#pragma once

#include <wx/string.h>

#include <optional>
#include <string>
#include <vector>

enum class Clef { Treble, Alto, Bass };

// Placeholder-synth voices.
enum Timbre { kFlute, kReed, kBowed, kLowBowed, kPiano, kPianoLow, kBrass, kPluck, kDrum };

struct Event {
    double beats = 1;  // in quarter notes
    bool dotted = false;
    bool rest = false;
    std::vector<int> steps;         // diatonic steps from the staff's middle line
    std::vector<char> accidentals;  // parallel to steps; 0 means none
    std::string articulations;      // any of . > - ^ f (staccato to fermata)
};

using Bar = std::vector<Event>;
using Score = std::vector<std::vector<Bar>>;  // staff -> bar -> events

struct TrackDef {
    wxString name;
    int timbre = kFlute;
};

struct StaffDef {
    wxString name;  // empty for the lower staff of a grand staff
    Clef clef = Clef::Treble;
    int track = 0;
    bool braceToNext = false;
};

struct Section {
    int bar;
    wxString label;
};

struct Marking {
    int bar, staff;
    double beat;
    wxString text;
    bool proposed = false;  // only set on the copy drawn during a preview
};

struct Hairpin {
    int staff = 0, bar = 0;
    double beat = 0;
    int endBar = 0;
    double endBeat = 0;
    bool crescendo = true;
    bool proposed = false;
};

// A time signature taking effect at a bar after the first.
struct MeterChange {
    int bar, top, bottom;
};

// A tempo taking effect at a bar after the first.
struct TempoChange {
    int bar = 0, bpm = 96;
    wxString text;
    bool proposed = false;
};

// A gradual tempo change across whole bars, ending at endBpm. Its text is the printed marking.
struct TempoRamp {
    int bar = 0, endBar = 0, endBpm = 96;
    wxString text;
    bool proposed = false;
};

// A rectangle of bars and staves. Staves of -1 mean every staff.
struct BarRange {
    int firstBar = -1, lastBar = -1, firstStaff = -1, lastStaff = -1;
    bool IsValid() const { return firstBar >= 0; }
    bool Contains(int staff, int bar) const {
        return IsValid() && bar >= firstBar && bar <= lastBar &&
               (firstStaff < 0 || (staff >= firstStaff && staff <= lastStaff));
    }
};
BarRange MakeBarRange(int barA, int staffA, int barB, int staffB);

struct Slur {
    int bar, staff;
    double from, to;
};

// A plugin choice for one track; the state strings hold the plugin's saved settings.
struct PluginSetup {
    int track = 0;
    std::string path, component, controller;
};

struct ProjectSettings {
    wxString title, subtitle, composer, tempoText;
    int tempo = 96;
    int fifths = 0;  // key signature: negative flats, positive sharps
    bool minor = false;
    int timeTop = 4, timeBottom = 4;
    int bars = 32;
    int barsPerLine = 4;

    double BarBeats() const { return timeTop * 4.0 / timeBottom; }
    // Compound meters count and beam in dotted quarters.
    double BeatUnit() const { return timeBottom == 8 && timeTop % 3 == 0 ? 1.5 : 4.0 / timeBottom; }
    double BeamUnit() const { return timeBottom == 8 && timeTop % 3 == 0 ? 1.5 : 1.0; }
};

struct Project {
    ProjectSettings settings;
    std::vector<TrackDef> tracks;
    std::vector<StaffDef> staves;
    Score score;
    std::vector<Section> sections;
    std::vector<Marking> markings;
    std::vector<Slur> slurs;
    std::vector<Hairpin> hairpins;
    std::vector<MeterChange> meters;  // sorted by bar
    std::vector<TempoChange> tempos;  // sorted by bar
    std::vector<TempoRamp> ramps;     // sorted by bar, never overlapping
    std::vector<PluginSetup> plugins;
    bool demo = false;  // the built-in sample, which has a scripted Director proposal

    // All in quarter notes unless noted. Bars are zero-based.
    void MeterAt(int bar, int& top, int& bottom) const;
    double BarBeats(int bar) const;
    double BeatUnit(int bar) const;  // the pulse: what a metronome clicks on
    double BeamUnit(int bar) const;
    double BarStart(int bar) const;
    double TotalBeats() const { return BarStart(settings.bars); }
    int BarAtBeat(double beat) const;
    int TempoAt(int bar) const;
    double TempoAtBeat(double beat) const;
    void NormalizeMeters();  // drops changes that repeat the meter before them
    void FitMarksToBars();   // after bars shrink or disappear
};

// One selectable instrument, listed in orchestral score order.
struct InstrumentSpec {
    const char* section;
    const char* name;
    Clef clef;
    int timbre;
    bool grandStaff;
};

// A change the Director suggests. `original` lets the app notice if the score moved on.
struct BarEdit {
    int staff = 0, bar = 0;
    Bar replacement, original;
};

struct Proposal {
    wxString title, summary;
    std::vector<BarEdit> edits;
    std::vector<Marking> dynamics, removedDynamics;
    std::vector<Hairpin> hairpins, removedHairpins;

    // Whole-project changes; a bar of 0 means the opening of the piece.
    std::optional<wxString> title_, subtitle, composer, tempoText;  // title_ renames the piece
    std::optional<int> fifths;
    std::optional<bool> minor;
    std::optional<int> bars, barsPerLine;
    std::vector<MeterChange> setMeters;
    std::vector<int> removedMeters;
    std::vector<TempoChange> setTempos;
    std::vector<int> removedTempos;
    std::vector<TempoRamp> setRamps;
    std::vector<int> removedRamps;  // by starting bar
    std::vector<Section> setSections;
    std::vector<int> removedSections;

    wxString details;    // plain-language list of everything this does, for the card
    wxString meterKey;   // the bar layout it was written against; empty to skip the check
};

// The project as it would be with the proposal applied.
Project ProjectWithProposal(const Project& base, const Proposal& proposal, bool flagProposed);
wxString MeterKey(const Project& project);
wxString PlainKeyLabel(int fifths, bool minor);

// Adds and removes a proposal's markings in the given lists.
void ApplyMarkEdits(const Proposal& proposal, std::vector<Marking>& markings, std::vector<Hairpin>& hairpins,
                    bool flagProposed);

const std::vector<InstrumentSpec>& InstrumentCatalog();
int FindInstrument(const char* name);

enum class ProjectTemplate { FullOrchestra, SmallOrchestra, Soloist };
std::vector<int> TemplateInstruments(ProjectTemplate which, int soloist);

// `instruments` holds catalog indices; they are put into score order and numbered here.
Project BuildProject(const ProjectSettings& settings, std::vector<int> instruments);

// One instrument in a score being edited; sourceTrack is -1 for a newly added one.
struct ScorePart {
    int catalog = 0;
    int sourceTrack = -1;
};

// Sorts parts into score order, keeping parts of the same instrument in their given order.
void SortParts(std::vector<ScorePart>& parts);
// Returns false if a track isn't a catalog instrument and so can't be edited.
bool PartsFromProject(const Project& project, std::vector<ScorePart>& out);
// Builds a score with the given parts, carrying over the music and plugins of kept tracks.
Project RebuildWithParts(const Project& old, std::vector<ScorePart> parts,
                         const std::vector<PluginSetup>& plugins);
Project MakeDemoProject();
Project MakePart(const Project& full, int track);  // one instrument, laid out on its own

Proposal MakeDemoProposal(const Project& project);

wxString KeyLabel(int fifths, bool minor);
wxString StaffLabel(const Project& project, int staff);
bool BarsEqual(const Bar& a, const Bar& b);

// Pitch helpers. Letters are C=0 ... B=6.
int MiddleLine(Clef clef);  // diatonic position of the clef's middle line, counted from C0
int KeyAlteration(int fifths, int letter);  // -1, 0 or +1 from the key signature
int StepToMidi(Clef clef, int fifths, int step, char accidental);
Bar FullBarRest(double barBeats);
bool ParseBar(const wxString& text, Bar& out);

bool SaveProjectFile(const wxString& path, const Project& project, wxString* error);
bool LoadProjectFile(const wxString& path, Project& out, wxString* error);
