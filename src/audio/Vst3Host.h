// Finds and hosts VST3 instruments. SDK details stay inside the .cpp.
#pragma once

#include <wx/wx.h>

#include <memory>
#include <string>
#include <vector>

#include "Instrument.h"

struct PluginInfo {
    wxString name;
    std::string path;
};

// Lists plugins in the standard VST3 folders without loading any of them.
std::vector<PluginInfo> ScanVst3Plugins();

class Vst3Instrument : public Instrument {
public:
    // Must run on the UI thread, as must destruction and OpenEditor.
    static std::unique_ptr<Vst3Instrument> Load(const PluginInfo& plugin, double sampleRate,
                                                int blockSize, wxString* error);
    ~Vst3Instrument() override;

    void Process(const MidiEvent* events, int count, float* left, float* right,
                 int frames) override;
    void SetTempo(double tempo) override;
    bool OpenEditor(wxWindow* parent, const wxString& title);
    // UI thread. These capture and restore the plugin's settings, including its patch.
    void GetState(std::string& component, std::string& controller) const;
    void SetState(const std::string& component, const std::string& controller);

private:
    Vst3Instrument();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
