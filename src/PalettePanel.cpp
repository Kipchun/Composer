#include "PalettePanel.h"

using namespace ui;

namespace {
constexpr int kNotes = 0, kAccidentals = 1, kArticulations = 2, kDynamics = 3;
const char kArticulationCodes[] = {'.', '>', '-', '^', 'f'};
const char* const kDynamicNames[] = {"pp", "p", "mp", "mf", "f", "ff"};
constexpr int kDot = 5, kRest = 6;
const char kAccidentalCodes[] = {'#', 'b', 'n', 'x', 'B'};
const double kValues[] = {4, 2, 1, 0.5, 0.25};
}  // namespace

PalettePanel::PalettePanel(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    const auto music = [](unsigned cp, const char* tip, int px = 26) {
        return Item{Glyph(cp), tip, Face::Music, px, {}};
    };
    const auto serif = [](const char* text) {
        return Item{text, wxString("Dynamic ") + text, Face::Serif, 18, {}};
    };
    const auto plain = [](const wxString& text, const char* tip, int px = 17) {
        return Item{text, tip, Face::Plain, px, {}};
    };

    Section notes{"NOTE VALUE",
                  {music(0x1D15D, "Whole note"), music(0x1D15E, "Half note"),
                   music(0x1D15F, "Quarter note"), music(0x1D160, "Eighth note"),
                   music(0x1D161, "Sixteenth note"), plain(Glyph(0x00B7), "Dotted", 26),
                   music(0x1D13D, "Rest")}};
    notes.selected = 2;

    sections_.push_back(std::move(notes));
    sections_.push_back({"ACCIDENTAL",
                         {music(0x266F, "Sharp", 22), music(0x266D, "Flat", 22),
                          music(0x266E, "Natural", 22), music(0x1D12A, "Double sharp", 22),
                          music(0x1D12B, "Double flat", 22)}});
    sections_.push_back({"ARTICULATION",
                         {plain(Glyph(0x2022), "Staccato", 14), plain(">", "Accent", 18),
                          plain(Glyph(0x2013), "Tenuto", 18), plain("^", "Marcato", 18),
                          music(0x1D110, "Fermata", 24)}});
    sections_.push_back({"DYNAMICS",
                         {serif("pp"), serif("p"), serif("mp"), serif("mf"), serif("f"),
                          serif("ff"), plain("<", "Crescendo"), plain(">", "Diminuendo")}});
    sections_.push_back({"CLEF",
                         {music(0x1D11E, "Treble clef", 26), music(0x1D121, "Alto clef", 26),
                          music(0x1D122, "Bass clef", 26)}});

    Bind(wxEVT_PAINT, &PalettePanel::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Layout2(); Refresh(); e.Skip(); });
    Bind(wxEVT_MOTION, &PalettePanel::OnMotion, this);
    Bind(wxEVT_LEFT_DOWN, &PalettePanel::OnLeftDown, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hoverSection_ = hoverItem_ = -1;
        Refresh();
    });
}

void PalettePanel::Layout2() {
    constexpr double kPad = 12, kCell = 40, kGap = 4;
    const double width = GetClientSize().x;
    const int columns = std::max(1, int((width - 2 * kPad + kGap) / (kCell + kGap)));

    double y = 10;
    for (Section& s : sections_) {
        s.titleY = y;
        y += 22;
        int index = 0;
        for (Item& item : s.items) {
            const int col = index % columns;
            const int row = index / columns;
            item.rect = wxRect2DDouble(kPad + col * (kCell + kGap), y + row * (kCell + kGap),
                                       kCell, kCell);
            ++index;
        }
        const int rows = (index + columns - 1) / columns;
        y += rows * (kCell + kGap) + 10;
    }
}

void PalettePanel::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kPanel));
    dc.Clear();
    Gc gc = MakeGc(dc);

    const wxFont titleFont = UiFont(11, true);
    for (size_t s = 0; s < sections_.size(); ++s) {
        Section& section = sections_[s];
        DrawTextAt(*gc, section.title, 12, section.titleY, titleFont, kMuted);
        for (size_t i = 0; i < section.items.size(); ++i) {
            const Item& item = section.items[i];
            const bool selected = IsOn(int(s), int(i));
            const bool hover = int(s) == hoverSection_ && int(i) == hoverItem_;
            FillRound(*gc, item.rect, 5, selected ? kAccent : hover ? kHover : kPanelAlt);
            wxFont font = item.face == Face::Music   ? MusicFont(item.fontPx)
                          : item.face == Face::Serif ? SerifFont(item.fontPx, true, true)
                                                     : UiFont(item.fontPx, true);
            DrawCentered(*gc, item.label, item.rect, font, selected ? kAccentInk : kText);
        }
    }
}

bool PalettePanel::FindItem(const wxPoint& p, int& section, int& item) {
    for (size_t s = 0; s < sections_.size(); ++s) {
        for (size_t i = 0; i < sections_[s].items.size(); ++i) {
            if (Hit(sections_[s].items[i].rect, p)) {
                section = int(s);
                item = int(i);
                return true;
            }
        }
    }
    section = item = -1;
    return false;
}

void PalettePanel::OnMotion(wxMouseEvent& e) {
    int s, i;
    FindItem(e.GetPosition(), s, i);
    if (s == hoverSection_ && i == hoverItem_) return;
    hoverSection_ = s;
    hoverItem_ = i;
    SetToolTip(s >= 0 ? sections_[s].items[i].tip : wxString());
    SetCursor(s >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW);
    Refresh();
}

void PalettePanel::OnLeftDown(wxMouseEvent& e) {
    int s, i;
    if (!FindItem(e.GetPosition(), s, i)) return;
    if (s == kNotes && i == kDot) {
        dotted_ = !dotted_;
    } else if (s == kNotes && i == kRest) {
        rest_ = !rest_;
    } else if (s == kNotes) {
        sections_[s].selected = i;  // a note value is always chosen
    } else {
        sections_[s].selected = sections_[s].selected == i ? -1 : i;
    }
    if (s == kNotes || s == kAccidentals) NotifyInput();

    if ((s == kArticulations || s == kDynamics) && onMark) {
        sections_[s == kArticulations ? kDynamics : kArticulations].selected = -1;
        const int on = sections_[s].selected;
        char articulation = 0;
        wxString dynamic;
        int hairpin = 0;
        if (on >= 0 && s == kArticulations) articulation = kArticulationCodes[on];
        else if (on >= 0 && on < 6) dynamic = kDynamicNames[on];
        else if (on >= 6) hairpin = on == 6 ? 1 : -1;
        onMark(articulation, dynamic, hairpin);
    }
    if (onTool) onTool(sections_[s].items[i].tip);
    Refresh();
}

bool PalettePanel::IsOn(int section, int item) const {
    if (section == kNotes && item == kDot) return dotted_;
    if (section == kNotes && item == kRest) return rest_;
    return sections_[section].selected == item;
}

PalettePanel::NoteInput PalettePanel::GetNoteInput() const {
    const int accidental = sections_[kAccidentals].selected;
    return {kValues[sections_[kNotes].selected] * (dotted_ ? 1.5 : 1.0), rest_,
            accidental >= 0 ? kAccidentalCodes[accidental] : char(0)};
}

void PalettePanel::NotifyInput() {
    if (onNoteInputChanged) onNoteInputChanged();
    Refresh();
}

void PalettePanel::SetDuration(int index) {
    sections_[kNotes].selected = index;
    NotifyInput();
}

void PalettePanel::ToggleDot() {
    dotted_ = !dotted_;
    NotifyInput();
}

void PalettePanel::SetAccidental(char accidental) {
    int index = -1;
    for (int i = 0; i < 5; ++i)
        if (kAccidentalCodes[i] == accidental) index = i;
    int& selected = sections_[kAccidentals].selected;
    selected = selected == index ? -1 : index;
    NotifyInput();
}

void PalettePanel::ClearAccidental() {
    sections_[kAccidentals].selected = -1;
    NotifyInput();
}

void PalettePanel::ClearMarks() {
    sections_[kArticulations].selected = -1;
    sections_[kDynamics].selected = -1;
    Refresh();
}
