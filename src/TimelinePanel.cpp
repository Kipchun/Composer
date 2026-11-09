#include "TimelinePanel.h"

#include <algorithm>

using namespace ui;

namespace {
constexpr double kLabelW = 136, kFormH = 24, kRulerH = 18, kRowH = 19;

const wxColour kTrackColors[] = {
    wxColour(0x6C, 0xA8, 0xE8), wxColour(0x8E, 0x8B, 0xE0), wxColour(0xE3, 0x8A, 0x6A),
    wxColour(0xC4, 0x72, 0x9F), wxColour(0x5F, 0xB4, 0xB0), wxColour(0x4C, 0x98, 0x94),
};
}  // namespace

TimelinePanel::TimelinePanel(wxWindow* parent)
    : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(0, int(kRowH));
    Bind(wxEVT_PAINT, &TimelinePanel::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_LEFT_DOWN, &TimelinePanel::OnLeftDown, this);
    Bind(wxEVT_MOTION, &TimelinePanel::OnMotion, this);
}

void TimelinePanel::SetScore(const ScoreCanvas* score) {
    score_ = score;
    ScoreChanged();
}

void TimelinePanel::ScoreChanged() {
    SetVirtualSize(-1, int(RowY(score_->StaffCount()) + 6));
    Refresh();
}

void TimelinePanel::SetSelection(const BarRange& range) {
    selection_ = range;
    Refresh();
}

void TimelinePanel::SetPlayhead(double beats, bool visible) {
    playhead_ = beats;
    playheadVisible_ = visible;
    Refresh();
}

double TimelinePanel::BarWidth() const {
    return (GetClientSize().x - kLabelW - 12) / double(std::max(1, score_->BarCount()));
}

double TimelinePanel::RowY(int row) const { return 6 + kFormH + kRulerH + row * kRowH; }

void TimelinePanel::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kPanel));
    dc.Clear();
    if (!score_) return;

    Gc gc = MakeGc(dc);
    const wxPoint origin = CalcScrolledPosition(wxPoint(0, 0));
    gc->Translate(origin.x, origin.y);

    const Project& project = score_->GetProject();
    const double barW = BarWidth();
    const int bars = score_->BarCount();
    const int rows = score_->StaffCount();
    const wxFont rowFont = UiFont(12);

    // Sections run from their bar to the start of the next one.
    for (size_t i = 0; i < project.sections.size(); ++i) {
        const Section& sec = project.sections[i];
        if (sec.bar >= bars) continue;
        const int end = i + 1 < project.sections.size() ? std::min(project.sections[i + 1].bar, bars) : bars;
        const wxRect2DDouble r(kLabelW + sec.bar * barW, 6, (end - sec.bar) * barW - 2, kFormH - 3);
        FillRound(*gc, r, 3, kPanelAlt);
        DrawTextAt(*gc, sec.label, r.m_x + 8, r.m_y + 2, UiFont(12, true), kText);
    }

    // Keep bar numbers legible when there are many bars.
    int numberEvery = 1;
    while (barW * numberEvery < 26) numberEvery *= 2;
    for (int bar = 0; bar < bars; bar += numberEvery)
        DrawTextAt(*gc, wxString::Format("%d", bar + 1), kLabelW + bar * barW + 3, 6 + kFormH,
                   UiFont(11), kMuted);

    const int colors = int(WXSIZEOF(kTrackColors));
    for (int row = 0; row < rows; ++row) {
        const double y = RowY(row);
        DrawTextAt(*gc, score_->StaffName(row), 12, y + 1, rowFont, kText);

        for (int i = 0; i < 2; ++i) {
            const wxRect2DDouble box(kLabelW - 40 + i * 18, y + 1, 16, kRowH - 4);
            FillRound(*gc, box, 2, kPanelAlt);
            DrawCentered(*gc, i == 0 ? "M" : "S", box, UiFont(10, true), kMuted);
        }

        const wxColour& color = kTrackColors[project.staves[row].track % colors];
        for (int bar = 0; bar < bars; ++bar) {
            const bool proposed = score_->IsProposalBar(row, bar);
            const wxRect2DDouble cell(kLabelW + bar * barW + 1, y + 1, std::max(1.0, barW - 2), kRowH - 3);
            FillRound(*gc, cell, 2, kBg);
            const double fill = score_->FilledFraction(row, bar);
            if (fill > 0) {
                wxRect2DDouble block = cell;
                block.m_width *= fill;
                FillRound(*gc, block, 2, proposed ? kProposalBright : color);
            }
            if (selection_.Contains(row, bar))
                StrokeRound(*gc, cell, 2, *wxWHITE, 1.4);
        }
    }

    if (playheadVisible_) {
        const int bar = project.BarAtBeat(playhead_);
        const double x = kLabelW + (bar + (playhead_ - project.BarStart(bar)) / project.BarBeats(bar)) * barW;
        Line(*gc, x, 6, x, RowY(rows), kAccent, 2);
    }
}

bool TimelinePanel::HitCell(const wxPoint& client, int& bar, int& staff) const {
    const wxPoint p = CalcUnscrolledPosition(client);
    if (p.x < kLabelW) return false;
    const int b = int((p.x - kLabelW) / BarWidth());
    if (b >= score_->BarCount()) return false;
    bar = b;
    staff = p.y >= RowY(0) && p.y < RowY(score_->StaffCount()) ? int((p.y - RowY(0)) / kRowH) : -1;
    return true;
}

void TimelinePanel::OnLeftDown(wxMouseEvent& e) {
    int bar = -1, staff = -1;
    BarRange range;
    dragging_ = HitCell(e.GetPosition(), bar, staff);
    if (dragging_) {
        if (!e.ShiftDown() || anchorBar_ < 0) {
            anchorBar_ = bar;
            anchorStaff_ = staff;
        }
        range = MakeBarRange(anchorBar_, anchorStaff_, bar, staff);
    } else {
        anchorBar_ = anchorStaff_ = -1;
    }
    SetSelection(range);
    if (onSelect) onSelect(range);
}

void TimelinePanel::OnMotion(wxMouseEvent& e) {
    if (!e.LeftIsDown()) {
        dragging_ = false;
        return;
    }
    int bar, staff;
    if (!dragging_ || !HitCell(e.GetPosition(), bar, staff)) return;
    const BarRange range = MakeBarRange(anchorBar_, anchorStaff_, bar, staff);
    if (range.firstBar == selection_.firstBar && range.lastBar == selection_.lastBar &&
        range.firstStaff == selection_.firstStaff && range.lastStaff == selection_.lastStaff)
        return;
    SetSelection(range);
    if (onSelect) onSelect(range);
}
