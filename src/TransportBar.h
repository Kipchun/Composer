// Top strip: playback buttons, position readouts, view switcher and zoom.
#pragma once

#include <functional>
#include <vector>

#include "Theme.h"

class TransportBar : public wxPanel {
public:
    enum Action { Rewind, Play, Stop, Record, Loop, Click, ZoomOut, ZoomIn, ViewScore, ViewParts, ViewRoll };

    explicit TransportBar(wxWindow* parent);

    void SetPlaying(bool playing);
    void SetPosition(int bar, int beat);
    void SetZoomPercent(int percent);
    bool IsLooping() const { return loop_; }

    std::function<void(Action)> onAction;

private:
    struct Button {
        Action action;
        wxRect2DDouble rect;
    };

    void Relayout();
    void OnPaint(wxPaintEvent&);
    void OnMotion(wxMouseEvent&);
    void OnLeftDown(wxMouseEvent&);
    void DrawIcon(wxGraphicsContext& gc, Action action, const wxRect2DDouble& r, const wxColour& c);
    void DrawReadout(wxGraphicsContext& gc, const wxRect2DDouble& r, const wxString& label,
                     const wxString& value);
    Button* FindButton(const wxPoint& p);

    std::vector<Button> buttons_;
    wxRect2DDouble position_, tempo_, key_, time_, zoomLabel_;
    bool playing_ = false, loop_ = false, click_ = true, recording_ = false;
    int bar_ = 1, beat_ = 1, zoom_ = 80, hover_ = -1;
    Action view_ = ViewScore;
};
