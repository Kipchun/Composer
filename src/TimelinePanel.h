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
    void SetSelection(const BarRange& range);
    void SetPlayhead(double beats, bool visible);

    std::function<void(const BarRange&)> onSelect;

private:
    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnMotion(wxMouseEvent&);
    bool HitCell(const wxPoint& client, int& bar, int& staff) const;
    double BarWidth() const;
    double RowY(int row) const;

    const ScoreCanvas* score_ = nullptr;
    BarRange selection_;
    int anchorBar_ = -1, anchorStaff_ = -1;
    bool dragging_ = false;
    double playhead_ = 0;
    bool playheadVisible_ = false;
};
