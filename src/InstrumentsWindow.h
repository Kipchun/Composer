// Chooses the sound for each staff and shows whether it is producing audio.
#pragma once

#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/timer.h>

#include <array>
#include <vector>

#include "ScoreCanvas.h"
#include "audio/AudioEngine.h"
#include "audio/Vst3Host.h"

class InstrumentsWindow : public wxFrame {
public:
    static constexpr int kTracks = 6;

    InstrumentsWindow(wxWindow* parent, AudioEngine* engine);

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
    std::vector<PluginInfo> plugins_;
    std::array<Row, kTracks> rows_;
    wxStaticText* status_ = nullptr;
    wxTimer meter_;
};
