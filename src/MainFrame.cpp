#include "MainFrame.h"

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/progdlg.h>

#include <set>

#include "ProjectDialogs.h"

using namespace ui;

namespace {
enum {
    ID_ViewPalette = wxID_HIGHEST + 1,
    ID_ViewTimeline,
    ID_ViewDirector,
    ID_NoteInput,
    ID_Instruments,
    ID_ProjectSettings,
    ID_ChangeInstruments,
};
const char* const kFileFilter = "Composer projects (*.composer)|*.composer";
const char* const kPaneDirector = "director";
const char* const kPanePalette = "palette";
const char* const kPaneTimeline = "timeline";
}  // namespace

MainFrame::MainFrame(const wxString& startupFile)
    : wxFrame(nullptr, wxID_ANY, "Composer - Nocturne in Blue", wxDefaultPosition, wxSize(1560, 980)),
      timer_(this) {
    SetBackgroundColour(kBg);
    BuildMenus();
    BuildPanes();
    Connect();
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    CreateStatusBar(4);
    const int widths[4] = {-1, 150, 180, 240};
    SetStatusWidths(4, widths);
    SetStatusText("Ready: click a bar to set the Director's scope", 0);
    ApplyProject(score_->GetProject(), wxEmptyString);
    if (!startupFile.empty()) OpenProjectFile(startupFile);
    Bind(wxEVT_CHAR_HOOK, &MainFrame::OnCharHook, this);
    CallAfter([this] { score_->SetFocus(); });  // otherwise the Director's text box takes it
    Centre();
}

MainFrame::~MainFrame() { aui_.UnInit(); }

void MainFrame::BuildMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New Project...\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &As...\tCtrl+Shift+S");
    file->Append(wxID_ANY, "&Export...");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");

    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");

    auto* view = new wxMenu;
    view->AppendCheckItem(ID_ViewPalette, "&Palette");
    view->AppendCheckItem(ID_ViewTimeline, "&Timeline");
    view->AppendCheckItem(ID_ViewDirector, "&Director\tCtrl+D");

    auto* notes = new wxMenu;
    // Plain-letter shortcuts are handled in OnCharHook so they can't steal typing
    // from the Director's text box, which is why these labels aren't accelerators.
    notes->AppendCheckItem(ID_NoteInput, "Note &Input Mode (N)");
    notes->Append(wxID_ANY, "&Transpose...");

    auto* score = new wxMenu;
    score->Append(ID_ChangeInstruments, "Add or Remove &Instruments...\tCtrl+Shift+I");
    score->Append(ID_Instruments, "Instrument S&ounds... (I)");
    score->Append(ID_ProjectSettings, "Project &Settings...\tCtrl+Shift+P");

    auto* playback = new wxMenu;
    playback->Append(wxID_ANY, "&Play / Pause (Space)");
    playback->Append(wxID_ANY, "&Mixer...");

    auto* director = new wxMenu;
    director->Append(wxID_ANY, "&New Conversation");
    director->Append(wxID_ANY, "Change &History...");
    director->Append(wxID_ANY, "&Settings...");

    auto* help = new wxMenu;
    help->Append(wxID_ABOUT, "&About Composer");

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(edit, "&Edit");
    bar->Append(view, "&View");
    bar->Append(notes, "&Notes");
    bar->Append(score, "&Score");
    bar->Append(playback, "&Playback");
    bar->Append(director, "&Director");
    bar->Append(help, "&Help");
    SetMenuBar(bar);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowInstruments(); }, ID_Instruments);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { EditSettings(); }, ID_ProjectSettings);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ChangeInstruments(); }, ID_ChangeInstruments);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewProject(); }, wxID_NEW);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenProject(); }, wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveProject(false); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveProject(true); }, wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { score_->SetNoteInput(!score_->IsNoteInput()); }, ID_NoteInput);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& e) { e.Check(score_->IsNoteInput()); }, ID_NoteInput);
    const auto textFocus = [] { return dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus()); };
    Bind(wxEVT_MENU, [this, textFocus](wxCommandEvent&) {
        if (auto* text = textFocus()) text->Undo();
        else score_->Undo();
    }, wxID_UNDO);
    Bind(wxEVT_MENU, [this, textFocus](wxCommandEvent&) {
        if (auto* text = textFocus()) text->Redo();
        else score_->Redo();
    }, wxID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { TogglePane(kPanePalette); }, ID_ViewPalette);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { TogglePane(kPaneTimeline); }, ID_ViewTimeline);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { TogglePane(kPaneDirector); }, ID_ViewDirector);

    const auto syncCheck = [this](wxUpdateUIEvent& e, const char* pane) {
        e.Check(aui_.GetPane(pane).IsShown());
    };
    Bind(wxEVT_UPDATE_UI, [=](wxUpdateUIEvent& e) { syncCheck(e, kPanePalette); }, ID_ViewPalette);
    Bind(wxEVT_UPDATE_UI, [=](wxUpdateUIEvent& e) { syncCheck(e, kPaneTimeline); }, ID_ViewTimeline);
    Bind(wxEVT_UPDATE_UI, [=](wxUpdateUIEvent& e) { syncCheck(e, kPaneDirector); }, ID_ViewDirector);
}

void MainFrame::BuildPanes() {
    aui_.SetManagedWindow(this);
    aui_.SetFlags(wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_LIVE_RESIZE | wxAUI_MGR_TRANSPARENT_HINT);

    wxAuiDockArt* art = aui_.GetArtProvider();
    art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, kBg);
    art->SetColour(wxAUI_DOCKART_SASH_COLOUR, kBg);
    art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, kBorder);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, kPanelAlt);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, kPanelAlt);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, kText);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, kPanelAlt);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, kPanelAlt);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, kText);
    art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
    art->SetMetric(wxAUI_DOCKART_SASH_SIZE, 4);
    art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);

    transport_ = new TransportBar(this);
    palette_ = new PalettePanel(this);
    score_ = new ScoreCanvas(this);
    timeline_ = new TimelinePanel(this);
    director_ = new DirectorPanel(this);

    aui_.AddPane(transport_, wxAuiPaneInfo()
                                 .Name("transport")
                                 .Top()
                                 .CaptionVisible(false)
                                 .PaneBorder(false)
                                 .Floatable(false)
                                 .Movable(false)
                                 .Gripper(false)
                                 .Resizable(false)
                                 .BestSize(-1, 58)
                                 .MinSize(-1, 58));
    aui_.AddPane(palette_, wxAuiPaneInfo()
                               .Name(kPanePalette)
                               .Caption("Palette")
                               .Left()
                               .BestSize(208, -1)
                               .MinSize(112, 200));
    aui_.AddPane(score_, wxAuiPaneInfo().Name("score").CenterPane());
    aui_.AddPane(director_, wxAuiPaneInfo()
                                .Name(kPaneDirector)
                                .Caption("Director")
                                .Right()
                                .BestSize(400, -1)
                                .MinSize(340, 300)
                                .FloatingSize(420, 680));
    aui_.AddPane(timeline_, wxAuiPaneInfo()
                                .Name(kPaneTimeline)
                                .Caption("Timeline")
                                .Bottom()
                                .BestSize(-1, 172)
                                .MinSize(300, 172));
    aui_.Update();
}

void MainFrame::Connect() {
    score_->onSelect = [this](int bar, int staff) {
        timeline_->SetSelection(bar, staff);
        director_->SetScope(bar, staff >= 0 ? score_->StaffName(staff) : wxString());
        if (bar >= 0) SetStatusText(wxString::Format("Selected bar %d", bar + 1), 0);
        else SetStatusText("Selection cleared", 0);
    };
    timeline_->onSelect = [this](int bar, int staff) {
        score_->SetFocus();
        Select(bar, staff);
    };
    score_->onZoom = [this](double z) { transport_->SetZoomPercent(int(z * 100 + 0.5)); };
    transport_->onAction = [this](TransportBar::Action a) {
        score_->SetFocus();
        OnTransport(a);
    };
    palette_->onTool = [this](const wxString& tool) {
        score_->SetFocus();
        SetStatusText("Tool: " + tool, 0);
    };
    palette_->onNoteInputChanged = [this] { SyncInputValue(); };
    score_->onAccidentalUsed = [this] { palette_->ClearAccidental(); };
    score_->onChanged = [this] {
        timeline_->ScoreChanged();
        PushSchedule();
        SetModified(true);
    };
    score_->onNotePreview = [this](int track, int midi) { engine_->Preview(track, midi); };
    score_->onInputStatus = [this](const wxString& text) { SetStatusText(text, 0); };
    timeline_->SetScore(score_);
    SyncInputValue();
    director_->onProposal = [this](ProposalState state) {
        score_->SetProposal(state);
        timeline_->SetProposal(state);
        PushSchedule();
        SetStatusText(state == ProposalState::Preview   ? "Previewing Director proposal"
                      : state == ProposalState::Applied ? "Director proposal applied"
                                                        : "Showing the original score",
                      0);
    };
    Bind(wxEVT_TIMER, &MainFrame::OnTick, this);
}

void MainFrame::PushSchedule() {
    engine_->SetSchedule(score_->BuildPlayback(), score_->TotalBeats());
}

void MainFrame::StopPlayback() {
    OnTransport(TransportBar::Stop);
}

void MainFrame::PushProjectInfo() {
    const ProjectSettings& s = score_->GetProject().settings;
    const wxString key = KeyLabel(s.fifths, s.minor);
    const wxString time = wxString::Format("%d/%d", s.timeTop, s.timeBottom);
    transport_->SetProjectInfo(s.tempo, key, time);
    SetStatusText(key + "  " + Glyph(0x00B7) + "  " + time, 1);
    SetStatusText(wxString::Format("%d bars  ", s.bars) + Glyph(0x00B7) +
                      wxString::Format("  %d staves", score_->StaffCount()),
                  2);
    engine_->SetTempo(s.tempo);
    engine_->SetClickUnit(s.BeatUnit());
    PushSchedule();
}

// Swaps in a whole new document: fresh audio tracks, instruments and conversation.
void MainFrame::ApplyProject(const Project& project, const wxString& path, bool keepConversation) {
    timer_.Stop();
    playing_ = false;
    showHead_ = false;
    beats_ = 0;
    transport_->SetPlaying(false);
    transport_->SetPosition(1, 1);

    // The old engine owns plugins whose windows must close before the list goes away.
    engine_.reset();
    if (instruments_) instruments_->Destroy();

    if (&project != &score_->GetProject()) score_->SetProject(project);
    const Project& current = score_->GetProject();

    engine_ = std::make_unique<AudioEngine>(int(current.tracks.size()));
    for (int t = 0; t < int(current.tracks.size()); ++t)
        engine_->SetInstrument(t, MakePlaceholder(current.tracks[t].timbre, engine_->SampleRate()));
    engine_->SetLoop(transport_->IsLooping());
    engine_->SetClick(transport_->IsClickOn());

    instruments_ = new InstrumentsWindow(this, engine_.get(), current.tracks);
    instruments_->onChanged = [this] { SetModified(true); };

    wxString missing;
    if (!current.plugins.empty()) {
        wxBusyCursor busy;
        wxProgressDialog progress("Loading instruments", "Starting...", int(current.plugins.size()), this,
                                  wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);
        int done = 0;
        for (const PluginSetup& setup : current.plugins) {
            progress.Update(done++, current.tracks[std::clamp(setup.track, 0, int(current.tracks.size()) - 1)].name);
            wxString error;
            if (!instruments_->RestorePlugin(setup, &error))
                missing += "\n" + wxString::FromUTF8(setup.path) + " (" + error + ")";
        }
        progress.Update(done);
    }

    timeline_->SetScore(score_);
    timeline_->SetSelection(-1, -1);
    timeline_->SetProposal(ProposalState::None);
    timeline_->SetPlayhead(0, false);
    if (!current.demo && !keepConversation) director_->ResetConversation(current.settings.title);

    path_ = path;
    PushProjectInfo();
    SetStatusText(engine_->HasDevice()
                      ? wxString::Format("Audio: %.1f kHz", engine_->SampleRate() / 1000)
                      : wxString("Audio: no output device"),
                  3);
    SetModified(false);
    Publish();

    if (!missing.empty())
        wxMessageBox("These plugins could not be restored, so their instruments use the placeholder sound:\n" + missing,
                     "Composer", wxOK | wxICON_WARNING, this);
}

void MainFrame::SetModified(bool modified) {
    modified_ = modified;
    UpdateTitle();
}

void MainFrame::UpdateTitle() {
    SetTitle("Composer - " + score_->GetProject().settings.title + (modified_ ? " *" : ""));
}

bool MainFrame::ConfirmDiscard() {
    if (!modified_) return true;
    const int answer = wxMessageBox("Save changes to " + score_->GetProject().settings.title + "?",
                                    "Composer", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) return false;
    return answer == wxNO || SaveProject(false);
}

void MainFrame::NewProject() {
    if (!ConfirmDiscard()) return;
    NewProjectDialog dialog(this);
    if (dialog.ShowModal() != wxID_OK) return;
    const Project created = dialog.BuildResult();
    ApplyProject(created, wxEmptyString);
    SetStatusText("Created a new project", 0);
    if (!created.plugins.empty()) ShowInstruments();  // so patches can be chosen
}

void MainFrame::OpenProject() {
    if (!ConfirmDiscard()) return;
    wxFileDialog dialog(this, "Open Project", "", "", kFileFilter, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) OpenProjectFile(dialog.GetPath());
}

void MainFrame::OpenProjectFile(const wxString& path) {
    Project loaded;
    wxString error;
    if (!LoadProjectFile(path, loaded, &error)) {
        wxMessageBox("Could not open the project.\n\n" + error, "Composer", wxOK | wxICON_ERROR, this);
        return;
    }
    ApplyProject(loaded, path);
    SetStatusText("Opened " + wxFileName(path).GetFullName(), 0);
}

bool MainFrame::SaveProject(bool chooseLocation) {
    wxString path = path_;
    if (chooseLocation || path.empty()) {
        wxFileDialog dialog(this, "Save Project", "", score_->GetProject().settings.title + ".composer",
                            kFileFilter, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return false;
        path = dialog.GetPath();
    }

    Project snapshot = score_->GetProject();
    snapshot.plugins = instruments_->GetPluginSetups();
    wxString error;
    if (!SaveProjectFile(path, snapshot, &error)) {
        wxMessageBox("The project was not saved.\n\n" + error, "Composer", wxOK | wxICON_ERROR, this);
        return false;
    }
    path_ = path;
    SetModified(false);
    SetStatusText("Saved " + wxFileName(path).GetFullName(), 0);
    return true;
}

void MainFrame::ChangeInstruments() {
    const Project& project = score_->GetProject();
    std::vector<ScorePart> current;
    if (!PartsFromProject(project, current)) {
        wxMessageBox("This project uses an instrument Composer doesn't recognise, so its list can't be edited.",
                     "Composer", wxOK | wxICON_WARNING, this);
        return;
    }

    ChangeInstrumentsDialog dialog(this, current);
    if (dialog.ShowModal() != wxID_OK) return;
    const std::vector<ScorePart> parts = dialog.GetParts();

    std::set<int> kept;
    bool added = false;
    for (const ScorePart& part : parts) {
        if (part.sourceTrack >= 0) kept.insert(part.sourceTrack);
        else added = true;
    }
    if (!added && kept.size() == project.tracks.size()) return;  // nothing changed

    // Only instruments with music on them need a warning.
    wxString losing;
    for (int staff = 0; staff < score_->StaffCount(); ++staff) {
        const int track = project.staves[staff].track;
        if (kept.count(track)) continue;
        bool hasNotes = false;
        for (int bar = 0; bar < score_->BarCount(); ++bar)
            hasNotes = hasNotes || score_->FilledFraction(staff, bar) > 0;
        const wxString name = project.tracks[track].name;
        if (hasNotes && !losing.Contains("\n" + name + "\n")) losing += "\n" + name + "\n";
    }
    if (!losing.empty()) {
        losing.Replace("\n\n", "\n");
        if (wxMessageBox("Removing these instruments deletes their notes, and this can't be undone:\n" + losing +
                             "\nContinue?",
                         "Add or Remove Instruments", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
            return;
    }

    const bool wasDemo = project.demo;
    const Project rebuilt = RebuildWithParts(project, parts, instruments_->GetPluginSetups());
    ApplyProject(rebuilt, path_, !wasDemo);
    SetModified(true);
    SetStatusText(added ? "Instruments updated. Press I to choose sounds for new ones." : "Instruments updated", 0);
}

void MainFrame::EditSettings() {
    ProjectSettingsDialog dialog(this, score_->GetProject().settings);
    if (dialog.ShowModal() != wxID_OK) return;

    const ProjectSettings next = dialog.Get();
    if (score_->WouldLoseNotes(next) &&
        wxMessageBox("These settings make bars shorter or remove bars, so some notes will be deleted. "
                     "This can't be undone.\n\nContinue?",
                     "Project Settings", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
        return;

    const bool wasDemo = score_->GetProject().demo;
    StopPlayback();
    score_->ApplySettings(next);
    if (wasDemo && !score_->GetProject().demo) director_->ResetConversation(next.title);
    PushProjectInfo();
    timeline_->SetSelection(-1, -1);
    SetModified(true);
}

void MainFrame::ShowInstruments() {
    instruments_->Show();
    instruments_->Raise();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (event.CanVeto() && !ConfirmDiscard()) {
        event.Veto();
        return;
    }
    timer_.Stop();
    engine_.reset();  // shuts plugins down while their windows still exist
    event.Skip();
}

void MainFrame::SyncInputValue() {
    const PalettePanel::NoteInput input = palette_->GetNoteInput();
    score_->SetInputValue(input.beats, input.rest, input.accidental);
}

void MainFrame::OnCharHook(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE && score_->IsNoteInput()) {
        score_->SetNoteInput(false);  // works even while typing to the Director
        return;
    }

    // Keys typed into a text box, or combined with Ctrl/Alt, are not commands.
    if (dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus()) || event.ControlDown() || event.AltDown()) {
        event.Skip();
        return;
    }

    const int key = event.GetKeyCode();
    if (key == WXK_SPACE) {
        OnTransport(TransportBar::Play);
        return;
    }
    if (!score_->IsNoteInput()) {
        if (key == 'N') score_->SetNoteInput(true);
        else if (key == 'I') ShowInstruments();
        else event.Skip();
        return;
    }

    if (key >= 'A' && key <= 'G') score_->TypePitch(char(key));
    else if (key >= '1' && key <= '5') palette_->SetDuration(key - '1');
    else if (key >= WXK_NUMPAD1 && key <= WXK_NUMPAD5) palette_->SetDuration(key - WXK_NUMPAD1);
    else if (key == 'N' || key == WXK_ESCAPE) score_->SetNoteInput(false);
    else if (key == '.' || key == WXK_NUMPAD_DECIMAL) palette_->ToggleDot();
    else if (key == 'R') score_->TypeRest();
    else if (key == '=') palette_->SetAccidental('#');
    else if (key == '-') palette_->SetAccidental('b');
    else if (key == '0') palette_->SetAccidental('n');
    else if (key == WXK_LEFT) score_->MoveCaret(-1);
    else if (key == WXK_RIGHT) score_->MoveCaret(1);
    else if (key == WXK_UP) score_->ChangePitch(event.ShiftDown() ? 7 : 1);
    else if (key == WXK_DOWN) score_->ChangePitch(event.ShiftDown() ? -7 : -1);
    else if (key == WXK_TAB) score_->MoveStaff(event.ShiftDown() ? -1 : 1);
    else if (key == WXK_BACK) score_->DeleteBack();
    else if (key == WXK_DELETE) score_->DeleteAtCaret();
    else event.Skip();
}

void MainFrame::Select(int bar, int staff) {
    score_->SetSelection(bar, staff);
    director_->SetScope(bar, staff >= 0 ? score_->StaffName(staff) : wxString());
}

void MainFrame::TogglePane(const wxString& name) {
    wxAuiPaneInfo& pane = aui_.GetPane(name);
    pane.Show(!pane.IsShown());
    aui_.Update();
}

void MainFrame::OnTransport(TransportBar::Action action) {
    switch (action) {
        case TransportBar::Play:
            playing_ = !playing_;
            showHead_ = true;
            transport_->SetPlaying(playing_);
            if (playing_) {
                engine_->Play();
                timer_.Start(30);
            } else {
                engine_->Pause();
                timer_.Stop();
            }
            break;
        case TransportBar::Stop:
            playing_ = false;
            showHead_ = false;
            engine_->Stop();
            timer_.Stop();
            transport_->SetPlaying(false);
            Publish();
            break;
        case TransportBar::Rewind:
            engine_->Seek(0);
            Publish();
            break;
        case TransportBar::OpenSettings: EditSettings(); break;
        case TransportBar::Loop: engine_->SetLoop(transport_->IsLooping()); break;
        case TransportBar::Click: engine_->SetClick(transport_->IsClickOn()); break;
        case TransportBar::ZoomIn: score_->SetZoom(score_->GetZoom() + 0.1); break;
        case TransportBar::ZoomOut: score_->SetZoom(score_->GetZoom() - 0.1); break;
        case TransportBar::ViewParts:
        case TransportBar::ViewRoll:
            SetStatusText("That view is a placeholder in this concept", 0);
            break;
        default:
            break;
    }
}

void MainFrame::OnTick(wxTimerEvent&) {
    // The engine stops itself at the end of the score unless looping.
    if (!engine_->IsPlaying()) {
        OnTransport(TransportBar::Stop);
        return;
    }
    Publish();
}

void MainFrame::Publish() {
    beats_ = engine_->Beats();
    score_->SetPlayhead(beats_, showHead_);
    timeline_->SetPlayhead(beats_, showHead_);
    const ProjectSettings& settings = score_->GetProject().settings;
    const int bar = int(beats_ / settings.BarBeats());
    const int beat = int((beats_ - bar * settings.BarBeats()) / settings.BeatUnit());
    transport_->SetPosition(bar + 1, beat + 1);
}
