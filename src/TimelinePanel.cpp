#include "TimelinePanel.h"

using namespace ui;

namespace {
constexpr double kLabelW = 136, kFormH = 24, kRulerH = 18, kRowH = 19;
constexpr int kRows = 6;

const wxColour kTrackColors[kRows] = {
    wxColour(0x6C, 0xA8, 0xE8), wxColour(0x8E, 0x8B, 0xE0), wxColour(0xE3, 0x8A, 0x6A),
    wxColour(0xC4, 0x72, 0x9F), wxColour(0x5F, 0xB4, 0xB0), wxColour(0x4C, 0x98, 0x94),
};
}  // namespace

TimelinePanel::TimelinePanel(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &TimelinePanel::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_LEFT_DOWN, &TimelinePanel::OnLeftDown, this);
}

void TimelinePanel::SetSelection(int bar, int staff) {
    selectedBar_ = bar;
    selectedStaff_ = staff;
    Refresh();
}

void TimelinePanel::SetPlayhead(double beats, bool visible) {
    playhead_ = beats;
    playheadVisible_ = visible;
    Refresh();
}

void TimelinePanel::SetProposal(ProposalState state) {
    proposal_ = state;
    Refresh();
}

double TimelinePanel::BarWidth() const {
    return (GetClientSize().x - kLabelW - 12) / double(ScoreCanvas::kBars);
}

double TimelinePanel::RowY(int row) const { return 6 + kFormH + kRulerH + row * kRowH; }

void TimelinePanel::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kPanel));
    dc.Clear();
    Gc gc = MakeGc(dc);

    const double barW = BarWidth();
    const wxFont rowFont = UiFont(12);

    // Form sections.
    const wxRect2DDouble a(kLabelW, 6, barW * 4 - 2, kFormH - 3);
    const wxRect2DDouble b(kLabelW + barW * 4, 6, barW * 4 - 2, kFormH - 3);
    FillRound(*gc, a, 3, kPanelAlt);
    FillRound(*gc, b, 3, kPanelAlt);
    DrawTextAt(*gc, "A  Opening", a.m_x + 8, a.m_y + 2, UiFont(12, true), kText);
    DrawTextAt(*gc, "B  Second phrase", b.m_x + 8, b.m_y + 2, UiFont(12, true), kText);

    for (int bar = 0; bar < ScoreCanvas::kBars; ++bar)
        DrawTextAt(*gc, wxString::Format("%d", bar + 1), kLabelW + bar * barW + 3, 6 + kFormH,
                   UiFont(11), kMuted);

    for (int row = 0; row < kRows; ++row) {
        const double y = RowY(row);
        DrawTextAt(*gc, ScoreCanvas::StaffName(row), 12, y + 1, rowFont, kText);

        for (int i = 0; i < 2; ++i) {
            const wxRect2DDouble box(kLabelW - 40 + i * 18, y + 1, 16, kRowH - 4);
            FillRound(*gc, box, 2, kPanelAlt);
            DrawCentered(*gc, i == 0 ? "M" : "S", box, UiFont(10, true), kMuted);
        }

        for (int bar = 0; bar < ScoreCanvas::kBars; ++bar) {
            const bool proposed =
                proposal_ == ProposalState::Preview && row == 2 && (bar == 4 || bar == 5);
            const wxRect2DDouble cell(kLabelW + bar * barW + 1, y + 1, barW - 2, kRowH - 3);
            FillRound(*gc, cell, 2, kBg);
            const double fill = score_ ? score_->FilledFraction(row, bar) : 0;
            if (fill > 0) {
                wxRect2DDouble block = cell;
                block.m_width *= fill;
                FillRound(*gc, block, 2, proposed ? kProposalBright : kTrackColors[row]);
            }
            const bool selected =
                bar == selectedBar_ && (selectedStaff_ < 0 || selectedStaff_ == row);
            if (selected) StrokeRound(*gc, cell, 2, *wxWHITE, 1.4);
        }
    }

    if (playheadVisible_) {
        const double x = kLabelW + playhead_ / ScoreCanvas::kBeatsPerBar * barW;
        Line(*gc, x, 6, x, RowY(kRows), kAccent, 2);
    }
}

void TimelinePanel::OnLeftDown(wxMouseEvent& e) {
    const wxPoint p = e.GetPosition();
    int bar = -1, staff = -1;
    if (p.x >= kLabelW) {
        const int b = int((p.x - kLabelW) / BarWidth());
        if (b < ScoreCanvas::kBars) bar = b;
        if (p.y >= RowY(0) && p.y < RowY(kRows)) staff = int((p.y - RowY(0)) / kRowH);
    }
    SetSelection(bar, staff);
    if (onSelect) onSelect(bar, staff);
}
