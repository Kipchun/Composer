// Bottom overview: rehearsal sections and a block per bar for each staff.
#pragma once

#include <wx/scrolwin.h>

#include <functional>

#include "ScoreCanvas.h"
#include "Theme.h"

class TimelinePanel : public wxScrolledCanvas {
public:
    explicit TimelinePanel(wxWindow* parent);

    void SetScore(const ScoreCanvas* score);
    void ScoreChanged();  // the project's shape may have changed too
    void SetSelection(int bar, int staff);
    void SetPlayhead(double beats, bool visible);

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
};
