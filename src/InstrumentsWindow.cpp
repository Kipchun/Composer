#include "InstrumentsWindow.h"

#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <algorithm>
#include <cmath>

#include "Theme.h"

using namespace ui;

InstrumentsWindow::InstrumentsWindow(wxWindow* parent, AudioEngine* engine,
                                     const std::vector<TrackDef>& tracks)
    : wxFrame(parent, wxID_ANY, "Instruments", wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT | wxFRAME_TOOL_WINDOW),
      engine_(engine),
      tracks_(tracks),
      plugins_(ScanVst3Plugins()),
      rows_(tracks.size()),
      meter_(this) {
    auto* root = new wxPanel(this);
    root->SetBackgroundColour(kPanel);

    auto* list = new wxScrolledWindow(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    list->SetBackgroundColour(kPanel);
    list->SetScrollRate(0, 16);

    wxArrayString names;
    names.Add("Placeholder sound");
    for (const PluginInfo& plugin : plugins_) names.Add(plugin.name);

    auto* grid = new wxFlexGridSizer(4, 8, 12);
    grid->AddGrowableCol(1, 1);
    for (const char* heading : {"Instrument", "Sound", "", "Level"}) {
        auto* label = new wxStaticText(list, wxID_ANY, heading);
        label->SetForegroundColour(kMuted);
        grid->Add(label);
    }

    for (int t = 0; t < int(tracks.size()); ++t) {
        Row& row = rows_[t];
        auto* label = new wxStaticText(list, wxID_ANY, tracks[t].name);
        label->SetForegroundColour(kText);
        row.choice = new wxChoice(list, wxID_ANY, wxDefaultPosition, wxSize(240, -1), names);
        row.choice->SetSelection(0);
        row.edit = new wxButton(list, wxID_ANY, "Open plugin");
        row.edit->Disable();
        row.level = new wxGauge(list, wxID_ANY, 100, wxDefaultPosition, wxSize(90, 12));

        grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(row.choice, 1, wxEXPAND);
        grid->Add(row.edit);
        grid->Add(row.level, 0, wxALIGN_CENTER_VERTICAL);

        row.choice->Bind(wxEVT_CHOICE, [this, t](wxCommandEvent&) { OnChoice(t); });
        row.edit->Bind(wxEVT_BUTTON, [this, t](wxCommandEvent&) { OpenEditor(t); });
    }

    auto* inner = new wxBoxSizer(wxVERTICAL);
    inner->Add(grid, 1, wxEXPAND | wxALL, 16);
    list->SetSizer(inner);

    status_ = new wxStaticText(root, wxID_ANY,
                               plugins_.empty()
                                   ? "No VST3 plugins found, so every instrument uses the placeholder sound."
                                   : "After choosing a plugin, open it and load an instrument inside it.");
    status_->SetForegroundColour(kMuted);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(list, 1, wxEXPAND);
    sizer->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    root->SetSizer(sizer);

    // Long instrument lists scroll rather than outgrowing the screen.
    const wxSize best = inner->ComputeFittingWindowSize(list);
    SetClientSize(best.x + 24, std::min(best.y, 620) + 44);

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

std::vector<PluginSetup> InstrumentsWindow::GetPluginSetups() const {
    std::vector<PluginSetup> setups;
    for (int t = 0; t < int(rows_.size()); ++t) {
        const Row& row = rows_[t];
        if (!row.plugin) continue;
        PluginSetup setup;
        setup.track = t;
        setup.path = plugins_[row.selection - 1].path;
        row.plugin->GetState(setup.component, setup.controller);
        setups.push_back(std::move(setup));
    }
    return setups;
}

bool InstrumentsWindow::RestorePlugin(const PluginSetup& setup, wxString* error) {
    if (setup.track < 0 || setup.track >= int(rows_.size())) return false;
    const auto found = std::find_if(plugins_.begin(), plugins_.end(),
                                    [&](const PluginInfo& p) { return p.path == setup.path; });
    if (found == plugins_.end()) {
        if (error) *error = "not installed";
        return false;
    }

    auto loaded = Vst3Instrument::Load(*found, engine_->SampleRate(), AudioEngine::kBlock, error);
    if (!loaded) return false;
    loaded->SetState(setup.component, setup.controller);  // before the audio thread sees it

    Row& row = rows_[setup.track];
    row.selection = int(found - plugins_.begin()) + 1;
    row.choice->SetSelection(row.selection);
    row.edit->Enable();
    row.plugin = loaded.get();
    engine_->SetInstrument(setup.track, std::move(loaded));
    return true;
}

void InstrumentsWindow::UsePlaceholder(int track) {
    Row& row = rows_[track];
    row.plugin = nullptr;
    row.selection = 0;
    row.choice->SetSelection(0);
    row.edit->Disable();
    engine_->SetInstrument(track, MakePlaceholder(tracks_[track].timbre, engine_->SampleRate()));
}

void InstrumentsWindow::OnChoice(int track) {
    Row& row = rows_[track];
    const int selection = row.choice->GetSelection();
    if (selection == row.selection) return;

    if (selection == 0) {
        UsePlaceholder(track);
        status_->SetLabel(tracks_[track].name + " now uses the placeholder sound.");
        if (onChanged) onChanged();
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
    if (onChanged) onChanged();
    OpenEditor(track);
}

void InstrumentsWindow::OpenEditor(int track) {
    Row& row = rows_[track];
    if (!row.plugin) return;
    const wxString title = tracks_[track].name + " - " + row.choice->GetStringSelection();
    if (!row.plugin->OpenEditor(GetParent(), title))
        status_->SetLabel("That plugin has no window to open.");
}

void InstrumentsWindow::OnMeterTick(wxTimerEvent&) {
    for (int t = 0; t < int(rows_.size()); ++t) {
        const float peak = engine_->TakePeak(t);
        rows_[t].level->SetValue(std::clamp(int(std::sqrt(peak) * 100), 0, 100));
    }
}
