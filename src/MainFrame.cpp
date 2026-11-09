#include "MainFrame.h"

using namespace ui;

namespace {
constexpr double kTempo = 96;
enum {
    ID_ViewPalette = wxID_HIGHEST + 1,
    ID_ViewTimeline,
    ID_ViewDirector,
};
const char* const kPaneDirector = "director";
const char* const kPanePalette = "palette";
const char* const kPaneTimeline = "timeline";
}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Composer - Nocturne in Blue", wxDefaultPosition, wxSize(1560, 980)),
      timer_(this) {
    SetBackgroundColour(kBg);
    BuildMenus();
    BuildPanes();
    Connect();
    CreateStatusBar(4);
    const int widths[4] = {-1, 150, 180, 240};
    SetStatusWidths(4, widths);
    SetStatusText("Ready: click a bar to set the Director's scope", 0);
    SetStatusText("E" + Glyph(0x266D) + " major  " + Glyph(0x00B7) + "  4/4", 1);
    SetStatusText("8 bars  " + Glyph(0x00B7) + "  6 staves", 2);
    SetStatusText("Concept UI: no audio or engine", 3);
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
    notes->Append(wxID_ANY, "Note &Input Mode\tN");
    notes->Append(wxID_ANY, "&Transpose...");

    auto* score = new wxMenu;
    score->Append(wxID_ANY, "&Instruments...\tI");
    score->Append(wxID_ANY, "&Key Signature...");
    score->Append(wxID_ANY, "&Time Signature...");

    auto* playback = new wxMenu;
    playback->Append(wxID_ANY, "&Play / Pause\tSpace");
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
    timeline_->onSelect = [this](int bar, int staff) { Select(bar, staff); };
    score_->onZoom = [this](double z) { transport_->SetZoomPercent(int(z * 100 + 0.5)); };
    transport_->onAction = [this](TransportBar::Action a) { OnTransport(a); };
    palette_->onTool = [this](const wxString& tool) { SetStatusText("Tool: " + tool, 0); };
    director_->onProposal = [this](ProposalState state) {
        score_->SetProposal(state);
        timeline_->SetProposal(state);
        SetStatusText(state == ProposalState::Preview   ? "Previewing Director proposal"
                      : state == ProposalState::Applied ? "Director proposal applied"
                                                        : "Showing the original score",
                      0);
    };
    Bind(wxEVT_TIMER, &MainFrame::OnTick, this);
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
                clock_.Start();
                timer_.Start(30);
            } else {
                timer_.Stop();
            }
            break;
        case TransportBar::Stop:
        case TransportBar::Rewind: {
            const bool stop = action == TransportBar::Stop;
            if (stop) {
                playing_ = false;
                showHead_ = false;
                timer_.Stop();
                transport_->SetPlaying(false);
            }
            beats_ = 0;
            clock_.Start();
            Publish();
            break;
        }
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
    beats_ += clock_.Time() / 1000.0 * kTempo / 60.0;
    clock_.Start();
    const double total = ScoreCanvas::kBars * ScoreCanvas::kBeatsPerBar;
    if (beats_ >= total) {
        if (transport_->IsLooping()) {
            beats_ -= total;
        } else {
            OnTransport(TransportBar::Stop);
            return;
        }
    }
    Publish();
}

void MainFrame::Publish() {
    score_->SetPlayhead(beats_, showHead_);
    timeline_->SetPlayhead(beats_, showHead_);
    const int beat = int(beats_);
    transport_->SetPosition(beat / ScoreCanvas::kBeatsPerBar + 1,
                            beat % ScoreCanvas::kBeatsPerBar + 1);
}
