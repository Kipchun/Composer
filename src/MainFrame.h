// Top-level window: hosts the dockable panes and connects them together.
#pragma once

#include <wx/aui/aui.h>
#include <wx/timer.h>

#include <memory>

#include "DirectorPanel.h"
#include "InstrumentsWindow.h"
#include "PalettePanel.h"
#include "ScoreCanvas.h"
#include "TimelinePanel.h"
#include "TransportBar.h"
#include "audio/AudioEngine.h"

class MainFrame : public wxFrame {
public:
    explicit MainFrame(const wxString& startupFile = wxEmptyString);
    ~MainFrame() override;

private:
    void BuildMenus();
    void BuildPanes();
    void Connect();
    void TogglePane(const wxString& name);
    void OnTransport(TransportBar::Action action);
    void OnTick(wxTimerEvent&);
    void Publish();
    void Select(const BarRange& range);
    void ChangeMeter();
    void ChangeTempo();
    void OnPaletteMark(char articulation, const wxString& dynamic, int hairpin);
    void OnCharHook(wxKeyEvent& event);
    void SyncInputValue();
    void PushSchedule();
    void ApplyProject(const Project& project, const wxString& path, bool keepConversation = false);
    void ChangeInstruments();
    void PushProjectInfo();
    void NewProject();
    void OpenProject();
    void OpenProjectFile(const wxString& path);
    bool SaveProject(bool chooseLocation);
    bool ConfirmDiscard();
    void EditSettings();
    void SetModified(bool modified);
    void UpdateTitle();
    void StopPlayback();
    void ShowInstruments();
    void OnClose(wxCloseEvent& event);

    wxAuiManager aui_;
    TransportBar* transport_ = nullptr;
    PalettePanel* palette_ = nullptr;
    ScoreCanvas* score_ = nullptr;
    TimelinePanel* timeline_ = nullptr;
    DirectorPanel* director_ = nullptr;
    InstrumentsWindow* instruments_ = nullptr;
    std::unique_ptr<AudioEngine> engine_;
    wxString path_;
    bool modified_ = false;

    wxTimer timer_;
    double beats_ = 0;
    bool playing_ = false;
    bool showHead_ = false;
};
