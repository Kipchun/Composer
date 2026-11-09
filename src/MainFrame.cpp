#include "MainFrame.h"

using namespace ui;

namespace {
enum {
    ID_ViewPalette = wxID_HIGHEST + 1,
    ID_ViewTimeline,
    ID_ViewDirector,
    ID_NoteInput,
    ID_Instruments,
};
const char* const kPaneDirector = "director";
const char* const kPanePalette = "palette";
const char* const kPaneTimeline = "timeline";
}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Composer - Nocturne in Blue", wxDefaultPosition, wxSize(1560, 980)),
      timer_(this) {
    SetBackgroundColour(kBg);
    engine_ = std::make_unique<AudioEngine>(InstrumentsWindow::kTracks);
    for (int t = 0; t < InstrumentsWindow::kTracks; ++t)
        engine_->SetInstrument(t, MakePlaceholder(t, engine_->SampleRate()));

    BuildMenus();
    BuildPanes();
    Connect();
    instruments_ = new InstrumentsWindow(this, engine_.get());
    PushSchedule();
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    CreateStatusBar(4);
    const int widths[4] = {-1, 150, 180, 240};
    SetStatusWidths(4, widths);
    SetStatusText("Ready: click a bar to set the Director's scope", 0);
    SetStatusText("E" + Glyph(0x266D) + " major  " + Glyph(0x00B7) + "  4/4", 1);
    SetStatusText("8 bars  " + Glyph(0x00B7) + "  6 staves", 2);
    SetStatusText(engine_->HasDevice()
                      ? wxString::Format("Audio: %.1f kHz", engine_->SampleRate() / 1000)
                      : wxString("Audio: no output device"),
                  3);
    Bind(wxEVT_CHAR_HOOK, &MainFrame::OnCharHook, this);
    CallAfter([this] { score_->SetFocus(); });  // otherwise the Director's text box takes it
    Centre();
}

MainFrame::~MainFrame() { aui_.UnInit(); }

void MainFrame::BuildMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New Score\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
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
    score->Append(ID_Instruments, "&Instruments... (I)");
    score->Append(wxID_ANY, "&Key Signature...");
    score->Append(wxID_ANY, "&Time Signature...");

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
        director_->SetScope(bar, staff);
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
        timeline_->Refresh();
        PushSchedule();
    };
    score_->onNotePreview = [this](int staff, int midi) { engine_->Preview(staff, midi); };
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
    engine_->SetSchedule(score_->BuildPlayback(), ScoreCanvas::TotalBeats());
}

void MainFrame::ShowInstruments() {
    instruments_->Show();
    instruments_->Raise();
}

void MainFrame::OnClose(wxCloseEvent& event) {
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
    director_->SetScope(bar, staff);
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
    const int beat = int(beats_);
    transport_->SetPosition(beat / ScoreCanvas::kBeatsPerBar + 1,
                            beat % ScoreCanvas::kBeatsPerBar + 1);
}
