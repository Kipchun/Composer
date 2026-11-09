#include "TransportBar.h"

using namespace ui;

TransportBar::TransportBar(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &TransportBar::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Relayout(); Refresh(); e.Skip(); });
    Bind(wxEVT_MOTION, &TransportBar::OnMotion, this);
    Bind(wxEVT_LEFT_DOWN, &TransportBar::OnLeftDown, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover_ = -1; Refresh(); });
}

void TransportBar::SetPlaying(bool playing) {
    playing_ = playing;
    Refresh();
}

void TransportBar::SetPosition(int bar, int beat) {
    if (bar == bar_ && beat == beat_) return;
    bar_ = bar;
    beat_ = beat;
    Refresh();
}

void TransportBar::SetProjectInfo(int tempo, const wxString& key, const wxString& time) {
    tempoValue_ = tempo;
    keyLabel_ = key;
    timeLabel_ = time;
    Refresh();
}

void TransportBar::SetTempoValue(int bpm) {
    if (bpm == tempoValue_) return;
    tempoValue_ = bpm;
    Refresh();
}

void TransportBar::SetTimeLabel(const wxString& time) {
    if (time == timeLabel_) return;
    timeLabel_ = time;
    Refresh();
}

void TransportBar::SetZoomPercent(int percent) {
    zoom_ = percent;
    Refresh();
}

void TransportBar::Relayout() {
    constexpr double kH = 36;
    const wxSize size = GetClientSize();
    const double y = (size.y - kH) / 2;

    buttons_.clear();
    double x = 148;
    for (Action a : {Rewind, Play, Stop, Record, Loop, Click}) {
        buttons_.push_back({a, wxRect2DDouble(x, y, 38, kH)});
        x += 42;
    }
    x += 14;
    position_ = wxRect2DDouble(x, y, 102, kH);
    x += 108;
    tempo_ = wxRect2DDouble(x, y, 72, kH);
    x += 78;
    key_ = wxRect2DDouble(x, y, 102, kH);
    x += 108;
    time_ = wxRect2DDouble(x, y, 56, kH);

    // Right-aligned group.
    double right = size.x - 14;
    right -= 30;
    buttons_.push_back({ZoomIn, wxRect2DDouble(right, y, 30, kH)});
    right -= 54;
    zoomLabel_ = wxRect2DDouble(right, y, 54, kH);
    right -= 30;
    buttons_.push_back({ZoomOut, wxRect2DDouble(right, y, 30, kH)});
    right -= 20 + 84;
    buttons_.push_back({ViewRoll, wxRect2DDouble(right, y, 84, kH)});
    right -= 62;
    buttons_.push_back({ViewParts, wxRect2DDouble(right, y, 62, kH)});
    right -= 62;
    buttons_.push_back({ViewScore, wxRect2DDouble(right, y, 62, kH)});
}

void TransportBar::DrawReadout(wxGraphicsContext& gc, const wxRect2DDouble& r,
                               const wxString& label, const wxString& value) {
    FillRound(gc, r, 5, kBg);
    DrawTextAt(gc, label, r.m_x + 9, r.m_y + 3, UiFont(10, true), kMuted);
    DrawTextAt(gc, value, r.m_x + 9, r.m_y + 14, UiFont(15, true), kText);
}

void TransportBar::DrawIcon(wxGraphicsContext& gc, Action action, const wxRect2DDouble& r,
                            const wxColour& c) {
    const double cx = r.m_x + r.m_width / 2, cy = r.m_y + r.m_height / 2;
    wxGraphicsPath p = gc.CreatePath();
    const auto triangle = [&](double x, double dir) {
        p.MoveToPoint(x, cy - 7);
        p.AddLineToPoint(x + dir * 12, cy);
        p.AddLineToPoint(x, cy + 7);
        p.CloseSubpath();
    };
    NoPen(gc);
    gc.SetBrush(wxBrush(c));
    switch (action) {
        case Rewind:
            gc.DrawRectangle(cx - 9, cy - 7, 2.5, 14);
            triangle(cx + 6, -1);
            gc.FillPath(p);
            break;
        case Play:
            if (playing_) {
                gc.DrawRectangle(cx - 6, cy - 7, 4, 14);
                gc.DrawRectangle(cx + 2, cy - 7, 4, 14);
            } else {
                triangle(cx - 5, 1);
                gc.FillPath(p);
            }
            break;
        case Stop:
            gc.DrawRectangle(cx - 6, cy - 6, 12, 12);
            break;
        case Record:
            gc.DrawEllipse(cx - 7, cy - 7, 14, 14);
            break;
        case Loop:
            StrokeRound(gc, wxRect2DDouble(cx - 9, cy - 5, 18, 10), 5, c, 1.8);
            NoPen(gc);
            gc.SetBrush(wxBrush(c));
            p.MoveToPoint(cx + 1, cy - 9);
            p.AddLineToPoint(cx + 7, cy - 5);
            p.AddLineToPoint(cx + 1, cy - 1);
            p.CloseSubpath();
            gc.FillPath(p);
            break;
        case Click: {
            p.MoveToPoint(cx - 3, cy - 8);
            p.AddLineToPoint(cx + 3, cy - 8);
            p.AddLineToPoint(cx + 7, cy + 8);
            p.AddLineToPoint(cx - 7, cy + 8);
            p.CloseSubpath();
            gc.FillPath(p);
            Line(gc, cx, cy + 4, cx + 7, cy - 8, r.m_x < 0 ? c : kBg, 1.6);
            break;
        }
        default:
            break;
    }
}

void TransportBar::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kPanel));
    dc.Clear();
    Gc gc = MakeGc(dc);
    const wxSize size = GetClientSize();
    Line(*gc, 0, size.y - 0.5, size.x, size.y - 0.5, kBorder);

    // Brand mark: a baton stroke beside the product name.
    SetStroke(*gc, kAccent, 3);
    gc->StrokeLine(16, size.y / 2.0 + 10, 30, size.y / 2.0 - 10);
    NoPen(*gc);
    gc->SetBrush(wxBrush(kAccent));
    gc->DrawEllipse(13, size.y / 2.0 + 7, 7, 7);
    DrawCentered(*gc, "Composer", wxRect2DDouble(36, 0, 92, size.y), SerifFont(17, true), kText);
    Line(*gc, 137, 12, 137, size.y - 12, kBorder);

    for (size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const bool hover = int(i) == hover_;
        const bool isView = b.action == ViewScore || b.action == ViewParts || b.action == ViewRoll;
        const bool isZoom = b.action == ZoomIn || b.action == ZoomOut;

        if (isView) {
            const bool on = b.action == view_;
            FillRound(*gc, b.rect, 5, on ? kAccent : hover ? kHover : kPanelAlt);
            const wxString label = b.action == ViewScore   ? "Score"
                                   : b.action == ViewParts ? "Parts"
                                                           : "Piano Roll";
            DrawCentered(*gc, label, b.rect, UiFont(13, on), on ? kAccentInk : kText);
        } else if (isZoom) {
            FillRound(*gc, b.rect, 5, hover ? kHover : kPanelAlt);
            DrawCentered(*gc, b.action == ZoomIn ? wxString("+") : Glyph(0x2212), b.rect, UiFont(18, true),
                         kText);
        } else {
            const bool active = (b.action == Play && playing_) || (b.action == Loop && loop_) ||
                                (b.action == Click && click_) ||
                                (b.action == Record && recording_);
            FillRound(*gc, b.rect, 5, hover ? kHover : kPanelAlt);
            if (active) StrokeRound(*gc, b.rect, 5, kAccent, 1.4);
            const wxColour color = b.action == Record ? kRecord : active ? kAccent : kText;
            DrawIcon(*gc, b.action, b.rect, color);
        }
    }

    DrawReadout(*gc, position_, "BAR / BEAT", wxString::Format("%03d / %d", bar_, beat_));
    DrawReadout(*gc, tempo_, "TEMPO", Glyph(0x2669) + wxString::Format(" %d", tempoValue_));
    DrawReadout(*gc, key_, "KEY", keyLabel_);
    DrawReadout(*gc, time_, "TIME", timeLabel_);
    DrawCentered(*gc, wxString::Format("%d%%", zoom_), zoomLabel_, UiFont(13), kText);
}

TransportBar::Button* TransportBar::FindButton(const wxPoint& p) {
    for (Button& b : buttons_)
        if (Hit(b.rect, p)) return &b;
    return nullptr;
}

void TransportBar::OnMotion(wxMouseEvent& e) {
    Button* b = FindButton(e.GetPosition());
    const int index = b ? int(b - buttons_.data()) : -1;
    if (index == hover_) return;
    hover_ = index;
    SetCursor(b ? wxCURSOR_HAND : wxCURSOR_ARROW);
    Refresh();
}

void TransportBar::OnLeftDown(wxMouseEvent& e) {
    Button* b = FindButton(e.GetPosition());
    if (!b) {
        // The tempo, key and time readouts open the project settings.
        const wxPoint p = e.GetPosition();
        if ((Hit(tempo_, p) || Hit(key_, p) || Hit(time_, p)) && onAction) onAction(OpenSettings);
        return;
    }
    switch (b->action) {
        case Loop: loop_ = !loop_; break;
        case Click: click_ = !click_; break;
        case Record: recording_ = !recording_; break;
        case ViewScore:
        case ViewParts:
        case ViewRoll: view_ = b->action; break;
        default: break;
    }
    Refresh();
    if (onAction) onAction(b->action);
}
