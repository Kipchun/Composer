// Top-level window: hosts the dockable panes and connects them together.
#pragma once

#include <wx/aui/aui.h>
#include <wx/stopwatch.h>
#include <wx/timer.h>

#include "DirectorPanel.h"
#include "PalettePanel.h"
#include "ScoreCanvas.h"
#include "TimelinePanel.h"
#include "TransportBar.h"

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void BuildMenus();
    void BuildPanes();
    void Connect();
    void TogglePane(const wxString& name);
    void OnTransport(TransportBar::Action action);
    void OnTick(wxTimerEvent&);
    void Publish();
    void Select(int bar, int staff);

    wxAuiManager aui_;
    TransportBar* transport_ = nullptr;
    PalettePanel* palette_ = nullptr;
    ScoreCanvas* score_ = nullptr;
    TimelinePanel* timeline_ = nullptr;
    DirectorPanel* director_ = nullptr;

    wxTimer timer_;
    wxStopWatch clock_;
    double beats_ = 0;
    bool playing_ = false;
    bool showHead_ = false;
};
