#include "ScoreCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

using namespace ui;

namespace {

constexpr double kPageW = 1040, kPageH = 1290, kMargin = 24;
constexpr double kSysX = 150, kSysW = 830;
constexpr double kLine = 8, kStaffH = 32;  // staff line gap and height
constexpr double kFirstTop = 215, kNextTop = 80, kBottom = 80, kSystemGap = 96;
constexpr double kStaffGap = 44, kMinStaffGap = 20, kGrandGapRatio = 0.68;
constexpr int kMaxStep = 12;
constexpr double kEps = 1e-6, kPitchReach = 62;


// The longest writable value that fits; there are no ties yet.
double LargestValueWithin(double beats) {
    for (double value : {4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.25})
        if (value <= beats + kEps) return value;
    return 0.25;
}

bool IsDotted(double beats) { return beats == 3.0 || beats == 1.5 || beats == 0.75; }

// Flexible space after a column, on top of its minimum: each halving of the note value loses a fixed amount, so short
// notes get far more room than their duration alone would give them.
double ColumnGap(double beats) { return 3 + 11 * std::log2(beats / 0.25); }

// Where the i-th sign of a key signature sits, written for each clef.
int KeySignatureStep(Clef clef, bool sharps, int i) {
    static const int kTrebleSharps[7] = {4, 1, 5, 2, -1, 3, 0};
    static const int kTrebleFlats[7] = {0, 3, -1, 2, -2, 1, -3};
    const int treble = sharps ? kTrebleSharps[i] : kTrebleFlats[i];
    return treble - (clef == Clef::Bass ? 2 : clef == Clef::Alto ? 1 : 0);
}

// How finely the mouse snaps for a given note value.
double SnapGrid(double beats) {
    if (beats >= 4) return 4;
    if (beats == 1.5) return 0.5;
    if (beats == 0.75) return 0.25;
    return beats < 1 ? beats : 1;
}

double StepY(double top, int step) { return top + kStaffH / 2 - step * kLine / 2; }

// Accidentals are drawn as shapes so they stay crisp and ink-coloured at any zoom.
void DrawFlat(wxGraphicsContext& gc, double x, double y, const wxColour& ink = kInk) {
    Line(gc, x, y - 14, x, y + 5, ink, 1.1);
    wxGraphicsPath bowl = gc.CreatePath();
    bowl.MoveToPoint(x, y + 5);
    bowl.AddCurveToPoint(x + 9, y - 1, x + 7, y - 6, x, y - 2);
    bowl.AddCurveToPoint(x + 4.8, y - 4, x + 5.5, y - 1, x, y + 3);
    bowl.CloseSubpath();
    NoPen(gc);
    gc.SetBrush(wxBrush(ink));
    gc.FillPath(bowl);
}

void DrawAccidental(wxGraphicsContext& gc, double x, double y, char accidental,
                    const wxColour& ink) {
    switch (accidental) {
        case '#':
            Line(gc, x - 2, y - 9, x - 2, y + 10, ink, 1);
            Line(gc, x + 2, y - 10, x + 2, y + 9, ink, 1);
            Line(gc, x - 5, y - 2, x + 5, y - 5, ink, 2.4);
            Line(gc, x - 5, y + 5, x + 5, y + 2, ink, 2.4);
            break;
        case 'b':
            DrawFlat(gc, x - 3, y, ink);
            break;
        case 'B':
            DrawFlat(gc, x - 8, y, ink);
            DrawFlat(gc, x - 1, y, ink);
            break;
        case 'n':
            Line(gc, x - 3, y - 10, x - 3, y + 5, ink, 1);
            Line(gc, x + 3, y - 5, x + 3, y + 10, ink, 1);
            Line(gc, x - 3, y - 3, x + 3, y - 5, ink, 2.2);
            Line(gc, x - 3, y + 5, x + 3, y + 3, ink, 2.2);
            break;
        case 'x':
            Line(gc, x - 3.5, y - 3.5, x + 3.5, y + 3.5, ink, 1.8);
            Line(gc, x - 3.5, y + 3.5, x + 3.5, y - 3.5, ink, 1.8);
            break;
        default:
            break;
    }
}

}  // namespace

ScoreCanvas::ScoreCanvas(wxWindow* parent)
    : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL | wxHSCROLL | wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(16, 16);

    SetProject(MakeDemoProject());

    Bind(wxEVT_PAINT, &ScoreCanvas::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &ScoreCanvas::OnLeftDown, this);
    Bind(wxEVT_MOUSEWHEEL, &ScoreCanvas::OnWheel, this);
    Bind(wxEVT_MOTION, &ScoreCanvas::OnMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        if (hover_.bar >= 0) {
            hover_ = Target{};
            Refresh();
        }
    });
}

void ScoreCanvas::SetProject(const Project& project) {
    project_ = project;
    undo_.clear();
    redo_.clear();
    preview_.clear();
    selectedBar_ = selectedStaff_ = -1;
    playheadVisible_ = false;
    followSystem_ = -1;
    noteInput_ = false;
    hover_ = lastEntered_ = Target{};
    SetCursor(wxCURSOR_ARROW);
    RefreshLayout();
    Scroll(0, 0);
}

bool ScoreCanvas::WouldLoseNotes(const ProjectSettings& next) const {
    const double nextBeats = next.BarBeats();
    for (const auto& staff : score_) {
        for (int bar = 0; bar < int(staff.size()); ++bar) {
            double pos = 0;
            for (const Event& ev : staff[bar]) {
                if (!ev.rest && (bar >= next.bars || pos + ev.beats > nextBeats + kEps)) return true;
                pos += ev.beats;
            }
        }
    }
    return false;
}

void ScoreCanvas::ApplySettings(const ProjectSettings& next) {
    const double oldBeats = BarBeats();
    project_.settings = next;
    const double beats = next.BarBeats();

    for (int staff = 0; staff < StaffCount(); ++staff) {
        score_[staff].resize(next.bars, FullBarRest(oldBeats));
        if (std::abs(oldBeats - beats) > kEps)
            for (int bar = 0; bar < next.bars; ++bar)
                WriteRange(staff, bar, beats, oldBeats + beats, nullptr);
    }
    // Changing the shape of the score invalidates undo snapshots and the bar selection.
    undo_.clear();
    redo_.clear();
    if (selectedBar_ >= next.bars) selectedBar_ = selectedStaff_ = -1;
    if (caret_.bar >= next.bars) caret_.bar = next.bars - 1;
    if (caret_.beat >= beats) caret_.beat = 0;
    if (std::abs(oldBeats - beats) > kEps) project_.demo = false;  // its proposal no longer fits
    preview_.clear();
    hover_ = lastEntered_ = Target{};
    RefreshLayout();
    if (onChanged) onChanged();
}

void ScoreCanvas::RefreshLayout() {
    LayoutPages();
    ComputeLayout();
    UpdateVirtualSize();
    Refresh();
}

wxString ScoreCanvas::StaffName(int staff) const { return StaffLabel(project_, staff); }

void ScoreCanvas::UpdateVirtualSize() {
    SetVirtualSize(int((kPageW + kMargin * 2) * zoom_), int((PageTop(pageCount_)) * zoom_));
}

// Staves squeeze together so a system fits a page, and the page grows past that limit.
void ScoreCanvas::LayoutPages() {
    const int count = StaffCount();
    const double usable = kPageH - kFirstTop - kBottom;
    double gap = kStaffGap;
    if (count > 1 && count * kStaffH + (count - 1) * kStaffGap > usable)
        gap = std::max(kMinStaffGap, (usable - count * kStaffH) / (count - 1));

    staffOffset_.assign(count, 0);
    double y = 0;
    for (int i = 0; i < count; ++i) {
        staffOffset_[i] = y;
        y += kStaffH;
        if (i + 1 < count) y += project_.staves[i].braceToNext ? gap * kGrandGapRatio : gap;
    }
    systemH_ = y;
    pageH_ = std::max(kPageH, kFirstTop + systemH_ + kBottom);

    systems_.clear();
    int page = 0;
    y = kFirstTop;
    const int perLine = project_.settings.barsPerLine;
    for (int first = 0; first < BarCount(); first += perLine) {
        if (!systems_.empty() && y + systemH_ > pageH_ - kBottom) {
            ++page;
            y = kNextTop;
        }
        systems_.push_back({first, std::min(perLine, BarCount() - first), page, y});
        y += systemH_ + kSystemGap;
    }
    pageCount_ = page + 1;
}

void ScoreCanvas::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.3, 2.0);
    UpdateVirtualSize();
    Refresh();
    if (onZoom) onZoom(zoom_);
}

void ScoreCanvas::ShowProposal(const Proposal& proposal) {
    preview_.clear();
    for (const BarEdit& edit : proposal.edits) preview_[{edit.staff, edit.bar}] = edit.replacement;
    ComputeLayout();
    Refresh();
}

void ScoreCanvas::HideProposal() {
    preview_.clear();
    ComputeLayout();
    Refresh();
}

bool ScoreCanvas::IsProposalStale(const Proposal& proposal) const {
    for (const BarEdit& edit : proposal.edits) {
        if (edit.staff >= StaffCount() || edit.bar >= BarCount() ||
            !BarsEqual(score_[edit.staff][edit.bar], edit.original))
            return true;
    }
    return false;
}

void ScoreCanvas::ApplyProposal(const Proposal& proposal) {
    if (IsProposalStale(proposal)) return;
    PushUndo();
    for (const BarEdit& edit : proposal.edits) score_[edit.staff][edit.bar] = edit.replacement;
    preview_.clear();
    Changed();
}

void ScoreCanvas::SetPlayhead(double beats, bool visible) {
    playhead_ = beats;
    playheadVisible_ = visible;
    if (visible) {
        // Turn to each new line of music as playback reaches it.
        const int system = std::min(SystemOfBar(int(beats / BarBeats())), int(systems_.size()) - 1);
        if (system != followSystem_) {
            followSystem_ = system;
            int ppuX, ppuY;
            GetScrollPixelsPerUnit(&ppuX, &ppuY);
            const double y = PageTop(systems_[system].page) + systems_[system].top - 50;
            if (ppuY > 0) Scroll(-1, std::max(0, int(y * zoom_)) / ppuY);
        }
    } else {
        followSystem_ = -1;
    }
    Refresh();
}

void ScoreCanvas::SetSelection(int bar, int staff) {
    selectedBar_ = bar;
    selectedStaff_ = staff;
    Refresh();
}

double ScoreCanvas::PageX() const {
    return std::max(kMargin, (GetClientSize().x / zoom_ - kPageW) / 2);
}

double ScoreCanvas::PageTop(int page) const { return kMargin + page * (pageH_ + kMargin); }

double ScoreCanvas::StaffTop(int system, int staff) const {
    return systems_[system].top + staffOffset_[staff];
}

double ScoreCanvas::PrefixWidth(int system) const {
    return 40 + std::abs(project_.settings.fifths) * 9 + (system == 0 ? 36 : 12);
}

// Lays out each system: every note gets a minimum width and long notes share what's left.
void ScoreCanvas::ComputeLayout() {
    struct Column {
        bool accidental = false;
        double extra = 0;  // room for a dot or flag to the right
    };
    constexpr double kLead = 18, kAccidentalRoom = 14, kMinColumn = 17;
    const double barBeats = BarBeats();
    const int perLine = project_.settings.barsPerLine;

    layout_.assign(BarCount(), BarLayout{});
    for (int system = 0; system < int(systems_.size()); ++system) {
        const int first = systems_[system].firstBar;
        const int count = systems_[system].barCount;
        std::vector<std::map<double, Column>> columns(count);
        std::vector<std::vector<double>> minimums(count), flexible(count);
        std::vector<double> leads(count);
        double totalMin = 0, totalFlex = 0;

        for (int b = 0; b < count; ++b) {
            for (int staff = 0; staff < StaffCount(); ++staff) {
                double pos = 0;
                for (const Event& ev : EventsFor(staff, first + b)) {
                    Column& column = columns[b][pos];
                    for (char accidental : ev.accidentals)
                        if (accidental) column.accidental = true;
                    if (ev.dotted) column.extra = std::max(column.extra, 7.0);
                    if (!ev.rest && ev.beats < 1) column.extra = std::max(column.extra, 5.0);
                    pos += ev.beats;
                }
            }

            leads[b] = kLead + (columns[b].begin()->second.accidental ? kAccidentalRoom : 0);
            totalMin += leads[b];
            for (auto it = columns[b].begin(); it != columns[b].end(); ++it) {
                const auto next = std::next(it);
                const double nextBeat = next == columns[b].end() ? barBeats : next->first;
                double minimum = kMinColumn + it->second.extra;
                if (next != columns[b].end() && next->second.accidental) minimum += kAccidentalRoom;
                minimums[b].push_back(minimum);
                flexible[b].push_back(ColumnGap(nextBeat - it->first));
                totalMin += minimum;
                totalFlex += flexible[b].back();
            }
        }

        // A short final line isn't stretched across the whole page.
        const double prefix = PrefixWidth(system);
        const double available = (kSysW - prefix) * count / perLine;
        const double share = std::max(0.0, available - totalMin) / totalFlex;
        const double scale = std::min(1.0, available / totalMin);

        double x = kSysX + prefix;
        for (int b = 0; b < count; ++b) {
            BarLayout& bar = layout_[first + b];
            bar.x = x;
            double columnX = x + leads[b] * scale;
            size_t i = 0;
            for (const auto& entry : columns[b]) {
                bar.columns.emplace_back(entry.first, columnX);
                columnX += (minimums[b][i] + flexible[b][i] * share) * scale;
                ++i;
            }
            bar.width = columnX - x;
            x = columnX;
        }
    }
}

int ScoreCanvas::BarAtX(int system, double x) const {
    for (int b = 0; b < systems_[system].barCount; ++b) {
        const int bar = systems_[system].firstBar + b;
        if (x >= BarX(bar) && x < BarX(bar) + BarWidth(bar)) return bar;
    }
    return -1;
}

double ScoreCanvas::BeatX(int bar, double beat) const {
    const auto& columns = layout_[bar].columns;
    for (size_t i = 0; i < columns.size(); ++i) {
        const double startBeat = columns[i].first, startX = columns[i].second;
        const bool last = i + 1 == columns.size();
        const double endBeat = last ? BarBeats() : columns[i + 1].first;
        const double endX = last ? BarX(bar) + BarWidth(bar) : columns[i + 1].second;
        if (beat <= endBeat + kEps || last)
            return startX + (beat - startBeat) / (endBeat - startBeat) * (endX - startX);
    }
    return BarX(bar);
}

bool ScoreCanvas::IsProposalBar(int staff, int bar) const { return preview_.count({staff, bar}) > 0; }

const std::vector<ScoreCanvas::Event>& ScoreCanvas::EventsFor(int staff, int bar) const {
    if (!preview_.empty()) {
        const auto it = preview_.find({staff, bar});
        if (it != preview_.end()) return it->second;
    }
    return score_[staff][bar];
}

void ScoreCanvas::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBg));
    dc.Clear();

    Gc gc = MakeGc(dc);
    const wxPoint origin = CalcScrolledPosition(wxPoint(0, 0));
    const double viewTop = -origin.y / zoom_;
    const double viewBottom = viewTop + GetClientSize().y / zoom_;

    gc->PushState();
    gc->Translate(origin.x, origin.y);
    gc->Scale(zoom_, zoom_);
    gc->Translate(PageX(), 0);
    for (int page = 0; page < pageCount_; ++page) {
        const double top = PageTop(page);
        if (top > viewBottom || top + pageH_ < viewTop) continue;
        gc->PushState();
        gc->Translate(0, top);
        DrawPage(*gc, page, viewTop - top, viewBottom - top);
        gc->PopState();
    }
    gc->PopState();
    if (noteInput_) DrawInputBanner(*gc);
}

void ScoreCanvas::DrawPage(wxGraphicsContext& gc, int page, double viewTop, double viewBottom) {
    FillRound(gc, wxRect2DDouble(4, 6, kPageW, pageH_), 2, wxColour(0, 0, 0, 90));
    FillRound(gc, wxRect2DDouble(0, 0, kPageW, pageH_), 2, kPaper);

    double w, h, d, l;
    if (page == 0) {
        const ProjectSettings& s = project_.settings;
        gc.SetFont(SerifFont(38), kInk);
        gc.GetTextExtent(s.title, &w, &h, &d, &l);
        gc.DrawText(s.title, (kPageW - w) / 2, 62);

        gc.SetFont(SerifFont(15, false, true), kInk);
        gc.GetTextExtent(s.subtitle, &w, &h, &d, &l);
        gc.DrawText(s.subtitle, (kPageW - w) / 2, 112);

        gc.SetFont(SerifFont(14), kInk);
        gc.GetTextExtent(s.composer, &w, &h, &d, &l);
        gc.DrawText(s.composer, kSysX + kSysW - w, 148);

        const wxFont tempoFont = SerifFont(15, true);
        DrawTextAt(gc, s.tempoText, kSysX + 6, 150, tempoFont, kInk);
        const double textW = s.tempoText.empty() ? 0 : TextWidth(gc, s.tempoText, tempoFont) + 10;
        DrawTextAt(gc, Glyph(0x2669) + wxString::Format(" = %d", s.tempo), kSysX + 6 + textW, 149,
                   MusicFont(15), kInk);
    }

    for (int system = 0; system < int(systems_.size()); ++system) {
        if (systems_[system].page != page) continue;
        const double top = systems_[system].top;
        if (top - 60 > viewBottom || top + systemH_ + 60 < viewTop) continue;
        DrawSystem(gc, system);
    }

    const wxString number = wxString::Format("%d", page + 1);
    gc.SetFont(SerifFont(12), kMuted);
    gc.GetTextExtent(number, &w, &h, &d, &l);
    gc.DrawText(number, (kPageW - w) / 2, pageH_ - 42);
}

void ScoreCanvas::DrawPrefix(wxGraphicsContext& gc, int system, int staff) {
    const double top = StaffTop(system, staff);
    const Clef clef = project_.staves[staff].clef;
    const ProjectSettings& s = project_.settings;

    if (clef == Clef::Bass)
        DrawBaseline(gc, Glyph(0x1D122), kSysX + 7, top + 24, MusicFont(34), kInk);
    else if (clef == Clef::Alto)
        DrawBaseline(gc, Glyph(0x1D121), kSysX + 7, top + 26, MusicFont(36), kInk);
    else
        DrawBaseline(gc, Glyph(0x1D11E), kSysX + 7, top + 24, MusicFont(40), kInk);

    const bool sharps = s.fifths > 0;
    double x = kSysX + 40;
    for (int i = 0; i < std::abs(s.fifths); ++i) {
        const double y = StepY(top, KeySignatureStep(clef, sharps, i));
        if (sharps) DrawAccidental(gc, x + 5, y, '#', kInk);
        else DrawFlat(gc, x + 2, y);
        x += 9;
    }

    if (system == 0) {
        const wxFont digit = SerifFont(20, true);
        DrawCentered(gc, wxString::Format("%d", s.timeTop), wxRect2DDouble(x + 6, top - 1, 22, 16), digit, kInk);
        DrawCentered(gc, wxString::Format("%d", s.timeBottom), wxRect2DDouble(x + 6, top + 15, 22, 16), digit, kInk);
    }
}

void ScoreCanvas::DrawSystem(wxGraphicsContext& gc, int system) {
    const System& sys = systems_[system];
    const int staves = StaffCount();
    const int firstBar = sys.firstBar;
    const int endBar = firstBar + sys.barCount;
    const double firstTop = StaffTop(system, 0);
    const double lastBottom = StaffTop(system, staves - 1) + kStaffH;
    const double lineEnd = BarX(endBar - 1) + BarWidth(endBar - 1);

    // Highlights go underneath the notation.
    if (selectedBar_ >= firstBar && selectedBar_ < endBar) {
        double y = firstTop - 10, height = lastBottom - firstTop + 20;
        if (selectedStaff_ >= 0) {
            y = StaffTop(system, selectedStaff_) - 10;
            height = kStaffH + 20;
        }
        FillRound(gc, wxRect2DDouble(BarX(selectedBar_) + 1, y, BarWidth(selectedBar_) - 2, height), 3,
                  WithAlpha(kSelect, 42));
    }
    if (!preview_.empty()) {
        bool tagged = false;
        for (int staff = 0; staff < staves; ++staff) {
            for (int bar = firstBar; bar < endBar;) {
                if (!IsProposalBar(staff, bar)) {
                    ++bar;
                    continue;
                }
                int end = bar;
                while (end + 1 < endBar && IsProposalBar(staff, end + 1)) ++end;
                const double top = StaffTop(system, staff);
                const wxRect2DDouble r(BarX(bar) + 1, top - 12, BarX(end) + BarWidth(end) - BarX(bar) - 2, kStaffH + 24);
                FillRound(gc, r, 4, WithAlpha(kProposal, 38));
                StrokeRound(gc, r, 4, kProposal, 1.2);
                if (!tagged) {
                    const wxRect2DDouble tag(r.m_x, r.m_y + r.m_height + 2, 150, 19);
                    FillRound(gc, tag, 3, kProposal);
                    DrawCentered(gc, "Director proposal", tag, UiFont(12, true), *wxWHITE);
                    tagged = true;
                }
                bar = end + 1;
            }
        }
    }

    for (int staff = 0; staff < staves; ++staff) {
        const double top = StaffTop(system, staff);
        const StaffDef& def = project_.staves[staff];
        for (int i = 0; i < 5; ++i) Line(gc, kSysX, top + i * kLine, lineEnd, top + i * kLine, kInk, 0.9);

        if (!def.name.empty()) {
            const double y = def.braceToNext
                                 ? top + (staffOffset_[staff + 1] - staffOffset_[staff] + kStaffH) / 2 - 9
                                 : top + 6;
            gc.SetFont(SerifFont(14), kInk);
            double w, h, d, l;
            gc.GetTextExtent(def.name, &w, &h, &d, &l);
            gc.DrawText(def.name, kSysX - 26 - w, y);
        }

        DrawPrefix(gc, system, staff);

        const double joinTo = def.braceToNext ? staffOffset_[staff + 1] - staffOffset_[staff] : kStaffH;
        for (int b = firstBar + 1; b <= endBar; ++b) {
            const double x = b == endBar ? lineEnd : BarX(b);
            if (b == BarCount()) {
                Line(gc, x - 6, top, x - 6, top + kStaffH, kInk, 1);
                Line(gc, x - 1.5, top, x - 1.5, top + kStaffH, kInk, 3.5);
            } else {
                Line(gc, x, top, x, top + joinTo, kInk, 1);
            }
        }

        for (int bar = firstBar; bar < endBar; ++bar) {
            const bool proposed = IsProposalBar(staff, bar);
            DrawEvents(gc, EventsFor(staff, bar), bar, top, proposed ? kProposal : kInk);
        }
    }

    // Left system line and grand-staff braces.
    Line(gc, kSysX, firstTop, kSysX, lastBottom, kInk, 1.3);
    for (int staff = 0; staff + 1 < staves; ++staff) {
        if (!project_.staves[staff].braceToNext) continue;
        const double braceTop = StaffTop(system, staff);
        const double braceBottom = StaffTop(system, staff + 1) + kStaffH;
        const double mid = (braceTop + braceBottom) / 2;
        wxGraphicsPath brace = gc.CreatePath();
        const double bx = kSysX - 4;
        brace.MoveToPoint(bx, braceTop);
        brace.AddCurveToPoint(bx - 15, braceTop + 8, bx + 1, mid - 10, bx - 11, mid);
        brace.AddCurveToPoint(bx + 1, mid + 10, bx - 15, braceBottom - 8, bx, braceBottom);
        brace.AddCurveToPoint(bx - 11, braceBottom - 8, bx + 5, mid + 10, bx - 7, mid);
        brace.AddCurveToPoint(bx + 5, mid - 10, bx - 11, braceTop + 8, bx, braceTop);
        NoPen(gc);
        gc.SetBrush(wxBrush(kInk));
        gc.FillPath(brace);
    }

    for (int bar = firstBar; bar < endBar; ++bar)
        DrawTextAt(gc, wxString::Format("%d", bar + 1), BarX(bar) + 3, firstTop - 24,
                   SerifFont(12, false, true), kMuted);

    for (const Section& section : project_.sections) {
        if (section.bar < firstBar || section.bar >= endBar) continue;
        const double x = section.bar == firstBar ? kSysX + 1 : BarX(section.bar) + 1;
        const wxRect2DDouble mark(x, firstTop - 34, 25, 24);
        StrokeRound(gc, mark, 1, kInk, 1.4);
        DrawCentered(gc, section.label, mark, SerifFont(16, true), kInk);
    }

    for (const Marking& m : project_.markings) {
        if (m.bar < firstBar || m.bar >= endBar || m.staff >= staves) continue;
        DrawTextAt(gc, m.text, BeatX(m.bar, m.beat) - 5, StaffTop(system, m.staff) + kStaffH + 16,
                   SerifFont(16, true, true), kInk);
    }

    for (const Slur& slur : project_.slurs) {
        if (slur.bar < firstBar || slur.bar >= endBar || slur.staff >= staves) continue;
        const double top = StaffTop(system, slur.staff);
        const double x1 = BeatX(slur.bar, slur.from);
        const double x2 = BeatX(slur.bar, slur.to) + 4;
        wxGraphicsPath path = gc.CreatePath();
        path.MoveToPoint(x1, top - 2);
        path.AddCurveToPoint(x1 + 26, top - 25, x2 - 26, top - 25, x2, top - 9);
        path.AddCurveToPoint(x2 - 26, top - 22, x1 + 26, top - 22, x1, top - 2);
        NoPen(gc);
        gc.SetBrush(wxBrush(kInk));
        gc.FillPath(path);
    }

    if (noteInput_) {
        if (caret_.bar >= firstBar && caret_.bar < endBar) {
            const double top = StaffTop(system, caret_.staff);
            const double x = BeatX(caret_.bar, caret_.beat);
            FillRound(gc, wxRect2DDouble(x - 11, top - 12, 22, kStaffH + 24), 3, WithAlpha(kSelect, 46));
            Line(gc, x - 11, top - 12, x - 11, top + kStaffH + 12, kSelect, 2);
            if (hover_.bar < 0) DrawGhost(gc, x, top, caret_.step, WrittenLength(caret_.beat));
        }
        if (hover_.bar >= firstBar && hover_.bar < endBar)
            DrawGhost(gc, BeatX(hover_.bar, hover_.beat), StaffTop(system, hover_.staff), hover_.step,
                      WrittenLength(hover_.beat));
    }

    if (playheadVisible_) {
        const int bar = std::min(int(playhead_ / BarBeats()), BarCount() - 1);
        if (bar >= firstBar && bar < endBar) {
            const double x = BeatX(bar, playhead_ - bar * BarBeats()) - 2;
            FillRound(gc, wxRect2DDouble(x - 5, firstTop - 18, 12, lastBottom - firstTop + 36), 3,
                      WithAlpha(kAccent, 60));
            Line(gc, x, firstTop - 18, x, lastBottom + 18, kAccent, 2.2);
        }
    }
}

void ScoreCanvas::DrawHead(wxGraphicsContext& gc, double x, double y, double beats,
                           const wxColour& ink) {
    const bool whole = beats >= 4;
    const bool hollow = beats >= 2;
    const double rx = whole ? 6.6 : 5.5, ry = whole ? 4.0 : 3.9;

    wxGraphicsPath path = gc.CreatePath();
    path.AddEllipse(-rx, -ry, rx * 2, ry * 2);
    if (hollow) {
        const double ix = whole ? 3.6 : 4.5, iy = whole ? 2.4 : 1.9;
        path.AddEllipse(-ix, -iy, ix * 2, iy * 2);
    }
    gc.PushState();
    gc.Translate(x, y);
    gc.Rotate(whole ? 0 : -0.36);
    NoPen(gc);
    gc.SetBrush(wxBrush(ink));
    gc.FillPath(path, wxODDEVEN_RULE);
    gc.PopState();
}

void ScoreCanvas::DrawRest(wxGraphicsContext& gc, double x, double top, double beats,
                           const wxColour& ink) {
    NoPen(gc);
    gc.SetBrush(wxBrush(ink));
    if (beats >= 4) {
        gc.DrawRectangle(x - 6.5, top + kLine, 13, 4.5);
    } else if (beats >= 2) {
        gc.DrawRectangle(x - 6, top + 2 * kLine - 4.5, 13, 4.5);
    } else if (beats >= 1) {
        DrawBaseline(gc, Glyph(0x1D13D), x - 5, top + 24, MusicFont(30), ink);
    } else if (beats >= 0.5) {
        DrawBaseline(gc, Glyph(0x1D13E), x - 5, top + 22, MusicFont(30), ink);
    } else {
        DrawBaseline(gc, Glyph(0x1D13F), x - 5, top + 22, MusicFont(30), ink);
    }
}

void ScoreCanvas::DrawEvents(wxGraphicsContext& gc, const std::vector<Event>& events, int bar,
                             double top, const wxColour& ink) {
    constexpr double kHeadX = 5.0, kStem = 27;

    struct Placed {
        double x = 0, beat = 0;
        bool up = false;
        double anchorY = 0, tipY = 0;  // stem ends
        double loY = 0, hiY = 0;       // lowest and highest noteheads
        double stemX = 0;
    };
    std::vector<Placed> placed(events.size());

    double beat = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const Event& ev = events[i];
        Placed& p = placed[i];
        p.beat = beat;
        p.x = BeatX(bar, beat);
        beat += ev.beats;
        if (ev.rest) {
            // A whole-bar rest is centred in the bar.
            const bool fullBar = ev.beats >= BarBeats() - kEps;
            DrawRest(gc, fullBar ? BarX(bar) + BarWidth(bar) / 2 : p.x, top, fullBar ? 4 : ev.beats, ink);
            continue;
        }

        double sum = 0;
        int lo = ev.steps[0], hi = ev.steps[0];
        for (size_t n = 0; n < ev.steps.size(); ++n) {
            const int s = ev.steps[n];
            sum += s;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
            DrawHead(gc, p.x, StepY(top, s), ev.beats, ink);
            if (ev.accidentals[n])
                DrawAccidental(gc, p.x - 15, StepY(top, s), ev.accidentals[n], ink);
            for (int ledger = -6; ledger >= s; ledger -= 2)
                Line(gc, p.x - 9, StepY(top, ledger), p.x + 9, StepY(top, ledger), ink, 1);
            for (int ledger = 6; ledger <= s; ledger += 2)
                Line(gc, p.x - 9, StepY(top, ledger), p.x + 9, StepY(top, ledger), ink, 1);
            if (ev.dotted) {
                const int dotStep = (s % 2 == 0) ? s + 1 : s;
                NoPen(gc);
                gc.SetBrush(wxBrush(ink));
                gc.DrawEllipse(p.x + 8, StepY(top, dotStep) - 2, 4, 4);
            }
        }
        p.up = sum / ev.steps.size() < 0;
        p.stemX = p.x + (p.up ? kHeadX : -kHeadX);
        p.loY = StepY(top, lo);
        p.hiY = StepY(top, hi);
        p.anchorY = StepY(top, p.up ? lo : hi);
        const double stemEnd = StepY(top, p.up ? hi : lo);
        p.tipY = stemEnd + (p.up ? -kStem : kStem);
    }

    // Stems, beams and flags. Sub-beat notes that share a beat are beamed together.
    const auto fillQuad = [&](double x1, double y1, double x2, double y2, double thick) {
        wxGraphicsPath beam = gc.CreatePath();
        beam.MoveToPoint(x1, y1);
        beam.AddLineToPoint(x2, y2);
        beam.AddLineToPoint(x2, y2 + thick);
        beam.AddLineToPoint(x1, y1 + thick);
        beam.CloseSubpath();
        NoPen(gc);
        gc.SetBrush(wxBrush(ink));
        gc.FillPath(beam);
    };

    const double beamUnit = project_.settings.BeamUnit();
    for (size_t i = 0; i < events.size();) {
        const Event& ev = events[i];
        if (ev.rest || ev.beats >= 4) {
            ++i;
            continue;
        }

        size_t end = i + 1;
        if (ev.beats < 1) {
            while (end < events.size() && !events[end].rest && events[end].beats < 1 &&
                   std::floor(placed[end].beat / beamUnit) == std::floor(placed[i].beat / beamUnit))
                ++end;
        }

        if (end - i < 2) {
            Placed& p = placed[i];
            Line(gc, p.stemX, p.anchorY, p.stemX, p.tipY, ink, 1.2);
            if (ev.beats < 1) {
                const double dir = p.up ? 1 : -1;
                const int flags = ev.beats < 0.5 ? 2 : 1;
                for (int n = 0; n < flags; ++n) {
                    const double tip = p.tipY + dir * n * 8;
                    wxGraphicsPath flag = gc.CreatePath();
                    flag.MoveToPoint(p.stemX, tip);
                    flag.AddCurveToPoint(p.stemX + 2, tip + dir * 10, p.stemX + 14, tip + dir * 12,
                                         p.stemX + 8, tip + dir * 24);
                    flag.AddCurveToPoint(p.stemX + 10, tip + dir * 15, p.stemX + 3, tip + dir * 14,
                                         p.stemX, tip + dir * 9);
                    flag.CloseSubpath();
                    NoPen(gc);
                    gc.SetBrush(wxBrush(ink));
                    gc.FillPath(flag);
                }
            }
            ++i;
            continue;
        }

        // The whole group shares one stem direction.
        double sum = 0;
        int count = 0;
        for (size_t k = i; k < end; ++k)
            for (int s : events[k].steps) {
                sum += s;
                ++count;
            }
        const bool up = sum / count < 0;
        for (size_t k = i; k < end; ++k) {
            Placed& n = placed[k];
            n.up = up;
            n.stemX = n.x + (up ? kHeadX : -kHeadX);
            n.anchorY = up ? n.loY : n.hiY;
            n.tipY = up ? n.hiY - kStem : n.loY + kStem;
        }

        // Fit a gently sloped beam that every stem can reach.
        Placed& first = placed[i];
        Placed& last = placed[end - 1];
        const double slope = std::clamp(last.tipY - first.tipY, -6.0, 6.0);
        const double span = std::max(1.0, last.stemX - first.stemX);
        const auto lineAt = [&](double x) { return first.tipY + slope * (x - first.stemX) / span; };
        double shift = 0;
        for (size_t k = i; k < end; ++k) {
            const double gap = placed[k].tipY - lineAt(placed[k].stemX);
            shift = up ? std::min(shift, gap) : std::max(shift, gap);
        }
        const double y0 = lineAt(first.stemX) + shift;
        const auto beamY = [&](double x) { return y0 + slope * (x - first.stemX) / span; };

        for (size_t k = i; k < end; ++k)
            Line(gc, placed[k].stemX, placed[k].anchorY, placed[k].stemX, beamY(placed[k].stemX),
                 ink, 1.2);

        const double thick = up ? 4.5 : -4.5;
        fillQuad(first.stemX - 0.6, beamY(first.stemX), last.stemX + 0.6, beamY(last.stemX), thick);

        // Second beam for sixteenths: joined to a neighbour, or a short stub.
        const double offset = up ? 7 : -7;
        for (size_t k = i; k < end; ++k) {
            if (events[k].beats >= 0.5) continue;
            const double x = placed[k].stemX;
            if (k + 1 < end && events[k + 1].beats < 0.5) {
                const double x2 = placed[k + 1].stemX;
                fillQuad(x, beamY(x) + offset, x2, beamY(x2) + offset, thick);
            } else if (k == i || events[k - 1].beats >= 0.5) {
                const double x2 = k == i ? x + 8 : x - 8;
                fillQuad(x, beamY(x) + offset, x2, beamY(x2) + offset, thick);
            }
        }
        i = end;
    }
}

void ScoreCanvas::OnLeftDown(wxMouseEvent& event) {
    SetFocus();
    if (noteInput_) {
        Target t;
        if (HitTest(event.GetPosition(), t)) ClickNote(t);
        return;
    }

    double x, y;
    int page;
    int hitBar = -1, hitStaff = -1;
    if (ToPage(event.GetPosition(), x, y, page)) {
        for (int system = 0; system < int(systems_.size()) && hitBar < 0; ++system) {
            if (systems_[system].page != page) continue;
            for (int staff = 0; staff < StaffCount(); ++staff) {
                const double top = StaffTop(system, staff);
                if (y < top - 16 || y > top + kStaffH + 16) continue;
                hitBar = BarAtX(system, x);
                if (hitBar >= 0) hitStaff = staff;
                break;
            }
        }
    }
    SetSelection(hitBar, hitStaff);
    if (onSelect) onSelect(hitBar, hitStaff);
}

void ScoreCanvas::OnMotion(wxMouseEvent& event) {
    if (!noteInput_) return;
    Target t;
    if (!HitTest(event.GetPosition(), t)) t = Target{};
    if (t.bar != hover_.bar || t.staff != hover_.staff || t.step != hover_.step ||
        t.beat != hover_.beat) {
        hover_ = t;
        Refresh();
    }
}

bool ScoreCanvas::ToPage(const wxPoint& client, double& x, double& y, int& page) const {
    const wxPoint logical = CalcUnscrolledPosition(client);
    x = logical.x / zoom_ - PageX();
    const double canvasY = logical.y / zoom_;
    page = std::clamp(int((canvasY - kMargin) / (pageH_ + kMargin)), 0, pageCount_ - 1);
    y = canvasY - PageTop(page);
    return y >= 0 && y <= pageH_;
}

bool ScoreCanvas::HitTest(const wxPoint& client, Target& out) const {
    double x, y;
    int page;
    if (!ToPage(client, x, y, page)) return false;

    // Take the nearest staff, since close staves can have overlapping reach.
    double best = kPitchReach;
    bool found = false;
    for (int system = 0; system < int(systems_.size()); ++system) {
        if (systems_[system].page != page) continue;
        const int bar = BarAtX(system, x);
        if (bar < 0) continue;
        for (int staff = 0; staff < StaffCount(); ++staff) {
            const double top = StaffTop(system, staff);
            const double distance = std::abs(y - (top + kStaffH / 2));
            if (distance >= best) continue;
            best = distance;
            found = true;
            out.bar = bar;
            out.staff = staff;
            out.step = std::clamp(int(std::lround((top + kStaffH / 2 - y) / (kLine / 2))), -kMaxStep, kMaxStep);
            // Spacing isn't proportional to time, so snap by distance on screen.
            const double grid = SnapGrid(inputBeats_);
            double nearest = 1e9;
            out.beat = 0;
            for (double beat = 0; beat < BarBeats() - kEps; beat += grid) {
                const double gap = std::abs(BeatX(bar, beat) - x);
                if (gap < nearest) {
                    nearest = gap;
                    out.beat = beat;
                }
            }
        }
    }
    return found;
}

double ScoreCanvas::WrittenLength(double beat) const {
    return LargestValueWithin(std::min(inputBeats_, BarBeats() - beat));
}

bool ScoreCanvas::CanEdit(int staff, int bar) {
    if (IsProposalBar(staff, bar)) {
        if (onInputStatus) onInputStatus("Hide or apply the Director's preview to edit these bars");
        return false;
    }
    return true;
}

void ScoreCanvas::PushUndo() {
    undo_.push_back(score_);
    redo_.clear();
}

void ScoreCanvas::Changed() {
    ComputeLayout();
    Refresh();
    if (onChanged) onChanged();
}

// Replaces [start, start + length) with the note, or with silence when note is null.
// Notes it overlaps are shortened or removed, and rests are rebuilt around the result.
void ScoreCanvas::WriteRange(int staff, int bar, double start, double length, const Event* note) {
    std::vector<std::pair<double, Event>> notes;
    double pos = 0;
    for (const Event& ev : score_[staff][bar]) {
        const double end = pos + ev.beats;
        if (!ev.rest) {
            if (end <= start + kEps || pos >= start + length - kEps) {
                notes.emplace_back(pos, ev);
            } else if (pos < start - kEps) {
                Event cut = ev;
                cut.beats = LargestValueWithin(start - pos);
                notes.emplace_back(pos, std::move(cut));
            }
        }
        pos = end;
    }
    if (note) notes.emplace_back(start, *note);
    std::sort(notes.begin(), notes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    if (notes.empty()) {
        score_[staff][bar] = FullBarRest(BarBeats());
        return;
    }

    Bar rebuilt;
    double cursor = 0;
    const auto restsUpTo = [&](double to) {
        while (cursor < to - kEps) {
            for (double value : {4.0, 2.0, 1.0, 0.5, 0.25}) {
                if (value <= to - cursor + kEps && std::fmod(cursor, value) < kEps) {
                    Event rest;
                    rest.rest = true;
                    rest.beats = value;
                    rebuilt.push_back(rest);
                    cursor += value;
                    break;
                }
            }
        }
    };
    for (auto& entry : notes) {
        restsUpTo(entry.first);
        entry.second.dotted = IsDotted(entry.second.beats);
        cursor = entry.first + entry.second.beats;
        rebuilt.push_back(std::move(entry.second));
    }
    restsUpTo(BarBeats());
    score_[staff][bar] = std::move(rebuilt);
}

void ScoreCanvas::ClickNote(const Target& t) {
    if (!CanEdit(t.staff, t.bar)) return;
    const double length = WrittenLength(t.beat);
    PushUndo();

    if (inputRest_) {
        WriteRange(t.staff, t.bar, t.beat, length, nullptr);
    } else {
        // Clicking an existing note removes it; clicking above or below one that
        // has the same value builds a chord.
        double pos = 0;
        Event* existing = nullptr;
        for (Event& ev : score_[t.staff][t.bar]) {
            if (std::abs(pos - t.beat) < kEps && !ev.rest) existing = &ev;
            pos += ev.beats;
        }

        const auto found = existing
                               ? std::find(existing->steps.begin(), existing->steps.end(), t.step)
                               : std::vector<int>::iterator{};
        if (existing && found != existing->steps.end()) {
            if (existing->steps.size() == 1) {
                WriteRange(t.staff, t.bar, t.beat, existing->beats, nullptr);
            } else {
                existing->accidentals.erase(existing->accidentals.begin() +
                                            (found - existing->steps.begin()));
                existing->steps.erase(found);
            }
        } else if (existing && std::abs(existing->beats - length) < kEps) {
            existing->steps.push_back(t.step);
            existing->accidentals.push_back(inputAccidental_);
            PreviewNote(t.staff, t.bar, t.beat, t.step);
        } else {
            Event note;
            note.beats = length;
            note.steps = {t.step};
            note.accidentals = {inputAccidental_};
            WriteRange(t.staff, t.bar, t.beat, length, &note);
            PreviewNote(t.staff, t.bar, t.beat, t.step);
        }
    }

    if (inputAccidental_ && onAccidentalUsed) onAccidentalUsed();
    caret_ = t;
    lastEntered_ = Target{};
    AdvanceCaret(length);
    Changed();
}

void ScoreCanvas::WriteAtCaret(const Event* note, int step) {
    if (!CanEdit(caret_.staff, caret_.bar)) return;
    const double length = WrittenLength(caret_.beat);
    PushUndo();
    Event written;
    if (note) {
        written = *note;
        written.beats = length;
    }
    WriteRange(caret_.staff, caret_.bar, caret_.beat, length, note ? &written : nullptr);
    if (note) PreviewNote(caret_.staff, caret_.bar, caret_.beat, step);
    lastEntered_ = note ? caret_ : Target{};
    if (note) lastEntered_.step = step;
    AdvanceCaret(length);
    Changed();
}

void ScoreCanvas::AdvanceCaret(double beats) {
    double next = caret_.beat + beats;
    if (next >= BarBeats() - kEps) {
        if (caret_.bar + 1 >= BarCount()) {
            if (onInputStatus) onInputStatus("End of the score");
            Refresh();
            return;
        }
        ++caret_.bar;
        next = 0;
    }
    caret_.beat = next;
    PublishCaret();
    EnsureCaretVisible();
}

void ScoreCanvas::PublishCaret() {
    if (onInputStatus)
        onInputStatus(wxString::Format("Note input: bar %d, %s, beat %g", caret_.bar + 1,
                                       StaffName(caret_.staff), caret_.beat + 1));
}

void ScoreCanvas::EnsureCaretVisible() {
    const int system = SystemOfBar(caret_.bar);
    const int y = int((StaffTop(system, caret_.staff) + PageTop(systems_[system].page)) * zoom_);
    const int viewTop = CalcUnscrolledPosition(wxPoint(0, 0)).y;
    const int height = GetClientSize().y;
    if (y < viewTop + 70 || y > viewTop + height - 90) {
        int ppuX, ppuY;
        GetScrollPixelsPerUnit(&ppuX, &ppuY);
        if (ppuY > 0) Scroll(-1, std::max(0, y - height / 2) / ppuY);
    }
}

void ScoreCanvas::SetNoteInput(bool on) {
    if (on == noteInput_) return;
    noteInput_ = on;
    hover_ = Target{};
    lastEntered_ = Target{};
    SetCursor(on ? wxCURSOR_CROSS : wxCURSOR_ARROW);
    if (on) {
        caret_ = Target{};
        caret_.bar = selectedBar_ >= 0 ? selectedBar_ : 0;
        caret_.staff = selectedStaff_ >= 0 ? selectedStaff_ : 0;
        caret_.beat = 0;
        caret_.step = -2;
        SetFocus();
        PublishCaret();
        EnsureCaretVisible();
    } else if (onInputStatus) {
        onInputStatus("Ready");
    }
    Refresh();
}

void ScoreCanvas::SetInputValue(double beats, bool rest, char accidental) {
    inputBeats_ = beats;
    inputRest_ = rest;
    inputAccidental_ = accidental;
    Refresh();
}

void ScoreCanvas::TypePitch(char letter) {
    // C=0 ... B=6, relative to the note on each clef's middle line.
    const int index = int(std::string("CDEFGAB").find(letter));
    const int middle = MiddleLine(project_.staves[caret_.staff].clef) % 7;
    const int wanted = ((index - middle) % 7 + 7) % 7;

    int step = caret_.step;
    for (int d = 0; d <= 6; ++d) {
        if (((caret_.step + d) % 7 + 7) % 7 == wanted) {
            step = caret_.step + d;
            break;
        }
        if (((caret_.step - d) % 7 + 7) % 7 == wanted) {
            step = caret_.step - d;
            break;
        }
    }
    step = std::clamp(step, -kMaxStep, kMaxStep);

    Event note;
    note.steps = {step};
    note.accidentals = {inputAccidental_};
    caret_.step = step;
    WriteAtCaret(&note, step);
    if (inputAccidental_ && onAccidentalUsed) onAccidentalUsed();
}

void ScoreCanvas::TypeRest() { WriteAtCaret(nullptr, 0); }

void ScoreCanvas::MoveCaret(int direction) {
    lastEntered_ = Target{};
    int bar = caret_.bar;
    double beat = caret_.beat;

    double pos = 0, previous = -1, next = -1;
    for (const Event& ev : score_[caret_.staff][bar]) {
        if (pos < beat - kEps) previous = pos;
        if (pos > beat + kEps && next < 0) next = pos;
        pos += ev.beats;
    }

    if (direction > 0) {
        if (next >= 0) {
            beat = next;
        } else if (bar + 1 < BarCount()) {
            ++bar;
            beat = 0;
        }
    } else if (previous >= 0) {
        beat = previous;
    } else if (bar > 0) {
        --bar;
        double start = 0, at = 0;
        for (const Event& ev : score_[caret_.staff][bar]) {
            start = at;
            at += ev.beats;
        }
        beat = start;
    }
    caret_.bar = bar;
    caret_.beat = beat;
    PublishCaret();
    EnsureCaretVisible();
    Refresh();
}

void ScoreCanvas::MoveStaff(int direction) {
    lastEntered_ = Target{};
    caret_.staff = std::clamp(caret_.staff + direction, 0, StaffCount() - 1);
    PublishCaret();
    EnsureCaretVisible();
    Refresh();
}

void ScoreCanvas::ChangePitch(int steps) {
    if (lastEntered_.bar < 0) {
        caret_.step = std::clamp(caret_.step + steps, -kMaxStep, kMaxStep);
        Refresh();
        return;
    }
    double pos = 0;
    for (Event& ev : score_[lastEntered_.staff][lastEntered_.bar]) {
        if (std::abs(pos - lastEntered_.beat) < kEps && !ev.rest && ev.steps.size() == 1) {
            const int moved = std::clamp(ev.steps[0] + steps, -kMaxStep, kMaxStep);
            PushUndo();
            ev.steps[0] = moved;
            PreviewNote(lastEntered_.staff, lastEntered_.bar, lastEntered_.beat, moved);
            lastEntered_.step = moved;
            caret_.step = moved;
            Changed();
            return;
        }
        pos += ev.beats;
    }
}

void ScoreCanvas::DeleteBack() {
    if (caret_.bar == 0 && caret_.beat < kEps) return;
    MoveCaret(-1);
    DeleteAtCaret();
}

void ScoreCanvas::DeleteAtCaret() {
    if (!CanEdit(caret_.staff, caret_.bar)) return;
    double pos = 0;
    for (const Event& ev : score_[caret_.staff][caret_.bar]) {
        if (std::abs(pos - caret_.beat) < kEps) {
            if (ev.rest) return;
            const double length = ev.beats;
            PushUndo();
            WriteRange(caret_.staff, caret_.bar, pos, length, nullptr);
            lastEntered_ = Target{};
            Changed();
            return;
        }
        pos += ev.beats;
    }
}

void ScoreCanvas::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(std::move(score_));
    score_ = std::move(undo_.back());
    undo_.pop_back();
    lastEntered_ = Target{};
    Changed();
}

void ScoreCanvas::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(std::move(score_));
    score_ = std::move(redo_.back());
    redo_.pop_back();
    lastEntered_ = Target{};
    Changed();
}

std::vector<PlayNote> ScoreCanvas::BuildPlayback() const {
    std::vector<PlayNote> notes;
    const int fifths = project_.settings.fifths;
    for (int staff = 0; staff < StaffCount(); ++staff) {
        const StaffDef& def = project_.staves[staff];
        for (int bar = 0; bar < BarCount(); ++bar) {
            // An accidental lasts until the end of its bar.
            std::map<int, char> alterations;
            double pos = bar * BarBeats();
            for (const Event& ev : EventsFor(staff, bar)) {
                if (!ev.rest) {
                    for (size_t n = 0; n < ev.steps.size(); ++n) {
                        if (ev.accidentals[n]) alterations[ev.steps[n]] = ev.accidentals[n];
                        const auto it = alterations.find(ev.steps[n]);
                        const char accidental = it == alterations.end() ? char(0) : it->second;
                        notes.push_back({def.track, pos, ev.beats, StepToMidi(def.clef, fifths, ev.steps[n], accidental)});
                    }
                }
                pos += ev.beats;
            }
        }
    }
    return notes;
}

void ScoreCanvas::PreviewNote(int staff, int bar, double beat, int step) {
    if (!onNotePreview) return;
    char accidental = 0;
    double pos = 0;
    for (const Event& ev : score_[staff][bar]) {
        if (pos > beat + kEps) break;
        for (size_t n = 0; n < ev.steps.size(); ++n)
            if (ev.steps[n] == step && ev.accidentals[n]) accidental = ev.accidentals[n];
        pos += ev.beats;
    }
    const StaffDef& def = project_.staves[staff];
    onNotePreview(def.track, StepToMidi(def.clef, project_.settings.fifths, step, accidental));
}

double ScoreCanvas::FilledFraction(int staff, int bar) const {
    double beats = 0;
    for (const Event& ev : score_[staff][bar])
        if (!ev.rest) beats += ev.beats;
    return beats / BarBeats();
}

void ScoreCanvas::DrawGhost(wxGraphicsContext& gc, double x, double top, int step, double beats) {
    const wxColour ink = WithAlpha(kSelect, 170);
    if (inputRest_) {
        DrawRest(gc, x, top, beats, ink);
        return;
    }
    DrawHead(gc, x, StepY(top, step), beats, ink);
    for (int ledger = -6; ledger >= step; ledger -= 2)
        Line(gc, x - 9, StepY(top, ledger), x + 9, StepY(top, ledger), ink, 1);
    for (int ledger = 6; ledger <= step; ledger += 2)
        Line(gc, x - 9, StepY(top, ledger), x + 9, StepY(top, ledger), ink, 1);
    if (beats < 4) {
        const bool up = step < 0;
        const double y = StepY(top, step);
        const double stemX = x + (up ? 5 : -5);
        Line(gc, stemX, y, stemX, y + (up ? -27 : 27), ink, 1.2);
    }
    if (inputAccidental_) DrawAccidental(gc, x - 15, StepY(top, step), inputAccidental_, ink);
}

void ScoreCanvas::DrawInputBanner(wxGraphicsContext& gc) {
    const wxString text =
        "A-G or click to write   |   1-5 value   |   . dot   |   R rest   |   "
        "Backspace delete   |   N / Esc to finish";
    const wxFont bold = UiFont(13, true), body = UiFont(13);
    const double labelW = TextWidth(gc, "NOTE INPUT", bold) + 20;
    const double textW = TextWidth(gc, text, body) + 20;
    const double x = std::max(8.0, (GetClientSize().x - labelW - textW) / 2);
    FillRound(gc, wxRect2DDouble(x, 8, labelW + textW, 28), 6, kPanelAlt);
    StrokeRound(gc, wxRect2DDouble(x, 8, labelW + textW, 28), 6, kAccent, 1.2);
    const wxRect2DDouble label(x, 8, labelW, 28);
    FillRound(gc, label, 6, kAccent);
    DrawCentered(gc, "NOTE INPUT", label, bold, kAccentInk);
    DrawCentered(gc, text, wxRect2DDouble(x + labelW, 8, textW, 28), body, kText);
}

void ScoreCanvas::OnWheel(wxMouseEvent& event) {
    if (event.ControlDown()) {
        SetZoom(zoom_ + (event.GetWheelRotation() > 0 ? 0.1 : -0.1));
    } else {
        event.Skip();
    }
}
