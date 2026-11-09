// The document: instruments, settings and notes, independent of the UI.
#pragma once

#include <wx/string.h>

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
};

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
    std::vector<PluginSetup> plugins;
    bool demo = false;  // the built-in sample, which has a scripted Director proposal
};

// One selectable instrument, listed in orchestral score order.
struct InstrumentSpec {
    const char* section;
    const char* name;
    Clef clef;
    int timbre;
    bool grandStaff;
};

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

wxString KeyLabel(int fifths, bool minor);
Bar FullBarRest(double barBeats);
bool ParseBar(const wxString& text, Bar& out);

bool SaveProjectFile(const wxString& path, const Project& project, wxString* error);
bool LoadProjectFile(const wxString& path, Project& out, wxString* error);
