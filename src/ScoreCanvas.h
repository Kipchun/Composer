// The engraved pages: draws the project's score and handles selection and note input.
#pragma once

#include <wx/scrolwin.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "Project.h"
#include "Theme.h"
#include "audio/PlayNote.h"

enum class ProposalState { None, Preview, Applied };

class ScoreCanvas : public wxScrolledCanvas {
public:
    explicit ScoreCanvas(wxWindow* parent);

    void SetProject(const Project& project);
    // The document itself. During a preview this is what's saved and sent to the Director.
    const Project& GetProject() const { return previewBase_ ? *previewBase_ : project_; }
    // What's on screen and playing, which includes any previewed proposal.
    const Project& DisplayProject() const { return project_; }
    // Leaves a preview so an edit can go ahead; onPreviewEnded tells the Director.
    void EndPreview();
    std::function<void()> onPreviewEnded;
    // Changes tempo, key, meter and so on; notes that no longer fit are removed.
    void ApplySettings(const ProjectSettings& settings);
    bool WouldLoseNotes(const ProjectSettings& settings) const;
    // A time signature from `bar` until the next change. Undoable.
    void SetMeterChange(int bar, int top, int bottom);
    bool WouldMeterLoseNotes(int bar, int top, int bottom) const;
    // A tempo from `bar` until the next change; remove clears one. Undoable.
    void SetTempoChange(int bar, int bpm, const wxString& text, bool remove);
    // A gradual change across bars; empty text with remove clears any overlapping one.
    void SetTempoRamp(int firstBar, int lastBar, int endBpm, const wxString& text, bool remove);

    // Off-screen drawing for export.
    int PageCount() const { return pageCount_; }
    wxImage RenderPage(int page, int pixelWidth);

    int BarCount() const { return project_.settings.bars; }
    int StaffCount() const { return int(project_.staves.size()); }
    double BarBeats(int bar) const { return project_.BarBeats(bar); }
    double TotalBeats() const { return project_.TotalBeats(); }
    std::vector<double> ClickBeats() const;
    std::vector<std::pair<double, double>> TempoPoints() const;  // quarter-note position -> bpm
    wxString StaffName(int staff) const;

    // Preview draws and plays the proposal without changing the score.
    void ShowProposal(const Proposal& proposal);
    void HideProposal();
    void ApplyProposal(const Proposal& proposal);  // one undo step
    bool IsProposalStale(const Proposal& proposal) const;
    bool IsProposalBar(int staff, int bar) const;  // shown in the current preview
    void SetPlayhead(double beats, bool visible);
    void SetSelection(const BarRange& range);
    const BarRange& GetSelection() const { return selection_; }
    void SetZoom(double zoom);
    double GetZoom() const { return zoom_; }

    std::function<void(const BarRange&)> onSelect;
    std::function<void(double zoom)> onZoom;

    // Note input mode. The value is what the next note will be written as.
    bool IsNoteInput() const { return noteInput_; }
    // In note input, the next note or click takes this mark. Pass 0 and empty to clear.
    void SetPendingMark(char articulation, const wxString& dynamic);
    // These act on the selected bars and return false if there's nothing to act on.
    bool ApplyDynamicToSelection(const wxString& dynamic);
    bool ApplyHairpinToSelection(bool crescendo);
    bool ToggleArticulationInSelection(char articulation);
    std::function<void()> onMarkUsed;
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

    std::function<void()> onChanged;
    std::function<void(int track, int midi)> onNotePreview;
    std::function<void()> onAccidentalUsed;
    std::function<void(const wxString&)> onInputStatus;

private:
    using Event = ::Event;

    struct Target {
        int bar = -1, staff = -1, step = 0;
        double beat = 0;
    };

    // Horizontal layout of one bar. Columns are shared by every staff.
    struct BarLayout {
        double x = 0, width = 0;
        std::vector<std::pair<double, double>> columns;  // beat -> x
    };

    struct System {
        int firstBar = 0, barCount = 0, page = 0;
        double top = 0;
    };

    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnWheel(wxMouseEvent&);
    void OnMotion(wxMouseEvent&);

    bool HitTest(const wxPoint& client, Target& out) const;
    bool HitBar(const wxPoint& client, int& bar, int& staff) const;
    bool ShowsMeter(int bar) const;
    void ClearPreview();
    void FitMarksToBars();
    void ClearPendingMark();
    void ToggleDynamic(int staff, int bar, double beat, const wxString& dynamic);
    const std::vector<Marking>& EffectiveMarkings() const { return project_.markings; }
    const std::vector<Hairpin>& EffectiveHairpins() const { return project_.hairpins; }
    bool ToPage(const wxPoint& client, double& x, double& y, int& page) const;
    double WrittenLength(int bar, double beat) const;
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

    void RefreshLayout();
    void LayoutPages();
    void UpdateVirtualSize();
    double PageX() const;
    double PageTop(int page) const;
    void ComputeLayout();
    double PrefixWidth(int system) const;
    double BarX(int bar) const { return layout_[bar].x; }
    double BarWidth(int bar) const { return layout_[bar].width; }
    int BarAtX(int system, double x) const;  // -1 if outside the system's bars
    double StaffTop(int system, int staff) const;
    double BeatX(int bar, double beat) const;
    int SystemOfBar(int bar) const { return bar / project_.settings.barsPerLine; }

    void DrawPage(wxGraphicsContext& gc, int page, double viewTop, double viewBottom);
    void DrawSystem(wxGraphicsContext& gc, int system);
    void DrawPrefix(wxGraphicsContext& gc, int system, int staff);
    void DrawEvents(wxGraphicsContext& gc, const std::vector<Event>& events, int bar, double top,
                    const wxColour& ink);
    void DrawHead(wxGraphicsContext& gc, double x, double y, double beats, const wxColour& ink);
    void DrawRest(wxGraphicsContext& gc, double x, double top, double beats, const wxColour& ink);

    const std::vector<Event>& EventsFor(int staff, int bar) const;

    Project project_;
    Score& score_ = project_.score;
    std::vector<BarLayout> layout_;
    std::vector<System> systems_;
    std::vector<double> staffOffset_;
    double systemH_ = 0, pageH_ = 0;
    int pageCount_ = 1;
    struct Snapshot {
        Score score;
        ProjectSettings settings;
        std::vector<MeterChange> meters;
        std::vector<TempoChange> tempos;
        std::vector<TempoRamp> ramps;
        std::vector<Section> sections;
        std::vector<Marking> markings;
        std::vector<Hairpin> hairpins;
        std::vector<Slur> slurs;
    };
    Snapshot Capture() const;
    void Restore(Snapshot snapshot);
    std::vector<Snapshot> undo_, redo_;
    std::map<std::pair<int, int>, Bar> preview_;  // (staff, bar) -> proposed contents
    std::unique_ptr<Project> previewBase_;  // the real document while project_ shows a preview

    double zoom_ = 0.8;
    double playhead_ = 0;
    bool playheadVisible_ = false;
    int followSystem_ = -1;
    BarRange selection_;
    int anchorBar_ = -1, anchorStaff_ = -1;
    bool dragging_ = false;
    char pendingArticulation_ = 0;
    wxString pendingDynamic_;

    bool printing_ = false;
    bool noteInput_ = false;
    double inputBeats_ = 1;
    bool inputRest_ = false;
    char inputAccidental_ = 0;
    Target caret_;        // step is where the caret's preview note sits
    Target hover_;        // valid when hover_.bar >= 0
    Target lastEntered_;  // the note arrow keys adjust; valid when bar >= 0
};
