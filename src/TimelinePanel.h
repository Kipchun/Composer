// Bottom overview: form sections and a block per bar for each instrument.
#pragma once

#include <functional>

#include "ScoreCanvas.h"
#include "Theme.h"

class TimelinePanel : public wxPanel {
public:
    explicit TimelinePanel(wxWindow* parent);

    void SetScore(const ScoreCanvas* score) { score_ = score; }
    void SetSelection(int bar, int staff);
    void SetPlayhead(double beats, bool visible);
    void SetProposal(ProposalState state);

    std::function<void(int bar, int staff)> onSelect;

private:
    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    double BarWidth() const;
    double RowY(int row) const;

    const ScoreCanvas* score_ = nullptr;
    int selectedBar_ = -1, selectedStaff_ = -1;
    double playhead_ = 0;
    bool playheadVisible_ = false;
    ProposalState proposal_ = ProposalState::None;
};
