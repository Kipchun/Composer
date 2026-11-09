#include "ScoreCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

using namespace ui;

namespace {

constexpr double kPageW = 1040, kPageH = 1290, kMargin = 24;
constexpr double kSysX = 150, kSysW = 830;
constexpr double kLine = 8, kStaffH = 32;  // staff line gap and height
constexpr double kFirstTop = 215, kSystemGap = 96;
constexpr int kStaves = 6, kBarsPerSystem = 4;
constexpr int kPianoRH = 4, kPianoLH = 5, kCello = 3, kViolin = 2;
constexpr int kProposalFirst = 4, kProposalLast = 5;

const char* const kNames[kStaves] = {"Flute", "Oboe", "Violin I", "Cello", "Piano", ""};

// Tokens are duration:step[,step...][accidental]. Durations are w h q e, with a
// trailing '.' for dotted; step "r" is a rest. Step 0 is the staff's middle line.
const char* const kScore[kStaves][ScoreCanvas::kBars] = {
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

const char* const kProposedBars[] = {"e:5 e:4 e:3 e:2 q:3 q:5", "q.:4 e:3 h:2"};

struct Marking {
    int bar, staff;
    double beat;
    const char* text;
};
const Marking kMarkings[] = {
    {0, kViolin, 0, "mp"}, {0, kCello, 0, "p"},   {0, kPianoLH, 0, "p"}, {1, 0, 3, "p"},
    {4, 0, 0, "mf"},       {4, kViolin, 0, "mf"}, {4, kCello, 0, "mp"},  {7, kViolin, 0, "pp"},
};

bool IsBass(int staff) { return staff == kCello || staff == kPianoLH; }

double StepY(double top, int step) { return top + kStaffH / 2 - step * kLine / 2; }

// Drawn as a shape so that it stays crisp and ink-coloured at any zoom.
void DrawFlat(wxGraphicsContext& gc, double x, double y) {
    Line(gc, x, y - 14, x, y + 5, kInk, 1.1);
    wxGraphicsPath bowl = gc.CreatePath();
    bowl.MoveToPoint(x, y + 5);
    bowl.AddCurveToPoint(x + 9, y - 1, x + 7, y - 6, x, y - 2);
    bowl.AddCurveToPoint(x + 4.8, y - 4, x + 5.5, y - 1, x, y + 3);
    bowl.CloseSubpath();
    NoPen(gc);
    gc.SetBrush(wxBrush(kInk));
    gc.FillPath(bowl);
}

}  // namespace

ScoreCanvas::ScoreCanvas(wxWindow* parent)
    : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL | wxHSCROLL | wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(16, 16);

    auto parse = [](const char* text) {
        std::vector<Event> events;
        std::istringstream in(text);
        std::string token;
        while (in >> token) {
            Event ev;
            const size_t colon = token.find(':');
            std::string dur = token.substr(0, colon);
            std::string body = token.substr(colon + 1);
            ev.dotted = dur.size() > 1 && dur[1] == '.';
            ev.beats = dur[0] == 'w' ? 4 : dur[0] == 'h' ? 2 : dur[0] == 'q' ? 1 : 0.5;
            if (ev.dotted) ev.beats *= 1.5;
            if (body == "r") {
                ev.rest = true;
            } else {
                const char tail = body.back();
                if (tail == '#' || tail == 'b' || tail == 'n') {
                    ev.accidental = tail;
                    body.pop_back();
                }
                std::istringstream steps(body);
                std::string step;
                while (std::getline(steps, step, ','))
                    ev.steps.push_back(std::atoi(step.c_str()));
            }
            events.push_back(std::move(ev));
        }
        return events;
    };

    score_.resize(kStaves);
    for (int s = 0; s < kStaves; ++s)
        for (int b = 0; b < kBars; ++b) score_[s].push_back(parse(kScore[s][b]));
    for (const char* text : kProposedBars) proposal_.push_back(parse(text));

    UpdateVirtualSize();

    Bind(wxEVT_PAINT, &ScoreCanvas::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &ScoreCanvas::OnLeftDown, this);
    Bind(wxEVT_MOUSEWHEEL, &ScoreCanvas::OnWheel, this);
}

wxString ScoreCanvas::StaffName(int staff) {
    if (staff == kPianoRH) return "Piano (RH)";
    if (staff == kPianoLH) return "Piano (LH)";
    return kNames[staff];
}

void ScoreCanvas::UpdateVirtualSize() {
    SetVirtualSize(int((kPageW + kMargin * 2) * zoom_), int((kPageH + kMargin * 2) * zoom_));
}

void ScoreCanvas::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.5, 2.0);
    UpdateVirtualSize();
    Refresh();
    if (onZoom) onZoom(zoom_);
}

void ScoreCanvas::SetProposal(ProposalState state) {
    proposalState_ = state;
    Refresh();
}

void ScoreCanvas::SetPlayhead(double beats, bool visible) {
    playhead_ = beats;
    playheadVisible_ = visible;
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

double ScoreCanvas::StaffTop(int system, int staff) const {
    // Piano staves sit closer together than the separate instruments.
    double y = kFirstTop;
    const double systemH = 5 * (kStaffH + 44) + kStaffH - 14;
    y += system * (systemH + kSystemGap);
    y += staff * (kStaffH + 44);
    if (staff == kPianoLH) y -= 14;
    return y;
}

double ScoreCanvas::BarWidth(int system) const {
    const double prefix = system == 0 ? 112 : 88;
    return (kSysW - prefix) / kBarsPerSystem;
}

double ScoreCanvas::BarX(int system, int barInSystem) const {
    const double prefix = system == 0 ? 112 : 88;
    return kSysX + prefix + barInSystem * BarWidth(system);
}

double ScoreCanvas::BeatX(double barX, double barW, double beat) const {
    return barX + 18 + beat / kBeatsPerBar * (barW - 30);
}

const std::vector<ScoreCanvas::Event>& ScoreCanvas::EventsFor(int staff, int bar) const {
    if (staff == kViolin && proposalState_ != ProposalState::None && bar >= kProposalFirst &&
        bar <= kProposalLast)
        return proposal_[bar - kProposalFirst];
    return score_[staff][bar];
}

void ScoreCanvas::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kBg));
    dc.Clear();

    Gc gc = MakeGc(dc);
    const wxPoint origin = CalcScrolledPosition(wxPoint(0, 0));
    gc->Translate(origin.x, origin.y);
    gc->Scale(zoom_, zoom_);
    gc->Translate(PageX(), kMargin);
    DrawPage(*gc);
}

void ScoreCanvas::DrawPage(wxGraphicsContext& gc) {
    FillRound(gc, wxRect2DDouble(4, 6, kPageW, kPageH), 2, wxColour(0, 0, 0, 90));
    FillRound(gc, wxRect2DDouble(0, 0, kPageW, kPageH), 2, kPaper);

    const wxFont titleFont = SerifFont(38);
    gc.SetFont(titleFont, kInk);
    double w, h, d, l;
    const wxString title = "Nocturne in Blue";
    gc.GetTextExtent(title, &w, &h, &d, &l);
    gc.DrawText(title, (kPageW - w) / 2, 62);

    const wxFont sub = SerifFont(15, false, true);
    const wxString subtitle = "for chamber ensemble";
    gc.SetFont(sub, kInk);
    gc.GetTextExtent(subtitle, &w, &h, &d, &l);
    gc.DrawText(subtitle, (kPageW - w) / 2, 112);

    const wxString composer = "Concept Score";
    const wxFont composerFont = SerifFont(14);
    gc.SetFont(composerFont, kInk);
    gc.GetTextExtent(composer, &w, &h, &d, &l);
    gc.DrawText(composer, kSysX + kSysW - w, 148);

    DrawTextAt(gc, "Andante", kSysX + 6, 150, SerifFont(15, true), kInk);
    DrawTextAt(gc, Glyph(0x2669) + " = 96", kSysX + 76, 149, MusicFont(15), kInk);

    for (int system = 0; system < 2; ++system) DrawSystem(gc, system);

    gc.SetFont(SerifFont(12), kMuted);
    gc.GetTextExtent("1", &w, &h, &d, &l);
    gc.DrawText("1", (kPageW - w) / 2, kPageH - 42);
}

void ScoreCanvas::DrawPrefix(wxGraphicsContext& gc, int system, int staff) {
    const double top = StaffTop(system, staff);
    const bool bass = IsBass(staff);

    // Font metrics place these two clefs on the correct staff lines.
    if (bass)
        DrawBaseline(gc, Glyph(0x1D122), kSysX + 7, top + 24, MusicFont(34), kInk);
    else
        DrawBaseline(gc, Glyph(0x1D11E), kSysX + 7, top + 24, MusicFont(40), kInk);

    // E-flat major: B, E, A flats.
    const int treble[3] = {0, 3, -1};
    const int low[3] = {-2, 1, -3};
    double x = kSysX + 40;
    for (int i = 0; i < 3; ++i) {
        const int step = bass ? low[i] : treble[i];
        DrawFlat(gc, x + 2, StepY(top, step));
        x += 9;
    }

    if (system == 0) {
        const wxFont digit = SerifFont(20, true);
        DrawCentered(gc, "4", wxRect2DDouble(kSysX + 76, top - 1, 18, 16), digit, kInk);
        DrawCentered(gc, "4", wxRect2DDouble(kSysX + 76, top + 15, 18, 16), digit, kInk);
    }
}

void ScoreCanvas::DrawSystem(wxGraphicsContext& gc, int system) {
    const double firstTop = StaffTop(system, 0);
    const double lastBottom = StaffTop(system, kPianoLH) + kStaffH;
    const double barW = BarWidth(system);
    const int firstBar = system * kBarsPerSystem;

    // Highlights go underneath the notation.
    if (selectedBar_ >= firstBar && selectedBar_ < firstBar + kBarsPerSystem) {
        const double x = BarX(system, selectedBar_ - firstBar);
        double y = firstTop - 10, height = lastBottom - firstTop + 20;
        if (selectedStaff_ >= 0) {
            y = StaffTop(system, selectedStaff_) - 10;
            height = kStaffH + 20;
        }
        FillRound(gc, wxRect2DDouble(x + 1, y, barW - 2, height), 3, WithAlpha(kSelect, 42));
    }
    if (system == 1 && proposalState_ == ProposalState::Preview) {
        const double top = StaffTop(system, kViolin);
        const wxRect2DDouble r(BarX(1, 0) + 1, top - 12, barW * 2 - 2, kStaffH + 24);
        FillRound(gc, r, 4, WithAlpha(kProposal, 38));
        StrokeRound(gc, r, 4, kProposal, 1.2);
        const wxRect2DDouble tag(r.m_x, r.m_y - 21, 150, 19);
        FillRound(gc, tag, 3, kProposal);
        DrawCentered(gc, "Director proposal", tag, UiFont(12, true), *wxWHITE);
    }

    // Staves, barlines, labels and music.
    for (int staff = 0; staff < kStaves; ++staff) {
        const double top = StaffTop(system, staff);
        for (int i = 0; i < 5; ++i)
            Line(gc, kSysX, top + i * kLine, kSysX + kSysW, top + i * kLine, kInk, 0.9);

        if (kNames[staff][0]) {
            const double y = staff == kPianoRH ? top + 30 : top + 6;
            gc.SetFont(SerifFont(14), kInk);
            double w, h, d, l;
            gc.GetTextExtent(kNames[staff], &w, &h, &d, &l);
            gc.DrawText(kNames[staff], kSysX - 26 - w, y);
        }

        DrawPrefix(gc, system, staff);

        const bool grandStaffJoin = staff == kPianoRH;
        for (int b = 0; b <= kBarsPerSystem; ++b) {
            double x = b == 0 ? BarX(system, 0) : BarX(system, 0) + b * barW;
            if (b == 0) continue;  // the prefix has no barline after it
            if (system == 1 && b == kBarsPerSystem) {
                Line(gc, x - 6, top, x - 6, top + kStaffH, kInk, 1);
                Line(gc, x - 1.5, top, x - 1.5, top + kStaffH, kInk, 3.5);
            } else {
                Line(gc, x, top, x, top + kStaffH + (grandStaffJoin ? 44 - 14 : 0), kInk, 1);
            }
        }

        for (int b = 0; b < kBarsPerSystem; ++b) {
            const int bar = firstBar + b;
            const bool proposed = staff == kViolin && proposalState_ == ProposalState::Preview &&
                                  bar >= kProposalFirst && bar <= kProposalLast;
            DrawEvents(gc, EventsFor(staff, bar), BarX(system, b), barW, top,
                       proposed ? kProposal : kInk);
        }
    }

    // Left system line and the piano brace.
    Line(gc, kSysX, firstTop, kSysX, lastBottom, kInk, 1.3);
    const double pianoTop = StaffTop(system, kPianoRH);
    const double mid = (pianoTop + lastBottom) / 2;
    wxGraphicsPath brace = gc.CreatePath();
    const double bx = kSysX - 4;
    brace.MoveToPoint(bx, pianoTop);
    brace.AddCurveToPoint(bx - 15, pianoTop + 8, bx + 1, mid - 10, bx - 11, mid);
    brace.AddCurveToPoint(bx + 1, mid + 10, bx - 15, lastBottom - 8, bx, lastBottom);
    brace.AddCurveToPoint(bx - 11, lastBottom - 8, bx + 5, mid + 10, bx - 7, mid);
    brace.AddCurveToPoint(bx + 5, mid - 10, bx - 11, pianoTop + 8, bx, pianoTop);
    NoPen(gc);
    gc.SetBrush(wxBrush(kInk));
    gc.FillPath(brace);

    // Bar numbers and rehearsal mark.
    for (int b = 0; b < kBarsPerSystem; ++b)
        DrawTextAt(gc, wxString::Format("%d", firstBar + b + 1), BarX(system, b) + 3,
                   firstTop - 24, SerifFont(12, false, true), kMuted);
    const wxRect2DDouble mark(kSysX + 1, firstTop - 34, 25, 24);
    StrokeRound(gc, mark, 1, kInk, 1.4);
    DrawCentered(gc, system == 0 ? "A" : "B", mark, SerifFont(16, true), kInk);

    for (const Marking& m : kMarkings) {
        if (m.bar < firstBar || m.bar >= firstBar + kBarsPerSystem) continue;
        const double x = BeatX(BarX(system, m.bar - firstBar), barW, m.beat);
        DrawTextAt(gc, m.text, x - 5, StaffTop(system, m.staff) + kStaffH + 16,
                   SerifFont(16, true, true), kInk);
    }

    // A phrase slur over the opening violin bar.
    if (system == 0) {
        const double top = StaffTop(0, kViolin);
        const double x1 = BeatX(BarX(0, 0), barW, 0);
        const double x2 = BeatX(BarX(0, 0), barW, 3) + 4;
        wxGraphicsPath slur = gc.CreatePath();
        slur.MoveToPoint(x1, top - 2);
        slur.AddCurveToPoint(x1 + 26, top - 25, x2 - 26, top - 25, x2, top - 9);
        slur.AddCurveToPoint(x2 - 26, top - 22, x1 + 26, top - 22, x1, top - 2);
        gc.FillPath(slur);
    }

    if (playheadVisible_) {
        const int bar = std::min(int(playhead_ / kBeatsPerBar), kBars - 1);
        if (bar >= firstBar && bar < firstBar + kBarsPerSystem) {
            const double beat = playhead_ - bar * kBeatsPerBar;
            const double x = BeatX(BarX(system, bar - firstBar), barW, beat) - 2;
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
        gc.DrawRectangle(x + 16, top + kLine, 13, 4.5);
    } else if (beats >= 2) {
        gc.DrawRectangle(x - 6, top + 2 * kLine - 4.5, 13, 4.5);
    } else if (beats >= 1) {
        DrawBaseline(gc, Glyph(0x1D13D), x - 5, top + 24, MusicFont(30), ink);
    } else {
        DrawBaseline(gc, Glyph(0x1D13E), x - 5, top + 22, MusicFont(30), ink);
    }
}

void ScoreCanvas::DrawEvents(wxGraphicsContext& gc, const std::vector<Event>& events, double barX,
                             double barW, double top, const wxColour& ink) {
    constexpr double kHeadX = 5.0, kStem = 27;

    struct Placed {
        double x = 0, beat = 0;
        bool up = false;
        double anchorY = 0, tipY = 0;  // stem ends
        double stemX = 0;
    };
    std::vector<Placed> placed(events.size());

    double beat = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const Event& ev = events[i];
        Placed& p = placed[i];
        p.beat = beat;
        p.x = BeatX(barX, barW, beat);
        beat += ev.beats;
        if (ev.rest) {
            DrawRest(gc, p.x, top, ev.beats, ink);
            continue;
        }

        double sum = 0;
        int lo = ev.steps[0], hi = ev.steps[0];
        for (int s : ev.steps) {
            sum += s;
            lo = std::min(lo, s);
            hi = std::max(hi, s);
            DrawHead(gc, p.x, StepY(top, s), ev.beats, ink);
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
        p.anchorY = StepY(top, p.up ? lo : hi);
        const double stemEnd = StepY(top, p.up ? hi : lo);
        p.tipY = stemEnd + (p.up ? -kStem : kStem);
    }

    // Stems, beams and flags.
    for (size_t i = 0; i < events.size(); ++i) {
        const Event& ev = events[i];
        if (ev.rest || ev.beats >= 4) continue;
        Placed& p = placed[i];

        const bool beamable = ev.beats == 0.5 && i + 1 < events.size() &&
                              events[i + 1].beats == 0.5 && !events[i + 1].rest &&
                              std::floor(p.beat) == p.beat;
        if (beamable) {
            Placed& q = placed[i + 1];
            const bool up = (StepY(top, ev.steps[0]) + StepY(top, events[i + 1].steps[0])) / 2 >
                            top + kStaffH / 2;
            for (Placed* n : {&p, &q}) {
                n->up = up;
                n->stemX = n->x + (up ? kHeadX : -kHeadX);
                n->tipY = n->anchorY + (up ? -kStem : kStem);
            }
            // Keep both stems reachable and limit the beam's slope.
            const double extreme = up ? std::min(p.tipY, q.tipY) : std::max(p.tipY, q.tipY);
            const double slope = std::clamp(q.tipY - p.tipY, -6.0, 6.0);
            p.tipY = extreme + (up ? (slope > 0 ? 0 : -slope) : (slope < 0 ? 0 : -slope));
            q.tipY = p.tipY + slope;

            Line(gc, p.stemX, p.anchorY, p.stemX, p.tipY, ink, 1.2);
            Line(gc, q.stemX, q.anchorY, q.stemX, q.tipY, ink, 1.2);
            const double thick = up ? 4.5 : -4.5;
            wxGraphicsPath beam = gc.CreatePath();
            beam.MoveToPoint(p.stemX - 0.6, p.tipY);
            beam.AddLineToPoint(q.stemX + 0.6, q.tipY);
            beam.AddLineToPoint(q.stemX + 0.6, q.tipY + thick);
            beam.AddLineToPoint(p.stemX - 0.6, p.tipY + thick);
            beam.CloseSubpath();
            NoPen(gc);
            gc.SetBrush(wxBrush(ink));
            gc.FillPath(beam);
            ++i;
            continue;
        }

        Line(gc, p.stemX, p.anchorY, p.stemX, p.tipY, ink, 1.2);
        if (ev.beats == 0.5) {
            const double dir = p.up ? 1 : -1;
            wxGraphicsPath flag = gc.CreatePath();
            flag.MoveToPoint(p.stemX, p.tipY);
            flag.AddCurveToPoint(p.stemX + 2, p.tipY + dir * 10, p.stemX + 14, p.tipY + dir * 12,
                                 p.stemX + 8, p.tipY + dir * 24);
            flag.AddCurveToPoint(p.stemX + 10, p.tipY + dir * 15, p.stemX + 3, p.tipY + dir * 14,
                                 p.stemX, p.tipY + dir * 9);
            flag.CloseSubpath();
            NoPen(gc);
            gc.SetBrush(wxBrush(ink));
            gc.FillPath(flag);
        }
    }
}

void ScoreCanvas::OnLeftDown(wxMouseEvent& event) {
    SetFocus();
    const wxPoint logical = CalcUnscrolledPosition(event.GetPosition());
    const double x = logical.x / zoom_ - PageX();
    const double y = logical.y / zoom_ - kMargin;

    int hitBar = -1, hitStaff = -1;
    for (int system = 0; system < 2 && hitBar < 0; ++system) {
        for (int staff = 0; staff < kStaves; ++staff) {
            const double top = StaffTop(system, staff);
            if (y < top - 16 || y > top + kStaffH + 16) continue;
            const double offset = x - BarX(system, 0);
            const int b = int(std::floor(offset / BarWidth(system)));
            if (offset >= 0 && b < kBarsPerSystem) {
                hitBar = system * kBarsPerSystem + b;
                hitStaff = staff;
            }
            break;
        }
    }
    SetSelection(hitBar, hitStaff);
    if (onSelect) onSelect(hitBar, hitStaff);
}

void ScoreCanvas::OnWheel(wxMouseEvent& event) {
    if (event.ControlDown()) {
        SetZoom(zoom_ + (event.GetWheelRotation() > 0 ? 0.1 : -0.1));
    } else {
        event.Skip();
    }
}
