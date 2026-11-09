// The engraved page: draws a hard-coded mock score and handles bar selection.
#pragma once

#include <wx/scrolwin.h>

#include <functional>
#include <vector>

#include "Theme.h"

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

private:
    struct Event {
        double beats = 1;
        bool dotted = false;
        bool rest = false;
        char accidental = 0;
        std::vector<int> steps;  // diatonic steps from the staff's middle line
    };

    void OnPaint(wxPaintEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnWheel(wxMouseEvent&);

    void UpdateVirtualSize();
    double PageX() const;
    double BarX(int system, int barInSystem) const;
    double BarWidth(int system) const;
    double StaffTop(int system, int staff) const;
    double BeatX(double barX, double barW, double beat) const;

    void DrawPage(wxGraphicsContext& gc);
    void DrawSystem(wxGraphicsContext& gc, int system);
    void DrawPrefix(wxGraphicsContext& gc, int system, int staff);
    void DrawEvents(wxGraphicsContext& gc, const std::vector<Event>& events, double barX,
                    double barW, double top, const wxColour& ink);
    void DrawHead(wxGraphicsContext& gc, double x, double y, double beats, const wxColour& ink);
    void DrawRest(wxGraphicsContext& gc, double x, double top, double beats, const wxColour& ink);

    const std::vector<Event>& EventsFor(int staff, int bar) const;

    std::vector<std::vector<std::vector<Event>>> score_;  // staff -> bar -> events
    std::vector<std::vector<Event>> proposal_;            // Violin I, bars 5-6

    ProposalState proposalState_ = ProposalState::None;
    double zoom_ = 0.8;
    double playhead_ = 0;
    bool playheadVisible_ = false;
    int selectedBar_ = -1;
    int selectedStaff_ = -1;
};
