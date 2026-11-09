// Chooses the sound for each instrument and shows whether it is producing audio.
#pragma once

#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/timer.h>

#include <functional>
#include <vector>

#include "Project.h"
#include "audio/AudioEngine.h"
#include "audio/Vst3Host.h"

class InstrumentsWindow : public wxFrame {
public:
    InstrumentsWindow(wxWindow* parent, AudioEngine* engine, const std::vector<TrackDef>& tracks);

    // Captures each chosen plugin and its current settings for saving.
    std::vector<PluginSetup> GetPluginSetups() const;
    // Returns false, with a reason, if the plugin is missing or won't load.
    bool RestorePlugin(const PluginSetup& setup, wxString* error);

    std::function<void()> onChanged;

private:
    struct Row {
        wxChoice* choice = nullptr;
        wxButton* edit = nullptr;
        wxGauge* level = nullptr;
        Vst3Instrument* plugin = nullptr;  // owned by the engine
        int selection = 0;
    };

    void OnChoice(int track);
    void UsePlaceholder(int track);
    void OpenEditor(int track);
    void OnMeterTick(wxTimerEvent&);

    AudioEngine* engine_;
    std::vector<TrackDef> tracks_;
    std::vector<PluginInfo> plugins_;
    std::vector<Row> rows_;
    wxStaticText* status_ = nullptr;
    wxTimer meter_;
};
