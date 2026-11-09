// The engraved page: draws a hard-coded mock score and handles bar selection.
#pragma once

#include <wx/scrolwin.h>

#include <functional>
#include <vector>

#include "Theme.h"
#include "audio/PlayNote.h"

enum class ProposalState { None, Preview, Applied };

class ScoreCanvas : public wxScrolledCanvas {
public:
    static constexpr int kBars = 8;
    static constexpr int kBeatsPerBar = 4;

    explicit ScoreCanvas(wxWindow* parent);

    void SetProposal(ProposalState state);
    void SetPlayhead(double beats, bool visible);
    void SetSelection(int bar, int staff);  // staff -1 selects the whole bar
    void SetZoom(double zoom);
    double GetZoom() const { return zoom_; }

    std::function<void(int bar, int staff)> onSelect;
    std::function<void(double zoom)> onZoom;

    static wxString StaffName(int staff);

    // Note input mode. The value is what the next note will be written as.
    bool IsNoteInput() const { return noteInput_; }
    void SetNoteInput(bool on);
    void SetInputValue(double beats, bool rest, char accidental);
    void TypePitch(char letter);
    void TypeRest();
    void MoveCaret(int direction);
    void MoveStaff(int direction);
    void ChangePitch(int steps);
    void DeleteBack();
    void DeleteAtCaret();
    void Undo();
    void Redo();

    double FilledFraction(int staff, int bar) const;
    std::vector<PlayNote> BuildPlayback() const;
    static double TotalBeats() { return kBars * kBeatsPerBar; }

    std::function<void()> onChanged;
    std::function<void(int staff, int midi)> onNotePreview;
    std::function<void()> onAccidentalUsed;
    std::function<void(const wxString&)> onInputStatus;

private:
    struct Event {
        double beats = 1;
        bool dotted = false;
        bool rest = false;
        std::vector<int> steps;  // diatonic steps from the staff's middle line
        std::vector<char> accidentals;  // parallel to steps; 0 means none
    };

    // Horizontal layout of one bar. Columns are shared by every staff.
    struct BarLayout {
        double x = 0, width = 0;
        std::vector<std::pair<double, double>> columns;  // beat -> x
    };

    using Bar = std::vector<Event>;
    using Score = std::vector<std::vector<Bar>>;  // staff -> bar -> events

    struct Target {
        int bar = -1, staff = -1, step = 0;
        double beat = 0;
    };

    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnWheel(wxMouseEvent&);
    void OnMotion(wxMouseEvent&);

    bool HitTest(const wxPoint& client, Target& out) const;
    double WrittenLength(double beat) const;
    bool CanEdit(int staff, int bar);
    void PushUndo();
    void WriteRange(int staff, int bar, double start, double length, const Event* note);
    void ClickNote(const Target& t);
    void WriteAtCaret(const Event* note, int step);
    void AdvanceCaret(double beats);
    void PublishCaret();
    void EnsureCaretVisible();
    void Changed();
    void PreviewNote(int staff, int bar, double beat, int step);
    void DrawGhost(wxGraphicsContext& gc, double x, double top, int step, double beats);
    void DrawInputBanner(wxGraphicsContext& gc);

    void UpdateVirtualSize();
    double PageX() const;
    void ComputeLayout();
    double BarX(int bar) const { return layout_[bar].x; }
    double BarWidth(int bar) const { return layout_[bar].width; }
    int BarAtX(int system, double x) const;  // -1 if outside the system's bars
    double StaffTop(int system, int staff) const;
    double BeatX(int bar, double beat) const;

    void DrawPage(wxGraphicsContext& gc);
    void DrawSystem(wxGraphicsContext& gc, int system);
    void DrawPrefix(wxGraphicsContext& gc, int system, int staff);
    void DrawEvents(wxGraphicsContext& gc, const std::vector<Event>& events, int bar, double top,
                    const wxColour& ink);
    void DrawHead(wxGraphicsContext& gc, double x, double y, double beats, const wxColour& ink);
    void DrawRest(wxGraphicsContext& gc, double x, double top, double beats, const wxColour& ink);

    const std::vector<Event>& EventsFor(int staff, int bar) const;

    Score score_;
    std::vector<BarLayout> layout_;
    std::vector<Score> undo_, redo_;
    std::vector<std::vector<Event>> proposal_;            // Violin I, bars 5-6

    ProposalState proposalState_ = ProposalState::None;
    double zoom_ = 0.8;
    double playhead_ = 0;
    bool playheadVisible_ = false;
    int selectedBar_ = -1;
    int selectedStaff_ = -1;

    bool noteInput_ = false;
    double inputBeats_ = 1;
    bool inputRest_ = false;
    char inputAccidental_ = 0;
    Target caret_;        // step is where the caret's preview note sits
    Target hover_;        // valid when hover_.bar >= 0
    Target lastEntered_;  // the note arrow keys adjust; valid when bar >= 0
};
