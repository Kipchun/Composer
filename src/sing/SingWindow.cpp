#include "sing/SingWindow.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "Theme.h"
#include "director/ScoreText.h"

using namespace ui;

namespace {
constexpr double kEps = 1e-6;
constexpr int kKeyW = 46, kRulerH = 24, kBarH = 12, kRowH = 12;
constexpr int kLowest = 24, kHighest = 108;  // C1 to C8
const double kGrids[] = {1.0, 0.5, 0.25};

bool IsBlack(int midi) {
    const int pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// A small input level meter.

class LevelBar : public wxWindow {
public:
    explicit LevelBar(wxWindow* parent) : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(110, 10)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(kPanelAlt));
            dc.Clear();
            Gc gc = MakeGc(dc);
            const wxSize size = GetClientSize();
            FillRound(*gc, {0, 0, double(size.x), double(size.y)}, 3, kBg);
            const double width = size.x * std::clamp(level_, 0.0, 1.0);
            if (width > 1) FillRound(*gc, {0, 0, width, double(size.y)}, 3, level_ > 0.9 ? kRecord : kProposalBright);
        });
    }
    void SetLevel(double level) {
        // Fast rise, slow fall, on a square-root scale so quiet singing still shows.
        const double shown = std::sqrt(std::max(0.0, level));
        level_ = shown > level_ ? shown : level_ * 0.85;
        Refresh(false);
    }

private:
    double level_ = 0;
};

// ---------------------------------------------------------------------------------------------
// The piano roll: notes as bars on a pitch-by-time grid, with the sung pitch traced behind them.

class PianoRoll : public wxWindow {
public:
    PianoRoll(wxWindow* parent, SingWindow* owner) : wxWindow(parent, wxID_ANY), w_(*owner) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(600, 300));
        Bind(wxEVT_PAINT, &PianoRoll::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &PianoRoll::OnDown, this);
        Bind(wxEVT_LEFT_DCLICK, &PianoRoll::OnDouble, this);
        Bind(wxEVT_LEFT_UP, &PianoRoll::OnUp, this);
        Bind(wxEVT_MOTION, &PianoRoll::OnMotion, this);
        Bind(wxEVT_MOUSEWHEEL, &PianoRoll::OnWheel, this);
        Bind(wxEVT_KEY_DOWN, &PianoRoll::OnKey, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { drag_ = Drag::None; });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            ClampScroll();
            Refresh(false);
            e.Skip();
        });
    }

    void SetPlayhead(double beat, bool visible) {
        playhead_ = beat;
        showHead_ = visible;
        if (visible) {
            // Keep the playhead on screen, paging ahead like the score does.
            const double width = ViewBeats();
            if (beat < left_ || beat > left_ + width * 0.85) {
                left_ = std::max(0.0, beat - width * 0.15);
                ClampScroll();
            }
        }
        Refresh(false);
    }

    // Shows the given beats and pitches.
    void Reveal(double beat, int lowMidi, int highMidi) {
        if (beat < left_ || beat > left_ + ViewBeats() * 0.9) left_ = std::max(0.0, beat - 1);
        const int rows = VisibleRows();
        if (highMidi >= lowMidi) {
            const int middle = (lowMidi + highMidi + 1) / 2;
            if (highMidi > top_ || lowMidi < top_ - rows + 1) top_ = middle + rows / 2;
        }
        ClampScroll();
        Refresh(false);
    }

    void CentreOn(int midi) {
        top_ = midi + VisibleRows() / 2;
        ClampScroll();
        Refresh(false);
    }

private:
    enum class Drag { None, Move, Resize, Band, Thumb };

    // Geometry.
    double ContentW() const { return std::max(1, GetClientSize().x - kKeyW); }
    double ContentH() const { return std::max(1, GetClientSize().y - kRulerH - kBarH); }
    double ViewBeats() const { return ContentW() / pxPerBeat_; }
    int VisibleRows() const { return int(ContentH() / kRowH); }
    double TotalBeats() const { return std::max(4.0, w_.GetProject().TotalBeats()); }
    double X(double beat) const { return kKeyW + (beat - left_) * pxPerBeat_; }
    double BeatAt(double x) const { return left_ + (x - kKeyW) / pxPerBeat_; }
    double Y(int midi) const { return kRulerH + (top_ - midi) * double(kRowH); }
    int MidiAt(double y) const { return top_ - int(std::floor((y - kRulerH) / kRowH)); }
    wxRect2DDouble NoteRect(const PatternNote& n) const {
        return {X(n.beat), Y(n.midi) + 1, std::max(3.0, n.length * pxPerBeat_ - 1), kRowH - 2.0};
    }
    wxRect2DDouble Thumb() const {
        const double track = ContentW(), total = TotalBeats();
        const double width = std::max(30.0, track * std::min(1.0, ViewBeats() / total));
        const double x = kKeyW + (track - width) * (total > ViewBeats() ? left_ / (total - ViewBeats()) : 0);
        return {x, GetClientSize().y - double(kBarH) + 2, width, kBarH - 4.0};
    }

    void ClampScroll() {
        left_ = std::clamp(left_, 0.0, std::max(0.0, TotalBeats() - ViewBeats() * 0.5));
        top_ = std::clamp(top_, kLowest + VisibleRows() - 1, kHighest);
    }

    double Snap(double beat) const {
        const double grid = w_.Grid();
        return std::round(beat / grid) * grid;
    }

    int HitNote(const wxPoint& p, bool& edge) const {
        auto& notes = w_.notes_;
        for (int i = int(notes.size()) - 1; i >= 0; --i) {
            const wxRect2DDouble r = NoteRect(notes[i]);
            if (Hit(r, p)) {
                edge = p.x >= r.m_x + r.m_width - 6 && r.m_width > 10;
                return i;
            }
        }
        edge = false;
        return -1;
    }

    void Audition(int midi) {
        if (w_.engine_ && !w_.IsBusy()) w_.engine_->Preview(w_.Track(), midi);
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(kBg));
        dc.Clear();
        Gc gcp = MakeGc(dc);
        wxGraphicsContext& gc = *gcp;
        const wxSize size = GetClientSize();
        const Project& project = w_.GetProject();
        const double right = size.x, bottom = size.y - kBarH;

        // Rows.
        gc.Clip(kKeyW, kRulerH, right - kKeyW, bottom - kRulerH);
        for (int midi = top_; midi >= kLowest && Y(midi) < bottom; --midi) {
            const double y = Y(midi);
            NoPen(gc);
            gc.SetBrush(wxBrush(IsBlack(midi) ? wxColour(0x18, 0x1A, 0x20) : wxColour(0x20, 0x23, 0x2B)));
            gc.DrawRectangle(kKeyW, y, right - kKeyW, kRowH);
            if (midi % 12 == 0) Line(gc, kKeyW, y + kRowH, right, y + kRowH, kBorder);
        }

        // Beats and bars of the project.
        const int bars = project.settings.bars;
        for (int bar = 0; bar < bars; ++bar) {
            const double start = project.BarStart(bar);
            if (start > left_ + ViewBeats()) break;
            const double length = project.BarBeats(bar), unit = project.BeatUnit(bar), grid = w_.Grid();
            if (start + length < left_) continue;
            for (double b = 0; b < length - kEps; b += grid) {
                const double x = X(start + b);
                const bool onBeat = std::abs(std::fmod(b + kEps, unit)) < 2 * kEps;
                if (b > kEps && pxPerBeat_ * grid >= 6)
                    Line(gc, x, kRulerH, x, bottom, onBeat ? wxColour(0x34, 0x39, 0x45) : wxColour(0x27, 0x2B, 0x33));
            }
            Line(gc, X(start), kRulerH, X(start), bottom, wxColour(0x55, 0x5C, 0x6C), 1.2);
        }
        const double end = X(project.TotalBeats());
        if (end < right) {
            NoPen(gc);
            gc.SetBrush(wxBrush(WithAlpha(*wxBLACK, 90)));
            gc.DrawRectangle(end, kRulerH, right - end, bottom - kRulerH);
        }

        // The pitch that was sung, under the notes.
        if (!w_.trace_.empty()) {
            const double shift = w_.shift_;
            wxGraphicsPath path = gc.CreatePath();
            bool drawing = false;
            double lastBeat = -1;
            for (const auto& [beat, midi] : w_.trace_) {
                if (beat < left_ - 1 || beat > left_ + ViewBeats() + 1) {
                    drawing = false;
                    continue;
                }
                const double x = X(beat), y = Y(0) - (midi + shift) * kRowH + kRowH / 2.0;
                if (midi <= 0 || (drawing && beat - lastBeat > 0.15)) drawing = false;
                if (midi <= 0) continue;
                if (drawing) path.AddLineToPoint(x, y);
                else path.MoveToPoint(x, y);
                drawing = true;
                lastBeat = beat;
            }
            SetStroke(gc, WithAlpha(kProposalBright, 150), 1.6);
            gc.StrokePath(path);
        }

        // Notes.
        const wxFont label = UiFont(10, true);
        const int fifths = project.settings.fifths;
        for (const PatternNote& note : w_.notes_) {
            const wxRect2DDouble r = NoteRect(note);
            if (r.m_x > right || r.m_x + r.m_width < kKeyW) continue;
            const wxColour fill = note.selected ? kSelect : kAccent;
            FillRound(gc, r, 3, fill);
            StrokeRound(gc, r, 3, note.selected ? wxColour(0xBF, 0xD7, 0xFF) : WithAlpha(*wxBLACK, 120));
            if (r.m_width > 30) {
                DrawTextAt(gc, MidiName(note.midi, fifths), r.m_x + 4, r.m_y - 1, label,
                           note.selected ? *wxWHITE : kAccentInk);
            }
        }

        if (drag_ == Drag::Band) {
            const double x0 = std::min(bandFrom_.m_x, bandTo_.m_x), y0 = std::min(bandFrom_.m_y, bandTo_.m_y);
            const wxRect2DDouble r(x0, y0, std::abs(bandTo_.m_x - bandFrom_.m_x), std::abs(bandTo_.m_y - bandFrom_.m_y));
            NoPen(gc);
            gc.SetBrush(wxBrush(WithAlpha(kSelect, 50)));
            gc.DrawRectangle(r.m_x, r.m_y, r.m_width, r.m_height);
            StrokeRound(gc, r, 0, kSelect);
        }

        if (showHead_) Line(gc, X(playhead_), kRulerH, X(playhead_), bottom, kRecord, 1.5);
        gc.ResetClip();

        // Keyboard.
        gc.Clip(0, kRulerH, kKeyW, bottom - kRulerH);
        const wxFont keyFont = UiFont(10);
        for (int midi = top_; midi >= kLowest && Y(midi) < bottom; --midi) {
            const double y = Y(midi);
            NoPen(gc);
            gc.SetBrush(wxBrush(IsBlack(midi) ? wxColour(0x2A, 0x2D, 0x35) : wxColour(0xD9, 0xDC, 0xE3)));
            gc.DrawRectangle(0, y, kKeyW - 1, kRowH - 1);
            if (midi % 12 == 0) DrawTextAt(gc, MidiName(midi, 0), 4, y - 1, keyFont, kInk);
        }
        gc.ResetClip();

        // Ruler with the project's bar numbers.
        NoPen(gc);
        gc.SetBrush(wxBrush(kPanelAlt));
        gc.DrawRectangle(0, 0, right, kRulerH);
        const wxFont rulerFont = UiFont(11);
        gc.Clip(kKeyW, 0, right - kKeyW, kRulerH);
        for (int bar = 0; bar < bars; ++bar) {
            const double start = project.BarStart(bar);
            if (start > left_ + ViewBeats()) break;
            if (start + project.BarBeats(bar) < left_) continue;
            Line(gc, X(start), 12, X(start), kRulerH, kMuted);
            DrawTextAt(gc, wxString::Format("%d", bar + 1), X(start) + 4, 4, rulerFont, kText);
        }
        // Where the next take starts.
        const double from = project.BarStart(std::clamp(w_.fromBar_->GetValue() - 1, 0, std::max(0, bars - 1)));
        wxGraphicsPath marker = gc.CreatePath();
        marker.MoveToPoint(X(from), kRulerH);
        marker.AddLineToPoint(X(from) + 9, kRulerH);
        marker.AddLineToPoint(X(from), kRulerH - 9);
        marker.CloseSubpath();
        NoPen(gc);
        gc.SetBrush(wxBrush(kRecord));
        gc.FillPath(marker);
        gc.ResetClip();

        // Scroll bar.
        NoPen(gc);
        gc.SetBrush(wxBrush(kPanel));
        gc.DrawRectangle(0, bottom, right, kBarH);
        FillRound(gc, Thumb(), 3, drag_ == Drag::Thumb ? kMuted : kHover);

        if (w_.notes_.empty() && w_.trace_.empty() && w_.mode_ == SingWindow::Mode::Idle) {
            DrawCentered(gc, "Press Record (R) and sing. Double-click to add a note by hand.",
                         {double(kKeyW), double(kRulerH), right - kKeyW, bottom - kRulerH}, UiFont(15), kMuted);
        }
    }

    void OnDown(wxMouseEvent& e) {
        SetFocus();
        const wxPoint p = e.GetPosition();
        const wxSize size = GetClientSize();
        if (p.y >= size.y - kBarH) {
            drag_ = Drag::Thumb;
            thumbGrab_ = p.x - Thumb().m_x;
            CaptureMouse();
            return;
        }
        if (p.y < kRulerH) {
            // Clicking the ruler moves where the next take starts.
            if (p.x > kKeyW && !w_.IsBusy()) {
                const Project& project = w_.GetProject();
                const int bar = project.BarAtBeat(std::max(0.0, BeatAt(p.x)));
                w_.fromBar_->SetValue(bar + 1);
                Refresh(false);
            }
            return;
        }
        if (p.x < kKeyW || w_.mode_ == SingWindow::Mode::Recording) return;

        auto& notes = w_.notes_;
        bool edge = false;
        const int hit = HitNote(p, edge);
        if (hit < 0) {
            if (!e.ShiftDown() && !e.ControlDown())
                for (auto& n : notes) n.selected = false;
            drag_ = Drag::Band;
            bandFrom_ = bandTo_ = wxPoint2DDouble(p.x, p.y);
            CaptureMouse();
            Refresh(false);
            return;
        }

        if (e.AltDown()) {
            // Alt-click splits a note at the nearest grid line.
            PatternNote& note = notes[hit];
            const double at = Snap(BeatAt(p.x));
            if (at > note.beat + kEps && at < note.beat + note.length - kEps) {
                w_.PushUndo();
                PatternNote second = note;
                second.beat = at;
                second.length = note.beat + note.length - at;
                notes[hit].length = at - note.beat;
                notes.insert(notes.begin() + hit + 1, second);
                w_.Changed();
            }
            return;
        }

        if (e.ShiftDown() || e.ControlDown()) {
            notes[hit].selected = !notes[hit].selected;
            Refresh(false);
            if (!notes[hit].selected) return;
        } else if (!notes[hit].selected) {
            for (auto& n : notes) n.selected = false;
            notes[hit].selected = true;
        }
        Audition(notes[hit].midi);
        drag_ = edge ? Drag::Resize : Drag::Move;
        grabBeat_ = BeatAt(p.x);
        grabMidi_ = MidiAt(p.y);
        grabIndex_ = hit;
        original_ = notes;
        moved_ = false;
        lastMidiShift_ = 0;
        CaptureMouse();
        Refresh(false);
    }

    void OnMotion(wxMouseEvent& e) {
        const wxPoint p = e.GetPosition();
        if (drag_ == Drag::None) {
            bool edge = false;
            const bool over = p.y > kRulerH && p.x > kKeyW && HitNote(p, edge) >= 0;
            SetCursor(over && edge ? wxCursor(wxCURSOR_SIZEWE) : wxNullCursor);
            return;
        }
        if (drag_ == Drag::Thumb) {
            const double track = ContentW() - Thumb().m_width;
            const double extra = std::max(0.0, TotalBeats() - ViewBeats());
            left_ = track > 0 ? (p.x - thumbGrab_ - kKeyW) / track * extra : 0;
            ClampScroll();
            Refresh(false);
            return;
        }
        if (drag_ == Drag::Band) {
            bandTo_ = wxPoint2DDouble(p.x, p.y);
            const double x0 = std::min(bandFrom_.m_x, bandTo_.m_x), x1 = std::max(bandFrom_.m_x, bandTo_.m_x);
            const double y0 = std::min(bandFrom_.m_y, bandTo_.m_y), y1 = std::max(bandFrom_.m_y, bandTo_.m_y);
            for (auto& note : w_.notes_) {
                const wxRect2DDouble r = NoteRect(note);
                if (r.m_x < x1 && r.m_x + r.m_width > x0 && r.m_y < y1 && r.m_y + r.m_height > y0) note.selected = true;
                else if (!e.ShiftDown() && !e.ControlDown()) note.selected = false;
            }
            Refresh(false);
            return;
        }

        auto& notes = w_.notes_;
        const double grid = w_.Grid();
        const double deltaBeat = std::round((BeatAt(p.x) - grabBeat_) / grid) * grid;
        const int deltaMidi = drag_ == Drag::Move ? MidiAt(p.y) - grabMidi_ : 0;
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (!notes[i].selected) continue;
            PatternNote next = original_[i];
            if (drag_ == Drag::Move) {
                next.beat = std::max(0.0, original_[i].beat + deltaBeat);
                next.midi = std::clamp(original_[i].midi + deltaMidi, kLowest, kHighest);
            } else {
                const double end = Snap(original_[i].beat + original_[i].length + deltaBeat);
                next.length = std::max(grid, end - next.beat);
            }
            if (std::abs(next.beat - notes[i].beat) > kEps || std::abs(next.length - notes[i].length) > kEps ||
                next.midi != notes[i].midi) {
                changed = true;
                notes[i].beat = next.beat;
                notes[i].length = next.length;
                notes[i].midi = next.midi;
            }
        }
        if (changed && !moved_) {
            // One undo step for the whole drag, holding the notes as they were.
            moved_ = true;
            w_.undo_.push_back(original_);
            w_.redo_.clear();
        }
        if (changed && deltaMidi != lastMidiShift_ && grabIndex_ < int(notes.size())) {
            lastMidiShift_ = deltaMidi;
            Audition(notes[grabIndex_].midi);
        }
        if (changed) Refresh(false);
    }

    void OnUp(wxMouseEvent&) {
        if (HasCapture()) ReleaseMouse();
        const Drag was = drag_;
        drag_ = Drag::None;
        if ((was == Drag::Move || was == Drag::Resize) && moved_) {
            for (auto& n : w_.notes_) n.selected = n.selected;  // selection survives the sort below
            w_.edited_ = true;
            SortAndTrim(w_.notes_);
            w_.Changed();
        }
        Refresh(false);
    }

    void OnDouble(wxMouseEvent& e) {
        const wxPoint p = e.GetPosition();
        if (p.y < kRulerH || p.x < kKeyW || p.y >= GetClientSize().y - kBarH || w_.IsBusy()) return;
        bool edge;
        const int hit = HitNote(p, edge);
        if (hit >= 0) return;
        const double grid = w_.Grid();
        PatternNote note;
        note.beat = std::max(0.0, std::floor(BeatAt(p.x) / grid) * grid);
        note.length = grid;
        note.midi = MidiAt(p.y);
        w_.PushUndo();
        for (auto& n : w_.notes_) n.selected = false;
        note.selected = true;
        w_.notes_.push_back(note);
        w_.edited_ = true;
        SortAndTrim(w_.notes_);
        Audition(note.midi);
        w_.Changed();
    }

    void OnWheel(wxMouseEvent& e) {
        const int steps = e.GetWheelRotation() / std::max(1, e.GetWheelDelta());
        if (e.ControlDown()) {
            // Zoom around the mouse.
            const double anchor = BeatAt(e.GetX());
            pxPerBeat_ = std::clamp(pxPerBeat_ * std::pow(1.15, steps), 12.0, 240.0);
            left_ = anchor - (e.GetX() - kKeyW) / pxPerBeat_;
        } else if (e.ShiftDown() || e.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
            left_ -= steps * ViewBeats() / 8 * (e.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL ? -1 : 1);
        } else {
            top_ += steps * 3;
        }
        ClampScroll();
        Refresh(false);
    }

    void OnKey(wxKeyEvent& e) {
        auto& notes = w_.notes_;
        const int key = e.GetKeyCode();
        const bool any = std::any_of(notes.begin(), notes.end(), [](const PatternNote& n) { return n.selected; });
        if (e.ControlDown() && key == 'A') {
            for (auto& n : notes) n.selected = true;
            Refresh(false);
        } else if (e.ControlDown() && key == 'Z') {
            w_.Undo();
        } else if (e.ControlDown() && key == 'Y') {
            w_.Redo();
        } else if ((key == WXK_DELETE || key == WXK_BACK) && any && !w_.IsBusy()) {
            w_.PushUndo();
            notes.erase(std::remove_if(notes.begin(), notes.end(), [](const PatternNote& n) { return n.selected; }),
                        notes.end());
            w_.edited_ = true;
            w_.Changed();
        } else if ((key == WXK_UP || key == WXK_DOWN) && !w_.IsBusy()) {
            w_.Transpose((key == WXK_UP ? 1 : -1) * (e.ShiftDown() ? 12 : 1));
        } else if ((key == WXK_LEFT || key == WXK_RIGHT) && any && !w_.IsBusy()) {
            const double delta = (key == WXK_RIGHT ? 1 : -1) * w_.Grid();
            w_.PushUndo();
            for (auto& n : notes)
                if (n.selected) n.beat = std::max(0.0, n.beat + delta);
            w_.edited_ = true;
            SortAndTrim(notes);
            w_.Changed();
        } else if (key == 'Q' && !w_.IsBusy()) {
            w_.PushUndo();
            SnapNotes(notes, w_.Grid(), any);
            w_.Changed();
        } else if (key == WXK_SPACE) {
            if (w_.IsBusy()) w_.StopAll();
            else w_.StartPlayback();
        } else if (key == 'R' && !e.ControlDown()) {
            if (w_.IsBusy()) w_.StopAll();
            else w_.StartRecording();
        } else if (key == WXK_ESCAPE) {
            if (w_.IsBusy()) {
                w_.StopAll();
            } else {
                for (auto& n : notes) n.selected = false;
                Refresh(false);
            }
        } else {
            e.Skip();
        }
    }

    SingWindow& w_;
    double left_ = 0;        // beat at the left edge
    int top_ = 79;           // midi of the top row
    double pxPerBeat_ = 56;
    double playhead_ = 0;
    bool showHead_ = false;

    Drag drag_ = Drag::None;
    double grabBeat_ = 0, thumbGrab_ = 0;
    int grabMidi_ = 0, grabIndex_ = -1, lastMidiShift_ = 0;
    bool moved_ = false;
    std::vector<PatternNote> original_;
    wxPoint2DDouble bandFrom_, bandTo_;
};

// ---------------------------------------------------------------------------------------------

SingWindow::SingWindow(wxWindow* parent, Host host, AudioEngine* engine)
    : wxFrame(parent, wxID_ANY, "Sing a Melody", wxDefaultPosition, wxSize(1080, 640),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
      host_(std::move(host)),
      engine_(engine),
      timer_(this) {
    BuildUi();
    RefreshStaves();
    UpdateButtons();
    Bind(wxEVT_TIMER, &SingWindow::OnTick, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        StopAll();
        if (e.CanVeto()) {
            e.Veto();
            Hide();  // keeps the pattern for next time
        } else {
            e.Skip();
        }
    });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        // Space and R work from anywhere in the window except the text box.
        const bool typing = wxWindow::FindFocus() == ideas_;
        if (!typing && !e.ControlDown() && !e.AltDown() &&
            (e.GetKeyCode() == WXK_SPACE || e.GetKeyCode() == 'R') && wxWindow::FindFocus() != roll_) {
            if (IsBusy()) StopAll();
            else if (e.GetKeyCode() == 'R') StartRecording();
            else StartPlayback();
            return;
        }
        e.Skip();
    });
}

SingWindow::~SingWindow() {
    timer_.Stop();
    mic_.Stop();
}

void SingWindow::BuildUi() {
    auto* root = new wxPanel(this);
    root->SetBackgroundColour(kPanel);
    const auto label = [](wxWindow* parent, const wxString& text) {
        auto* l = new wxStaticText(parent, wxID_ANY, text);
        l->SetForegroundColour(kMuted);
        return l;
    };

    // Recording row.
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    record_ = new wxButton(root, wxID_ANY, Glyph(0x25CF) + "  Record");
    record_->SetForegroundColour(kRecord);
    play_ = new wxButton(root, wxID_ANY, Glyph(0x25B6) + "  Play");
    stop_ = new wxButton(root, wxID_ANY, Glyph(0x25A0) + "  Stop");
    staff_ = new wxChoice(root, wxID_ANY, wxDefaultPosition, wxSize(170, -1));
    fromBar_ = new wxSpinCtrl(root, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 999, 1);
    countIn_ = new wxChoice(root, wxID_ANY);
    countIn_->Append("No count-in");
    countIn_->Append("1 bar count-in");
    countIn_->Append("2 bars count-in");
    countIn_->SetSelection(1);
    click_ = new wxCheckBox(root, wxID_ANY, "Click");
    click_->SetValue(true);
    hearScore_ = new wxCheckBox(root, wxID_ANY, "Hear the score");
    hearScore_->SetValue(true);
    level_ = new LevelBar(root);

    top->Add(record_, 0, wxRIGHT, 6);
    top->Add(play_, 0, wxRIGHT, 6);
    top->Add(stop_, 0, wxRIGHT, 18);
    top->Add(label(root, "Staff"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(staff_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    top->Add(label(root, "From bar"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(fromBar_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    top->Add(countIn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    top->Add(click_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    top->Add(hearScore_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    top->AddStretchSpacer();
    top->Add(label(root, "Mic"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(level_, 0, wxALIGN_CENTER_VERTICAL);

    // Editing row.
    auto* edit = new wxBoxSizer(wxHORIZONTAL);
    grid_ = new wxChoice(root, wxID_ANY);
    grid_->Append("Quarter notes");
    grid_->Append("Eighth notes");
    grid_->Append("Sixteenth notes");
    grid_->SetSelection(1);
    keepKey_ = new wxCheckBox(root, wxID_ANY, "Keep to the key");
    auto* octaveDown = new wxButton(root, wxID_ANY, "Octave " + Glyph(0x2193), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* octaveUp = new wxButton(root, wxID_ANY, "Octave " + Glyph(0x2191), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* semiDown = new wxButton(root, wxID_ANY, "Semitone " + Glyph(0x2193), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* semiUp = new wxButton(root, wxID_ANY, "Semitone " + Glyph(0x2191), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* snap = new wxButton(root, wxID_ANY, "Snap to Grid (Q)", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* clear = new wxButton(root, wxID_ANY, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    edit->Add(label(root, "Grid"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    edit->Add(grid_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    edit->Add(keepKey_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
    edit->Add(octaveDown, 0, wxRIGHT, 4);
    edit->Add(octaveUp, 0, wxRIGHT, 10);
    edit->Add(semiDown, 0, wxRIGHT, 4);
    edit->Add(semiUp, 0, wxRIGHT, 18);
    edit->Add(snap, 0, wxRIGHT, 4);
    edit->Add(clear, 0);
    edit->AddStretchSpacer();
    auto* help = label(root, "Drag to move or resize  " + Glyph(0x00B7) + "  Alt-click splits  " + Glyph(0x00B7) +
                                 "  Ctrl+wheel zooms");
    edit->Add(help, 0, wxALIGN_CENTER_VERTICAL);

    roll_ = new PianoRoll(root, this);

    // Output row.
    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    status_ = new wxStaticText(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    status_->SetForegroundColour(kText);
    ideas_ = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxSize(320, -1), wxTE_PROCESS_ENTER);
    ideas_->SetHint("Ideas for the Director, e.g. \"harmonise it in the strings\"");
    develop_ = new wxButton(root, wxID_ANY, "Develop with Director");
    insert_ = new wxButton(root, wxID_ANY, "Insert into Score");
    bottom->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    bottom->Add(ideas_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    bottom->Add(develop_, 0, wxRIGHT, 6);
    bottom->Add(insert_, 0);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(top, 0, wxEXPAND | wxALL, 10);
    sizer->Add(edit, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    sizer->Add(roll_, 1, wxEXPAND);
    sizer->Add(bottom, 0, wxEXPAND | wxALL, 10);
    root->SetSizer(sizer);

    record_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartRecording(); });
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartPlayback(); });
    stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StopAll(); });
    staff_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        staffName_ = staff_->GetStringSelection();
        FitOctaveToStaff();
    });
    fromBar_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { roll_->Refresh(false); });
    grid_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { Requantize(); });
    keepKey_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (!edited_ && !sung_.empty()) Requantize();
    });
    octaveDown->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Transpose(-12); });
    octaveUp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Transpose(12); });
    semiDown->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Transpose(-1); });
    semiUp->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Transpose(1); });
    snap->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        PushUndo();
        const bool any = std::any_of(notes_.begin(), notes_.end(), [](const PatternNote& n) { return n.selected; });
        SnapNotes(notes_, Grid(), any);
        Changed();
    });
    clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (IsBusy() || (notes_.empty() && trace_.empty())) return;
        PushUndo();
        notes_.clear();
        trace_.clear();
        sung_.clear();
        shift_ = 0;
        edited_ = false;
        Changed();
        SetStatus("Cleared. Ctrl+Z brings the notes back.");
    });
    insert_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { InsertIntoScore(); });
    develop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DevelopWithDirector(); });
    ideas_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { DevelopWithDirector(); });
}

double SingWindow::Grid() const { return kGrids[std::clamp(grid_->GetSelection(), 0, 2)]; }

int SingWindow::Staff() const {
    const int count = int(GetProject().staves.size());
    return std::clamp(staff_->GetSelection(), 0, std::max(0, count - 1));
}

int SingWindow::Track() const {
    const Project& project = GetProject();
    return project.staves.empty() ? 0 : project.staves[Staff()].track;
}

void SingWindow::SetStatus(const wxString& text) {
    status_->SetLabel(text);
    status_->SetToolTip(text);
    status_->GetParent()->Layout();
}

void SingWindow::RefreshStaves() {
    const Project& project = GetProject();
    staff_->Clear();
    int keep = -1;
    for (int staff = 0; staff < int(project.staves.size()); ++staff) {
        const wxString name = StaffLabel(project, staff);
        staff_->Append(name);
        if (name == staffName_) keep = staff;
    }
    staff_->SetSelection(keep >= 0 ? keep : 0);
    staffName_ = staff_->GetStringSelection();
    fromBar_->SetRange(1, std::max(1, project.settings.bars));
    roll_->Refresh(false);
}

void SingWindow::Detach() {
    StopAll();
    engine_ = nullptr;
}

void SingWindow::Attach(AudioEngine* engine) {
    engine_ = engine;
    RefreshStaves();
    UpdateButtons();
}

void SingWindow::ShowFor(int bar) {
    if (bar >= 0 && !IsBusy()) fromBar_->SetValue(bar + 1);
    Show();
    Raise();
    roll_->SetFocus();
    roll_->Refresh(false);
    if (notes_.empty() && status_->GetLabel().empty())
        SetStatus("Choose a staff and a starting bar, then press Record and sing after the count-in.");
}

void SingWindow::UpdateButtons() {
    const bool idle = mode_ == Mode::Idle;
    record_->Enable(idle && engine_);
    play_->Enable(idle && engine_ && (!notes_.empty() || hearScore_->GetValue()));
    stop_->Enable(!idle);
    staff_->Enable(idle);
    fromBar_->Enable(idle);
    countIn_->Enable(idle);
    insert_->Enable(idle && !notes_.empty());
    develop_->Enable(idle && !notes_.empty());
    record_->SetLabel(mode_ == Mode::Recording ? Glyph(0x25CF) + "  Recording" : Glyph(0x25CF) + "  Record");
}

// ---------------------------------------------------------------------------------------------
// Recording and playback

void SingWindow::StartRecording() {
    if (IsBusy() || !engine_) return;
    const Project& project = GetProject();
    if (project.staves.empty()) return;
    host_.stopMainPlayback();

    std::string error;
    if (!mic_.Start(&error)) {
        SetStatus(wxString::FromUTF8(error));
        wxMessageBox(wxString::FromUTF8(error), "Sing a Melody", wxOK | wxICON_WARNING, this);
        return;
    }

    const int bar = std::clamp(fromBar_->GetValue() - 1, 0, project.settings.bars - 1);
    takeStartBeat_ = project.BarStart(bar);
    takeEndBeat_ = takeStartBeat_;
    tracker_ = std::make_unique<PitchTracker>(mic_.SampleRate());
    micRead_ = 0;
    haveOffset_ = false;
    sung_.clear();
    trace_.clear();
    traceDone_ = 0;
    shift_ = 0;
    silentWarned_ = false;

    // Recording over part of the pattern replaces just that part.
    PushUndo();
    beforeTake_ = notes_;
    takeCount_ = 0;
    takeOctaves_ = 0;
    keptOutside_.clear();
    for (const PatternNote& note : notes_)
        if (note.beat + note.length <= takeStartBeat_ + kEps) keptOutside_.push_back(note);
    for (auto& note : keptOutside_) note.selected = false;
    notes_ = keptOutside_;

    const std::vector<PlayNote> score = hearScore_->GetValue() ? host_.scorePlayback() : std::vector<PlayNote>{};
    engine_->SetSchedule(score, project.TotalBeats(), host_.clickBeats(), host_.tempoPoints());
    engine_->SetClick(click_->GetValue());

    const int bars = countIn_->GetSelection();
    const double unit = project.BeatUnit(bar);
    const int perBar = std::max(1, int(std::lround(project.BarBeats(bar) / unit)));
    const double secondsPerClick = unit * 60.0 / project.TempoAtBeat(takeStartBeat_);
    engine_->PlayTake(takeStartBeat_, project.TotalBeats(), bars * perBar, secondsPerClick);

    mode_ = Mode::Recording;
    if (host_.recordingChanged) host_.recordingChanged(true);
    UpdateButtons();
    roll_->SetPlayhead(takeStartBeat_, true);
    roll_->SetFocus();
    SetStatus(bars > 0 ? "Count-in..." : "Recording. Sing now.");
    timer_.Start(30);
}

void SingWindow::StartPlayback() {
    if (IsBusy() || !engine_) return;
    const Project& project = GetProject();
    if (project.staves.empty()) return;
    host_.stopMainPlayback();

    std::vector<PlayNote> schedule = hearScore_->GetValue() ? host_.scorePlayback() : std::vector<PlayNote>{};
    const int track = Track();
    double last = 0;
    for (const PatternNote& note : notes_) {
        schedule.push_back({track, note.beat, note.length, note.midi, 0.75f});
        last = std::max(last, note.beat + note.length);
    }
    const int bar = std::clamp(fromBar_->GetValue() - 1, 0, project.settings.bars - 1);
    double start = project.BarStart(bar);
    // Start where the pattern does if the chosen bar is after it.
    if (!notes_.empty() && start >= last) start = project.BarStart(project.BarAtBeat(notes_.front().beat));
    const double stop = std::min(project.TotalBeats(), last > start ? last + 1 : project.TotalBeats());

    engine_->SetSchedule(schedule, project.TotalBeats(), host_.clickBeats(), host_.tempoPoints());
    engine_->SetClick(click_->GetValue() && hearScore_->GetValue());
    engine_->PlayTake(start, stop, 0, 1);
    mode_ = Mode::Playing;
    UpdateButtons();
    roll_->SetPlayhead(start, true);
    SetStatus("Playing");
    timer_.Start(30);
}

void SingWindow::StopAll() {
    if (mode_ == Mode::Idle) return;
    if (engine_) engine_->Stop();
    Finish();
}

void SingWindow::Finish() {
    timer_.Stop();
    const Mode was = mode_;
    mode_ = Mode::Idle;
    if (was == Mode::Recording) {
        UpdateTake(true);
        mic_.Stop();
        level_->SetLevel(0);
        if (host_.recordingChanged) host_.recordingChanged(false);
    }
    if (engine_) engine_->Stop();
    host_.restoreSchedule();
    roll_->SetPlayhead(0, false);
    UpdateButtons();

    if (was == Mode::Recording) {
        const size_t found = takeCount_;
        if (sung_.empty()) {
            SetStatus(trace_.empty() ? "Nothing was heard. Check the microphone level and Windows privacy settings."
                                     : "No clear notes were found. Try singing louder or closer to the microphone.");
        } else {
            wxString text = wxString::Format("Found %zu note%s", found, found == 1 ? "" : "s");
            if (takeOctaves_ != 0)
                text += wxString::Format(", moved %s %d octave%s for %s", takeOctaves_ > 0 ? "up" : "down",
                                         std::abs(takeOctaves_), std::abs(takeOctaves_) == 1 ? "" : "s",
                                         staff_->GetStringSelection());
            if (std::abs(tuningCents_) >= 10)
                text += wxString::Format(" (you were %d cents %s; allowed for)", int(std::abs(tuningCents_)),
                                         tuningCents_ > 0 ? "sharp" : "flat");
            SetStatus(text + ".");
        }
        if (!notes_.empty()) {
            int low = 127, high = 0;
            for (const PatternNote& note : notes_) {
                low = std::min(low, note.midi);
                high = std::max(high, note.midi);
            }
            roll_->Reveal(takeStartBeat_, low, high);
        }
    } else {
        SetStatus(wxEmptyString);
    }
    roll_->Refresh(false);
}

void SingWindow::OnTick(wxTimerEvent&) {
    if (!engine_) return;
    const bool countIn = engine_->InCountIn();
    if (mode_ == Mode::Recording) {
        level_->SetLevel(mic_.TakePeak());
        UpdateTake(false);
        if (!countIn && status_->GetLabel() == "Count-in...") SetStatus("Recording. Sing now. Press Space or R to stop.");
        // A microphone that delivers pure silence is usually blocked by Windows privacy settings.
        if (!silentWarned_ && micRead_ > size_t(mic_.SampleRate() * 2)) {
            silentWarned_ = true;
            bool silent = true;
            for (const PitchFrame& frame : tracker_->Frames()) silent = silent && frame.level <= 0;
            if (silent) SetStatus("The microphone is sending silence. Windows may be blocking it: Settings > Privacy > Microphone.");
        }
    }
    if (!countIn && !engine_->IsPlaying()) {
        Finish();  // reached the end
        return;
    }
    roll_->SetPlayhead(engine_->Beats(), true);
}

void SingWindow::UpdateTake(bool final) {
    std::vector<float> fresh;
    micRead_ = mic_.Read(micRead_, fresh);
    if (!fresh.empty()) tracker_->Feed(fresh.data(), fresh.size());

    if (!haveOffset_) {
        int64_t micStart = 0, takeStart = 0;
        if (!mic_.StartTime(micStart) || !engine_ || !engine_->TakeStartTime(takeStart)) return;
        offsetSeconds_ = double(takeStart - micStart) / 1e7;
        haveOffset_ = true;
    }

    const Project& project = GetProject();
    const TempoClock clock(host_.tempoPoints());
    const double origin = clock.BeatToSeconds(takeStartBeat_);
    const auto& frames = tracker_->Frames();
    for (; traceDone_ < frames.size(); ++traceDone_) {
        const double beat = clock.SecondsToBeat(origin + frames[traceDone_].time - offsetSeconds_);
        if (beat >= takeStartBeat_ - 0.25) trace_.emplace_back(beat, frames[traceDone_].midi);
    }
    if (engine_ && !engine_->InCountIn()) takeEndBeat_ = std::max(takeEndBeat_, engine_->Beats());

    // Re-reading the whole take each time lets later frames correct earlier guesses.
    sung_ = FindNotes(frames, tracker_->FrameSeconds(), offsetSeconds_);
    std::vector<PatternNote> found = QuantizeNotes(sung_, clock, takeStartBeat_, Grid(), project.settings.fifths,
                                                   keepKey_->GetValue(), shift_, &tuningCents_);
    if (!final) {
        // Leave the last note open while it's still being sung.
        found.erase(std::remove_if(found.begin(), found.end(),
                                   [&](const PatternNote& n) { return n.beat >= takeEndBeat_ - 0.05; }),
                    found.end());
    } else if (!found.empty() && !project.staves.empty()) {
        // Voices and instruments rarely share an octave: move the take to suit the staff.
        std::vector<int> pitches;
        for (const PatternNote& note : found) pitches.push_back(note.midi);
        std::nth_element(pitches.begin(), pitches.begin() + pitches.size() / 2, pitches.end());
        const int centre = StepToMidi(project.staves[Staff()].clef, 0, 0, 0);
        takeOctaves_ = int(std::lround((centre - pitches[pitches.size() / 2]) / 12.0));
        for (auto& note : found) note.midi = std::clamp(note.midi + 12 * takeOctaves_, kLowest, kHighest);
        shift_ += 12 * takeOctaves_;
    }
    takeCount_ = found.size();
    notes_ = keptOutside_;
    notes_.insert(notes_.end(), found.begin(), found.end());
    if (final) {
        // Notes after where the take stopped are kept.
        for (PatternNote note : beforeTake_) {
            if (note.beat < takeEndBeat_ - kEps) continue;
            note.selected = false;
            notes_.push_back(note);
        }
    }
    SortAndTrim(notes_);
    edited_ = false;
    roll_->Refresh(false);
}

// ---------------------------------------------------------------------------------------------
// Editing

void SingWindow::PushUndo() {
    undo_.push_back(notes_);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void SingWindow::Undo() {
    if (undo_.empty() || IsBusy()) return;
    redo_.push_back(notes_);
    notes_ = undo_.back();
    undo_.pop_back();
    edited_ = true;
    Changed();
}

void SingWindow::Redo() {
    if (redo_.empty() || IsBusy()) return;
    undo_.push_back(notes_);
    notes_ = redo_.back();
    redo_.pop_back();
    edited_ = true;
    Changed();
}

void SingWindow::Changed() {
    UpdateButtons();
    roll_->Refresh(false);
}

void SingWindow::Transpose(int semitones) {
    if (IsBusy() || notes_.empty()) return;
    const bool any = std::any_of(notes_.begin(), notes_.end(), [](const PatternNote& n) { return n.selected; });
    PushUndo();
    for (auto& note : notes_)
        if (!any || note.selected) note.midi = std::clamp(note.midi + semitones, kLowest, kHighest);
    if (!any) shift_ += semitones;  // the trace follows when everything moves
    else edited_ = true;
    Changed();
}

void SingWindow::FitOctaveToStaff() {
    const Project& project = GetProject();
    if (notes_.empty() || project.staves.empty()) return;
    std::vector<int> pitches;
    for (const PatternNote& note : notes_) pitches.push_back(note.midi);
    std::nth_element(pitches.begin(), pitches.begin() + pitches.size() / 2, pitches.end());
    const int median = pitches[pitches.size() / 2];
    const int centre = StepToMidi(project.staves[Staff()].clef, 0, 0, 0);
    const int octaves = int(std::lround((centre - median) / 12.0));
    if (octaves == 0) {
        roll_->CentreOn(median);
        return;
    }
    PushUndo();
    for (auto& note : notes_) note.midi = std::clamp(note.midi + 12 * octaves, kLowest, kHighest);
    shift_ += 12 * octaves;
    roll_->CentreOn(median + 12 * octaves);
    Changed();
    SetStatus(wxString::Format("Moved %s %d octave%s to suit %s. Ctrl+Z undoes this.", octaves > 0 ? "up" : "down",
                               std::abs(octaves), std::abs(octaves) == 1 ? "" : "s", staff_->GetStringSelection()));
}

void SingWindow::Requantize() {
    if (IsBusy()) return;
    PushUndo();
    if (!edited_ && !sung_.empty()) {
        // Nothing has been changed by hand, so place the take again from what was sung.
        const Project& project = GetProject();
        const TempoClock clock(host_.tempoPoints());
        std::vector<PatternNote> found = QuantizeNotes(sung_, clock, takeStartBeat_, Grid(), project.settings.fifths,
                                                       keepKey_->GetValue(), shift_, &tuningCents_);
        notes_ = keptOutside_;
        notes_.insert(notes_.end(), found.begin(), found.end());
        SortAndTrim(notes_);
    } else {
        SnapNotes(notes_, Grid(), false);
    }
    Changed();
}

// ---------------------------------------------------------------------------------------------
// Output

bool SingWindow::BuildBars(int& first, int& last, std::vector<Bar>& bars, std::vector<wxString>& texts,
                           wxString& error) const {
    const Project& project = GetProject();
    if (notes_.empty()) {
        error = "There are no notes yet.";
        return false;
    }
    double low = 1e9, high = 0;
    for (const PatternNote& note : notes_) {
        low = std::min(low, note.beat);
        high = std::max(high, note.beat + note.length);
    }
    if (low >= project.TotalBeats() - kEps) {
        error = "The notes are after the end of the piece. Add bars in Project Settings first.";
        return false;
    }
    const int staff = Staff();
    first = project.BarAtBeat(low);
    last = std::min(project.settings.bars - 1, project.BarAtBeat(std::max(low, high - kEps)));
    texts = PatternToBarTexts(project, notes_, first, last);
    bars.clear();
    for (int bar = first; bar <= last; ++bar) {
        Bar out;
        wxString why;
        if (!ParseBarText(project, staff, bar, texts[bar - first], out, &why)) {
            error = wxString::Format("Bar %d can't be written on %s: %s. Try moving the notes by an octave.", bar + 1,
                                     StaffLabel(project, staff), why);
            return false;
        }
        bars.push_back(std::move(out));
    }
    return true;
}

void SingWindow::InsertIntoScore() {
    int first = 0, last = 0;
    std::vector<Bar> bars;
    std::vector<wxString> texts;
    wxString error;
    if (!BuildBars(first, last, bars, texts, error)) {
        SetStatus(error);
        return;
    }
    const Project& project = GetProject();
    const int staff = Staff();
    bool occupied = false;
    for (int bar = first; bar <= last; ++bar)
        for (const Event& ev : project.score[staff][bar]) occupied = occupied || !ev.rest;
    const wxString where = first == last ? wxString::Format("bar %d", first + 1)
                                         : wxString::Format("bars %d-%d", first + 1, last + 1);
    if (occupied && wxMessageBox(StaffLabel(project, staff) + " already has notes in " + where +
                                     ". Replace them with the sung melody?\n\nCtrl+Z in the score undoes this.",
                                 "Insert into Score", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES)
        return;

    Proposal proposal;
    proposal.title = "Sung melody";
    proposal.meterKey = MeterKey(project);
    for (int bar = first; bar <= last; ++bar)
        proposal.edits.push_back({staff, bar, bars[bar - first], project.score[staff][bar]});
    host_.insert(proposal);
    SetStatus("Inserted into " + StaffLabel(project, staff) + ", " + where + ". Ctrl+Z in the score undoes it.");
}

void SingWindow::DevelopWithDirector() {
    int first = 0, last = 0;
    std::vector<Bar> bars;
    std::vector<wxString> texts;
    wxString error;
    if (!BuildBars(first, last, bars, texts, error)) {
        SetStatus(error);
        return;
    }
    const Project& project = GetProject();
    const wxString staff = StaffLabel(project, Staff());
    const wxString where = first == last ? wxString::Format("bar %d", first + 1)
                                         : wxString::Format("bars %d-%d", first + 1, last + 1);

    wxString context = "Sung melody: the user sang this into the microphone and tidied it in the pattern editor. "
                       "It is meant for " + staff + " in " + where + " and is not in the score yet.\n";
    for (int bar = first; bar <= last; ++bar)
        context += wxString::Format("  Bar %d: ", bar + 1) + texts[bar - first] + "\n";
    context += "Treat it as the user's own idea: keep its contour and rhythm recognisable, and include it on " + staff +
               " in your proposal along with whatever you build around it, unless the user asks otherwise.";

    wxString ideas = wxString(ideas_->GetValue()).Trim().Trim(false);
    if (ideas.empty()) ideas = "Develop this into a fuller passage that fits the piece.";
    const wxString message = "Sung melody for " + staff + ", " + where + ": " + ideas;
    if (!host_.develop(message, context)) {
        SetStatus("The Director is busy. Try again when it has finished replying.");
        return;
    }
    ideas_->Clear();
    SetStatus("Sent to the Director. Its proposal will appear in the Director panel for you to review.");
}
