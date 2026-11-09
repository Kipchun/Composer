#include "InstrumentsWindow.h"

#include <wx/sizer.h>

#include <algorithm>
#include <cmath>

#include "Theme.h"

using namespace ui;

InstrumentsWindow::InstrumentsWindow(wxWindow* parent, AudioEngine* engine)
    : wxFrame(parent, wxID_ANY, "Instruments", wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT | wxFRAME_TOOL_WINDOW),
      engine_(engine),
      plugins_(ScanVst3Plugins()),
      meter_(this) {
    auto* panel = new wxPanel(this);
    panel->SetBackgroundColour(kPanel);

    wxArrayString names;
    names.Add("Placeholder sound");
    for (const PluginInfo& plugin : plugins_) names.Add(plugin.name);

    auto* grid = new wxFlexGridSizer(4, 8, 12);
    grid->AddGrowableCol(1, 1);
    for (const char* heading : {"Staff", "Sound", "", "Level"}) {
        auto* label = new wxStaticText(panel, wxID_ANY, heading);
        label->SetForegroundColour(kMuted);
        grid->Add(label);
    }

    for (int t = 0; t < kTracks; ++t) {
        Row& row = rows_[t];
        auto* label = new wxStaticText(panel, wxID_ANY, ScoreCanvas::StaffName(t));
        label->SetForegroundColour(kText);
        row.choice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxSize(240, -1), names);
        row.choice->SetSelection(0);
        row.edit = new wxButton(panel, wxID_ANY, "Open plugin");
        row.edit->Disable();
        row.level = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(90, 12));

        grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(row.choice, 1, wxEXPAND);
        grid->Add(row.edit);
        grid->Add(row.level, 0, wxALIGN_CENTER_VERTICAL);

        row.choice->Bind(wxEVT_CHOICE, [this, t](wxCommandEvent&) { OnChoice(t); });
        row.edit->Bind(wxEVT_BUTTON, [this, t](wxCommandEvent&) { OpenEditor(t); });
    }

    status_ = new wxStaticText(panel, wxID_ANY,
                               plugins_.empty()
                                   ? "No VST3 plugins found, so every staff uses the placeholder sound."
                                   : "After choosing a plugin, open it and load an instrument inside it.");
    status_->SetForegroundColour(kMuted);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND | wxALL, 16);
    sizer->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    panel->SetSizerAndFit(sizer);
    SetClientSize(panel->GetBestSize());

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        if (e.CanVeto()) {
            e.Veto();
            Hide();
            meter_.Stop();
        } else {
            e.Skip();
        }
    });
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        if (e.IsShown()) meter_.Start(60);
        e.Skip();
    });
    Bind(wxEVT_TIMER, &InstrumentsWindow::OnMeterTick, this);
}

void InstrumentsWindow::UsePlaceholder(int track) {
    Row& row = rows_[track];
    row.plugin = nullptr;
    row.selection = 0;
    row.choice->SetSelection(0);
    row.edit->Disable();
    engine_->SetInstrument(track, MakePlaceholder(track, engine_->SampleRate()));
}

void InstrumentsWindow::OnChoice(int track) {
    Row& row = rows_[track];
    const int selection = row.choice->GetSelection();
    if (selection == row.selection) return;

    if (selection == 0) {
        UsePlaceholder(track);
        status_->SetLabel(ScoreCanvas::StaffName(track) + " now uses the placeholder sound.");
        return;
    }

    const PluginInfo& plugin = plugins_[selection - 1];
    status_->SetLabel("Loading " + plugin.name + "...");
    status_->Update();

    wxString error;
    std::unique_ptr<Vst3Instrument> loaded;
    {
        wxBusyCursor busy;
        loaded = Vst3Instrument::Load(plugin, engine_->SampleRate(), AudioEngine::kBlock, &error);
    }
    if (!loaded) {
        UsePlaceholder(track);
        status_->SetLabel(error + ". Using the placeholder sound.");
        return;
    }

    row.plugin = loaded.get();
    row.selection = selection;
    row.edit->Enable();
    engine_->SetInstrument(track, std::move(loaded));  // the old instrument is freed here
    status_->SetLabel("Loaded " + plugin.name + ". Open it to load an instrument inside it.");
    OpenEditor(track);
}

void InstrumentsWindow::OpenEditor(int track) {
    Row& row = rows_[track];
    if (!row.plugin) return;
    const wxString title = ScoreCanvas::StaffName(track) + " - " + row.choice->GetStringSelection();
    if (!row.plugin->OpenEditor(GetParent(), title))
        status_->SetLabel("That plugin has no window to open.");
}

void InstrumentsWindow::OnMeterTick(wxTimerEvent&) {
    for (int t = 0; t < kTracks; ++t) {
        const float peak = engine_->TakePeak(t);
        rows_[t].level->SetValue(std::clamp(int(std::sqrt(peak) * 100), 0, 100));
    }
}
