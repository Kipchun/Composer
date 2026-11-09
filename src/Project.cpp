#include "Project.h"

#include <wx/base64.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "Theme.h"

const std::vector<InstrumentSpec>& InstrumentCatalog() {
    static const std::vector<InstrumentSpec> catalog = {
        {"Woodwinds", "Piccolo", Clef::Treble, kFlute, false},
        {"Woodwinds", "Flute", Clef::Treble, kFlute, false},
        {"Woodwinds", "Alto Flute", Clef::Treble, kFlute, false},
        {"Woodwinds", "Oboe", Clef::Treble, kReed, false},
        {"Woodwinds", "English Horn", Clef::Treble, kReed, false},
        {"Woodwinds", "Clarinet", Clef::Treble, kReed, false},
        {"Woodwinds", "Bass Clarinet", Clef::Bass, kReed, false},
        {"Woodwinds", "Bassoon", Clef::Bass, kReed, false},
        {"Woodwinds", "Contrabassoon", Clef::Bass, kReed, false},
        {"Brass", "Horn", Clef::Treble, kBrass, false},
        {"Brass", "Trumpet", Clef::Treble, kBrass, false},
        {"Brass", "Trombone", Clef::Bass, kBrass, false},
        {"Brass", "Bass Trombone", Clef::Bass, kBrass, false},
        {"Brass", "Tuba", Clef::Bass, kBrass, false},
        {"Percussion", "Timpani", Clef::Bass, kDrum, false},
        {"Percussion", "Glockenspiel", Clef::Treble, kPluck, false},
        {"Percussion", "Xylophone", Clef::Treble, kPluck, false},
        {"Percussion", "Vibraphone", Clef::Treble, kPluck, false},
        {"Percussion", "Marimba", Clef::Treble, kPluck, false},
        {"Keyboards and Harp", "Harp", Clef::Treble, kPluck, true},
        {"Keyboards and Harp", "Celesta", Clef::Treble, kPiano, true},
        {"Keyboards and Harp", "Piano", Clef::Treble, kPiano, true},
        {"Keyboards and Harp", "Organ", Clef::Treble, kReed, true},
        {"Voices", "Soprano", Clef::Treble, kFlute, false},
        {"Voices", "Mezzo-soprano", Clef::Treble, kFlute, false},
        {"Voices", "Tenor", Clef::Treble, kBowed, false},
        {"Voices", "Baritone", Clef::Bass, kLowBowed, false},
        {"Strings", "Violin I", Clef::Treble, kBowed, false},
        {"Strings", "Violin II", Clef::Treble, kBowed, false},
        {"Strings", "Viola", Clef::Alto, kBowed, false},
        {"Strings", "Cello", Clef::Bass, kLowBowed, false},
        {"Strings", "Double Bass", Clef::Bass, kLowBowed, false},
    };
    return catalog;
}

int FindInstrument(const char* name) {
    const auto& catalog = InstrumentCatalog();
    for (size_t i = 0; i < catalog.size(); ++i)
        if (wxString(catalog[i].name) == name) return int(i);
    return 0;
}

std::vector<int> TemplateInstruments(ProjectTemplate which, int soloist) {
    if (which == ProjectTemplate::Soloist) return {soloist};

    struct Part {
        const char* name;
        int count;
    };
    static const std::vector<Part> fullParts = {
        {"Piccolo", 1},  {"Flute", 2},    {"Oboe", 2},          {"Clarinet", 2}, {"Bassoon", 2},
        {"Horn", 4},     {"Trumpet", 2},  {"Trombone", 2},      {"Bass Trombone", 1}, {"Tuba", 1},
        {"Timpani", 1},  {"Harp", 1},     {"Violin I", 1},      {"Violin II", 1}, {"Viola", 1},
        {"Cello", 1},    {"Double Bass", 1},
    };
    static const std::vector<Part> smallParts = {
        {"Flute", 1},    {"Oboe", 1},     {"Clarinet", 1},      {"Bassoon", 1}, {"Horn", 2},
        {"Trumpet", 1},  {"Timpani", 1},  {"Violin I", 1},      {"Violin II", 1}, {"Viola", 1},
        {"Cello", 1},    {"Double Bass", 1},
    };

    std::vector<int> result;
    for (const Part& part : which == ProjectTemplate::FullOrchestra ? fullParts : smallParts)
        for (int i = 0; i < part.count; ++i) result.push_back(FindInstrument(part.name));
    return result;
}

Bar FullBarRest(double barBeats) {
    Event rest;
    rest.rest = true;
    rest.beats = barBeats;
    return {rest};
}

Project BuildProject(const ProjectSettings& settings, std::vector<int> instruments) {
    const auto& catalog = InstrumentCatalog();
    // The catalog is already in score order, so sorting the indices is enough.
    std::sort(instruments.begin(), instruments.end());

    std::map<int, int> totals, seen;
    for (int index : instruments) ++totals[index];

    Project project;
    project.settings = settings;
    for (int index : instruments) {
        const InstrumentSpec& spec = catalog[index];
        wxString name = spec.name;
        if (totals[index] > 1) name += wxString::Format(" %d", ++seen[index]);

        const int track = int(project.tracks.size());
        project.tracks.push_back({name, spec.timbre});
        if (spec.grandStaff) {
            project.staves.push_back({name, Clef::Treble, track, true});
            project.staves.push_back({"", Clef::Bass, track, false});
        } else {
            project.staves.push_back({name, spec.clef, track, false});
        }
    }
    project.score.assign(project.staves.size(),
                         std::vector<Bar>(settings.bars, FullBarRest(settings.BarBeats())));
    return project;
}

void SortParts(std::vector<ScorePart>& parts) {
    std::stable_sort(parts.begin(), parts.end(),
                     [](const ScorePart& a, const ScorePart& b) { return a.catalog < b.catalog; });
}

namespace {

// Finds a track's instrument, allowing for a part number like "Flute 2".
int CatalogIndexForName(const wxString& name) {
    const auto& catalog = InstrumentCatalog();
    const auto find = [&](const wxString& wanted) {
        for (size_t i = 0; i < catalog.size(); ++i)
            if (wanted == catalog[i].name) return int(i);
        return -1;
    };
    const int exact = find(name);
    if (exact >= 0) return exact;
    long number = 0;
    return name.AfterLast(' ').ToLong(&number) ? find(name.BeforeLast(' ')) : -1;
}

}  // namespace

bool PartsFromProject(const Project& project, std::vector<ScorePart>& out) {
    out.clear();
    for (int t = 0; t < int(project.tracks.size()); ++t) {
        const int catalog = CatalogIndexForName(project.tracks[t].name);
        if (catalog < 0) return false;
        out.push_back({catalog, t});
    }
    SortParts(out);
    return true;
}

Project RebuildWithParts(const Project& old, std::vector<ScorePart> parts,
                         const std::vector<PluginSetup>& plugins) {
    SortParts(parts);
    std::vector<int> indices;
    for (const ScorePart& part : parts) indices.push_back(part.catalog);
    Project result = BuildProject(old.settings, indices);
    result.sections = old.sections;

    const auto stavesOf = [](const Project& project, int track) {
        std::vector<int> staves;
        for (int s = 0; s < int(project.staves.size()); ++s)
            if (project.staves[s].track == track) staves.push_back(s);
        return staves;
    };

    std::vector<int> staffMap(old.staves.size(), -1);
    for (int t = 0; t < int(parts.size()); ++t) {
        if (parts[t].sourceTrack < 0) continue;
        const std::vector<int> from = stavesOf(old, parts[t].sourceTrack);
        const std::vector<int> to = stavesOf(result, t);
        for (size_t k = 0; k < std::min(from.size(), to.size()); ++k) {
            staffMap[from[k]] = to[k];
            result.score[to[k]] = old.score[from[k]];
        }
        for (PluginSetup plugin : plugins) {
            if (plugin.track != parts[t].sourceTrack) continue;
            plugin.track = t;
            result.plugins.push_back(std::move(plugin));
        }
    }

    for (Marking m : old.markings) {
        if (staffMap[m.staff] < 0) continue;
        m.staff = staffMap[m.staff];
        result.markings.push_back(std::move(m));
    }
    for (Slur slur : old.slurs) {
        if (staffMap[slur.staff] < 0) continue;
        slur.staff = staffMap[slur.staff];
        result.slurs.push_back(slur);
    }
    return result;
}

wxString KeyLabel(int fifths, bool minor) {
    static const char* const major[15] = {"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C",
                                          "G",  "D",  "A",  "E",  "B",  "F#", "C#"};
    static const char* const minorNames[15] = {"Ab", "Eb", "Bb", "F", "C", "G",  "D", "A",
                                               "E",  "B",  "F#", "C#", "G#", "D#", "A#"};
    const wxString name = (minor ? minorNames : major)[std::clamp(fifths, -7, 7) + 7];
    wxString label = name.Left(1);
    if (name.length() > 1) label += ui::Glyph(name[1] == 'b' ? 0x266D : 0x266F);
    return label + (minor ? " minor" : " major");
}

// Tokens are <duration>:<steps>, where the duration is w h q e s (optionally dotted) or a
// number of quarter notes, and steps is "r" or a comma list like -2#,0.
bool ParseBar(const wxString& text, Bar& out) {
    out.clear();
    wxStringTokenizer tokens(text, " ");
    while (tokens.HasMoreTokens()) {
        const wxString token = tokens.GetNextToken();
        const wxString duration = token.BeforeFirst(':');
        const wxString body = token.AfterFirst(':');
        if (duration.empty() || body.empty()) return false;

        Event ev;
        const wxUniChar letter = duration[0];
        double beats = 0;
        if (letter == 'w') beats = 4;
        else if (letter == 'h') beats = 2;
        else if (letter == 'q') beats = 1;
        else if (letter == 'e') beats = 0.5;
        else if (letter == 's') beats = 0.25;
        else if (!duration.ToCDouble(&beats)) return false;
        if (duration.length() == 2 && duration[1] == '.') beats *= 1.5;
        if (beats <= 0 || beats > 16) return false;
        ev.beats = beats;
        ev.dotted = beats == 3.0 || beats == 1.5 || beats == 0.75;

        if (body == "r") {
            ev.rest = true;
        } else {
            wxStringTokenizer notes(body, ",");
            while (notes.HasMoreTokens()) {
                wxString note = notes.GetNextToken();
                char accidental = 0;
                const wxUniChar tail = note.Last();
                if (tail == '#' || tail == 'b' || tail == 'n' || tail == 'x' || tail == 'B') {
                    accidental = char(tail.GetValue());
                    note.RemoveLast();
                }
                long step = 0;
                if (!note.ToLong(&step) || step < -30 || step > 30) return false;
                ev.steps.push_back(int(step));
                ev.accidentals.push_back(accidental);
            }
            if (ev.steps.empty()) return false;
        }
        out.push_back(std::move(ev));
    }
    return !out.empty();
}

Project MakeDemoProject() {
    ProjectSettings settings;
    settings.title = "Nocturne in Blue";
    settings.subtitle = "for chamber ensemble";
    settings.composer = "Concept Score";
    settings.tempoText = "Andante";
    settings.tempo = 96;
    settings.fifths = -3;
    settings.bars = 8;

    // Laid out by hand: the sample keeps its piano below the strings, unlike score order.
    Project project;
    project.settings = settings;
    project.demo = true;
    project.tracks = {{"Flute", kFlute}, {"Oboe", kReed}, {"Violin I", kBowed}, {"Cello", kLowBowed}, {"Piano", kPiano}};
    project.staves = {{"Flute", Clef::Treble, 0, false},    {"Oboe", Clef::Treble, 1, false},
                      {"Violin I", Clef::Treble, 2, false}, {"Cello", Clef::Bass, 3, false},
                      {"Piano", Clef::Treble, 4, true},     {"", Clef::Bass, 4, false}};
    project.score.assign(6, std::vector<Bar>(settings.bars, FullBarRest(settings.BarBeats())));

    static const char* const music[6][8] = {
        {"w:r", "h:r q:r e:3 e:4", "q.:5 e:4 q:3 q:1", "h.:2 q:r", "q:5 e:6 e:5 q:4 q:3",
         "h:4 q:3 q:2", "q.:1 e:3 q:5 q:6", "h.:3 q:r"},
        {"h:-2 h:-1", "h:-4 h:-2", "h:-1 h:-3", "h:-3 h:-2", "q:-2 q:-1 h:-2", "h:-2 h:-4",
         "h:-1 q:1 q:-1", "h.:-2 q:r"},
        {"q:-2 e:-1 e:0 q:3 q:2", "q.:1 e:0 h:-2", "e:1 e:3 e:5 e:3 q:1 q:-1", "h:2 q:0 q:r",
         "q:3 e:2 e:1 q:0 q:-2", "q.:-2 e:-1 h:0", "e:1 e:0 e:-1 e:1 q:3 q:1", "w:-4"},
        {"q:-6 q:-2 q:1 q:-2", "q:-1 q:3 q:1 q:3", "q:-3 q:1 q:-1 q:1", "q:-2 q:2 q:0 q:2",
         "q:-6 q:-2 q:1 q:-2", "q:-4 q:0 q:-2 q:0", "q:-3 q:1 q:-1 q:1", "h:-2 h:-6"},
        {"h:-4,-2,0 h:-4,-2,0", "h:-6,-4,-2 h:-6,-4,-2", "h:-6,-4,-1 h:-6,-4,-1",
         "h:-5,-3,0 q:-5,-3,0 q:r", "h:-4,-2,0 h:-4,-2,0", "h:-5,-2,0 h:-5,-2,0",
         "h:-6,-4,-1 h:-6,-4,-1", "w:-4,-2,0"},
        {"e:-6 e:-2 e:1 e:-2 e:-6 e:-2 e:1 e:-2", "e:-1 e:3 e:1 e:3 e:-1 e:3 e:1 e:3",
         "e:-3 e:1 e:-1 e:1 e:-3 e:1 e:-1 e:1", "e:-2 e:2 e:0 e:2 e:-2 e:2 e:0 e:2",
         "e:-6 e:-2 e:1 e:-2 e:-6 e:-2 e:1 e:-2", "e:-4 e:0 e:-2 e:0 e:-4 e:0 e:-2 e:0",
         "e:-3 e:1 e:-1 e:1 e:-3 e:1 e:-1 e:1", "w:-6"},
    };
    for (int staff = 0; staff < 6; ++staff)
        for (int bar = 0; bar < 8; ++bar) ParseBar(music[staff][bar], project.score[staff][bar]);

    project.sections = {{0, "A"}, {4, "B"}};
    project.markings = {{0, 2, 0, "mp"}, {0, 3, 0, "p"},  {0, 5, 0, "p"},  {1, 0, 3, "p"},
                        {4, 0, 0, "mf"}, {4, 2, 0, "mf"}, {4, 3, 0, "mp"}, {7, 2, 0, "pp"}};
    project.slurs = {{0, 2, 0, 3}};
    return project;
}

// ---------------------------------------------------------------------------
// File format: UTF-8 lines of tab-separated fields, starting with a version line.

namespace {

constexpr const char* kMagic = "COMPOSER";
constexpr int kVersion = 1;

wxString Escape(wxString text) {
    text.Replace("\\", "\\\\");
    text.Replace("\t", "\\t");
    text.Replace("\n", "\\n");
    return text;
}

wxString Unescape(const wxString& text) {
    wxString out;
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] == '\\' && i + 1 < text.length()) {
            ++i;
            out += text[i] == 't' ? wxUniChar('\t') : text[i] == 'n' ? wxUniChar('\n') : text[i];
        } else {
            out += text[i];
        }
    }
    return out;
}

wxString SerializeBar(const Bar& bar) {
    wxString out;
    for (const Event& ev : bar) {
        if (!out.empty()) out += ' ';
        out += wxString::FromCDouble(ev.beats) + ":";
        if (ev.rest) {
            out += 'r';
            continue;
        }
        for (size_t n = 0; n < ev.steps.size(); ++n) {
            if (n) out += ',';
            out += wxString::Format("%d", ev.steps[n]);
            if (ev.accidentals[n]) out += wxUniChar(ev.accidentals[n]);
        }
    }
    return out;
}

std::string Encode(const std::string& bytes) {
    return std::string(wxBase64Encode(bytes.data(), bytes.size()).ToStdString());
}

std::string Decode(const wxString& text) {
    const wxMemoryBuffer buffer = wxBase64Decode(text);
    return std::string(static_cast<const char*>(buffer.GetData()), buffer.GetDataLen());
}

bool ToInt(const wxString& text, int& out) {
    long value = 0;
    if (!text.ToLong(&value)) return false;
    out = int(value);
    return true;
}

bool Fail(wxString* error, const wxString& why) {
    if (error) *error = why;
    return false;
}

}  // namespace

bool SaveProjectFile(const wxString& path, const Project& project, wxString* error) {
    const ProjectSettings& s = project.settings;
    wxString text;
    const auto line = [&](const wxString& key, const wxString& value) {
        text += key + "\t" + value + "\n";
    };

    text += wxString::Format("%s\t%d\n", kMagic, kVersion);
    line("title", Escape(s.title));
    line("subtitle", Escape(s.subtitle));
    line("composer", Escape(s.composer));
    line("tempotext", Escape(s.tempoText));
    line("tempo", wxString::Format("%d", s.tempo));
    line("key", wxString::Format("%d\t%d", s.fifths, s.minor ? 1 : 0));
    line("time", wxString::Format("%d\t%d", s.timeTop, s.timeBottom));
    line("bars", wxString::Format("%d\t%d", s.bars, s.barsPerLine));
    if (project.demo) line("demo", "1");

    for (const TrackDef& t : project.tracks)
        line("track", wxString::Format("%d\t", t.timbre) + Escape(t.name));
    for (const StaffDef& st : project.staves)
        line("staff", wxString::Format("%d\t%d\t%d\t", st.track, int(st.clef), st.braceToNext ? 1 : 0) +
                          Escape(st.name));
    for (const Section& sec : project.sections)
        line("section", wxString::Format("%d\t", sec.bar) + Escape(sec.label));
    for (const Marking& m : project.markings)
        line("mark", wxString::Format("%d\t%d\t", m.bar, m.staff) + wxString::FromCDouble(m.beat) +
                         "\t" + Escape(m.text));
    for (const Slur& sl : project.slurs)
        line("slur", wxString::Format("%d\t%d\t", sl.bar, sl.staff) + wxString::FromCDouble(sl.from) +
                         "\t" + wxString::FromCDouble(sl.to));
    for (size_t staff = 0; staff < project.score.size(); ++staff)
        for (size_t bar = 0; bar < project.score[staff].size(); ++bar)
            line("notes", wxString::Format("%zu\t%zu\t", staff, bar) +
                              SerializeBar(project.score[staff][bar]));
    for (const PluginSetup& p : project.plugins)
        line("plugin", wxString::Format("%d\t", p.track) + Escape(wxString::FromUTF8(p.path)) + "\t" +
                           Encode(p.component) + "\t" + Encode(p.controller));

    // Written beside the target and renamed, so a failed save can't damage the old file.
    const wxString temp = path + ".tmp";
    {
        wxFFile file(temp, "wb");
        const wxScopedCharBuffer utf8 = text.ToUTF8();
        if (!file.IsOpened() || !file.Write(utf8.data(), utf8.length()) || !file.Close())
            return Fail(error, "Could not write to that location.");
    }
    if (!wxRenameFile(temp, path, true)) {
        wxRemoveFile(temp);
        return Fail(error, "Could not replace the existing file.");
    }
    return true;
}

bool LoadProjectFile(const wxString& path, Project& out, wxString* error) {
    wxFFile file(path, "rb");
    wxString text;
    if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8))
        return Fail(error, "Could not read the file.");

    Project p;
    ProjectSettings& s = p.settings;
    bool sawHeader = false;
    std::vector<std::tuple<long, long, wxString>> pendingNotes;

    wxStringTokenizer lines(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    while (lines.HasMoreTokens()) {
        const wxString raw = lines.GetNextToken();
        if (raw.empty()) continue;
        const wxArrayString f = wxSplit(raw, '\t', 0);
        const wxString& key = f[0];
        const auto need = [&](size_t count) { return f.size() >= count + 1; };

        if (!sawHeader) {
            int version = 0;
            if (key != kMagic || !need(1) || !ToInt(f[1], version))
                return Fail(error, "This is not a Composer project.");
            if (version > kVersion)
                return Fail(error, "This project was saved by a newer version of Composer.");
            sawHeader = true;
            continue;
        }

        bool ok = true;
        if (key == "title" && need(1)) s.title = Unescape(f[1]);
        else if (key == "subtitle" && need(1)) s.subtitle = Unescape(f[1]);
        else if (key == "composer" && need(1)) s.composer = Unescape(f[1]);
        else if (key == "tempotext" && need(1)) s.tempoText = Unescape(f[1]);
        else if (key == "tempo" && need(1)) ok = ToInt(f[1], s.tempo);
        else if (key == "key" && need(2)) {
            int minor = 0;
            ok = ToInt(f[1], s.fifths) && ToInt(f[2], minor);
            s.minor = minor != 0;
        } else if (key == "time" && need(2)) ok = ToInt(f[1], s.timeTop) && ToInt(f[2], s.timeBottom);
        else if (key == "bars" && need(2)) ok = ToInt(f[1], s.bars) && ToInt(f[2], s.barsPerLine);
        else if (key == "demo") p.demo = true;
        else if (key == "track" && need(2)) {
            TrackDef t;
            ok = ToInt(f[1], t.timbre);
            t.name = Unescape(f[2]);
            p.tracks.push_back(t);
        } else if (key == "staff" && need(4)) {
            StaffDef st;
            int clef = 0, brace = 0;
            ok = ToInt(f[1], st.track) && ToInt(f[2], clef) && ToInt(f[3], brace) && clef >= 0 && clef <= 2;
            st.clef = Clef(clef);
            st.braceToNext = brace != 0;
            st.name = Unescape(f[4]);
            p.staves.push_back(st);
        } else if (key == "section" && need(2)) {
            Section sec{};
            ok = ToInt(f[1], sec.bar);
            sec.label = Unescape(f[2]);
            p.sections.push_back(sec);
        } else if (key == "mark" && need(4)) {
            Marking m{};
            ok = ToInt(f[1], m.bar) && ToInt(f[2], m.staff) && f[3].ToCDouble(&m.beat);
            m.text = Unescape(f[4]);
            p.markings.push_back(m);
        } else if (key == "slur" && need(4)) {
            Slur sl{};
            ok = ToInt(f[1], sl.bar) && ToInt(f[2], sl.staff) && f[3].ToCDouble(&sl.from) && f[4].ToCDouble(&sl.to);
            p.slurs.push_back(sl);
        } else if (key == "notes" && need(3)) {
            long staff = 0, bar = 0;
            ok = f[1].ToLong(&staff) && f[2].ToLong(&bar);
            pendingNotes.emplace_back(staff, bar, f[3]);
        } else if (key == "plugin" && need(4)) {
            PluginSetup plugin;
            ok = ToInt(f[1], plugin.track);
            plugin.path = Unescape(f[2]).ToStdString(wxConvUTF8);
            plugin.component = Decode(f[3]);
            plugin.controller = Decode(f[4]);
            p.plugins.push_back(std::move(plugin));
        }
        if (!ok) return Fail(error, "The project file is damaged (line: " + key + ").");
    }

    if (!sawHeader) return Fail(error, "This is not a Composer project.");
    if (s.tempo < 10 || s.tempo > 400 || s.fifths < -7 || s.fifths > 7 || s.timeTop < 1 ||
        s.timeTop > 16 || (s.timeBottom != 2 && s.timeBottom != 4 && s.timeBottom != 8) ||
        s.bars < 1 || s.bars > 2000 || s.barsPerLine < 1 || s.barsPerLine > 16)
        return Fail(error, "The project settings are out of range.");
    if (p.tracks.empty() || p.staves.empty()) return Fail(error, "The project has no instruments.");
    for (const StaffDef& st : p.staves)
        if (st.track < 0 || st.track >= int(p.tracks.size()))
            return Fail(error, "The project file is damaged (instruments).");

    const double barBeats = s.BarBeats();
    p.score.assign(p.staves.size(), std::vector<Bar>(s.bars, FullBarRest(barBeats)));
    for (const auto& [staff, bar, body] : pendingNotes) {
        if (staff < 0 || staff >= long(p.score.size()) || bar < 0 || bar >= s.bars)
            return Fail(error, "The project file is damaged (notes).");
        Bar parsed;
        if (!ParseBar(body, parsed)) return Fail(error, "The project file is damaged (notes).");
        double total = 0;
        for (const Event& ev : parsed) total += ev.beats;
        if (std::abs(total - barBeats) > 1e-6)
            return Fail(error, wxString::Format("Bar %ld has the wrong length.", bar + 1));
        p.score[staff][bar] = std::move(parsed);
    }

    out = std::move(p);
    return true;
}
